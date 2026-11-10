#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aced.h"
#include "adscodes.h"
#include "acdocman.h"
#include "actrans.h"
#include "acutads.h"
#include "commands.h"
#include "text_util.h"
#include "../src/js_engine.h"

namespace
{
const int kDefaultIpcPort = 8123;
const int kMaxScriptBytes = 1024 * 1024;

std::thread g_serverThread;
std::thread g_clientThread;
std::atomic<bool> g_running(false);
std::atomic<bool> g_clientBusy(false);
SOCKET g_listenSocket = INVALID_SOCKET;
SOCKET g_activeClientSocket = INVALID_SOCKET;
std::mutex g_socketMutex;

class DocumentLockGuard
{
public:
    explicit DocumentLockGuard(AcApDocument* document)
        : document_(document), locked_(false), status_(Acad::eOk)
    {
        if (document_ == nullptr || acDocManager == nullptr)
        {
            return;
        }

        if ((document_->myLockMode() & AcAp::kWrite) == 0)
        {
            status_ = acDocManager->lockDocument(document_, AcAp::kWrite, nullptr, nullptr, false);
            locked_ = (status_ == Acad::eOk);
        }
    }

    ~DocumentLockGuard()
    {
        if (locked_ && acDocManager != nullptr && document_ != nullptr)
        {
            acDocManager->unlockDocument(document_);
        }
    }

    bool ok() const
    {
        return status_ == Acad::eOk;
    }

    Acad::ErrorStatus status() const
    {
        return status_;
    }

private:
    AcApDocument* document_;
    bool locked_;
    Acad::ErrorStatus status_;
};

struct IpcEvalRequest
{
    explicit IpcEvalRequest(const std::string& scriptText)
        : script(scriptText), done(false)
    {
    }

    std::string script;
    js_engine::EvalResult result;
    std::mutex mutex;
    std::condition_variable condition;
    bool done;
};

void setListenSocket(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_socketMutex);
    g_listenSocket = socket;
}

SOCKET getListenSocket()
{
    std::lock_guard<std::mutex> lock(g_socketMutex);
    return g_listenSocket;
}

void closeSocketHandle(SOCKET socket)
{
    if (socket != INVALID_SOCKET)
    {
        closesocket(socket);
    }
}

void closeListenSocket()
{
    SOCKET socket = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        socket = g_listenSocket;
        g_listenSocket = INVALID_SOCKET;
    }

    closeSocketHandle(socket);
}

void setActiveClientSocket(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_socketMutex);
    g_activeClientSocket = socket;
}

SOCKET clearActiveClientSocket(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_socketMutex);
    if (g_activeClientSocket == socket)
    {
        g_activeClientSocket = INVALID_SOCKET;
        return socket;
    }
    return INVALID_SOCKET;
}

void closeActiveClientSocket()
{
    SOCKET socket = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_socketMutex);
        socket = g_activeClientSocket;
        g_activeClientSocket = INVALID_SOCKET;
    }

    closeSocketHandle(socket);
}

std::string jsonError(const std::string& message)
{
    std::string result = "{\"error\":\"";
    for (unsigned char ch : message)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                char escaped[7] = {};
                std::sprintf(escaped, "\\u%04X", static_cast<unsigned int>(ch));
                result += escaped;
            }
            else
            {
                result += static_cast<char>(ch);
            }
            break;
        }
    }
    result += "\"}";
    return result;
}

const UINT WM_GRX_IPC_EVAL = WM_USER + 0x7123;
HWND g_hBridgeWnd = nullptr;
const wchar_t* const kBridgeClassName = L"GrxScriptIpcBridgeClass";

void executeOnMainThread(IpcEvalRequest* request)
{
    if (request == nullptr)
    {
        return;
    }

    js_engine::EvalResult result;
    try
    {
        AcApDocument* document = acDocManager != nullptr ? acDocManager->curDocument() : nullptr;
        DocumentLockGuard documentLock(document);
        if (!documentLock.ok())
        {
            result = {false, jsonError("failed to lock current document: " + std::to_string(static_cast<int>(documentLock.status())))};
        }
        else
        {
            result = js_engine::evalJson(request->script);
            if (result.ok)
            {
                js_engine::eval("if (globalThis.transaction !== null) { tm.commit(); }");
            }
            else
            {
                js_engine::eval("if (globalThis.transaction !== null) { tm.abort(); }");
            }
        }
    }
    catch (const std::exception& ex)
    {
        result = {false, jsonError(std::string("Exception: ") + ex.what())};
    }
    catch (...)
    {
        result = {false, jsonError("Unknown exception during script evaluation")};
    }

    request->result = result;
    request->done = true;
}

LRESULT CALLBACK ipcBridgeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_GRX_IPC_EVAL)
    {
        IpcEvalRequest* request = reinterpret_cast<IpcEvalRequest*>(wParam);
        executeOnMainThread(request);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ensureBridgeWindowOnMainThread()
{
    if (g_hBridgeWnd != nullptr && IsWindow(g_hBridgeWnd))
    {
        return;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ipcBridgeWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kBridgeClassName;
    RegisterClassExW(&wc);

    g_hBridgeWnd = CreateWindowExW(
        0,
        kBridgeClassName,
        L"GrxScriptIpcBridge",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        hInst,
        nullptr
    );
}

void destroyBridgeWindow()
{
    if (g_hBridgeWnd != nullptr && IsWindow(g_hBridgeWnd))
    {
        DestroyWindow(g_hBridgeWnd);
        g_hBridgeWnd = nullptr;
        UnregisterClassW(kBridgeClassName, GetModuleHandleW(nullptr));
    }
}

std::string evaluateScriptInApplicationContext(const std::string& script)
{
    if (g_hBridgeWnd == nullptr || !IsWindow(g_hBridgeWnd))
    {
        return jsonError("AutoCAD IPC bridge window not ready");
    }

    IpcEvalRequest request(script);

    DWORD_PTR dwResult = 0;
    LRESULT lr = SendMessageTimeoutW(
        g_hBridgeWnd,
        WM_GRX_IPC_EVAL,
        reinterpret_cast<WPARAM>(&request),
        0,
        SMTO_NORMAL,
        20000,
        &dwResult
    );

    if (lr == 0)
    {
        DWORD err = GetLastError();
        return jsonError(err == ERROR_TIMEOUT ? "AutoCAD request timed out (busy)" : "Failed to marshal to AutoCAD main thread");
    }

    return request.result.text;
}

std::string readClientScript(SOCKET client)
{
    DWORD recvTimeoutMs = 15000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs), sizeof(recvTimeoutMs));

    std::string script;
    std::vector<char> buffer(8192);
    const int firstRecv = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (firstRecv <= 0)
    {
        return script;
    }
    script.assign(buffer.data(), buffer.data() + firstRecv);

    // Continue reading any remaining chunks that arrive immediately or until EOF
    while (script.size() < static_cast<size_t>(kMaxScriptBytes))
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(client, &readSet);
        timeval tv = {0, 100000}; // 100ms poll for trailing chunks
        const int ready = select(0, &readSet, nullptr, nullptr, &tv);
        if (ready <= 0)
        {
            break;
        }
        const int n = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (n <= 0)
        {
            break;
        }
        script.append(buffer.data(), buffer.data() + n);
    }

    return script;
}

void sendAll(SOCKET client, const std::string& response)
{
    const char* data = response.data();
    int remaining = static_cast<int>(response.size());
    while (remaining > 0)
    {
        const int sent = send(client, data, remaining, 0);
        if (sent == SOCKET_ERROR || sent == 0)
        {
            return;
        }
        data += sent;
        remaining -= sent;
    }
}

void handleClient(SOCKET client)
{
    setActiveClientSocket(client);

    const std::string script = readClientScript(client);
    if (script.empty())
    {
        sendAll(client, jsonError("empty JavaScript script"));
        closeSocketHandle(clearActiveClientSocket(client));
        return;
    }

    if (script.size() > static_cast<size_t>(kMaxScriptBytes))
    {
        sendAll(client, jsonError("JavaScript script is too large"));
        closeSocketHandle(clearActiveClientSocket(client));
        return;
    }

    sendAll(client, evaluateScriptInApplicationContext(script));
    closeSocketHandle(clearActiveClientSocket(client));
}

void clientThreadMain(SOCKET client)
{
    handleClient(client);
    g_clientBusy = false;
}

void serverThreadMain(int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        g_running = false;
        return;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        WSACleanup();
        g_running = false;
        return;
    }

    BOOL yes = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(port));

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        closeSocketHandle(listenSocket);
        WSACleanup();
        g_running = false;
        return;
    }

    setListenSocket(listenSocket);

    while (g_running)
    {
        SOCKET client = accept(listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            break;
        }

        BOOL nodelay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

        if (g_clientThread.joinable())
        {
            g_clientThread.join();
        }

        g_clientBusy = true;
        try
        {
            g_clientThread = std::thread(clientThreadMain, client);
        }
        catch (...)
        {
            g_clientBusy = false;
            sendAll(client, jsonError("failed to start client thread"));
            closeSocketHandle(client);
        }
    }

    closeListenSocket();
    if (g_clientThread.joinable())
    {
        g_clientThread.join();
    }
    WSACleanup();
    g_clientBusy = false;
    g_running = false;
}

bool startServer(int port)
{
    if (g_running)
    {
        return false;
    }

    if (g_serverThread.joinable())
    {
        g_serverThread.join();
    }

    g_running = true;
    g_serverThread = std::thread(serverThreadMain, port);
    return true;
}

void stopServer()
{
    g_running = false;
    closeListenSocket();
    closeActiveClientSocket();
    if (g_serverThread.joinable())
    {
        g_serverThread.join();
    }
}
}

namespace commands
{
void jsServer()
{
    if (g_running)
    {
        stopServer();
        acutPrintf(ACRX_T("\nJS IPC server stopped."));
        return;
    }

    int port = kDefaultIpcPort;
    int inputPort = 0;
    const int status = acedGetInt(ACRX_T("\nIPC port <8123>: "), &inputPort);
    if (status == RTNORM)
    {
        if (inputPort <= 0 || inputPort > 65535)
        {
            acutPrintf(ACRX_T("\nInvalid TCP port."));
            return;
        }
        port = inputPort;
    }
    else if (status != RTNONE)
    {
        acutPrintf(ACRX_T("\nJS IPC server canceled."));
        return;
    }

    if (!startServer(port))
    {
        acutPrintf(ACRX_T("\nJS IPC server is already running."));
        return;
    }

    acutPrintf(ACRX_T("\nJS IPC server listening on 127.0.0.1:%d. Run js:server again to stop."), port);
}

void initIpcBridge()
{
    ensureBridgeWindowOnMainThread();
}

void shutdownIpcBridge()
{
    destroyBridgeWindow();
}

void stopJsIpcServer()
{
    stopServer();
    destroyBridgeWindow();
}

bool startJsIpcServer(int port)
{
    ensureBridgeWindowOnMainThread();
    return startServer(port);
}

bool isJsIpcServerRunning()
{
    return g_running;
}

void qtoProps()
{
    ensureBridgeWindowOnMainThread();
    if (!g_running)
    {
        startServer(kDefaultIpcPort);
        acutPrintf(ACRX_T("\n[QTO] IPC server active on 127.0.0.1:%d"), kDefaultIpcPort);
    }

    // If QTO Property Box window is already open, restore and focus it
    HWND existingWnd = FindWindowW(nullptr, L"QTO Property Box");
    if (existingWnd != nullptr)
    {
        if (IsIconic(existingWnd))
        {
            ShowWindow(existingWnd, SW_RESTORE);
        }
        else
        {
            ShowWindow(existingWnd, SW_SHOW);
        }
        SetForegroundWindow(existingWnd);
        acutPrintf(ACRX_T("\n[QTO] QTO Property Box focused.\n"));
        return;
    }

    // Locate module directory of loaded .arx / .grx
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&qtoProps),
        &hMod);

    wchar_t modPath[MAX_PATH] = {};
    std::wstring modDir;
    if (hMod != nullptr && GetModuleFileNameW(hMod, modPath, MAX_PATH) > 0)
    {
        std::wstring full(modPath);
        size_t pos = full.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
        {
            modDir = full.substr(0, pos);
        }
    }

    std::vector<std::wstring> candidates;
    if (!modDir.empty())
    {
        candidates.push_back(modDir + L"\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\bin\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\bin\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\..\\bin\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\qto-property-box\\src-tauri\\target\\release\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\qto-property-box\\src-tauri\\target\\debug\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\..\\qto-property-box\\src-tauri\\target\\release\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\..\\qto-property-box\\src-tauri\\target\\debug\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\..\\..\\qto-property-box\\src-tauri\\target\\release\\qto-property-box.exe");
        candidates.push_back(modDir + L"\\..\\..\\..\\..\\qto-property-box\\src-tauri\\target\\debug\\qto-property-box.exe");
    }

    std::wstring foundExe;
    for (const auto& cand : candidates)
    {
        DWORD attr = GetFileAttributesW(cand.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            foundExe = cand;
            break;
        }
    }

    if (foundExe.empty())
    {
        acutPrintf(ACRX_T("\n[QTO] qto-property-box.exe not found. Make sure it is built in qto-property-box/ or dist/bin/.\n"));
        return;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"\"" + foundExe + L"\"";
    if (CreateProcessW(
            foundExe.c_str(),
            &cmdLine[0],
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        acutPrintf(ACRX_T("\n[QTO] Launched QTO Property Box.\n"));
    }
    else
    {
        acutPrintf(ACRX_T("\n[QTO] Failed to launch QTO Property Box (error %lu).\n"), GetLastError());
    }
}
}


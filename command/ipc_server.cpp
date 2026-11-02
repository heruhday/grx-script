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
        if (document_ == nullptr || acDocManager == nullptr || document_->myLockMode() != AcAp::kNone)
        {
            return;
        }

        status_ = acDocManager->lockDocument(document_, AcAp::kWrite);
        locked_ = status_ == Acad::eOk;
    }

    ~DocumentLockGuard()
    {
        if (locked_ && acDocManager != nullptr)
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

void applicationEvalCallback(void* data)
{
    IpcEvalRequest* request = static_cast<IpcEvalRequest*>(data);
    js_engine::EvalResult result;

    AcApDocument* document = acDocManager != nullptr ? acDocManager->curDocument() : nullptr;
    DocumentLockGuard documentLock(document);
    if (!documentLock.ok())
    {
        result = {false, jsonError("failed to lock current document: " + std::to_string(static_cast<int>(documentLock.status())))};
    }
    else
    {
        result = js_engine::evalJson(request->script);
    }

    {
        std::lock_guard<std::mutex> lock(request->mutex);
        request->result = result;
        request->done = true;
    }
    request->condition.notify_one();
}

std::string evaluateScriptInApplicationContext(const std::string& script)
{
    IpcEvalRequest* request = new IpcEvalRequest(script);
    acDocManager->executeInApplicationContext(applicationEvalCallback, request);

    std::unique_lock<std::mutex> lock(request->mutex);
    request->condition.wait(lock, [request]() { return request->done; });
    const std::string response = request->result.text;
    lock.unlock();

    delete request;
    return response;
}

std::string readClientScript(SOCKET client)
{
    std::string script;
    std::vector<char> buffer(4096);
    const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (received > 0)
    {
        script.assign(buffer.data(), buffer.data() + received);
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

        if (g_clientBusy)
        {
            sendAll(client, jsonError("IPC server is busy"));
            closeSocketHandle(client);
            continue;
        }

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

void stopJsIpcServer()
{
    stopServer();
}

bool startJsIpcServer(int port)
{
    return startServer(port);
}

bool isJsIpcServerRunning()
{
    return g_running;
}
}


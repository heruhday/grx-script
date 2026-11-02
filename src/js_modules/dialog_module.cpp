#include "js_modules.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "acedads.h"

namespace js_modules
{
    namespace
    {
        const wchar_t *kDialogWindowClass = L"Test01WebView2Dialog";
        const int kDialogWidth = 720;
        const int kDialogHeight = 760;
        const BYTE kDialogOpacity = 153; // 60% opacity = 40% transparent

        using Microsoft::WRL::Callback;
        using Microsoft::WRL::ComPtr;

        ComPtr<ICoreWebView2Environment> g_webViewEnvironment;
        bool g_dialogOleInitialized = false;

        HRESULT ensureDialogOleInitialized()
        {
            if (g_dialogOleInitialized)
            {
                return S_OK;
            }

            const HRESULT status = OleInitialize(nullptr);
            if (status == S_OK || status == S_FALSE)
            {
                g_dialogOleInitialized = true;
            }
            return status;
        }

        struct DialogState
        {
            HWND window = nullptr;
            HWND owner = nullptr;
            bool complete = false;
            bool ownerWasEnabled = false;
            bool saveStateOnFinish = false;
            bool hasPlacement = false;
            bool hasZoom = false;
            int x = 0;
            int y = 0;
            int width = kDialogWidth;
            int height = kDialogHeight;
            double zoom = 1.0;
            std::string resultJson;
            std::string inputJson;
            std::string error;
            bool closeWithPageData = false;
            bool closeRequestPending = false;
            ComPtr<ICoreWebView2Controller> controller;
            ComPtr<ICoreWebView2> webView;
            EventRegistrationToken messageToken = {};
            EventRegistrationToken navigationToken = {};

            void prepareForOpen(const std::string &json, bool returnPageDataOnClose)
            {
                complete = false;
                saveStateOnFinish = false;
                resultJson.clear();
                inputJson = json;
                error.clear();
                closeWithPageData = returnPageDataOnClose;
                closeRequestPending = false;
            }

            void captureWindowState()
            {
                RECT bounds = {};
                if (window != nullptr && GetWindowRect(window, &bounds))
                {
                    x = bounds.left;
                    y = bounds.top;
                    width = bounds.right - bounds.left;
                    height = bounds.bottom - bounds.top;
                    hasPlacement = width > 0 && height > 0;
                }

                double currentZoom = 1.0;
                if (controller != nullptr && SUCCEEDED(controller->get_ZoomFactor(&currentZoom)) && currentZoom > 0.0)
                {
                    zoom = currentZoom;
                    hasZoom = true;
                }
            }

            void finish(const std::string &json, bool saveState)
            {
                if (complete)
                {
                    return;
                }

                if (saveState)
                {
                    captureWindowState();
                    saveStateOnFinish = true;
                }
                resultJson = json;
                complete = true;
                if (window != nullptr)
                {
                    ShowWindow(window, SW_HIDE);
                }
            }

            void fail(const char *operation, HRESULT status)
            {
                char buffer[160] = {};
                sprintf_s(buffer, "%s failed (HRESULT 0x%08X)", operation, static_cast<unsigned int>(status));
                error = buffer;
                finish(std::string(), false);
            }
        };

        std::map<std::string, std::shared_ptr<DialogState>> g_dialogHosts;

        std::wstring utf8ToWide(const std::string &text)
        {
            if (text.empty())
            {
                return std::wstring();
            }

            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0)
            {
                return std::wstring();
            }

            std::wstring result(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), &result[0], size);
            return result;
        }

        std::string wideToUtf8(const wchar_t *text)
        {
            if (text == nullptr || *text == L'\0')
            {
                return std::string();
            }

            const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (size <= 1)
            {
                return std::string();
            }

            std::string result(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
            result.resize(static_cast<size_t>(size - 1));
            return result;
        }

        bool readIntProperty(JSContext *context, JSValueConst object, const char *name, int &result)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            int32_t converted = 0;
            const bool ok = !JS_IsException(value) && JS_IsNumber(value) && JS_ToInt32(context, &converted, value) == 0;
            JS_FreeValue(context, value);
            if (ok)
            {
                result = static_cast<int>(converted);
            }
            return ok;
        }

        bool readDoubleProperty(JSContext *context, JSValueConst object, const char *name, double &result)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            double converted = 0.0;
            const bool ok = !JS_IsException(value) && JS_IsNumber(value) && JS_ToFloat64(context, &converted, value) == 0;
            JS_FreeValue(context, value);
            if (ok)
            {
                result = converted;
            }
            return ok;
        }

        JSValue getDialogStateMap(JSContext *context)
        {
            JSValue global = JS_GetGlobalObject(context);
            JSValue states = JS_GetPropertyStr(context, global, "dialogState");
            if (!JS_IsObject(states))
            {
                JS_FreeValue(context, states);
                states = JS_NewObject(context);
                JS_SetPropertyStr(context, global, "dialogState", JS_DupValue(context, states));
            }
            JS_FreeValue(context, global);
            return states;
        }

        void loadDialogState(JSContext *context, const std::string &key, DialogState &state)
        {
            JSValue global = JS_GetGlobalObject(context);
            JSValue states = JS_GetPropertyStr(context, global, "dialogState");
            JS_FreeValue(context, global);
            if (!JS_IsObject(states))
            {
                JS_FreeValue(context, states);
                return;
            }

            JSValue saved = JS_GetPropertyStr(context, states, key.c_str());
            if (JS_IsObject(saved))
            {
                JSValue position = JS_GetPropertyStr(context, saved, "position");
                JSValue size = JS_GetPropertyStr(context, saved, "size");
                if (JS_IsObject(position) && JS_IsObject(size))
                {
                    int x = 0;
                    int y = 0;
                    int width = 0;
                    int height = 0;
                    if (readIntProperty(context, position, "x", x) &&
                        readIntProperty(context, position, "y", y) &&
                        readIntProperty(context, size, "width", width) &&
                        readIntProperty(context, size, "height", height) &&
                        width >= 240 && height >= 160)
                    {
                        state.x = x;
                        state.y = y;
                        state.width = width;
                        state.height = height;
                        state.hasPlacement = true;
                    }
                }
                JS_FreeValue(context, position);
                JS_FreeValue(context, size);

                double zoom = 1.0;
                if (readDoubleProperty(context, saved, "zoom", zoom) && zoom >= 0.25 && zoom <= 5.0)
                {
                    state.zoom = zoom;
                    state.hasZoom = true;
                }
            }
            JS_FreeValue(context, saved);
            JS_FreeValue(context, states);
        }

        void saveDialogState(JSContext *context, const std::string &key, const DialogState &state)
        {
            if (!state.saveStateOnFinish)
            {
                return;
            }

            JSValue states = getDialogStateMap(context);
            JSValue saved = JS_NewObject(context);
            JSValue position = JS_NewObject(context);
            JSValue size = JS_NewObject(context);
            JS_SetPropertyStr(context, position, "x", JS_NewInt32(context, state.x));
            JS_SetPropertyStr(context, position, "y", JS_NewInt32(context, state.y));
            JS_SetPropertyStr(context, size, "width", JS_NewInt32(context, state.width));
            JS_SetPropertyStr(context, size, "height", JS_NewInt32(context, state.height));
            JS_SetPropertyStr(context, saved, "position", position);
            JS_SetPropertyStr(context, saved, "size", size);
            JS_SetPropertyStr(context, saved, "zoom", JS_NewFloat64(context, state.zoom));
            JS_SetPropertyStr(context, states, key.c_str(), saved);
            JS_FreeValue(context, states);
        }

        std::wstring fullHtmlPath(const std::wstring &name)
        {
            std::wstring relative = name;
            if (relative.size() < 5 || _wcsicmp(relative.c_str() + relative.size() - 5, L".html") != 0)
            {
                relative += L".html";
            }

            const DWORD required = GetFullPathNameW(relative.c_str(), 0, nullptr, nullptr);
            if (required == 0)
            {
                return std::wstring();
            }

            std::vector<wchar_t> buffer(static_cast<size_t>(required));
            if (GetFullPathNameW(relative.c_str(), required, buffer.data(), nullptr) == 0)
            {
                return std::wstring();
            }
            return std::wstring(buffer.data());
        }

        std::wstring pathToFileUrl(const std::wstring &path)
        {
            DWORD size = 4096;
            std::vector<wchar_t> buffer(static_cast<size_t>(size));
            if (FAILED(UrlCreateFromPathW(path.c_str(), buffer.data(), &size, 0)))
            {
                return std::wstring();
            }
            return std::wstring(buffer.data());
        }

        std::wstring webViewUserDataFolder()
        {
            wchar_t localAppData[MAX_PATH] = {};
            if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData)))
            {
                return std::wstring();
            }

            std::wstring root(localAppData);
            root += L"\\test01";
            CreateDirectoryW(root.c_str(), nullptr);
            root += L"\\WebView2";
            CreateDirectoryW(root.c_str(), nullptr);
            return root;
        }

        void resizeWebView(DialogState *state)
        {
            if (state == nullptr || state->window == nullptr || state->controller == nullptr)
            {
                return;
            }

            RECT bounds = {};
            GetClientRect(state->window, &bounds);
            state->controller->put_Bounds(bounds);
        }

        LRESULT CALLBACK dialogWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            DialogState *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
                state = static_cast<DialogState *>(create->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }

            switch (message)
            {
            case WM_SIZE:
                resizeWebView(state);
                return 0;
            case WM_MOUSEACTIVATE:
                SetActiveWindow(window);
                SetFocus(window);
                if (state != nullptr && state->controller != nullptr)
                {
                    state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                }
                return MA_ACTIVATE;
            case WM_ACTIVATE:
                if (LOWORD(wParam) != WA_INACTIVE && state != nullptr && state->controller != nullptr)
                {
                    state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                }
                return DefWindowProcW(window, message, wParam, lParam);
            case WM_CLOSE:
                if (state != nullptr)
                {
                    if (state->closeWithPageData && state->webView != nullptr && !state->closeRequestPending)
                    {
                        state->closeRequestPending = true;
                        const HRESULT closeStatus = state->webView->ExecuteScript(
                            L"(function(){if(typeof window.__jsonEditorClose==='function'){window.__jsonEditorClose();return true;}return false;})()",
                            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                [state](HRESULT status, LPCWSTR result) -> HRESULT
                                {
                                    if (!state->complete && (FAILED(status) || result == nullptr || wcscmp(result, L"true") != 0))
                                    {
                                        state->finish("null", true);
                                    }
                                    return S_OK;
                                }).Get());
                        if (FAILED(closeStatus))
                        {
                            state->finish("null", true);
                        }
                    }
                    else if (!state->closeRequestPending)
                    {
                        state->finish("null", true);
                    }
                }
                else
                {
                    DestroyWindow(window);
                }
                return 0;
            case WM_NCDESTROY:
                if (state != nullptr)
                {
                    state->window = nullptr;
                }
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                return DefWindowProcW(window, message, wParam, lParam);
            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }

        HINSTANCE moduleInstance()
        {
            HINSTANCE instance = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&dialogWindowProc),
                &instance);
            return instance;
        }

        bool ensureWindowClass(HINSTANCE instance)
        {
            WNDCLASSEXW existing = {};
            existing.cbSize = sizeof(existing);
            if (GetClassInfoExW(instance, kDialogWindowClass, &existing))
            {
                if (existing.lpfnWndProc == dialogWindowProc)
                {
                    return true;
                }
                if (!UnregisterClassW(kDialogWindowClass, instance))
                {
                    return false;
                }
            }

            WNDCLASSEXW windowClass = {};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.hInstance = instance;
            windowClass.lpfnWndProc = dialogWindowProc;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            windowClass.lpszClassName = kDialogWindowClass;
            return RegisterClassExW(&windowClass) != 0;
        }

        HWND createDialogWindow(const std::shared_ptr<DialogState> &state, HINSTANCE instance)
        {
            RECT ownerRect = {};
            if (state->owner == nullptr || !GetWindowRect(state->owner, &ownerRect))
            {
                ownerRect.left = 0;
                ownerRect.top = 0;
                ownerRect.right = GetSystemMetrics(SM_CXSCREEN);
                ownerRect.bottom = GetSystemMetrics(SM_CYSCREEN);
            }

            int width = state->hasPlacement ? state->width : kDialogWidth;
            int height = state->hasPlacement ? state->height : kDialogHeight;
            int x = state->hasPlacement
                        ? state->x
                        : ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
            int y = state->hasPlacement
                        ? state->y
                        : ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

            RECT requested = {x, y, x + width, y + height};
            HMONITOR monitor = MonitorFromRect(&requested, MONITOR_DEFAULTTONULL);
            if (monitor == nullptr)
            {
                monitor = state->owner != nullptr
                              ? MonitorFromWindow(state->owner, MONITOR_DEFAULTTONEAREST)
                              : MonitorFromRect(&ownerRect, MONITOR_DEFAULTTOPRIMARY);
                x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
                y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
            }

            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo))
            {
                const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
                const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
                const int workLeft = static_cast<int>(monitorInfo.rcWork.left);
                const int workTop = static_cast<int>(monitorInfo.rcWork.top);
                const int workRight = static_cast<int>(monitorInfo.rcWork.right);
                const int workBottom = static_cast<int>(monitorInfo.rcWork.bottom);
                width = std::min(width, workWidth);
                height = std::min(height, workHeight);
                x = std::max(workLeft, std::min(x, workRight - width));
                y = std::max(workTop, std::min(y, workBottom - height));
            }
            HWND window = CreateWindowExW(
                WS_EX_DLGMODALFRAME | WS_EX_LAYERED,
                kDialogWindowClass,
                L"Input",
                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                x,
                y,
                width,
                height,
                state->owner,
                nullptr,
                instance,
                state.get());

            if (window != nullptr)
            {
                SetLayeredWindowAttributes(window, 0, kDialogOpacity, LWA_ALPHA);

                const BOOL darkMode = TRUE;
                HRESULT darkStatus = DwmSetWindowAttribute(window, 20, &darkMode, sizeof(darkMode));
                if (FAILED(darkStatus))
                {
                    DwmSetWindowAttribute(window, 19, &darkMode, sizeof(darkMode));
                }

                const COLORREF captionColor = RGB(30, 30, 30);
                const COLORREF textColor = RGB(242, 242, 242);
                const COLORREF borderColor = RGB(60, 60, 60);
                DwmSetWindowAttribute(window, 35, &captionColor, sizeof(captionColor));
                DwmSetWindowAttribute(window, 36, &textColor, sizeof(textColor));
                DwmSetWindowAttribute(window, 34, &borderColor, sizeof(borderColor));
            }

            return window;
        }

        void startWebViewController(
            const std::shared_ptr<DialogState> &state,
            const std::wstring &url,
            ICoreWebView2Environment *environment)
        {
            if (state->complete || state->window == nullptr)
            {
                return;
            }

            const HRESULT controllerStatus = environment->CreateCoreWebView2Controller(
                state->window,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [state, url](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT
                    {
                        if (FAILED(result) || controller == nullptr)
                        {
                            state->fail("creating the WebView2 controller", result);
                            return S_OK;
                        }

                        state->controller = controller;
                        ComPtr<ICoreWebView2Controller2> controller2;
                        if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2))))
                        {
                            COREWEBVIEW2_COLOR background = {255, 30, 30, 30};
                            controller2->put_DefaultBackgroundColor(background);
                        }
                        result = controller->get_CoreWebView2(&state->webView);
                        if (FAILED(result) || state->webView == nullptr)
                        {
                            state->fail("getting the WebView2 instance", result);
                            return S_OK;
                        }

                        resizeWebView(state.get());
                        if (state->hasZoom)
                        {
                            controller->put_ZoomFactor(state->zoom);
                        }
                        controller->put_IsVisible(TRUE);
                        EnableWindow(state->window, TRUE);
                        SetForegroundWindow(state->window);
                        SetActiveWindow(state->window);
                        controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

                        result = state->webView->add_WebMessageReceived(
                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [state](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT
                                {
                                    LPWSTR json = nullptr;
                                    const HRESULT messageStatus = args->get_WebMessageAsJson(&json);
                                    if (FAILED(messageStatus) || json == nullptr)
                                    {
                                        state->fail("reading the WebView2 message", messageStatus);
                                        return S_OK;
                                    }

                                    const std::string utf8 = wideToUtf8(json);
                                    CoTaskMemFree(json);
                                    state->finish(utf8.empty() ? "null" : utf8, true);
                                    return S_OK;
                                }).Get(),
                            &state->messageToken);
                        if (FAILED(result))
                        {
                            state->fail("registering the WebView2 message handler", result);
                            return S_OK;
                        }

                        result = state->webView->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [state](ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT
                                {
                                    BOOL succeeded = FALSE;
                                    if (state->complete || state->inputJson.empty() ||
                                        FAILED(args->get_IsSuccess(&succeeded)) || !succeeded)
                                    {
                                        return S_OK;
                                    }

                                    const std::wstring json = utf8ToWide(state->inputJson);
                                    if (json.empty() || FAILED(sender->PostWebMessageAsJson(json.c_str())))
                                    {
                                        state->fail("sending JSON to the dialog", E_FAIL);
                                    }
                                    return S_OK;
                                }).Get(),
                            &state->navigationToken);
                        if (FAILED(result))
                        {
                            state->fail("registering the WebView2 navigation handler", result);
                            return S_OK;
                        }

                        result = state->webView->Navigate(url.c_str());
                        if (FAILED(result))
                        {
                            state->fail("navigating to the HTML file", result);
                        }
                        return S_OK;
                    }).Get());

            if (FAILED(controllerStatus))
            {
                state->fail("starting the WebView2 controller", controllerStatus);
            }
        }

        void startWebView(const std::shared_ptr<DialogState> &state, const std::wstring &url)
        {
            if (g_webViewEnvironment != nullptr)
            {
                startWebViewController(state, url, g_webViewEnvironment.Get());
                return;
            }

            const std::wstring userData = webViewUserDataFolder();
            const HRESULT status = CreateCoreWebView2EnvironmentWithOptions(
                nullptr,
                userData.empty() ? nullptr : userData.c_str(),
                nullptr,
                Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [state, url](HRESULT environmentStatus, ICoreWebView2Environment *environment) -> HRESULT
                    {
                        if (FAILED(environmentStatus) || environment == nullptr)
                        {
                            state->fail("creating the WebView2 environment", environmentStatus);
                            return S_OK;
                        }

                        g_webViewEnvironment = environment;
                        startWebViewController(state, url, g_webViewEnvironment.Get());
                        return S_OK;
                    }).Get());

            if (FAILED(status))
            {
                state->fail("starting WebView2", status);
            }
        }

        JSValue runDialog(
            JSContext *context,
            const std::string &nameUtf8,
            const std::string &inputJson,
            bool closeWithPageData,
            const wchar_t *windowTitle)
        {
            const std::wstring name = utf8ToWide(nameUtf8);
            if (name.empty())
            {
                return JS_ThrowTypeError(context, "dialog expects a non-empty UTF-8 file name");
            }

            const std::wstring htmlPath = fullHtmlPath(name);
            const DWORD attributes = htmlPath.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(htmlPath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                const std::string displayPath = wideToUtf8(htmlPath.c_str());
                return JS_ThrowReferenceError(context, "dialog HTML file was not found: %s", displayPath.empty() ? nameUtf8.c_str() : displayPath.c_str());
            }

            std::wstring url = pathToFileUrl(htmlPath);
            if (url.empty())
            {
                return JS_ThrowInternalError(context, "could not create a file URL for the dialog HTML");
            }

            WIN32_FILE_ATTRIBUTE_DATA fileData = {};
            if (GetFileAttributesExW(htmlPath.c_str(), GetFileExInfoStandard, &fileData))
            {
                ULARGE_INTEGER modified = {};
                modified.LowPart = fileData.ftLastWriteTime.dwLowDateTime;
                modified.HighPart = fileData.ftLastWriteTime.dwHighDateTime;
                url += L"?v=";
                url += std::to_wstring(modified.QuadPart);
            }

            const HRESULT comStatus = ensureDialogOleInitialized();
            if (FAILED(comStatus))
            {
                return JS_ThrowInternalError(context, "WebView2 requires an STA thread (OleInitialize HRESULT 0x%08X)", static_cast<unsigned int>(comStatus));
            }

            const std::string stateKey = wideToUtf8(htmlPath.c_str());
            std::shared_ptr<DialogState> state;
            const auto existingHost = g_dialogHosts.find(stateKey);
            if (existingHost != g_dialogHosts.end())
            {
                state = existingHost->second;
            }
            else
            {
                state = std::make_shared<DialogState>();
                g_dialogHosts[stateKey] = state;
            }

            state->prepareForOpen(inputJson, closeWithPageData);
            state->owner = adsw_acadMainWnd();
            loadDialogState(context, stateKey, *state);
            const HINSTANCE instance = moduleInstance();
            if (instance == nullptr || !ensureWindowClass(instance))
            {
                return JS_ThrowInternalError(context, "could not register the WebView2 dialog window class");
            }

            if (state->window == nullptr)
            {
                state->window = createDialogWindow(state, instance);
            }
            if (state->window == nullptr)
            {
                return JS_ThrowInternalError(context, "could not create the WebView2 dialog window");
            }
            SetWindowTextW(state->window, windowTitle);

            state->ownerWasEnabled = state->owner != nullptr && IsWindowEnabled(state->owner) != FALSE;
            EnableWindow(state->window, TRUE);
            ShowWindow(state->window, SW_SHOW);
            BringWindowToTop(state->window);
            SetForegroundWindow(state->window);
            SetActiveWindow(state->window);
            SetFocus(state->window);
            if (state->ownerWasEnabled)
            {
                EnableWindow(state->owner, FALSE);
            }
            UpdateWindow(state->window);
            if (state->webView != nullptr && state->controller != nullptr)
            {
                if (state->hasZoom)
                {
                    state->controller->put_ZoomFactor(state->zoom);
                }
                state->controller->put_IsVisible(TRUE);
                const HRESULT navigateStatus = state->webView->Navigate(url.c_str());
                if (FAILED(navigateStatus))
                {
                    state->fail("navigating to the HTML file", navigateStatus);
                }
                else
                {
                    state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                }
            }
            else
            {
                startWebView(state, url);
            }

            MSG message = {};
            while (!state->complete)
            {
                const BOOL messageStatus = GetMessageW(&message, nullptr, 0, 0);
                if (messageStatus <= 0)
                {
                    if (messageStatus == 0)
                    {
                        PostQuitMessage(static_cast<int>(message.wParam));
                    }
                    state->complete = true;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (state->ownerWasEnabled && state->owner != nullptr)
            {
                EnableWindow(state->owner, TRUE);
                SetActiveWindow(state->owner);
            }
            saveDialogState(context, stateKey, *state);
            if (!state->error.empty())
            {
                return JS_ThrowInternalError(context, "%s", state->error.c_str());
            }
            if (state->resultJson.empty())
            {
                return JS_NULL;
            }
            return JS_ParseJSON(context, state->resultJson.c_str(), state->resultJson.size(), "<dialog.postMessage>");
        }

        JSValue jsDialogInput(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
            {
                return JS_ThrowTypeError(context, "dialog.input expects an HTML file name without the .html extension");
            }

            const char *nameChars = JS_ToCString(context, argv[0]);
            if (nameChars == nullptr)
            {
                return JS_EXCEPTION;
            }
            const std::string nameUtf8(nameChars);
            JS_FreeCString(context, nameChars);
            return runDialog(context, nameUtf8, std::string(), false, L"Input");
        }

        JSValue jsDialogInputJson(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || JS_IsUndefined(argv[0]))
            {
                return JS_ThrowTypeError(context, "dialog.input_json expects a JSON-compatible value");
            }

            JSValue serialized = JS_JSONStringify(context, argv[0], JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsException(serialized))
            {
                return JS_EXCEPTION;
            }
            if (JS_IsUndefined(serialized))
            {
                JS_FreeValue(context, serialized);
                return JS_ThrowTypeError(context, "dialog.input_json value is not JSON-compatible");
            }

            const char *jsonChars = JS_ToCString(context, serialized);
            if (jsonChars == nullptr)
            {
                JS_FreeValue(context, serialized);
                return JS_EXCEPTION;
            }
            const std::string inputJson(jsonChars);
            JS_FreeCString(context, jsonChars);
            JS_FreeValue(context, serialized);

            std::string nameUtf8("person");
            if (argc >= 2 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
            {
                const char *nameChars = JS_ToCString(context, argv[1]);
                if (nameChars == nullptr)
                {
                    return JS_EXCEPTION;
                }
                nameUtf8 = nameChars;
                JS_FreeCString(context, nameChars);
            }

            return runDialog(context, nameUtf8, inputJson, true, L"Properties");
        }
    }

    JSValue createDialogModule(JSContext *context)
    {
        JSValue dialog = JS_NewObject(context);
        JSValue states = getDialogStateMap(context);
        JS_SetPropertyStr(context, dialog, "state", states);
        JS_SetPropertyStr(context, dialog, "input", JS_NewCFunction(context, jsDialogInput, "input", 1));
        JS_SetPropertyStr(context, dialog, "input_json", JS_NewCFunction(context, jsDialogInputJson, "input_json", 2));
        return dialog;
    }

    void uninstallDialog()
    {
        for (auto &entry : g_dialogHosts)
        {
            const std::shared_ptr<DialogState> &state = entry.second;
            if (state->webView != nullptr)
            {
                state->webView->remove_WebMessageReceived(state->messageToken);
                state->webView->remove_NavigationCompleted(state->navigationToken);
                state->webView.Reset();
            }
            if (state->controller != nullptr)
            {
                state->controller->Close();
                state->controller.Reset();
            }
            if (state->window != nullptr)
            {
                DestroyWindow(state->window);
            }
        }
        g_dialogHosts.clear();
        g_webViewEnvironment.Reset();
        if (g_dialogOleInitialized)
        {
            OleUninitialize();
            g_dialogOleInitialized = false;
        }

        const HINSTANCE instance = moduleInstance();
        if (instance == nullptr)
        {
            return;
        }

        WNDCLASSEXW existing = {};
        existing.cbSize = sizeof(existing);
        if (GetClassInfoExW(instance, kDialogWindowClass, &existing) && existing.lpfnWndProc == dialogWindowProc)
        {
            UnregisterClassW(kDialogWindowClass, instance);
        }
    }
}

#include "js_modules.h"
#include "js_module_utils.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace js_modules
{
    namespace
    {
        JSClassID g_socketClassId = 0;
        JSClassID g_serverClassId = 0;
        bool g_wsaInitialized = false;

        bool ensureWinsock(std::string &error)
        {
            if (g_wsaInitialized)
            {
                return true;
            }

            WSADATA wsaData;
            const int status = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (status != 0)
            {
                error = "WSAStartup failed";
                return false;
            }

            g_wsaInitialized = true;
            return true;
        }

        void closeSocket(SOCKET socket)
        {
            if (socket != INVALID_SOCKET)
            {
                closesocket(socket);
            }
        }

        struct TcpSocketData
        {
            SOCKET socket = INVALID_SOCKET;
        };

        struct TcpServerData
        {
            SOCKET socket = INVALID_SOCKET;
        };

        TcpSocketData *socketData(JSContext *context, JSValueConst value)
        {
            return static_cast<TcpSocketData *>(JS_GetOpaque2(context, value, g_socketClassId));
        }

        TcpServerData *serverData(JSContext *context, JSValueConst value)
        {
            return static_cast<TcpServerData *>(JS_GetOpaque2(context, value, g_serverClassId));
        }

        void socketFinalizer(JSRuntime *runtime, JSValue value)
        {
            (void)runtime;
            TcpSocketData *data = static_cast<TcpSocketData *>(JS_GetOpaque(value, g_socketClassId));
            if (data != nullptr)
            {
                closeSocket(data->socket);
                delete data;
            }
        }

        void serverFinalizer(JSRuntime *runtime, JSValue value)
        {
            (void)runtime;
            TcpServerData *data = static_cast<TcpServerData *>(JS_GetOpaque(value, g_serverClassId));
            if (data != nullptr)
            {
                closeSocket(data->socket);
                delete data;
            }
        }

        bool parsePortValue(JSContext *context, JSValueConst value, uint16_t &port, std::string &error)
        {
            if (JS_IsNumber(value) || JS_IsString(value))
            {
                const std::string text = valueToString(context, value);
                char *end = nullptr;
                const long result = std::strtol(text.c_str(), &end, 10);
                if (end == nullptr || *end != '\0' || result <= 0 || result > 65535)
                {
                    error = "port must be an integer from 1 to 65535";
                    return false;
                }
                port = static_cast<uint16_t>(result);
                return true;
            }

            error = "port must be a number or string";
            return false;
        }

        bool parseHostValue(JSContext *context, JSValueConst value, std::string &host, std::string &error)
        {
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                if (!JS_IsString(value))
                {
                    error = "host must be a string";
                    return false;
                }
                host = valueToString(context, value);
            }
            return true;
        }

        bool parseOptionsAddress(JSContext *context, JSValueConst options, std::string &host, uint16_t &port, std::string &error, const std::string &defaultHost)
        {
            if (!JS_IsObject(options))
            {
                error = "network options must be an object";
                return false;
            }

            JSValue portValue = JS_GetPropertyStr(context, options, "port");
            if (JS_IsUndefined(portValue) || JS_IsNull(portValue))
            {
                JS_FreeValue(context, portValue);
                error = "network options require port";
                return false;
            }
            if (!parsePortValue(context, portValue, port, error))
            {
                JS_FreeValue(context, portValue);
                return false;
            }
            JS_FreeValue(context, portValue);

            JSValue hostValue = JS_GetPropertyStr(context, options, "hostname");
            if (JS_IsUndefined(hostValue) || JS_IsNull(hostValue))
            {
                JS_FreeValue(context, hostValue);
                hostValue = JS_GetPropertyStr(context, options, "host");
            }

            host = defaultHost;
            if (!JS_IsUndefined(hostValue) && !JS_IsNull(hostValue))
            {
                if (!parseHostValue(context, hostValue, host, error))
                {
                    JS_FreeValue(context, hostValue);
                    return false;
                }
            }
            JS_FreeValue(context, hostValue);
            return true;
        }

        bool resolveAddress(const std::string &host, uint16_t port, struct addrinfo **result, bool passive, std::string &error)
        {
            struct addrinfo hints;
            std::memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            if (passive)
            {
                hints.ai_flags = AI_PASSIVE;
            }

            const std::string portText = std::to_string(port);
            const int status = getaddrinfo(host.empty() ? nullptr : host.c_str(), portText.c_str(), &hints, result);
            if (status != 0)
            {
                error = gai_strerrorA(status);
                return false;
            }
            return true;
        }

        SOCKET createTcpClientSocket(const std::string &host, uint16_t port, std::string &error)
        {
            struct addrinfo *result = nullptr;
            if (!resolveAddress(host, port, &result, false, error))
            {
                return INVALID_SOCKET;
            }

            SOCKET socketHandle = INVALID_SOCKET;
            for (struct addrinfo *ptr = result; ptr != nullptr; ptr = ptr->ai_next)
            {
                SOCKET s = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
                if (s == INVALID_SOCKET)
                {
                    continue;
                }

                if (::connect(s, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0)
                {
                    socketHandle = s;
                    break;
                }

                closesocket(s);
            }

            freeaddrinfo(result);
            if (socketHandle == INVALID_SOCKET)
            {
                error = "failed to connect";
            }
            return socketHandle;
        }

        SOCKET createTcpServerSocket(const std::string &host, uint16_t port, std::string &error)
        {
            struct addrinfo *result = nullptr;
            if (!resolveAddress(host, port, &result, true, error))
            {
                return INVALID_SOCKET;
            }

            SOCKET socketHandle = INVALID_SOCKET;
            for (struct addrinfo *ptr = result; ptr != nullptr; ptr = ptr->ai_next)
            {
                SOCKET s = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
                if (s == INVALID_SOCKET)
                {
                    continue;
                }

                const BOOL yes = TRUE;
                setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));

                if (::bind(s, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) != 0)
                {
                    closesocket(s);
                    continue;
                }

                if (::listen(s, SOMAXCONN) != 0)
                {
                    closesocket(s);
                    continue;
                }

                socketHandle = s;
                break;
            }

            freeaddrinfo(result);
            if (socketHandle == INVALID_SOCKET)
            {
                error = "failed to create listening socket";
            }
            return socketHandle;
        }

        JSValue createSocketObject(JSContext *context, SOCKET socketHandle)
        {
            if (socketHandle == INVALID_SOCKET)
            {
                return JS_ThrowInternalError(context, "invalid socket handle");
            }

            JSValue socketObject = JS_NewObjectClass(context, g_socketClassId);
            if (JS_IsException(socketObject))
            {
                closesocket(socketHandle);
                return socketObject;
            }

            TcpSocketData *data = new TcpSocketData();
            data->socket = socketHandle;
            JS_SetOpaque(socketObject, data);
            JSValue writeFn = JS_NewCFunction(context, [](JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *argv) -> JSValue {
                TcpSocketData *data = socketData(ctx, thisValue);
                if (data == nullptr || data->socket == INVALID_SOCKET)
                {
                    return JS_ThrowInternalError(ctx, "socket is closed");
                }

                if (argc < 1)
                {
                    return JS_ThrowTypeError(ctx, "write expects data");
                }
                /* Extract optional type parameter: 'json', 'text', 'bin' (default 'json') */
                std::string type = "json";
                if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
                {
                    type = valueToString(ctx, argv[1]);
                }

                std::vector<uint8_t> bytes;
                std::string error;

                if (type == "bin")
                {
                    /* Binary: pass Uint8Array/ArrayBuffer/byte array as-is */
                    if (!bytesFromJs(ctx, argv[0], bytes, error))
                    {
                        return JS_ThrowTypeError(ctx, "bin mode expects Uint8Array or byte array, got %s", error.c_str());
                    }
                }
                else if (type == "json")
                {
                    /* JSON: stringify then encode as UTF-8 text */
                    JSValue global = JS_GetGlobalObject(ctx);
                    JSValue json = JS_GetPropertyStr(ctx, global, "JSON");
                    JSValue stringify = JS_GetPropertyStr(ctx, json, "stringify");
                    JSValue strVal = JS_Call(ctx, stringify, json, 1, &argv[0]);
                    if (JS_IsException(strVal))
                    {
                        JS_FreeValue(ctx, strVal);
                        JS_FreeValue(ctx, stringify);
                        JS_FreeValue(ctx, json);
                        JS_FreeValue(ctx, global);
                        return JS_ThrowTypeError(ctx, "JSON.stringify failed");
                    }
                    const std::string s = valueToString(ctx, strVal);
                    bytes.assign(s.begin(), s.end());
                    JS_FreeValue(ctx, strVal);
                    JS_FreeValue(ctx, stringify);
                    JS_FreeValue(ctx, json);
                    JS_FreeValue(ctx, global);
                }
                else if (type == "text")
                {
                    /* Text: convert to string, encode as UTF-8 */
                    const std::string s = valueToString(ctx, argv[0]);
                    bytes.assign(s.begin(), s.end());
                }
                else if (type == "msgpack")
                {
                    /* MessagePack: use msgpack.encode if available */
                    JSValue global = JS_GetGlobalObject(ctx);
                    JSValue msgpack = JS_GetPropertyStr(ctx, global, "msgpack");
                    if (!JS_IsUndefined(msgpack) && !JS_IsNull(msgpack))
                    {
                        JSValue encode = JS_GetPropertyStr(ctx, msgpack, "encode");
                        if (!JS_IsUndefined(encode) && !JS_IsNull(encode))
                        {
                            JSValue encoded = JS_Call(ctx, encode, msgpack, 1, &argv[0]);
                            if (!JS_IsException(encoded))
                            {
                                std::string error_msg;
                                if (bytesFromJs(ctx, encoded, bytes, error_msg))
                                {
                                    // success
                                }
                                else
                                {
                                    JS_FreeValue(ctx, encoded);
                                    JS_FreeValue(ctx, encode);
                                    JS_FreeValue(ctx, msgpack);
                                    JS_FreeValue(ctx, global);
                                    return JS_ThrowTypeError(ctx, "msgpack.encode failed to produce bytes");
                                }
                                JS_FreeValue(ctx, encoded);
                            }
                            else
                            {
                                JS_FreeValue(ctx, encode);
                                JS_FreeValue(ctx, msgpack);
                                JS_FreeValue(ctx, global);
                                return JS_ThrowTypeError(ctx, "msgpack.encode failed");
                            }
                        }
                        else
                        {
                            JS_FreeValue(ctx, encode);
                            JS_FreeValue(ctx, msgpack);
                            JS_FreeValue(ctx, global);
                            return JS_ThrowTypeError(ctx, "msgpack.encode not available");
                        }
                    }
                    else
                    {
                        JS_FreeValue(ctx, msgpack);
                        JS_FreeValue(ctx, global);
                        return JS_ThrowTypeError(ctx, "msgpack module not loaded");
                    }
                    JS_FreeValue(ctx, msgpack);
                    JS_FreeValue(ctx, global);
                }
                else if (bytesFromJs(ctx, argv[0], bytes, error))
                {
                    // fallback: unknown type, try binary
                }
                else
                {
                    /* Final fallback: convert to string */
                    const std::string s = valueToString(ctx, argv[0]);
                    bytes.assign(s.begin(), s.end());
                }

                const int sent = static_cast<int>(::send(data->socket, reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), 0));
                if (sent == SOCKET_ERROR)
                {
                    return JS_ThrowInternalError(ctx, "send failed: %d", WSAGetLastError());
                }
                return JS_NewInt32(ctx, sent);
            }, "write", 1);
            JS_SetPropertyStr(context, socketObject, "write", writeFn);
            JS_SetPropertyStr(context, socketObject, "send", JS_DupValue(context, writeFn));

            JS_SetPropertyStr(context, socketObject, "read", JS_NewCFunction(context, [](JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *argv) -> JSValue {
                TcpSocketData *data = socketData(ctx, thisValue);
                if (data == nullptr || data->socket == INVALID_SOCKET)
                {
                    return JS_ThrowInternalError(ctx, "socket is closed");
                }

                int length = 4096;
                if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
                {
                    int value = 0;
                    if (JS_ToInt32(ctx, &value, argv[0]) < 0 || value <= 0)
                    {
                        return JS_ThrowTypeError(ctx, "read size must be a positive integer");
                    }
                    length = value;
                }

                /* Extract optional encoding parameter: 'json', 'bin', 'text' (default 'json') */
                std::string type = "json";
                if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
                {
                    type = valueToString(ctx, argv[1]);
                }

                std::vector<uint8_t> buffer(length);
                const int received = static_cast<int>(::recv(data->socket, reinterpret_cast<char *>(buffer.data()), length, 0));
                if (received == SOCKET_ERROR)
                {
                    return JS_ThrowInternalError(ctx, "recv failed: %d", WSAGetLastError());
                }

                if (received == 0)
                {
                    return JS_NULL;
                }

                if (type == "bin")
                {
                    /* Binary: return raw Uint8Array */
                    return JS_NewUint8ArrayCopy(ctx, buffer.data(), static_cast<size_t>(received));
                }
                else if (type == "text")
                {
                    /* Text: construct JS string directly from UTF-8 bytes */
                    return JS_NewStringLen(ctx, reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(received));
                }
                else if (type == "json")
                {
                    /* JSON: create JS string from bytes then call JSON.parse(str) */
                    JSValue strVal = JS_NewStringLen(ctx, reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(received));
                    if (JS_IsException(strVal))
                    {
                        return JS_ThrowInternalError(ctx, "failed to create string from bytes");
                    }
                    
                    JSValue global = JS_GetGlobalObject(ctx);
                    JSValue json = JS_GetPropertyStr(ctx, global, "JSON");
                    JSValue parse = JS_GetPropertyStr(ctx, json, "parse");
                    JSValue argvParse[1] = { strVal };
                    JSValue parsed = JS_Call(ctx, parse, json, 1, argvParse);
                    
                    if (JS_IsException(parsed))
                    {
                        /* Log the received string for debugging */
                        JSValue exception = JS_GetException(ctx);
                        const char *exceptionStr = JS_ToCString(ctx, exception);
                        const char *receivedStr = JS_ToCString(ctx, strVal);
                        
                        std::string errorMsg = "JSON parse error: ";
                        if (exceptionStr) errorMsg += exceptionStr;
                        errorMsg += " (received: ";
                        if (receivedStr) 
                        {
                            errorMsg += receivedStr;
                        }
                        errorMsg += ")";
                        
                        JS_FreeCString(ctx, exceptionStr);
                        JS_FreeCString(ctx, receivedStr);
                        JS_FreeValue(ctx, exception);
                        JS_FreeValue(ctx, parse);
                        JS_FreeValue(ctx, json);
                        JS_FreeValue(ctx, global);
                        JS_FreeValue(ctx, strVal);
                        
                        return JS_ThrowInternalError(ctx, "%s", errorMsg.c_str());
                    }
                    
                    JS_FreeValue(ctx, parse);
                    JS_FreeValue(ctx, json);
                    JS_FreeValue(ctx, global);
                    JS_FreeValue(ctx, strVal);
                    return parsed;
                }
                else if (type == "msgpack")
                {
                    /* MessagePack: use msgpack.decode if available */
                    JSValue bufferArray = JS_NewUint8ArrayCopy(ctx, buffer.data(), static_cast<size_t>(received));
                    JSValue global = JS_GetGlobalObject(ctx);
                    JSValue msgpack = JS_GetPropertyStr(ctx, global, "msgpack");
                    if (!JS_IsUndefined(msgpack) && !JS_IsNull(msgpack))
                    {
                        JSValue decode = JS_GetPropertyStr(ctx, msgpack, "decode");
                        if (!JS_IsUndefined(decode) && !JS_IsNull(decode))
                        {
                            JSValue decoded = JS_Call(ctx, decode, msgpack, 1, &bufferArray);
                            JS_FreeValue(ctx, decode);
                            JS_FreeValue(ctx, msgpack);
                            JS_FreeValue(ctx, global);
                            JS_FreeValue(ctx, bufferArray);
                            return decoded;
                        }
                        JS_FreeValue(ctx, decode);
                    }
                    JS_FreeValue(ctx, msgpack);
                    JS_FreeValue(ctx, global);
                    JS_FreeValue(ctx, bufferArray);
                    return JS_ThrowInternalError(ctx, "msgpack.decode not available");
                }

                return JS_ThrowTypeError(ctx, "read encoding must be 'text', 'json', 'bin', or 'msgpack'");
            }, "read", 2));

            JS_SetPropertyStr(context, socketObject, "close", JS_NewCFunction(context, [](JSContext *ctx, JSValueConst thisValue, int, JSValueConst *) -> JSValue {
                TcpSocketData *data = socketData(ctx, thisValue);
                if (data == nullptr)
                {
                    return JS_ThrowInternalError(ctx, "socket is invalid");
                }
                if (data->socket != INVALID_SOCKET)
                {
                    closesocket(data->socket);
                    data->socket = INVALID_SOCKET;
                }
                return JS_UNDEFINED;
            }, "close", 0));
            /* end(): optional data then close (half-close semantics not implemented) */
            JS_SetPropertyStr(context, socketObject, "end", JS_NewCFunction(context, [](JSContext *ctx, JSValueConst thisValue, int argc, JSValueConst *argv) -> JSValue {
                TcpSocketData *data = socketData(ctx, thisValue);
                if (data == nullptr || data->socket == INVALID_SOCKET)
                {
                    return JS_ThrowInternalError(ctx, "socket is closed");
                }
                if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
                {
                    std::vector<uint8_t> bytes;
                    std::string error;
                    if (bytesFromJs(ctx, argv[0], bytes, error))
                    {
                        // ok
                    }
                    else
                    {
                        if (JS_IsObject(argv[0]))
                        {
                            JSValue global = JS_GetGlobalObject(ctx);
                            JSValue json = JS_GetPropertyStr(ctx, global, "JSON");
                            JSValue stringify = JS_GetPropertyStr(ctx, json, "stringify");
                            JSValue strVal = JS_Call(ctx, stringify, json, 1, &argv[0]);
                            if (!JS_IsException(strVal))
                            {
                                const std::string s = valueToString(ctx, strVal);
                                bytes.assign(s.begin(), s.end());
                            }
                            else
                            {
                                const std::string s = valueToString(ctx, argv[0]);
                                bytes.assign(s.begin(), s.end());
                            }
                            JS_FreeValue(ctx, strVal);
                            JS_FreeValue(ctx, stringify);
                            JS_FreeValue(ctx, json);
                            JS_FreeValue(ctx, global);
                        }
                        else
                        {
                            const std::string s = valueToString(ctx, argv[0]);
                            bytes.assign(s.begin(), s.end());
                        }
                    }
                    const int sent = static_cast<int>(::send(data->socket, reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), 0));
                    if (sent == SOCKET_ERROR)
                    {
                        return JS_ThrowInternalError(ctx, "send failed: %d", WSAGetLastError());
                    }
                }
                /* perform full close for simplicity */
                if (data->socket != INVALID_SOCKET)
                {
                    closesocket(data->socket);
                    data->socket = INVALID_SOCKET;
                }
                return JS_UNDEFINED;
            }, "end", 1));

            /* expose remote peer address/port when available */
            struct sockaddr_storage addr;
            int addrlen = sizeof(addr);
            if (getpeername(socketHandle, reinterpret_cast<struct sockaddr *>(&addr), &addrlen) == 0)
            {
                char hostbuf[NI_MAXHOST] = {0};
                char servbuf[NI_MAXSERV] = {0};
                if (getnameinfo(reinterpret_cast<struct sockaddr *>(&addr), addrlen, hostbuf, sizeof(hostbuf), servbuf, sizeof(servbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0)
                {
                    JS_SetPropertyStr(context, socketObject, "remoteAddress", JS_NewString(context, hostbuf));
                    int port = atoi(servbuf);
                    JS_SetPropertyStr(context, socketObject, "remotePort", JS_NewInt32(context, port));
                }
            }

            return socketObject;
        }

        JSValue createServerObject(JSContext *context, SOCKET socketHandle)
        {
            if (socketHandle == INVALID_SOCKET)
            {
                return JS_ThrowInternalError(context, "invalid listen socket");
            }

            JSValue serverObject = JS_NewObjectClass(context, g_serverClassId);
            if (JS_IsException(serverObject))
            {
                closesocket(socketHandle);
                return serverObject;
            }

            TcpServerData *data = new TcpServerData();
            data->socket = socketHandle;
            JS_SetOpaque(serverObject, data);
            JS_SetPropertyStr(context, serverObject, "accept", JS_NewCFunction(context, [](JSContext *ctx, JSValueConst thisValue, int, JSValueConst *) -> JSValue {
                TcpServerData *server = serverData(ctx, thisValue);
                if (server == nullptr || server->socket == INVALID_SOCKET)
                {
                    return JS_ThrowInternalError(ctx, "server is closed");
                }

                SOCKET clientSocket = ::accept(server->socket, nullptr, nullptr);
                if (clientSocket == INVALID_SOCKET)
                {
                    return JS_ThrowInternalError(ctx, "accept failed: %d", WSAGetLastError());
                }
                return createSocketObject(ctx, clientSocket);
            }, "accept", 0));

            JS_SetPropertyStr(context, serverObject, "close", JS_NewCFunction(context, [](JSContext *ctx, JSValueConst thisValue, int, JSValueConst *) -> JSValue {
                TcpServerData *server = serverData(ctx, thisValue);
                if (server == nullptr)
                {
                    return JS_ThrowInternalError(ctx, "server is invalid");
                }
                if (server->socket != INVALID_SOCKET)
                {
                    closesocket(server->socket);
                    server->socket = INVALID_SOCKET;
                }
                return JS_UNDEFINED;
            }, "close", 0));

            return serverObject;
        }

        JSValue jsNetConnect(JSContext *context, JSValueConst, int argc, JSValueConst *argv);
        JSValue jsNetListen(JSContext *context, JSValueConst, int argc, JSValueConst *argv);

        void ensureNetClasses(JSContext *context)
        {
            JSRuntime *runtime = JS_GetRuntime(context);
            if (g_socketClassId == 0)
            {
                JS_NewClassID(runtime, &g_socketClassId);
                JSClassDef classDef = {};
                classDef.class_name = "TcpSocket";
                classDef.finalizer = socketFinalizer;
                JS_NewClass(runtime, g_socketClassId, &classDef);
            }
            if (g_serverClassId == 0)
            {
                JS_NewClassID(runtime, &g_serverClassId);
                JSClassDef classDef = {};
                classDef.class_name = "TcpServer";
                classDef.finalizer = serverFinalizer;
                JS_NewClass(runtime, g_serverClassId, &classDef);
            }
        }

        JSValue createNetModuleObject(JSContext *context)
        {
            ensureNetClasses(context);
            JSValue net = JS_NewObject(context);
            JS_SetPropertyStr(context, net, "connect", JS_NewCFunction(context, jsNetConnect, "connect", 2));
            JS_SetPropertyStr(context, net, "listen", JS_NewCFunction(context, jsNetListen, "listen", 2));
            return net;
        }

        JSValue jsNetConnect(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string error;
            if (!ensureWinsock(error))
            {
                return JS_ThrowInternalError(context, "%s", error.c_str());
            }

            std::string host = "127.0.0.1";
            uint16_t port = 0;
            if (argc == 0)
            {
                return JS_ThrowTypeError(context, "connect expects at least port or options");
            }

            if (argc == 1)
            {
                if (JS_IsObject(argv[0]))
                {
                    if (!parseOptionsAddress(context, argv[0], host, port, error, "127.0.0.1"))
                    {
                        return JS_ThrowTypeError(context, "%s", error.c_str());
                    }
                }
                else
                {
                    if (!parsePortValue(context, argv[0], port, error))
                    {
                        return JS_ThrowTypeError(context, "%s", error.c_str());
                    }
                }
            }
            else if (argc >= 2)
            {
                if (!parseHostValue(context, argv[0], host, error))
                {
                    return JS_ThrowTypeError(context, "%s", error.c_str());
                }
                if (!parsePortValue(context, argv[1], port, error))
                {
                    return JS_ThrowTypeError(context, "%s", error.c_str());
                }
            }

            SOCKET sock = createTcpClientSocket(host, port, error);
            if (sock == INVALID_SOCKET)
            {
                return JS_ThrowInternalError(context, "%s", error.c_str());
            }

            ensureNetClasses(context);
            return createSocketObject(context, sock);
        }

        JSValue jsNetListen(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string error;
            if (!ensureWinsock(error))
            {
                return JS_ThrowInternalError(context, "%s", error.c_str());
            }

            std::string host = "0.0.0.0";
            uint16_t port = 0;
            if (argc == 0)
            {
                return JS_ThrowTypeError(context, "listen expects port or options");
            }

            if (argc == 1)
            {
                if (JS_IsObject(argv[0]))
                {
                    if (!parseOptionsAddress(context, argv[0], host, port, error, "0.0.0.0"))
                    {
                        return JS_ThrowTypeError(context, "%s", error.c_str());
                    }
                }
                else
                {
                    if (!parsePortValue(context, argv[0], port, error))
                    {
                        return JS_ThrowTypeError(context, "%s", error.c_str());
                    }
                }
            }
            else if (argc >= 2)
            {
                if (!parseHostValue(context, argv[0], host, error))
                {
                    return JS_ThrowTypeError(context, "%s", error.c_str());
                }
                if (!parsePortValue(context, argv[1], port, error))
                {
                    return JS_ThrowTypeError(context, "%s", error.c_str());
                }
            }

            SOCKET sock = createTcpServerSocket(host, port, error);
            if (sock == INVALID_SOCKET)
            {
                return JS_ThrowInternalError(context, "%s", error.c_str());
            }

            ensureNetClasses(context);
            return createServerObject(context, sock);
        }

        JSValue createNetModule(JSContext *context)
        {
            ensureNetClasses(context);
            JSValue net = JS_NewObject(context);
            JS_SetPropertyStr(context, net, "connect", JS_NewCFunction(context, jsNetConnect, "connect", 2));
            JS_SetPropertyStr(context, net, "listen", JS_NewCFunction(context, jsNetListen, "listen", 2));
            return net;
        }
    }

    JSValue createNetModule(JSContext *context)
    {
        return createNetModuleObject(context);
    }
}

#include "js_modules.h"

#include <windows.h>
#include <string>
#include "js_module_utils.h"
#include <cstdlib>

namespace js_modules
{
    namespace
    {
        JSValue jsProcessCwd(JSContext *context, JSValueConst, int /*argc*/, JSValueConst * /*argv*/)
        {
            char buffer[MAX_PATH] = {};
            const DWORD len = GetCurrentDirectoryA(MAX_PATH, buffer);
            if (len == 0 || len >= MAX_PATH)
            {
                return JS_NewString(context, ".");
            }
            return JS_NewString(context, buffer);
        }

        JSValue jsProcessChdir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "process.chdir expects a path");
            }
            const std::string path = valueToString(context, argv[0]);
            const BOOL ok = SetCurrentDirectoryA(path.c_str());
            if (!ok)
            {
                const DWORD err = GetLastError();
                return JS_ThrowInternalError(context, "SetCurrentDirectory failed: %u", err);
            }
            return JS_UNDEFINED;
        }

        JSValue jsProcessPid(JSContext *context, JSValueConst, int /*argc*/, JSValueConst * /*argv*/)
        {
            const DWORD pid = GetCurrentProcessId();
            return JS_NewInt32(context, static_cast<int32_t>(pid));
        }

        JSValue jsProcessEnvGet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "env.get expects a name");
            }
            const std::string name = valueToString(context, argv[0]);
            const char *val = std::getenv(name.c_str());
            if (val == nullptr)
            {
                return JS_UNDEFINED;
            }
            return JS_NewString(context, val);
        }

        JSValue jsProcessEnvSet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "env.set expects name and value");
            }
            const std::string name = valueToString(context, argv[0]);
            const std::string value = valueToString(context, argv[1]);
            const BOOL ok = SetEnvironmentVariableA(name.c_str(), value.c_str());
            return JS_NewBool(context, ok != 0);
        }

        JSValue jsProcessEnvProxyGet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "env proxy get expects target and prop");
            }
            JSValueConst target = argv[0];
            JSValueConst prop = argv[1];
            const std::string name = valueToString(context, prop);
            JSAtom atom = JS_NewAtom(context, name.c_str());
            JSValue result = JS_GetProperty(context, target, atom);
            JS_FreeAtom(context, atom);
            if (!JS_IsUndefined(result))
            {
                return result;
            }
            const char *val = std::getenv(name.c_str());
            return val == nullptr ? JS_UNDEFINED : JS_NewString(context, val);
        }
 
        JSValue jsProcessEnvProxySet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 3)
            {
                return JS_ThrowTypeError(context, "env proxy set expects target, prop, and value");
            }
            JSValueConst target = argv[0];
            JSValueConst prop = argv[1];
            JSValueConst value = argv[2];
            const std::string name = valueToString(context, prop);
            const std::string strValue = valueToString(context, value);
            const BOOL ok = SetEnvironmentVariableA(name.c_str(), strValue.c_str());
            return JS_NewBool(context, ok != 0);
        }

        JSValue jsStdoutWrite(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "stdout.write expects data");
            }
            const std::string s = valueToString(context, argv[0]);
            fwrite(s.data(), 1, s.size(), stdout);
            fflush(stdout);
            OutputDebugStringA(s.c_str());
            return JS_NewInt32(context, static_cast<int32_t>(s.size()));
        }

        JSValue jsStderrWrite(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "stderr.write expects data");
            }
            const std::string s = valueToString(context, argv[0]);
            fwrite(s.data(), 1, s.size(), stderr);
            fflush(stderr);
            OutputDebugStringA(s.c_str());
            return JS_NewInt32(context, static_cast<int32_t>(s.size()));
        }

        // High-resolution time
        static LARGE_INTEGER s_perfFreq = {0};

        JSValue jsHrtime(JSContext *context, JSValueConst, int /*argc*/, JSValueConst * /*argv*/)
        {
            if (s_perfFreq.QuadPart == 0)
            {
                QueryPerformanceFrequency(&s_perfFreq);
            }
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const long long ns = (now.QuadPart * 1000000000LL) / s_perfFreq.QuadPart;
            const long long secs = ns / 1000000000LL;
            const long long rem = ns - secs * 1000000000LL;
            JSValue arr = JS_NewArray(context);
            JS_SetPropertyUint32(context, arr, 0, JS_NewInt32(context, static_cast<int32_t>(secs)));
            JS_SetPropertyUint32(context, arr, 1, JS_NewInt32(context, static_cast<int32_t>(rem)));
            return arr;
        }

        JSValue jsHrtimeBigint(JSContext *context, JSValueConst, int /*argc*/, JSValueConst * /*argv*/)
        {
            if (s_perfFreq.QuadPart == 0)
            {
                QueryPerformanceFrequency(&s_perfFreq);
            }
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const long long ns = (now.QuadPart * 1000000000LL) / s_perfFreq.QuadPart;
            return JS_NewBigInt64(context, ns);
        }

        JSValue jsNextTick(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsFunction(context, argv[0]))
            {
                return JS_ThrowTypeError(context, "nextTick expects a function");
            }
            // Synchronously invoke the callback (best-effort nextTick)
            JSValue ret = JS_Call(context, argv[0], JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(ret))
            {
                return ret;
            }
            JS_FreeValue(context, ret);
            return JS_UNDEFINED;
        }

    }

    JSValue createProcessModule(JSContext *context)
    {
        JSValue process = JS_NewObject(context);
        JS_SetPropertyStr(context, process, "cwd", JS_NewCFunction(context, jsProcessCwd, "cwd", 0));
        JS_SetPropertyStr(context, process, "chdir", JS_NewCFunction(context, jsProcessChdir, "chdir", 1));
        JS_SetPropertyStr(context, process, "pid", jsProcessPid(context, JS_UNDEFINED, 0, nullptr));
        JS_SetPropertyStr(context, process, "platform", JS_NewString(context, "win32"));

        // env object with get/set helpers (populated lazily via get())
        JSValue env = JS_NewObject(context);
        JS_SetPropertyStr(context, env, "get", JS_NewCFunction(context, jsProcessEnvGet, "get", 1));
        JS_SetPropertyStr(context, env, "set", JS_NewCFunction(context, jsProcessEnvSet, "set", 2));
        JSValue handler = JS_NewObject(context);
        JS_SetPropertyStr(context, handler, "get", JS_NewCFunction(context, jsProcessEnvProxyGet, "get", 2));
        JS_SetPropertyStr(context, handler, "set", JS_NewCFunction(context, jsProcessEnvProxySet, "set", 3));
        JSValue envProxy = JS_NewProxy(context, env, handler);
        JS_SetPropertyStr(context, process, "env", envProxy);
        JS_FreeValue(context, handler);

        // stdout / stderr
        JSValue stdoutObj = JS_NewObject(context);
        JS_SetPropertyStr(context, stdoutObj, "write", JS_NewCFunction(context, jsStdoutWrite, "write", 1));
        JS_SetPropertyStr(context, process, "stdout", stdoutObj);
        JSValue stderrObj = JS_NewObject(context);
        JS_SetPropertyStr(context, stderrObj, "write", JS_NewCFunction(context, jsStderrWrite, "write", 1));
        JS_SetPropertyStr(context, process, "stderr", stderrObj);

        // timing and nextTick
        JS_SetPropertyStr(context, process, "hrtime", JS_NewCFunction(context, jsHrtime, "hrtime", 0));
        JS_SetPropertyStr(context, process, "hrtimeBigint", JS_NewCFunction(context, jsHrtimeBigint, "hrtimeBigint", 0));
        JS_SetPropertyStr(context, process, "nextTick", JS_NewCFunction(context, jsNextTick, "nextTick", 1));

        return process;
    }
}

#include "js_modules.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>

#include "adscodes.h"
#include "aced.h"
#include "text_util.h"
#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        struct ConsoleReadOptions
        {
            std::string prompt;
            bool allowSpaces = true;
            bool multiline = false;
            bool newline = true;
            std::string type = "string";
        };

        std::string valueToConsoleString(JSContext *context, JSValueConst value)
        {
            return JS_IsString(value) ? valueToString(context, value) : valueToInspectString(context, value);
        }

        JSValue parseInspectFormatOption(JSContext *context, JSValueConst value, InspectFormat &format)
        {
            if (JS_IsUndefined(value) || JS_IsNull(value))
            {
                format = InspectFormat::Pretty;
                return JS_UNDEFINED;
            }

            if (JS_IsBool(value))
            {
                format = JS_ToBool(context, value) != 0 ? InspectFormat::Compact : InspectFormat::Pretty;
                return JS_UNDEFINED;
            }

            if (JS_IsString(value))
            {
                const std::string text = valueToString(context, value);
                if (text == "yaml")
                {
                    format = InspectFormat::Yaml;
                    return JS_UNDEFINED;
                }
                if (text == "compact")
                {
                    format = InspectFormat::Compact;
                    return JS_UNDEFINED;
                }
                if (text == "pretty")
                {
                    format = InspectFormat::Pretty;
                    return JS_UNDEFINED;
                }
                return JS_ThrowTypeError(context, "console.inspect format must be 'pretty', 'compact', or 'yaml'");
            }

            auto readFormatString = [&](const char *name, bool &handled) -> JSValue {
                handled = false;
                JSValue property = JS_GetPropertyStr(context, value, name);
                if (JS_IsException(property))
                {
                    return property;
                }
                if (JS_IsUndefined(property) || JS_IsNull(property))
                {
                    JS_FreeValue(context, property);
                    return JS_UNDEFINED;
                }
                if (!JS_IsString(property))
                {
                    JS_FreeValue(context, property);
                    return JS_ThrowTypeError(context, "console.inspect format option must be a string");
                }
                handled = true;
                const std::string text = valueToString(context, property);
                JS_FreeValue(context, property);
                if (text == "yaml")
                {
                    format = InspectFormat::Yaml;
                    return JS_UNDEFINED;
                }
                if (text == "compact")
                {
                    format = InspectFormat::Compact;
                    return JS_UNDEFINED;
                }
                if (text == "pretty")
                {
                    format = InspectFormat::Pretty;
                    return JS_UNDEFINED;
                }
                return JS_ThrowTypeError(context, "console.inspect format must be 'pretty', 'compact', or 'yaml'");
            };

            if (JS_IsObject(value))
            {
                bool handled = false;
                JSValue error = readFormatString("format", handled);
                if (JS_IsException(error) || handled)
                {
                    return error;
                }

                error = readFormatString("type", handled);
                if (JS_IsException(error) || handled)
                {
                    return error;
                }

                JSValue yamlValue = JS_GetPropertyStr(context, value, "yaml");
                if (JS_IsException(yamlValue))
                {
                    return yamlValue;
                }
                if (!JS_IsUndefined(yamlValue) && !JS_IsNull(yamlValue) && JS_ToBool(context, yamlValue) != 0)
                {
                    format = InspectFormat::Yaml;
                    JS_FreeValue(context, yamlValue);
                    return JS_UNDEFINED;
                }
                JS_FreeValue(context, yamlValue);

                JSValue compactValue = JS_GetPropertyStr(context, value, "compact");
                if (JS_IsException(compactValue))
                {
                    return compactValue;
                }
                if (!JS_IsUndefined(compactValue) && !JS_IsNull(compactValue))
                {
                    format = JS_ToBool(context, compactValue) != 0 ? InspectFormat::Compact : InspectFormat::Pretty;
                }
                JS_FreeValue(context, compactValue);
                return JS_UNDEFINED;
            }

            return JS_ThrowTypeError(context, "console.inspect options must be a boolean, string, or object");
        }

        JSValue getObjectProperty(JSContext *context, JSValueConst object, const char *name, JSValue &property)
        {
            property = JS_GetPropertyStr(context, object, name);
            if (JS_IsException(property))
            {
                return property;
            }
            return JS_UNDEFINED;
        }

        JSValue parseSpaceOption(JSContext *context, JSValueConst value, bool &allowSpaces)
        {
            const std::string space = valueToString(context, value);
            if (space == "allow")
            {
                allowSpaces = true;
                return JS_UNDEFINED;
            }

            if (space == "deny")
            {
                allowSpaces = false;
                return JS_UNDEFINED;
            }

            return JS_ThrowTypeError(context, "console.read space must be 'allow' or 'deny'");
        }

        JSValue parseReadOptions(JSContext *context, int argc, JSValueConst *argv, ConsoleReadOptions &options)
        {
            if (argc < 1 || JS_IsUndefined(argv[0]))
            {
                return JS_ThrowTypeError(context, "console.read expects a prompt or options object");
            }

            if (JS_IsObject(argv[0]) && !JS_IsString(argv[0]))
            {
                options.multiline = true;

                JSValue property = JS_UNDEFINED;
                JSValue error = getObjectProperty(context, argv[0], "prompt", property);
                if (JS_IsException(error))
                {
                    return error;
                }

                if (JS_IsUndefined(property))
                {
                    JS_FreeValue(context, property);
                    return JS_ThrowTypeError(context, "console.read options.prompt is required");
                }
                options.prompt = valueToString(context, property);
                JS_FreeValue(context, property);

                error = getObjectProperty(context, argv[0], "space", property);
                if (JS_IsException(error))
                {
                    return error;
                }
                if (!JS_IsUndefined(property))
                {
                    error = parseSpaceOption(context, property, options.allowSpaces);
                    JS_FreeValue(context, property);
                    if (JS_IsException(error))
                    {
                        return error;
                    }
                }
                else
                {
                    JS_FreeValue(context, property);
                }

                error = getObjectProperty(context, argv[0], "type", property);
                if (JS_IsException(error))
                {
                    return error;
                }
                if (!JS_IsUndefined(property))
                {
                    options.type = valueToString(context, property);
                }
                JS_FreeValue(context, property);

                if (options.type != "string" && options.type != "value" && options.type != "bin")
                {
                    return JS_ThrowTypeError(context, "console.read type must be 'string', 'value', or 'bin'");
                }

                if (options.type == "value" || options.type == "bin")
                {
                    options.multiline = false;
                }

                error = getObjectProperty(context, argv[0], "multiline", property);
                if (JS_IsException(error))
                {
                    return error;
                }
                if (!JS_IsUndefined(property))
                {
                    options.multiline = JS_ToBool(context, property) != 0;
                }
                JS_FreeValue(context, property);

                error = getObjectProperty(context, argv[0], "newline", property);
                if (JS_IsException(error))
                {
                    return error;
                }
                if (!JS_IsUndefined(property))
                {
                    options.newline = JS_ToBool(context, property) != 0;
                }
                JS_FreeValue(context, property);
            }
            else
            {
                options.prompt = valueToString(context, argv[0]);

                if (argc >= 2 && !JS_IsUndefined(argv[1]))
                {
                    JSValue error = parseSpaceOption(context, argv[1], options.allowSpaces);
                    if (JS_IsException(error))
                    {
                        return error;
                    }
                }

                if (argc >= 3 && !JS_IsUndefined(argv[2]))
                {
                    options.multiline = JS_ToBool(context, argv[2]) != 0;
                }

                if (argc >= 4 && !JS_IsUndefined(argv[3]))
                {
                    options.newline = JS_ToBool(context, argv[3]) != 0;
                }

                if (argc >= 5 && !JS_IsUndefined(argv[4]))
                {
                    options.type = valueToString(context, argv[4]);
                }
            }

            if (options.type != "string" && options.type != "value" && options.type != "bin")
            {
                return JS_ThrowTypeError(context, "console.read type must be 'string', 'value', or 'bin'");
            }

            return JS_UNDEFINED;
        }

        JSValue jsConsoleLog(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string output;
            for (int i = 0; i < argc; ++i)
            {
                if (i > 0)
                {
                    output += " ";
                }
                output += valueToConsoleString(context, argv[i]);
            }

            const std::basic_string<ACHAR> text = command_text::fromUtf8(output);
            acutPrintf(ACRX_T("\n%s"), text.c_str());
            return JS_UNDEFINED;
        }

        JSValue jsConsoleInspect(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            InspectFormat format = InspectFormat::Pretty;
            if (argc >= 2)
            {
                JSValue error = parseInspectFormatOption(context, argv[1], format);
                if (JS_IsException(error))
                {
                    return error;
                }
            }

            const std::string output = valueToInspectString(context, argc >= 1 ? argv[0] : JS_UNDEFINED, format);
            const std::basic_string<ACHAR> text = command_text::fromUtf8(output);
            acutPrintf(ACRX_T("\n%s"), text.c_str());
            return JS_UNDEFINED;
        }

        JSValue readConsoleLine(JSContext *context, const std::basic_string<ACHAR> &prompt, bool allowSpaces, std::string &line, bool &canceled)
        {
            canceled = false;
            ACHAR buffer[2048] = {};
            const int status = acedGetString(allowSpaces ? 1 : 0, prompt.c_str(), buffer, sizeof(buffer) / sizeof(buffer[0]));
            if (status == RTNORM)
            {
                line = command_text::toUtf8(buffer);
                return JS_UNDEFINED;
            }

            if (status == RTNONE)
            {
                line.clear();
                return JS_UNDEFINED;
            }

            if (status == RTCAN)
            {
                canceled = true;
                line.clear();
                return JS_UNDEFINED;
            }

            return JS_ThrowInternalError(context, "console.read failed with status %d", status);
        }

        std::string trimAsciiWhitespace(const std::string &value)
        {
            size_t start = 0;
            while (start < value.size() && static_cast<unsigned char>(value[start]) <= ' ')
            {
                ++start;
            }

            size_t end = value.size();
            while (end > start && static_cast<unsigned char>(value[end - 1]) <= ' ')
            {
                --end;
            }

            return value.substr(start, end - start);
        }

        std::string sourceToEvalExpression(const std::string &source)
        {
            const std::string trimmed = trimAsciiWhitespace(source);
            if (trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}')
            {
                return "(" + source + ")";
            }

            return source;
        }

        JSValue returnConsoleReadResult(JSContext *context, const std::string &source, const std::string &type)
        {
            if (type == "string")
            {
                return JS_NewString(context, source.c_str());
            }

            const std::string evalSource = sourceToEvalExpression(source);
            JSValue value = JS_Eval(context, evalSource.c_str(), evalSource.size(), "<console.read>", JS_EVAL_TYPE_GLOBAL);
            JSRuntime *runtime = JS_GetRuntime(context);
            JSContext *pendingContext = nullptr;
            while (JS_IsJobPending(runtime))
            {
                if (JS_ExecutePendingJob(runtime, &pendingContext) < 0)
                {
                    break;
                }
            }

            if (JS_IsException(value))
            {
                return value;
            }

            if (type == "value")
            {
                return value;
            }

            JSValue bytes = encodeMsgpackValue(context, value);
            JS_FreeValue(context, value);
            return bytes;
        }

        JSValue jsConsoleRead(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            ConsoleReadOptions options;
            JSValue error = parseReadOptions(context, argc, argv, options);
            if (JS_IsException(error))
            {
                return error;
            }

            const std::basic_string<ACHAR> prompt = command_text::fromUtf8(options.prompt);
            std::string line;
            bool canceled = false;
            error = readConsoleLine(context, prompt, options.allowSpaces, line, canceled);
            if (JS_IsException(error))
            {
                return error;
            }

            if (canceled)
            {
                return JS_ThrowInternalError(context, "console.read canceled");
            }

            if (!options.multiline)
            {
                return returnConsoleReadResult(context, line, options.type);
            }

            std::string output = line;
            for (;;)
            {
                error = readConsoleLine(context, ACRX_T("\n... "), options.allowSpaces, line, canceled);
                if (JS_IsException(error))
                {
                    return error;
                }

                if (canceled)
                {
                    break;
                }

                if (options.newline)
                {
                    output += "\n";
                }
                output += line;
            }

            return returnConsoleReadResult(context, output, options.type);
        }
    }

    void installConsole(JSContext *context)
    {
        JSValue global = JS_GetGlobalObject(context);
        JSValue console = JS_NewObject(context);
        JS_SetPropertyStr(context, console, "log", JS_NewCFunction(context, jsConsoleLog, "log", 1));
        JS_SetPropertyStr(context, console, "inspect", JS_NewCFunction(context, jsConsoleInspect, "inspect", 2));
        JS_SetPropertyStr(context, console, "read", JS_NewCFunction(context, jsConsoleRead, "read", 1));
        JS_SetPropertyStr(context, global, "console", console);
        JS_FreeValue(context, global);
    }
}

#include "js_module_utils.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace js_modules
{
    namespace
    {
        const int kMaxInspectDepth = 4;
        const size_t kMaxInlineInspectLength = 80;

        std::string indent(int depth)
        {
            return std::string(static_cast<size_t>(depth * 2), ' ');
        }

        bool isIdentifierKey(const std::string &text)
        {
            if (text.empty())
            {
                return false;
            }

            const unsigned char first = static_cast<unsigned char>(text[0]);
            if (!(std::isalpha(first) || text[0] == '_' || text[0] == '$'))
            {
                return false;
            }

            for (size_t i = 1; i < text.size(); ++i)
            {
                const unsigned char ch = static_cast<unsigned char>(text[i]);
                if (!(std::isalnum(ch) || text[i] == '_' || text[i] == '$'))
                {
                    return false;
                }
            }

            return true;
        }

        std::string quoteString(const std::string &text)
        {
            std::string result("'");
            for (unsigned char ch : text)
            {
                switch (ch)
                {
                case '\\':
                    result += "\\\\";
                    break;
                case '\'':
                    result += "\\'";
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
                case '\b':
                    result += "\\b";
                    break;
                case '\f':
                    result += "\\f";
                    break;
                case '\v':
                    result += "\\v";
                    break;
                case '\0':
                    result += "\\0";
                    break;
                default:
                    if (ch < 0x20 || ch == 0x7f)
                    {
                        char escaped[5] = {};
                        std::sprintf(escaped, "\\x%02X", static_cast<unsigned int>(ch));
                        result += escaped;
                    }
                    else
                    {
                        result += static_cast<char>(ch);
                    }
                    break;
                }
            }
            result += "'";
            return result;
        }

        std::string formatKey(const std::string &key)
        {
            return isIdentifierKey(key) ? key : quoteString(key);
        }

        bool containsNewline(const std::string &text)
        {
            return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
        }

        bool isYamlPlainText(const std::string &text)
        {
            if (text.empty())
            {
                return false;
            }

            const unsigned char first = static_cast<unsigned char>(text[0]);
            if (std::isspace(first) || text[0] == '-' || text[0] == '?' || text[0] == ':' ||
                text[0] == '[' || text[0] == ']' || text[0] == '{' || text[0] == '}' ||
                text[0] == '&' || text[0] == '*' || text[0] == '#' || text[0] == '!' ||
                text[0] == '|' || text[0] == '>' || text[0] == '@' || text[0] == '`' ||
                text[0] == '"' || text[0] == '\'')
            {
                return false;
            }

            for (unsigned char ch : text)
            {
                if (ch < 0x20 || ch == 0x7f || ch == '\n' || ch == '\r' || ch == '\t' ||
                    ch == ':' || ch == ',' || ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
                    ch == '#')
                {
                    return false;
                }
            }

            const std::string lower = [&]() {
                std::string copy(text);
                for (char &ch : copy)
                {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                return copy;
            }();

            return lower != "true" && lower != "false" && lower != "null" &&
                   lower != "undefined" && lower != "~";
        }

        std::string quoteYamlString(const std::string &text)
        {
            std::string result("'");
            for (char ch : text)
            {
                if (ch == '\'')
                {
                    result += "''";
                }
                else if (ch == '\n')
                {
                    result += "\\n";
                }
                else if (ch == '\r')
                {
                    result += "\\r";
                }
                else if (ch == '\t')
                {
                    result += "\\t";
                }
                else
                {
                    result += ch;
                }
            }
            result += "'";
            return result;
        }

        std::string yamlScalarString(const std::string &text)
        {
            return isYamlPlainText(text) ? text : quoteYamlString(text);
        }

        std::string yamlKey(const std::string &key)
        {
            return isYamlPlainText(key) ? key : quoteYamlString(key);
        }

        std::string inspectValue(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen, bool compactMode);

        std::string inspectArray(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen, bool compactMode)
        {
            const uint32_t length = arrayLength(context, value);
            if (length == 0)
            {
                return "[]";
            }

            if (compactMode)
            {
                std::string result("[");
                for (uint32_t i = 0; i < length; ++i)
                {
                    JSValue item = JS_GetPropertyUint32(context, value, i);
                    result += inspectValue(context, item, depth + 1, seen, compactMode);
                    JS_FreeValue(context, item);
                    if (i + 1 < length)
                    {
                        result += ",";
                    }
                }
                result += "]";
                return result;
            }

            std::string compact("[ ");
            bool canUseCompact = true;
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                const std::string itemText = inspectValue(context, item, depth + 1, seen, compactMode);
                JS_FreeValue(context, item);

                if (containsNewline(itemText))
                {
                    canUseCompact = false;
                    break;
                }

                compact += itemText;
                if (i + 1 < length)
                {
                    compact += ", ";
                }
            }
            compact += " ]";

            if (canUseCompact && compact.size() <= kMaxInlineInspectLength)
            {
                return compact;
            }

            std::string result("[\n");
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                result += indent(depth + 1);
                result += inspectValue(context, item, depth + 1, seen, compactMode);
                JS_FreeValue(context, item);

                if (i + 1 < length)
                {
                    result += ",";
                }
                result += "\n";
            }
            result += indent(depth);
            result += "]";
            return result;
        }

        std::string inspectObject(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen, bool compactMode)
        {
            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, value, flags) < 0)
            {
                return valueToString(context, value);
            }

            if (propertyCount == 0)
            {
                JS_FreePropertyEnum(context, properties, propertyCount);
                return "{}";
            }

            if (compactMode)
            {
                std::string result("{");
                for (uint32_t i = 0; i < propertyCount; ++i)
                {
                    JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                    const std::string key = valueToString(context, keyValue);
                    JS_FreeValue(context, keyValue);

                    JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
                    result += formatKey(key);
                    result += ":";
                    result += inspectValue(context, propertyValue, depth + 1, seen, compactMode);
                    JS_FreeValue(context, propertyValue);
                    if (i + 1 < propertyCount)
                    {
                        result += ",";
                    }
                }
                JS_FreePropertyEnum(context, properties, propertyCount);
                result += "}";
                return result;
            }

            std::string compact("{ ");
            bool canUseCompact = true;
            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                const std::string key = valueToString(context, keyValue);
                JS_FreeValue(context, keyValue);

                JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
                const std::string propertyText = inspectValue(context, propertyValue, depth + 1, seen, compactMode);
                JS_FreeValue(context, propertyValue);

                if (containsNewline(propertyText))
                {
                    canUseCompact = false;
                    break;
                }

                compact += formatKey(key);
                compact += ": ";
                compact += propertyText;
                if (i + 1 < propertyCount)
                {
                    compact += ", ";
                }
            }
            compact += " }";

            if (canUseCompact && compact.size() <= kMaxInlineInspectLength)
            {
                JS_FreePropertyEnum(context, properties, propertyCount);
                return compact;
            }

            std::string result("{\n");
            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                const std::string key = valueToString(context, keyValue);
                JS_FreeValue(context, keyValue);

                JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
                result += indent(depth + 1);
                result += formatKey(key);
                result += ": ";
                result += inspectValue(context, propertyValue, depth + 1, seen, compactMode);
                JS_FreeValue(context, propertyValue);

                if (i + 1 < propertyCount)
                {
                    result += ",";
                }
                result += "\n";
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            result += indent(depth);
            result += "}";
            return result;
        }

        std::string inspectValue(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen, bool compactMode)
        {
            if (JS_IsString(value))
            {
                return quoteString(valueToString(context, value));
            }

            if (!JS_IsObject(value) || JS_IsFunction(context, value))
            {
                return valueToString(context, value);
            }

            if (depth >= kMaxInspectDepth)
            {
                return JS_IsArray(value) ? "[Array]" : "[Object]";
            }

            void *ptr = JS_VALUE_GET_PTR(value);
            for (void *seenPtr : seen)
            {
                if (seenPtr == ptr)
                {
                    return "[Circular]";
                }
            }

            seen.push_back(ptr);
            const std::string result = JS_IsArray(value)
                                           ? inspectArray(context, value, depth, seen, compactMode)
                                           : inspectObject(context, value, depth, seen, compactMode);
            seen.pop_back();
            return result;
        }

        std::string yamlValue(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen);

        bool yamlUsesBlock(JSContext *context, JSValueConst value)
        {
            if (!JS_IsObject(value) || JS_IsFunction(context, value))
            {
                return false;
            }

            if (JS_IsArray(value))
            {
                return arrayLength(context, value) > 0;
            }

            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, value, flags) < 0)
            {
                return false;
            }
            JS_FreePropertyEnum(context, properties, propertyCount);
            return propertyCount > 0;
        }

        std::string yamlArray(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen)
        {
            const uint32_t length = arrayLength(context, value);
            if (length == 0)
            {
                return "[]";
            }

            std::string result;
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                result += indent(depth);
                if (yamlUsesBlock(context, item))
                {
                    result += "-\n";
                    result += yamlValue(context, item, depth + 1, seen);
                }
                else
                {
                    result += "- ";
                    result += yamlValue(context, item, depth + 1, seen);
                }
                JS_FreeValue(context, item);
                if (i + 1 < length)
                {
                    result += "\n";
                }
            }
            return result;
        }

        std::string yamlObject(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen)
        {
            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, value, flags) < 0)
            {
                return yamlScalarString(valueToString(context, value));
            }

            if (propertyCount == 0)
            {
                JS_FreePropertyEnum(context, properties, propertyCount);
                return "{}";
            }

            std::string result;
            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                const std::string key = valueToString(context, keyValue);
                JS_FreeValue(context, keyValue);

                JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
                result += indent(depth);
                result += yamlKey(key);
                if (yamlUsesBlock(context, propertyValue))
                {
                    result += ":\n";
                    result += yamlValue(context, propertyValue, depth + 1, seen);
                }
                else
                {
                    result += ": ";
                    result += yamlValue(context, propertyValue, depth + 1, seen);
                }
                JS_FreeValue(context, propertyValue);

                if (i + 1 < propertyCount)
                {
                    result += "\n";
                }
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            return result;
        }

        std::string yamlValue(JSContext *context, JSValueConst value, int depth, std::vector<void *> &seen)
        {
            if (JS_IsString(value))
            {
                return yamlScalarString(valueToString(context, value));
            }

            if (!JS_IsObject(value) || JS_IsFunction(context, value))
            {
                return valueToString(context, value);
            }

            if (depth >= kMaxInspectDepth)
            {
                return JS_IsArray(value) ? "[Array]" : "[Object]";
            }

            void *ptr = JS_VALUE_GET_PTR(value);
            for (void *seenPtr : seen)
            {
                if (seenPtr == ptr)
                {
                    return "[Circular]";
                }
            }

            seen.push_back(ptr);
            const std::string result = JS_IsArray(value)
                                           ? yamlArray(context, value, depth, seen)
                                           : yamlObject(context, value, depth, seen);
            seen.pop_back();
            return result;
        }

        bool isMergeableObject(JSContext *context, JSValueConst value)
        {
            return JS_IsObject(value) && !JS_IsArray(value) && !JS_IsFunction(context, value);
        }

        JSValue deepCopyValueInternal(JSContext *context, JSValueConst value, int depth);

        JSValue deepCopyObjectProperties(JSContext *context, JSValueConst source, JSValue destination, int depth)
        {
            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, source, flags) < 0)
            {
                return JS_EXCEPTION;
            }

            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSValue sourceValue = JS_GetProperty(context, source, properties[i].atom);
                if (JS_IsException(sourceValue))
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return sourceValue;
                }

                JSValue copiedValue = deepCopyValueInternal(context, sourceValue, depth + 1);
                JS_FreeValue(context, sourceValue);
                if (JS_IsException(copiedValue))
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return copiedValue;
                }

                if (JS_SetProperty(context, destination, properties[i].atom, copiedValue) < 0)
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return JS_EXCEPTION;
                }
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            return JS_UNDEFINED;
        }

        JSValue deepCopyValueInternal(JSContext *context, JSValueConst value, int depth)
        {
            if (depth > 64)
            {
                return JS_ThrowInternalError(context, "object nesting is too deep");
            }

            if (JS_IsArray(value))
            {
                JSValue copy = JS_NewArray(context);
                JSValue error = deepCopyObjectProperties(context, value, copy, depth);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, copy);
                    return error;
                }
                return copy;
            }

            if (isMergeableObject(context, value))
            {
                JSValue copy = JS_NewObject(context);
                JSValue error = deepCopyObjectProperties(context, value, copy, depth);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, copy);
                    return error;
                }
                return copy;
            }

            return JS_DupValue(context, value);
        }

        JSValue deepMergeObjectsInternal(JSContext *context, JSValueConst source, JSValueConst destination, int depth)
        {
            if (depth > 64)
            {
                return JS_ThrowInternalError(context, "object nesting is too deep");
            }

            if (!isMergeableObject(context, source) || !isMergeableObject(context, destination))
            {
                return JS_ThrowTypeError(context, "deepMergeObjects expects source and destination objects");
            }

            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, source, flags) < 0)
            {
                return JS_EXCEPTION;
            }

            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSAtom atom = properties[i].atom;
                JSValue sourceValue = JS_GetProperty(context, source, atom);
                if (JS_IsException(sourceValue))
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return sourceValue;
                }

                JSValue destinationValue = JS_GetProperty(context, destination, atom);
                if (JS_IsException(destinationValue))
                {
                    JS_FreeValue(context, sourceValue);
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return destinationValue;
                }

                if (JS_IsNull(sourceValue))
                {
                    if (!JS_IsNull(destinationValue) && !JS_IsUndefined(destinationValue))
                    {
                        JS_DeleteProperty(context, destination, atom, 0);
                    }
                }
                else if (JS_IsNull(destinationValue) || JS_IsUndefined(destinationValue))
                {
                    JSValue copiedValue = deepCopyValueInternal(context, sourceValue, depth + 1);
                    if (JS_IsException(copiedValue) || JS_SetProperty(context, destination, atom, copiedValue) < 0)
                    {
                        JS_FreeValue(context, sourceValue);
                        JS_FreeValue(context, destinationValue);
                        JS_FreePropertyEnum(context, properties, propertyCount);
                        return JS_EXCEPTION;
                    }
                }
                else if (isMergeableObject(context, sourceValue) && isMergeableObject(context, destinationValue))
                {
                    JSValue error = deepMergeObjectsInternal(context, sourceValue, destinationValue, depth + 1);
                    if (JS_IsException(error))
                    {
                        JS_FreeValue(context, sourceValue);
                        JS_FreeValue(context, destinationValue);
                        JS_FreePropertyEnum(context, properties, propertyCount);
                        return error;
                    }
                }
                else
                {
                    JSValue copiedValue = deepCopyValueInternal(context, sourceValue, depth + 1);
                    if (JS_IsException(copiedValue) || JS_SetProperty(context, destination, atom, copiedValue) < 0)
                    {
                        JS_FreeValue(context, sourceValue);
                        JS_FreeValue(context, destinationValue);
                        JS_FreePropertyEnum(context, properties, propertyCount);
                        return JS_EXCEPTION;
                    }
                }

                JS_FreeValue(context, sourceValue);
                JS_FreeValue(context, destinationValue);
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            return JS_UNDEFINED;
        }

        JSValue _jsClone(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "deepCopy expects a value");
            }

            return deepCopyValue(context, argv[0]);
        }

        JSValue _jsMerge(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "deepMerge expects source and destination objects");
            }

            return deepMergeObjects(context, argv[0], argv[1]);
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
                return JS_ThrowTypeError(context, "inspect format must be 'pretty', 'compact', or 'yaml'");
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
                    return JS_ThrowTypeError(context, "inspect format option must be a string");
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
                return JS_ThrowTypeError(context, "inspect format must be 'pretty', 'compact', or 'yaml'");
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

            return JS_ThrowTypeError(context, "inspect option must be a boolean, string, or object");
        }

        JSValue _jsInspect(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
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
            return JS_NewStringLen(context, output.data(), output.size());
        }

        JSValue _jsRound(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "round expects a number");
            }

            double value = 0.0;
            if (JS_ToFloat64(context, &value, argv[0]) < 0)
            {
                return JS_EXCEPTION;
            }

            int32_t precision = 0;
            if (argc >= 2 && !JS_IsUndefined(argv[1]))
            {
                if (JS_ToInt32(context, &precision, argv[1]) < 0)
                {
                    return JS_EXCEPTION;
                }
                if (precision < -308 || precision > 308)
                {
                    return JS_ThrowRangeError(context, "round precision must be between -308 and 308");
                }
            }

            double epsilon = 0.0;
            if (argc >= 3 && !JS_IsUndefined(argv[2]))
            {
                if (JS_ToFloat64(context, &epsilon, argv[2]) < 0)
                {
                    return JS_EXCEPTION;
                }
            }

            const double factor = std::pow(10.0, static_cast<double>(precision));
            if (!std::isfinite(value) || !std::isfinite(factor) || factor == 0.0)
            {
                return JS_NewFloat64(context, value);
            }

            return JS_NewFloat64(context, std::round((value + epsilon) * factor) / factor);
        }
    }

    uint32_t arrayLength(JSContext *context, JSValueConst array)
    {
        uint32_t length = 0;
        JSValue lengthValue = JS_GetPropertyStr(context, array, "length");
        JS_ToUint32(context, &length, lengthValue);
        JS_FreeValue(context, lengthValue);
        return length;
    }

    std::string valueToString(JSContext *context, JSValueConst value)
    {
        const char *chars = JS_ToCString(context, value);
        if (chars == nullptr)
        {
            return "<failed to convert JavaScript value>";
        }

        std::string result(chars);
        JS_FreeCString(context, chars);
        return result;
    }

    std::string valueToInspectString(JSContext *context, JSValueConst value, bool compact)
    {
        return valueToInspectString(context, value, compact ? InspectFormat::Compact : InspectFormat::Pretty);
    }

    std::string valueToInspectString(JSContext *context, JSValueConst value, InspectFormat format)
    {
        std::vector<void *> seen;
        if (format == InspectFormat::Yaml)
        {
            return yamlValue(context, value, 0, seen);
        }

        return inspectValue(context, value, 0, seen, format == InspectFormat::Compact);
    }

    JSValue deepCopyValue(JSContext *context, JSValueConst value)
    {
        return deepCopyValueInternal(context, value, 0);
    }

    JSValue deepMergeObjects(JSContext *context, JSValueConst source, JSValueConst destination)
    {
        JSValue error = deepMergeObjectsInternal(context, source, destination, 0);
        if (JS_IsException(error))
        {
            return error;
        }

        JS_FreeValue(context, error);
        return JS_DupValue(context, destination);
    }

    void installUtilities(JSContext *context)
    {
        JSValue global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "clone", JS_NewCFunction(context, _jsClone, "clone", 1));
        JS_SetPropertyStr(context, global, "merge", JS_NewCFunction(context, _jsMerge, "merge", 2));
        JS_SetPropertyStr(context, global, "inspect", JS_NewCFunction(context, _jsInspect, "inspect", 2));
        JS_SetPropertyStr(context, global, "round", JS_NewCFunction(context, _jsRound, "round", 3));
        
        JS_FreeValue(context, global);
    }

    bool bytesFromJs(JSContext *context, JSValueConst value, std::vector<uint8_t> &bytes, std::string &error)
    {
        size_t size = 0;
        uint8_t *data = JS_GetUint8Array(context, &size, value);
        if (data != nullptr)
        {
            bytes.assign(data, data + size);
            return true;
        }

        data = JS_GetArrayBuffer(context, &size, value);
        if (data != nullptr)
        {
            bytes.assign(data, data + size);
            return true;
        }

        if (JS_IsArray(value))
        {
            const uint32_t length = arrayLength(context, value);
            bytes.reserve(length);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                uint32_t byte = 0;
                const int ok = JS_ToUint32(context, &byte, item);
                JS_FreeValue(context, item);
                if (ok < 0 || byte > 255)
                {
                    error = "byte array contains a non-byte value";
                    return false;
                }
                bytes.push_back(static_cast<uint8_t>(byte));
            }
            return true;
        }

        error = "expected Uint8Array, ArrayBuffer, or byte array";
        return false;
    }
}

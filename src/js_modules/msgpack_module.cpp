#include "js_modules.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "js_module_utils.h"
#include "msgpack_serde.h"

namespace js_modules
{
    namespace
    {
        const char *kMsgpackExtMarker = "__msgpackExt";

        bool writeJsValueToMsgpack(JSContext *context, JSValueConst value, msgpack_serde::Writer &writer, std::string &error, int depth);

        bool writeJsArrayToMsgpack(JSContext *context, JSValueConst value, msgpack_serde::Writer &writer, std::string &error, int depth)
        {
            const uint32_t length = arrayLength(context, value);
            writer.writeArrayHeader(length);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                const bool ok = writeJsValueToMsgpack(context, item, writer, error, depth + 1);
                JS_FreeValue(context, item);
                if (!ok)
                {
                    return false;
                }
            }
            return true;
        }

        bool writeJsObjectToMsgpack(JSContext *context, JSValueConst value, msgpack_serde::Writer &writer, std::string &error, int depth)
        {
            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, value, flags) < 0)
            {
                error = "failed to enumerate object properties";
                return false;
            }

            writer.writeMapHeader(propertyCount);
            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                const std::string key = valueToString(context, keyValue);
                JS_FreeValue(context, keyValue);

                writer.writeString(key);

                JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
                const bool ok = writeJsValueToMsgpack(context, propertyValue, writer, error, depth + 1);
                JS_FreeValue(context, propertyValue);
                if (!ok)
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return false;
                }
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            return true;
        }

        bool writeJsExtensionToMsgpack(JSContext *context, JSValueConst value, msgpack_serde::Writer &writer, std::string &error)
        {
            JSValue marker = JS_GetPropertyStr(context, value, kMsgpackExtMarker);
            const bool isExtension = JS_ToBool(context, marker) != 0;
            JS_FreeValue(context, marker);
            if (!isExtension)
            {
                return false;
            }

            JSValue typeValue = JS_GetPropertyStr(context, value, "type");
            int32_t type = 0;
            const int typeOk = JS_ToInt32(context, &type, typeValue);
            JS_FreeValue(context, typeValue);
            if (typeOk < 0 || type < -128 || type > 127)
            {
                error = "MessagePack extension type must be between -128 and 127";
                return false;
            }

            JSValue dataValue = JS_GetPropertyStr(context, value, "data");
            std::vector<uint8_t> data;
            const bool dataOk = bytesFromJs(context, dataValue, data, error);
            JS_FreeValue(context, dataValue);
            if (!dataOk)
            {
                error = "MessagePack extension data must be Uint8Array, ArrayBuffer, or byte array";
                return false;
            }

            writer.writeExtension(static_cast<int8_t>(type), data.data(), data.size());
            return true;
        }

        bool writeJsValueToMsgpack(JSContext *context, JSValueConst value, msgpack_serde::Writer &writer, std::string &error, int depth)
        {
            if (depth > 64)
            {
                error = "object nesting is too deep";
                return false;
            }

            if (JS_IsNull(value) || JS_IsUndefined(value))
            {
                writer.writeNil();
                return true;
            }

            if (JS_IsBool(value))
            {
                writer.writeBool(JS_ToBool(context, value) != 0);
                return true;
            }

            size_t byteLength = 0;
            uint8_t *bytes = JS_GetUint8Array(context, &byteLength, value);
            if (bytes != nullptr)
            {
                writer.writeBinary(bytes, byteLength);
                return true;
            }

            bytes = JS_GetArrayBuffer(context, &byteLength, value);
            if (bytes != nullptr)
            {
                writer.writeBinary(bytes, byteLength);
                return true;
            }

            if (JS_IsNumber(value))
            {
                double number = 0.0;
                if (JS_ToFloat64(context, &number, value) < 0)
                {
                    error = "failed to convert number";
                    return false;
                }

                if (std::isfinite(number) && std::floor(number) == number)
                {
                    if (number >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
                        number <= static_cast<double>(std::numeric_limits<int64_t>::max()))
                    {
                        writer.writeInt(static_cast<int64_t>(number));
                        return true;
                    }
                }

                writer.writeDouble(number);
                return true;
            }

            if (JS_IsString(value))
            {
                writer.writeString(valueToString(context, value));
                return true;
            }

            if (JS_IsFunction(context, value))
            {
                error = "cannot encode function values as MessagePack";
                return false;
            }

            if (JS_IsArray(value))
            {
                return writeJsArrayToMsgpack(context, value, writer, error, depth);
            }

            if (JS_IsObject(value))
            {
                if (writeJsExtensionToMsgpack(context, value, writer, error))
                {
                    return true;
                }

                if (!error.empty())
                {
                    return false;
                }

                return writeJsObjectToMsgpack(context, value, writer, error, depth);
            }

            error = "unsupported JavaScript value for MessagePack encoding";
            return false;
        }

        std::string msgpackMapKeyToString(const msgpack_serde::Value &key)
        {
            char text[64] = {};
            switch (key.type)
            {
            case msgpack_serde::Value::Type::Nil:
                return "null";
            case msgpack_serde::Value::Type::Bool:
                return key.boolValue ? "true" : "false";
            case msgpack_serde::Value::Type::Int:
                std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(key.intValue));
                return text;
            case msgpack_serde::Value::Type::UInt:
                std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(key.uintValue));
                return text;
            case msgpack_serde::Value::Type::Double:
                std::snprintf(text, sizeof(text), "%.17g", key.doubleValue);
                return text;
            case msgpack_serde::Value::Type::String:
                return key.stringValue;
            case msgpack_serde::Value::Type::Binary:
                return "[binary-key]";
            case msgpack_serde::Value::Type::Array:
                return "[array-key]";
            case msgpack_serde::Value::Type::Map:
                return "[map-key]";
            case msgpack_serde::Value::Type::Extension:
                return "[extension-key]";
            default:
                return "[key]";
            }
        }

        JSValue msgpackValueToJs(JSContext *context, const msgpack_serde::Value &value)
        {
            switch (value.type)
            {
            case msgpack_serde::Value::Type::Nil:
                return JS_NULL;
            case msgpack_serde::Value::Type::Bool:
                return JS_NewBool(context, value.boolValue);
            case msgpack_serde::Value::Type::Int:
                return JS_NewInt64(context, value.intValue);
            case msgpack_serde::Value::Type::UInt:
                if (value.uintValue <= UINT32_MAX)
                {
                    return JS_NewUint32(context, static_cast<uint32_t>(value.uintValue));
                }
                return JS_NewFloat64(context, static_cast<double>(value.uintValue));
            case msgpack_serde::Value::Type::Double:
                return JS_NewFloat64(context, value.doubleValue);
            case msgpack_serde::Value::Type::String:
                return JS_NewStringLen(context, value.stringValue.data(), value.stringValue.size());
            case msgpack_serde::Value::Type::Binary:
                return JS_NewUint8ArrayCopy(context, value.binaryValue.data(), value.binaryValue.size());
            case msgpack_serde::Value::Type::Array:
            {
                JSValue array = JS_NewArray(context);
                for (uint32_t i = 0; i < value.arrayValue.size(); ++i)
                {
                    JS_SetPropertyUint32(context, array, i, msgpackValueToJs(context, value.arrayValue[i]));
                }
                return array;
            }
            case msgpack_serde::Value::Type::Map:
            {
                JSValue object = JS_NewObject(context);
                for (const auto &entry : value.mapValue)
                {
                    const std::string key = msgpackMapKeyToString(entry.first);
                    JS_SetPropertyStr(context, object, key.c_str(), msgpackValueToJs(context, entry.second));
                }
                return object;
            }
            case msgpack_serde::Value::Type::Extension:
            {
                JSValue object = JS_NewObject(context);
                JS_SetPropertyStr(context, object, kMsgpackExtMarker, JS_NewBool(context, true));
                JS_SetPropertyStr(context, object, "type", JS_NewInt32(context, value.extensionType));
                JS_SetPropertyStr(context, object, "data", JS_NewUint8ArrayCopy(context, value.extensionData.data(), value.extensionData.size()));
                return object;
            }
            default:
                return JS_UNDEFINED;
            }
        }

        JSValue jsMsgpackEncode(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "msgpack.encode expects a value");
            }

            return encodeMsgpackValue(context, argv[0]);
        }

        JSValue jsMsgpackDecode(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "msgpack.decode expects bytes");
            }

            std::vector<uint8_t> bytes;
            std::string error;
            if (!bytesFromJs(context, argv[0], bytes, error))
            {
                return JS_ThrowTypeError(context, "%s", error.c_str());
            }

            msgpack_serde::Reader reader(bytes.data(), bytes.size());
            msgpack_serde::Value value;
            if (!reader.read(value, error))
            {
                return JS_ThrowTypeError(context, "%s", error.c_str());
            }

            return msgpackValueToJs(context, value);
        }

        JSValue jsMsgpackExt(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "msgpack.ext expects type and bytes");
            }

            int32_t type = 0;
            if (JS_ToInt32(context, &type, argv[0]) < 0 || type < -128 || type > 127)
            {
                return JS_ThrowTypeError(context, "MessagePack extension type must be between -128 and 127");
            }

            std::vector<uint8_t> data;
            std::string error;
            if (!bytesFromJs(context, argv[1], data, error))
            {
                return JS_ThrowTypeError(context, "%s", error.c_str());
            }

            JSValue object = JS_NewObject(context);
            JS_SetPropertyStr(context, object, kMsgpackExtMarker, JS_NewBool(context, true));
            JS_SetPropertyStr(context, object, "type", JS_NewInt32(context, type));
            JS_SetPropertyStr(context, object, "data", JS_NewUint8ArrayCopy(context, data.data(), data.size()));
            return object;
        }
    }

    void installMsgpack(JSContext *context)
    {
        JSValue global = JS_GetGlobalObject(context);
        JSValue msgpack = JS_NewObject(context);
        JS_SetPropertyStr(context, msgpack, "encode", JS_NewCFunction(context, jsMsgpackEncode, "encode", 1));
        JS_SetPropertyStr(context, msgpack, "decode", JS_NewCFunction(context, jsMsgpackDecode, "decode", 1));
        JS_SetPropertyStr(context, msgpack, "ext", JS_NewCFunction(context, jsMsgpackExt, "ext", 2));
        JS_SetPropertyStr(context, global, "msgpack", msgpack);
        JS_FreeValue(context, global);
    }

    JSValue encodeMsgpackValue(JSContext *context, JSValueConst value)
    {
        msgpack_serde::Writer writer;
        writer.reserve(256);
        std::string error;
        if (!writeJsValueToMsgpack(context, value, writer, error, 0))
        {
            return JS_ThrowTypeError(context, "%s", error.c_str());
        }

        const std::vector<uint8_t> &bytes = writer.bytes();
        return JS_NewUint8ArrayCopy(context, bytes.data(), bytes.size());
    }

    JSValue decodeMsgpackBytes(JSContext *context, const void *bytes, unsigned long long size)
    {
        msgpack_serde::Reader reader(static_cast<const uint8_t *>(bytes), static_cast<size_t>(size));
        msgpack_serde::Value value;
        std::string error;
        if (!reader.read(value, error))
        {
            return JS_ThrowTypeError(context, "%s", error.c_str());
        }

        return msgpackValueToJs(context, value);
    }
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4800)
#endif
#include "quickjs.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace js_modules
{
    enum class InspectFormat
    {
        Pretty,
        Compact,
        Yaml
    };

    uint32_t arrayLength(JSContext *context, JSValueConst array);
    std::string valueToString(JSContext *context, JSValueConst value);
    std::string valueToInspectString(JSContext *context, JSValueConst value, bool compact = false);
    std::string valueToInspectString(JSContext *context, JSValueConst value, InspectFormat format);
    JSValue deepCopyValue(JSContext *context, JSValueConst value);
    JSValue deepMergeObjects(JSContext *context, JSValueConst source, JSValueConst destination);
    bool bytesFromJs(JSContext *context, JSValueConst value, std::vector<uint8_t> &bytes, std::string &error);
    int getGlobalRoundDigits(JSContext *context, int defaultDigits = 3);
    double roundToPrecision(double value, int digits);
}

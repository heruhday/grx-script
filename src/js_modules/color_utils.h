#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

namespace color_utils
{
    struct ParsedColor
    {
        enum class Kind
        {
            Aci,
            Rgb
        };

        Kind kind = Kind::Aci;
        int aci = 256; // ByLayer default
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
    };

    inline double clamp(double value, double minValue, double maxValue)
    {
        return std::max(minValue, std::min(maxValue, value));
    }

    inline double normalizeHue(double hue)
    {
        hue = std::fmod(hue, 360.0);
        return hue < 0.0 ? hue + 360.0 : hue;
    }

    inline double normalizeUnit(double value)
    {
        if (value > 1.0)
        {
            value /= 100.0;
        }
        return clamp(value, 0.0, 1.0);
    }

    inline uint8_t toByte(double value)
    {
        return static_cast<uint8_t>(clamp(std::floor(value + 0.5), 0.0, 255.0));
    }

    inline double hueToRgb(double p, double q, double t)
    {
        if (t < 0.0) t += 1.0;
        if (t > 1.0) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 1.0 / 2.0) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    }

    inline void hslToRgb(double hue, double saturation, double lightness, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        const double h = normalizeHue(hue) / 360.0;
        const double s = normalizeUnit(saturation);
        const double l = normalizeUnit(lightness);

        double rd = l, gd = l, bd = l;
        if (s > 0.0)
        {
            const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
            const double p = 2.0 * l - q;
            rd = hueToRgb(p, q, h + 1.0 / 3.0);
            gd = hueToRgb(p, q, h);
            bd = hueToRgb(p, q, h - 1.0 / 3.0);
        }

        r = toByte(rd * 255.0);
        g = toByte(gd * 255.0);
        b = toByte(bd * 255.0);
    }

    inline void hsvToRgb(double hue, double saturation, double value, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        const double h = normalizeHue(hue) / 60.0;
        const double s = normalizeUnit(saturation);
        const double v = normalizeUnit(value);
        const int i = static_cast<int>(std::floor(h));
        const double f = h - i;
        const double p = v * (1.0 - s);
        const double q = v * (1.0 - s * f);
        const double t = v * (1.0 - s * (1.0 - f));

        double rd = 0.0, gd = 0.0, bd = 0.0;
        switch (i % 6)
        {
        case 0: rd = v; gd = t; bd = p; break;
        case 1: rd = q; gd = v; bd = p; break;
        case 2: rd = p; gd = v; bd = t; break;
        case 3: rd = p; gd = q; bd = v; break;
        case 4: rd = t; gd = p; bd = v; break;
        default: rd = v; gd = p; bd = q; break;
        }

        r = toByte(rd * 255.0);
        g = toByte(gd * 255.0);
        b = toByte(bd * 255.0);
    }

    inline void cmykToRgb(double cyan, double magenta, double yellow, double key, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        const double c = normalizeUnit(cyan);
        const double m = normalizeUnit(magenta);
        const double y = normalizeUnit(yellow);
        const double k = normalizeUnit(key);

        r = toByte(255.0 * (1.0 - c) * (1.0 - k));
        g = toByte(255.0 * (1.0 - m) * (1.0 - k));
        b = toByte(255.0 * (1.0 - y) * (1.0 - k));
    }

    inline double labPivotInverse(double value)
    {
        const double value3 = value * value * value;
        return value3 > 0.008856 ? value3 : (value - 16.0 / 116.0) / 7.787;
    }

    inline double linearToSrgb(double value)
    {
        value = clamp(value, 0.0, 1.0);
        return 255.0 * (value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
    }

    inline void hctToRgb(double hue, double chroma, double tone, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        const double kPi = 3.14159265358979323846;
        const double kD65X = 95.047;
        const double kD65Y = 100.000;
        const double kD65Z = 108.883;

        const double hRad = normalizeHue(hue) * kPi / 180.0;
        const double c = std::max(0.0, chroma);
        const double t = clamp(tone, 0.0, 100.0);

        const double a = std::cos(hRad) * c;
        const double bComp = std::sin(hRad) * c;
        const double fy = (t + 16.0) / 116.0;
        const double fx = a / 500.0 + fy;
        const double fz = fy - bComp / 200.0;

        const double x = kD65X * labPivotInverse(fx) / 100.0;
        const double y = kD65Y * labPivotInverse(fy) / 100.0;
        const double z = kD65Z * labPivotInverse(fz) / 100.0;

        const double rl = x * 3.2404542 + y * -1.5371385 + z * -0.4985314;
        const double gl = x * -0.9692660 + y * 1.8760108 + z * 0.0415560;
        const double bl = x * 0.0556434 + y * -0.2040259 + z * 1.0572252;

        r = toByte(linearToSrgb(rl));
        g = toByte(linearToSrgb(gl));
        b = toByte(linearToSrgb(bl));
    }

    inline std::string trim(const std::string &str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }

    inline std::string toLower(std::string text)
    {
        for (size_t i = 0; i < text.size(); ++i)
        {
            text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        }
        return text;
    }

    inline bool parseHexColor(const std::string &rawText, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        std::string text = trim(rawText);
        if (text.empty() || text[0] != '#') return false;
        text = text.substr(1);

        auto hexVal = [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        if (text.size() == 3 || text.size() == 4) // #rgb or #rgba
        {
            int r1 = hexVal(text[0]);
            int g1 = hexVal(text[1]);
            int b1 = hexVal(text[2]);
            if (r1 < 0 || g1 < 0 || b1 < 0) return false;
            r = static_cast<uint8_t>(r1 * 17);
            g = static_cast<uint8_t>(g1 * 17);
            b = static_cast<uint8_t>(b1 * 17);
            return true;
        }

        if (text.size() == 6 || text.size() == 8) // #rrggbb or #rrggbbaa
        {
            int r1 = hexVal(text[0]), r2 = hexVal(text[1]);
            int g1 = hexVal(text[2]), g2 = hexVal(text[3]);
            int b1 = hexVal(text[4]), b2 = hexVal(text[5]);
            if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
            r = static_cast<uint8_t>(r1 * 16 + r2);
            g = static_cast<uint8_t>(g1 * 16 + g2);
            b = static_cast<uint8_t>(b1 * 16 + b2);
            return true;
        }

        return false;
    }

    inline bool parseNumberList(const char *cursor, std::vector<double> &values)
    {
        while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (*cursor != '(') return false;
        ++cursor;

        for (;;)
        {
            while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
            char *end = nullptr;
            const double val = std::strtod(cursor, &end);
            if (end == cursor) return false;

            values.push_back(val);
            cursor = end;
            while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;

            // Optional '%' suffix for CSS percent values
            if (*cursor == '%')
            {
                ++cursor;
                while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
            }

            if (*cursor == ',')
            {
                ++cursor;
                continue;
            }

            if (*cursor == ')')
            {
                ++cursor;
                while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
                return *cursor == '\0';
            }

            return false;
        }
    }

    inline bool parseColorString(const std::string &rawText, ParsedColor &outColor, std::string &error)
    {
        const std::string text = trim(rawText);
        const std::string lower = toLower(text);

        // Standard AutoCAD color names
        if (lower == "byblock") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 0; return true; }
        if (lower == "bylayer") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 256; return true; }
        if (lower == "red") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 1; return true; }
        if (lower == "yellow") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 2; return true; }
        if (lower == "green") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 3; return true; }
        if (lower == "cyan") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 4; return true; }
        if (lower == "blue") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 5; return true; }
        if (lower == "magenta") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 6; return true; }
        if (lower == "white") { outColor.kind = ParsedColor::Kind::Aci; outColor.aci = 7; return true; }

        uint8_t red = 0, green = 0, blue = 0;
        if (parseHexColor(text, red, green, blue))
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            outColor.r = red;
            outColor.g = green;
            outColor.b = blue;
            return true;
        }

        const size_t open = lower.find('(');
        if (open == std::string::npos)
        {
            error = "unrecognized color string: " + text;
            return false;
        }

        const std::string space = trim(lower.substr(0, open));
        std::vector<double> values;
        if (!parseNumberList(lower.c_str() + open, values))
        {
            error = "invalid channel numbers in color string: " + text;
            return false;
        }

        if (space == "rgb" && values.size() >= 3)
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            outColor.r = toByte(values[0]);
            outColor.g = toByte(values[1]);
            outColor.b = toByte(values[2]);
            return true;
        }
        if (space == "rgba" && values.size() >= 4)
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            outColor.r = toByte(values[0]);
            outColor.g = toByte(values[1]);
            outColor.b = toByte(values[2]);
            return true;
        }
        if (space == "hsl" && values.size() >= 3)
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            hslToRgb(values[0], values[1], values[2], outColor.r, outColor.g, outColor.b);
            return true;
        }
        if ((space == "hsv" || space == "hsb") && values.size() >= 3)
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            hsvToRgb(values[0], values[1], values[2], outColor.r, outColor.g, outColor.b);
            return true;
        }
        if (space == "cmyk" && values.size() >= 4)
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            cmykToRgb(values[0], values[1], values[2], values[3], outColor.r, outColor.g, outColor.b);
            return true;
        }
        if (space == "hct" && values.size() >= 3)
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            hctToRgb(values[0], values[1], values[2], outColor.r, outColor.g, outColor.b);
            return true;
        }

        error = "unsupported color functional string: " + space;
        return false;
    }

    inline bool getPropDouble(JSContext *context, JSValueConst obj, const char *propName, double &outVal)
    {
        JSValue val = JS_GetPropertyStr(context, obj, propName);
        if (JS_IsUndefined(val) || JS_IsNull(val))
        {
            JS_FreeValue(context, val);
            return false;
        }
        double d = 0.0;
        int ret = JS_ToFloat64(context, &d, val);
        JS_FreeValue(context, val);
        if (ret < 0) return false;
        outVal = d;
        return true;
    }

    inline bool getPropDoubleAny(JSContext *context, JSValueConst obj, const std::vector<const char *> &propNames, double &outVal)
    {
        for (const char *name : propNames)
        {
            if (getPropDouble(context, obj, name, outVal)) return true;
        }
        return false;
    }

    inline bool parseColorArray(JSContext *context, JSValueConst array, ParsedColor &outColor, std::string &error)
    {
        JSValue lenVal = JS_GetPropertyStr(context, array, "length");
        int64_t len = 0;
        JS_ToInt64(context, &len, lenVal);
        JS_FreeValue(context, lenVal);

        if (len < 3)
        {
            error = "color array must contain at least 3 elements [r, g, b]";
            return false;
        }

        double channels[4] = {0, 0, 0, 1};
        for (int i = 0; i < std::min(static_cast<int>(len), 4); ++i)
        {
            JSValue elem = JS_GetPropertyUint32(context, array, static_cast<uint32_t>(i));
            JS_ToFloat64(context, &channels[i], elem);
            JS_FreeValue(context, elem);
        }

        outColor.kind = ParsedColor::Kind::Rgb;
        outColor.r = toByte(channels[0]);
        outColor.g = toByte(channels[1]);
        outColor.b = toByte(channels[2]);
        return true;
    }

    inline bool parseColorObject(JSContext *context, JSValueConst obj, ParsedColor &outColor, std::string &error)
    {
        // 1. Check explicit 'space' property
        JSValue spaceVal = JS_GetPropertyStr(context, obj, "space");
        std::string space;
        if (JS_IsString(spaceVal))
        {
            const char *s = JS_ToCString(context, spaceVal);
            if (s)
            {
                space = toLower(trim(s));
                JS_FreeCString(context, s);
            }
        }
        JS_FreeValue(context, spaceVal);

        // 2. Check nested color space wrapper: { hsl: [200, 70, 50] } or { hsl: { h: ... } }
        const char *spaces[] = { "hsl", "hsv", "hsb", "cmyk", "hct", "rgb" };
        for (const char *sp : spaces)
        {
            JSValue nested = JS_GetPropertyStr(context, obj, sp);
            if (!JS_IsUndefined(nested) && !JS_IsNull(nested))
            {
                if (JS_IsArray(nested))
                {
                    JSValue lenVal = JS_GetPropertyStr(context, nested, "length");
                    int64_t arrLen = 0;
                    JS_ToInt64(context, &arrLen, lenVal);
                    JS_FreeValue(context, lenVal);

                    std::vector<double> vals;
                    for (int64_t i = 0; i < arrLen; ++i)
                    {
                        JSValue elem = JS_GetPropertyUint32(context, nested, static_cast<uint32_t>(i));
                        double d = 0.0;
                        JS_ToFloat64(context, &d, elem);
                        JS_FreeValue(context, elem);
                        vals.push_back(d);
                    }
                    JS_FreeValue(context, nested);

                    std::string sName(sp);
                    outColor.kind = ParsedColor::Kind::Rgb;
                    if (sName == "hsl" && vals.size() >= 3)
                    {
                        hslToRgb(vals[0], vals[1], vals[2], outColor.r, outColor.g, outColor.b);
                        return true;
                    }
                    if ((sName == "hsv" || sName == "hsb") && vals.size() >= 3)
                    {
                        hsvToRgb(vals[0], vals[1], vals[2], outColor.r, outColor.g, outColor.b);
                        return true;
                    }
                    if (sName == "cmyk" && vals.size() >= 4)
                    {
                        cmykToRgb(vals[0], vals[1], vals[2], vals[3], outColor.r, outColor.g, outColor.b);
                        return true;
                    }
                    if (sName == "hct" && vals.size() >= 3)
                    {
                        hctToRgb(vals[0], vals[1], vals[2], outColor.r, outColor.g, outColor.b);
                        return true;
                    }
                    if (sName == "rgb" && vals.size() >= 3)
                    {
                        outColor.r = toByte(vals[0]);
                        outColor.g = toByte(vals[1]);
                        outColor.b = toByte(vals[2]);
                        return true;
                    }
                    error = "invalid nested " + sName + " array length";
                    return false;
                }
                else if (JS_IsObject(nested))
                {
                    bool ok = parseColorObject(context, nested, outColor, error);
                    JS_FreeValue(context, nested);
                    return ok;
                }
                JS_FreeValue(context, nested);
            }
        }

        // 3. Check AutoCAD Color Index (ACI)
        double aciVal = 0.0;
        if (getPropDoubleAny(context, obj, { "aci", "index", "colorIndex" }, aciVal))
        {
            int idx = static_cast<int>(aciVal);
            if (idx < 0 || idx > 256)
            {
                error = "aci color index must be from 0 to 256";
                return false;
            }
            outColor.kind = ParsedColor::Kind::Aci;
            outColor.aci = idx;
            return true;
        }

        // 4. If space is explicitly set:
        if (space == "hsl")
        {
            double h = 0.0, s = 100.0, l = 50.0;
            getPropDoubleAny(context, obj, { "h", "hue" }, h);
            getPropDoubleAny(context, obj, { "s", "sat", "saturation" }, s);
            getPropDoubleAny(context, obj, { "l", "light", "lightness" }, l);
            outColor.kind = ParsedColor::Kind::Rgb;
            hslToRgb(h, s, l, outColor.r, outColor.g, outColor.b);
            return true;
        }
        if (space == "hsv" || space == "hsb")
        {
            double h = 0.0, s = 100.0, v = 100.0;
            getPropDoubleAny(context, obj, { "h", "hue" }, h);
            getPropDoubleAny(context, obj, { "s", "sat", "saturation" }, s);
            getPropDoubleAny(context, obj, { "v", "val", "value", "b", "brightness" }, v);
            outColor.kind = ParsedColor::Kind::Rgb;
            hsvToRgb(h, s, v, outColor.r, outColor.g, outColor.b);
            return true;
        }
        if (space == "cmyk")
        {
            double c = 0.0, m = 0.0, y = 0.0, k = 0.0;
            getPropDoubleAny(context, obj, { "c", "cyan" }, c);
            getPropDoubleAny(context, obj, { "m", "magenta" }, m);
            getPropDoubleAny(context, obj, { "y", "yellow" }, y);
            getPropDoubleAny(context, obj, { "k", "black", "key" }, k);
            outColor.kind = ParsedColor::Kind::Rgb;
            cmykToRgb(c, m, y, k, outColor.r, outColor.g, outColor.b);
            return true;
        }
        if (space == "hct")
        {
            double h = 0.0, c = 0.0, t = 50.0;
            getPropDoubleAny(context, obj, { "h", "hue" }, h);
            getPropDoubleAny(context, obj, { "c", "chroma" }, c);
            getPropDoubleAny(context, obj, { "t", "tone" }, t);
            outColor.kind = ParsedColor::Kind::Rgb;
            hctToRgb(h, c, t, outColor.r, outColor.g, outColor.b);
            return true;
        }

        // 5. Automatic detection from channel keys:
        // 5a. HCT: has 'h' and ('t' or 'tone')
        double hVal = 0.0, tVal = 0.0;
        if (getPropDoubleAny(context, obj, { "h", "hue" }, hVal) &&
            getPropDoubleAny(context, obj, { "t", "tone" }, tVal))
        {
            double cVal = 0.0;
            getPropDoubleAny(context, obj, { "c", "chroma" }, cVal);
            outColor.kind = ParsedColor::Kind::Rgb;
            hctToRgb(hVal, cVal, tVal, outColor.r, outColor.g, outColor.b);
            return true;
        }

        // 5b. CMYK: has 'c' (or cyan) and 'm' (or magenta) and 'y' (or yellow)
        double cVal = 0.0, mVal = 0.0, yVal = 0.0;
        if (getPropDoubleAny(context, obj, { "c", "cyan" }, cVal) &&
            getPropDoubleAny(context, obj, { "m", "magenta" }, mVal) &&
            getPropDoubleAny(context, obj, { "y", "yellow" }, yVal))
        {
            double kVal = 0.0;
            getPropDoubleAny(context, obj, { "k", "key", "black" }, kVal);
            outColor.kind = ParsedColor::Kind::Rgb;
            cmykToRgb(cVal, mVal, yVal, kVal, outColor.r, outColor.g, outColor.b);
            return true;
        }

        // 5c. HSV: has 'h' and ('v', 'val', 'value') OR ('h' and 's' and 'b' without 'l')
        double vVal = 0.0;
        if (getPropDoubleAny(context, obj, { "h", "hue" }, hVal) &&
            (getPropDoubleAny(context, obj, { "v", "val", "value" }, vVal) ||
             (!getPropDoubleAny(context, obj, { "l", "light", "lightness" }, vVal) &&
              getPropDoubleAny(context, obj, { "b", "brightness" }, vVal))))
        {
            double sVal = 100.0;
            getPropDoubleAny(context, obj, { "s", "sat", "saturation" }, sVal);
            outColor.kind = ParsedColor::Kind::Rgb;
            hsvToRgb(hVal, sVal, vVal, outColor.r, outColor.g, outColor.b);
            return true;
        }

        // 5d. HSL: has 'h' (or 'hue') and ('l' or 'light' or 'lightness' or 's' or 'saturation')
        double lVal = 50.0;
        bool hasL = getPropDoubleAny(context, obj, { "l", "light", "lightness" }, lVal);
        double sVal = 100.0;
        bool hasS = getPropDoubleAny(context, obj, { "s", "sat", "saturation" }, sVal);
        if (getPropDoubleAny(context, obj, { "h", "hue" }, hVal) && (hasL || hasS))
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            hslToRgb(hVal, sVal, lVal, outColor.r, outColor.g, outColor.b);
            return true;
        }

        // 5e. RGB: has 'r' and 'g' and 'b'
        double rVal = 0.0, gVal = 0.0, bVal = 0.0;
        if (getPropDoubleAny(context, obj, { "r", "red" }, rVal) &&
            getPropDoubleAny(context, obj, { "g", "green" }, gVal) &&
            getPropDoubleAny(context, obj, { "b", "blue" }, bVal))
        {
            outColor.kind = ParsedColor::Kind::Rgb;
            outColor.r = toByte(rVal);
            outColor.g = toByte(gVal);
            outColor.b = toByte(bVal);
            return true;
        }

        error = "clr object must contain valid color channels: HSL ({h,s,l}), HSV ({h,s,v}), CMYK ({c,m,y,k}), HCT ({h,c,t}), or RGB ({r,g,b})";
        return false;
    }

    inline bool parseColorFromJs(JSContext *context, JSValueConst value, ParsedColor &outColor, std::string &error)
    {
        if (JS_IsNumber(value))
        {
            int colorIndex = 0;
            if (JS_ToInt32(context, &colorIndex, value) < 0 || colorIndex < 0 || colorIndex > 256)
            {
                error = "clr must be an AutoCAD color index from 0 to 256";
                return false;
            }
            outColor.kind = ParsedColor::Kind::Aci;
            outColor.aci = colorIndex;
            return true;
        }

        if (JS_IsString(value))
        {
            const char *str = JS_ToCString(context, value);
            if (!str)
            {
                error = "failed to read color string";
                return false;
            }
            std::string s(str);
            JS_FreeCString(context, str);
            return parseColorString(s, outColor, error);
        }

        if (JS_IsArray(value))
        {
            return parseColorArray(context, value, outColor, error);
        }

        if (JS_IsObject(value))
        {
            return parseColorObject(context, value, outColor, error);
        }

        error = "clr must be 0..256, hex string ('#rrggbb'), CSS string ('hsl(...)'), or color object ({h,s,l}, {c,m,y,k}, {h,c,t}, {r,g,b})";
        return false;
    }
}

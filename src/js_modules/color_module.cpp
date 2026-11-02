#include "js_modules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        const double kPi = 3.14159265358979323846;
        const double kD65X = 95.047;
        const double kD65Y = 100.000;
        const double kD65Z = 108.883;

        double clamp(double value, double minValue, double maxValue)
        {
            return std::max(minValue, std::min(maxValue, value));
        }

        double normalizeHue(double hue)
        {
            hue = std::fmod(hue, 360.0);
            return hue < 0.0 ? hue + 360.0 : hue;
        }

        double normalizeUnit(double value)
        {
            return clamp(value > 1.0 ? value / 100.0 : value, 0.0, 1.0);
        }

        double normalizeAlpha(double value)
        {
            return clamp(value > 1.0 ? value / 255.0 : value, 0.0, 1.0);
        }

        int toByte(double value)
        {
            return static_cast<int>(clamp(std::floor(value + 0.5), 0.0, 255.0));
        }

        double propNumber(JSContext *context, JSValueConst object, const char *name, double fallback)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            if (JS_IsUndefined(value) || JS_IsNull(value))
            {
                JS_FreeValue(context, value);
                return fallback;
            }

            double number = fallback;
            JS_ToFloat64(context, &number, value);
            JS_FreeValue(context, value);
            return number;
        }

        double argNumber(JSContext *context, int argc, JSValueConst *argv, int index, const char *name, double fallback)
        {
            if (argc > 0 && JS_IsObject(argv[0]) && !JS_IsArray(argv[0]))
            {
                return propNumber(context, argv[0], name, fallback);
            }

            if (argc > index)
            {
                double number = fallback;
                JS_ToFloat64(context, &number, argv[index]);
                return number;
            }

            return fallback;
        }

        JSValue rgbObject(JSContext *context, double r, double g, double b)
        {
            JSValue result = JS_NewObject(context);
            JS_SetPropertyStr(context, result, "r", JS_NewInt32(context, toByte(r)));
            JS_SetPropertyStr(context, result, "g", JS_NewInt32(context, toByte(g)));
            JS_SetPropertyStr(context, result, "b", JS_NewInt32(context, toByte(b)));
            return result;
        }

        JSValue rgbaObject(JSContext *context, double r, double g, double b, double a)
        {
            JSValue result = rgbObject(context, r, g, b);
            JS_SetPropertyStr(context, result, "a", JS_NewFloat64(context, normalizeAlpha(a)));
            return result;
        }

        JSValue tripleObject(JSContext *context, const char *a, double av, const char *b, double bv, const char *c, double cv);
        JSValue quadObject(JSContext *context, const char *a, double av, const char *b, double bv, const char *c, double cv, const char *d, double dv);

        std::string toLowerAscii(std::string text)
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            }
            return text;
        }

        void skipSpaces(const char *&cursor)
        {
            while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
            {
                ++cursor;
            }
        }

        bool parseNumberList(const char *cursor, std::vector<double> &values)
        {
            skipSpaces(cursor);
            if (*cursor != '(')
            {
                return false;
            }
            ++cursor;

            for (;;)
            {
                skipSpaces(cursor);
                char *end = nullptr;
                const double value = std::strtod(cursor, &end);
                if (end == cursor)
                {
                    return false;
                }
                values.push_back(value);
                cursor = end;
                skipSpaces(cursor);

                if (*cursor == ',')
                {
                    ++cursor;
                    continue;
                }

                if (*cursor == ')')
                {
                    ++cursor;
                    skipSpaces(cursor);
                    return *cursor == '\0';
                }

                return false;
            }
        }

        JSValue parsedObject(JSContext *context, const char *space, const char *a, double av, const char *b, double bv, const char *c, double cv)
        {
            JSValue result = tripleObject(context, a, av, b, bv, c, cv);
            JS_SetPropertyStr(context, result, "space", JS_NewString(context, space));
            return result;
        }

        JSValue parsedObject(JSContext *context, const char *space, const char *a, double av, const char *b, double bv, const char *c, double cv, const char *d, double dv)
        {
            JSValue result = quadObject(context, a, av, b, bv, c, cv, d, dv);
            JS_SetPropertyStr(context, result, "space", JS_NewString(context, space));
            return result;
        }

        JSValue tripleObject(JSContext *context, const char *a, double av, const char *b, double bv, const char *c, double cv)
        {
            JSValue result = JS_NewObject(context);
            JS_SetPropertyStr(context, result, a, JS_NewFloat64(context, av));
            JS_SetPropertyStr(context, result, b, JS_NewFloat64(context, bv));
            JS_SetPropertyStr(context, result, c, JS_NewFloat64(context, cv));
            return result;
        }

        JSValue quadObject(JSContext *context, const char *a, double av, const char *b, double bv, const char *c, double cv, const char *d, double dv)
        {
            JSValue result = tripleObject(context, a, av, b, bv, c, cv);
            JS_SetPropertyStr(context, result, d, JS_NewFloat64(context, dv));
            return result;
        }

        void readRgb(JSContext *context, int argc, JSValueConst *argv, double &r, double &g, double &b)
        {
            r = clamp(argNumber(context, argc, argv, 0, "r", 0.0), 0.0, 255.0);
            g = clamp(argNumber(context, argc, argv, 1, "g", 0.0), 0.0, 255.0);
            b = clamp(argNumber(context, argc, argv, 2, "b", 0.0), 0.0, 255.0);
        }

        double hueToRgb(double p, double q, double t)
        {
            if (t < 0.0)
                t += 1.0;
            if (t > 1.0)
                t -= 1.0;
            if (t < 1.0 / 6.0)
                return p + (q - p) * 6.0 * t;
            if (t < 1.0 / 2.0)
                return q;
            if (t < 2.0 / 3.0)
                return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
            return p;
        }

        JSValue jsHslToRgb(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            const double h = normalizeHue(argNumber(context, argc, argv, 0, "h", 0.0)) / 360.0;
            const double s = normalizeUnit(argNumber(context, argc, argv, 1, "s", 0.0));
            const double l = normalizeUnit(argNumber(context, argc, argv, 2, "l", 0.0));

            double r = l, g = l, b = l;
            if (s > 0.0)
            {
                const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
                const double p = 2.0 * l - q;
                r = hueToRgb(p, q, h + 1.0 / 3.0);
                g = hueToRgb(p, q, h);
                b = hueToRgb(p, q, h - 1.0 / 3.0);
            }

            return rgbObject(context, r * 255.0, g * 255.0, b * 255.0);
        }

        JSValue jsRgbToHsl(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            double r, g, b;
            readRgb(context, argc, argv, r, g, b);
            r /= 255.0;
            g /= 255.0;
            b /= 255.0;

            const double maxValue = std::max(r, std::max(g, b));
            const double minValue = std::min(r, std::min(g, b));
            double h = 0.0;
            double s = 0.0;
            const double l = (maxValue + minValue) / 2.0;
            const double delta = maxValue - minValue;
            if (delta > 0.0)
            {
                s = l > 0.5 ? delta / (2.0 - maxValue - minValue) : delta / (maxValue + minValue);
                if (maxValue == r)
                    h = (g - b) / delta + (g < b ? 6.0 : 0.0);
                else if (maxValue == g)
                    h = (b - r) / delta + 2.0;
                else
                    h = (r - g) / delta + 4.0;
                h *= 60.0;
            }

            return tripleObject(context, "h", normalizeHue(h), "s", s, "l", l);
        }

        JSValue jsHsvToRgb(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            const double h = normalizeHue(argNumber(context, argc, argv, 0, "h", 0.0)) / 60.0;
            const double s = normalizeUnit(argNumber(context, argc, argv, 1, "s", 0.0));
            const double v = normalizeUnit(argNumber(context, argc, argv, 2, "v", 0.0));
            const int i = static_cast<int>(std::floor(h));
            const double f = h - i;
            const double p = v * (1.0 - s);
            const double q = v * (1.0 - s * f);
            const double t = v * (1.0 - s * (1.0 - f));

            double r, g, b;
            switch (i % 6)
            {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
            }
            return rgbObject(context, r * 255.0, g * 255.0, b * 255.0);
        }

        JSValue jsRgbToHsv(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            double r, g, b;
            readRgb(context, argc, argv, r, g, b);
            r /= 255.0;
            g /= 255.0;
            b /= 255.0;

            const double maxValue = std::max(r, std::max(g, b));
            const double minValue = std::min(r, std::min(g, b));
            const double delta = maxValue - minValue;
            double h = 0.0;
            if (delta > 0.0)
            {
                if (maxValue == r)
                    h = 60.0 * std::fmod(((g - b) / delta), 6.0);
                else if (maxValue == g)
                    h = 60.0 * (((b - r) / delta) + 2.0);
                else
                    h = 60.0 * (((r - g) / delta) + 4.0);
            }

            return tripleObject(context, "h", normalizeHue(h), "s", maxValue == 0.0 ? 0.0 : delta / maxValue, "v", maxValue);
        }

        JSValue jsCmykToRgb(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            const double c = normalizeUnit(argNumber(context, argc, argv, 0, "c", 0.0));
            const double m = normalizeUnit(argNumber(context, argc, argv, 1, "m", 0.0));
            const double y = normalizeUnit(argNumber(context, argc, argv, 2, "y", 0.0));
            const double k = normalizeUnit(argNumber(context, argc, argv, 3, "k", 0.0));
            return rgbObject(context, 255.0 * (1.0 - c) * (1.0 - k), 255.0 * (1.0 - m) * (1.0 - k), 255.0 * (1.0 - y) * (1.0 - k));
        }

        JSValue jsRgbToCmyk(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            double r, g, b;
            readRgb(context, argc, argv, r, g, b);
            r /= 255.0;
            g /= 255.0;
            b /= 255.0;
            const double k = 1.0 - std::max(r, std::max(g, b));
            if (k >= 1.0)
            {
                return quadObject(context, "c", 0.0, "m", 0.0, "y", 0.0, "k", 1.0);
            }
            return quadObject(context, "c", (1.0 - r - k) / (1.0 - k), "m", (1.0 - g - k) / (1.0 - k), "y", (1.0 - b - k) / (1.0 - k), "k", k);
        }

        double srgbToLinear(double value)
        {
            value /= 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        }

        double linearToSrgb(double value)
        {
            value = clamp(value, 0.0, 1.0);
            return 255.0 * (value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
        }

        double labPivot(double value)
        {
            return value > 0.008856 ? std::pow(value, 1.0 / 3.0) : (7.787 * value) + (16.0 / 116.0);
        }

        double labPivotInverse(double value)
        {
            const double value3 = value * value * value;
            return value3 > 0.008856 ? value3 : (value - 16.0 / 116.0) / 7.787;
        }

        JSValue jsRgbToHct(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            double r, g, b;
            readRgb(context, argc, argv, r, g, b);
            r = srgbToLinear(r);
            g = srgbToLinear(g);
            b = srgbToLinear(b);

            const double x = (r * 0.4124564 + g * 0.3575761 + b * 0.1804375) * 100.0;
            const double y = (r * 0.2126729 + g * 0.7151522 + b * 0.0721750) * 100.0;
            const double z = (r * 0.0193339 + g * 0.1191920 + b * 0.9503041) * 100.0;

            const double fx = labPivot(x / kD65X);
            const double fy = labPivot(y / kD65Y);
            const double fz = labPivot(z / kD65Z);
            const double l = 116.0 * fy - 16.0;
            const double a = 500.0 * (fx - fy);
            const double bb = 200.0 * (fy - fz);
            const double h = normalizeHue(std::atan2(bb, a) * 180.0 / kPi);
            const double c = std::sqrt(a * a + bb * bb);
            return tripleObject(context, "h", h, "c", c, "t", clamp(l, 0.0, 100.0));
        }

        JSValue jsHctToRgb(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            const double h = normalizeHue(argNumber(context, argc, argv, 0, "h", 0.0)) * kPi / 180.0;
            const double c = std::max(0.0, argNumber(context, argc, argv, 1, "c", 0.0));
            const double t = clamp(argNumber(context, argc, argv, 2, "t", 0.0), 0.0, 100.0);

            const double a = std::cos(h) * c;
            const double b = std::sin(h) * c;
            const double fy = (t + 16.0) / 116.0;
            const double fx = a / 500.0 + fy;
            const double fz = fy - b / 200.0;

            const double x = kD65X * labPivotInverse(fx) / 100.0;
            const double y = kD65Y * labPivotInverse(fy) / 100.0;
            const double z = kD65Z * labPivotInverse(fz) / 100.0;

            const double rl = x * 3.2404542 + y * -1.5371385 + z * -0.4985314;
            const double gl = x * -0.9692660 + y * 1.8760108 + z * 0.0415560;
            const double bl = x * 0.0556434 + y * -0.2040259 + z * 1.0572252;
            return rgbObject(context, linearToSrgb(rl), linearToSrgb(gl), linearToSrgb(bl));
        }

        JSValue jsRgbaToRgb(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            double r, g, b;
            readRgb(context, argc, argv, r, g, b);
            const double a = normalizeAlpha(argNumber(context, argc, argv, 3, "a", 1.0));

            double br = 255.0, bg = 255.0, bb = 255.0;
            if (argc >= 2 && JS_IsObject(argv[1]) && !JS_IsArray(argv[1]))
            {
                br = clamp(propNumber(context, argv[1], "r", 255.0), 0.0, 255.0);
                bg = clamp(propNumber(context, argv[1], "g", 255.0), 0.0, 255.0);
                bb = clamp(propNumber(context, argv[1], "b", 255.0), 0.0, 255.0);
            }
            else if (argc >= 7)
            {
                JS_ToFloat64(context, &br, argv[4]);
                JS_ToFloat64(context, &bg, argv[5]);
                JS_ToFloat64(context, &bb, argv[6]);
            }

            return rgbObject(context, r * a + br * (1.0 - a), g * a + bg * (1.0 - a), b * a + bb * (1.0 - a));
        }

        JSValue jsRgbToRgba(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            double r, g, b;
            readRgb(context, argc, argv, r, g, b);
            double a = 1.0;
            if (argc > 0 && JS_IsObject(argv[0]) && !JS_IsArray(argv[0]))
            {
                a = argc > 1 ? argNumber(context, argc - 1, argv + 1, 0, "a", propNumber(context, argv[0], "a", 1.0)) : propNumber(context, argv[0], "a", 1.0);
            }
            else if (argc > 3)
            {
                a = argNumber(context, argc, argv, 3, "a", 1.0);
            }
            return rgbaObject(context, r, g, b, a);
        }

        JSValue jsParseColor(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "color.parse expects a color string");
            }

            const std::string source = valueToString(context, argv[0]);
            const size_t openParen = source.find('(');
            if (openParen == std::string::npos)
            {
                return JS_ThrowTypeError(context, "color.parse expected NAME(...)");
            }

            std::string name = source.substr(0, openParen);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            {
                name.erase(name.size() - 1);
            }
            size_t first = 0;
            while (first < name.size() && std::isspace(static_cast<unsigned char>(name[first])))
            {
                ++first;
            }
            name = toLowerAscii(name.substr(first));

            std::vector<double> values;
            if (!parseNumberList(source.c_str() + openParen, values))
            {
                return JS_ThrowTypeError(context, "color.parse expected comma-separated numeric channels");
            }

            if (name == "hsl" && values.size() == 3)
            {
                return parsedObject(context, "hsl", "h", values[0], "s", values[1], "l", values[2]);
            }
            if (name == "hsv" && values.size() == 3)
            {
                return parsedObject(context, "hsv", "h", values[0], "s", values[1], "v", values[2]);
            }
            if (name == "hct" && values.size() == 3)
            {
                return parsedObject(context, "hct", "h", values[0], "c", values[1], "t", values[2]);
            }
            if (name == "rgb" && values.size() == 3)
            {
                return parsedObject(context, "rgb", "r", values[0], "g", values[1], "b", values[2]);
            }
            if (name == "rgba" && values.size() == 4)
            {
                return parsedObject(context, "rgba", "r", values[0], "g", values[1], "b", values[2], "a", values[3]);
            }
            if (name == "cmyk" && values.size() == 4)
            {
                return parsedObject(context, "cmyk", "c", values[0], "m", values[1], "y", values[2], "k", values[3]);
            }

            return JS_ThrowTypeError(context, "color.parse unsupported color string");
        }
    }

    JSValue createColorModule(JSContext *context)
    {
        JSValue color = JS_NewObject(context);
        JS_SetPropertyStr(context, color, "parse", JS_NewCFunction(context, jsParseColor, "parse", 1));
        JS_SetPropertyStr(context, color, "hslToRgb", JS_NewCFunction(context, jsHslToRgb, "hslToRgb", 3));
        JS_SetPropertyStr(context, color, "rgbToHsl", JS_NewCFunction(context, jsRgbToHsl, "rgbToHsl", 3));
        JS_SetPropertyStr(context, color, "hsvToRgb", JS_NewCFunction(context, jsHsvToRgb, "hsvToRgb", 3));
        JS_SetPropertyStr(context, color, "rgbToHsv", JS_NewCFunction(context, jsRgbToHsv, "rgbToHsv", 3));
        JS_SetPropertyStr(context, color, "cmykToRgb", JS_NewCFunction(context, jsCmykToRgb, "cmykToRgb", 4));
        JS_SetPropertyStr(context, color, "rgbToCmyk", JS_NewCFunction(context, jsRgbToCmyk, "rgbToCmyk", 3));
        JS_SetPropertyStr(context, color, "hctToRgb", JS_NewCFunction(context, jsHctToRgb, "hctToRgb", 3));
        JS_SetPropertyStr(context, color, "rgbToHct", JS_NewCFunction(context, jsRgbToHct, "rgbToHct", 3));
        JS_SetPropertyStr(context, color, "rgbaToRgb", JS_NewCFunction(context, jsRgbaToRgb, "rgbaToRgb", 4));
        JS_SetPropertyStr(context, color, "rgbToRgba", JS_NewCFunction(context, jsRgbToRgba, "rgbToRgba", 4));
        return color;
    }
}

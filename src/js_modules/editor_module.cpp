#include "js_modules.h"
#include "safe_commands.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_set>

#include "aced.h"
#include "acedads.h"
#include "adscodes.h"
#include "acutads.h"
#include "acutmem.h"
#include "acly.h"
#include "dbapserv.h"
#include "dbmain.h"
#include "dbsymtb.h"
#include "dbtrans.h"
#include "rxclass.h"
#include "dbcurve.h"
#include "dbents.h"
#include "dbhatch.h"
#include "dbmline.h"
#include "dbpl.h"
#include "dbregion.h"
#include "dbspline.h"
#include "gevec3d.h"
#include "text_util.h"
#include "js_module_utils.h"
#include "cad_units.h"

namespace js_modules
{
    namespace
    {
        std::string handleToString(const AcDbHandle &handle)
        {
            char text[32] = {};
            std::sprintf(text, "%llX", static_cast<unsigned long long>(static_cast<Adesk::UInt64>(handle)));
            return text;
        }

        JSValue handleToInteger(JSContext *context, const AcDbHandle &handle)
        {
            return JS_NewInt64(context, static_cast<int64_t>(static_cast<Adesk::UInt64>(handle)));
        }

        struct QsLayerSpec
        {
            const char *name;
            Adesk::UInt8 red;
            Adesk::UInt8 green;
            Adesk::UInt8 blue;
        };

        struct LayerSpec
        {
            std::string name;
            bool useColorIndex;
            int colorIndex;
            Adesk::UInt8 red;
            Adesk::UInt8 green;
            Adesk::UInt8 blue;
            double transparency;
        };

        const QsLayerSpec kQsLayers[] = {
            {"Qs.Stair", 255, 151, 158},
            {"Qs.Stair.Lantai", 243, 169, 105},
            {"Qs.Stair.Plint", 197, 192, 44},
            {"Qs.Stair.Border", 125, 209, 92},
            {"Qs.Stair.TrapDatar", 29, 214, 144},
            {"Qs.Stair.TrapTegak", 0, 214, 205},
            {"Qs.Stair.StepNosing", 0, 207, 255},
            {"Qs.Stair.Railing", 161, 184, 255},
            {"Qs.Stair.Balustrade", 238, 156, 255},
            {"Qs.Wall", 255, 151, 158},
            {"Qs.Wall.Ext.BataRingan", 243, 169, 105},
            {"Qs.Wall.Ext.BataMerah", 197, 192, 44},
            {"Qs.Wall.Int.BataRingan", 125, 209, 92},
            {"Qs.Wall.Int.BataMerah", 29, 214, 144},
            {"Qs.Wall.Int.BataMerahToilet", 0, 214, 205},
            {"Qs.WallFin", 255, 151, 158},
            {"Qs.WallFin.Plaster", 243, 169, 105},
            {"Qs.WallFin.Aci", 197, 192, 44},
            {"Qs.WallFin.Cat", 125, 209, 92},
            {"Qs.WallFin.Wallpaper", 29, 214, 144},
            {"Qs.WallFin.Keramik", 0, 214, 205},
            {"Qs.WallFin.HT", 0, 207, 255},
            {"Qs.WallFin.Marmer", 161, 184, 255},
            {"Qs.DW", 255, 151, 158},
            {"Qs.DW.P1", 243, 169, 105},
            {"Qs.DW.PJ1", 197, 192, 44},
            {"Qs.DW.J1", 125, 209, 92},
            {"Qs.FloorFIn", 255, 151, 158},
            {"Qs.CeilFin", 255, 151, 158},
            {"Qs.CeilFin.GB1", 243, 169, 105},
            {"Qs.CeilFin.GB2", 197, 192, 44},
            {"Qs.CeilFin.Drop.300", 125, 209, 92},
            {"Qs.CeilFin.Cove.200", 29, 214, 144},
            {"Qs.CeilFin.CurtainBox", 0, 214, 205},
            {"Qs.CeilFin.ShadowLine", 0, 207, 255},
            {"Qs.San", 197, 192, 44},
        };

        JSValue pointToArray(JSContext *context, const ads_point point)
        {
            JSValue array = JS_NewArray(context);
            JS_SetPropertyUint32(context, array, 0, JS_NewFloat64(context, point[0]));
            JS_SetPropertyUint32(context, array, 1, JS_NewFloat64(context, point[1]));
            JS_SetPropertyUint32(context, array, 2, JS_NewFloat64(context, point[2]));
            return array;
        }

        bool collectXDataBytes(AcDbEntity *entity, std::vector<uint8_t> &bytes)
        {
            resbuf *xdata = entity->xData(nullptr);
            if (xdata == nullptr)
            {
                return false;
            }

            const int maxIterations = 10000;
            const size_t maxChunk = 1000000; // 1MB
            int iterations = 0;
            std::unordered_set<resbuf *> seen;

            for (resbuf *rb = xdata; rb != nullptr && iterations < maxIterations; rb = rb->rbnext, ++iterations)
            {
                if (seen.find(rb) != seen.end())
                {
                    break; // cycle detected
                }
                seen.insert(rb);

                // defensive checks: only use binary chunks with valid buffer and sane length
                bool isXDataBinary = rb->restype == AcDb::kDxfXdBinaryChunk;
                bool isBinaryChunk = rb->restype >= AcDb::kDxfBinaryChunk && rb->restype <= AcDb::kDxfBinaryChunk + 9;
                if ((isXDataBinary || isBinaryChunk) && rb->resval.rbinary.buf != nullptr && rb->resval.rbinary.clen > 0)
                {
                    const uint8_t *chunk = reinterpret_cast<const uint8_t *>(rb->resval.rbinary.buf);
                    size_t clen = static_cast<size_t>(rb->resval.rbinary.clen);
                    if (clen > 0 && clen < maxChunk)
                    {
                        bytes.insert(bytes.end(), chunk, chunk + clen);
                    }
                }
            }

            acutRelRb(xdata);
            return !bytes.empty();
        }

        JSValue decodeXDataValue(JSContext *context, AcDbEntity *entity)
        {
            std::vector<uint8_t> bytes;
            if (!collectXDataBytes(entity, bytes))
            {
                return JS_NULL;
            }

            JSValue value = decodeMsgpackBytes(context, bytes.data(), bytes.size());
            if (JS_IsException(value))
            {
                JS_FreeValue(context, value);
                return JS_NULL;
            }
            return value;
        }

        std::string qsFilterNameForLayer(const char *layerName)
        {
            const std::string name(layerName != nullptr ? layerName : "");
            const size_t firstDot = name.find('.');
            if (firstDot == std::string::npos)
            {
                return name;
            }

            const size_t secondDot = name.find('.', firstDot + 1);
            if (secondDot == std::string::npos)
            {
                return name;
            }

            return name.substr(0, secondDot);
        }

        AcLyLayerFilter *findLayerFilterByName(AcLyLayerFilter *filter, const std::basic_string<ACHAR> &name)
        {
            if (filter == nullptr)
            {
                return nullptr;
            }

            const ACHAR *filterName = filter->name();
            if (filterName != nullptr && name == filterName)
            {
                return filter;
            }

            const AcArray<AcLyLayerFilter *> &nested = filter->getNestedFilters();
            for (int i = 0; i < nested.length(); ++i)
            {
                AcLyLayerFilter *match = findLayerFilterByName(nested[i], name);
                if (match != nullptr)
                {
                    return match;
                }
            }

            return nullptr;
        }

        const char *acadErrorName(Acad::ErrorStatus status)
        {
            switch (status)
            {
            case Acad::eOk:
                return "eOk";
            case Acad::eInvalidInput:
                return "eInvalidInput";
            case Acad::eNullObjectPointer:
                return "eNullObjectPointer";
            default:
                return "unknown";
            }
        }

        bool byteFromInt(int value, Adesk::UInt8 &out)
        {
            if (value < 0 || value > 255)
            {
                return false;
            }

            out = static_cast<Adesk::UInt8>(value);
            return true;
        }

        double clampDouble(double value, double minValue, double maxValue)
        {
            return std::max(minValue, std::min(maxValue, value));
        }

        int toByte(double value)
        {
            return static_cast<int>(clampDouble(std::floor(value + 0.5), 0.0, 255.0));
        }

        double normalizeHue(double hue)
        {
            hue = std::fmod(hue, 360.0);
            return hue < 0.0 ? hue + 360.0 : hue;
        }

        double normalizeUnit(double value)
        {
            return clampDouble(value > 1.0 ? value / 100.0 : value, 0.0, 1.0);
        }

        std::string toLowerAscii(std::string text)
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            }
            return text;
        }

        std::string trimAscii(const std::string &text)
        {
            size_t first = 0;
            while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
            {
                ++first;
            }

            size_t last = text.size();
            while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
            {
                --last;
            }

            return text.substr(first, last - first);
        }

        bool parseNumberList(const char *cursor, std::vector<double> &values)
        {
            while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
            {
                ++cursor;
            }

            if (*cursor != '(')
            {
                return false;
            }
            ++cursor;

            for (;;)
            {
                while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
                {
                    ++cursor;
                }

                char *end = nullptr;
                const double value = std::strtod(cursor, &end);
                if (end == cursor)
                {
                    return false;
                }

                values.push_back(value);
                cursor = end;

                while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
                {
                    ++cursor;
                }

                if (*cursor == ',')
                {
                    ++cursor;
                    continue;
                }

                if (*cursor == ')')
                {
                    ++cursor;
                    while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
                    {
                        ++cursor;
                    }
                    return *cursor == '\0';
                }

                return false;
            }
        }

        bool setRgbBytes(double redValue, double greenValue, double blueValue, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            return byteFromInt(toByte(redValue), red) &&
                   byteFromInt(toByte(greenValue), green) &&
                   byteFromInt(toByte(blueValue), blue);
        }

        bool hslToRgb(double hue, double saturation, double lightness, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            const double h = normalizeHue(hue) / 360.0;
            const double s = normalizeUnit(saturation);
            const double l = normalizeUnit(lightness);

            double r = l;
            double g = l;
            double b = l;
            if (s > 0.0)
            {
                const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
                const double p = 2.0 * l - q;
                auto hueToRgb = [](double pValue, double qValue, double t)
                {
                    if (t < 0.0)
                        t += 1.0;
                    if (t > 1.0)
                        t -= 1.0;
                    if (t < 1.0 / 6.0)
                        return pValue + (qValue - pValue) * 6.0 * t;
                    if (t < 1.0 / 2.0)
                        return qValue;
                    if (t < 2.0 / 3.0)
                        return pValue + (qValue - pValue) * (2.0 / 3.0 - t) * 6.0;
                    return pValue;
                };
                r = hueToRgb(p, q, h + 1.0 / 3.0);
                g = hueToRgb(p, q, h);
                b = hueToRgb(p, q, h - 1.0 / 3.0);
            }

            return setRgbBytes(r * 255.0, g * 255.0, b * 255.0, red, green, blue);
        }

        bool hsvToRgb(double hue, double saturation, double value, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            const double h = normalizeHue(hue) / 60.0;
            const double s = normalizeUnit(saturation);
            const double v = normalizeUnit(value);
            const int i = static_cast<int>(std::floor(h));
            const double f = h - i;
            const double p = v * (1.0 - s);
            const double q = v * (1.0 - s * f);
            const double t = v * (1.0 - s * (1.0 - f));

            double r = 0.0;
            double g = 0.0;
            double b = 0.0;
            switch (i % 6)
            {
            case 0:
                r = v;
                g = t;
                b = p;
                break;
            case 1:
                r = q;
                g = v;
                b = p;
                break;
            case 2:
                r = p;
                g = v;
                b = t;
                break;
            case 3:
                r = p;
                g = q;
                b = v;
                break;
            case 4:
                r = t;
                g = p;
                b = v;
                break;
            default:
                r = v;
                g = p;
                b = q;
                break;
            }

            return setRgbBytes(r * 255.0, g * 255.0, b * 255.0, red, green, blue);
        }

        bool cmykToRgb(double cyan, double magenta, double yellow, double key, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            const double c = normalizeUnit(cyan);
            const double m = normalizeUnit(magenta);
            const double y = normalizeUnit(yellow);
            const double k = normalizeUnit(key);
            return setRgbBytes(
                255.0 * (1.0 - c) * (1.0 - k),
                255.0 * (1.0 - m) * (1.0 - k),
                255.0 * (1.0 - y) * (1.0 - k),
                red, green, blue);
        }

        bool hctToRgb(double hue, double chroma, double tone, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            const double pi = 3.14159265358979323846;
            const double angle = normalizeHue(hue) * pi / 180.0;
            const double a = chroma * std::cos(angle);
            const double b = chroma * std::sin(angle);
            const double fy = (tone + 16.0) / 116.0;
            const double fx = fy + a / 500.0;
            const double fz = fy - b / 200.0;
            auto labPivot = [](double t)
            {
                const double t3 = t * t * t;
                return t3 > 0.008856 ? t3 : (t - 16.0 / 116.0) / 7.787;
            };

            double x = 95.047 * labPivot(fx) / 100.0;
            double y = 100.000 * labPivot(fy) / 100.0;
            double z = 108.883 * labPivot(fz) / 100.0;

            double r = x * 3.2406 + y * -1.5372 + z * -0.4986;
            double g = x * -0.9689 + y * 1.8758 + z * 0.0415;
            double bl = x * 0.0557 + y * -0.2040 + z * 1.0570;
            auto gamma = [](double channel)
            {
                channel = std::max(0.0, channel);
                return channel <= 0.0031308 ? 12.92 * channel : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
            };

            return setRgbBytes(gamma(r) * 255.0, gamma(g) * 255.0, gamma(bl) * 255.0, red, green, blue);
        }

        bool parseHexColor(const std::string &text, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            if (text.size() != 4 && text.size() != 7)
            {
                return false;
            }

            if (text[0] != '#')
            {
                return false;
            }

            auto hexValue = [](char ch) -> int
            {
                if (ch >= '0' && ch <= '9')
                    return ch - '0';
                if (ch >= 'a' && ch <= 'f')
                    return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F')
                    return ch - 'A' + 10;
                return -1;
            };

            if (text.size() == 4)
            {
                const int r = hexValue(text[1]);
                const int g = hexValue(text[2]);
                const int b = hexValue(text[3]);
                if (r < 0 || g < 0 || b < 0)
                {
                    return false;
                }
                red = static_cast<Adesk::UInt8>(r * 17);
                green = static_cast<Adesk::UInt8>(g * 17);
                blue = static_cast<Adesk::UInt8>(b * 17);
                return true;
            }

            const int r1 = hexValue(text[1]);
            const int r2 = hexValue(text[2]);
            const int g1 = hexValue(text[3]);
            const int g2 = hexValue(text[4]);
            const int b1 = hexValue(text[5]);
            const int b2 = hexValue(text[6]);
            if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
            {
                return false;
            }

            red = static_cast<Adesk::UInt8>(r1 * 16 + r2);
            green = static_cast<Adesk::UInt8>(g1 * 16 + g2);
            blue = static_cast<Adesk::UInt8>(b1 * 16 + b2);
            return true;
        }

        bool readByteProperty(JSContext *context, JSValueConst object, const char *name, Adesk::UInt8 &out, std::string &error)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            if (JS_IsException(value))
            {
                error = std::string("failed to read color.") + name;
                return false;
            }

            int intValue = 0;
            const int status = JS_ToInt32(context, &intValue, value);
            JS_FreeValue(context, value);
            if (status < 0 || !byteFromInt(intValue, out))
            {
                error = std::string("color.") + name + " must be an integer from 0 to 255";
                return false;
            }

            return true;
        }

        bool parseColorString(const std::string &rawText, LayerSpec &spec, std::string &error)
        {
            const std::string text = trimAscii(rawText);
            const std::string lower = toLowerAscii(text);

            if (parseHexColor(text, spec.red, spec.green, spec.blue))
            {
                spec.useColorIndex = false;
                return true;
            }

            const size_t open = lower.find('(');
            const size_t close = lower.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close < open)
            {
                error = "color string must be rgb(...), hsl(...), hsv(...), cmyk(...), hct(...), or #rrggbb";
                return false;
            }

            const std::string space = trimAscii(lower.substr(0, open));
            std::vector<double> values;
            if (!parseNumberList(lower.c_str() + open, values))
            {
                error = "color string contains invalid numeric channels";
                return false;
            }

            bool ok = false;
            if (space == "rgb" && values.size() == 3)
            {
                ok = setRgbBytes(values[0], values[1], values[2], spec.red, spec.green, spec.blue);
            }
            else if (space == "hsl" && values.size() == 3)
            {
                ok = hslToRgb(values[0], values[1], values[2], spec.red, spec.green, spec.blue);
            }
            else if (space == "hsv" && values.size() == 3)
            {
                ok = hsvToRgb(values[0], values[1], values[2], spec.red, spec.green, spec.blue);
            }
            else if (space == "cmyk" && values.size() == 4)
            {
                ok = cmykToRgb(values[0], values[1], values[2], values[3], spec.red, spec.green, spec.blue);
            }
            else if (space == "hct" && values.size() == 3)
            {
                ok = hctToRgb(values[0], values[1], values[2], spec.red, spec.green, spec.blue);
            }
            else
            {
                error = "unsupported color string or wrong channel count";
                return false;
            }

            spec.useColorIndex = false;
            if (!ok)
            {
                error = "color channels are out of range";
            }
            return ok;
        }

        bool parseColorValue(JSContext *context, JSValueConst value, LayerSpec &spec, std::string &error)
        {
            if (JS_IsString(value))
            {
                const std::string text = valueToString(context, value);
                return parseColorString(text, spec, error);
            }

            if (JS_IsNumber(value))
            {
                int colorIndex = 0;
                if (JS_ToInt32(context, &colorIndex, value) < 0 || colorIndex < 0 || colorIndex > 256)
                {
                    error = "numeric color must be an AutoCAD color index from 0 to 256";
                    return false;
                }

                spec.useColorIndex = true;
                spec.colorIndex = colorIndex;
                return true;
            }

            if (JS_IsObject(value))
            {
                spec.useColorIndex = false;
                return readByteProperty(context, value, "r", spec.red, error) &&
                       readByteProperty(context, value, "g", spec.green, error) &&
                       readByteProperty(context, value, "b", spec.blue, error);
            }

            error = "color must be a color string, AutoCAD color index, or { r, g, b } object";
            return false;
        }

        bool readTransparency(JSContext *context, JSValueConst object, LayerSpec &spec, std::string &error)
        {
            spec.transparency = 0.0;

            JSValue value = JS_GetPropertyStr(context, object, "trp");
            if (JS_IsException(value))
            {
                error = "failed to read transparency property";
                return false;
            }

            if (JS_IsUndefined(value) || JS_IsNull(value))
            {
                JS_FreeValue(context, value);
                return true;
            }

            double transparency = 0.0;
            const int status = JS_ToFloat64(context, &transparency, value);
            JS_FreeValue(context, value);
            if (status < 0 || transparency < 0.0 || transparency > 100.0)
            {
                error = "transparency must be a number from 0 to 100";
                return false;
            }

            spec.transparency = transparency * 100.0;
            return true;
        }

        bool readLayerSpec(JSContext *context, JSValueConst value, LayerSpec &spec, std::string &error)
        {
            if (!JS_IsObject(value))
            {
                error = "each layer item must be an object";
                return false;
            }

            JSValue layerValue = JS_GetPropertyStr(context, value, "layer");
            if (JS_IsException(layerValue))
            {
                error = "failed to read layer property";
                return false;
            }

            if (!JS_IsString(layerValue))
            {
                JS_FreeValue(context, layerValue);
                error = "layer property must be a string";
                return false;
            }

            spec.name = valueToString(context, layerValue);
            JS_FreeValue(context, layerValue);

            JSValue colorValue = JS_GetPropertyStr(context, value, "color");
            if (JS_IsException(colorValue))
            {
                error = "failed to read color property";
                return false;
            }

            const bool ok = parseColorValue(context, colorValue, spec, error);
            JS_FreeValue(context, colorValue);
            return ok && readTransparency(context, value, spec, error);
        }

        JSValue specsFromJsArray(JSContext *context, JSValueConst value, std::vector<LayerSpec> &specs)
        {
            if (!JS_IsArray(value))
            {
                return JS_ThrowTypeError(context, "editor.createLayers expects an array of { layer, color } objects");
            }

            JSValue lengthValue = JS_GetPropertyStr(context, value, "length");
            if (JS_IsException(lengthValue))
            {
                return lengthValue;
            }

            uint32_t length = 0;
            const int lengthStatus = JS_ToUint32(context, &length, lengthValue);
            JS_FreeValue(context, lengthValue);
            if (lengthStatus < 0)
            {
                return JS_EXCEPTION;
            }

            specs.clear();
            specs.reserve(length);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                if (JS_IsException(item))
                {
                    return item;
                }

                LayerSpec spec;
                std::string error;
                const bool ok = readLayerSpec(context, item, spec, error);
                JS_FreeValue(context, item);
                if (!ok)
                {
                    return JS_ThrowTypeError(context, "editor.createLayers item %u: %s", static_cast<unsigned int>(i), error.c_str());
                }

                specs.push_back(spec);
            }

            return JS_UNDEFINED;
        }

        std::vector<LayerSpec> builtInQsLayerSpecs()
        {
            std::vector<LayerSpec> specs;
            specs.reserve(sizeof(kQsLayers) / sizeof(kQsLayers[0]));
            for (size_t i = 0; i < sizeof(kQsLayers) / sizeof(kQsLayers[0]); ++i)
            {
                LayerSpec spec;
                spec.name = kQsLayers[i].name;
                spec.useColorIndex = false;
                spec.colorIndex = 0;
                spec.red = kQsLayers[i].red;
                spec.green = kQsLayers[i].green;
                spec.blue = kQsLayers[i].blue;
                spec.transparency = 0.0;
                specs.push_back(spec);
            }
            return specs;
        }

        Acad::ErrorStatus upsertLayer(AcDbLayerTable *layerTable, const LayerSpec &spec, bool &created)
        {
            created = false;
            if (layerTable == nullptr)
            {
                return Acad::eNullObjectPointer;
            }

            AcCmColor color;
            Acad::ErrorStatus status = spec.useColorIndex
                                           ? color.setColorIndex(static_cast<Adesk::Int16>(spec.colorIndex))
                                           : color.setRGB(spec.red, spec.green, spec.blue);
            if (status != Acad::eOk)
            {
                return status;
            }

            AcCmTransparency transparency;
            transparency.setAlpha(static_cast<Adesk::UInt8>(toByte((100.0 - spec.transparency) * 255.0 / 100.0)));

            const std::basic_string<ACHAR> layerName = command_text::fromUtf8(spec.name);
            if (layerTable->has(layerName.c_str()))
            {
                AcDbLayerTableRecord *record = nullptr;
                status = layerTable->getAt(layerName.c_str(), record, AcDb::kForWrite);
                if (status != Acad::eOk)
                {
                    return status;
                }

                record->setColor(color);
                status = record->setTransparency(transparency);
                if (status != Acad::eOk)
                {
                    record->close();
                    return status;
                }

                record->close();
                return Acad::eOk;
            }

            AcDbLayerTableRecord *record = new AcDbLayerTableRecord;
            status = record->setName(layerName.c_str());
            if (status != Acad::eOk)
            {
                delete record;
                return status;
            }

            record->setIsFrozen(0);
            record->setIsOff(0);
            record->setVPDFLT(0);
            record->setIsLocked(0);
            record->setColor(color);
            status = record->setTransparency(transparency);
            if (status != Acad::eOk)
            {
                delete record;
                return status;
            }

            status = layerTable->add(record);
            if (status != Acad::eOk)
            {
                delete record;
                return status;
            }

            created = true;
            record->close();
            return Acad::eOk;
        }

        Acad::ErrorStatus upsertLayerFilter(AcLyLayerFilter *root, const std::string &filterName, bool &created)
        {
            created = false;
            if (root == nullptr)
            {
                return Acad::eNullObjectPointer;
            }

            const std::basic_string<ACHAR> nativeName = command_text::fromUtf8(filterName);
            const std::string query = "NAME==" + filterName + ".*";
            const std::basic_string<ACHAR> nativeQuery = command_text::fromUtf8(query);

            AcLyLayerFilter *filter = findLayerFilterByName(root, nativeName);
            if (filter == nullptr)
            {
                filter = new AcLyLayerFilter();
                Acad::ErrorStatus status = filter->setName(nativeName.c_str());
                if (status != Acad::eOk)
                {
                    delete filter;
                    return status;
                }

                status = filter->setFilterExpression(nativeQuery.c_str());
                if (status != Acad::eOk)
                {
                    delete filter;
                    return status;
                }

                status = root->addNested(filter);
                if (status != Acad::eOk)
                {
                    delete filter;
                    return status;
                }

                created = true;
                return Acad::eOk;
            }

            return filter->setFilterExpression(nativeQuery.c_str());
        }

        JSValue colorToJs(JSContext *context, const AcCmColor &color)
        {
            if (color.isByBlock())
            {
                return JS_NewInt32(context, 0);
            }

            if (color.isByLayer())
            {
                return JS_NewInt32(context, 256);
            }

            if (color.isByACI())
            {
                return JS_NewInt32(context, color.colorIndex());
            }

            JSValue rgb = JS_NewObject(context);
            JS_SetPropertyStr(context, rgb, "r", JS_NewInt32(context, color.red()));
            JS_SetPropertyStr(context, rgb, "g", JS_NewInt32(context, color.green()));
            JS_SetPropertyStr(context, rgb, "b", JS_NewInt32(context, color.blue()));
            return rgb;
        }

        JSValue transparencyToJs(JSContext *context, const AcCmTransparency &transparency)
        {
            if (transparency.isByBlock())
            {
                return JS_NewInt32(context, -1);
            }

            if (transparency.isByLayer())
            {
                return JS_NewInt32(context, -2);
            }

            return JS_NewFloat64(context, ( 100.0 - transparency.alphaPercent() * 100.0));
        }

        bool isPolylineOrSpline(AcRxClass *objectClass)
        {
            return objectClass != nullptr &&
                   (objectClass->isDerivedFrom(AcDbPolyline::desc()) ||
                    objectClass->isDerivedFrom(AcDbSpline::desc()));
        }

        bool isCurveClosedForArea(AcDbCurve *curve, AcRxClass *objectClass)
        {
            if (curve->isClosed())
            {
                return true;
            }

            if (!isPolylineOrSpline(objectClass))
            {
                return false;
            }

            AcGePoint3d startPoint;
            AcGePoint3d endPoint;
            return curve->getStartPoint(startPoint) == Acad::eOk &&
                   curve->getEndPoint(endPoint) == Acad::eOk &&
                   startPoint.isEqualTo(endPoint);
        }

        void addCurveMetrics(JSContext *context, JSValueConst descriptor, AcDbCurve *curve, AcRxClass *objectClass, int roundDigits)
        {
            double endParam = 0.0;
            double length = 0.0;
            if (curve->getEndParam(endParam) == Acad::eOk &&
                curve->getDistAtParam(endParam, length) == Acad::eOk)
            {
                const double lScaled = roundToPrecision(length, roundDigits);
                JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, lScaled));
                JS_DefinePropertyValueStr(context, descriptor, "length", JS_NewFloat64(context, lScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }

            if (isCurveClosedForArea(curve, objectClass))
            {
                double area = 0.0;
                if (curve->getArea(area) == Acad::eOk)
                {
                    const double aScaled = roundToPrecision(area, roundDigits);
                    JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, aScaled));
                    JS_DefinePropertyValueStr(context, descriptor, "area", JS_NewFloat64(context, aScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
                }
            }
        }

        void addRegionMetrics(JSContext *context, JSValueConst descriptor, AcDbRegion *region, int roundDigits)
        {
            double perimeter = 0.0;
            if (region->getPerimeter(perimeter) == Acad::eOk)
            {
                const double pScaled = roundToPrecision(perimeter, roundDigits);
                JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, pScaled));
                JS_DefinePropertyValueStr(context, descriptor, "length", JS_NewFloat64(context, pScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }

            double area = 0.0;
            if (region->getArea(area) == Acad::eOk)
            {
                const double aScaled = roundToPrecision(area, roundDigits);
                JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, aScaled));
                JS_DefinePropertyValueStr(context, descriptor, "area", JS_NewFloat64(context, aScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }
        }

        void addHatchMetrics(JSContext *context, JSValueConst descriptor, AcDbHatch *hatch, int roundDigits)
        {
            double area = 0.0;
            if (hatch->getArea(area) == Acad::eOk)
            {
                const double aScaled = roundToPrecision(area, roundDigits);
                JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, aScaled));
                JS_DefinePropertyValueStr(context, descriptor, "area", JS_NewFloat64(context, aScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }

            AcDbRegion *region = hatch->getRegionArea();
            if (region != nullptr)
            {
                double perimeter = 0.0;
                if (region->getPerimeter(perimeter) == Acad::eOk)
                {
                    const double pScaled = roundToPrecision(perimeter, roundDigits);
                    JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, pScaled));
                    JS_DefinePropertyValueStr(context, descriptor, "length", JS_NewFloat64(context, pScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
                }
                delete region;
            }
        }

        void addMlineMetrics(JSContext *context, JSValueConst descriptor, AcDbMline *mline, int roundDigits)
        {
            const int vertexCount = mline->numVertices();
            if (vertexCount <= 0)
            {
                return;
            }

            const AcGePoint3d firstPoint = mline->vertexAt(0);
            const AcGePoint3d lastPoint = mline->vertexAt(vertexCount - 1);

            if (vertexCount >= 2)
            {
                double length = 0.0;
                AcGePoint3d previous = firstPoint;
                for (int i = 1; i < vertexCount; ++i)
                {
                    const AcGePoint3d current = mline->vertexAt(i);
                    length += previous.distanceTo(current);
                    previous = current;
                }

                if (mline->closedMline())
                {
                    length += lastPoint.distanceTo(firstPoint);
                }

                const double lScaled = roundToPrecision(length, roundDigits);
                JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, lScaled));
                JS_DefinePropertyValueStr(context, descriptor, "length", JS_NewFloat64(context, lScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }

            if (mline->closedMline() && vertexCount >= 3)
            {
                AcGeVector3d areaVector(0.0, 0.0, 0.0);
                for (int i = 1; i + 1 < vertexCount; ++i)
                {
                    const AcGeVector3d edge1 = mline->vertexAt(i) - firstPoint;
                    const AcGeVector3d edge2 = mline->vertexAt(i + 1) - firstPoint;
                    areaVector += edge1.crossProduct(edge2);
                }
                const double aScaled = roundToPrecision(0.5 * areaVector.length(), roundDigits);
                JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, aScaled));
                JS_DefinePropertyValueStr(context, descriptor, "area", JS_NewFloat64(context, aScaled), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }
        }

        void addBlockReferenceProperties(JSContext *context, JSValueConst descriptor, AcDbBlockReference *blockReference)
        {
            std::string blockName;
            AcDbBlockTableRecord *blockRecord = nullptr;
            if (acdbOpenObject(blockRecord, blockReference->blockTableRecord(), AcDb::kForRead) == Acad::eOk &&
                blockRecord != nullptr)
            {
                ACHAR *name = nullptr;
                if (blockRecord->getName(name) == Acad::eOk && name != nullptr)
                {
                    blockName = command_text::toUtf8(name);
                    acutDelString(name);
                }
                blockRecord->close();
            }
            JS_SetPropertyStr(context, descriptor, "insert", JS_NewString(context, blockName.c_str()));

            JSValue attributes = JS_NewObject(context);
            AcDbObjectIterator *iterator = blockReference->attributeIterator();
            if (iterator != nullptr)
            {
                for (; !iterator->done(); iterator->step())
                {
                    AcDbAttribute *attribute = nullptr;
                    if (blockReference->openAttribute(attribute, iterator->objectId(), AcDb::kForRead) != Acad::eOk ||
                        attribute == nullptr)
                    {
                        continue;
                    }

                    const ACHAR *tag = attribute->tagConst();
                    const ACHAR *value = attribute->textStringConst();
                    if (tag != nullptr)
                    {
                        JS_SetPropertyStr(context, attributes, command_text::toUtf8(tag).c_str(), JS_NewString(context, value != nullptr ? command_text::toUtf8(value).c_str() : ""));
                    }
                    attribute->close();
                }
                delete iterator;
            }
            JS_SetPropertyStr(context, descriptor, "attr", attributes);
        }

        void addEntityProperties(JSContext *context, JSValueConst descriptor, AcDbEntity *entity)
        {
            const int roundDigits = getGlobalRoundDigits(context, 3);

            AcRxClass *objectClass = entity->isA();
            if (objectClass != nullptr)
            {
                const ACHAR *className = objectClass->name();
                JS_SetPropertyStr(context, descriptor, "ty", JS_NewString(context, className != nullptr ? command_text::toUtf8(className).c_str() : ""));
                if (objectClass->isDerivedFrom(AcDbCurve::desc()))
                {
                    addCurveMetrics(context, descriptor, static_cast<AcDbCurve *>(entity), objectClass, roundDigits);
                }
                else if (objectClass->isDerivedFrom(AcDbRegion::desc()))
                {
                    addRegionMetrics(context, descriptor, static_cast<AcDbRegion *>(entity), roundDigits);
                }
                else if (objectClass->isDerivedFrom(AcDbHatch::desc()))
                {
                    addHatchMetrics(context, descriptor, static_cast<AcDbHatch *>(entity), roundDigits);
                }
                else if (objectClass->isDerivedFrom(AcDbMline::desc()))
                {
                    addMlineMetrics(context, descriptor, static_cast<AcDbMline *>(entity), roundDigits);
                }
                else if (objectClass->isDerivedFrom(AcDbBlockReference::desc()))
                {
                    addBlockReferenceProperties(context, descriptor, static_cast<AcDbBlockReference *>(entity));
                }
            }

            JS_SetPropertyStr(context, descriptor, "clr", colorToJs(context, entity->color()));
            JS_SetPropertyStr(context, descriptor, "trp", transparencyToJs(context, entity->transparency()));

            ACHAR *layer = entity->layer();
            JS_SetPropertyStr(context, descriptor, "lyr", JS_NewString(context, layer != nullptr ? command_text::toUtf8(layer).c_str() : ""));

            if (layer != nullptr)
            {
                acutDelString(layer);
            }

            JSValue xdataVal = decodeXDataValue(context, entity);
            if (!JS_IsNull(xdataVal) && !JS_IsUndefined(xdataVal))
            {
                JS_SetPropertyStr(context, descriptor, "data", JS_DupValue(context, xdataVal));
            }
            JS_FreeValue(context, xdataVal);
        }

        JSValue entityToObject(JSContext *context, const ads_name entityName, const ads_point *pickPoint)
        {
            AcDbObjectId objectId;
            const Acad::ErrorStatus status = acdbGetObjectId(objectId, entityName);
            if (status != Acad::eOk)
            {
                return JS_ThrowInternalError(context, "failed to get selected entity id: %d", status);
            }

            JSValue entity = JS_NewObject(context);
            const AcDbHandle handle = objectId.handle();
            JS_SetPropertyStr(context, entity, "id", handleToInteger(context, handle));
            JS_SetPropertyStr(context, entity, "hand", JS_NewString(context, handleToString(handle).c_str()));

            AcDbEntity *dbEntity = nullptr;
            AcDbDatabase *workingDb = acdbHostApplicationServices()->workingDatabase();
            AcDbTransactionManager *txManager = workingDb != nullptr ? workingDb->transactionManager() : nullptr;
            AcTransaction *transaction = nullptr;
            Acad::ErrorStatus openStatus = Acad::eOk;

            if (txManager != nullptr)
            {
                transaction = txManager->topTransaction();
                if (transaction != nullptr)
                {
                    // Use existing global transaction
                    AcDbObject *dbObject = nullptr;
                    openStatus = transaction->getObject(dbObject, objectId, AcDb::kForRead);
                    dbEntity = static_cast<AcDbEntity *>(dbObject);
                }
                else
                {
                    // No active transaction, use direct open
                    openStatus = acdbOpenObject(dbEntity, objectId, AcDb::kForRead);
                }
            }
            else
            {
                openStatus = acdbOpenObject(dbEntity, objectId, AcDb::kForRead);
            }

            if (openStatus == Acad::eOk && dbEntity != nullptr)
            {
                addEntityProperties(context, entity, dbEntity);
                // Don't close/end transaction - let caller manage it via global tm
                if (transaction == nullptr && dbEntity != nullptr)
                {
                    dbEntity->close();
                }
            }
            else
            {
                JS_SetPropertyStr(context, entity, "ty", JS_NewString(context, ""));
                JS_SetPropertyStr(context, entity, "clr", JS_NewInt32(context, 256));
                JS_SetPropertyStr(context, entity, "trp", JS_NewInt32(context, -2));
                JS_SetPropertyStr(context, entity, "lyr", JS_NewString(context, ""));
                JS_SetPropertyStr(context, entity, "data", JS_NULL);
            }

            // if (pickPoint != nullptr)
            // {
            //     JS_SetPropertyStr(context, entity, "pickPoint", pointToArray(context, *pickPoint));
            // }

            return entity;
        }

        JSValue passesFilter(JSContext *context, JSValueConst filter, JSValueConst entity, bool &accepted)
        {
            accepted = true;
            if (!JS_IsFunction(context, filter))
            {
                return JS_UNDEFINED;
            }

            JSValue args[] = {JS_DupValue(context, entity)};
            JSValue result = JS_Call(context, filter, JS_UNDEFINED, 1, args);
            JS_FreeValue(context, args[0]);
            if (JS_IsException(result))
            {
                return result;
            }

            accepted = JS_ToBool(context, result) != 0;
            JS_FreeValue(context, result);
            return JS_UNDEFINED;
        }

        JSValue selectSingle(JSContext *context, const std::basic_string<ACHAR> &prompt, JSValueConst filter, bool useImplied = true, bool onlyImplied = false)
        {
            if (useImplied)
            {
                ads_name selectionSet;
                acdbNameClear(selectionSet);
                const int status = acedSSGet(ACRX_T("I"), nullptr, nullptr, nullptr, selectionSet);
                if (status == RTNORM)
                {
                    Adesk::Int32 length = 0;
                    if (acedSSLength(selectionSet, &length) == RTNORM && length > 0)
                    {
                        for (Adesk::Int32 i = 0; i < length; ++i)
                        {
                            ads_name entityName;
                            if (acedSSName(selectionSet, i, entityName) != RTNORM)
                            {
                                continue;
                            }

                            JSValue entity = entityToObject(context, entityName, nullptr);
                            if (JS_IsException(entity))
                            {
                                acedSSFree(selectionSet);
                                return entity;
                            }

                            bool accepted = true;
                            JSValue error = passesFilter(context, filter, entity, accepted);
                            if (JS_IsException(error))
                            {
                                JS_FreeValue(context, entity);
                                acedSSFree(selectionSet);
                                return error;
                            }

                            if (accepted)
                            {
                                ads_name empty;
                                acdbNameClear(empty);
                                acedSSSetFirst(empty, empty);
                                acedSSFree(selectionSet);
                                return entity;
                            }

                            JS_FreeValue(context, entity);
                        }
                    }
                    acedSSFree(selectionSet);
                }

                if (onlyImplied)
                {
                    return JS_NULL;
                }
            }

            for (;;)
            {
                ads_name entityName;
                ads_point pickPoint;
                const int status = acedEntSel(prompt.c_str(), entityName, pickPoint);
                if (status == RTCAN || status == RTNONE)
                {
                    return JS_NULL;
                }

                if (status != RTNORM)
                {
                    return JS_ThrowInternalError(context, "editor.select failed with status %d", status);
                }

                JSValue entity = entityToObject(context, entityName, &pickPoint);
                if (JS_IsException(entity))
                {
                    return entity;
                }

                bool accepted = true;
                JSValue error = passesFilter(context, filter, entity, accepted);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, entity);
                    return error;
                }

                if (accepted)
                {
                    return entity;
                }

                JS_FreeValue(context, entity);
                acutPrintf(ACRX_T("\nSelected entity was rejected by filter."));
            }
        }

        JSValue selectMultiple(JSContext *context, const std::basic_string<ACHAR> &prompt, JSValueConst filter, bool useImplied = true, bool onlyImplied = false)
        {
            ads_name selectionSet;
            acdbNameClear(selectionSet);
            int status = RTERROR;
            bool fromImplied = false;

            if (useImplied)
            {
                status = acedSSGet(ACRX_T("I"), nullptr, nullptr, nullptr, selectionSet);
                if (status == RTNORM)
                {
                    Adesk::Int32 length = 0;
                    if (acedSSLength(selectionSet, &length) == RTNORM && length > 0)
                    {
                        fromImplied = true;
                        ads_name empty;
                        acdbNameClear(empty);
                        acedSSSetFirst(empty, empty);
                    }
                    else
                    {
                        acedSSFree(selectionSet);
                        acdbNameClear(selectionSet);
                        status = RTERROR;
                    }
                }
            }

            if (!fromImplied)
            {
                if (onlyImplied)
                {
                    return JS_NewArray(context);
                }

                if (!prompt.empty())
                {
                    acutPrintf(ACRX_T("\n%s"), prompt.c_str());
                }

                status = acedSSGet(nullptr, nullptr, nullptr, nullptr, selectionSet);
                if (status == RTCAN || status == RTNONE)
                {
                    return JS_NewArray(context);
                }

                if (status != RTNORM)
                {
                    return JS_ThrowInternalError(context, "editor.select failed with status %d", status);
                }
            }

            Adesk::Int32 length = 0;
            if (acedSSLength(selectionSet, &length) != RTNORM)
            {
                acedSSFree(selectionSet);
                return JS_ThrowInternalError(context, "failed to read selection set length");
            }

            JSValue array = JS_NewArray(context);
            uint32_t acceptedIndex = 0;
            for (Adesk::Int32 i = 0; i < length; ++i)
            {
                ads_name entityName;
                if (acedSSName(selectionSet, i, entityName) != RTNORM)
                {
                    continue;
                }

                JSValue entity = entityToObject(context, entityName, nullptr);
                if (JS_IsException(entity))
                {
                    acedSSFree(selectionSet);
                    return entity;
                }

                bool accepted = true;
                JSValue error = passesFilter(context, filter, entity, accepted);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, entity);
                    acedSSFree(selectionSet);
                    return error;
                }

                if (accepted)
                {
                    JS_SetPropertyUint32(context, array, acceptedIndex++, entity);
                }
                else
                {
                    JS_FreeValue(context, entity);
                }
            }

            acedSSFree(selectionSet);
            return array;
        }

        JSValue jsEditorSelect(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string prompt = "Select an entity: ";
            std::string selection = "single";
            JSValueConst filter = JS_UNDEFINED;
            JSValue allocatedFilter = JS_UNDEFINED;
            bool useImplied = true;
            bool onlyImplied = false;

            if (argc >= 1 && JS_IsObject(argv[0]) && !JS_IsFunction(context, argv[0]))
            {
                JSValueConst options = argv[0];
                JSValue pVal = JS_GetPropertyStr(context, options, "prompt");
                if (!JS_IsUndefined(pVal) && !JS_IsNull(pVal))
                {
                    prompt = valueToString(context, pVal);
                }
                JS_FreeValue(context, pVal);

                JSValue mVal = JS_GetPropertyStr(context, options, "mode");
                if (JS_IsUndefined(mVal) || JS_IsNull(mVal))
                {
                    mVal = JS_GetPropertyStr(context, options, "selectionMode");
                }
                if (!JS_IsUndefined(mVal) && !JS_IsNull(mVal))
                {
                    selection = valueToString(context, mVal);
                }
                JS_FreeValue(context, mVal);

                JSValue iVal = JS_GetPropertyStr(context, options, "implied");
                if (!JS_IsUndefined(iVal) && !JS_IsNull(iVal))
                {
                    useImplied = JS_ToBool(context, iVal) != 0;
                }
                JS_FreeValue(context, iVal);

                JSValue oiVal = JS_GetPropertyStr(context, options, "onlyImplied");
                if (!JS_IsUndefined(oiVal) && !JS_IsNull(oiVal))
                {
                    onlyImplied = JS_ToBool(context, oiVal) != 0;
                }
                JS_FreeValue(context, oiVal);

                JSValue fVal = JS_GetPropertyStr(context, options, "filter");
                if (JS_IsUndefined(fVal) || JS_IsNull(fVal))
                {
                    fVal = JS_GetPropertyStr(context, options, "filterFn");
                }
                if (!JS_IsUndefined(fVal) && !JS_IsNull(fVal))
                {
                    if (!JS_IsFunction(context, fVal))
                    {
                        JS_FreeValue(context, fVal);
                        return JS_ThrowTypeError(context, "editor.select filter must be a function");
                    }
                    allocatedFilter = fVal;
                    filter = fVal;
                }
                else
                {
                    JS_FreeValue(context, fVal);
                }
            }
            else
            {
                int argIdx = 0;
                if (argIdx < argc && !JS_IsUndefined(argv[argIdx]))
                {
                    if (JS_IsFunction(context, argv[argIdx]))
                    {
                        filter = argv[argIdx];
                    }
                    else
                    {
                        prompt = valueToString(context, argv[argIdx]);
                    }
                    argIdx++;
                }

                if (argIdx < argc && !JS_IsUndefined(argv[argIdx]))
                {
                    if (JS_IsFunction(context, argv[argIdx]))
                    {
                        filter = argv[argIdx];
                    }
                    else if (JS_IsBool(argv[argIdx]))
                    {
                        useImplied = JS_ToBool(context, argv[argIdx]) != 0;
                    }
                    else
                    {
                        selection = valueToString(context, argv[argIdx]);
                    }
                    argIdx++;
                }

                if (argIdx < argc && !JS_IsUndefined(argv[argIdx]))
                {
                    if (JS_IsFunction(context, argv[argIdx]))
                    {
                        filter = argv[argIdx];
                    }
                    else if (JS_IsBool(argv[argIdx]))
                    {
                        useImplied = JS_ToBool(context, argv[argIdx]) != 0;
                    }
                    argIdx++;
                }

                if (argIdx < argc && !JS_IsUndefined(argv[argIdx]))
                {
                    if (JS_IsBool(argv[argIdx]))
                    {
                        useImplied = JS_ToBool(context, argv[argIdx]) != 0;
                    }
                }
            }

            if (selection == "implied")
            {
                selection = "multiple";
                useImplied = true;
                onlyImplied = true;
            }

            const std::basic_string<ACHAR> nativePrompt = command_text::fromUtf8(prompt);
            JSValue result = JS_UNDEFINED;
            if (selection == "single")
            {
                result = selectSingle(context, nativePrompt, filter, useImplied, onlyImplied);
            }
            else if (selection == "multiple")
            {
                result = selectMultiple(context, nativePrompt, filter, useImplied, onlyImplied);
            }
            else
            {
                result = JS_ThrowTypeError(context, "editor.select selection must be 'single', 'multiple', or 'implied'");
            }

            if (!JS_IsUndefined(allocatedFilter))
            {
                JS_FreeValue(context, allocatedFilter);
            }

            return result;
        }

        JSValue createLayersFromSpecs(JSContext *context, const std::vector<LayerSpec> &specs)
        {
            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return JS_ThrowInternalError(context, "no active AutoCAD database");
            }

            AcDbLayerTable *layerTable = nullptr;
            Acad::ErrorStatus status = db->getSymbolTable(layerTable, AcDb::kForWrite);
            if (status != Acad::eOk)
            {
                return JS_ThrowInternalError(context, "failed to open layer table for write: %d", status);
            }

            int createdLayers = 0;
            int updatedLayers = 0;
            std::vector<std::string> filterNames;
            std::unordered_set<std::string> seenFilters;

            for (size_t i = 0; i < specs.size(); ++i)
            {
                bool created = false;
                status = upsertLayer(layerTable, specs[i], created);
                if (status != Acad::eOk)
                {
                    layerTable->close();
                    return JS_ThrowInternalError(context, "failed to create or update layer '%s': %d", specs[i].name.c_str(), status);
                }

                if (created)
                {
                    ++createdLayers;
                }
                else
                {
                    ++updatedLayers;
                }

                const std::string filterName = qsFilterNameForLayer(specs[i].name.c_str());
                if (seenFilters.insert(filterName).second)
                {
                    filterNames.push_back(filterName);
                }
            }

            layerTable->close();

            AcLyLayerFilterManager *filterManager = aclyGetLayerFilterManager(db);
            if (filterManager == nullptr)
            {
                return JS_ThrowInternalError(context, "failed to get AutoCAD layer filter manager");
            }

            AcLyLayerFilter *rootFilter = nullptr;
            AcLyLayerFilter *currentFilter = nullptr;
            status = filterManager->getFilters(rootFilter, currentFilter);
            if (status != Acad::eOk || rootFilter == nullptr)
            {
                if (rootFilter != nullptr)
                {
                    delete rootFilter;
                }
                return JS_ThrowInternalError(context, "failed to read layer filters: %d", status);
            }

            int createdFilters = 0;
            int updatedFilters = 0;
            for (size_t i = 0; i < filterNames.size(); ++i)
            {
                bool created = false;
                status = upsertLayerFilter(rootFilter, filterNames[i], created);
                if (status != Acad::eOk)
                {
                    delete rootFilter;
                    const std::string query = "NAME==" + filterNames[i] + ".*";
                    return JS_ThrowInternalError(
                        context,
                        "failed to create or update layer filter '%s' with expression '%s': %s (%d)",
                        filterNames[i].c_str(),
                        query.c_str(),
                        acadErrorName(status),
                        status);
                }

                if (created)
                {
                    ++createdFilters;
                }
                else
                {
                    ++updatedFilters;
                }
            }

            status = filterManager->setFilters(rootFilter, currentFilter != nullptr ? currentFilter : rootFilter);
            delete rootFilter;
            if (status != Acad::eOk)
            {
                return JS_ThrowInternalError(context, "failed to save layer filters: %d", status);
            }

            JSValue result = JS_NewObject(context);
            JS_SetPropertyStr(context, result, "layers", JS_NewInt32(context, static_cast<int>(specs.size())));
            JS_SetPropertyStr(context, result, "createdLayers", JS_NewInt32(context, createdLayers));
            JS_SetPropertyStr(context, result, "updatedLayers", JS_NewInt32(context, updatedLayers));
            JS_SetPropertyStr(context, result, "filters", JS_NewInt32(context, static_cast<int>(filterNames.size())));
            JS_SetPropertyStr(context, result, "createdFilters", JS_NewInt32(context, createdFilters));
            JS_SetPropertyStr(context, result, "updatedFilters", JS_NewInt32(context, updatedFilters));
            return result;
        }

        JSValue jsEditorCreateLayers(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::vector<LayerSpec> specs;
            if (argc >= 1 && !JS_IsUndefined(argv[0]))
            {
                JSValue parsed = specsFromJsArray(context, argv[0], specs);
                if (JS_IsException(parsed))
                {
                    return parsed;
                }
            }
            else
            {
                specs = builtInQsLayerSpecs();
            }

            return createLayersFromSpecs(context, specs);
        }

        JSValue jsEditorCreateQsLayers(JSContext *context, JSValueConst, int, JSValueConst *)
        {
            const std::vector<LayerSpec> specs = builtInQsLayerSpecs();
            return createLayersFromSpecs(context, specs);
        }

        bool parsePointCoordinate(JSContext *context, JSValueConst value, ads_point point)
        {
            if (JS_IsArray(value))
            {
                point[0] = 0.0;
                point[1] = 0.0;
                point[2] = 0.0;
                const uint32_t len = arrayLength(context, value);
                for (uint32_t i = 0; i < len && i < 3; ++i)
                {
                    JSValue item = JS_GetPropertyUint32(context, value, i);
                    double v = 0.0;
                    if (JS_ToFloat64(context, &v, item) >= 0)
                    {
                        point[i] = v;
                    }
                    JS_FreeValue(context, item);
                }
                return true;
            }
            if (JS_IsObject(value))
            {
                point[0] = 0.0;
                point[1] = 0.0;
                point[2] = 0.0;
                JSValue vx = JS_GetPropertyStr(context, value, "x");
                JSValue vy = JS_GetPropertyStr(context, value, "y");
                JSValue vz = JS_GetPropertyStr(context, value, "z");
                double x = 0.0, y = 0.0, z = 0.0;
                if (!JS_IsUndefined(vx) && JS_ToFloat64(context, &x, vx) >= 0) point[0] = x;
                if (!JS_IsUndefined(vy) && JS_ToFloat64(context, &y, vy) >= 0) point[1] = y;
                if (!JS_IsUndefined(vz) && JS_ToFloat64(context, &z, vz) >= 0) point[2] = z;
                JS_FreeValue(context, vx);
                JS_FreeValue(context, vy);
                JS_FreeValue(context, vz);
                return true;
            }
            return false;
        }

        JSValue jsEditorGetString(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string prompt = "\nEnter string: ";
            bool allowSpaces = true;
            std::string defaultValue;
            bool hasDefault = false;

            if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
            {
                if (JS_IsString(argv[0]))
                {
                    prompt = valueToString(context, argv[0]);
                }
                else if (JS_IsObject(argv[0]))
                {
                    JSValue p = JS_GetPropertyStr(context, argv[0], "prompt");
                    if (JS_IsString(p))
                    {
                        prompt = valueToString(context, p);
                    }
                    JS_FreeValue(context, p);

                    JSValue s = JS_GetPropertyStr(context, argv[0], "allowSpaces");
                    if (!JS_IsUndefined(s))
                    {
                        allowSpaces = JS_ToBool(context, s) != 0;
                    }
                    JS_FreeValue(context, s);

                    JSValue d = JS_GetPropertyStr(context, argv[0], "default");
                    if (JS_IsString(d))
                    {
                        defaultValue = valueToString(context, d);
                        hasDefault = true;
                    }
                    JS_FreeValue(context, d);
                }
            }

            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
            {
                if (JS_IsBool(argv[1]))
                {
                    allowSpaces = JS_ToBool(context, argv[1]) != 0;
                }
                else if (JS_IsObject(argv[1]))
                {
                    JSValue s = JS_GetPropertyStr(context, argv[1], "allowSpaces");
                    if (!JS_IsUndefined(s))
                    {
                        allowSpaces = JS_ToBool(context, s) != 0;
                    }
                    JS_FreeValue(context, s);

                    JSValue d = JS_GetPropertyStr(context, argv[1], "default");
                    if (JS_IsString(d))
                    {
                        defaultValue = valueToString(context, d);
                        hasDefault = true;
                    }
                    JS_FreeValue(context, d);
                }
                else if (JS_IsString(argv[1]))
                {
                    defaultValue = valueToString(context, argv[1]);
                    hasDefault = true;
                }
            }

            const std::basic_string<ACHAR> nativePrompt = command_text::fromUtf8(prompt);
            ACHAR buffer[4096] = {};
            const int status = acedGetString(allowSpaces ? 1 : 0, nativePrompt.c_str(), buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));

            if (status == RTNORM)
            {
                std::string utf8 = command_text::toUtf8(buffer);
                return JS_NewStringLen(context, utf8.data(), utf8.size());
            }

            if (status == RTNONE)
            {
                if (hasDefault)
                {
                    return JS_NewStringLen(context, defaultValue.data(), defaultValue.size());
                }
                return JS_NewString(context, "");
            }

            if (status == RTCAN)
            {
                return JS_NULL;
            }

            return JS_NULL;
        }

        JSValue jsEditorGetPoint(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string prompt = "\nSpecify point: ";
            ads_point basePt = {};
            bool hasBasePt = false;

            if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
            {
                if (JS_IsString(argv[0]))
                {
                    prompt = valueToString(context, argv[0]);
                }
                else if (JS_IsObject(argv[0]))
                {
                    JSValue p = JS_GetPropertyStr(context, argv[0], "prompt");
                    if (JS_IsString(p))
                    {
                        prompt = valueToString(context, p);
                    }
                    JS_FreeValue(context, p);

                    JSValue b = JS_GetPropertyStr(context, argv[0], "basePoint");
                    if (!JS_IsUndefined(b) && !JS_IsNull(b))
                    {
                        hasBasePt = parsePointCoordinate(context, b, basePt);
                    }
                    JS_FreeValue(context, b);
                }
            }

            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
            {
                hasBasePt = parsePointCoordinate(context, argv[1], basePt);
            }

            const std::basic_string<ACHAR> nativePrompt = command_text::fromUtf8(prompt);
            ads_point resultPt = {};
            const int status = acedGetPoint(hasBasePt ? basePt : nullptr, nativePrompt.c_str(), resultPt);

            if (status == RTNORM)
            {
                JSValue array = pointToArray(context, resultPt);
                JS_SetPropertyStr(context, array, "x", JS_NewFloat64(context, resultPt[0]));
                JS_SetPropertyStr(context, array, "y", JS_NewFloat64(context, resultPt[1]));
                JS_SetPropertyStr(context, array, "z", JS_NewFloat64(context, resultPt[2]));
                return array;
            }

            if (status == RTCAN || status == RTNONE)
            {
                return JS_NULL;
            }

            return JS_NULL;
        }

        JSValue jsEditorGetNumber(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string prompt = "\nEnter number: ";
            bool isInteger = false;
            double defaultVal = 0.0;
            bool hasDefault = false;

            if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
            {
                if (JS_IsString(argv[0]))
                {
                    prompt = valueToString(context, argv[0]);
                }
                else if (JS_IsObject(argv[0]))
                {
                    JSValue p = JS_GetPropertyStr(context, argv[0], "prompt");
                    if (JS_IsString(p))
                    {
                        prompt = valueToString(context, p);
                    }
                    JS_FreeValue(context, p);

                    JSValue d = JS_GetPropertyStr(context, argv[0], "default");
                    if (!JS_IsUndefined(d) && !JS_IsNull(d))
                    {
                        if (JS_ToFloat64(context, &defaultVal, d) >= 0)
                        {
                            hasDefault = true;
                        }
                    }
                    JS_FreeValue(context, d);

                    JSValue intOpt = JS_GetPropertyStr(context, argv[0], "integer");
                    if (!JS_IsUndefined(intOpt))
                    {
                        isInteger = JS_ToBool(context, intOpt) != 0;
                    }
                    JS_FreeValue(context, intOpt);
                }
            }

            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
            {
                if (JS_IsNumber(argv[1]))
                {
                    if (JS_ToFloat64(context, &defaultVal, argv[1]) >= 0)
                    {
                        hasDefault = true;
                    }
                }
                else if (JS_IsObject(argv[1]))
                {
                    JSValue d = JS_GetPropertyStr(context, argv[1], "default");
                    if (!JS_IsUndefined(d) && !JS_IsNull(d))
                    {
                        if (JS_ToFloat64(context, &defaultVal, d) >= 0)
                        {
                            hasDefault = true;
                        }
                    }
                    JS_FreeValue(context, d);

                    JSValue intOpt = JS_GetPropertyStr(context, argv[1], "integer");
                    if (!JS_IsUndefined(intOpt))
                    {
                        isInteger = JS_ToBool(context, intOpt) != 0;
                    }
                    JS_FreeValue(context, intOpt);
                }
            }

            const std::basic_string<ACHAR> nativePrompt = command_text::fromUtf8(prompt);
            if (isInteger)
            {
                int intVal = 0;
                const int status = acedGetInt(nativePrompt.c_str(), &intVal);
                if (status == RTNORM)
                {
                    return JS_NewInt32(context, intVal);
                }
                if (status == RTNONE && hasDefault)
                {
                    return JS_NewInt32(context, static_cast<int32_t>(defaultVal));
                }
                return JS_NULL;
            }
            else
            {
                ads_real realVal = 0.0;
                const int status = acedGetReal(nativePrompt.c_str(), &realVal);
                if (status == RTNORM)
                {
                    return JS_NewFloat64(context, realVal);
                }
                if (status == RTNONE && hasDefault)
                {
                    return JS_NewFloat64(context, defaultVal);
                }
                return JS_NULL;
            }
        }

        JSValue jsEditorGetValue(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string prompt = "\nEnter value: ";
            JSValue defaultValue = JS_UNDEFINED;
            bool hasDefault = false;

            if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
            {
                if (JS_IsString(argv[0]))
                {
                    prompt = valueToString(context, argv[0]);
                }
                else if (JS_IsObject(argv[0]))
                {
                    JSValue p = JS_GetPropertyStr(context, argv[0], "prompt");
                    if (JS_IsString(p))
                    {
                        prompt = valueToString(context, p);
                    }
                    JS_FreeValue(context, p);

                    defaultValue = JS_GetPropertyStr(context, argv[0], "default");
                    if (!JS_IsUndefined(defaultValue))
                    {
                        hasDefault = true;
                    }
                }
            }

            if (argc >= 2 && !JS_IsUndefined(argv[1]))
            {
                defaultValue = argv[1];
                hasDefault = true;
            }

            const std::basic_string<ACHAR> nativePrompt = command_text::fromUtf8(prompt);
            ACHAR buffer[4096] = {};
            const int status = acedGetString(1, nativePrompt.c_str(), buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));

            if (status == RTCAN)
            {
                if (hasDefault && !JS_IsUndefined(defaultValue))
                {
                    JS_FreeValue(context, defaultValue);
                }
                return JS_NULL;
            }

            if (status == RTNONE)
            {
                if (hasDefault && !JS_IsUndefined(defaultValue))
                {
                    return JS_DupValue(context, defaultValue);
                }
                return JS_NULL;
            }

            std::string line = command_text::toUtf8(buffer);

            // Trim whitespace
            size_t start = 0;
            while (start < line.size() && static_cast<unsigned char>(line[start]) <= ' ')
            {
                ++start;
            }
            size_t end = line.size();
            while (end > start && static_cast<unsigned char>(line[end - 1]) <= ' ')
            {
                --end;
            }
            const std::string trimmed = line.substr(start, end - start);
            if (trimmed.empty())
            {
                if (hasDefault && !JS_IsUndefined(defaultValue))
                {
                    return JS_DupValue(context, defaultValue);
                }
                return JS_NULL;
            }

            std::string evalSource = trimmed;
            if (trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}')
            {
                evalSource = "(" + trimmed + ")";
            }

            JSValue val = JS_Eval(context, evalSource.c_str(), evalSource.size(), "<editor.getValue>", JS_EVAL_TYPE_GLOBAL);
            JSRuntime *runtime = JS_GetRuntime(context);
            JSContext *pendingContext = nullptr;
            while (JS_IsJobPending(runtime))
            {
                if (JS_ExecutePendingJob(runtime, &pendingContext) < 0)
                {
                    break;
                }
            }

            if (JS_IsException(val))
            {
                JSValue exception = JS_GetException(context);
                JS_FreeValue(context, exception);

                // Fall back to returning string representation if evaluation failed
                return JS_NewStringLen(context, trimmed.data(), trimmed.size());
            }

            return val;
        }
    }

    void installEditor(JSContext *context)
    {
        JSValue global = JS_GetGlobalObject(context);
        JSValue editor = JS_NewObject(context);
        JS_SetPropertyStr(context, editor, "select", JS_NewCFunction(context, jsEditorSelect, "select", 4));
        JS_SetPropertyStr(context, editor, "getString", JS_NewCFunction(context, jsEditorGetString, "getString", 2));
        JS_SetPropertyStr(context, editor, "getPoint", JS_NewCFunction(context, jsEditorGetPoint, "getPoint", 2));
        JS_SetPropertyStr(context, editor, "getNumber", JS_NewCFunction(context, jsEditorGetNumber, "getNumber", 2));
        JS_SetPropertyStr(context, editor, "getValue", JS_NewCFunction(context, jsEditorGetValue, "getValue", 2));
        JS_SetPropertyStr(context, editor, "createLayers", JS_NewCFunction(context, jsEditorCreateLayers, "createLayers", 1));
        JS_SetPropertyStr(context, editor, "createQsLayers", JS_NewCFunction(context, jsEditorCreateQsLayers, "createQsLayers", 0));
        safe_commands::registerSafeCommands(context, editor, false);
        JS_SetPropertyStr(context, global, "editor", editor);
        JS_FreeValue(context, global);
    }
}

#include "js_modules.h"
#include "color_utils.h"

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
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__GCRX__) || defined(GSTARCAD)
#include "gccoredefs.h"
#else
#include "accoredefs.h"
#endif
#include "acly.h"
#include "dbapserv.h"
#include "dbmain.h"
#include "dbsymtb.h"
#include "text_util.h"
#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
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

        std::string toLowerAscii(std::string text)
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
            }
            return text;
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
            double b2 = x * 0.0557 + y * -0.2040 + z * 1.0570;
            auto gamma = [](double channel)
            {
                channel = std::max(0.0, channel);
                return channel <= 0.0031308 ? 12.92 * channel : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
            };

            return setRgbBytes(gamma(r) * 255.0, gamma(g) * 255.0, gamma(b2) * 255.0, red, green, blue);
        }

        bool parseHexColor(const std::string &text, Adesk::UInt8 &red, Adesk::UInt8 &green, Adesk::UInt8 &blue)
        {
            if ((text.size() != 4 && text.size() != 7) || text[0] != '#')
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

        bool readRgbObject(JSContext *context, JSValueConst object, AcCmColor &color, std::string &error)
        {
            const char *names[] = {"r", "g", "b"};
            Adesk::UInt8 values[] = {0, 0, 0};
            for (int i = 0; i < 3; ++i)
            {
                JSValue value = JS_GetPropertyStr(context, object, names[i]);
                int intValue = 0;
                const int status = JS_ToInt32(context, &intValue, value);
                JS_FreeValue(context, value);
                if (status < 0 || !byteFromInt(intValue, values[i]))
                {
                    error = std::string("color.") + names[i] + " must be an integer from 0 to 255";
                    return false;
                }
            }

            return color.setRGB(values[0], values[1], values[2]) == Acad::eOk;
        }

        bool parseColorString(const std::string &rawText, AcCmColor &color, std::string &error)
        {
            const std::string text = trimAscii(rawText);
            const std::string lower = toLowerAscii(text);
            Adesk::UInt8 red = 0;
            Adesk::UInt8 green = 0;
            Adesk::UInt8 blue = 0;

            if (parseHexColor(text, red, green, blue))
            {
                return color.setRGB(red, green, blue) == Acad::eOk;
            }

            const size_t open = lower.find('(');
            if (open == std::string::npos)
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
                ok = setRgbBytes(values[0], values[1], values[2], red, green, blue);
            }
            else if (space == "hsl" && values.size() == 3)
            {
                ok = hslToRgb(values[0], values[1], values[2], red, green, blue);
            }
            else if (space == "hsv" && values.size() == 3)
            {
                ok = hsvToRgb(values[0], values[1], values[2], red, green, blue);
            }
            else if (space == "cmyk" && values.size() == 4)
            {
                ok = cmykToRgb(values[0], values[1], values[2], values[3], red, green, blue);
            }
            else if (space == "hct" && values.size() == 3)
            {
                ok = hctToRgb(values[0], values[1], values[2], red, green, blue);
            }
            else
            {
                error = "unsupported color string or wrong channel count";
                return false;
            }

            if (!ok)
            {
                error = "color channels are out of range";
                return false;
            }

            return color.setRGB(red, green, blue) == Acad::eOk;
        }

        bool colorFromJs(JSContext *context, JSValueConst value, AcCmColor &color, std::string &error)
        {
            color_utils::ParsedColor pc;
            if (!color_utils::parseColorFromJs(context, value, pc, error))
            {
                return false;
            }

            if (pc.kind == color_utils::ParsedColor::Kind::Aci)
            {
                return color.setColorIndex(static_cast<Adesk::Int16>(pc.aci)) == Acad::eOk;
            }
            else
            {
                return color.setRGB(pc.r, pc.g, pc.b) == Acad::eOk;
            }
        }

        bool transparencyFromJs(JSContext *context, JSValueConst value, AcCmTransparency &transparency, std::string &error)
        {
            double percent = 0.0;
            if (JS_ToFloat64(context, &percent, value) < 0 || percent < 0.0 || percent > 100.0)
            {
                acutPrintf(L"\n--- transparency value: %f", percent);
                error = "transparency must be a number from 0 to 100";
                return false;
            }

            transparency = AcCmTransparency(percent);
            return true;
        }

        double transparencyToPercent(const AcCmTransparency &transparency)
        {
            if (transparency.isByAlpha())
            {
                return clampDouble(transparency.alphaPercent(), 0.0, 100.0);
            }

            return 0.0;
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

        JSValue layerToJs(JSContext *context, const std::string &name, AcDbLayerTableRecord *record)
        {
            JSValue result = JS_NewObject(context);
            JS_SetPropertyStr(context, result, "lyr", JS_NewString(context, name.c_str()));
            JS_SetPropertyStr(context, result, "clr", colorToJs(context, record->color()));
            JS_SetPropertyStr(context, result, "trp", JS_NewFloat64(context, transparencyToPercent(record->transparency())));
            return result;
        }

        std::string filterNameForLayer(const char *layerName)
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

        JSValue filterNamesFromJsLayers(JSContext *context, JSValueConst value, std::vector<std::string> &filterNames)
        {
            if (!JS_IsArray(value))
            {
                return JS_ThrowTypeError(context, "layer.updateLayerFilter expects an array of layer names or { layer } objects");
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

            std::unordered_set<std::string> seen;
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue item = JS_GetPropertyUint32(context, value, i);
                if (JS_IsException(item))
                {
                    return item;
                }

                std::string layerName;
                if (JS_IsString(item))
                {
                    layerName = valueToString(context, item);
                }
                else if (JS_IsObject(item))
                {
                    JSValue layerValue = JS_GetPropertyStr(context, item, "layer");
                    if (JS_IsUndefined(layerValue) || JS_IsNull(layerValue))
                    {
                        JS_FreeValue(context, layerValue);
                        layerValue = JS_GetPropertyStr(context, item, "name");
                    }

                    if (!JS_IsString(layerValue))
                    {
                        JS_FreeValue(context, layerValue);
                        JS_FreeValue(context, item);
                        return JS_ThrowTypeError(context, "layer.updateLayerFilter item %u must have a string layer or name property", static_cast<unsigned int>(i));
                    }

                    layerName = valueToString(context, layerValue);
                    JS_FreeValue(context, layerValue);
                }
                else
                {
                    JS_FreeValue(context, item);
                    return JS_ThrowTypeError(context, "layer.updateLayerFilter item %u must be a string or object", static_cast<unsigned int>(i));
                }

                JS_FreeValue(context, item);
                if (layerName.empty())
                {
                    return JS_ThrowTypeError(context, "layer.updateLayerFilter item %u has an empty layer name", static_cast<unsigned int>(i));
                }

                const std::string filterName = filterNameForLayer(layerName.c_str());
                if (seen.insert(filterName).second)
                {
                    filterNames.push_back(filterName);
                }
            }

            return JS_UNDEFINED;
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

        JSValue jsLayerCreateLayer(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]))
            {
                return JS_ThrowTypeError(context, "layer.createLayer expects an array of layer objects");
            }

            if (!JS_IsArray(argv[0]))
            {
                return JS_ThrowTypeError(context, "layer.createLayer expects an array of layer objects");
            }

            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return JS_ThrowInternalError(context, "no active AutoCAD database");
            }

            JSValue lengthValue = JS_GetPropertyStr(context, argv[0], "length");
            uint32_t arrayLength = 0;
            JS_ToUint32(context, &arrayLength, lengthValue);
            JS_FreeValue(context, lengthValue);

            if (arrayLength == 0)
            {
                return JS_ThrowTypeError(context, "layer.createLayer expects at least one layer object");
            }

            AcDbLayerTable *layerTable = nullptr;
            Acad::ErrorStatus status = db->getSymbolTable(layerTable, AcDb::kForWrite);
            if (status != Acad::eOk)
            {
                return JS_ThrowInternalError(context, "failed to open layer table: %d", status);
            }

            JSValue result = JS_NewArray(context);
            uint32_t resultIndex = 0;

            for (uint32_t i = 0; i < arrayLength; ++i)
            {
                JSValue layerObj = JS_GetPropertyUint32(context, argv[0], i);
                if (!JS_IsObject(layerObj))
                {
                    JS_FreeValue(context, layerObj);
                    layerTable->close();
                    return JS_ThrowTypeError(context, "layer.createLayer array element %u is not an object", i);
                }

                // Get layer name (required)
                JSValue lyrValue = JS_GetPropertyStr(context, layerObj, "lyr");
                if (!JS_IsString(lyrValue))
                {
                    JS_FreeValue(context, lyrValue);
                    JS_FreeValue(context, layerObj);
                    layerTable->close();
                    return JS_ThrowTypeError(context, "layer.createLayer element %u: 'lyr' property is required and must be a string", i);
                }

                const std::string name = valueToString(context, lyrValue);
                const std::basic_string<ACHAR> nativeName = command_text::fromUtf8(name);
                JS_FreeValue(context, lyrValue);

                AcDbLayerTableRecord *record = nullptr;
                bool exists = layerTable->has(nativeName.c_str());
                bool added = false;
                if (exists)
                {
                    status = layerTable->getAt(nativeName.c_str(), record, AcDb::kForWrite);
                    if (status != Acad::eOk)
                    {
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowInternalError(context, "failed to open layer '%s': %d", name.c_str(), status);
                    }
                }
                else
                {
                    record = new AcDbLayerTableRecord();
                    status = record->setName(nativeName.c_str());
                    if (status != Acad::eOk)
                    {
                        delete record;
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowInternalError(context, "failed to name layer '%s': %d", name.c_str(), status);
                    }
                    record->setIsFrozen(0);
                    record->setIsOff(0);
                    record->setVPDFLT(0);
                    record->setIsLocked(0);

                    status = layerTable->add(record);
                    if (status != Acad::eOk)
                    {
                        delete record;
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowInternalError(context, "failed to add layer '%s': %d", name.c_str(), status);
                    }
                    added = true;
                }

                // Process color (optional)
                JSValue clrValue = JS_GetPropertyStr(context, layerObj, "clr");
                if (!JS_IsUndefined(clrValue) && !JS_IsNull(clrValue))
                {
                    AcCmColor color;
                    std::string error;
                    if (!colorFromJs(context, clrValue, color, error))
                    {
                        if (exists)
                        {
                            record->close();
                        }
                        else
                        {
                            delete record;
                        }
                        JS_FreeValue(context, clrValue);
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowTypeError(context, "layer.createLayer element %u color: %s", i, error.c_str());
                    }
                    record->setColor(color);
                }
                else if (!exists)
                {
                    AcCmColor color = db->cecolor();
                    if (color.isByLayer())
                    {
                        color.setColorIndex(7);
                    }
                    record->setColor(color);
                }
                JS_FreeValue(context, clrValue);

                // Process transparency (optional)
                JSValue trpValue = JS_GetPropertyStr(context, layerObj, "trp");
                if (!JS_IsUndefined(trpValue) && !JS_IsNull(trpValue))
                {
                    AcCmTransparency transparency;
                    std::string error;
                    if (!transparencyFromJs(context, trpValue, transparency, error))
                    {
                        if (exists)
                        {
                            record->close();
                        }
                        else
                        {
                            delete record;
                        }
                        JS_FreeValue(context, trpValue);
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowTypeError(context, "layer.createLayer element %u transparency: %s", i, error.c_str());
                    }
                    status = record->setTransparency(transparency);
                    if (status != Acad::eOk)
                    {
                        if (exists)
                        {
                            record->close();
                        }
                        else
                        {
                            delete record;
                        }
                        JS_FreeValue(context, trpValue);
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowInternalError(context, "failed to set layer transparency: %d", status);
                    }
                }
                else if (!exists)
                {
                    AcCmTransparency transparency(100.0);
                    record->setTransparency(transparency);
                }
                JS_FreeValue(context, trpValue);

                // Process frozen (optional)
                JSValue frozenValue = JS_GetPropertyStr(context, layerObj, "frozen");
                if (!JS_IsUndefined(frozenValue) && !JS_IsNull(frozenValue))
                {
                    int frozen = JS_ToBool(context, frozenValue);
                    record->setIsFrozen(frozen ? 1 : 0);
                }
                JS_FreeValue(context, frozenValue);

                // Process locked (optional)
                JSValue lockedValue = JS_GetPropertyStr(context, layerObj, "locked");
                if (!JS_IsUndefined(lockedValue) && !JS_IsNull(lockedValue))
                {
                    int locked = JS_ToBool(context, lockedValue);
                    record->setIsLocked(locked ? 1 : 0);
                }
                JS_FreeValue(context, lockedValue);

                if (!exists && !added)
                {
                    status = layerTable->add(record);
                    if (status != Acad::eOk)
                    {
                        delete record;
                        JS_FreeValue(context, layerObj);
                        layerTable->close();
                        return JS_ThrowInternalError(context, "failed to add layer '%s': %d", name.c_str(), status);
                    }
                }

                JSValue layerDescriptor = layerToJs(context, name, record);
                JS_SetPropertyUint32(context, result, resultIndex++, layerDescriptor);

                record->close();
                JS_FreeValue(context, layerObj);
            }

            layerTable->close();
            return result;
        }

        JSValue jsLayerUpdateLayerFilter(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]))
            {
                return JS_ThrowTypeError(context, "layer.updateLayerFilter expects a layer list");
            }

            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return JS_ThrowInternalError(context, "no active AutoCAD database");
            }

            std::vector<std::string> filterNames;
            JSValue parsed = filterNamesFromJsLayers(context, argv[0], filterNames);
            if (JS_IsException(parsed))
            {
                return parsed;
            }

            if (filterNames.empty())
            {
                return JS_ThrowTypeError(context, "layer.updateLayerFilter needs at least one layer");
            }

            AcLyLayerFilterManager *filterManager = aclyGetLayerFilterManager(db);
            if (filterManager == nullptr)
            {
                return JS_ThrowInternalError(context, "failed to get AutoCAD layer filter manager");
            }

            AcLyLayerFilter *rootFilter = nullptr;
            AcLyLayerFilter *currentFilter = nullptr;
            Acad::ErrorStatus status = filterManager->getFilters(rootFilter, currentFilter);
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
                    return JS_ThrowInternalError(context, "failed to create or update layer filter '%s' with expression '%s': %d", filterNames[i].c_str(), query.c_str(), status);
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
            JS_SetPropertyStr(context, result, "filters", JS_NewInt32(context, static_cast<int>(filterNames.size())));
            JS_SetPropertyStr(context, result, "createdFilters", JS_NewInt32(context, createdFilters));
            JS_SetPropertyStr(context, result, "updatedFilters", JS_NewInt32(context, updatedFilters));
            return result;
        }
    }

    JSValue createLayerModule(JSContext *context)
    {
        JSValue layer = JS_NewObject(context);
        JS_SetPropertyStr(context, layer, "createLayer", JS_NewCFunction(context, jsLayerCreateLayer, "createLayer", 3));
        JS_SetPropertyStr(context, layer, "updateLayerFilter", JS_NewCFunction(context, jsLayerUpdateLayerFilter, "updateLayerFilter", 1));
        return layer;
    }
}

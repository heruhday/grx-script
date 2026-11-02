#include "js_modules.h"
#include "safe_commands.h"
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
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "acedads.h"
#include "adscodes.h"
#include "acdbads.h"
#include "acutads.h"
#include "acutmem.h"
#include "dbapserv.h"
#include "dbhandle.h"
#include "dbmain.h"
#include "dbsymtb.h"
#include "dbtrans.h"
#include "rxclass.h"
#include "dbcurve.h"
#include "dbents.h"
#include "dbhatch.h"
#include "dbmline.h"
#include "dbmtext.h"
#include "dbpl.h"
#include "dbregion.h"
#include "dbspline.h"
#include "gevec3d.h"
#include "text_util.h"
#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        const ACHAR *kEntityDataRegApp = ACRX_T("G");

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

        int toByte(double value)
        {
            return static_cast<int>(std::max(0.0, std::min(255.0, std::floor(value + 0.5))));
        }

        static AcCmColor g_lastUsedColor;
        static bool g_hasLastUsedColor = false;

        Acad::ErrorStatus ensureEntityLayerExists(const ACHAR *layerName)
        {
            if (layerName == nullptr || *layerName == 0)
            {
                return Acad::eInvalidInput;
            }

            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return Acad::eNoDatabase;
            }

            AcDbLayerTable *layerTable = nullptr;
            Acad::ErrorStatus status = db->getSymbolTable(layerTable, AcDb::kForWrite);
            if (status != Acad::eOk || layerTable == nullptr)
            {
                return status;
            }

            if (layerTable->has(layerName))
            {
                layerTable->close();
                return Acad::eOk;
            }

            AcDbLayerTableRecord *record = new AcDbLayerTableRecord();
            status = record->setName(layerName);
            if (status == Acad::eOk)
            {
                AcCmColor color;
                if (g_hasLastUsedColor)
                {
                    color = g_lastUsedColor;
                }
                else
                {
                    color.setColorIndex(7);
                }
                record->setColor(color);
                record->setIsFrozen(false);
                record->setIsOff(false);
                record->setIsLocked(false);
                status = layerTable->add(record);
            }

            if (status == Acad::eOk)
            {
                record->close();
            }
            else
            {
                delete record;
            }
            layerTable->close();
            return status;
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
            int intValue = 0;
            if (JS_ToInt32(context, &intValue, value) < 0)
            {
                error = "trp must be -2, -1, or 0..100";
                return false;
            }

            if (intValue == -2)
            {
                transparency.setMethod(AcCmTransparency::kByLayer);
                return true;
            }

            if (intValue == -1)
            {
                transparency.setMethod(AcCmTransparency::kByBlock);
                return true;
            }

            if (intValue < 0 || intValue > 100)
            {
                error = "trp must be -2, -1, or 0..100";
                return false;
            }

            transparency.setAlpha(static_cast<Adesk::UInt8>(toByte((100.0 - intValue) * 255.0 / 100.0)));
            return true;
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

            return JS_NewInt32(context, static_cast<int>(std::max(0.0, std::min(100.0, std::floor((255.0 - transparency.alpha()) * 100.0 / 255.0 + 0.5)))));
        }

        bool collectXDataBytes(AcDbEntity *entity, std::vector<uint8_t> &bytes)
        {
            resbuf *xdata = entity->xData(kEntityDataRegApp);
            if (xdata == nullptr)
            {
                return false;
            }

            for (resbuf *rb = xdata; rb != nullptr; rb = rb->rbnext)
            {
                if (rb->restype == AcDb::kDxfXdBinaryChunk && rb->resval.rbinary.buf != nullptr && rb->resval.rbinary.clen > 0)
                {
                    const uint8_t *chunk = reinterpret_cast<const uint8_t *>(rb->resval.rbinary.buf);
                    bytes.insert(bytes.end(), chunk, chunk + rb->resval.rbinary.clen);
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

        bool msgpackBytesFromValue(JSContext *context, JSValueConst value, std::vector<uint8_t> &bytes, std::string &error)
        {
            JSValue encoded = encodeMsgpackValue(context, value);
            if (JS_IsException(encoded))
            {
                error = "failed to encode data as MessagePack";
                return false;
            }

            size_t byteLength = 0;
            uint8_t *buffer = JS_GetUint8Array(context, &byteLength, encoded);
            if (buffer == nullptr)
            {
                JS_FreeValue(context, encoded);
                error = "failed to read encoded MessagePack bytes";
                return false;
            }

            bytes.assign(buffer, buffer + byteLength);
            JS_FreeValue(context, encoded);
            return true;
        }

        resbuf *newStringRb(int type, const std::basic_string<ACHAR> &text)
        {
            resbuf *rb = acutNewRb(type);
            if (rb == nullptr)
            {
                return nullptr;
            }

            rb->resval.rstring = nullptr;
            if (acutNewString(text.c_str(), rb->resval.rstring) != Acad::eOk || rb->resval.rstring == nullptr)
            {
                acutRelRb(rb);
                return nullptr;
            }
            return rb;
        }

        resbuf *xdataFromBytes(const std::vector<uint8_t> &bytes)
        {
            resbuf *head = newStringRb(AcDb::kDxfRegAppName, kEntityDataRegApp);
            if (head == nullptr)
            {
                return nullptr;
            }

            resbuf *tail = head;
            const size_t chunkSize = 127;
            for (size_t offset = 0; offset < bytes.size(); offset += chunkSize)
            {
                const size_t count = std::min(chunkSize, bytes.size() - offset);
                resbuf *chunk = acutNewRb(AcDb::kDxfXdBinaryChunk);
                if (chunk == nullptr)
                {
                    acutRelRb(head);
                    return nullptr;
                }

                chunk->resval.rbinary.clen = static_cast<short>(count);
                chunk->resval.rbinary.buf = static_cast<char *>(std::malloc(count));
                if (chunk->resval.rbinary.buf == nullptr)
                {
                    acutRelRb(chunk);
                    acutRelRb(head);
                    return nullptr;
                }

                std::copy(bytes.data() + offset, bytes.data() + offset + count, reinterpret_cast<uint8_t *>(chunk->resval.rbinary.buf));
                tail->rbnext = chunk;
                tail = chunk;
            }

            return head;
        }

        resbuf *xdataDeleteMarker()
        {
            return newStringRb(AcDb::kDxfRegAppName, kEntityDataRegApp);
        }

        Acad::ErrorStatus ensureRegApp()
        {
            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return Acad::eNoDatabase;
            }

            AcDbRegAppTable *regAppTable = nullptr;
            Acad::ErrorStatus status = db->getRegAppTable(regAppTable, AcDb::kForRead);
            if (status != Acad::eOk || regAppTable == nullptr)
            {
                return status;
            }

            if (regAppTable->has(kEntityDataRegApp))
            {
                regAppTable->close();
                return Acad::eOk;
            }

            status = regAppTable->upgradeOpen();
            if (status != Acad::eOk)
            {
                regAppTable->close();
                return status;
            }

            AcDbRegAppTableRecord *record = new AcDbRegAppTableRecord();
            status = record->setName(kEntityDataRegApp);
            if (status == Acad::eOk)
            {
                status = regAppTable->add(record);
            }

            if (status == Acad::eOk)
            {
                record->close();
            }
            else
            {
                delete record;
            }

            regAppTable->close();
            return status;
        }

        Acad::ErrorStatus objectIdFromHandleValue(AcDbObjectId &objectId, JSContext *context, JSValueConst value)
        {
            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return Acad::eNoDatabase;
            }

            AcDbHandle handle;
            if (JS_IsString(value))
            {
                const std::basic_string<ACHAR> text = command_text::fromUtf8(valueToString(context, value));
                handle = text.c_str();
            }
            else
            {
                int64_t number = 0;
                if (JS_ToInt64(context, &number, value) < 0 || number <= 0)
                {
                    return Acad::eInvalidInput;
                }
                handle = static_cast<Adesk::UInt64>(number);
            }

            return db->getAcDbObjectId(objectId, false, handle);
        }

        Acad::ErrorStatus objectIdFromPatch(AcDbObjectId &objectId, JSContext *context, JSValueConst patch)
        {
            JSValue id = JS_GetPropertyStr(context, patch, "id");
            if (JS_IsUndefined(id) || JS_IsNull(id))
            {
                JS_FreeValue(context, id);
                id = JS_GetPropertyStr(context, patch, "hand");
            }
            if (JS_IsUndefined(id) || JS_IsNull(id))
            {
                JS_FreeValue(context, id);
                id = JS_GetPropertyStr(context, patch, "handle");
            }

            if (JS_IsUndefined(id) || JS_IsNull(id))
            {
                JS_FreeValue(context, id);
                return Acad::eInvalidInput;
            }

            const Acad::ErrorStatus status = objectIdFromHandleValue(objectId, context, id);
            JS_FreeValue(context, id);
            return status;
        }

        Acad::ErrorStatus objectIdFromAdsName(AcDbObjectId &objectId, const ads_name entityName)
        {
            return acdbGetObjectId(objectId, entityName);
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

        void addCurveMetrics(JSContext *context, JSValueConst descriptor, AcDbCurve *curve, AcRxClass *objectClass)
        {
            double endParam = 0.0;
            double length = 0.0;
            if (curve->getEndParam(endParam) == Acad::eOk &&
                curve->getDistAtParam(endParam, length) == Acad::eOk)
            {
                JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, length));
                JS_DefinePropertyValueStr(context, descriptor, "length", JS_NewFloat64(context, length), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            }

            if (isCurveClosedForArea(curve, objectClass))
            {
                double area = 0.0;
                if (curve->getArea(area) == Acad::eOk)
                {
                    JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, area));
                    JS_DefinePropertyValueStr(context, descriptor, "area", JS_NewFloat64(context, area), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
                }
            }
        }

        void addRegionMetrics(JSContext *context, JSValueConst descriptor, AcDbRegion *region)
        {
            double perimeter = 0.0;
            if (region->getPerimeter(perimeter) == Acad::eOk)
            {
                JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, perimeter));
            }

            double area = 0.0;
            if (region->getArea(area) == Acad::eOk)
            {
                JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, area));
            }
        }

        void addHatchMetrics(JSContext *context, JSValueConst descriptor, AcDbHatch *hatch)
        {
            double area = 0.0;
            if (hatch->getArea(area) == Acad::eOk)
            {
                JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, area));
            }

            AcDbRegion *region = hatch->getRegionArea();
            if (region != nullptr)
            {
                double perimeter = 0.0;
                if (region->getPerimeter(perimeter) == Acad::eOk)
                {
                    JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, perimeter));
                }
                delete region;
            }
        }

        void addMlineMetrics(JSContext *context, JSValueConst descriptor, AcDbMline *mline)
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

                JS_SetPropertyStr(context, descriptor, "l", JS_NewFloat64(context, length));
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
                JS_SetPropertyStr(context, descriptor, "a", JS_NewFloat64(context, 0.5 * areaVector.length()));
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

        void addTextProperties(JSContext *context, JSValueConst descriptor, AcDbEntity *entity, AcRxClass *objectClass)
        {
            if (objectClass->isDerivedFrom(AcDbMText::desc()))
            {
                AcDbMText *mtext = static_cast<AcDbMText *>(entity);
                ACHAR *contents = mtext->contents();
                JS_SetPropertyStr(
                    context,
                    descriptor,
                    "text",
                    JS_NewString(context, contents != nullptr ? command_text::toUtf8(contents).c_str() : ""));
                if (contents != nullptr)
                {
                    acutDelString(contents);
                }
            }
            else if (objectClass->isDerivedFrom(AcDbText::desc()))
            {
                const ACHAR *text = static_cast<AcDbText *>(entity)->textStringConst();
                JS_SetPropertyStr(
                    context,
                    descriptor,
                    "text",
                    JS_NewString(context, text != nullptr ? command_text::toUtf8(text).c_str() : ""));
            }
        }

        bool isCadBuiltinProp(const char *name)
        {
            if (name == nullptr) return false;
            static const char *const kCadProps[] = {
                "id", "hand", "handle", "ty", "type",
                "clr", "color", "trp", "transparency",
                "visible", "lyr", "layer", "text", "attr",
                "data", "highlight", "unhighlight", "l", "a",
                "area", "len", "length"
            };
            for (const char *cp : kCadProps)
            {
                if (std::strcmp(name, cp) == 0) return true;
            }
            return false;
        }

        void addEntityProperties(JSContext *context, JSValueConst descriptor, AcDbEntity *entity)
        {
            AcRxClass *objectClass = entity->isA();
            if (objectClass != nullptr)
            {
                const ACHAR *className = objectClass->name();
                const std::string tyStr = className != nullptr ? command_text::toUtf8(className) : "";
                JS_SetPropertyStr(context, descriptor, "ty", JS_NewString(context, tyStr.c_str()));
                JS_DefinePropertyValueStr(context, descriptor, "type", JS_NewString(context, tyStr.c_str()), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
                addTextProperties(context, descriptor, entity, objectClass);
                if (objectClass->isDerivedFrom(AcDbCurve::desc()))
                {
                    addCurveMetrics(context, descriptor, static_cast<AcDbCurve *>(entity), objectClass);
                }
                else if (objectClass->isDerivedFrom(AcDbRegion::desc()))
                {
                    addRegionMetrics(context, descriptor, static_cast<AcDbRegion *>(entity));
                }
                else if (objectClass->isDerivedFrom(AcDbHatch::desc()))
                {
                    addHatchMetrics(context, descriptor, static_cast<AcDbHatch *>(entity));
                }
                else if (objectClass->isDerivedFrom(AcDbMline::desc()))
                {
                    addMlineMetrics(context, descriptor, static_cast<AcDbMline *>(entity));
                }
                else if (objectClass->isDerivedFrom(AcDbBlockReference::desc()))
                {
                    addBlockReferenceProperties(context, descriptor, static_cast<AcDbBlockReference *>(entity));
                }
            }

            JS_SetPropertyStr(context, descriptor, "clr", colorToJs(context, entity->color()));
            JS_DefinePropertyValueStr(context, descriptor, "color", colorToJs(context, entity->color()), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

            JS_SetPropertyStr(context, descriptor, "trp", transparencyToJs(context, entity->transparency()));
            JS_DefinePropertyValueStr(context, descriptor, "transparency", transparencyToJs(context, entity->transparency()), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);

            JS_SetPropertyStr(context, descriptor, "visible", JS_NewBool(context, entity->visibility() == AcDb::kVisible));

            ACHAR *layer = entity->layer();
            const std::string lyrStr = layer != nullptr ? command_text::toUtf8(layer) : "";
            JS_SetPropertyStr(context, descriptor, "lyr", JS_NewString(context, lyrStr.c_str()));
            JS_DefinePropertyValueStr(context, descriptor, "layer", JS_NewString(context, lyrStr.c_str()), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            if (layer != nullptr)
            {
                acutDelString(layer);
            }

            JSValue xdata = decodeXDataValue(context, entity);
            JS_SetPropertyStr(context, descriptor, "data", JS_DupValue(context, xdata));

            if (JS_IsObject(xdata) && !JS_IsNull(xdata))
            {
                JSPropertyEnum *tab = nullptr;
                uint32_t len = 0;
                if (JS_GetOwnPropertyNames(context, &tab, &len, xdata, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0)
                {
                    for (uint32_t pi = 0; pi < len; ++pi)
                    {
                        const char *propName = JS_AtomToCString(context, tab[pi].atom);
                        if (propName)
                        {
                            if (!isCadBuiltinProp(propName))
                            {
                                JSValue propVal = JS_GetProperty(context, xdata, tab[pi].atom);
                                JS_DefinePropertyValueStr(context, descriptor, propName, propVal, JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
                            }
                            JS_FreeCString(context, propName);
                        }
                        JS_FreeAtom(context, tab[pi].atom);
                    }
                    js_free(context, tab);
                }
            }
            JS_FreeValue(context, xdata);
        }

        JSValue entityToObject(JSContext *context, const AcDbObjectId &objectId)
        {
            AcDbEntity *entity = nullptr;
            const Acad::ErrorStatus openStatus = acdbOpenObject(entity, objectId, AcDb::kForRead);
            if (openStatus != Acad::eOk || entity == nullptr)
            {
                return JS_NULL;
            }

            JSValue descriptor = JS_NewObject(context);
            const AcDbHandle handle = objectId.handle();
            const std::string handStr = handleToString(handle);
            JS_SetPropertyStr(context, descriptor, "id", handleToInteger(context, handle));
            JS_SetPropertyStr(context, descriptor, "hand", JS_NewString(context, handStr.c_str()));
            JS_DefinePropertyValueStr(context, descriptor, "handle", JS_NewString(context, handStr.c_str()), JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
            addEntityProperties(context, descriptor, entity);
            entity->close();
            return descriptor;
        }

        JSValue entityToObject(JSContext *context, const ads_name entityName)
        {
            AcDbObjectId objectId;
            const Acad::ErrorStatus status = objectIdFromAdsName(objectId, entityName);
            if (status != Acad::eOk)
            {
                return JS_NULL;
            }

            return entityToObject(context, objectId);
        }

        bool propertyIsPresent(JSContext *context, JSValueConst object, const char *name, JSValue &value)
        {
            value = JS_GetPropertyStr(context, object, name);
            return !JS_IsUndefined(value);
        }

        bool propertyOrAliasPresent(JSContext *context, JSValueConst object, const char *name, const char *alias, JSValue &value)
        {
            if (propertyIsPresent(context, object, name, value))
            {
                return true;
            }
            if (alias != nullptr && propertyIsPresent(context, object, alias, value))
            {
                return true;
            }
            return false;
        }

        std::string asciiLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        Acad::ErrorStatus setAttributeText(JSContext *context, AcDbAttribute *attribute, JSValueConst value)
        {
            const std::basic_string<ACHAR> text = JS_IsNull(value)
                                                      ? std::basic_string<ACHAR>()
                                                      : command_text::fromUtf8(valueToString(context, value));
            return attribute->setTextString(text.c_str());
        }

        bool propertyNameIsPresent(JSContext *context, JSValueConst object, const char *name, JSValue &value)
        {
            return propertyIsPresent(context, object, name, value) && !JS_IsUndefined(value);
        }

        Acad::ErrorStatus applyAttributePatch(JSContext *context, AcDbAttribute *attribute, JSValueConst value, std::string &error)
        {
            if (!JS_IsObject(value) || JS_IsArray(value))
            {
                const Acad::ErrorStatus status = setAttributeText(context, attribute, value);
                if (status != Acad::eOk)
                {
                    error = "failed to set block attribute text";
                }
                return status;
            }

            bool recognized = false;
            JSValue property;
            const char *textNames[] = {"txt", "text", "val", "value"};
            for (size_t i = 0; i < sizeof(textNames) / sizeof(textNames[0]); ++i)
            {
                if (propertyNameIsPresent(context, value, textNames[i], property))
                {
                    recognized = true;
                    const Acad::ErrorStatus status = setAttributeText(context, attribute, property);
                    JS_FreeValue(context, property);
                    if (status != Acad::eOk)
                    {
                        error = "failed to set block attribute text";
                        return status;
                    }
                    break;
                }
                JS_FreeValue(context, property);
            }

            if (propertyNameIsPresent(context, value, "clr", property))
            {
                recognized = true;
                if (!JS_IsNull(property))
                {
                    AcCmColor color;
                    if (!colorFromJs(context, property, color, error))
                    {
                        JS_FreeValue(context, property);
                        return Acad::eInvalidInput;
                    }
                    attribute->setColor(color);
                }
                JS_FreeValue(context, property);
            }
            else
            {
                JS_FreeValue(context, property);
            }

            if (propertyNameIsPresent(context, value, "trp", property))
            {
                recognized = true;
                if (!JS_IsNull(property))
                {
                    AcCmTransparency transparency;
                    if (!transparencyFromJs(context, property, transparency, error))
                    {
                        JS_FreeValue(context, property);
                        return Acad::eInvalidInput;
                    }
                    attribute->setTransparency(transparency);
                }
                JS_FreeValue(context, property);
            }
            else
            {
                JS_FreeValue(context, property);
            }

            if (propertyNameIsPresent(context, value, "visible", property))
            {
                recognized = true;
                const bool visible = JS_ToBool(context, property) != 0;
                JS_FreeValue(context, property);
                const Acad::ErrorStatus status = attribute->setVisibility(visible ? AcDb::kVisible : AcDb::kInvisible);
                if (status != Acad::eOk)
                {
                    error = "failed to set block attribute visibility";
                    return status;
                }
            }
            else
            {
                JS_FreeValue(context, property);
            }

            if (propertyNameIsPresent(context, value, "lyr", property))
            {
                recognized = true;
                if (!JS_IsNull(property))
                {
                    const std::basic_string<ACHAR> layerName = command_text::fromUtf8(valueToString(context, property));
                    Acad::ErrorStatus status = ensureEntityLayerExists(layerName.c_str());
                    if (status == Acad::eOk)
                    {
                        status = attribute->setLayer(layerName.c_str());
                    }
                    JS_FreeValue(context, property);
                    if (status != Acad::eOk)
                    {
                        error = "failed to set block attribute layer";
                        return status;
                    }
                }
                else
                {
                    JS_FreeValue(context, property);
                }
            }
            else
            {
                JS_FreeValue(context, property);
            }

            if (!recognized)
            {
                error = "attr tag object must include txt, val, clr, trp, visible, or lyr";
                return Acad::eInvalidInput;
            }

            return Acad::eOk;
        }

        Acad::ErrorStatus setBlockReferenceAttributes(JSContext *context, AcDbBlockReference *blockReference, JSValueConst attrValue, std::string &error)
        {
            if (!JS_IsObject(attrValue) || JS_IsArray(attrValue))
            {
                error = "attr must be an object with attribute tags as keys";
                return Acad::eInvalidInput;
            }

            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, attrValue, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
            {
                error = "failed to read attr object keys";
                return Acad::eInvalidInput;
            }

            Acad::ErrorStatus status = Acad::eOk;
            for (uint32_t i = 0; i < propertyCount && status == Acad::eOk; ++i)
            {
                const char *tagChars = JS_AtomToCString(context, properties[i].atom);
                if (tagChars == nullptr)
                {
                    status = Acad::eInvalidInput;
                    error = "failed to read attr tag";
                    break;
                }
                const std::string wantedTag = asciiLower(tagChars);
                JS_FreeCString(context, tagChars);

                JSValue jsPatch = JS_GetProperty(context, attrValue, properties[i].atom);
                if (JS_IsException(jsPatch))
                {
                    status = Acad::eInvalidInput;
                    error = "failed to read attr value";
                    break;
                }
                if (JS_IsUndefined(jsPatch))
                {
                    JS_FreeValue(context, jsPatch);
                    continue;
                }

                bool found = false;
                AcDbObjectIterator *iterator = blockReference->attributeIterator();
                if (iterator == nullptr)
                {
                    JS_FreeValue(context, jsPatch);
                    status = Acad::eInvalidInput;
                    error = "failed to iterate block attributes";
                    break;
                }

                for (; !iterator->done(); iterator->step())
                {
                    AcDbAttribute *attribute = nullptr;
                    const Acad::ErrorStatus openStatus = blockReference->openAttribute(attribute, iterator->objectId(), AcDb::kForWrite);
                    if (openStatus != Acad::eOk || attribute == nullptr)
                    {
                        continue;
                    }

                    const ACHAR *tag = attribute->tagConst();
                    if (tag != nullptr && asciiLower(command_text::toUtf8(tag)) == wantedTag)
                    {
                        found = true;
                        const Acad::ErrorStatus setStatus = applyAttributePatch(context, attribute, jsPatch, error);
                        if (setStatus != Acad::eOk)
                        {
                            status = setStatus;
                            attribute->close();
                            break;
                        }
                    }
                    attribute->close();
                }
                delete iterator;
                JS_FreeValue(context, jsPatch);

                if (status == Acad::eOk && !found)
                {
                    status = Acad::eInvalidInput;
                    error = "block attribute tag not found";
                }
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            return status;
        }

        struct EntitySelectionArgs
        {
            std::basic_string<ACHAR> prompt;
            JSValue filter = JS_UNDEFINED;
            JSValue out = JS_UNDEFINED;
            JSValue set = JS_UNDEFINED;
            std::string name;
            bool suppressOutput = false;
            bool useImplied = true;
            bool onlyImplied = false;
        };

        void freeSelectionArgs(JSContext *context, EntitySelectionArgs &args)
        {
            if (!JS_IsUndefined(args.filter))
            {
                JS_FreeValue(context, args.filter);
                args.filter = JS_UNDEFINED;
            }
            if (!JS_IsUndefined(args.out))
            {
                JS_FreeValue(context, args.out);
                args.out = JS_UNDEFINED;
            }
            if (!JS_IsUndefined(args.set))
            {
                JS_FreeValue(context, args.set);
                args.set = JS_UNDEFINED;
            }
        }

        JSValue parseSelectionOptions(JSContext *context, JSValueConst options, EntitySelectionArgs &args)
        {
            if (!JS_IsObject(options))
            {
                return JS_ThrowTypeError(context, "entity selection options must be an object");
            }

            JSValue value = JS_GetPropertyStr(context, options, "filter");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                if (!JS_IsFunction(context, value))
                {
                    JS_FreeValue(context, value);
                    
                    return JS_ThrowTypeError(context, "entity selection filter must be a function");
                }
                args.filter = JS_DupValue(context, value);
            }
            JS_FreeValue(context, value);

            if (propertyIsPresent(context, options, "out", value))
            {
                if (JS_IsNull(value))
                {
                    if (!JS_IsUndefined(args.out))
                    {
                        JS_FreeValue(context, args.out);
                        args.out = JS_UNDEFINED;
                    }
                    args.suppressOutput = true;
                }
                else if (!JS_IsUndefined(value))
                {
                    if (!JS_IsFunction(context, value))
                    {
                        JS_FreeValue(context, value);
                        return JS_ThrowTypeError(context, "entity selection out must be a function or null");
                    }
                    if (!JS_IsUndefined(args.out))
                    {
                        JS_FreeValue(context, args.out);
                    }
                    args.out = JS_DupValue(context, value);
                    args.suppressOutput = false;
                }
                JS_FreeValue(context, value);
            }

            value = JS_GetPropertyStr(context, options, "set");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                if (!JS_IsFunction(context, value) && !JS_IsObject(value))
                {
                    JS_FreeValue(context, value);
                    return JS_ThrowTypeError(context, "entity selection set must be a function or an object");
                }
                args.set = JS_DupValue(context, value);
            }
            JS_FreeValue(context, value);

            value = JS_GetPropertyStr(context, options, "prompt");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                args.prompt = command_text::fromUtf8(valueToString(context, value));
            }
            JS_FreeValue(context, value);

            value = JS_GetPropertyStr(context, options, "name");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                args.name = valueToString(context, value);
            }
            JS_FreeValue(context, value);

            value = JS_GetPropertyStr(context, options, "implied");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                args.useImplied = JS_ToBool(context, value) != 0;
            }
            JS_FreeValue(context, value);

            value = JS_GetPropertyStr(context, options, "onlyImplied");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                args.onlyImplied = JS_ToBool(context, value) != 0;
            }
            JS_FreeValue(context, value);

            value = JS_GetPropertyStr(context, options, "mode");
            if (!JS_IsUndefined(value) && !JS_IsNull(value))
            {
                const std::string modeStr = valueToString(context, value);
                if (modeStr == "implied")
                {
                    args.useImplied = true;
                    args.onlyImplied = true;
                }
            }
            JS_FreeValue(context, value);

            return JS_UNDEFINED;
        }

        JSValue parseSelectionArgs(JSContext *context, int argc, JSValueConst *argv, EntitySelectionArgs &args)
        {
            if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0]))
            {
                if (JS_IsFunction(context, argv[0]))
                {
                    args.filter = JS_DupValue(context, argv[0]);
                }
                else if (JS_IsObject(argv[0]))
                {
                    JSValue parsed = parseSelectionOptions(context, argv[0], args);
                    if (JS_IsException(parsed))
                    {
                        freeSelectionArgs(context, args);
                        return parsed;
                    }
                }
                else
                {
                    args.prompt = command_text::fromUtf8(valueToString(context, argv[0]));
                }
            }

            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
            {
                if (JS_IsFunction(context, argv[1]))
                {
                    args.filter = JS_DupValue(context, argv[1]);
                }
                else if (JS_IsObject(argv[1]))
                {
                    JSValue parsed = parseSelectionOptions(context, argv[1], args);
                    if (JS_IsException(parsed))
                    {
                        freeSelectionArgs(context, args);
                        return parsed;
                    }
                }
                else
                {
                    return JS_ThrowTypeError(context, "entity selection second argument must be a function or object");
                }
            }

            if (argc >= 3 && !JS_IsUndefined(argv[2]))
            {
                if (JS_IsNull(argv[2]))
                {
                    if (!JS_IsUndefined(args.out))
                    {
                        JS_FreeValue(context, args.out);
                        args.out = JS_UNDEFINED;
                    }
                    args.suppressOutput = true;
                }
                else
                {
                    if (!JS_IsFunction(context, argv[2]))
                    {
                        return JS_ThrowTypeError(context, "entity selection out must be a function or null");
                    }
                    if (!JS_IsUndefined(args.out))
                    {
                        JS_FreeValue(context, args.out);
                    }
                    args.out = JS_DupValue(context, argv[2]);
                    args.suppressOutput = false;
                }
            }

            if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3]))
            {
                if (!JS_IsFunction(context, argv[3]))
                {
                    return JS_ThrowTypeError(context, "entity selection set must be a function");
                }
                args.set = JS_DupValue(context, argv[3]);
            }

            return JS_UNDEFINED;
        }

        JSValue passesFilter(JSContext *context, JSValueConst filter, JSValueConst entity, bool &accepted)
        {
            accepted = true;
            if (!JS_IsFunction(context, filter))
            {
                return JS_UNDEFINED;
            }

            JSValue callArgs[] = {JS_DupValue(context, entity)};
            JSValue result = JS_Call(context, filter, JS_UNDEFINED, 1, callArgs);
            JS_FreeValue(context, callArgs[0]);
            if (JS_IsException(result))
            {
                return result;
            }

            accepted = JS_ToBool(context, result) != 0;
            JS_FreeValue(context, result);
            return JS_UNDEFINED;
        }

        JSValue mapEntityOutput(JSContext *context, JSValueConst out, JSValueConst entity)
        {
            if (!JS_IsFunction(context, out))
            {
                return JS_DupValue(context, entity);
            }

            JSValue callArgs[] = {JS_DupValue(context, entity)};
            JSValue result = JS_Call(context, out, JS_UNDEFINED, 1, callArgs);
            JS_FreeValue(context, callArgs[0]);
            return result;
        }

        JSValue copyObjectProperties(JSContext *context, JSValueConst source, JSValue destination)
        {
            JSPropertyEnum *properties = nullptr;
            uint32_t propertyCount = 0;
            if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, source, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
            {
                return JS_EXCEPTION;
            }

            for (uint32_t i = 0; i < propertyCount; ++i)
            {
                JSValue value = JS_GetProperty(context, source, properties[i].atom);
                if (JS_IsException(value))
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return value;
                }

                if (JS_SetProperty(context, destination, properties[i].atom, value) < 0)
                {
                    JS_FreePropertyEnum(context, properties, propertyCount);
                    return JS_EXCEPTION;
                }
            }

            JS_FreePropertyEnum(context, properties, propertyCount);
            return JS_UNDEFINED;
        }

        bool ensurePatchHasId(JSContext *context, JSValue patch, JSValueConst entity)
        {
            JSValue id = JS_GetPropertyStr(context, patch, "id");
            JSValue hand = JS_GetPropertyStr(context, patch, "hand");
            const bool hasId = !JS_IsUndefined(id) && !JS_IsNull(id);
            const bool hasHand = !JS_IsUndefined(hand) && !JS_IsNull(hand);
            JS_FreeValue(context, id);
            JS_FreeValue(context, hand);

            if (hasId || hasHand)
            {
                return true;
            }

            JSValue entityHand = JS_GetPropertyStr(context, entity, "hand");
            if (JS_IsUndefined(entityHand) || JS_IsNull(entityHand))
            {
                JS_FreeValue(context, entityHand);
                return false;
            }

            if (JS_SetPropertyStr(context, patch, "hand", JS_DupValue(context, entityHand)) < 0)
            {
                JS_FreeValue(context, entityHand);
                return false;
            }

            JS_FreeValue(context, entityHand);
            return true;
        }

        JSValue prepareEntitySetPatch(JSContext *context, JSValueConst entity, JSValueConst setValue)
        {
            if (JS_IsUndefined(setValue) || JS_IsNull(setValue))
            {
                return JS_NULL;
            }

            if (!JS_IsObject(setValue))
            {
                return JS_ThrowTypeError(context, "entity selection set must be a function or an object");
            }

            JSValue patch = JS_NewObject(context);
            if (JS_IsException(patch))
            {
                return patch;
            }

            JSValue error = copyObjectProperties(context, setValue, patch);
            if (JS_IsException(error))
            {
                JS_FreeValue(context, patch);
                return error;
            }

            if (!ensurePatchHasId(context, patch, entity))
            {
                JS_FreeValue(context, patch);
                return JS_ThrowTypeError(context, "entity selection set patch requires id or hand");
            }

            return patch;
        }

        JSValue entityGetArray(JSContext *context, JSValueConst handles, JSValueConst filter, JSValueConst out)
        {
            JSValue array = JS_NewArray(context);
            const uint32_t length = arrayLength(context, handles);
            uint32_t acceptedIndex = 0;

            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue handle = JS_GetPropertyUint32(context, handles, i);
                if (JS_IsException(handle))
                {
                    JS_FreeValue(context, array);
                    return handle;
                }

                AcDbObjectId objectId;
                const Acad::ErrorStatus status = objectIdFromHandleValue(objectId, context, handle);
                JS_FreeValue(context, handle);
                if (status != Acad::eOk)
                {
                    continue;
                }

                JSValue entity = entityToObject(context, objectId);
                if (JS_IsException(entity))
                {
                    JS_FreeValue(context, array);
                    return entity;
                }

                if (JS_IsNull(entity))
                {
                    JS_FreeValue(context, entity);
                    continue;
                }

                bool accepted = true;
                JSValue error = passesFilter(context, filter, entity, accepted);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, entity);
                    JS_FreeValue(context, array);
                    return error;
                }

                if (accepted)
                {
                    JSValue output = mapEntityOutput(context, out, entity);
                    JS_FreeValue(context, entity);
                    if (JS_IsException(output))
                    {
                        JS_FreeValue(context, array);
                        return output;
                    }
                    JS_SetPropertyUint32(context, array, acceptedIndex++, output);
                }
                else
                {
                    JS_FreeValue(context, entity);
                }
            }

            return array;
        }

        void clearImpliedSelection()
        {
            ads_name empty;
            acdbNameClear(empty);
            acedSSSetFirst(empty, empty);
        }

        Acad::ErrorStatus setEntityData(JSContext *context, AcDbEntity *entity, JSValueConst dataValue, std::string &error)
        {
            Acad::ErrorStatus status = ensureRegApp();
            if (status != Acad::eOk)
            {
                error = "failed to register xdata regapp";
                return status;
            }

            resbuf *xdata = nullptr;
            if (JS_IsNull(dataValue))
            {
                xdata = xdataDeleteMarker();
            }
            else
            {
                JSValue existing = decodeXDataValue(context, entity);
                JSValue merged = JS_IsNull(existing) || JS_IsUndefined(existing)
                    ? deepCopyValue(context, dataValue)
                    : deepMergeObjects(context, dataValue, existing);
                JS_FreeValue(context, existing);

                if (JS_IsException(merged))
                {
                    error = "failed to merge entity xdata";
                    return Acad::eInvalidInput;
                }

                std::vector<uint8_t> bytes;
                const bool ok = msgpackBytesFromValue(context, merged, bytes, error);
                JS_FreeValue(context, merged);
                if (!ok)
                {
                    return Acad::eInvalidInput;
                }
                xdata = xdataFromBytes(bytes);
            }

            if (xdata == nullptr)
            {
                return Acad::eOutOfMemory;
            }

            status = entity->setXData(xdata);
            acutRelRb(xdata);
            if (status != Acad::eOk)
            {
                error = "setXData failed";
            }
            return status;
        }

        Acad::ErrorStatus applyEntityHighlight(const AcDbObjectId &objectId, bool highlightRequested, bool unhighlightRequested)
        {
            if (!highlightRequested && !unhighlightRequested)
            {
                return Acad::eOk;
            }

            AcDbEntity *entity = nullptr;
            Acad::ErrorStatus status = acdbOpenObject(entity, objectId, AcDb::kForRead);
            if (status != Acad::eOk || entity == nullptr)
            {
                return status;
            }

            if (unhighlightRequested)
            {
                status = entity->unhighlight();
            }

            if (status == Acad::eOk && highlightRequested)
            {
                status = entity->highlight();
            }

            entity->close();
            return status;
        }

        JSValue entitySetInternal(JSContext *context, JSValueConst patch)
        {
            AcDbObjectId objectId;
            Acad::ErrorStatus status = objectIdFromPatch(objectId, context, patch);
            if (status != Acad::eOk)
            {
                return JS_ThrowTypeError(context, "entity.set needs id or hand");
            }

            bool highlightRequested = false;
            bool unhighlightRequested = false;
            JSValue actionValue;
            if (propertyIsPresent(context, patch, "highlight", actionValue))
            {
                if (!JS_IsUndefined(actionValue))
                {
                    highlightRequested = JS_ToBool(context, actionValue) != 0;
                }
                JS_FreeValue(context, actionValue);
            }

            if (propertyIsPresent(context, patch, "unhighlight", actionValue))
            {
                if (!JS_IsUndefined(actionValue))
                {
                    unhighlightRequested = JS_ToBool(context, actionValue) != 0;
                }
                JS_FreeValue(context, actionValue);
            }

            AcDbEntity *entity = nullptr;
            status = acdbOpenObject(entity, objectId, AcDb::kForWrite);
            if (status != Acad::eOk || entity == nullptr)
            {
                return JS_ThrowInternalError(context, "failed to open entity for write: %d", status);
            }

            JSValue value;
            if (propertyOrAliasPresent(context, patch, "clr", "color", value))
            {
                if (!JS_IsUndefined(value))
                {
                    AcCmColor color;
                    std::string error;
                    if (!colorFromJs(context, value, color, error))
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowTypeError(context, "%s", error.c_str());
                    }
                    entity->setColor(color);
                    g_lastUsedColor = color;
                    g_hasLastUsedColor = true;
                }
                JS_FreeValue(context, value);
            }

            if (propertyOrAliasPresent(context, patch, "trp", "transparency", value))
            {
                if (!JS_IsUndefined(value))
                {
                    AcCmTransparency transparency;
                    std::string error;
                    if (!transparencyFromJs(context, value, transparency, error))
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowTypeError(context, "%s", error.c_str());
                    }
                    entity->setTransparency(transparency);
                }
                JS_FreeValue(context, value);
            }

            if (propertyIsPresent(context, patch, "visible", value))
            {
                if (!JS_IsUndefined(value))
                {
                    const bool visible = JS_ToBool(context, value) != 0;
                    status = entity->setVisibility(visible ? AcDb::kVisible : AcDb::kInvisible);
                    if (status != Acad::eOk)
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowInternalError(context, "failed to set entity visibility: %d", status);
                    }
                }
                JS_FreeValue(context, value);
            }

            if (propertyOrAliasPresent(context, patch, "lyr", "layer", value))
            {
                if (!JS_IsUndefined(value) && !JS_IsNull(value))
                {
                    const std::basic_string<ACHAR> layerName = command_text::fromUtf8(valueToString(context, value));
                    status = ensureEntityLayerExists(layerName.c_str());
                    if (status == Acad::eOk)
                    {
                        status = entity->setLayer(layerName.c_str());
                    }
                    if (status != Acad::eOk)
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowInternalError(
                            context,
                            "failed to create or set entity layer '%s': %d",
                            command_text::toUtf8(layerName.c_str()).c_str(),
                            status);
                    }
                }
                JS_FreeValue(context, value);
            }

            if (propertyIsPresent(context, patch, "text", value))
            {
                if (!JS_IsUndefined(value))
                {
                    AcRxClass *objectClass = entity->isA();
                    if (objectClass == nullptr ||
                        (!objectClass->isDerivedFrom(AcDbText::desc()) &&
                         !objectClass->isDerivedFrom(AcDbMText::desc())))
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowTypeError(context, "text can only be set on AcDbText or AcDbMText");
                    }

                    const std::basic_string<ACHAR> text = JS_IsNull(value)
                                                             ? std::basic_string<ACHAR>()
                                                             : command_text::fromUtf8(valueToString(context, value));
                    if (objectClass->isDerivedFrom(AcDbMText::desc()))
                    {
                        status = static_cast<Acad::ErrorStatus>(
                            static_cast<AcDbMText *>(entity)->setContents(text.c_str()));
                    }
                    else
                    {
                        status = static_cast<AcDbText *>(entity)->setTextString(text.c_str());
                    }

                    if (status != Acad::eOk)
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowInternalError(context, "failed to set entity text: %d", status);
                    }
                }
                JS_FreeValue(context, value);
            }

            if (propertyIsPresent(context, patch, "attr", value))
            {
                if (!JS_IsUndefined(value))
                {
                    AcRxClass *objectClass = entity->isA();
                    if (objectClass == nullptr || !objectClass->isDerivedFrom(AcDbBlockReference::desc()))
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowTypeError(context, "attr can only be set on AcDbBlockReference");
                    }

                    std::string error;
                    status = setBlockReferenceAttributes(context, static_cast<AcDbBlockReference *>(entity), value, error);
                    if (status != Acad::eOk)
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowInternalError(context, "%s (%d)", error.c_str(), status);
                    }
                }
                JS_FreeValue(context, value);
            }

            if (propertyIsPresent(context, patch, "data", value))
            {
                if (!JS_IsUndefined(value))
                {
                    std::string error;
                    status = setEntityData(context, entity, value, error);
                    if (status != Acad::eOk)
                    {
                        JS_FreeValue(context, value);
                        entity->close();
                        return JS_ThrowInternalError(context, "failed to set entity xdata: %s (%d)", error.c_str(), status);
                    }
                }
                JS_FreeValue(context, value);
            }

            // Also check for any custom properties in patch that are not CAD builtins
            // e.g. { code: 'CT1', building: 'TowerA', floor: 1 }
            JSPropertyEnum *patchTab = nullptr;
            uint32_t patchLen = 0;
            if (JS_GetOwnPropertyNames(context, &patchTab, &patchLen, patch, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0)
            {
                JSValue customObj = JS_UNDEFINED;
                for (uint32_t pi = 0; pi < patchLen; ++pi)
                {
                    const char *pName = JS_AtomToCString(context, patchTab[pi].atom);
                    if (pName)
                    {
                        if (!isCadBuiltinProp(pName))
                        {
                            if (JS_IsUndefined(customObj))
                            {
                                customObj = JS_NewObject(context);
                            }
                            JSValue pVal = JS_GetProperty(context, patch, patchTab[pi].atom);
                            JS_SetPropertyStr(context, customObj, pName, pVal);
                        }
                        JS_FreeCString(context, pName);
                    }
                    JS_FreeAtom(context, patchTab[pi].atom);
                }
                js_free(context, patchTab);

                if (!JS_IsUndefined(customObj))
                {
                    std::string error;
                    status = setEntityData(context, entity, customObj, error);
                    JS_FreeValue(context, customObj);
                    if (status != Acad::eOk)
                    {
                        entity->close();
                        return JS_ThrowInternalError(context, "failed to set entity custom properties: %s (%d)", error.c_str(), status);
                    }
                }
            }

            entity->close();
            status = applyEntityHighlight(objectId, highlightRequested, unhighlightRequested);
            if (status != Acad::eOk)
            {
                return JS_ThrowInternalError(context, "failed to update entity highlight: %d", status);
            }

            return entityToObject(context, objectId);
        }

        JSValue entitySetOutput(JSContext *context, JSValueConst out, JSValue result)
        {
            if (JS_IsException(result))
            {
                return result;
            }

            if (!JS_IsFunction(context, out))
            {
                JS_FreeValue(context, result);
                return JS_UNDEFINED;
            }

            JSValue output = mapEntityOutput(context, out, result);
            JS_FreeValue(context, result);
            return output;
        }

        JSValue entitySetPatchArray(JSContext *context, JSValueConst patches, JSValueConst out)
        {
            const bool wantsOutput = JS_IsFunction(context, out);
            JSValue array = wantsOutput ? JS_NewArray(context) : JS_UNDEFINED;
            if (wantsOutput && JS_IsException(array))
            {
                return array;
            }

            const uint32_t length = arrayLength(context, patches);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue patch = JS_GetPropertyUint32(context, patches, i);
                if (JS_IsException(patch))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return patch;
                }

                if (!JS_IsObject(patch) || JS_IsArray(patch))
                {
                    JS_FreeValue(context, patch);
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return JS_ThrowTypeError(context, "entity.set array items must be objects");
                }

                JSValue result = entitySetInternal(context, patch);
                JS_FreeValue(context, patch);
                if (JS_IsException(result))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return result;
                }

                if (wantsOutput)
                {
                    JSValue output = mapEntityOutput(context, out, result);
                    JS_FreeValue(context, result);
                    if (JS_IsException(output))
                    {
                        JS_FreeValue(context, array);
                        return output;
                    }
                    JS_SetPropertyUint32(context, array, i, output);
                }
                else
                {
                    JS_FreeValue(context, result);
                }
            }

            return wantsOutput ? array : JS_UNDEFINED;
        }

        JSValue clonePatchWithHandle(JSContext *context, JSValueConst patch, JSValueConst handle)
        {
            JSValue clone = JS_NewObject(context);
            if (JS_IsException(clone))
            {
                return clone;
            }

            JSValue error = copyObjectProperties(context, patch, clone);
            if (JS_IsException(error))
            {
                JS_FreeValue(context, clone);
                return error;
            }

            if (JS_SetPropertyStr(context, clone, JS_IsString(handle) ? "hand" : "id", JS_DupValue(context, handle)) < 0)
            {
                JS_FreeValue(context, clone);
                return JS_EXCEPTION;
            }

            return clone;
        }

        JSValue entitySetSharedPatchArray(JSContext *context, JSValueConst patch, JSValueConst handles, JSValueConst out)
        {
            const bool wantsOutput = JS_IsFunction(context, out);
            JSValue array = wantsOutput ? JS_NewArray(context) : JS_UNDEFINED;
            if (wantsOutput && JS_IsException(array))
            {
                return array;
            }

            const uint32_t length = arrayLength(context, handles);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue handle = JS_GetPropertyUint32(context, handles, i);
                if (JS_IsException(handle))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return handle;
                }

                JSValue entityPatch = clonePatchWithHandle(context, patch, handle);
                JS_FreeValue(context, handle);
                if (JS_IsException(entityPatch))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return entityPatch;
                }

                JSValue result = entitySetInternal(context, entityPatch);
                JS_FreeValue(context, entityPatch);
                if (JS_IsException(result))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return result;
                }

                if (wantsOutput)
                {
                    JSValue output = mapEntityOutput(context, out, result);
                    JS_FreeValue(context, result);
                    if (JS_IsException(output))
                    {
                        JS_FreeValue(context, array);
                        return output;
                    }
                    JS_SetPropertyUint32(context, array, i, output);
                }
                else
                {
                    JS_FreeValue(context, result);
                }
            }

            return wantsOutput ? array : JS_UNDEFINED;
        }

        JSValue entitySetPatchFactoryArray(JSContext *context, JSValueConst factory, JSValueConst handles, JSValueConst out)
        {
            const bool wantsOutput = JS_IsFunction(context, out);
            JSValue array = wantsOutput ? JS_NewArray(context) : JS_UNDEFINED;
            if (wantsOutput && JS_IsException(array))
            {
                return array;
            }

            const uint32_t length = arrayLength(context, handles);
            for (uint32_t i = 0; i < length; ++i)
            {
                JSValue handle = JS_GetPropertyUint32(context, handles, i);
                if (JS_IsException(handle))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return handle;
                }

                AcDbObjectId objectId;
                Acad::ErrorStatus status = objectIdFromHandleValue(objectId, context, handle);
                if (status != Acad::eOk)
                {
                    JS_FreeValue(context, handle);
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return JS_ThrowTypeError(context, "entity.set needs id or hand");
                }

                JSValue entity = entityToObject(context, objectId);
                if (JS_IsException(entity))
                {
                    JS_FreeValue(context, handle);
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return entity;
                }

                if (JS_IsNull(entity))
                {
                    JS_FreeValue(context, entity);
                    JS_FreeValue(context, handle);
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return JS_ThrowTypeError(context, "entity.set needs id or hand");
                }

                JS_SetPropertyStr(context, entity, "idx", JS_NewUint32(context, i));

                JSValue callArgs[] = {JS_DupValue(context, entity)};
                JSValue patch = JS_Call(context, factory, JS_UNDEFINED, 1, callArgs);
                JS_FreeValue(context, callArgs[0]);
                JS_FreeValue(context, entity);
                if (JS_IsException(patch))
                {
                    JS_FreeValue(context, handle);
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return patch;
                }

                if (!JS_IsObject(patch) || JS_IsArray(patch))
                {
                    JS_FreeValue(context, patch);
                    JS_FreeValue(context, handle);
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return JS_ThrowTypeError(context, "entity.set callback must return an object");
                }

                JSValue entityPatch = clonePatchWithHandle(context, patch, handle);
                JS_FreeValue(context, patch);
                JS_FreeValue(context, handle);
                if (JS_IsException(entityPatch))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return entityPatch;
                }

                JSValue result = entitySetInternal(context, entityPatch);
                JS_FreeValue(context, entityPatch);
                if (JS_IsException(result))
                {
                    if (wantsOutput)
                    {
                        JS_FreeValue(context, array);
                    }
                    return result;
                }

                if (wantsOutput)
                {
                    JSValue output = mapEntityOutput(context, out, result);
                    JS_FreeValue(context, result);
                    if (JS_IsException(output))
                    {
                        JS_FreeValue(context, array);
                        return output;
                    }
                    JS_SetPropertyUint32(context, array, i, output);
                }
                else
                {
                    JS_FreeValue(context, result);
                }
            }

            return wantsOutput ? array : JS_UNDEFINED;
        }

        JSValue jsEntityGet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "entity.get expects id, handle, or an array of ids/handles");
            }

            if (JS_IsArray(argv[0]))
            {
                JSValueConst filter = JS_UNDEFINED;
                if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
                {
                    if (!JS_IsFunction(context, argv[1]))
                    {
                        return JS_ThrowTypeError(context, "entity.get filter must be a function");
                    }
                    filter = argv[1];
                }

                JSValueConst out = JS_UNDEFINED;
                if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]))
                {
                    if (!JS_IsFunction(context, argv[2]))
                    {
                        return JS_ThrowTypeError(context, "entity.get out must be a function");
                    }
                    out = argv[2];
                }

                return entityGetArray(context, argv[0], filter, out);
            }

            JSValueConst out = JS_UNDEFINED;
            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
            {
                if (!JS_IsFunction(context, argv[1]))
                {
                    return JS_ThrowTypeError(context, "entity.get out must be a function");
                }
                out = argv[1];
            }

            AcDbObjectId objectId;
            const Acad::ErrorStatus status = objectIdFromHandleValue(objectId, context, argv[0]);
            if (status != Acad::eOk)
            {
                return JS_NULL;
            }

            JSValue entity = entityToObject(context, objectId);
            if (JS_IsException(entity) || JS_IsNull(entity))
            {
                return entity;
            }

            JSValue output = mapEntityOutput(context, out, entity);
            JS_FreeValue(context, entity);
            return output;
        }

        JSValue jsEntityGetBlockEntities(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "entity.getBlockEntities expects block reference id or handle");
            }

            AcDbObjectId objectId;
            const Acad::ErrorStatus status = objectIdFromHandleValue(objectId, context, argv[0]);
            if (status != Acad::eOk || !objectId.isValid() || objectId.isErased())
            {
                return JS_NewArray(context);
            }

            AcDbEntity *pEnt = nullptr;
            if (acdbOpenObject(pEnt, objectId, AcDb::kForRead) != Acad::eOk || pEnt == nullptr)
            {
                return JS_NewArray(context);
            }

            AcDbBlockReference *pBlkRef = AcDbBlockReference::cast(pEnt);
            if (pBlkRef == nullptr)
            {
                pEnt->close();
                return JS_NewArray(context);
            }

            AcDbObjectId btrId = pBlkRef->blockTableRecord();
            pBlkRef->close();

            if (!btrId.isValid() || btrId.isErased())
            {
                return JS_NewArray(context);
            }

            AcDbBlockTableRecord *pBTR = nullptr;
            if (acdbOpenObject(pBTR, btrId, AcDb::kForRead) != Acad::eOk || pBTR == nullptr)
            {
                return JS_NewArray(context);
            }

            JSValue resArr = JS_NewArray(context);
            uint32_t outIdx = 0;

            AcDbBlockTableRecordIterator *pIter = nullptr;
            if (pBTR->newIterator(pIter) == Acad::eOk && pIter != nullptr)
            {
                for (; !pIter->done(); pIter->step())
                {
                    AcDbObjectId childId;
                    if (pIter->getEntityId(childId) == Acad::eOk && childId.isValid() && !childId.isErased())
                    {
                        JSValue childVal = entityToObject(context, childId);
                        if (!JS_IsNull(childVal) && !JS_IsUndefined(childVal) && !JS_IsException(childVal))
                        {
                            JS_SetPropertyUint32(context, resArr, outIdx++, childVal);
                        }
                        else
                        {
                            JS_FreeValue(context, childVal);
                        }
                    }
                }
                delete pIter;
            }

            pBTR->close();
            return resArr;
        }

        JSValue jsEntitySet(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            (void)thisValue;
            if (argc >= 1 && JS_IsArray(argv[0]))
            {
                JSValueConst out = argc >= 2 && JS_IsFunction(context, argv[argc - 1]) ? argv[argc - 1] : JS_UNDEFINED;
                return entitySetPatchArray(context, argv[0], out);
            }

            if (argc >= 2 && JS_IsFunction(context, argv[0]) && JS_IsArray(argv[1]))
            {
                JSValueConst out = argc >= 3 && JS_IsFunction(context, argv[argc - 1]) ? argv[argc - 1] : JS_UNDEFINED;
                return entitySetPatchFactoryArray(context, argv[0], argv[1], out);
            }

            if (argc >= 2 && JS_IsObject(argv[0]) && !JS_IsArray(argv[0]) && JS_IsArray(argv[1]))
            {
                JSValueConst out = argc >= 3 && JS_IsFunction(context, argv[argc - 1]) ? argv[argc - 1] : JS_UNDEFINED;
                return entitySetSharedPatchArray(context, argv[0], argv[1], out);
            }

            if (argc < 1 || !JS_IsObject(argv[0]))
            {
                if (argc >= 2 && JS_IsObject(argv[1]) && !JS_IsFunction(context, argv[1]) && !JS_IsArray(argv[1]))
                {
                    JSValue patch = JS_NewObject(context);
                    if (JS_IsException(patch))
                    {
                        return patch;
                    }

                    JSValue error = copyObjectProperties(context, argv[1], patch);
                    if (JS_IsException(error))
                    {
                        JS_FreeValue(context, patch);
                        return error;
                    }

                    JS_SetPropertyStr(context, patch, JS_IsString(argv[0]) ? "hand" : "id", JS_DupValue(context, argv[0]));
                    JSValue result = entitySetInternal(context, patch);
                    JSValueConst out = argc >= 3 && JS_IsFunction(context, argv[argc - 1]) ? argv[argc - 1] : JS_UNDEFINED;
                    JS_FreeValue(context, patch);
                    return entitySetOutput(context, out, result);
                }

                return JS_ThrowTypeError(context, "entity.set expects an object, an object array, (idOrHandle, object), (object, idOrHandle array), or (function, idOrHandle array), with optional final output function");
            }

            JSValue result = entitySetInternal(context, argv[0]);
            JSValueConst out = argc >= 2 && JS_IsFunction(context, argv[argc - 1]) ? argv[argc - 1] : JS_UNDEFINED;
            return entitySetOutput(context, out, result);
        }

        JSValue jsEntityPick(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            EntitySelectionArgs args;
            JSValue parsed = parseSelectionArgs(context, argc, argv, args);
            if (JS_IsException(parsed))
            {
                return parsed;
            }

            for (;;)
            {
                ads_name entityName;
                acdbNameClear(entityName);
                ads_point pickPoint;
                const int status = acedEntSel(args.prompt.c_str(), entityName, pickPoint);
                if (status == RTCAN || status == RTNONE)
                {
                    freeSelectionArgs(context, args);
                    return JS_NULL;
                }

                if (status != RTNORM)
                {
                    freeSelectionArgs(context, args);
                    return JS_ThrowInternalError(context, "entity.pick failed with status %d", status);
                }

                JSValue entity = entityToObject(context, entityName);
                if (JS_IsException(entity))
                {
                    freeSelectionArgs(context, args);
                    return entity;
                }

                bool accepted = true;
                JSValue error = passesFilter(context, args.filter, entity, accepted);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, entity);
                    freeSelectionArgs(context, args);
                    return error;
                }

                if (accepted)
                {
                    JSValue selectedEntity = JS_DupValue(context, entity);
                    JSValue setResult = JS_UNDEFINED;
                    if (!JS_IsUndefined(args.set) && !JS_IsNull(args.set))
                    {
                        if (JS_IsFunction(context, args.set))
                        {
                            JSValue callArgs[] = {JS_DupValue(context, entity)};
                            setResult = JS_Call(context, args.set, JS_UNDEFINED, 1, callArgs);
                            JS_FreeValue(context, callArgs[0]);
                            if (JS_IsException(setResult))
                            {
                                JS_FreeValue(context, selectedEntity);
                                JS_FreeValue(context, entity);
                                acutPrintf(ACRX_T("\nSelected entity was rejected by filter."));
                                freeSelectionArgs(context, args);
                                return setResult;
                            }
                        }
                        else
                        {
                            setResult = JS_DupValue(context, args.set);
                        }

                        if (!JS_IsUndefined(setResult) && !JS_IsNull(setResult))
                        {
                            JSValue patch = prepareEntitySetPatch(context, entity, setResult);
                            JS_FreeValue(context, setResult);
                            if (JS_IsException(patch))
                            {
                                JS_FreeValue(context, selectedEntity);
                                JS_FreeValue(context, entity);
                                freeSelectionArgs(context, args);
                                return patch;
                            }

                            JS_FreeValue(context, selectedEntity);
                            selectedEntity = entitySetInternal(context, patch);
                            JS_FreeValue(context, patch);
                            if (JS_IsException(selectedEntity))
                            {
                                JS_FreeValue(context, entity);
                                freeSelectionArgs(context, args);
                                return selectedEntity;
                            }
                        }
                    }

                    JS_FreeValue(context, entity);
                    if (args.suppressOutput)
                    {
                        JS_FreeValue(context, selectedEntity);
                        freeSelectionArgs(context, args);
                        return JS_NULL;
                    }

                    JSValue output = mapEntityOutput(context, args.out, selectedEntity);
                    JS_FreeValue(context, selectedEntity);
                    freeSelectionArgs(context, args);
                    if (JS_IsException(output))
                    {
                        return output;
                    }
                    return output;
                }

                JS_FreeValue(context, entity);
                acutPrintf(ACRX_T("\nSelected entity was rejected by filter."));
            }
        }

        JSValue jsEntitySelect(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            EntitySelectionArgs args;
            JSValue parsed = parseSelectionArgs(context, argc, argv, args);
            if (JS_IsException(parsed))
            {
                return parsed;
            }

            ads_name selectionSet;
            acdbNameClear(selectionSet);
            int status = RTERROR;
            bool fromImplied = false;

            if (args.useImplied)
            {
                status = acedSSGet(ACRX_T("I"), nullptr, nullptr, nullptr, selectionSet);
                if (status == RTNORM)
                {
                    Adesk::Int32 length = 0;
                    if (acedSSLength(selectionSet, &length) == RTNORM && length > 0)
                    {
                        fromImplied = true;
                        clearImpliedSelection();
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
                if (args.onlyImplied)
                {
                    freeSelectionArgs(context, args);
                    return args.suppressOutput ? JS_NULL : JS_NewArray(context);
                }

                if (!args.prompt.empty())
                {
                    acutPrintf(ACRX_T("\n%s"), args.prompt.c_str());
                }

                status = acedSSGet(nullptr, nullptr, nullptr, nullptr, selectionSet);
                if (status == RTCAN || status == RTNONE)
                {
                    freeSelectionArgs(context, args);
                    return args.suppressOutput ? JS_NULL : JS_NewArray(context);
                }

                if (status != RTNORM)
                {
                    freeSelectionArgs(context, args);
                    return JS_ThrowInternalError(context, "entity.select failed with status %d", status);
                }
            }

            Adesk::Int32 length = 0;
            if (acedSSLength(selectionSet, &length) != RTNORM)
            {
                acedSSFree(selectionSet);
                return JS_ThrowInternalError(context, "failed to read selection set length");
            }

            JSValue array = args.suppressOutput ? JS_NULL : JS_NewArray(context);
            uint32_t acceptedIndex = 0;
            for (Adesk::Int32 i = 0; i < length; ++i)
            {
                ads_name entityName;
                acdbNameClear(entityName);
                if (acedSSName(selectionSet, i, entityName) != RTNORM)
                {
                    continue;
                }

                JSValue entity = entityToObject(context, entityName);
                if (JS_IsException(entity))
                {
                    acedSSFree(selectionSet);
                    freeSelectionArgs(context, args);
                    return entity;
                }

                bool accepted = true;
                JSValue error = passesFilter(context, args.filter, entity, accepted);
                if (JS_IsException(error))
                {
                    JS_FreeValue(context, entity);
                    acedSSFree(selectionSet);
                    freeSelectionArgs(context, args);
                    return error;
                }

                if (accepted)
                {
                    JSValue selectedEntity = JS_DupValue(context, entity);
                    JSValue setResult = JS_UNDEFINED;
                    if (!JS_IsUndefined(args.set) && !JS_IsNull(args.set))
                    {
                        if (JS_IsFunction(context, args.set))
                        {
                            JSValue callArgs[] = {JS_DupValue(context, entity)};
                            setResult = JS_Call(context, args.set, JS_UNDEFINED, 1, callArgs);
                            JS_FreeValue(context, callArgs[0]);
                            if (JS_IsException(setResult))
                            {
                                JS_FreeValue(context, selectedEntity);
                                JS_FreeValue(context, entity);
                                acedSSFree(selectionSet);
                                freeSelectionArgs(context, args);
                                return setResult;
                            }
                        }
                        else
                        {
                            setResult = JS_DupValue(context, args.set);
                        }

                        if (!JS_IsUndefined(setResult) && !JS_IsNull(setResult))
                        {
                            JSValue patch = prepareEntitySetPatch(context, entity, setResult);
                            JS_FreeValue(context, setResult);
                            if (JS_IsException(patch))
                            {
                                JS_FreeValue(context, selectedEntity);
                                JS_FreeValue(context, entity);
                                acedSSFree(selectionSet);
                                freeSelectionArgs(context, args);
                                return patch;
                            }

                            JS_FreeValue(context, selectedEntity);
                            selectedEntity = entitySetInternal(context, patch);
                            JS_FreeValue(context, patch);
                            if (JS_IsException(selectedEntity))
                            {
                                JS_FreeValue(context, entity);
                                acedSSFree(selectionSet);
                                freeSelectionArgs(context, args);
                                return selectedEntity;
                            }
                        }
                    }

                    JS_FreeValue(context, entity);
                    if (args.suppressOutput)
                    {
                        JS_FreeValue(context, selectedEntity);
                        continue;
                    }

                    JSValue output = mapEntityOutput(context, args.out, selectedEntity);
                    JS_FreeValue(context, selectedEntity);
                    if (JS_IsException(output))
                    {
                        acedSSFree(selectionSet);
                        freeSelectionArgs(context, args);
                        return output;
                    }
                    JS_SetPropertyUint32(context, array, acceptedIndex++, output);
                }
                else
                {
                    JS_FreeValue(context, entity);
                }
            }

            // If caller requested a named selection set for Lisp, build a new
            // selection set with only the accepted members and make it the
            // implied pickfirst selection. The user can then capture it in
            // AutoLISP using `(setq <name> (ssget "P"))`.
            if (!args.name.empty())
            {
                // We cannot safely construct a named AutoLISP selection set here
                // across all ADS APIs. Instead, make the user aware how to
                // capture the implied pickfirst selection into a Lisp variable.
                // The filtered results are returned to JS; to capture them in
                // AutoLISP, first set the implied selection using the command
                // line (e.g. by selecting) and then run:
                acutPrintf(ACRX_T("\nTo capture selection in AutoLISP: (setq %s (ssget \"P\"))"), args.name.c_str());
            }

            acedSSFree(selectionSet);
            freeSelectionArgs(context, args);
            return array;
        }

        JSValue createEntityObject(JSContext *context)
        {
            JSValue entity = JS_NewObject(context);
            JS_SetPropertyStr(context, entity, "get", JS_NewCFunction(context, jsEntityGet, "get", 2));
            JS_SetPropertyStr(context, entity, "getBlockEntities", JS_NewCFunction(context, jsEntityGetBlockEntities, "getBlockEntities", 1));
            JS_SetPropertyStr(context, entity, "set", JS_NewCFunction(context, jsEntitySet, "set", 2));
            JS_SetPropertyStr(context, entity, "pick", JS_NewCFunction(context, jsEntityPick, "pick", 2));
            JS_SetPropertyStr(context, entity, "select", JS_NewCFunction(context, jsEntitySelect, "select", 4));
            safe_commands::registerSafeCommands(context, entity, true);
            return entity;
        }
    }

    JSValue createEntityModule(JSContext *context)
    {
        return createEntityObject(context);
    }

    void installEntity(JSContext *context)
    {
        JSValue global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "entity", createEntityModule(context));
        JS_FreeValue(context, global);
    }
}

#include "safe_commands.h"
#include "query_dsl/query_dsl.h"
#include "safe_transaction.h"
#include "selection_event.h"
#include "text_util.h"
#include "js_modules/js_modules.h"
#include "js_modules/js_module_utils.h"
#include "js_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <regex>

#include "aced.h"
#include "acedads.h"
#include "adscodes.h"
#include "acutads.h"
#include "acutmem.h"
#include "dbapserv.h"
#include "dbmain.h"
#include "dbsymtb.h"
#include "dbtrans.h"
#include "actrans.h"
#include "acdocman.h"
#include "rxclass.h"
#include "dbcurve.h"
#include "dbents.h"
#include "dbhatch.h"
#include "dbmline.h"
#include "dbpl.h"
#include "dbregion.h"
#include "dbspline.h"
#include "gevec3d.h"
#include "acedCmdNF.h"

namespace safe_commands
{
    namespace
    {
        // History of last successful query strings per command
        static std::string s_lastQuerySsSet = "{clr: 2, data: {code: 'CT2'}}";
        static std::string s_lastQuerySsGet = "({ filter: e => true, implied: true })";
        static std::string s_lastQueryESet = "({ set: e => ({ layer: \"0\" }) })";
        static std::string s_lastQueryEGet = "({ implied: false })";
        static std::string s_lastQueryEShow = "e => ({ a: e.a })";
        static std::string s_lastQuerySsQuery = "({ implied: true })";

        std::string handleToString(const AcDbHandle &handle)
        {
            char text[32] = {};
            std::sprintf(text, "%llX", static_cast<unsigned long long>(static_cast<Adesk::UInt64>(handle)));
            return text;
        }

        Acad::ErrorStatus objectIdFromHandleValue(AcDbObjectId &objectId, JSContext *context, JSValueConst value)
        {
            objectId = AcDbObjectId::kNull;
            AcDbDatabase *db = acdbHostApplicationServices()->workingDatabase();
            if (db == nullptr)
            {
                return Acad::eNoDatabase;
            }

            AcDbHandle handle;
            if (JS_IsNumber(value))
            {
                int64_t integerHandle = 0;
                if (JS_ToInt64(context, &integerHandle, value) < 0 || integerHandle <= 0)
                {
                    return Acad::eInvalidInput;
                }
                handle = AcDbHandle(static_cast<Adesk::UInt64>(integerHandle));
            }
            else if (JS_IsString(value))
            {
                const std::string text = js_modules::valueToString(context, value);
                if (text.empty())
                {
                    return Acad::eInvalidInput;
                }
                const std::basic_string<ACHAR> nativeText = command_text::fromUtf8(text);
                handle = AcDbHandle(nativeText.c_str());
            }
            else if (JS_IsObject(value))
            {
                JSValue idProp = JS_GetPropertyStr(context, value, "id");
                JSValue handProp = JS_GetPropertyStr(context, value, "hand");
                if (JS_IsUndefined(handProp))
                {
                    handProp = JS_GetPropertyStr(context, value, "handle");
                }
                Acad::ErrorStatus status = Acad::eInvalidInput;
                if (!JS_IsUndefined(idProp) && !JS_IsNull(idProp))
                {
                    status = objectIdFromHandleValue(objectId, context, idProp);
                }
                else if (!JS_IsUndefined(handProp) && !JS_IsNull(handProp))
                {
                    status = objectIdFromHandleValue(objectId, context, handProp);
                }
                JS_FreeValue(context, idProp);
                JS_FreeValue(context, handProp);
                return status;
            }
            else
            {
                return Acad::eInvalidInput;
            }

            if (handle.isNull())
            {
                return Acad::eNullHandle;
            }

            return db->getAcDbObjectId(objectId, false, handle);
        }

        Acad::ErrorStatus objectIdFromAdsName(AcDbObjectId &objectId, const ads_name entityName)
        {
            objectId = AcDbObjectId::kNull;
            return acdbGetObjectId(objectId, entityName);
        }

        // =====================================================================
        // Direct invocation of entity.get(hand) and entity.set(hand, patch)
        // =====================================================================

        JSValue callEntityGet(JSContext *context, const std::string &handleStr)
        {
            JSValue global = JS_GetGlobalObject(context);
            JSValue entityObj = JS_GetPropertyStr(context, global, "entity");
            JSValue getFn = JS_GetPropertyStr(context, entityObj, "get");

            JSValue handVal = JS_NewString(context, handleStr.c_str());
            JSValueConst argv[1] = { handVal };
            JSValue result = JS_Call(context, getFn, entityObj, 1, argv);

            JS_FreeValue(context, handVal);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, entityObj);
            JS_FreeValue(context, global);
            return result;
        }

        JSValue callEntitySet(JSContext *context, const std::string &handleStr, JSValueConst patch)
        {
            JSValue global = JS_GetGlobalObject(context);
            JSValue entityObj = JS_GetPropertyStr(context, global, "entity");
            JSValue setFn = JS_GetPropertyStr(context, entityObj, "set");

            JSValue handVal = JS_NewString(context, handleStr.c_str());
            JSValueConst argv[2] = { handVal, patch };
            JSValue result = JS_Call(context, setFn, entityObj, 2, argv);

            JS_FreeValue(context, handVal);
            JS_FreeValue(context, setFn);
            JS_FreeValue(context, entityObj);
            JS_FreeValue(context, global);
            return result;
        }

        bool isReservedOptionKey(const std::string &key)
        {
            static const std::unordered_set<std::string> s_reserved = {
                "filter", "where", "get", "out", "entities", "ids",
                "implied", "prompt", "mode", "selectionMode", "onlyImplied",
                "set", "init", "post", "query"
            };
            return s_reserved.find(key) != s_reserved.end();
        }

        bool hasPatternProperties(JSContext *context, JSValueConst obj)
        {
            if (!JS_IsObject(obj) || JS_IsNull(obj)) return false;
            JSPropertyEnum *tab = nullptr;
            uint32_t len = 0;
            if (JS_GetOwnPropertyNames(context, &tab, &len, obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0)
            {
                return false;
            }
            bool foundPattern = false;
            for (uint32_t i = 0; i < len; ++i)
            {
                const char *prop = JS_AtomToCString(context, tab[i].atom);
                if (prop)
                {
                    std::string pStr(prop);
                    JS_FreeCString(context, prop);
                    if (!isReservedOptionKey(pStr))
                    {
                        foundPattern = true;
                        break;
                    }
                }
            }
            for (uint32_t i = 0; i < len; ++i)
            {
                JS_FreeAtom(context, tab[i].atom);
            }
            js_free(context, tab);
            return foundPattern;
        }

        bool matchesPattern(JSContext *context, JSValueConst pattern, JSValueConst target, bool isTopLevelOptions = false, JSValueConst rootTarget = JS_UNDEFINED);

        bool valuesMatch(JSContext *context, JSValueConst expected, JSValueConst actual)
        {
            if (JS_IsFunction(context, expected))
            {
                JSValueConst argv[1] = { actual };
                JSValue res = JS_Call(context, expected, JS_UNDEFINED, 1, argv);
                if (JS_IsException(res))
                {
                    JSValue exc = JS_GetException(context);
                    JS_FreeValue(context, exc);
                    JS_FreeValue(context, res);
                    return false;
                }
                bool pass = JS_ToBool(context, res) != 0;
                JS_FreeValue(context, res);
                return pass;
            }

            if (JS_IsArray(expected))
            {
                JSValue lenVal = JS_GetPropertyStr(context, expected, "length");
                int64_t arrLen = 0;
                JS_ToInt64(context, &arrLen, lenVal);
                JS_FreeValue(context, lenVal);

                for (int64_t i = 0; i < arrLen; ++i)
                {
                    JSValue elem = JS_GetPropertyUint32(context, expected, static_cast<uint32_t>(i));
                    bool matched = valuesMatch(context, elem, actual);
                    JS_FreeValue(context, elem);
                    if (matched) return true;
                }
                return false;
            }

            // RegExp test
            JSValue testProp = JS_GetPropertyStr(context, expected, "test");
            if (JS_IsFunction(context, testProp))
            {
                JS_SetPropertyStr(context, expected, "lastIndex", JS_NewInt32(context, 0));
                JSValue actStr = JS_UNDEFINED;
                if (!JS_IsString(actual))
                {
                    actStr = JS_ToString(context, actual);
                }
                else
                {
                    actStr = JS_DupValue(context, actual);
                }
                JSValueConst argv[1] = { actStr };
                JSValue res = JS_Call(context, testProp, expected, 1, argv);
                JS_FreeValue(context, actStr);
                JS_FreeValue(context, testProp);
                if (JS_IsException(res))
                {
                    JSValue exc = JS_GetException(context);
                    JS_FreeValue(context, exc);
                    JS_FreeValue(context, res);
                    return false;
                }
                bool pass = JS_ToBool(context, res) != 0;
                JS_FreeValue(context, res);
                return pass;
            }
            JS_FreeValue(context, testProp);

            if (JS_IsObject(expected) && !JS_IsNull(expected))
            {
                JSValue notProp = JS_GetPropertyStr(context, expected, "not");
                if (!JS_IsUndefined(notProp))
                {
                    bool innerMatch = valuesMatch(context, notProp, actual);
                    JS_FreeValue(context, notProp);
                    return !innerMatch;
                }

                if (!JS_IsObject(actual) || JS_IsNull(actual))
                {
                    if (JS_IsNumber(actual))
                    {
                        int32_t aci = 0;
                        if (JS_ToInt32(context, &aci, actual) == 0 && aci >= 0 && aci <= 256)
                        {
                            JSValue hasR = JS_GetPropertyStr(context, expected, "r");
                            JSValue hasG = JS_GetPropertyStr(context, expected, "g");
                            JSValue hasB = JS_GetPropertyStr(context, expected, "b");
                            bool hasRgb = !JS_IsUndefined(hasR) || !JS_IsUndefined(hasG) || !JS_IsUndefined(hasB);
                            JS_FreeValue(context, hasR);
                            JS_FreeValue(context, hasG);
                            JS_FreeValue(context, hasB);
                            if (hasRgb)
                            {
                                AcCmColor col;
                                col.setColorIndex(static_cast<Adesk::UInt16>(aci));
                                JSValue rgbObj = JS_NewObject(context);
                                JS_SetPropertyStr(context, rgbObj, "r", JS_NewInt32(context, col.red()));
                                JS_SetPropertyStr(context, rgbObj, "g", JS_NewInt32(context, col.green()));
                                JS_SetPropertyStr(context, rgbObj, "b", JS_NewInt32(context, col.blue()));
                                bool matched = matchesPattern(context, expected, rgbObj, false);
                                JS_FreeValue(context, rgbObj);
                                return matched;
                            }
                        }
                    }
                    return false;
                }
                return matchesPattern(context, expected, actual, false);
            }

            if (JS_IsNumber(expected))
            {
                if (JS_IsNumber(actual))
                {
                    double dExp = 0, dAct = 0;
                    JS_ToFloat64(context, &dExp, expected);
                    JS_ToFloat64(context, &dAct, actual);
                    return std::abs(dExp - dAct) < 1e-6;
                }
                if (JS_IsObject(actual) && !JS_IsNull(actual))
                {
                    int32_t expAci = 0;
                    if (JS_ToInt32(context, &expAci, expected) == 0 && expAci >= 0 && expAci <= 256)
                    {
                        JSValue actR = JS_GetPropertyStr(context, actual, "r");
                        JSValue actG = JS_GetPropertyStr(context, actual, "g");
                        JSValue actB = JS_GetPropertyStr(context, actual, "b");
                        if (JS_IsNumber(actR) && JS_IsNumber(actG) && JS_IsNumber(actB))
                        {
                            int32_t r = 0, g = 0, b = 0;
                            JS_ToInt32(context, &r, actR);
                            JS_ToInt32(context, &g, actG);
                            JS_ToInt32(context, &b, actB);
                            AcCmColor col;
                            col.setColorIndex(static_cast<Adesk::UInt16>(expAci));
                            bool match = (col.red() == r && col.green() == g && col.blue() == b);
                            JS_FreeValue(context, actR);
                            JS_FreeValue(context, actG);
                            JS_FreeValue(context, actB);
                            return match;
                        }
                        JS_FreeValue(context, actR);
                        JS_FreeValue(context, actG);
                        JS_FreeValue(context, actB);
                    }
                }
                return false;
            }

            if (JS_IsString(expected))
            {
                const char *sExp = JS_ToCString(context, expected);
                if (sExp)
                {
                    if (_stricmp(sExp, "any") == 0)
                    {
                        JS_FreeCString(context, sExp);
                        return !JS_IsUndefined(actual) && !JS_IsNull(actual);
                    }
                    if (std::strcmp(sExp, "*") == 0)
                    {
                        JS_FreeCString(context, sExp);
                        if (JS_IsUndefined(actual) || JS_IsNull(actual)) return false;
                        if (JS_IsString(actual))
                        {
                            const char *sAct = JS_ToCString(context, actual);
                            bool ok = (sAct && sAct[0] != '\0');
                            if (sAct) JS_FreeCString(context, sAct);
                            return ok;
                        }
                        return true;
                    }

                    if (JS_IsString(actual))
                    {
                        const char *sAct = JS_ToCString(context, actual);
                        bool match = false;
                        if (sAct)
                        {
                            if (std::strcmp(sExp, sAct) == 0 || _stricmp(sExp, sAct) == 0)
                            {
                                match = true;
                            }
                            else
                            {
                                std::string prefixed = std::string("AcDb") + sExp;
                                if (_stricmp(prefixed.c_str(), sAct) == 0)
                                {
                                    match = true;
                                }
                            }

                            if (!match && std::strpbrk(sExp, "*?#@.~[]") != nullptr)
                            {
                                std::basic_string<ACHAR> actW = command_text::fromUtf8(sAct);
                                std::basic_string<ACHAR> expW = command_text::fromUtf8(sExp);
                                for (auto &c : actW) c = static_cast<ACHAR>(towupper(c));
                                for (auto &c : expW) c = static_cast<ACHAR>(towupper(c));
                                if (acutWcMatch(actW.c_str(), expW.c_str()) == RTNORM)
                                {
                                    match = true;
                                }
                            }

                            if (!match && std::strpbrk(sExp, ".*+?^$|()[]{}") != nullptr)
                            {
                                try
                                {
                                    std::regex re(sExp, std::regex_constants::icase);
                                    if (std::regex_match(sAct, re))
                                    {
                                        match = true;
                                    }
                                }
                                catch (...)
                                {
                                }
                            }
                            JS_FreeCString(context, sAct);
                        }
                        JS_FreeCString(context, sExp);
                        return match;
                    }
                    else if (JS_IsNumber(actual))
                    {
                        double dAct = 0;
                        JS_ToFloat64(context, &dAct, actual);
                        bool match = false;
                        if (sExp[0] == '>' || sExp[0] == '<' || sExp[0] == '!')
                        {
                            const char *numPart = sExp + 1;
                            if (*numPart == '=') numPart++;
                            while (*numPart == ' ') numPart++;
                            char *endPtr = nullptr;
                            double threshold = std::strtod(numPart, &endPtr);
                            if (endPtr != numPart)
                            {
                                if (sExp[0] == '>' && sExp[1] == '=') match = (dAct >= threshold);
                                else if (sExp[0] == '>') match = (dAct > threshold);
                                else if (sExp[0] == '<' && sExp[1] == '=') match = (dAct <= threshold);
                                else if (sExp[0] == '<') match = (dAct < threshold);
                                else if (sExp[0] == '!' && sExp[1] == '=') match = (std::abs(dAct - threshold) > 1e-6);
                            }
                        }
                        else
                        {
                            char buf[64];
                            std::snprintf(buf, sizeof(buf), "%.6g", dAct);
                            if (std::strcmp(buf, sExp) == 0) match = true;
                        }
                        JS_FreeCString(context, sExp);
                        return match;
                    }
                    JS_FreeCString(context, sExp);
                }
                return false;
            }

            if (JS_IsBool(expected))
            {
                return JS_IsBool(actual) && (JS_ToBool(context, expected) == JS_ToBool(context, actual));
            }

            if (JS_IsNull(expected)) return JS_IsNull(actual);
            if (JS_IsUndefined(expected)) return JS_IsUndefined(actual);

            return false;
        }

        bool matchesPattern(JSContext *context, JSValueConst pattern, JSValueConst target, bool isTopLevelOptions, JSValueConst rootTarget)
        {
            if (!JS_IsObject(pattern) || JS_IsNull(pattern))
            {
                return true;
            }
            if (!JS_IsObject(target) || JS_IsNull(target))
            {
                return false;
            }

            JSPropertyEnum *tab = nullptr;
            uint32_t len = 0;
            if (JS_GetOwnPropertyNames(context, &tab, &len, pattern, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0)
            {
                return true;
            }

            bool allMatched = true;
            for (uint32_t i = 0; i < len; ++i)
            {
                const char *prop = JS_AtomToCString(context, tab[i].atom);
                if (!prop) continue;
                std::string propName(prop);
                JS_FreeCString(context, prop);

                if (propName == "where" || propName == "filter")
                {
                    JSValue whereFn = JS_GetProperty(context, pattern, tab[i].atom);
                    if (JS_IsFunction(context, whereFn))
                    {
                        JSValueConst argv[1] = { target };
                        JSValue res = JS_Call(context, whereFn, JS_UNDEFINED, 1, argv);
                        bool pass = true;
                        if (JS_IsException(res))
                        {
                            JSValue exc = JS_GetException(context);
                            JS_FreeValue(context, exc);
                            pass = false;
                        }
                        else
                        {
                            pass = JS_ToBool(context, res) != 0;
                            JS_FreeValue(context, res);
                        }
                        JS_FreeValue(context, whereFn);
                        if (!pass)
                        {
                            allMatched = false;
                            break;
                        }
                        continue;
                    }
                    JS_FreeValue(context, whereFn);
                }

                if (isTopLevelOptions && isReservedOptionKey(propName))
                {
                    continue;
                }

                JSValue expected = JS_GetProperty(context, pattern, tab[i].atom);
                JSValue actual = JS_GetProperty(context, target, tab[i].atom);

                if (JS_IsUndefined(actual))
                {
                    if (propName == "clr") actual = JS_GetPropertyStr(context, target, "color");
                    else if (propName == "color") actual = JS_GetPropertyStr(context, target, "clr");
                    else if (propName == "lyr") actual = JS_GetPropertyStr(context, target, "layer");
                    else if (propName == "layer") actual = JS_GetPropertyStr(context, target, "lyr");
                    else if (propName == "ty") actual = JS_GetPropertyStr(context, target, "type");
                    else if (propName == "type") actual = JS_GetPropertyStr(context, target, "ty");
                    else if (propName == "hand") actual = JS_GetPropertyStr(context, target, "handle");
                    else if (propName == "handle") actual = JS_GetPropertyStr(context, target, "hand");
                    else if (propName == "trp") actual = JS_GetPropertyStr(context, target, "transparency");
                    else if (propName == "transparency") actual = JS_GetPropertyStr(context, target, "trp");
                    else if (propName == "e") actual = JS_DupValue(context, target);
                }

                if (JS_IsUndefined(actual))
                {
                    JSValue dataVal = JS_GetPropertyStr(context, target, "data");
                    if (JS_IsObject(dataVal) && !JS_IsNull(dataVal))
                    {
                        actual = JS_GetProperty(context, dataVal, tab[i].atom);
                    }
                    JS_FreeValue(context, dataVal);
                }

                // If property not found in current target and rootTarget is provided (e.g. searching inside 'data'),
                // fallback to checking root entity for standard properties (ty, lyr, clr, hand, etc.)
                if (JS_IsUndefined(actual) && !JS_IsUndefined(rootTarget) && !JS_IsNull(rootTarget))
                {
                    actual = JS_GetProperty(context, rootTarget, tab[i].atom);
                    if (JS_IsUndefined(actual))
                    {
                        if (propName == "clr") actual = JS_GetPropertyStr(context, rootTarget, "color");
                        else if (propName == "color") actual = JS_GetPropertyStr(context, rootTarget, "clr");
                        else if (propName == "lyr") actual = JS_GetPropertyStr(context, rootTarget, "layer");
                        else if (propName == "layer") actual = JS_GetPropertyStr(context, rootTarget, "lyr");
                        else if (propName == "ty") actual = JS_GetPropertyStr(context, rootTarget, "type");
                        else if (propName == "type") actual = JS_GetPropertyStr(context, rootTarget, "ty");
                        else if (propName == "hand") actual = JS_GetPropertyStr(context, rootTarget, "handle");
                        else if (propName == "handle") actual = JS_GetPropertyStr(context, rootTarget, "hand");
                    }
                }

                bool match = false;
                if (JS_IsObject(expected) && !JS_IsNull(expected) && !JS_IsFunction(context, expected) && !JS_IsArray(expected))
                {
                    JSValue notProp = JS_GetPropertyStr(context, expected, "not");
                    if (!JS_IsUndefined(notProp))
                    {
                        JS_FreeValue(context, notProp);
                        match = valuesMatch(context, expected, actual);
                    }
                    else
                    {
                        // Pass target as rootTarget when descending into child objects (e.g. data)
                        match = matchesPattern(context, expected, actual, false, target);
                    }
                }
                else
                {
                    match = valuesMatch(context, expected, actual);
                }

                JS_FreeValue(context, expected);
                JS_FreeValue(context, actual);

                if (!match)
                {
                    allMatched = false;
                    break;
                }
            }

            for (uint32_t i = 0; i < len; ++i)
            {
                JS_FreeAtom(context, tab[i].atom);
            }
            js_free(context, tab);

            return allMatched;
        }

        bool evaluatePredicate(JSContext *context, JSValueConst predicate, JSValueConst arg)
        {
            if (JS_IsUndefined(predicate) || JS_IsNull(predicate))
            {
                return true;
            }
            if (JS_IsFunction(context, predicate))
            {
                JSValueConst argv[1] = { arg };
                JSValue res = JS_Call(context, predicate, JS_UNDEFINED, 1, argv);
                if (JS_IsException(res))
                {
                    JSValue exc = JS_GetException(context);
                    JS_FreeValue(context, exc);
                    JS_FreeValue(context, res);
                    return false;
                }
                const bool pass = JS_ToBool(context, res) != 0;
                JS_FreeValue(context, res);
                return pass;
            }
            if (JS_IsObject(predicate))
            {
                return matchesPattern(context, predicate, arg, true);
            }
            return true;
        }

        struct CandidateItem
        {
            std::string hand;
            AcDbObjectId id;
        };

        std::vector<CandidateItem> collectCandidatesFromSelectionSet(ads_name ss, AcDbDatabase *targetDb)
        {
            std::vector<CandidateItem> candidates;
            Adesk::Int32 length = 0;
            if (acedSSLength(ss, &length) != RTNORM || length <= 0)
            {
                return candidates;
            }

            std::unordered_set<uint64_t> seen;
            for (Adesk::Int32 i = 0; i < length; ++i)
            {
                ads_name ent;
                if (acedSSName(ss, i, ent) == RTNORM)
                {
                    AcDbObjectId id;
                    if (acdbGetObjectId(id, ent) == Acad::eOk &&
                        id.isValid() && !id.isErased() && id.database() == targetDb)
                    {
                        const uint64_t rawId = static_cast<uint64_t>(id.asOldId());
                        if (seen.insert(rawId).second)
                        {
                            candidates.push_back({ handleToString(id.handle()), id });
                        }
                    }
                }
            }
            return candidates;
        }

        std::vector<CandidateItem> acquireSelectionCandidates(JSContext *context,
                                                             JSValueConst entitiesVal,
                                                             const char *promptStr,
                                                             AcDbDatabase *targetDb)
        {
            std::vector<CandidateItem> candidates;

            if (JS_IsArray(entitiesVal))
            {
                const uint32_t len = js_modules::arrayLength(context, entitiesVal);
                std::unordered_set<uint64_t> seen;
                for (uint32_t i = 0; i < len; ++i)
                {
                    JSValue elem = JS_GetPropertyUint32(context, entitiesVal, i);
                    AcDbObjectId id;
                    if (objectIdFromHandleValue(id, context, elem) == Acad::eOk &&
                        id.isValid() && !id.isErased() && id.database() == targetDb)
                    {
                        const uint64_t rawId = static_cast<uint64_t>(id.asOldId());
                        if (seen.insert(rawId).second)
                        {
                            candidates.push_back({ handleToString(id.handle()), id });
                        }
                    }
                    JS_FreeValue(context, elem);
                }
                return candidates;
            }

            // Check pickfirst / implied selection first
            ads_name ssImplied;
            acdbNameClear(ssImplied);
            if (acedSSGet(ACRX_T("_:I"), nullptr, nullptr, nullptr, ssImplied) == RTNORM)
            {
                candidates = collectCandidatesFromSelectionSet(ssImplied, targetDb);
                acedSSFree(ssImplied);
                if (!candidates.empty())
                {
                    return candidates;
                }
            }

            // Interactive selection prompt
            ads_name ssPrompt;
            acdbNameClear(ssPrompt);
            const std::basic_string<ACHAR> nativePrompt = promptStr
                ? command_text::fromUtf8(promptStr)
                : ACRX_T("\nSelect objects: ");

            const int rc = acedSSGet(nullptr, nullptr, nullptr, nullptr, ssPrompt);
            if (rc == RTNORM)
            {
                candidates = collectCandidatesFromSelectionSet(ssPrompt, targetDb);
                acedSSFree(ssPrompt);
            }

            return candidates;
        }

        // Bracket balancing check for multi-line command query input
        bool isCompleteExpression(const std::string &code)
        {
            int paren = 0;
            int brace = 0;
            int bracket = 0;
            bool inString = false;
            char stringQuote = 0;
            bool escaping = false;

            for (size_t i = 0; i < code.length(); ++i)
            {
                const char ch = code[i];
                if (escaping)
                {
                    escaping = false;
                    continue;
                }
                if (ch == '\\' && inString)
                {
                    escaping = true;
                    continue;
                }
                if (inString)
                {
                    if (ch == stringQuote)
                    {
                        inString = false;
                    }
                    continue;
                }

                if (ch == '\'' || ch == '"' || ch == '`')
                {
                    inString = true;
                    stringQuote = ch;
                    continue;
                }

                // Skip single-line comments
                if (ch == '/' && i + 1 < code.length() && code[i + 1] == '/')
                {
                    size_t nextLine = code.find('\n', i + 2);
                    if (nextLine == std::string::npos) break;
                    i = nextLine;
                    continue;
                }

                if (ch == '(') paren++;
                else if (ch == ')') paren--;
                else if (ch == '{') brace++;
                else if (ch == '}') brace--;
                else if (ch == '[') bracket++;
                else if (ch == ']') bracket--;
            }

            if (inString) return false;
            if (paren > 0 || brace > 0 || bracket > 0) return false;

            // Check if ending with incomplete token
            size_t last = code.find_last_not_of(" \t\r\n");
            if (last != std::string::npos)
            {
                char lastChar = code[last];
                if (lastChar == ',' || lastChar == '+' || lastChar == '-' ||
                    lastChar == '*' || lastChar == '/' || lastChar == '?' ||
                    lastChar == ':' || lastChar == '=' || lastChar == '&' ||
                    lastChar == '|' || lastChar == '.')
                {
                    return false;
                }

                // Check if ending with keyword "return"
                if (last >= 5 && code.compare(last - 5, 6, "return") == 0)
                {
                    bool prevOk = (last == 5 || (!isalnum(static_cast<unsigned char>(code[last - 6])) && code[last - 6] != '_' && code[last - 6] != '$'));
                    if (prevOk) return false;
                }
            }

            return true;
        }

        static std::unordered_map<int, std::string> s_lastPlaceholderValues;

        std::vector<int> detectTemplatePlaceholders(const std::string &query)
        {
            std::set<int> uniquePlaceholders;
            const size_t len = query.size();
            for (size_t i = 0; i < len; ++i)
            {
                if (query[i] == '_')
                {
                    if (i > 0)
                    {
                        unsigned char prev = static_cast<unsigned char>(query[i - 1]);
                        if (std::isalnum(prev) || prev == '$' || prev == '_')
                        {
                            continue;
                        }
                    }
                    size_t j = i + 1;
                    while (j < len && std::isdigit(static_cast<unsigned char>(query[j])))
                    {
                        ++j;
                    }
                    if (j == i + 1)
                    {
                        continue;
                    }
                    if (j < len)
                    {
                        unsigned char next = static_cast<unsigned char>(query[j]);
                        if (std::isalnum(next) || next == '$' || next == '_')
                        {
                            continue;
                        }
                    }
                    try
                    {
                        int num = std::stoi(query.substr(i + 1, j - (i + 1)));
                        uniquePlaceholders.insert(num);
                    }
                    catch (...)
                    {
                    }
                    i = j - 1;
                }
            }
            return std::vector<int>(uniquePlaceholders.begin(), uniquePlaceholders.end());
        }

        std::string formatTemplateValueForJs(const std::string &rawInput)
        {
            std::string s = rawInput;
            size_t first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "\"\"";
            size_t last = s.find_last_not_of(" \t\r\n");
            s = s.substr(first, last - first + 1);

            if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"')))
            {
                return s;
            }
            if (s.size() >= 2 && ((s.front() == '[' && s.back() == ']') || (s.front() == '{' && s.back() == '}')))
            {
                return s;
            }
            if (s == "true" || s == "false" || s == "null" || s == "undefined")
            {
                return s;
            }
            char *endPtr = nullptr;
            errno = 0;
            (void)std::strtod(s.c_str(), &endPtr);
            if (endPtr != s.c_str() && *endPtr == '\0' && errno == 0)
            {
                return s;
            }

            std::string quoted = "\"";
            for (char c : s)
            {
                if (c == '"') quoted += "\\\"";
                else if (c == '\\') quoted += "\\\\";
                else if (c == '\n') quoted += "\\n";
                else if (c == '\r') quoted += "\\r";
                else if (c == '\t') quoted += "\\t";
                else quoted += c;
            }
            quoted += "\"";
            return quoted;
        }

        bool promptTemplatePlaceholders(const std::vector<int> &placeholders, std::unordered_map<int, std::string> &values)
        {
            for (int idx : placeholders)
            {
                const auto it = s_lastPlaceholderValues.find(idx);
                const std::string defaultVal = (it != s_lastPlaceholderValues.end()) ? it->second : "";

                ACHAR promptBuf[256] = {};
                if (!defaultVal.empty())
                {
                    swprintf(promptBuf, 256, ACRX_T("\n_%d <%s>: "), idx, command_text::fromUtf8(defaultVal).c_str());
                }
                else
                {
                    swprintf(promptBuf, 256, ACRX_T("\n_%d: "), idx);
                }

                ACHAR line[2048] = {};
                const int rc = acedGetString(1, promptBuf, line);
                if (rc != RTNORM)
                {
                    return false;
                }

                std::string rawInput = command_text::toUtf8(line);
                size_t first = rawInput.find_first_not_of(" \t\r\n");
                if (first == std::string::npos)
                {
                    if (!defaultVal.empty())
                    {
                        rawInput = defaultVal;
                    }
                    else
                    {
                        rawInput = "";
                    }
                }
                else
                {
                    size_t last = rawInput.find_last_not_of(" \t\r\n");
                    rawInput = rawInput.substr(first, last - first + 1);
                }

                s_lastPlaceholderValues[idx] = rawInput;
                values[idx] = formatTemplateValueForJs(rawInput);
            }
            return true;
        }

        std::string wrapWithPlaceholderBindings(const std::string &code, const std::unordered_map<int, std::string> &values)
        {
            if (values.empty())
            {
                return code;
            }
            std::string c = code;
            while (!c.empty() && (c.back() == ';' || c.back() == ' ' || c.back() == '\t' || c.back() == '\r' || c.back() == '\n'))
            {
                c.pop_back();
            }
            std::string wrapped = "(() => {\n";
            for (const auto &pair : values)
            {
                wrapped += "  const _" + std::to_string(pair.first) + " = " + pair.second + ";\n";
            }
            wrapped += "  return " + c + ";\n})()";
            return wrapped;
        }

        // Reads interactive Query <Last>: prompt with multi-line bracket matching
        bool promptQuery(const ACHAR *defaultQuery, std::string &lastQuery, std::string &outQuery)
        {
            const std::string defaultStr = lastQuery.empty()
                ? (defaultQuery ? command_text::toUtf8(defaultQuery) : "")
                : lastQuery;

            ACHAR promptBuf[512] = {};
            if (!defaultStr.empty())
            {
                swprintf(promptBuf, 512, ACRX_T("\nQuery <%s>: "), command_text::fromUtf8(defaultStr).c_str());
            }
            else
            {
                swprintf(promptBuf, 512, ACRX_T("\nQuery <Last>: "));
            }

            ACHAR line[4096] = {};
            const int rc = acedGetString(1, promptBuf, line);
            if (rc != RTNORM)
            {
                return false;
            }

            std::string text = command_text::toUtf8(line);
            size_t start = text.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
            {
                if (!defaultStr.empty())
                {
                    outQuery = defaultStr;
                    lastQuery = defaultStr;
                    return true;
                }
                outQuery = "";
                return true;
            }

            std::string accumulated = text.substr(start);
            while (!isCompleteExpression(accumulated))
            {
                ACHAR contLine[4096] = {};
                if (acedGetString(1, ACRX_T(""), contLine) != RTNORM)
                {
                    break;
                }
                accumulated += "\n";
                accumulated += command_text::toUtf8(contLine);
            }

            outQuery = accumulated;
            lastQuery = accumulated;
            return true;
        }

        static std::string s_lastSsQueryMode = "Last";

        bool promptSsQueryString(std::string &lastQuery, std::string &outQuery)
        {
            const bool hasLast = !lastQuery.empty();
            const std::string defaultMode = (hasLast && s_lastSsQueryMode == "Last") ? "Last" : (hasLast ? s_lastSsQueryMode : "Query");
            const ACHAR *promptStr = (defaultMode == "Last" && hasLast)
                ? ACRX_T("\nQuery [Query/Last]<Last>: ")
                : ACRX_T("\nQuery [Query/Last]<Query>: ");

            ACHAR line[4096] = {};
            const int rc = acedGetString(1, promptStr, line);
            if (rc != RTNORM)
            {
                return false;
            }

            std::string text = command_text::toUtf8(line);
            size_t start = text.find_first_not_of(" \t\r\n");
            std::string trimmed = (start == std::string::npos) ? "" : text.substr(start);

            // Case 1: Enter pressed (empty input)
            if (trimmed.empty())
            {
                if (defaultMode == "Last" && hasLast)
                {
                    outQuery = lastQuery;
                    s_lastSsQueryMode = "Last";
                    return true;
                }
                s_lastSsQueryMode = "Query";
                return promptQuery(ACRX_T(""), lastQuery, outQuery);
            }

            // Case 2: User typed L or Last
            if (_stricmp(trimmed.c_str(), "L") == 0 || _stricmp(trimmed.c_str(), "Last") == 0)
            {
                if (hasLast)
                {
                    outQuery = lastQuery;
                    s_lastSsQueryMode = "Last";
                    return true;
                }
                acutPrintf(ACRX_T("\nNo previous query found."));
                s_lastSsQueryMode = "Query";
                return promptQuery(ACRX_T(""), lastQuery, outQuery);
            }

            // Case 3: User typed Q or Query
            if (_stricmp(trimmed.c_str(), "Q") == 0 || _stricmp(trimmed.c_str(), "Query") == 0)
            {
                s_lastSsQueryMode = "Query";
                return promptQuery(ACRX_T(""), lastQuery, outQuery);
            }

            // Case 4: User typed or pasted the query directly
            std::string accumulated = trimmed;
            while (!isCompleteExpression(accumulated))
            {
                ACHAR contLine[4096] = {};
                if (acedGetString(1, ACRX_T(""), contLine) != RTNORM)
                {
                    break;
                }
                accumulated += "\n";
                accumulated += command_text::toUtf8(contLine);
            }

            outQuery = accumulated;
            lastQuery = accumulated;
            s_lastSsQueryMode = "Query";
            return true;
        }

        std::string fixReturnAsi(const std::string &code)
        {
            std::string result;
            result.reserve(code.size() + 16);
            size_t i = 0;
            const size_t len = code.size();
            bool inString = false;
            char quote = 0;

            while (i < len)
            {
                if (inString)
                {
                    if (code[i] == '\\' && i + 1 < len)
                    {
                        result.push_back(code[i++]);
                        result.push_back(code[i++]);
                        continue;
                    }
                    if (code[i] == quote)
                    {
                        inString = false;
                    }
                    result.push_back(code[i++]);
                    continue;
                }

                if (code[i] == '"' || code[i] == '\'' || code[i] == '`')
                {
                    inString = true;
                    quote = code[i];
                    result.push_back(code[i++]);
                    continue;
                }

                if (code[i] == '/' && i + 1 < len && code[i + 1] == '/')
                {
                    size_t nextLine = code.find('\n', i + 2);
                    if (nextLine == std::string::npos)
                    {
                        result.append(code.substr(i));
                        break;
                    }
                    result.append(code.substr(i, nextLine - i));
                    i = nextLine;
                    continue;
                }

                if (code[i] == '/' && i + 1 < len && code[i + 1] == '*')
                {
                    size_t endComment = code.find("*/", i + 2);
                    if (endComment == std::string::npos)
                    {
                        result.append(code.substr(i));
                        break;
                    }
                    result.append(code.substr(i, endComment + 2 - i));
                    i = endComment + 2;
                    continue;
                }

                if (code.compare(i, 6, "return") == 0)
                {
                    bool prevOk = (i == 0 || (!isalnum(static_cast<unsigned char>(code[i - 1])) && code[i - 1] != '_' && code[i - 1] != '$'));
                    bool nextOk = (i + 6 == len || (!isalnum(static_cast<unsigned char>(code[i + 6])) && code[i + 6] != '_' && code[i + 6] != '$'));
                    if (prevOk && nextOk)
                    {
                        size_t j = i + 6;
                        bool hasNewline = false;
                        while (j < len && (code[j] == ' ' || code[j] == '\t' || code[j] == '\r' || code[j] == '\n'))
                        {
                            if (code[j] == '\r' || code[j] == '\n')
                            {
                                hasNewline = true;
                            }
                            j++;
                        }

                        if (hasNewline && j < len && code[j] != ';' && code[j] != '}')
                        {
                            result += "return ";
                            i = j;
                            continue;
                        }
                    }
                }

                result.push_back(code[i++]);
            }

            return result;
        }

        bool isBlockStatement(const std::string &code)
        {
            size_t first = code.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return false;
            size_t last = code.find_last_not_of(" \t\r\n");
            std::string s = code.substr(first, last - first + 1);

            if (s.rfind("(() =>", 0) == 0 || s.rfind("(function", 0) == 0)
            {
                return false;
            }

            bool hasOuterBraces = (s.front() == '{' && s.back() == '}');
            int targetDepth = hasOuterBraces ? 1 : 0;

            int depth = 0;
            bool inString = false;
            char quote = 0;

            for (size_t i = 0; i < s.size(); ++i)
            {
                if (inString)
                {
                    if (s[i] == '\\' && i + 1 < s.size())
                    {
                        i++;
                        continue;
                    }
                    if (s[i] == quote)
                    {
                        inString = false;
                    }
                    continue;
                }

                if (s[i] == '"' || s[i] == '\'' || s[i] == '`')
                {
                    inString = true;
                    quote = s[i];
                    continue;
                }

                if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/')
                {
                    size_t nextLine = s.find('\n', i + 2);
                    if (nextLine == std::string::npos) break;
                    i = nextLine;
                    continue;
                }

                if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*')
                {
                    size_t endComment = s.find("*/", i + 2);
                    if (endComment == std::string::npos) break;
                    i = endComment + 1;
                    continue;
                }

                if (s[i] == '{' || s[i] == '(' || s[i] == '[')
                {
                    depth++;
                    continue;
                }
                if (s[i] == '}' || s[i] == ')' || s[i] == ']')
                {
                    depth--;
                    continue;
                }

                if (depth == targetDepth)
                {
                    auto isKeyword = [&](size_t pos, const char *kw, size_t kwLen) -> bool
                    {
                        if (pos + kwLen > s.size()) return false;
                        if (s.compare(pos, kwLen, kw) != 0) return false;
                        bool prevOk = (pos == 0 || (!isalnum(static_cast<unsigned char>(s[pos - 1])) && s[pos - 1] != '_' && s[pos - 1] != '$'));
                        bool nextOk = (pos + kwLen == s.size() || (!isalnum(static_cast<unsigned char>(s[pos + kwLen])) && s[pos + kwLen] != '_' && s[pos + kwLen] != '$'));
                        if (!prevOk || !nextOk) return false;

                        size_t k = pos + kwLen;
                        while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '\r' || s[k] == '\n'))
                        {
                            k++;
                        }
                        if (k < s.size() && s[k] == ':')
                        {
                            return false;
                        }
                        return true;
                    };

                    if (isKeyword(i, "return", 6) ||
                        isKeyword(i, "let", 3) ||
                        isKeyword(i, "const", 5) ||
                        isKeyword(i, "var", 3))
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        std::string transformUnkeyedConditions(const std::string &s)
        {
            std::string out;
            size_t i = 0;
            while (i < s.size())
            {
                if (s[i] == '{')
                {
                    size_t depth = 0;
                    size_t j = i;
                    char inStr = 0;
                    bool inRegex = false;
                    while (j < s.size())
                    {
                        char c = s[j];
                        if (inStr)
                        {
                            if (c == '\\' && j + 1 < s.size()) j++;
                            else if (c == inStr) inStr = 0;
                        }
                        else if (inRegex)
                        {
                            if (c == '\\' && j + 1 < s.size()) j++;
                            else if (c == '/') inRegex = false;
                        }
                        else if (c == '\'' || c == '"' || c == '`')
                        {
                            inStr = c;
                        }
                        else if (c == '/')
                        {
                            if (j + 1 < s.size() && s[j + 1] == '/')
                            {
                                size_t endLine = s.find('\n', j + 2);
                                if (endLine == std::string::npos) endLine = s.size();
                                j = endLine;
                                continue;
                            }
                            else if (j + 1 < s.size() && s[j + 1] == '*')
                            {
                                size_t endComment = s.find("*/", j + 2);
                                if (endComment == std::string::npos) endComment = s.size();
                                else endComment += 1;
                                j = endComment;
                                continue;
                            }
                            else
                            {
                                size_t k = j;
                                while (k > 0 && (s[k - 1] == ' ' || s[k - 1] == '\t' || s[k - 1] == '\r' || s[k - 1] == '\n')) k--;
                                if (k > 0 && std::strchr(":,([{=!&|?", s[k - 1]) != nullptr)
                                {
                                    inRegex = true;
                                }
                            }
                        }
                        else if (c == '{')
                        {
                            depth++;
                        }
                        else if (c == '}')
                        {
                            depth--;
                            if (depth == 0) break;
                        }
                        j++;
                    }

                    if (depth == 0)
                    {
                        std::string inner = s.substr(i + 1, j - (i + 1));
                        std::vector<std::string> segments;
                        size_t segStart = 0;
                        size_t segDepth = 0;
                        char inS = 0;
                        bool inR = false;
                        for (size_t k = 0; k < inner.size(); ++k)
                        {
                            char c = inner[k];
                            if (inS)
                            {
                                if (c == '\\' && k + 1 < inner.size()) k++;
                                else if (c == inS) inS = 0;
                            }
                            else if (inR)
                            {
                                if (c == '\\' && k + 1 < inner.size()) k++;
                                else if (c == '/') inR = false;
                            }
                            else if (c == '\'' || c == '"' || c == '`')
                            {
                                inS = c;
                            }
                            else if (c == '/')
                            {
                                size_t prev = k;
                                while (prev > 0 && (inner[prev - 1] == ' ' || inner[prev - 1] == '\t' || inner[prev - 1] == '\r' || inner[prev - 1] == '\n')) prev--;
                                if (prev > 0 && std::strchr(":,([{=!&|?", inner[prev - 1]) != nullptr)
                                {
                                    inR = true;
                                }
                            }
                            else if (c == '{' || c == '(' || c == '[')
                            {
                                segDepth++;
                            }
                            else if (c == '}' || c == ')' || c == ']')
                            {
                                segDepth--;
                            }
                            else if (c == ',' && segDepth == 0)
                            {
                                segments.push_back(inner.substr(segStart, k - segStart));
                                segStart = k + 1;
                            }
                        }
                        segments.push_back(inner.substr(segStart));

                        std::vector<std::string> transformedSegs;
                        std::vector<std::string> whereExprs;
                        for (const auto &seg : segments)
                        {
                            size_t f = seg.find_first_not_of(" \t\r\n");
                            if (f == std::string::npos) continue;
                            size_t l = seg.find_last_not_of(" \t\r\n");
                            std::string trimmed = seg.substr(f, l - f + 1);

                            bool hasColon = false;
                            size_t d = 0;
                            char inS2 = 0;
                            bool inR2 = false;
                            for (size_t idx = 0; idx < seg.size(); ++idx)
                            {
                                char ch = seg[idx];
                                if (inS2)
                                {
                                    if (ch == '\\' && idx + 1 < seg.size()) idx++;
                                    else if (ch == inS2) inS2 = 0;
                                }
                                else if (inR2)
                                {
                                    if (ch == '\\' && idx + 1 < seg.size()) idx++;
                                    else if (ch == '/') inR2 = false;
                                }
                                else if (ch == '\'' || ch == '"' || ch == '`')
                                {
                                    inS2 = ch;
                                }
                                else if (ch == '/')
                                {
                                    size_t prev = idx;
                                    while (prev > 0 && (seg[prev - 1] == ' ' || seg[prev - 1] == '\t')) prev--;
                                    if (prev > 0 && std::strchr(":,([{=!&|?", seg[prev - 1]) != nullptr)
                                    {
                                        inR2 = true;
                                    }
                                }
                                else if (ch == '{' || ch == '(' || ch == '[')
                                {
                                    d++;
                                }
                                else if (ch == '}' || ch == ')' || ch == ']')
                                {
                                    d--;
                                }
                                else if (ch == ':' && d == 0)
                                {
                                    hasColon = true;
                                    break;
                                }
                            }

                            if (!hasColon && (trimmed[0] == '.' ||
                                trimmed.find('>') != std::string::npos || trimmed.find('<') != std::string::npos ||
                                trimmed.find("==") != std::string::npos || trimmed.find("!=") != std::string::npos ||
                                trimmed.find("&&") != std::string::npos || trimmed.find("||") != std::string::npos))
                            {
                                try {
                                    std::regex reDot(R"((^|[^a-zA-Z0-9_$.])\.([a-zA-Z0-9_$]+))");
                                    std::string converted = std::regex_replace(trimmed, reDot, "$1_e.$2");
                                    whereExprs.push_back(converted);
                                } catch (...) {
                                    whereExprs.push_back(trimmed);
                                }
                            }
                            else
                            {
                                transformedSegs.push_back(transformUnkeyedConditions(seg));
                            }
                        }

                        if (!whereExprs.empty())
                        {
                            std::string combined;
                            for (size_t wi = 0; wi < whereExprs.size(); ++wi)
                            {
                                if (wi > 0) combined += " && ";
                                combined += "(" + whereExprs[wi] + ")";
                            }
                            std::string whereProp = "where: (_e) => { try { with (_e) { with ((_e && _e.data) || {}) { return Boolean(" + combined + "); } } } catch { return false; } }";
                            transformedSegs.insert(transformedSegs.begin(), whereProp);
                        }

                        out.push_back('{');
                        for (size_t si = 0; si < transformedSegs.size(); ++si)
                        {
                            if (si > 0) out += ", ";
                            out += transformedSegs[si];
                        }
                        out.push_back('}');
                        i = j + 1;
                        continue;
                    }
                }
                out.push_back(s[i]);
                i++;
            }
            return out;
        }

        std::string normalizeShowQuery(const std::string &rawQuery)
        {
            std::string q = rawQuery;
            size_t first = q.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            size_t last = q.find_last_not_of(" \t\r\n");
            q = q.substr(first, last - first + 1);

            // 1. Fix JS Automatic Semicolon Insertion (ASI) on return\n{
            q = fixReturnAsi(q);

            // 2. Wrap block statements with statements/return as an arrow IIFE
            if (isBlockStatement(q))
            {
                if (q.front() == '{' && q.back() == '}')
                {
                    return "(() => " + q + ")()";
                }
                else
                {
                    return "(() => {\n" + q + "\n})()";
                }
            }

            // Check if query is an object pattern
            bool isObjectPattern = false;
            if (q.front() == '{' && q.back() == '}')
            {
                isObjectPattern = true;
            }
            else if (q.front() != '(' && q.find(':') != std::string::npos &&
                     q.find('?') == std::string::npos && q.find(';') == std::string::npos &&
                     q.find("=>") == std::string::npos)
            {
                isObjectPattern = true;
            }

            if (isObjectPattern)
            {
                // Transform unkeyed condition expressions like {.h > 1 && .h < 25} to where: (_e) => ...
                q = transformUnkeyedConditions(q);

                // 1. Fix quote typos like ['CT*,'*MR*'] -> ['CT*', '*MR*']
                try
                {
                    std::regex reTypo(R"(,'\s*([*?#@.A-Za-z0-9_ -]+'))");
                    q = std::regex_replace(q, reTypo, "', '$1");

                    std::regex reMissingComma(R"('(\s*)')");
                    q = std::regex_replace(q, reMissingComma, "', '");

                    std::regex reRgb(R"(\brgb\s*\{)");
                    q = std::regex_replace(q, reRgb, "{");

                    std::regex reEKey(R"(([\{,]\s*)e\.([a-zA-Z0-9_$]+)\s*:)");
                    q = std::regex_replace(q, reEKey, "$1$2:");

                    std::regex reEKeyStart(R"(^\s*e\.([a-zA-Z0-9_$]+)\s*:)");
                    q = std::regex_replace(q, reEKeyStart, "$1:");

                    std::regex reIn(R"(:\s*in\s*\[)");
                    q = std::regex_replace(q, reIn, ": [");
                }
                catch (...)
                {
                }

                // 2. Transform except / not in / !in to { not: ... }
                std::string transformed;
                size_t i = 0;
                while (i < q.size())
                {
                    if (q[i] == ':')
                    {
                        size_t p = i + 1;
                        while (p < q.size() && (q[p] == ' ' || q[p] == '\t')) p++;
                        bool isExcept = false;
                        size_t kwLen = 0;
                        if (q.compare(p, 6, "except") == 0 && (p + 6 == q.size() || !isalnum(static_cast<unsigned char>(q[p + 6]))))
                        {
                            isExcept = true;
                            kwLen = 6;
                        }
                        else if (q.compare(p, 6, "not in") == 0 && (p + 6 == q.size() || !isalnum(static_cast<unsigned char>(q[p + 6]))))
                        {
                            isExcept = true;
                            kwLen = 6;
                        }
                        else if (q.compare(p, 3, "!in") == 0 && (p + 3 == q.size() || !isalnum(static_cast<unsigned char>(q[p + 3]))))
                        {
                            isExcept = true;
                            kwLen = 3;
                        }

                        if (isExcept)
                        {
                            size_t valStart = p + kwLen;
                            while (valStart < q.size() && (q[valStart] == ' ' || q[valStart] == '\t')) valStart++;

                            if (valStart < q.size() && q[valStart] == '[')
                            {
                                size_t depth = 0;
                                size_t j = valStart;
                                char inStr = 0;
                                while (j < q.size())
                                {
                                    char c = q[j];
                                    if (inStr)
                                    {
                                        if (c == inStr) inStr = 0;
                                    }
                                    else if (c == '\'' || c == '"' || c == '`')
                                    {
                                        inStr = c;
                                    }
                                    else if (c == '[')
                                    {
                                        depth++;
                                    }
                                    else if (c == ']')
                                    {
                                        depth--;
                                        if (depth == 0) break;
                                    }
                                    j++;
                                }
                                if (j < q.size() && depth == 0)
                                {
                                    transformed.append(q.substr(i, p - i));
                                    transformed.append("{ not: ");
                                    transformed.append(q.substr(valStart, j - valStart + 1));
                                    transformed.append(" }");
                                    i = j + 1;
                                    continue;
                                }
                            }
                            else
                            {
                                size_t j = valStart;
                                char inStr = 0;
                                size_t braceDepth = 0;
                                while (j < q.size())
                                {
                                    char c = q[j];
                                    if (inStr)
                                    {
                                        if (c == inStr) inStr = 0;
                                    }
                                    else if (c == '\'' || c == '"' || c == '`')
                                    {
                                        inStr = c;
                                    }
                                    else if (c == '{' || c == '(' || c == '[')
                                    {
                                        braceDepth++;
                                    }
                                    else if (c == '}' || c == ')' || c == ']')
                                    {
                                        if (braceDepth == 0) break;
                                        braceDepth--;
                                    }
                                    else if (c == ',' && braceDepth == 0)
                                    {
                                        break;
                                    }
                                    j++;
                                }
                                std::string singleVal = q.substr(valStart, j - valStart);
                                size_t sFirst = singleVal.find_first_not_of(" \t\r\n");
                                size_t sLast = singleVal.find_last_not_of(" \t\r\n");
                                if (sFirst != std::string::npos) singleVal = singleVal.substr(sFirst, sLast - sFirst + 1);

                                transformed.append(q.substr(i, p - i));
                                transformed.append("{ not: ");
                                transformed.append(singleVal);
                                transformed.append(" }");
                                i = j;
                                continue;
                            }
                        }
                    }
                    transformed.push_back(q[i]);
                    i++;
                }
                q = transformed;

                // Wrap object literal in parentheses
                if (q.front() == '{' && q.back() == '}')
                {
                    return "(" + q + ")";
                }
                else
                {
                    return "({" + q + "})";
                }
            }

            bool isArrow = false;
            size_t arrowPos = q.find("=>");
            if (arrowPos != std::string::npos)
            {
                std::string beforeArrow = q.substr(0, arrowPos);
                if (beforeArrow.find('e') != std::string::npos)
                {
                    isArrow = true;
                }
            }

            if (isArrow)
            {
                std::string param = q.substr(0, arrowPos + 2);
                std::string body = q.substr(arrowPos + 2);
                size_t bFirst = body.find_first_not_of(" \t\r\n");
                if (bFirst != std::string::npos)
                {
                    body = body.substr(bFirst);
                    if (body.front() == '{' && body.back() == '}' &&
                        body.find("return") == std::string::npos &&
                        body.find(';') == std::string::npos)
                    {
                        return param + " (" + body + ")";
                    }
                }
                return q;
            }

            if (q.rfind("function", 0) == 0)
            {
                return q;
            }

            if (q.find("e.") != std::string::npos || q.find("e[") != std::string::npos ||
                q.find('>') != std::string::npos || q.find('<') != std::string::npos ||
                q.find("==") != std::string::npos || q.find("!=") != std::string::npos ||
                q.find("&&") != std::string::npos || q.find("||") != std::string::npos)
            {
                return "e => { try { with (e) { with ((e && e.data) || {}) { return Boolean(" + q + "); } } } catch { return false; } }";
            }

            return q;
        }

        const char *const kExpandEntitiesScript =
            "function expandEntities(list) {\n"
            "  const result = [];\n"
            "  function process(e, parentIds, parentData) {\n"
            "    if (!e) return;\n"
            "    const isBlock = (e.ty === 'AcDbBlockReference' || e.type === 'AcDbBlockReference');\n"
            "    const hasBlockFlag = Boolean((e.data && e.data.block) || e.block);\n"
            "    if (isBlock && hasBlockFlag) {\n"
            "      const currId = (e.id !== undefined && e.id !== null) ? e.id : (e.hand || e.handle);\n"
            "      if (parentIds.includes(currId)) return;\n"
            "      const nextParentIds = [...parentIds, currId];\n"
            "      const mergedData = Object.assign({}, parentData, e.data);\n"
            "      const children = (typeof entity !== 'undefined' && entity.getBlockEntities)\n"
            "        ? entity.getBlockEntities(e.hand || e.handle || e.id)\n"
            "        : [];\n"
            "      for (let i = 0; i < children.length; i++) {\n"
            "        process(children[i], nextParentIds, mergedData);\n"
            "      }\n"
            "    } else {\n"
            "      e.ids = parentIds.slice();\n"
            "      if (parentIds.length > 0) {\n"
            "        e.data = Object.assign({}, parentData, e.data);\n"
            "        const reserved = [\n"
            "          'id', 'hand', 'handle', 'ty', 'type', 'clr', 'color',\n"
            "          'trp', 'transparency', 'visible', 'lyr', 'layer', 'text',\n"
            "          'attr', 'data', 'ids', 'l', 'a', 'area', 'len', 'length'\n"
            "        ];\n"
            "        for (const k in e.data) {\n"
            "          if (!reserved.includes(k) && (e[k] === undefined || e[k] === null)) {\n"
            "            e[k] = e.data[k];\n"
            "          }\n"
            "        }\n"
            "      }\n"
            "      result.push(e);\n"
            "    }\n"
            "  }\n"
            "  for (let i = 0; i < list.length; i++) process(list[i], [], {});\n"
            "  return result;\n"
            "}\n";

        void runCommandAndPrintRepl(const std::string &code)
        {
            const js_engine::EvalResult result = js_engine::evalForRepl(code);
            if (result.ok)
            {
                if (!result.text.empty() && result.text != "undefined")
                {
                    acutPrintf(ACRX_T("\n%s\n"), command_text::fromUtf8(result.text).c_str());
                }
            }
            else
            {
                acutPrintf(ACRX_T("\nError: %s\n"), command_text::fromUtf8(result.text).c_str());
            }
        }
    } // namespace

    // =========================================================================
    // QuickJS Native Implementation of Safe Commands using entity.get / entity.set
    // =========================================================================

    // editor.ssSet(options)
    JSValue jsSafeSsSet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        if (argc < 1)
        {
            return JS_ThrowTypeError(context, "editor.ssSet expects options object or function");
        }

        JSValueConst options = argv[0];
        JSValue setFn = JS_UNDEFINED;
        JSValue filterFn = JS_UNDEFINED;
        JSValue initFn = JS_UNDEFINED;
        JSValue postFn = JS_UNDEFINED;
        JSValue getFn = JS_UNDEFINED;
        JSValue promptVal = JS_UNDEFINED;
        JSValue entitiesVal = JS_UNDEFINED;
        bool leaveSelected = false;

        if (JS_IsFunction(context, options))
        {
            setFn = JS_DupValue(context, options);
        }
        else if (JS_IsObject(options))
        {
            setFn = JS_GetPropertyStr(context, options, "set");
            filterFn = JS_GetPropertyStr(context, options, "filter");
            initFn = JS_GetPropertyStr(context, options, "init");
            postFn = JS_GetPropertyStr(context, options, "post");
            getFn = JS_GetPropertyStr(context, options, "get");
            promptVal = JS_GetPropertyStr(context, options, "prompt");
            entitiesVal = JS_GetPropertyStr(context, options, "entities");
            if (JS_IsUndefined(entitiesVal))
            {
                entitiesVal = JS_GetPropertyStr(context, options, "handles");
            }
            if (JS_IsUndefined(entitiesVal))
            {
                entitiesVal = JS_GetPropertyStr(context, options, "ids");
            }
            JSValue impliedVal = JS_GetPropertyStr(context, options, "implied");
            if (!JS_IsUndefined(impliedVal))
            {
                leaveSelected = JS_ToBool(context, impliedVal) != 0;
            }
            JS_FreeValue(context, impliedVal);

            if (JS_IsUndefined(filterFn) || JS_IsNull(filterFn))
            {
                if (hasPatternProperties(context, options))
                {
                    filterFn = JS_DupValue(context, options);
                }
            }
        }
        else
        {
            return JS_ThrowTypeError(context, "ssSet options requires 'set' function or object patch");
        }

        if (JS_IsUndefined(setFn) || JS_IsNull(setFn))
        {
            // If options does not have an explicit 'set' property, check if options itself is a patch object
            if (JS_IsObject(options) && !JS_IsFunction(context, options) && JS_IsUndefined(filterFn) && JS_IsUndefined(initFn) && JS_IsUndefined(postFn))
            {
                JS_FreeValue(context, setFn);
                setFn = JS_DupValue(context, options);
            }
            else
            {
                JS_FreeValue(context, setFn);
                JS_FreeValue(context, filterFn);
                JS_FreeValue(context, initFn);
                JS_FreeValue(context, postFn);
                JS_FreeValue(context, getFn);
                JS_FreeValue(context, promptVal);
                JS_FreeValue(context, entitiesVal);
                return JS_ThrowTypeError(context, "ssSet options requires 'set' function or object patch");
            }
        }

        std::string promptStr = "\nSelect objects: ";
        if (!JS_IsUndefined(promptVal) && !JS_IsNull(promptVal))
        {
            promptStr = js_modules::valueToString(context, promptVal);
        }
        JS_FreeValue(context, promptVal);

        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (pDoc == nullptr)
        {
            JS_FreeValue(context, setFn);
            JS_FreeValue(context, filterFn);
            JS_FreeValue(context, initFn);
            JS_FreeValue(context, postFn);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, entitiesVal);
            return JS_ThrowInternalError(context, "No active AutoCAD document");
        }

        safe_tx::SafeDocLock docLock(pDoc);
        safe_tx::ExecutionContext execCtx = safe_tx::ExecutionContext::capture();

        std::vector<CandidateItem> candidates = acquireSelectionCandidates(
            context, entitiesVal, promptStr.c_str(), execCtx.pDb);
        JS_FreeValue(context, entitiesVal);

        if (candidates.empty())
        {
            JS_FreeValue(context, setFn);
            JS_FreeValue(context, filterFn);
            JS_FreeValue(context, initFn);
            JS_FreeValue(context, postFn);
            JS_FreeValue(context, getFn);
            return JS_NewArray(context);
        }

        if (JS_IsFunction(context, initFn))
        {
            JSValue initArg = JS_NewObject(context);
            JS_SetPropertyStr(context, initArg, "count", JS_NewInt32(context, static_cast<int>(candidates.size())));
            JSValueConst argvInit[1] = { initArg };
            JSValue initRet = JS_Call(context, initFn, JS_UNDEFINED, 1, argvInit);
            JS_FreeValue(context, initArg);
            if (JS_IsException(initRet))
            {
                JS_FreeValue(context, setFn);
                JS_FreeValue(context, filterFn);
                JS_FreeValue(context, initFn);
                JS_FreeValue(context, postFn);
                JS_FreeValue(context, getFn);
                return initRet;
            }
            JS_FreeValue(context, initRet);
        }

        JSValue resultsArray = JS_NewArray(context);
        std::vector<AcDbObjectId> matchedIds;
        int processedCount = 0;
        int matchedCount = 0;
        uint32_t outIdx = 0;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (acedUsrBrk())
            {
                break;
            }

            // 1. Read detached snapshot using entity.get(hand)
            JSValue e = callEntityGet(context, candidates[i].hand);
            if (JS_IsException(e) || JS_IsNull(e) || JS_IsUndefined(e))
            {
                JS_FreeValue(context, e);
                continue;
            }
            processedCount++;

            // 2. Filter using filter(e)
            bool matched = true;
            if (!JS_IsUndefined(filterFn) && !JS_IsNull(filterFn))
            {
                matched = evaluatePredicate(context, filterFn, e);
            }

            if (!matched)
            {
                JS_FreeValue(context, e);
                continue;
            }

            matchedCount++;
            matchedIds.push_back(candidates[i].id);

            // 3. Evaluate patch: set(e)
            JSValue patch = JS_UNDEFINED;
            if (JS_IsFunction(context, setFn))
            {
                JSValueConst argvSet[1] = { e };
                patch = JS_Call(context, setFn, JS_UNDEFINED, 1, argvSet);
                if (JS_IsException(patch))
                {
                    JS_FreeValue(context, e);
                    JS_FreeValue(context, setFn);
                    JS_FreeValue(context, filterFn);
                    JS_FreeValue(context, initFn);
                    JS_FreeValue(context, postFn);
                    JS_FreeValue(context, getFn);
                    JS_FreeValue(context, resultsArray);
                    return patch;
                }
            }
            else
            {
                patch = JS_DupValue(context, setFn);
            }

            // 4. Mutate entity using entity.set(hand, patch)
            JSValue updated = JS_UNDEFINED;
            if (JS_IsObject(patch))
            {
                updated = callEntitySet(context, candidates[i].hand, patch);
                JS_FreeValue(context, patch);
                JS_FreeValue(context, e);
            }
            else
            {
                JS_FreeValue(context, patch);
                updated = e;
            }

            if (JS_IsException(updated))
            {
                JS_FreeValue(context, setFn);
                JS_FreeValue(context, filterFn);
                JS_FreeValue(context, initFn);
                JS_FreeValue(context, postFn);
                JS_FreeValue(context, getFn);
                JS_FreeValue(context, resultsArray);
                return updated;
            }

            // 5. Output mapping if get(e) specified
            JSValue item = JS_UNDEFINED;
            if (JS_IsFunction(context, getFn))
            {
                JSValueConst argvGet[1] = { updated };
                item = JS_Call(context, getFn, JS_UNDEFINED, 1, argvGet);
                JS_FreeValue(context, updated);
            }
            else
            {
                item = updated;
            }

            JS_SetPropertyUint32(context, resultsArray, outIdx++, item);
        }

        if (JS_IsFunction(context, postFn))
        {
            JSValue postArg = JS_NewObject(context);
            JS_SetPropertyStr(context, postArg, "processedCount", JS_NewInt32(context, processedCount));
            JS_SetPropertyStr(context, postArg, "matchedCount", JS_NewInt32(context, matchedCount));
            JSValueConst argvPost[1] = { postArg };
            JSValue postRet = JS_Call(context, postFn, JS_UNDEFINED, 1, argvPost);
            JS_FreeValue(context, postArg);
            JS_FreeValue(context, postRet);
        }

        JS_FreeValue(context, setFn);
        JS_FreeValue(context, filterFn);
        JS_FreeValue(context, initFn);
        JS_FreeValue(context, postFn);
        JS_FreeValue(context, getFn);

        if (leaveSelected && !matchedIds.empty())
        {
            safe_tx::applyImpliedSelection(matchedIds);
        }

        // Return single entity snapshot if only 1 candidate was processed/matched, else array
        if (outIdx == 1 && candidates.size() == 1)
        {
            JSValue single = JS_GetPropertyUint32(context, resultsArray, 0);
            JS_FreeValue(context, resultsArray);
            return single;
        }

        return resultsArray;
    }

    // editor.ssGet(options)
    JSValue jsSafeSsGet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        JSValueConst options = (argc >= 1 && JS_IsObject(argv[0])) ? argv[0] : JS_UNDEFINED;
        JSValue filterFn = JS_UNDEFINED;
        JSValue getFn = JS_UNDEFINED;
        JSValue entitiesVal = JS_UNDEFINED;
        bool leaveSelected = true; // default true for SS-GET

        if (argc >= 1 && JS_IsFunction(context, argv[0]))
        {
            filterFn = JS_DupValue(context, argv[0]);
        }
        else if (!JS_IsUndefined(options) && !JS_IsNull(options))
        {
            filterFn = JS_GetPropertyStr(context, options, "filter");
            if (JS_IsUndefined(filterFn))
            {
                filterFn = JS_GetPropertyStr(context, options, "where");
            }
            getFn = JS_GetPropertyStr(context, options, "get");
            if (JS_IsUndefined(getFn))
            {
                getFn = JS_GetPropertyStr(context, options, "out");
            }
            entitiesVal = JS_GetPropertyStr(context, options, "entities");
            if (JS_IsUndefined(entitiesVal))
            {
                entitiesVal = JS_GetPropertyStr(context, options, "handles");
            }
            if (JS_IsUndefined(entitiesVal))
            {
                entitiesVal = JS_GetPropertyStr(context, options, "ids");
            }
            JSValue impliedVal = JS_GetPropertyStr(context, options, "implied");
            if (!JS_IsUndefined(impliedVal))
            {
                leaveSelected = JS_ToBool(context, impliedVal) != 0;
            }
            JS_FreeValue(context, impliedVal);

            if (JS_IsUndefined(filterFn) || JS_IsNull(filterFn))
            {
                if (hasPatternProperties(context, options))
                {
                    filterFn = JS_DupValue(context, options);
                }
            }
        }

        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (pDoc == nullptr)
        {
            JS_FreeValue(context, filterFn);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, entitiesVal);
            return JS_ThrowInternalError(context, "No active AutoCAD document");
        }

        safe_tx::SafeDocLock docLock(pDoc);
        safe_tx::ExecutionContext execCtx = safe_tx::ExecutionContext::capture();

        std::vector<CandidateItem> candidates = acquireSelectionCandidates(
            context, entitiesVal, "\nSelect objects: ", execCtx.pDb);
        JS_FreeValue(context, entitiesVal);

        JSValue resultsArray = JS_NewArray(context);
        std::vector<AcDbObjectId> matchedIds;
        uint32_t outIdx = 0;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (acedUsrBrk())
            {
                break;
            }

            // Read entity snapshot using entity.get(hand)
            JSValue e = callEntityGet(context, candidates[i].hand);
            if (JS_IsException(e) || JS_IsNull(e) || JS_IsUndefined(e))
            {
                JS_FreeValue(context, e);
                continue;
            }

            bool matched = true;
            if (!JS_IsUndefined(filterFn) && !JS_IsNull(filterFn))
            {
                matched = evaluatePredicate(context, filterFn, e);
            }

            if (!matched)
            {
                JS_FreeValue(context, e);
                continue;
            }

            matchedIds.push_back(candidates[i].id);

            JSValue item = JS_UNDEFINED;
            if (JS_IsFunction(context, getFn))
            {
                JSValueConst argvGet[1] = { e };
                item = JS_Call(context, getFn, JS_UNDEFINED, 1, argvGet);
                JS_FreeValue(context, e);
            }
            else
            {
                item = e;
            }

            JS_SetPropertyUint32(context, resultsArray, outIdx++, item);
        }

        JS_FreeValue(context, filterFn);
        JS_FreeValue(context, getFn);

        if (leaveSelected && !matchedIds.empty())
        {
            safe_tx::applyImpliedSelection(matchedIds);
        }

        return resultsArray;
    }

    // editor.eSet(options) or editor.eSet(target, patch)
    JSValue jsSafeESet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (pDoc == nullptr)
        {
            return JS_ThrowInternalError(context, "No active AutoCAD document");
        }

        safe_tx::SafeDocLock docLock(pDoc);
        safe_tx::ExecutionContext execCtx = safe_tx::ExecutionContext::capture();

        std::string targetHand;
        AcDbObjectId targetId;
        JSValue setFn = JS_UNDEFINED;
        JSValue getFn = JS_UNDEFINED;

        if (argc >= 1 && JS_IsObject(argv[0]) && !JS_IsFunction(context, argv[0]))
        {
            JSValueConst options = argv[0];
            JSValue handleVal = JS_GetPropertyStr(context, options, "handle");
            if (JS_IsUndefined(handleVal)) handleVal = JS_GetPropertyStr(context, options, "hand");
            if (JS_IsUndefined(handleVal)) handleVal = JS_GetPropertyStr(context, options, "id");
            if (!JS_IsUndefined(handleVal) && !JS_IsNull(handleVal))
            {
                if (objectIdFromHandleValue(targetId, context, handleVal) == Acad::eOk)
                {
                    targetHand = handleToString(targetId.handle());
                }
            }
            JS_FreeValue(context, handleVal);

            setFn = JS_GetPropertyStr(context, options, "set");
            getFn = JS_GetPropertyStr(context, options, "get");
            if ((JS_IsUndefined(setFn) || JS_IsNull(setFn)) && targetHand.empty())
            {
                JS_FreeValue(context, setFn);
                setFn = JS_DupValue(context, options);
            }
        }
        else if (argc >= 1 && (JS_IsString(argv[0]) || JS_IsNumber(argv[0])))
        {
            if (objectIdFromHandleValue(targetId, context, argv[0]) == Acad::eOk)
            {
                targetHand = handleToString(targetId.handle());
            }
            if (argc >= 2)
            {
                setFn = JS_DupValue(context, argv[1]);
            }
        }
        else if (argc >= 1 && JS_IsFunction(context, argv[0]))
        {
            setFn = JS_DupValue(context, argv[0]);
        }

        // If target was not specified, pick interactively
        if (targetHand.empty())
        {
            ads_name ename;
            ads_point pt;
            if (acedEntSel(ACRX_T("\nSelect an entity: "), ename, pt) != RTNORM)
            {
                JS_FreeValue(context, setFn);
                JS_FreeValue(context, getFn);
                return JS_NULL;
            }
            if (objectIdFromAdsName(targetId, ename) != Acad::eOk ||
                !targetId.isValid() || targetId.isErased() || targetId.database() != execCtx.pDb)
            {
                JS_FreeValue(context, setFn);
                JS_FreeValue(context, getFn);
                return JS_NULL;
            }
            targetHand = handleToString(targetId.handle());
        }

        // 1. Read entity snapshot with entity.get(hand)
        JSValue e = callEntityGet(context, targetHand);
        if (JS_IsException(e) || JS_IsNull(e) || JS_IsUndefined(e))
        {
            JS_FreeValue(context, setFn);
            JS_FreeValue(context, getFn);
            return e;
        }

        // 2. Evaluate set(e)
        JSValue patch = JS_UNDEFINED;
        if (JS_IsFunction(context, setFn))
        {
            JSValueConst argvSet[1] = { e };
            patch = JS_Call(context, setFn, JS_UNDEFINED, 1, argvSet);
            JS_FreeValue(context, setFn);
        }
        else if (JS_IsObject(setFn))
        {
            patch = setFn;
        }
        else
        {
            JS_FreeValue(context, setFn);
            JS_FreeValue(context, getFn);
            return e;
        }

        if (JS_IsException(patch))
        {
            JS_FreeValue(context, e);
            JS_FreeValue(context, getFn);
            return patch;
        }

        // 3. Mutate entity with entity.set(hand, patch)
        JSValue updated = callEntitySet(context, targetHand, patch);
        JS_FreeValue(context, patch);
        JS_FreeValue(context, e);

        if (JS_IsException(updated))
        {
            JS_FreeValue(context, getFn);
            return updated;
        }

        // 4. Output mapping if get(e) provided
        if (JS_IsFunction(context, getFn))
        {
            JSValueConst argvGet[1] = { updated };
            JSValue item = JS_Call(context, getFn, JS_UNDEFINED, 1, argvGet);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, updated);
            return item;
        }

        JS_FreeValue(context, getFn);
        return updated;
    }

    // editor.eGet(options) or editor.eGet(target, getFn)
    JSValue jsSafeEGet(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (pDoc == nullptr)
        {
            return JS_ThrowInternalError(context, "No active AutoCAD document");
        }

        safe_tx::SafeDocLock docLock(pDoc);
        safe_tx::ExecutionContext execCtx = safe_tx::ExecutionContext::capture();

        std::string targetHand;
        AcDbObjectId targetId;
        JSValue getFn = JS_UNDEFINED;
        bool leaveSelected = false; // default false for E-GET

        if (argc >= 1 && JS_IsObject(argv[0]) && !JS_IsFunction(context, argv[0]))
        {
            JSValueConst options = argv[0];
            JSValue handleVal = JS_GetPropertyStr(context, options, "handle");
            if (JS_IsUndefined(handleVal)) handleVal = JS_GetPropertyStr(context, options, "hand");
            if (JS_IsUndefined(handleVal)) handleVal = JS_GetPropertyStr(context, options, "id");
            if (!JS_IsUndefined(handleVal) && !JS_IsNull(handleVal))
            {
                if (objectIdFromHandleValue(targetId, context, handleVal) == Acad::eOk)
                {
                    targetHand = handleToString(targetId.handle());
                }
            }
            JS_FreeValue(context, handleVal);

            getFn = JS_GetPropertyStr(context, options, "get");
            if (JS_IsUndefined(getFn))
            {
                getFn = JS_GetPropertyStr(context, options, "out");
            }

            JSValue impliedVal = JS_GetPropertyStr(context, options, "implied");
            if (!JS_IsUndefined(impliedVal))
            {
                leaveSelected = JS_ToBool(context, impliedVal) != 0;
            }
            JS_FreeValue(context, impliedVal);

            if (JS_IsUndefined(getFn) || JS_IsNull(getFn))
            {
                if (hasPatternProperties(context, options))
                {
                    getFn = JS_DupValue(context, options);
                }
            }
        }
        else if (argc >= 1 && (JS_IsString(argv[0]) || JS_IsNumber(argv[0])))
        {
            if (objectIdFromHandleValue(targetId, context, argv[0]) == Acad::eOk)
            {
                targetHand = handleToString(targetId.handle());
            }
            if (argc >= 2 && JS_IsFunction(context, argv[1]))
            {
                getFn = JS_DupValue(context, argv[1]);
            }
        }
        else if (argc >= 1 && JS_IsFunction(context, argv[0]))
        {
            getFn = JS_DupValue(context, argv[0]);
        }

        // Pick entity interactively if no handle was provided
        if (targetHand.empty())
        {
            ads_name ename;
            ads_point pt;
            if (acedEntSel(ACRX_T("\nSelect an entity: "), ename, pt) != RTNORM)
            {
                JS_FreeValue(context, getFn);
                return JS_NULL;
            }
            if (objectIdFromAdsName(targetId, ename) != Acad::eOk ||
                !targetId.isValid() || targetId.isErased() || targetId.database() != execCtx.pDb)
            {
                JS_FreeValue(context, getFn);
                return JS_NULL;
            }
            targetHand = handleToString(targetId.handle());
        }

        // Read snapshot using entity.get(hand)
        JSValue e = callEntityGet(context, targetHand);
        if (JS_IsException(e) || JS_IsNull(e) || JS_IsUndefined(e))
        {
            JS_FreeValue(context, getFn);
            return e;
        }

        if (leaveSelected && targetId.isValid())
        {
            safe_tx::applyImpliedSelection({ targetId });
        }

        if (JS_IsFunction(context, getFn))
        {
            JSValueConst argvGet[1] = { e };
            JSValue item = JS_Call(context, getFn, JS_UNDEFINED, 1, argvGet);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, e);
            return item;
        }
        else if (JS_IsObject(getFn) && !JS_IsNull(getFn) && hasPatternProperties(context, getFn))
        {
            bool matched = matchesPattern(context, getFn, e, true);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, e);
            return JS_NewBool(context, matched);
        }

        JS_FreeValue(context, getFn);
        return e;
    }

    // editor.eShow(options) or editor.eShow(showFn) or editor.eShow(target, showFn)
    JSValue jsSafeEShow(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (pDoc == nullptr)
        {
            return JS_ThrowInternalError(context, "No active AutoCAD document");
        }

        safe_tx::SafeDocLock docLock(pDoc);
        safe_tx::ExecutionContext execCtx = safe_tx::ExecutionContext::capture();

        std::string targetHand;
        AcDbObjectId targetId;
        bool doZoom = true;
        JSValue showFn = JS_UNDEFINED;

        if (argc >= 1 && JS_IsFunction(context, argv[0]))
        {
            showFn = JS_DupValue(context, argv[0]);
        }
        else if (argc >= 1 && JS_IsObject(argv[0]))
        {
            JSValueConst options = argv[0];
            JSValue handleVal = JS_GetPropertyStr(context, options, "handle");
            if (JS_IsUndefined(handleVal)) handleVal = JS_GetPropertyStr(context, options, "hand");
            if (JS_IsUndefined(handleVal)) handleVal = JS_GetPropertyStr(context, options, "id");
            if (!JS_IsUndefined(handleVal) && !JS_IsNull(handleVal))
            {
                if (objectIdFromHandleValue(targetId, context, handleVal) == Acad::eOk)
                {
                    targetHand = handleToString(targetId.handle());
                }
            }
            JS_FreeValue(context, handleVal);

            JSValue zoomVal = JS_GetPropertyStr(context, options, "zoom");
            if (!JS_IsUndefined(zoomVal))
            {
                doZoom = JS_ToBool(context, zoomVal) != 0;
            }
            JS_FreeValue(context, zoomVal);

            showFn = JS_GetPropertyStr(context, options, "show");
            if (JS_IsUndefined(showFn))
            {
                showFn = JS_GetPropertyStr(context, options, "get");
            }
        }
        else if (argc >= 1 && (JS_IsString(argv[0]) || JS_IsNumber(argv[0])))
        {
            if (objectIdFromHandleValue(targetId, context, argv[0]) == Acad::eOk)
            {
                targetHand = handleToString(targetId.handle());
            }
            if (argc >= 2 && JS_IsFunction(context, argv[1]))
            {
                showFn = JS_DupValue(context, argv[1]);
            }
        }

        // Pick entity interactively if no handle provided: e = entity.pick('\nPick an entity:')
        if (targetHand.empty())
        {
            ads_name ename;
            ads_point pt;
            if (acedEntSel(ACRX_T("\nPick an entity: "), ename, pt) != RTNORM)
            {
                JS_FreeValue(context, showFn);
                return JS_NULL;
            }
            if (objectIdFromAdsName(targetId, ename) != Acad::eOk ||
                !targetId.isValid() || targetId.isErased() || targetId.database() != execCtx.pDb)
            {
                JS_FreeValue(context, showFn);
                return JS_NULL;
            }
            targetHand = handleToString(targetId.handle());
        }

        // Read snapshot using entity.get(hand)
        JSValue e = callEntityGet(context, targetHand);
        if (JS_IsException(e) || JS_IsNull(e) || JS_IsUndefined(e))
        {
            JS_FreeValue(context, showFn);
            return e;
        }

        // Zoom to extents if requested
        if (doZoom && targetId.isValid())
        {
            AcDbExtents extents;
            bool hasExtents = false;
            AcDbEntity *pEnt = nullptr;
            if (acdbOpenObject(pEnt, targetId, AcDb::kForRead) == Acad::eOk && pEnt != nullptr)
            {
                hasExtents = (pEnt->getGeomExtents(extents) == Acad::eOk);
                pEnt->close();
            }

            if (hasExtents)
            {
                const AcGePoint3d minPt = extents.minPoint();
                const AcGePoint3d maxPt = extents.maxPoint();
                const double dx = maxPt.x - minPt.x;
                const double dy = maxPt.y - minPt.y;
                double margin = (std::max)(dx, dy) * 0.2;
                if (margin < 1.0)
                {
                    margin = 1.0;
                }

                ads_point pt1, pt2;
                pt1[0] = minPt.x - margin;
                pt1[1] = minPt.y - margin;
                pt1[2] = 0.0;
                pt2[0] = maxPt.x + margin;
                pt2[1] = maxPt.y + margin;
                pt2[2] = 0.0;

                acedCommandS(RTSTR, ACRX_T("_.ZOOM"), RTSTR, ACRX_T("_W"), RTPOINT, pt1, RTPOINT, pt2, RTNONE);
            }
        }

        if (targetId.isValid())
        {
            safe_tx::applyImpliedSelection({ targetId });
        }

        // If showFn is provided, call ShowFn with the entity as argument: showFn(e)
        if (JS_IsFunction(context, showFn))
        {
            JSValueConst argvCall[1] = { e };
            JSValue res = JS_Call(context, showFn, JS_UNDEFINED, 1, argvCall);
            JS_FreeValue(context, showFn);
            JS_FreeValue(context, e);
            return res;
        }

        JS_FreeValue(context, showFn);
        return e;
    }

    // editor.ssQuery(options) — search current space directly without interactive selection
    JSValue jsSafeSsQuery(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        JSValueConst options = (argc >= 1 && JS_IsObject(argv[0])) ? argv[0] : JS_UNDEFINED;
        JSValue filterFn = JS_UNDEFINED;
        JSValue getFn = JS_UNDEFINED;
        JSValue queryFn = JS_UNDEFINED;
        bool leaveSelected = true; // default true for SS-QUERY

        if (argc >= 1 && JS_IsString(argv[0]))
        {
            const std::string dslStr = js_modules::valueToString(context, argv[0]);
            if (query_dsl::isDslQuery(dslStr))
            {
                query_dsl::DslParseResult parsed = query_dsl::parse(dslStr);
                leaveSelected = parsed.options.implied;
                const std::string fnCode = query_dsl::compileToJsFunction(dslStr, "entries");
                queryFn = JS_Eval(context, fnCode.c_str(), fnCode.length(), "<query_dsl>", JS_EVAL_TYPE_GLOBAL);
            }
        }

        if (argc >= 1 && JS_IsFunction(context, argv[0]))
        {
            filterFn = JS_DupValue(context, argv[0]);
        }
        else if (!JS_IsUndefined(options) && !JS_IsNull(options))
        {
            filterFn = JS_GetPropertyStr(context, options, "filter");
            if (JS_IsUndefined(filterFn))
            {
                filterFn = JS_GetPropertyStr(context, options, "where");
            }
            getFn = JS_GetPropertyStr(context, options, "get");
            if (JS_IsUndefined(getFn))
            {
                getFn = JS_GetPropertyStr(context, options, "out");
            }
            queryFn = JS_GetPropertyStr(context, options, "query");
            JSValue impliedVal = JS_GetPropertyStr(context, options, "implied");
            if (!JS_IsUndefined(impliedVal))
            {
                leaveSelected = JS_ToBool(context, impliedVal) != 0;
            }
            JS_FreeValue(context, impliedVal);

            if (JS_IsUndefined(filterFn) || JS_IsNull(filterFn))
            {
                if (hasPatternProperties(context, options))
                {
                    filterFn = JS_DupValue(context, options);
                }
            }
        }

        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (pDoc == nullptr)
        {
            JS_FreeValue(context, filterFn);
            JS_FreeValue(context, getFn);
            JS_FreeValue(context, queryFn);
            return JS_ThrowInternalError(context, "No active AutoCAD document");
        }

        safe_tx::SafeDocLock docLock(pDoc);
        safe_tx::ExecutionContext execCtx = safe_tx::ExecutionContext::capture();

        bool selectInteractive = false;
        JSValue entitiesVal = JS_UNDEFINED;
        if (!JS_IsUndefined(options) && !JS_IsNull(options))
        {
            JSValue selVal = JS_GetPropertyStr(context, options, "select");
            if (!JS_IsUndefined(selVal))
            {
                selectInteractive = JS_ToBool(context, selVal) != 0;
            }
            JS_FreeValue(context, selVal);

            entitiesVal = JS_GetPropertyStr(context, options, "entities");
            if (JS_IsUndefined(entitiesVal))
            {
                entitiesVal = JS_GetPropertyStr(context, options, "handles");
            }
        }

        std::vector<CandidateItem> candidates;
        if (!JS_IsUndefined(entitiesVal) && !JS_IsNull(entitiesVal))
        {
            candidates = acquireSelectionCandidates(context, entitiesVal, "\nSelect objects: ", execCtx.pDb);
            JS_FreeValue(context, entitiesVal);
        }
        else if (selectInteractive)
        {
            candidates = acquireSelectionCandidates(context, JS_UNDEFINED, "\nSelect objects: ", execCtx.pDb);
            if (candidates.empty())
            {
                JS_FreeValue(context, filterFn);
                JS_FreeValue(context, getFn);
                JS_FreeValue(context, queryFn);
                return JS_UNDEFINED;
            }
        }
        else
        {
            // Iterate current editing space directly
            AcDbBlockTableRecord *pSpace = nullptr;
            if (acdbOpenObject(pSpace, execCtx.currentSpaceId, AcDb::kForRead) == Acad::eOk && pSpace != nullptr)
            {
                AcDbBlockTableRecordIterator *pIter = nullptr;
                if (pSpace->newIterator(pIter) == Acad::eOk && pIter != nullptr)
                {
                    for (; !pIter->done(); pIter->step())
                    {
                        AcDbObjectId id;
                        if (pIter->getEntityId(id) == Acad::eOk && id.isValid() && !id.isErased())
                        {
                            candidates.push_back({ handleToString(id.handle()), id });
                        }
                    }
                    delete pIter;
                }
                pSpace->close();
            }
        }

        JSValue resultsArray = JS_NewArray(context);
        std::vector<AcDbObjectId> matchedIds;
        uint32_t outIdx = 0;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (acedUsrBrk())
            {
                break;
            }

            // Read entity snapshot using entity.get(hand)
            JSValue e = callEntityGet(context, candidates[i].hand);
            if (JS_IsException(e) || JS_IsNull(e) || JS_IsUndefined(e))
            {
                JS_FreeValue(context, e);
                continue;
            }

            bool matched = true;
            if (!JS_IsUndefined(filterFn) && !JS_IsNull(filterFn))
            {
                matched = evaluatePredicate(context, filterFn, e);
            }

            if (!matched)
            {
                JS_FreeValue(context, e);
                continue;
            }

            matchedIds.push_back(candidates[i].id);

            JSValue item = JS_UNDEFINED;
            if (JS_IsFunction(context, getFn))
            {
                JSValueConst argvGet[1] = { e };
                item = JS_Call(context, getFn, JS_UNDEFINED, 1, argvGet);
                JS_FreeValue(context, e);
            }
            else
            {
                item = e;
            }

            JS_SetPropertyUint32(context, resultsArray, outIdx++, item);
        }

        JS_FreeValue(context, filterFn);
        JS_FreeValue(context, getFn);

        if (leaveSelected && !matchedIds.empty())
        {
            safe_tx::applyImpliedSelection(matchedIds);
        }

        // Expand any AcDbBlockReference with data.block === true
        std::string expandWrapper =
            std::string(kExpandEntitiesScript) +
            "expandEntities;";
        JSValue expandFn = JS_Eval(context, expandWrapper.c_str(), expandWrapper.length(), "<expand_blocks>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsFunction(context, expandFn))
        {
            JSValueConst argvExp[1] = { resultsArray };
            JSValue expanded = JS_Call(context, expandFn, JS_UNDEFINED, 1, argvExp);
            JS_FreeValue(context, expandFn);
            if (!JS_IsException(expanded) && JS_IsArray(expanded))
            {
                JS_FreeValue(context, resultsArray);
                resultsArray = expanded;
            }
            else
            {
                JS_FreeValue(context, expanded);
            }
        }

        if (JS_IsFunction(context, queryFn))
        {
            JSValueConst argvQuery[1] = { resultsArray };
            JSValue qRes = JS_Call(context, queryFn, JS_UNDEFINED, 1, argvQuery);
            JS_FreeValue(context, queryFn);
            JS_FreeValue(context, resultsArray);
            return qRes;
        }

        JS_FreeValue(context, queryFn);
        return resultsArray;
    }

    JSValue jsDslCompile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        if (argc < 1 || !JS_IsString(argv[0]))
        {
            return JS_ThrowTypeError(context, "dsl.compile expects a DSL query string");
        }
        const std::string dslStr = js_modules::valueToString(context, argv[0]);
        const std::string jsCode = query_dsl::compileToJsCode(dslStr, "entries");
        return JS_NewString(context, jsCode.c_str());
    }

    JSValue jsQtoPoll(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        std::string dwgName = "Untitled.dwg";
        if (pDoc != nullptr && pDoc->docTitle() != nullptr)
        {
            dwgName = command_text::toUtf8(pDoc->docTitle());
        }

#if defined(_GCRXAPP) || defined(GSTARCAD_VERSION)
        const char *platformStr = "GstarCAD";
#else
        const char *platformStr = "AutoCAD";
#endif

        const std::vector<std::string> handles = selection_event::getSelectedHandles();
        const uint64_t ver = selection_event::getSelectionVersion();

        int64_t clientVer = -1;
        if (argc >= 1 && JS_IsNumber(argv[0]))
        {
            JS_ToInt64(context, &clientVer, argv[0]);
        }

        JSValue outObj = JS_NewObject(context);
        JS_SetPropertyStr(context, outObj, "version", JS_NewInt64(context, static_cast<int64_t>(ver)));
        JS_SetPropertyStr(context, outObj, "dwg", JS_NewString(context, dwgName.c_str()));
        JS_SetPropertyStr(context, outObj, "platform", JS_NewString(context, platformStr));
        JS_SetPropertyStr(context, outObj, "count", JS_NewInt32(context, static_cast<int32_t>(handles.size())));

        if (clientVer > 0 && static_cast<uint64_t>(clientVer) == ver)
        {
            JS_SetPropertyStr(context, outObj, "changed", JS_NewBool(context, false));
            return outObj;
        }

        JS_SetPropertyStr(context, outObj, "changed", JS_NewBool(context, true));

        JSValue handlesArr = JS_NewArray(context);
        JSValue entitiesArr = JS_NewArray(context);
        uint32_t entIdx = 0;

        for (size_t i = 0; i < handles.size(); ++i)
        {
            JS_SetPropertyUint32(context, handlesArr, static_cast<uint32_t>(i), JS_NewString(context, handles[i].c_str()));
            JSValue entObj = callEntityGet(context, handles[i]);
            if (!JS_IsException(entObj) && !JS_IsNull(entObj) && !JS_IsUndefined(entObj))
            {
                JS_SetPropertyUint32(context, entitiesArr, entIdx++, entObj);
            }
            else
            {
                JS_FreeValue(context, entObj);
            }
        }

        JS_SetPropertyStr(context, outObj, "handles", handlesArr);
        JS_SetPropertyStr(context, outObj, "entities", entitiesArr);

        // Collect layers from LayerTable (lightweight, safe from any context)
        JSValue layersArr = JS_NewArray(context);
        if (pDoc != nullptr && pDoc->database() != nullptr)
        {
            AcDbLayerTable *pLayerTable = nullptr;
            if (pDoc->database()->getLayerTable(pLayerTable, AcDb::kForRead) == Acad::eOk && pLayerTable != nullptr)
            {
                AcDbLayerTableIterator *pLyrIter = nullptr;
                if (pLayerTable->newIterator(pLyrIter) == Acad::eOk && pLyrIter != nullptr)
                {
                    uint32_t lyrIdx = 0;
                    for (; !pLyrIter->done(); pLyrIter->step())
                    {
                        AcDbLayerTableRecord *pRecord = nullptr;
                        if (pLyrIter->getRecord(pRecord, AcDb::kForRead) == Acad::eOk && pRecord != nullptr)
                        {
                            ACHAR *name = nullptr;
                            if (pRecord->getName(name) == Acad::eOk && name != nullptr)
                            {
                                JSValue item = JS_NewObject(context);
                                JS_SetPropertyStr(context, item, "layer", JS_NewString(context, command_text::toUtf8(name).c_str()));
                                JS_SetPropertyStr(context, item, "count", JS_NewInt32(context, 0));
                                JS_SetPropertyUint32(context, layersArr, lyrIdx++, item);
                                acutDelString(name);
                            }
                            pRecord->close();
                        }
                    }
                    delete pLyrIter;
                }
                pLayerTable->close();
            }
        }
        JS_SetPropertyStr(context, outObj, "layers", layersArr);

        return outObj;
    }

    JSValue jsQtoSelectHandles(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
    {
        if (argc < 1 || !JS_IsArray(argv[0]))
        {
            return JS_ThrowTypeError(context, "qtoSelectHandles expects an array of handles");
        }

        JSValue lenVal = JS_GetPropertyStr(context, argv[0], "length");
        int64_t len = 0;
        JS_ToInt64(context, &len, lenVal);
        JS_FreeValue(context, lenVal);

        std::vector<AcDbObjectId> ids;
        for (int64_t i = 0; i < len; ++i)
        {
            JSValue elem = JS_GetPropertyUint32(context, argv[0], static_cast<uint32_t>(i));
            AcDbObjectId objId;
            if (objectIdFromHandleValue(objId, context, elem) == Acad::eOk && objId.isValid() && !objId.isErased())
            {
                ids.push_back(objId);
            }
            JS_FreeValue(context, elem);
        }

        safe_tx::applyImpliedSelection(ids);

        return jsQtoPoll(context, JS_UNDEFINED, 0, nullptr);
    }

    void registerSafeCommands(JSContext *context, JSValue targetObj, bool isEntityModule)
    {
        JS_SetPropertyStr(context, targetObj, "ssSet", JS_NewCFunction(context, jsSafeSsSet, "ssSet", 1));
        JS_SetPropertyStr(context, targetObj, "compileDsl", JS_NewCFunction(context, jsDslCompile, "compileDsl", 1));
        JS_SetPropertyStr(context, targetObj, "eSet", JS_NewCFunction(context, jsSafeESet, "eSet", 2));
        JS_SetPropertyStr(context, targetObj, "eGet", JS_NewCFunction(context, jsSafeEGet, "eGet", 2));
        JS_SetPropertyStr(context, targetObj, "ssGet", JS_NewCFunction(context, jsSafeSsGet, "ssGet", 1));
        JS_SetPropertyStr(context, targetObj, "ssQuery", JS_NewCFunction(context, jsSafeSsQuery, "ssQuery", 1));
        JS_SetPropertyStr(context, targetObj, "eShow", JS_NewCFunction(context, jsSafeEShow, "eShow", 1));
        JS_SetPropertyStr(context, targetObj, "qtoPoll", JS_NewCFunction(context, jsQtoPoll, "qtoPoll", 1));
        JS_SetPropertyStr(context, targetObj, "qtoSelectHandles", JS_NewCFunction(context, jsQtoSelectHandles, "qtoSelectHandles", 2));

        if (isEntityModule)
        {
            JS_SetPropertyStr(context, targetObj, "show", JS_NewCFunction(context, jsSafeEShow, "show", 1));
        }
    }

    // =========================================================================
    // AutoLISP Callable Subroutines
    // =========================================================================

    int lispEGet(resbuf *rb)
    {
        std::string argStr;
        if (rb != nullptr)
        {
            if (rb->restype == RTSTR && rb->resval.rstring != nullptr)
            {
                argStr = command_text::toUtf8(rb->resval.rstring);
            }
            else if (rb->restype == RTENAME)
            {
                AcDbObjectId id;
                if (acdbGetObjectId(id, rb->resval.rlname) == Acad::eOk)
                {
                    argStr = "'" + handleToString(id.handle()) + "'";
                }
            }
        }

        std::string jsCode = "editor.eGet(" + argStr + ");";
        const js_engine::EvalResult result = js_engine::evalForRepl(jsCode);
        if (result.ok && result.text != "null" && result.text != "undefined")
        {
            const std::basic_string<ACHAR> resStr = command_text::fromUtf8(result.text);
            acedRetStr(resStr.c_str());
            return RTNORM;
        }

        acedRetNil();
        return RTERROR;
    }

    int lispESet(resbuf *rb)
    {
        if (rb == nullptr)
        {
            acutPrintf(ACRX_T("\nError: (e-set) requires target or query options."));
            acedRetNil();
            return RTERROR;
        }

        std::string target;
        if (rb->restype == RTSTR && rb->resval.rstring != nullptr)
        {
            target = normalizeShowQuery(command_text::toUtf8(rb->resval.rstring));
        }
        else if (rb->restype == RTENAME)
        {
            AcDbObjectId id;
            if (acdbGetObjectId(id, rb->resval.rlname) == Acad::eOk)
            {
                target = "'" + handleToString(id.handle()) + "'";
            }
        }

        std::string jsCode;
        if (rb->rbnext != nullptr)
        {
            resbuf *pNext = rb->rbnext;
            std::string patch = (pNext->restype == RTSTR && pNext->resval.rstring != nullptr)
                ? normalizeShowQuery(command_text::toUtf8(pNext->resval.rstring))
                : "{}";
            jsCode = "editor.eSet(" + target + ", " + patch + ");";
        }
        else
        {
            if (target.rfind("(() =>", 0) == 0)
            {
                jsCode = "editor.eSet(" + target + ");";
            }
            else if (target.find("set") != std::string::npos && target.find(':') != std::string::npos)
            {
                jsCode = "editor.eSet(" + target + ");";
            }
            else
            {
                jsCode = "editor.eSet({ set: " + target + " });";
            }
        }

        const js_engine::EvalResult result = js_engine::evalForRepl(jsCode);
        if (result.ok && result.text != "null")
        {
            const std::basic_string<ACHAR> resStr = command_text::fromUtf8(result.text);
            acedRetStr(resStr.c_str());
            return RTNORM;
        }

        acutPrintf(ACRX_T("\n(e-set) error: %s\n"), command_text::fromUtf8(result.text).c_str());
        acedRetNil();
        return RTERROR;
    }

    std::string buildHandlesJsArray(const std::vector<std::string> &handles)
    {
        std::string s = "[";
        for (size_t i = 0; i < handles.size(); ++i)
        {
            if (i > 0) s += ",";
            s += "\"" + handles[i] + "\"";
        }
        s += "]";
        return s;
    }

    bool extractHandlesFromResbuf(resbuf *&rb, std::vector<std::string> &handles, bool &hadSelection)
    {
        hadSelection = false;
        handles.clear();
        if (rb == nullptr)
        {
            return false;
        }

        if (rb->restype == RTPICKS)
        {
            hadSelection = true;
            Adesk::Int32 len = 0;
            if (acedSSLength(rb->resval.rlname, &len) == RTNORM && len > 0)
            {
                std::unordered_set<std::string> seen;
                for (Adesk::Int32 i = 0; i < len; ++i)
                {
                    ads_name ent;
                    if (acedSSName(rb->resval.rlname, i, ent) == RTNORM)
                    {
                        AcDbObjectId id;
                        if (acdbGetObjectId(id, ent) == Acad::eOk && id.isValid() && !id.isErased())
                        {
                            std::string h = handleToString(id.handle());
                            if (!h.empty() && seen.insert(h).second)
                            {
                                handles.push_back(h);
                            }
                        }
                    }
                }
            }
            rb = rb->rbnext;
            return true;
        }

        if (rb->restype == RTENAME)
        {
            hadSelection = true;
            AcDbObjectId id;
            if (acdbGetObjectId(id, rb->resval.rlname) == Acad::eOk && id.isValid() && !id.isErased())
            {
                std::string h = handleToString(id.handle());
                if (!h.empty())
                {
                    handles.push_back(h);
                }
            }
            rb = rb->rbnext;
            return true;
        }

        if (rb->restype == RTLB)
        {
            hadSelection = true;
            rb = rb->rbnext;
            int depth = 1;
            std::unordered_set<std::string> seen;
            while (rb != nullptr && depth > 0)
            {
                if (rb->restype == RTLB)
                {
                    depth++;
                }
                else if (rb->restype == RTLE)
                {
                    depth--;
                }
                else if (rb->restype == RTENAME)
                {
                    AcDbObjectId id;
                    if (acdbGetObjectId(id, rb->resval.rlname) == Acad::eOk && id.isValid() && !id.isErased())
                    {
                        std::string h = handleToString(id.handle());
                        if (!h.empty() && seen.insert(h).second)
                        {
                            handles.push_back(h);
                        }
                    }
                }
                else if (rb->restype == RTSTR && rb->resval.rstring != nullptr)
                {
                    std::string h = command_text::toUtf8(rb->resval.rstring);
                    if (!h.empty() && seen.insert(h).second)
                    {
                        handles.push_back(h);
                    }
                }
                rb = rb->rbnext;
            }
            return true;
        }

        if (rb->restype == RTNIL)
        {
            hadSelection = true;
            rb = rb->rbnext;
            return true;
        }

        if (rb->restype == RTSTR && rb->rbnext != nullptr && rb->rbnext->restype == RTSTR)
        {
            std::string firstStr = command_text::toUtf8(rb->resval.rstring);
            if (!firstStr.empty() && firstStr.find_first_of(" \t\r\n{}:=>") == std::string::npos)
            {
                hadSelection = true;
                handles.push_back(firstStr);
                rb = rb->rbnext;
                return true;
            }
        }

        return false;
    }

    int lispSsGet(resbuf *rb)
    {
        bool hadSelection = false;
        std::vector<std::string> handles;
        extractHandlesFromResbuf(rb, handles, hadSelection);

        std::string jsCode;
        if (hadSelection)
        {
            std::string handlesJs = buildHandlesJsArray(handles);
            if (rb != nullptr && rb->restype == RTSTR && rb->resval.rstring != nullptr)
            {
                const std::string arg = normalizeShowQuery(command_text::toUtf8(rb->resval.rstring));
                if (arg.rfind("{", 0) == 0)
                {
                    jsCode = "editor.ssGet(Object.assign({ handles: " + handlesJs + " }, (" + arg + ")));";
                }
                else
                {
                    jsCode = "editor.ssGet({ handles: " + handlesJs + ", filter: (" + arg + ") });";
                }
            }
            else
            {
                jsCode = "editor.ssGet({ handles: " + handlesJs + " });";
            }
        }
        else
        {
            if (rb != nullptr && rb->restype == RTSTR && rb->resval.rstring != nullptr)
            {
                const std::string arg = normalizeShowQuery(command_text::toUtf8(rb->resval.rstring));
                jsCode = "editor.ssGet(" + arg + ");";
            }
            else
            {
                jsCode = "editor.ssGet();";
            }
        }

        js_engine::EvalResult result = js_engine::evalJson(jsCode);
        if (!result.ok)
        {
            result = js_engine::evalForRepl(jsCode);
        }
        if (result.ok)
        {
            const std::basic_string<ACHAR> resStr = command_text::fromUtf8(result.text);
            acedRetStr(resStr.c_str());
            return RTNORM;
        }

        acedRetNil();
        return RTERROR;
    }

    int lispSsSet(resbuf *rb)
    {
        bool hadSelection = false;
        std::vector<std::string> handles;
        extractHandlesFromResbuf(rb, handles, hadSelection);

        if (rb == nullptr || rb->restype != RTSTR || rb->resval.rstring == nullptr)
        {
            acutPrintf(ACRX_T("\nError: (ss-set [ss] <patch-options>) expects a string argument."));
            acedRetNil();
            return RTERROR;
        }

        const std::string arg = command_text::toUtf8(rb->resval.rstring);
        std::string norm = normalizeShowQuery(arg);
        std::string jsCode;

        if (hadSelection)
        {
            std::string handlesJs = buildHandlesJsArray(handles);
            if (norm.rfind("(() =>", 0) == 0)
            {
                jsCode = "editor.ssSet(Object.assign({ handles: " + handlesJs + " }, (" + norm + ")()));";
            }
            else if (norm.find("set") != std::string::npos && norm.find(':') != std::string::npos)
            {
                jsCode = "editor.ssSet(Object.assign({ handles: " + handlesJs + " }, (" + norm + ")));";
            }
            else
            {
                jsCode = "editor.ssSet({ handles: " + handlesJs + ", set: (" + norm + ") });";
            }
        }
        else
        {
            if (norm.rfind("(() =>", 0) == 0)
            {
                jsCode = "editor.ssSet(" + norm + ");";
            }
            else if (norm.find("set") != std::string::npos && norm.find(':') != std::string::npos)
            {
                jsCode = "editor.ssSet(" + norm + ");";
            }
            else
            {
                jsCode = "editor.ssSet({ set: " + norm + " });";
            }
        }

        js_engine::EvalResult result = js_engine::evalJson(jsCode);
        if (!result.ok)
        {
            result = js_engine::evalForRepl(jsCode);
        }
        if (result.ok)
        {
            acedRetVoid();
            return RTNORM;
        }

        acutPrintf(ACRX_T("\n(ss-set) error: %s\n"), command_text::fromUtf8(result.text).c_str());
        acedRetNil();
        return RTERROR;
    }

    int lispSsQuery(resbuf *rb)
    {
        bool hadSelection = false;
        std::vector<std::string> handles;
        extractHandlesFromResbuf(rb, handles, hadSelection);

        std::string jsCode;
        if (hadSelection)
        {
            std::string handlesJs = buildHandlesJsArray(handles);
            if (rb != nullptr && rb->restype == RTSTR && rb->resval.rstring != nullptr)
            {
                const std::string arg = command_text::toUtf8(rb->resval.rstring);
                if (query_dsl::isDslQuery(arg))
                {
                    query_dsl::DslParseResult parsed = query_dsl::parse(arg);
                    const std::string fn = query_dsl::compileToJsFunction(arg, "entries");
                    std::ostringstream ss;
                    ss << "editor.ssQuery({\n"
                       << "  handles: " << handlesJs << ",\n"
                       << "  implied: " << (parsed.options.implied ? "true" : "false") << ",\n"
                       << "  query: " << fn << "\n"
                       << "});";
                    jsCode = ss.str();
                }
                else
                {
                    std::string norm = normalizeShowQuery(arg);
                    if (norm.rfind("{", 0) == 0)
                    {
                        jsCode = "editor.ssQuery(Object.assign({ handles: " + handlesJs + " }, (" + norm + ")));";
                    }
                    else if (norm.find("=>") != std::string::npos || norm.rfind("function", 0) == 0)
                    {
                        jsCode = "editor.ssQuery({ handles: " + handlesJs + ", filter: (" + norm + ") });";
                    }
                    else
                    {
                        jsCode = "editor.ssQuery(Object.assign({ handles: " + handlesJs + " }, (" + norm + ")));";
                    }
                }
            }
            else
            {
                jsCode = "editor.ssQuery({ handles: " + handlesJs + " });";
            }
        }
        else
        {
            if (rb == nullptr || rb->restype != RTSTR || rb->resval.rstring == nullptr)
            {
                acutPrintf(ACRX_T("\nError: (ss-query [ss] <query-options>) expects a string argument."));
                acedRetNil();
                return RTERROR;
            }

            const std::string arg = command_text::toUtf8(rb->resval.rstring);
            if (query_dsl::isDslQuery(arg))
            {
                jsCode = "editor.ssQuery(" + query_dsl::compileToSsQueryOptions(arg) + ");";
            }
            else
            {
                std::string norm = normalizeShowQuery(arg);
                jsCode = "editor.ssQuery(" + norm + ");";
            }
        }

        js_engine::EvalResult result = js_engine::evalJson(jsCode);
        if (!result.ok)
        {
            result = js_engine::evalForRepl(jsCode);
        }
        if (result.ok)
        {
            const std::basic_string<ACHAR> resStr = command_text::fromUtf8(result.text);
            acedRetStr(resStr.c_str());
            return RTNORM;
        }

        acutPrintf(ACRX_T("\n(ss-query) error: %s\n"), command_text::fromUtf8(result.text).c_str());
        acedRetNil();
        return RTERROR;
    }

    int lispEShow(resbuf *rb)
    {
        std::string argStr;
        if (rb != nullptr)
        {
            if (rb->restype == RTSTR && rb->resval.rstring != nullptr)
            {
                argStr = normalizeShowQuery(command_text::toUtf8(rb->resval.rstring));
                if (argStr.find("{") == std::string::npos && argStr.find("(") == std::string::npos && argStr.find("=>") == std::string::npos)
                {
                    argStr = "'" + argStr + "'";
                }
            }
            else if (rb->restype == RTENAME)
            {
                AcDbObjectId id;
                if (acdbGetObjectId(id, rb->resval.rlname) == Acad::eOk)
                {
                    argStr = "'" + handleToString(id.handle()) + "'";
                }
            }
        }

        const std::string jsCode = "editor.eShow(" + argStr + ");";
        const js_engine::EvalResult result = js_engine::evalForRepl(jsCode);
        if (result.ok && result.text != "null" && result.text != "undefined")
        {
            acedRetT();
            return RTNORM;
        }

        acedRetNil();
        return RTERROR;
    }

    // =========================================================================
    // AutoCAD Interactive Commands (CLI) with Query <Last>: Prompt
    // =========================================================================

    // Command: SS-SET
    void cmdSsSet()
    {
        std::string query;
        if (!promptQuery(ACRX_T(""), s_lastQuerySsSet, query) || query.empty())
        {
            return;
        }

        std::vector<int> placeholders = detectTemplatePlaceholders(query);

        if (!placeholders.empty())
        {
            while (true)
            {
                std::unordered_map<int, std::string> placeholderValues;
                if (!promptTemplatePlaceholders(placeholders, placeholderValues))
                {
                    break;
                }

                std::string norm = normalizeShowQuery(query);
                std::string code;
                if (norm.rfind("(() =>", 0) == 0)
                {
                    code = "editor.ssSet(" + norm + ");";
                }
                else if (norm.find("set") != std::string::npos && norm.find(':') != std::string::npos)
                {
                    code = "editor.ssSet(" + norm + ");";
                }
                else
                {
                    code = "editor.ssSet({ set: " + norm + " });";
                }

                code = wrapWithPlaceholderBindings(code, placeholderValues);

                const js_engine::EvalResult result = js_engine::evalForRepl(code);
                if (!result.ok)
                {
                    acutPrintf(ACRX_T("\nError: %s\n"), command_text::fromUtf8(result.text).c_str());
                    break;
                }

                if (result.text == "[]")
                {
                    break;
                }
            }
            return;
        }

        std::string norm = normalizeShowQuery(query);
        std::string code;
        if (norm.rfind("(() =>", 0) == 0)
        {
            code = "editor.ssSet(" + norm + ");";
        }
        else if (norm.find("set") != std::string::npos && norm.find(':') != std::string::npos)
        {
            code = "editor.ssSet(" + norm + ");";
        }
        else
        {
            code = "editor.ssSet({ set: " + norm + " });";
        }

        const js_engine::EvalResult result = js_engine::evalForRepl(code);
        if (!result.ok)
        {
            acutPrintf(ACRX_T("\nError: %s\n"), command_text::fromUtf8(result.text).c_str());
        }
    }

    // Command: SS-GET
    void cmdSsGet()
    {
        std::string query;
        if (!promptQuery(ACRX_T("({ implied: true })"), s_lastQuerySsGet, query))
        {
            return;
        }

        std::vector<int> placeholders = detectTemplatePlaceholders(query);

        if (!placeholders.empty())
        {
            while (true)
            {
                std::unordered_map<int, std::string> placeholderValues;
                if (!promptTemplatePlaceholders(placeholders, placeholderValues))
                {
                    break;
                }

                std::string code;
                if (query.empty())
                {
                    code = "editor.ssGet();";
                }
                else
                {
                    std::string norm = normalizeShowQuery(query);
                    code = "editor.ssGet(" + norm + ");";
                }

                code = wrapWithPlaceholderBindings(code, placeholderValues);

                const js_engine::EvalResult result = js_engine::evalForRepl(code);
                if (!result.ok)
                {
                    acutPrintf(ACRX_T("\nError: %s\n"), command_text::fromUtf8(result.text).c_str());
                    break;
                }

                if (result.text == "[]")
                {
                    break;
                }

                const std::basic_string<ACHAR> resStr = command_text::fromUtf8(result.text);
                acutPrintf(ACRX_T("\n%s\n"), resStr.c_str());
            }
            return;
        }

        std::string code;
        if (query.empty())
        {
            code = "editor.ssGet();";
        }
        else
        {
            std::string norm = normalizeShowQuery(query);
            code = "editor.ssGet(" + norm + ");";
        }
        runCommandAndPrintRepl(code);
    }

    // Command: E-SET
    void cmdESet()
    {
        std::string query;
        if (!promptQuery(ACRX_T("({ set: e => ({}) })"), s_lastQueryESet, query) || query.empty())
        {
            return;
        }

        std::vector<int> placeholders = detectTemplatePlaceholders(query);

        if (!placeholders.empty())
        {
            while (true)
            {
                std::unordered_map<int, std::string> placeholderValues;
                if (!promptTemplatePlaceholders(placeholders, placeholderValues))
                {
                    break;
                }

                std::string norm = normalizeShowQuery(query);
                std::string code;
                if (norm.rfind("(() =>", 0) == 0)
                {
                    code = "editor.eSet(" + norm + ");";
                }
                else if (norm.find("set") != std::string::npos && norm.find(':') != std::string::npos)
                {
                    code = "editor.eSet(" + norm + ");";
                }
                else
                {
                    code = "editor.eSet({ set: " + norm + " });";
                }

                code = wrapWithPlaceholderBindings(code, placeholderValues);

                const js_engine::EvalResult result = js_engine::evalForRepl(code);
                if (!result.ok)
                {
                    acutPrintf(ACRX_T("\nError: %s\n"), command_text::fromUtf8(result.text).c_str());
                    break;
                }

                if (result.text == "null")
                {
                    break;
                }
            }
            return;
        }

        std::string norm = normalizeShowQuery(query);
        std::string code;
        if (norm.rfind("(() =>", 0) == 0)
        {
            code = "editor.eSet(" + norm + ");";
        }
        else if (norm.find("set") != std::string::npos && norm.find(':') != std::string::npos)
        {
            code = "editor.eSet(" + norm + ");";
        }
        else
        {
            code = "editor.eSet({ set: " + norm + " });";
        }

        runCommandAndPrintRepl(code);
    }

    // Command: E-GET
    void cmdEGet()
    {
        std::string query;
        if (!promptQuery(ACRX_T("({ implied: false })"), s_lastQueryEGet, query))
        {
            return;
        }

        std::vector<int> placeholders = detectTemplatePlaceholders(query);
        std::unordered_map<int, std::string> placeholderValues;
        if (!placeholders.empty())
        {
            if (!promptTemplatePlaceholders(placeholders, placeholderValues))
            {
                return;
            }
        }

        std::string code;
        if (query.empty())
        {
            code = "editor.eGet();";
        }
        else
        {
            std::string norm = normalizeShowQuery(query);
            code = "editor.eGet(" + norm + ");";
        }

        if (!placeholderValues.empty())
        {
            code = wrapWithPlaceholderBindings(code, placeholderValues);
        }

        runCommandAndPrintRepl(code);
    }

    // Command: E-SHOW
    void cmdEShow()
    {
        std::string query;
        if (!promptQuery(ACRX_T("e => ({ a: e.a })"), s_lastQueryEShow, query))
        {
            return;
        }

        std::string code;
        if (query.empty())
        {
            code = "editor.eShow(e => ({ a: e.a }));";
        }
        else
        {
            std::string norm = normalizeShowQuery(query);
            code = "editor.eShow(" + norm + ");";
        }
        runCommandAndPrintRepl(code);
    }

    // Command: SS-QUERY
    void cmdSsQuery()
    {
        std::string query;
        if (!promptSsQueryString(s_lastQuerySsQuery, query) || query.empty())
        {
            return;
        }

        std::string defaultChoice = "All";

        while (true)
        {
            if (acedUsrBrk())
            {
                acutPrintf(ACRX_T("\n... command canceled\n"));
                break;
            }

            ACHAR promptBuf[256] = {};
            if (defaultChoice == "Select")
            {
                swprintf(promptBuf, 256, ACRX_T("\nselect objects or Esc to cancel [Select/All] <Select>: "));
            }
            else
            {
                swprintf(promptBuf, 256, ACRX_T("\nselect objects [Select/All] <All>: "));
            }

            const ACHAR *prompts[2] = {
                promptBuf,
                ACRX_T("\nRemove objects: ")
            };

            ads_name ss;
            acdbNameClear(ss);

            int rc = acedSSGet(ACRX_T("_:$:S"), prompts, nullptr, nullptr, ss);

            if (rc == RTCAN)
            {
                acutPrintf(ACRX_T("\n... command canceled\n"));
                break;
            }

            bool runAll = false;
            bool runSelection = false;

            if (rc == RTNORM)
            {
                Adesk::Int32 len = 0;
                if (acedSSLength(ss, &len) == RTNORM && len > 0)
                {
                    runSelection = true;
                }
                else
                {
                    acedSSFree(ss);
                    if (defaultChoice == "All")
                    {
                        runAll = true;
                    }
                    else
                    {
                        acutPrintf(ACRX_T("\n... command canceled\n"));
                        break;
                    }
                }
            }
            else if (rc == RTNONE || rc == RTERROR)
            {
                int errnoVal = 0;
                resbuf rbErr;
                if (acedGetVar(ACRX_T("ERRNO"), &rbErr) == RTNORM)
                {
                    errnoVal = rbErr.resval.rint;
                }

                if (errnoVal == 7)
                {
                    // User picked empty space, prompt again
                    continue;
                }

                // User pressed Enter (errno == 52 or 0)
                if (defaultChoice == "All")
                {
                    runAll = true;
                }
                else
                {
                    acutPrintf(ACRX_T("\n... command canceled\n"));
                    break;
                }
            }
            else
            {
                acutPrintf(ACRX_T("\n... command canceled\n"));
                break;
            }

            std::string handlesJson = "[";

            if (runAll)
            {
                AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
                if (pDoc != nullptr && pDoc->database() != nullptr)
                {
                    AcDbBlockTableRecord *pSpace = nullptr;
                    if (acdbOpenObject(pSpace, pDoc->database()->currentSpaceId(), AcDb::kForRead) == Acad::eOk && pSpace != nullptr)
                    {
                        AcDbBlockTableRecordIterator *pIter = nullptr;
                        if (pSpace->newIterator(pIter) == Acad::eOk && pIter != nullptr)
                        {
                            for (; !pIter->done(); pIter->step())
                            {
                                AcDbObjectId id;
                                if (pIter->getEntityId(id) == Acad::eOk && id.isValid() && !id.isErased())
                                {
                                    if (handlesJson.length() > 1) handlesJson += ", ";
                                    handlesJson += "'" + handleToString(id.handle()) + "'";
                                }
                            }
                            delete pIter;
                        }
                        pSpace->close();
                    }
                }
                handlesJson += "]";
                defaultChoice = "All";
            }
            else if (runSelection)
            {
                Adesk::Int32 len = 0;
                if (acedSSLength(ss, &len) == RTNORM)
                {
                    for (Adesk::Int32 i = 0; i < len; ++i)
                    {
                        ads_name ent;
                        if (acedSSName(ss, i, ent) == RTNORM)
                        {
                            AcDbObjectId id;
                            if (acdbGetObjectId(id, ent) == Acad::eOk && id.isValid())
                            {
                                if (handlesJson.length() > 1) handlesJson += ", ";
                                handlesJson += "'" + handleToString(id.handle()) + "'";
                            }
                        }
                    }
                }
                handlesJson += "]";
                acedSSFree(ss);
                defaultChoice = "Select";
            }
            else
            {
                continue;
            }

            std::string queryFnJs;
            if (query_dsl::isDslQuery(query))
            {
                queryFnJs = query_dsl::compileToJsFunction(query, "entries");
            }
            else
            {
                std::string norm = normalizeShowQuery(query);
                if (norm.rfind("(() =>", 0) == 0)
                {
                    if (query.find("entries") != std::string::npos)
                    {
                        queryFnJs = "((entries) => { const _f = (" + norm + "); return typeof _f === 'function' ? _f(entries) : _f; })";
                    }
                    else
                    {
                        queryFnJs = "((entries) => { const _f = (" + norm + "); return typeof _f === 'function' ? entries.map(_f) : _f; })";
                    }
                }
                else if (norm.find("entries") != std::string::npos)
                {
                    queryFnJs = "((" + norm + "))";
                }
                else if (norm.find("=>") != std::string::npos || norm.rfind("function", 0) == 0)
                {
                    queryFnJs = "((entries) => entries.map(" + norm + "))";
                }
                else
                {
                    queryFnJs = "((entries) => (" + query + ")(entries))";
                }
            }

            std::string code =
                "(() => {\n" +
                std::string(kExpandEntitiesScript) +
                "  const fn = " + queryFnJs + ";\n"
                "  const handles = " + handlesJson + ";\n"
                "  const rawEntries = handles.map(h => entity.get(h)).filter(e => e !== null);\n"
                "  const entries = expandEntities(rawEntries);\n"
                "  const _res = fn(entries);\n"
                "  const _getDigits = () => {\n"
                "    if (typeof globalThis.ROUND === 'number' && Number.isFinite(globalThis.ROUND)) return Math.floor(globalThis.ROUND);\n"
                "    if (typeof globalThis.round === 'number' && Number.isFinite(globalThis.round)) return Math.floor(globalThis.round);\n"
                "    if (typeof globalThis.ROUND_DIGITS === 'number' && Number.isFinite(globalThis.ROUND_DIGITS)) return Math.floor(globalThis.ROUND_DIGITS);\n"
                "    if (typeof globalThis.round_digits === 'number' && Number.isFinite(globalThis.round_digits)) return Math.floor(globalThis.round_digits);\n"
                "    if (typeof globalThis.round === 'function' && typeof globalThis.round.digits === 'number' && Number.isFinite(globalThis.round.digits)) return Math.floor(globalThis.round.digits);\n"
                "    if (typeof globalThis.digits === 'number' && Number.isFinite(globalThis.digits)) return Math.floor(globalThis.digits);\n"
                "    if (typeof globalThis.precision === 'number' && Number.isFinite(globalThis.precision)) return Math.floor(globalThis.precision);\n"
                "    return undefined;\n"
                "  };\n"
                "  const _roundValue = (val, d) => {\n"
                "    if (d === undefined || d === null) return val;\n"
                "    if (typeof val === 'number') {\n"
                "      if (!Number.isFinite(val)) return val;\n"
                "      if (typeof globalThis.round === 'function') {\n"
                "        try { return globalThis.round(val, d); } catch {}\n"
                "      }\n"
                "      const factor = Math.pow(10, d);\n"
                "      return Math.round((val + Number.EPSILON) * factor) / factor;\n"
                "    }\n"
                "    if (Array.isArray(val)) return val.map(x => _roundValue(x, d));\n"
                "    if (val !== null && typeof val === 'object') {\n"
                "      const out = Object.create(Object.getPrototypeOf(val));\n"
                "      for (const k of Object.keys(val)) out[k] = _roundValue(val[k], d);\n"
                "      return out;\n"
                "    }\n"
                "    return val;\n"
                "  };\n"
                "  return _roundValue(_res, _getDigits());\n"
                "})();";

            const js_engine::EvalResult result = js_engine::evalForRepl(code);
            if (result.ok)
            {
                if (!result.text.empty() && result.text != "undefined")
                {
                    acutPrintf(ACRX_T("\n%s\n"), command_text::fromUtf8(result.text).c_str());
                }
            }
            else
            {
                acutPrintf(ACRX_T("\nError: %s\n"), command_text::fromUtf8(result.text).c_str());
            }
        }
    }
}

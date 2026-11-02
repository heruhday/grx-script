#include "query_dsl/query_dsl.h"
#include "quickjs.h"
#include "../src/js_modules/color_utils.h"

#include <iostream>
#include <string>
#include <cassert>

namespace
{
    JSValue evalJs(JSContext *ctx, const std::string &code)
    {
        JSValue val = JS_Eval(ctx, code.c_str(), code.length(), "<test>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(val))
        {
            JSValue exceptionVal = JS_GetException(ctx);
            const char *str = JS_ToCString(ctx, exceptionVal);
            std::cerr << "QuickJS Error during eval:\n"
                      << (str ? str : "unknown") << "\nCode was:\n"
                      << code << std::endl;
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exceptionVal);
            assert(false && "QuickJS evaluation threw exception");
        }
        return val;
    }

    std::string getJson(JSContext *ctx, JSValue val)
    {
        JSValue jsonStr = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
        const char *cstr = JS_ToCString(ctx, jsonStr);
        std::string s = cstr ? cstr : "";
        JS_FreeCString(ctx, cstr);
        JS_FreeValue(ctx, jsonStr);
        return s;
    }

    double getPropNumber(JSContext *ctx, JSValue obj, const char *prop)
    {
        JSValue val = JS_GetPropertyStr(ctx, obj, prop);
        double d = 0;
        JS_ToFloat64(ctx, &d, val);
        JS_FreeValue(ctx, val);
        return d;
    }

    bool getPropBool(JSContext *ctx, JSValue obj, const char *prop)
    {
        JSValue val = JS_GetPropertyStr(ctx, obj, prop);
        bool b = JS_ToBool(ctx, val) != 0;
        JS_FreeValue(ctx, val);
        return b;
    }

    std::string getPropString(JSContext *ctx, JSValue obj, const char *prop)
    {
        JSValue val = JS_GetPropertyStr(ctx, obj, prop);
        const char *cstr = JS_ToCString(ctx, val);
        std::string s = cstr ? cstr : "";
        JS_FreeCString(ctx, cstr);
        JS_FreeValue(ctx, val);
        return s;
    }
}

int main()
{
    std::cout << "=============================================" << std::endl;
    std::cout << " Running Comprehensive QuickJS Tests for DSL" << std::endl;
    std::cout << "=============================================" << std::endl;

    JSRuntime *rt = JS_NewRuntime();
    assert(rt != nullptr);

    // -------------------------------------------------------------
    // Test 1: Example 1 — finite converted numbers, strings to numbers, Infinity/null to 0
    // -------------------------------------------------------------
    {
        std::cout << "[Test 1] Example 1: Sums finite converted numbers by code" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "Query: {e.code: {a: +=e.a, l: +=e.l}}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { code: 'A', a: '2.5', l: 4 },\n"
                    "  { code: 'A', a: 3, l: 'bad' },\n"
                    "  { code: 'B', a: Infinity, l: null },\n"
                    "  { code: 'B', l: '7' }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Verify result: { A: { a: 5.5, l: 4 }, B: { a: 0, l: 7 } }
        JSValue result = evalJs(ctx, "result");
        JSValue groupA = JS_GetPropertyStr(ctx, result, "A");
        JSValue groupB = JS_GetPropertyStr(ctx, result, "B");

        assert(!JS_IsUndefined(groupA));
        assert(!JS_IsUndefined(groupB));

        double aA = getPropNumber(ctx, groupA, "a");
        double aL = getPropNumber(ctx, groupA, "l");
        double bA = getPropNumber(ctx, groupB, "a");
        double bL = getPropNumber(ctx, groupB, "l");

        std::cout << "  Group A: a=" << aA << " (expected 5.5), l=" << aL << " (expected 4)" << std::endl;
        std::cout << "  Group B: a=" << bA << " (expected 0), l=" << bL << " (expected 7)" << std::endl;

        assert(aA == 5.5);
        assert(aL == 4.0);
        assert(bA == 0.0);
        assert(bL == 7.0);

        // Verify prototype is null
        JSValue isNullProto = evalJs(ctx, "Object.getPrototypeOf(result) === null");
        assert(JS_ToBool(ctx, isNullProto));
        JS_FreeValue(ctx, isNullProto);

        JS_FreeValue(ctx, groupA);
        JS_FreeValue(ctx, groupB);
        JS_FreeValue(ctx, result);
        JS_FreeContext(ctx);
    }

    // -------------------------------------------------------------
    // Test 2: Example 2 — Multi-level nested groups (Building -> Floor -> Code)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 2] Example 2: Isolates building, floor and code groups" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "Query: {e.building: {e.floor: {e.code: {a: +=e.a, l: +=e.l}}}}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { building: 'B1', floor: 1, code: 'X', a: 2, l: 3 },\n"
                    "  { building: 'B1', floor: 1, code: 'X', a: 4, l: 5 },\n"
                    "  { building: 'B1', floor: 2, code: 'X', a: 1, l: 2 },\n"
                    "  { building: 'B2', floor: 1, code: 'X', a: 9, l: 8 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Verify result: B1.1.X: a=6, l=8; B1.2.X: a=1, l=2; B2.1.X: a=9, l=8
        JSValue b1_1_x_a = evalJs(ctx, "result.B1[1].X.a");
        JSValue b1_1_x_l = evalJs(ctx, "result.B1[1].X.l");
        JSValue b1_2_x_a = evalJs(ctx, "result.B1[2].X.a");
        JSValue b2_1_x_a = evalJs(ctx, "result.B2[1].X.a");

        double d_b1_1_x_a = 0, d_b1_1_x_l = 0, d_b1_2_x_a = 0, d_b2_1_x_a = 0;
        JS_ToFloat64(ctx, &d_b1_1_x_a, b1_1_x_a);
        JS_ToFloat64(ctx, &d_b1_1_x_l, b1_1_x_l);
        JS_ToFloat64(ctx, &d_b1_2_x_a, b1_2_x_a);
        JS_ToFloat64(ctx, &d_b2_1_x_a, b2_1_x_a);

        assert(d_b1_1_x_a == 6.0);
        assert(d_b1_1_x_l == 8.0);
        assert(d_b1_2_x_a == 1.0);
        assert(d_b2_1_x_a == 9.0);

        JS_FreeValue(ctx, b1_1_x_a);
        JS_FreeValue(ctx, b1_1_x_l);
        JS_FreeValue(ctx, b1_2_x_a);
        JS_FreeValue(ctx, b2_1_x_a);
        JS_FreeContext(ctx);
        std::cout << "  Multi-level grouping verified successfully." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 3: Prototype names are safe (__proto__, constructor)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 3] Prototype safety: __proto__ and constructor as group keys and fields" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "{e.code: {\"__proto__\": +=e.a, constructor: +=e.l}}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { code: '__proto__', a: 2, l: 3 },\n"
                    "  { code: '__proto__', a: 4, l: 5 },\n"
                    "  { code: 'constructor', a: 1, l: 1 }\n"
                    "];");

        evalJs(ctx, jsCode);

        JSValue proto_proto = evalJs(ctx, "result['__proto__']['__proto__']");
        JSValue proto_ctor = evalJs(ctx, "result['__proto__']['constructor']");
        JSValue ctor_proto = evalJs(ctx, "result['constructor']['__proto__']");
        JSValue ctor_ctor = evalJs(ctx, "result['constructor']['constructor']");

        double d1 = 0, d2 = 0, d3 = 0, d4 = 0;
        JS_ToFloat64(ctx, &d1, proto_proto);
        JS_ToFloat64(ctx, &d2, proto_ctor);
        JS_ToFloat64(ctx, &d3, ctor_proto);
        JS_ToFloat64(ctx, &d4, ctor_ctor);

        assert(d1 == 6.0);
        assert(d2 == 8.0);
        assert(d3 == 1.0);
        assert(d4 == 1.0);

        JS_FreeValue(ctx, proto_proto);
        JS_FreeValue(ctx, proto_ctor);
        JS_FreeValue(ctx, ctor_proto);
        JS_FreeValue(ctx, ctor_ctor);
        JS_FreeContext(ctx);
        std::cout << "  __proto__ and constructor safely handled without prototype pollution." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 4: Complex JS Expressions: ??, ? :, optional chaining ?., bitwise <<, arithmetic *
    // -------------------------------------------------------------
    {
        std::cout << "[Test 4] Complex JS Expressions in Grouping & Aggregates" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "Query: {\n"
            "  e.building ?? \"unknown\": {\n"
            "    e.floor > 5 ? \"high\" : \"low\": {\n"
            "      area: +=(e.a ?? 0) * (e.quantity || 1),\n"
            "      total: +=e.enabled ? e.l * 2 : 0,\n"
            "      flags: +=(e.flags & 3) << 1,\n"
            "      value: +=e.metrics?.[\"area\"] ?? 0\n"
            "    }\n"
            "  }\n"
            "}";

        assert(query_dsl::isDslQuery(dsl));
        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { building: 'TowerA', floor: 10, a: 100, quantity: 2, enabled: true, l: 50, flags: 7, metrics: { area: 88 } },\n"
                    "  { building: null, floor: 2, a: null, quantity: null, enabled: false, l: 20, flags: 1, metrics: null }\n"
                    "];");

        evalJs(ctx, jsCode);

        // TowerA / high:
        // area = 100 * 2 = 200
        // total = 50 * 2 = 100
        // flags = (7 & 3) << 1 = 3 << 1 = 6
        // value = 88
        double towerArea = getPropNumber(ctx, evalJs(ctx, "result.TowerA.high"), "area");
        double towerTotal = getPropNumber(ctx, evalJs(ctx, "result.TowerA.high"), "total");
        double towerFlags = getPropNumber(ctx, evalJs(ctx, "result.TowerA.high"), "flags");
        double towerValue = getPropNumber(ctx, evalJs(ctx, "result.TowerA.high"), "value");

        assert(towerArea == 200.0);
        assert(towerTotal == 100.0);
        assert(towerFlags == 6.0);
        assert(towerValue == 88.0);

        // unknown / low:
        // area = 0 * 1 = 0
        // total = 0
        // flags = (1 & 3) << 1 = 2
        // value = 0
        double unkArea = getPropNumber(ctx, evalJs(ctx, "result.unknown.low"), "area");
        double unkTotal = getPropNumber(ctx, evalJs(ctx, "result.unknown.low"), "total");
        double unkFlags = getPropNumber(ctx, evalJs(ctx, "result.unknown.low"), "flags");
        double unkValue = getPropNumber(ctx, evalJs(ctx, "result.unknown.low"), "value");

        assert(unkArea == 0.0);
        assert(unkTotal == 0.0);
        assert(unkFlags == 2.0);
        assert(unkValue == 0.0);

        JS_FreeContext(ctx);
        std::cout << "  Complex expressions evaluated with exact precision." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 5: Global Builtins (Math.abs, Math.max) and Object Methods (e.amount(), e.code.trim())
    // -------------------------------------------------------------
    {
        std::cout << "[Test 5] Builtins and method calls" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "Query: {e.code.trim().toUpperCase(): {absA: +=Math.abs(e.a), effL: +=Math.max(e.amount(), 0)}}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { code: ' a1 ', a: -15, l: 40, amount() { return this.l; } },\n"
                    "  { code: 'A1', a: 25, l: -10, amount() { return this.l * 2; } }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Result for 'A1':
        // absA = |-15| + 25 = 40
        // effL = max(40, 0) + max(-20, 0) = 40 + 0 = 40
        double absA = getPropNumber(ctx, evalJs(ctx, "result.A1"), "absA");
        double effL = getPropNumber(ctx, evalJs(ctx, "result.A1"), "effL");

        assert(absA == 40.0);
        assert(effL == 40.0);
        JS_FreeContext(ctx);
        std::cout << "  Builtin and receiver methods verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 6: All Operators (*=, ++, push, Math.min, Math.max, &&=, ||=, =)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 6] Multi-operator test (*=, ++, push, min/max, &&=, ||=, =)" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "{\n"
            "  e.cat: {\n"
            "    prod: *=e.scale,\n"
            "    count: ++,\n"
            "    items: push(e.id),\n"
            "    minV: Math.min(e.val),\n"
            "    maxV: Math.max(e.val),\n"
            "    allValid: &&=e.valid,\n"
            "    firstName: ||=e.name,\n"
            "    lastStr: =e.last\n"
            "  }\n"
            "}";

        assert(query_dsl::isDslQuery(dsl));
        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { cat: 'item', scale: 2, valid: true, name: 'First', id: 101, val: 50, last: 'alpha' },\n"
                    "  { cat: 'item', scale: 3, valid: true, name: 'Second', id: 102, val: 80, last: 'beta' }\n"
                    "];");

        evalJs(ctx, jsCode);

        JSValue item = evalJs(ctx, "result.item");
        double prod = getPropNumber(ctx, item, "prod");
        double count = getPropNumber(ctx, item, "count");
        double minV = getPropNumber(ctx, item, "minV");
        double maxV = getPropNumber(ctx, item, "maxV");
        bool allValid = getPropBool(ctx, item, "allValid");
        std::string firstName = getPropString(ctx, item, "firstName");
        std::string lastStr = getPropString(ctx, item, "lastStr");

        std::string itemsJson = getJson(ctx, evalJs(ctx, "result.item.items"));

        std::cout << "  prod=" << prod << " (exp 6), count=" << count << " (exp 2), min=" << minV << ", max=" << maxV << std::endl;
        std::cout << "  items=" << itemsJson << ", allValid=" << allValid << ", firstName=" << firstName << ", lastStr=" << lastStr << std::endl;

        assert(prod == 6.0); // 1 * 2 * 3 = 6
        assert(count == 2.0); // 0 + 1 + 1 = 2
        assert(minV == 50.0);
        assert(maxV == 80.0);
        assert(allValid == true);
        assert(firstName == "First");
        assert(lastStr == "beta");
        assert(itemsJson == "[101,102]");

        JS_FreeValue(ctx, item);
        JS_FreeContext(ctx);
        std::cout << "  All operator actions verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 7: Arrow Function & SS-QUERY Options Compilation
    // -------------------------------------------------------------
    {
        std::cout << "[Test 7] Function and ssQuery Options Wrapping" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "Query <({ implied: true })>: {e.code: {a: +=e.a}}";
        std::string optCode = query_dsl::compileToSsQueryOptions(dsl);

        evalJs(ctx, "entries = [{ code: 'X', a: 15 }, { code: 'X', a: 25 }];");
        evalJs(ctx, "options = " + optCode + ";");

        bool implied = getPropBool(ctx, evalJs(ctx, "options"), "implied");
        assert(implied == true);

        // Execute options.query(entries)
        evalJs(ctx, "res = options.query(entries);");
        double val = getPropNumber(ctx, evalJs(ctx, "res.X"), "a");
        assert(val == 40.0);

        // Test with implied: false
        const std::string dslFalse = "Query <({ implied: false })>: {e.code: {a: +=e.a}}";
        std::string optCodeFalse = query_dsl::compileToSsQueryOptions(dslFalse);
        evalJs(ctx, "optionsFalse = " + optCodeFalse + ";");
        bool impliedFalse = getPropBool(ctx, evalJs(ctx, "optionsFalse"), "implied");
        assert(impliedFalse == false);

        JS_FreeContext(ctx);
        std::cout << "  ssQuery options wrapping and function execution verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 8: Empty Entries & Trailing Commas
    // -------------------------------------------------------------
    {
        std::cout << "[Test 8] Empty entries and trailing commas" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "Query <({ implied: true })>: {e.info.code: {a: +=e.values?.a,},}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        // Empty array
        evalJs(ctx, "entries = [];");
        evalJs(ctx, jsCode);
        std::string emptyJson = getJson(ctx, evalJs(ctx, "result"));
        assert(emptyJson == "{}");

        JS_FreeContext(ctx);
        ctx = JS_NewContext(rt);

        // Non-empty array with missing fields
        evalJs(ctx, "entries = [{ info: { code: 'A' }, values: { a: '2' } }, { info: { code: 'A' } }];");
        evalJs(ctx, jsCode);
        double a = getPropNumber(ctx, evalJs(ctx, "result.A"), "a");
        assert(a == 2.0);

        JS_FreeContext(ctx);
        std::cout << "  Empty input and trailing commas verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 9: Arbitrary Nesting Depth (5 Levels Deep)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 9] Arbitrary nesting depth (5 levels: project -> site -> building -> floor -> zone)" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "{e.proj: {e.site: {e.bldg: {e.flr: {e.zone: {totalA: +=e.a, count: ++}}}}}}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { proj: 'P1', site: 'S1', bldg: 'B1', flr: 'F1', zone: 'Z1', a: 100 },\n"
                    "  { proj: 'P1', site: 'S1', bldg: 'B1', flr: 'F1', zone: 'Z1', a: 50 },\n"
                    "  { proj: 'P1', site: 'S1', bldg: 'B1', flr: 'F2', zone: 'Z2', a: 80 }\n"
                    "];");

        evalJs(ctx, jsCode);

        double totalA = getPropNumber(ctx, evalJs(ctx, "result.P1.S1.B1.F1.Z1"), "totalA");
        double count = getPropNumber(ctx, evalJs(ctx, "result.P1.S1.B1.F1.Z1"), "count");
        double f2_totalA = getPropNumber(ctx, evalJs(ctx, "result.P1.S1.B1.F2.Z2"), "totalA");

        assert(totalA == 150.0);
        assert(count == 2.0);
        assert(f2_totalA == 80.0);

        JS_FreeContext(ctx);
        std::cout << "  5-level deep nesting verified successfully." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 10: Hierarchical Subtotals alongside Nested Groups
    // -------------------------------------------------------------
    {
        std::cout << "[Test 10] Hierarchical Subtotals alongside Nested Groups (Building -> Floor -> Code -> Room)" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "{\n"
            "  e.building:\n"
            "  {\n"
            "    total: {a: +=e.a, l: +=e.l},\n"
            "    e.floor:\n"
            "    {\n"
            "      total: {a: +=e.a, l: +=e.l},\n"
            "      e.code:\n"
            "      {\n"
            "        e.room: {a: +=e.a, l: +=e.l}\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "}";

        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");
        std::cout << "--- Generated JS for Subtotals Query ---:\n" << jsCode << "\n" << std::endl;

        evalJs(ctx, "entries = [\n"
                    "  { building: 'TowerA', floor: 'F1', code: 'WALL', room: '101', a: 100, l: 30 },\n"
                    "  { building: 'TowerA', floor: 'F1', code: 'WALL', room: '102', a: 150, l: 40 },\n"
                    "  { building: 'TowerA', floor: 'F2', code: 'DOOR', room: '201', a: 50, l: 10 },\n"
                    "  { building: 'TowerB', floor: 'F1', code: 'WALL', room: '001', a: 200, l: 60 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Verify Building Total for TowerA: a = 100 + 150 + 50 = 300, l = 30 + 40 + 10 = 80
        double bldgTotalA = getPropNumber(ctx, evalJs(ctx, "result.TowerA.total"), "a");
        double bldgTotalL = getPropNumber(ctx, evalJs(ctx, "result.TowerA.total"), "l");
        assert(bldgTotalA == 300.0);
        assert(bldgTotalL == 80.0);

        // Verify Floor Total for TowerA.F1: a = 100 + 150 = 250, l = 30 + 40 = 70
        double floorTotalA = getPropNumber(ctx, evalJs(ctx, "result.TowerA.F1.total"), "a");
        double floorTotalL = getPropNumber(ctx, evalJs(ctx, "result.TowerA.F1.total"), "l");
        assert(floorTotalA == 250.0);
        assert(floorTotalL == 70.0);

        // Verify Room Level Breakdown for TowerA.F1.WALL['101']: a = 100, l = 30
        double room101A = getPropNumber(ctx, evalJs(ctx, "result.TowerA.F1.WALL['101']"), "a");
        double room101L = getPropNumber(ctx, evalJs(ctx, "result.TowerA.F1.WALL['101']"), "l");
        assert(room101A == 100.0);
        assert(room101L == 30.0);

        // Verify Room Level Breakdown for TowerA.F1.WALL['102']: a = 150, l = 40
        double room102A = getPropNumber(ctx, evalJs(ctx, "result.TowerA.F1.WALL['102']"), "a");
        double room102L = getPropNumber(ctx, evalJs(ctx, "result.TowerA.F1.WALL['102']"), "l");
        assert(room102A == 150.0);
        assert(room102L == 40.0);

        // Verify Building Total for TowerB: a = 200, l = 60
        double bldgBTotalA = getPropNumber(ctx, evalJs(ctx, "result.TowerB.total"), "a");
        double bldgBTotalL = getPropNumber(ctx, evalJs(ctx, "result.TowerB.total"), "l");
        assert(bldgBTotalA == 200.0);
        assert(bldgBTotalL == 60.0);

        JS_FreeContext(ctx);
        std::cout << "  Hierarchical subtotals verified successfully in QuickJS!" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 11: Multiple Subtotal Blocks & Rollup Stats at the Same Level
    // -------------------------------------------------------------
    {
        std::cout << "[Test 11] Multiple subtotal blocks (total & stats) at building level" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "{\n"
            "  e.building:\n"
            "  {\n"
            "    total: { a: +=e.a, l: +=e.l },\n"
            "    stats: { count: ++, maxA: Math.max(e.a), handles: push(e.handle) },\n"
            "    e.floor:\n"
            "    {\n"
            "      total: { a: +=e.a, count: ++ }\n"
            "    }\n"
            "  }\n"
            "}";

        assert(query_dsl::isDslQuery(dsl));
        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { building: 'B1', floor: 'F1', handle: '10A', a: 100, l: 20 },\n"
                    "  { building: 'B1', floor: 'F1', handle: '10B', a: 250, l: 30 },\n"
                    "  { building: 'B1', floor: 'F2', handle: '20A', a: 80, l: 15 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Building totals & stats
        double totalA = getPropNumber(ctx, evalJs(ctx, "result.B1.total"), "a");
        double totalL = getPropNumber(ctx, evalJs(ctx, "result.B1.total"), "l");
        double count = getPropNumber(ctx, evalJs(ctx, "result.B1.stats"), "count");
        double maxA = getPropNumber(ctx, evalJs(ctx, "result.B1.stats"), "maxA");
        std::string handlesJson = getJson(ctx, evalJs(ctx, "result.B1.stats.handles"));

        assert(totalA == 430.0);
        assert(totalL == 65.0);
        assert(count == 3.0);
        assert(maxA == 250.0);
        assert(handlesJson == "[\"10A\",\"10B\",\"20A\"]");

        // Floor totals
        double f1A = getPropNumber(ctx, evalJs(ctx, "result.B1.F1.total"), "a");
        double f1Count = getPropNumber(ctx, evalJs(ctx, "result.B1.F1.total"), "count");
        double f2A = getPropNumber(ctx, evalJs(ctx, "result.B1.F2.total"), "a");

        assert(f1A == 350.0);
        assert(f1Count == 2.0);
        assert(f2A == 80.0);

        JS_FreeContext(ctx);
        std::cout << "  Multiple subtotal blocks and collections verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 12: Multiple Sibling Groups at the Same Level (Branching Breakdown)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 12] Multiple Sibling Groups (byFloor and byTrade) under Building" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "{\n"
            "  e.building:\n"
            "  {\n"
            "    total: { a: +=e.a },\n"
            "    byFloor: { e.floor: { a: +=e.a } },\n"
            "    byTrade: { e.trade: { a: +=e.a } }\n"
            "  }\n"
            "}";

        assert(query_dsl::isDslQuery(dsl));
        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { building: 'HQ', floor: '1', trade: 'HVAC', a: 120 },\n"
                    "  { building: 'HQ', floor: '1', trade: 'PLUMBING', a: 80 },\n"
                    "  { building: 'HQ', floor: '2', trade: 'HVAC', a: 200 }\n"
                    "];");

        evalJs(ctx, jsCode);

        double totalA = getPropNumber(ctx, evalJs(ctx, "result.HQ.total"), "a");
        double f1A = getPropNumber(ctx, evalJs(ctx, "result.HQ.byFloor['1']"), "a");
        double f2A = getPropNumber(ctx, evalJs(ctx, "result.HQ.byFloor['2']"), "a");
        double hvacA = getPropNumber(ctx, evalJs(ctx, "result.HQ.byTrade.HVAC"), "a");
        double plumbA = getPropNumber(ctx, evalJs(ctx, "result.HQ.byTrade.PLUMBING"), "a");

        assert(totalA == 400.0);
        assert(f1A == 200.0);
        assert(f2A == 200.0);
        assert(hvacA == 320.0);
        assert(plumbA == 80.0);

        JS_FreeContext(ctx);
        std::cout << "  Multi-branch grouping verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 13: Dynamic Arithmetic Expressions in Subtotals (Pricing & Tax)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 13] Pricing and tax calculation expressions in subtotals" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "{\n"
            "  e.building:\n"
            "  {\n"
            "    costSummary:\n"
            "    {\n"
            "      rawCost: +=(e.a ?? 0) * (e.unitPrice || 10),\n"
            "      tax: +=((e.a ?? 0) * (e.unitPrice || 10)) * 0.11\n"
            "    },\n"
            "    e.floor:\n"
            "    {\n"
            "      netCost: +=e.a * (e.unitPrice || 10)\n"
            "    }\n"
            "  }\n"
            "}";

        assert(query_dsl::isDslQuery(dsl));
        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { building: 'Tower', floor: 'F1', a: 100, unitPrice: 20 },\n" // cost 2000, tax 220
                    "  { building: 'Tower', floor: 'F2', a: 50, unitPrice: 0 }\n"     // cost 50*10=500, tax 55
                    "];");

        evalJs(ctx, jsCode);

        double rawCost = getPropNumber(ctx, evalJs(ctx, "result.Tower.costSummary"), "rawCost");
        double tax = getPropNumber(ctx, evalJs(ctx, "result.Tower.costSummary"), "tax");
        double f1Cost = getPropNumber(ctx, evalJs(ctx, "result.Tower.F1"), "netCost");
        double f2Cost = getPropNumber(ctx, evalJs(ctx, "result.Tower.F2"), "netCost");

        assert(rawCost == 2500.0);
        assert(tax == 275.0);
        assert(f1Cost == 2000.0);
        assert(f2Cost == 500.0);

        JS_FreeContext(ctx);
        std::cout << "  Pricing and formula expressions verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 14: Parser Error Handling & Rejection of Malformed Queries
    // -------------------------------------------------------------
    {
        std::cout << "[Test 14] Parser Error Handling & Graceful Rejection" << std::endl;

        // Empty input
        assert(!query_dsl::isDslQuery(""));
        assert(!query_dsl::isDslQuery("    \t\r\n"));

        // Missing braces
        assert(!query_dsl::isDslQuery("{e.code: {a: +=e.a}"));
        assert(!query_dsl::isDslQuery("e.code: {a: +=e.a}}"));

        // Malformed header
        assert(!query_dsl::isDslQuery("Query <({ implied: maybe })>: {e.code: {a: +=e.a}}"));

        // Non-DSL text
        assert(!query_dsl::isDslQuery("SELECT * FROM entities;"));
        assert(!query_dsl::isDslQuery("Hello world!"));

        // Trailing garbage after query
        query_dsl::DslParseResult res = query_dsl::parse("{e.code: {a: +=e.a}} unexpected_chars");
        assert(!res.success);
        assert(res.errorMessage.find("Unexpected trailing characters") != std::string::npos);

        std::cout << "  Syntax error detection verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 15: Mathematical Invariant in Takeoff Hierarchy
    // Sum of all room areas strictly equals floor total;
    // Sum of all floor totals strictly equals building total.
    // -------------------------------------------------------------
    {
        std::cout << "[Test 15] Mathematical Takeoff Consistency: Sum(Rooms) == Floor == Building" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl =
            "{\n"
            "  e.building:\n"
            "  {\n"
            "    total: { a: +=e.a, l: +=e.l },\n"
            "    e.floor:\n"
            "    {\n"
            "      total: { a: +=e.a, l: +=e.l },\n"
            "      e.code:\n"
            "      {\n"
            "        e.room: { a: +=e.a, l: +=e.l }\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "}";

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { building: 'Complex', floor: '1', code: 'WALL', room: 'A', a: 12.5, l: 5.0 },\n"
                    "  { building: 'Complex', floor: '1', code: 'WALL', room: 'B', a: 17.5, l: 7.0 },\n"
                    "  { building: 'Complex', floor: '1', code: 'SLAB', room: 'C', a: 30.0, l: 12.0 },\n"
                    "  { building: 'Complex', floor: '2', code: 'WALL', room: 'D', a: 40.0, l: 16.0 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Building total
        double bldgA = getPropNumber(ctx, evalJs(ctx, "result.Complex.total"), "a");
        double bldgL = getPropNumber(ctx, evalJs(ctx, "result.Complex.total"), "l");

        // Floor totals
        double f1A = getPropNumber(ctx, evalJs(ctx, "result.Complex['1'].total"), "a");
        double f1L = getPropNumber(ctx, evalJs(ctx, "result.Complex['1'].total"), "l");
        double f2A = getPropNumber(ctx, evalJs(ctx, "result.Complex['2'].total"), "a");
        double f2L = getPropNumber(ctx, evalJs(ctx, "result.Complex['2'].total"), "l");

        // Individual rooms
        double rAA = getPropNumber(ctx, evalJs(ctx, "result.Complex['1'].WALL.A"), "a");
        double rBA = getPropNumber(ctx, evalJs(ctx, "result.Complex['1'].WALL.B"), "a");
        double rCA = getPropNumber(ctx, evalJs(ctx, "result.Complex['1'].SLAB.C"), "a");
        double rDA = getPropNumber(ctx, evalJs(ctx, "result.Complex['2'].WALL.D"), "a");

        // Invariant 1: Sum(Rooms on Floor 1) == Floor 1 Total
        assert(rAA + rBA + rCA == f1A);
        // Invariant 2: Sum(Rooms on Floor 2) == Floor 2 Total
        assert(rDA == f2A);
        // Invariant 3: Floor 1 Total + Floor 2 Total == Building Total
        assert(f1A + f2A == bldgA);
        assert(f1L + f2L == bldgL);

        assert(bldgA == 100.0);
        assert(bldgL == 40.0);

        JS_FreeContext(ctx);
        std::cout << "  Mathematical consistency verified: Sum(Rooms) = " << (rAA + rBA + rCA + rDA)
                  << " == Building Total = " << bldgA << std::endl;
    }

    // -------------------------------------------------------------
    // Test 16: Flat Array Grand Total Aggregation [+=e.a, +=e.l] & [+=e.a, +=e.l, ++]
    // -------------------------------------------------------------
    {
        std::cout << "[Test 16] Flat Array Grand Total [+=e.a, +=e.l, ++]" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "Query: [+=e.a, +=e.l, ++]";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");
        std::cout << "--- Generated JS for Flat Array [+=e.a, +=e.l, ++] ---:\n" << jsCode << "\n" << std::endl;

        evalJs(ctx, "entries = [\n"
                    "  { a: 100, l: 25 },\n"
                    "  { a: 50.5, l: 15.5 },\n"
                    "  { a: 200, l: 60 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // result is [ 350.5, 100.5, 3 ]
        double totalA = getPropNumber(ctx, evalJs(ctx, "result"), "0");
        double totalL = getPropNumber(ctx, evalJs(ctx, "result"), "1");
        double count = getPropNumber(ctx, evalJs(ctx, "result"), "2");
        std::string json = getJson(ctx, evalJs(ctx, "result"));

        std::cout << "  Array result: " << json << std::endl;
        assert(totalA == 350.5);
        assert(totalL == 100.5);
        assert(count == 3.0);
        assert(json == "[350.5,100.5,3]");

        JS_FreeContext(ctx);
        std::cout << "  Flat array grand total verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 17: Grouped Array Values {e.code: [+=e.a, +=e.l]}
    // -------------------------------------------------------------
    {
        std::cout << "[Test 17] Grouped Array Values {e.code: [+=e.a, +=e.l]}" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "{e.code: [+=e.a, +=e.l]}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");
        std::cout << "--- Generated JS for {e.code: [+=e.a, +=e.l]} ---:\n" << jsCode << "\n" << std::endl;

        evalJs(ctx, "entries = [\n"
                    "  { code: 'WALL', a: 500, l: 120 },\n"
                    "  { code: 'WALL', a: 300, l: 80 },\n"
                    "  { code: 'DOOR', a: 45, l: 15 }\n"
                    "];");

        evalJs(ctx, jsCode);

        double wallA = getPropNumber(ctx, evalJs(ctx, "result.WALL"), "0");
        double wallL = getPropNumber(ctx, evalJs(ctx, "result.WALL"), "1");
        double doorA = getPropNumber(ctx, evalJs(ctx, "result.DOOR"), "0");
        double doorL = getPropNumber(ctx, evalJs(ctx, "result.DOOR"), "1");

        assert(wallA == 800.0);
        assert(wallL == 200.0);
        assert(doorA == 45.0);
        assert(doorL == 15.0);

        JS_FreeContext(ctx);
        std::cout << "  Grouped array values verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 18: Division / Arithmetic in Aggregations {e.code : [+=e.a/1e6, +=e.l/1e3]}
    // -------------------------------------------------------------
    {
        std::cout << "[Test 18] Division / Arithmetic in Aggregations {e.code : [+=e.a/1e6, +=e.l/1e3]}" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "{e.code : [+=e.a/1e6, +=e.l/1e3]}";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");
        std::cout << "--- Generated JS for Arithmetic Aggregation ---:\n" << jsCode << "\n" << std::endl;

        evalJs(ctx, "entries = [\n"
                    "  { code: 'CT1', a: 11665718.874857234, l: 13665.585803461432 },\n"
                    "  { code: 'CT1', a: 4187364.0373186776, l: 8208.2525543472 }\n"
                    "];");

        evalJs(ctx, jsCode);

        double ct1A = getPropNumber(ctx, evalJs(ctx, "result.CT1"), "0");
        double ct1L = getPropNumber(ctx, evalJs(ctx, "result.CT1"), "1");

        // Expected A = (11665718.874857234 + 4187364.0373186776) / 1e6 = 15.853082912175912
        // Expected L = (13665.585803461432 + 8208.2525543472) / 1e3 = 21.87383835780863
        assert(std::abs(ct1A - 15.853082912175912) < 1e-6);
        assert(std::abs(ct1L - 21.87383835780863) < 1e-6);

        JS_FreeContext(ctx);
        std::cout << "  Arithmetic division in aggregations verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 19: Trailing `where` filter clause
    // -------------------------------------------------------------
    {
        std::cout << "[Test 19] Trailing `where` filter clause" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        const std::string dsl = "{e.code : [+=e.a/1e6, +=e.l/1e3]} where e.code == 'CT1'";
        assert(query_dsl::isDslQuery(dsl));

        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");
        std::cout << "--- Generated JS for Trailing `where` ---:\n" << jsCode << "\n" << std::endl;

        evalJs(ctx, "entries = [\n"
                    "  { code: 'CT1', a: 10000000, l: 10000 },\n"
                    "  { code: 'CT2', a: 50000000, l: 50000 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // CT1 should be present, CT2 should be excluded!
        JSValue ct1 = evalJs(ctx, "result.CT1");
        JSValue ct2 = evalJs(ctx, "result.CT2");

        assert(!JS_IsUndefined(ct1));
        assert(JS_IsUndefined(ct2));

        double ct1A = getPropNumber(ctx, ct1, "0");
        double ct1L = getPropNumber(ctx, ct1, "1");
        assert(ct1A == 10.0);
        assert(ct1L == 10.0);

        JS_FreeContext(ctx);
        std::cout << "  Trailing `where` filter verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 20: Header options where filter and prefix where
    // -------------------------------------------------------------
    {
        std::cout << "[Test 20] Header options and prefix where" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        // Prefix where:
        const std::string dsl1 = "where e.lyr == 'WALL': {e.code: [+=e.a, +=e.l]}";
        assert(query_dsl::isDslQuery(dsl1));
        std::string jsCode1 = query_dsl::compileToJsCode(dsl1, "entries");

        evalJs(ctx, "entries = [\n"
                    "  { code: 'W1', lyr: 'WALL', a: 100, l: 10 },\n"
                    "  { code: 'W2', lyr: 'WINDOW', a: 200, l: 20 }\n"
                    "];");

        evalJs(ctx, jsCode1);
        assert(!JS_IsUndefined(evalJs(ctx, "result.W1")));
        assert(JS_IsUndefined(evalJs(ctx, "result.W2")));

        // Query header options:
        const std::string dsl2 = "Query <({ where: e.a > 150 })>: {e.code: [+=e.a, +=e.l]}";
        assert(query_dsl::isDslQuery(dsl2));
        std::string jsCode2 = query_dsl::compileToJsCode(dsl2, "entries");

        evalJs(ctx, jsCode2);
        assert(JS_IsUndefined(evalJs(ctx, "result.W1")));
        assert(!JS_IsUndefined(evalJs(ctx, "result.W2")));

        JS_FreeContext(ctx);
        std::cout << "  Header options and prefix where verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 21: Truthy and Error-Resilient `where` Expression
    // -------------------------------------------------------------
    {
        std::cout << "[Test 21] Truthy and Error-Resilient `where` Expression" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        // dsl with truthy check `where e.a`:
        const std::string dsl = "{e.code : [+=e.a/1e6, +=e.l/1e3]} where e.a";
        assert(query_dsl::isDslQuery(dsl));
        std::string jsCode = query_dsl::compileToJsCode(dsl, "entries");
        std::cout << "--- Generated JS for Truthy where ---:\n" << jsCode << "\n" << std::endl;

        // Mix of valid area, 0 area, null area, undefined area, NaN area:
        evalJs(ctx, "entries = [\n"
                    "  { code: 'OK', a: 10000000, l: 5000 },\n"
                    "  { code: 'ZERO', a: 0, l: 1000 },\n"
                    "  { code: 'NULL', a: null, l: 2000 },\n"
                    "  { code: 'UNDEF', l: 3000 },\n"
                    "  { code: 'NAN', a: NaN, l: 4000 }\n"
                    "];");

        evalJs(ctx, jsCode);

        // Only OK should be aggregated, all falsy a values safely skipped!
        JSValue ok = evalJs(ctx, "result.OK");
        assert(!JS_IsUndefined(ok));
        assert(JS_IsUndefined(evalJs(ctx, "result.ZERO")));
        assert(JS_IsUndefined(evalJs(ctx, "result.NULL")));
        assert(JS_IsUndefined(evalJs(ctx, "result.UNDEF")));
        assert(JS_IsUndefined(evalJs(ctx, "result.NAN")));

        double okA = getPropNumber(ctx, ok, "0");
        assert(okA == 10.0);

        // Non-error test with deep missing properties that would normally throw TypeError:
        JS_FreeContext(ctx);
        ctx = JS_NewContext(rt);

        evalJs(ctx, "entries = [{ code: 'OK', a: 100 }];");
        const std::string dslDeep = "{e.code: [+=e.a]} where e.deep.missing.value > 0";
        std::string jsDeep = query_dsl::compileToJsCode(dslDeep, "entries");
        evalJs(ctx, jsDeep);
        // Should produce empty result without throwing any exception:
        std::string jsonResult = getJson(ctx, evalJs(ctx, "result"));
        assert(jsonResult == "{}");

        JS_FreeContext(ctx);
        std::cout << "  Truthy and error-free where evaluation verified." << std::endl;
    }

    // -------------------------------------------------------------
    // Test 22: Bare Scalar Queries (+=a, ++, +=a/1e6 where a, +=a, +=l)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 22] Bare Scalar Queries (+=a, ++, +=a/1e6 where a)" << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        // 1. Bare `+=a`:
        const std::string dsl1 = "+=a";
        assert(query_dsl::isDslQuery(dsl1));
        std::string jsCode1 = query_dsl::compileToJsCode(dsl1, "entries");
        std::cout << "--- Generated JS for '+=a' ---:\n" << jsCode1 << "\n" << std::endl;

        evalJs(ctx, "entries = [\n"
                    "  { a: 100, l: 20 },\n"
                    "  { a: 250, l: 30 },\n"
                    "  { l: 50 }\n"
                    "];");

        evalJs(ctx, jsCode1);
        double res1 = 0;
        JS_ToFloat64(ctx, &res1, evalJs(ctx, "result"));
        assert(res1 == 350.0);

        // 2. Bare count `++`:
        JS_FreeContext(ctx);
        ctx = JS_NewContext(rt);
        evalJs(ctx, "entries = [{ a: 100, l: 20 }, { a: 250, l: 30 }, { l: 50 }];");
        const std::string dsl2 = "++";
        assert(query_dsl::isDslQuery(dsl2));
        std::string jsCode2 = query_dsl::compileToJsCode(dsl2, "entries");
        evalJs(ctx, jsCode2);
        double res2 = 0;
        JS_ToFloat64(ctx, &res2, evalJs(ctx, "result"));
        assert(res2 == 3.0);

        // 3. Bare `+=a/1e6 where a`:
        JS_FreeContext(ctx);
        ctx = JS_NewContext(rt);
        evalJs(ctx, "entries = [{ a: 100, l: 20 }, { a: 250, l: 30 }, { l: 50 }];");
        const std::string dsl3 = "+=a/1e6 where a";
        assert(query_dsl::isDslQuery(dsl3));
        std::string jsCode3 = query_dsl::compileToJsCode(dsl3, "entries");
        std::cout << "--- Generated JS for '+=a/1e6 where a' ---:\n" << jsCode3 << "\n" << std::endl;
        evalJs(ctx, jsCode3);
        double res3 = 0;
        JS_ToFloat64(ctx, &res3, evalJs(ctx, "result"));
        assert(res3 == 0.00035);

        // 4. Bare multiple aggregates `+=a, +=l`:
        JS_FreeContext(ctx);
        ctx = JS_NewContext(rt);
        evalJs(ctx, "entries = [{ a: 100, l: 20 }, { a: 250, l: 30 }, { l: 50 }];");
        const std::string dsl4 = "+=a, +=l";
        assert(query_dsl::isDslQuery(dsl4));
        std::string jsCode4 = query_dsl::compileToJsCode(dsl4, "entries");
        evalJs(ctx, jsCode4);
        std::string json4 = getJson(ctx, evalJs(ctx, "result"));
        assert(json4 == "[350,100]");

        // 5. Bare single plus `+a`:
        JS_FreeContext(ctx);
        ctx = JS_NewContext(rt);
        evalJs(ctx, "entries = [{ a: 100, l: 20 }, { a: 250, l: 30 }, { l: 50 }];");
        const std::string dsl5 = "+a";
        assert(query_dsl::isDslQuery(dsl5));
        std::string jsCode5 = query_dsl::compileToJsCode(dsl5, "entries");
        evalJs(ctx, jsCode5);
        double res5 = 0;
        JS_ToFloat64(ctx, &res5, evalJs(ctx, "result"));
        assert(res5 == 350.0);

        JS_FreeContext(ctx);
        std::cout << "  Bare scalar queries verified successfully." << std::endl;
    }

    // 25. Multiline query and block statement IIFE execution
    {
        std::cout << "\n[Test 25] Multiline query and block statement IIFE evaluation..." << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        // Simulation of editor.ssSet receiving wrapped IIFE block
        evalJs(ctx, "editor = { ssSet: function(opts) { return opts; } };");

        const std::string multilineIife =
            "editor.ssSet((() => { let counter = 0;\n"
            "    return {\n"
            "        filter: e => e.ty === \"AcDbLine\",\n"
            "        set: e => ({ layer: \"0\", count: ++counter })\n"
            "    }\n"
            "})());";

        JSValue res = evalJs(ctx, multilineIife);
        assert(JS_IsObject(res));
        JSValue filterFn = JS_GetPropertyStr(ctx, res, "filter");
        assert(JS_IsFunction(ctx, filterFn));
        JSValue setFn = JS_GetPropertyStr(ctx, res, "set");
        assert(JS_IsFunction(ctx, setFn));

        // Test filter
        evalJs(ctx, "globalThis.f = " + multilineIife + ";");
        JSValue testEntLine = evalJs(ctx, "f.filter({ ty: 'AcDbLine' });");
        assert(JS_ToBool(ctx, testEntLine) != 0);
        JSValue testEntCircle = evalJs(ctx, "f.filter({ ty: 'AcDbCircle' });");
        assert(JS_ToBool(ctx, testEntCircle) == 0);

        // Test counter increment in closure
        JSValue setRes1 = evalJs(ctx, "f.set({ layer: 'Old' });");
        assert(getPropNumber(ctx, setRes1, "count") == 1.0);
        JSValue setRes2 = evalJs(ctx, "f.set({ layer: 'Old' });");
        assert(getPropNumber(ctx, setRes2, "count") == 2.0);

        JS_FreeValue(ctx, testEntLine);
        JS_FreeValue(ctx, testEntCircle);
        JS_FreeValue(ctx, setRes1);
        JS_FreeValue(ctx, setRes2);
        JS_FreeValue(ctx, filterFn);
        JS_FreeValue(ctx, setFn);
        JS_FreeValue(ctx, res);
        JS_FreeContext(ctx);

        std::cout << "  Multiline query and block statement IIFE verified successfully." << std::endl;
    }

    // 26. Debug count: += 1 alongside nested group
    {
        std::cout << "\n[Test 26] Debug user query with sibling count and nested group..." << std::endl;
        const std::string q = "{e.code: {e.a: {a: +=e.h*e.l/1e6}, count: += 1}}";
        assert(query_dsl::isDslQuery(q));
        std::string js = query_dsl::compileToJsCode(q, "entries");

        JSContext *ctx = JS_NewContext(rt);
        evalJs(ctx, "entries = ["
                    "  { code: 'CT1', a: 160000, h: 2, l: 3000 },"
                    "  { code: 'CT1', a: 160000, h: 2, l: 4000 },"
                    "  { code: 'CT2', a: 160000, h: 4, l: 5000 }"
                    "];");
        evalJs(ctx, js);
        std::string jsonRes = getJson(ctx, evalJs(ctx, "result"));
        std::cout << "Result JSON: " << jsonRes << std::endl;

        // CT1 should have count: 2, CT2 should have count: 1
        JSValue ct1 = evalJs(ctx, "result.CT1");
        assert(getPropNumber(ctx, ct1, "count") == 2.0);
        JSValue ct2 = evalJs(ctx, "result.CT2");
        assert(getPropNumber(ctx, ct2, "count") == 1.0);

        JS_FreeValue(ctx, ct1);
        JS_FreeValue(ctx, ct2);
        JS_FreeContext(ctx);

        std::cout << "  Sibling count and nested group verified successfully." << std::endl;
    }

    // 27. Test query with e.id: e.a
    {
        std::cout << "\n[Test 27] Test query with e.id: e.a..." << std::endl;
        const std::string q2 = "{e.code: {e.a: {a: +=e.h*e.l/1e6, e.id: e.a}, count: += 1}}";
        query_dsl::DslParseResult res2 = query_dsl::parse(q2);
        std::cout << "success: " << res2.success << ", error: " << res2.errorMessage << std::endl;
        assert(res2.success);

        std::string js2 = query_dsl::compileToJsCode(q2, "entries");
        std::cout << "Generated JS:\n" << js2 << std::endl;

        JSContext *ctx = JS_NewContext(rt);
        evalJs(ctx, "entries = ["
                    "  { code: 'CT1', a: 160000, h: 2, l: 3000, id: '2F1A' },"
                    "  { code: 'CT1', a: 160000, h: 2, l: 4000, id: '2F1B' },"
                    "  { code: 'CT2', a: 160000, h: 4, l: 5000, id: '2F1C' }"
                    "];");
        evalJs(ctx, js2);
        std::string jsonRes2 = getJson(ctx, evalJs(ctx, "result"));
        std::cout << "Result JSON for q2: " << jsonRes2 << std::endl;

        // Verify CT1.160000 has a, 2F1A, 2F1B
        JSValue ct1Group = evalJs(ctx, "result.CT1['160000']");
        assert(getPropNumber(ctx, ct1Group, "a") == 0.014);
        assert(getPropNumber(ctx, ct1Group, "2F1A") == 160000.0);
        assert(getPropNumber(ctx, ct1Group, "2F1B") == 160000.0);

        JS_FreeValue(ctx, ct1Group);
        JS_FreeContext(ctx);

        std::cout << "  Dynamic property key e.id: e.a verified successfully!" << std::endl;
    }

    // 28. Test recursive block reference expansion in SS-QUERY
    {
        std::cout << "\n[Test 28] Test recursive block reference expansion in SS-QUERY..." << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        // Setup mock entity.getBlockEntities
        const std::string mockScript =
            "globalThis.entity = {\n"
            "  getBlockEntities: function(hand) {\n"
            "    if (hand === 'BLK1') {\n"
            "      return [\n"
            "        { id: 201, hand: 'LINE1', ty: 'AcDbLine', a: 160000, l: 3000, data: { h: 2, l: 3000 } },\n"
            "        { id: 202, hand: 'SUBBLK', ty: 'AcDbBlockReference', data: { block: true, subCode: 'S1' } }\n"
            "      ];\n"
            "    }\n"
            "    if (hand === 'SUBBLK') {\n"
            "      return [\n"
            "        { id: 301, hand: 'LINE2', ty: 'AcDbLine', a: 160000, l: 4000, data: { h: 2, l: 4000 } }\n"
            "      ];\n"
            "    }\n"
            "    return [];\n"
            "  }\n"
            "};\n";
        evalJs(ctx, mockScript);

        const std::string expandEntitiesFn =
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
        evalJs(ctx, expandEntitiesFn);

        // Input: 1 regular line + 1 block reference with data.block === true
        const std::string testInput =
            "const raw = [\n"
            "  { id: 100, hand: 'PLAIN_LINE', ty: 'AcDbLine', a: 50000, l: 1000, data: { code: 'CT0' } },\n"
            "  { id: 101, hand: 'BLK1', ty: 'AcDbBlockReference', data: { block: true, code: 'CT1', project: 'P1' } }\n"
            "];\n"
            "globalThis.expanded = expandEntities(raw);\n";
        evalJs(ctx, testInput);

        // Verify expanded length: PLAIN_LINE (1) + LINE1 (from BLK1) + LINE2 (from SUBBLK) = 3
        JSValue expLenVal = evalJs(ctx, "expanded.length");
        int expLen = 0;
        JS_ToInt32(ctx, &expLen, expLenVal);
        assert(expLen == 3);

        // Verify PLAIN_LINE: ids should be empty
        JSValue ids0Len = evalJs(ctx, "expanded[0].ids.length");
        int len0 = -1;
        JS_ToInt32(ctx, &len0, ids0Len);
        assert(len0 == 0);

        // Verify LINE1: ids should be [101], code should be 'CT1'
        std::string jsonLine1 = getJson(ctx, evalJs(ctx, "expanded[1]"));
        std::cout << "Expanded LINE1: " << jsonLine1 << std::endl;
        JSValue ids1 = evalJs(ctx, "expanded[1].ids[0]");
        int id1 = 0;
        JS_ToInt32(ctx, &id1, ids1);
        assert(id1 == 101);
        assert(getPropString(ctx, evalJs(ctx, "expanded[1]"), "code") == "CT1");
        assert(getPropString(ctx, evalJs(ctx, "expanded[1]"), "project") == "P1");

        // Verify LINE2 (grandchild): ids should be [101, 202]
        std::string jsonLine2 = getJson(ctx, evalJs(ctx, "expanded[2]"));
        std::cout << "Expanded LINE2: " << jsonLine2 << std::endl;
        JSValue ids2_0 = evalJs(ctx, "expanded[2].ids[0]");
        JSValue ids2_1 = evalJs(ctx, "expanded[2].ids[1]");
        int id2_0 = 0, id2_1 = 0;
        JS_ToInt32(ctx, &id2_0, ids2_0);
        JS_ToInt32(ctx, &id2_1, ids2_1);
        assert(id2_0 == 101);
        assert(id2_1 == 202);
        assert(getPropString(ctx, evalJs(ctx, "expanded[2]"), "code") == "CT1");
        assert(getPropString(ctx, evalJs(ctx, "expanded[2]"), "subCode") == "S1");

        JS_FreeValue(ctx, expLenVal);
        JS_FreeValue(ctx, ids0Len);
        JS_FreeValue(ctx, ids1);
        JS_FreeValue(ctx, ids2_0);
        JS_FreeValue(ctx, ids2_1);
        JS_FreeContext(ctx);

        std::cout << "  Recursive block reference expansion verified successfully!" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 29: Direct scalar aggregation on group keys {e.code: += 1}, {e.code: +=e.a}
    // -------------------------------------------------------------
    {
        std::cout << "\n[Test 29] Direct scalar aggregations on group keys..." << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        evalJs(ctx, "entries = [\n"
                    "  { code: 'CT1', a: 10, floor: '1F' },\n"
                    "  { code: 'CT1', a: 20, floor: '1F' },\n"
                    "  { code: 'CT2', a: 50, floor: '2F' },\n"
                    "  { code: 'CT1', a: 30, floor: '2F' }\n"
                    "];");

        // 1. {e.code: += 1}
        const std::string q1 = "{e.code: += 1}";
        assert(query_dsl::isDslQuery(q1));
        std::string jsCode1 = "(() => {\n" + query_dsl::compileToJsCode(q1, "entries") + "\nglobalThis.result = result;\n})();";
        std::cout << "Generated JS for {e.code: += 1}:\n" << jsCode1 << std::endl;
        evalJs(ctx, jsCode1);
        std::string jsonRes1 = getJson(ctx, evalJs(ctx, "result"));
        std::cout << "Result JSON for {e.code: += 1}: " << jsonRes1 << std::endl;
        assert(getPropNumber(ctx, evalJs(ctx, "result"), "CT1") == 3.0);
        assert(getPropNumber(ctx, evalJs(ctx, "result"), "CT2") == 1.0);

        // 2. {e.code: +=e.a}
        const std::string q2 = "{e.code: +=e.a}";
        assert(query_dsl::isDslQuery(q2));
        std::string jsCode2 = "(() => {\n" + query_dsl::compileToJsCode(q2, "entries") + "\nglobalThis.result = result;\n})();";
        std::cout << "Generated JS for {e.code: +=e.a}:\n" << jsCode2 << std::endl;
        evalJs(ctx, jsCode2);
        std::string jsonRes2 = getJson(ctx, evalJs(ctx, "result"));
        std::cout << "Result JSON for {e.code: +=e.a}: " << jsonRes2 << std::endl;
        assert(getPropNumber(ctx, evalJs(ctx, "result"), "CT1") == 60.0);
        assert(getPropNumber(ctx, evalJs(ctx, "result"), "CT2") == 50.0);

        // 3. {e.code: ++}
        const std::string q3 = "{e.code: ++}";
        assert(query_dsl::isDslQuery(q3));
        std::string jsCode3 = "(() => {\n" + query_dsl::compileToJsCode(q3, "entries") + "\nglobalThis.result = result;\n})();";
        evalJs(ctx, jsCode3);
        assert(getPropNumber(ctx, evalJs(ctx, "result"), "CT1") == 3.0);
        assert(getPropNumber(ctx, evalJs(ctx, "result"), "CT2") == 1.0);

        // 4. Nested: {e.floor: {e.code: += 1}}
        const std::string q4 = "{e.floor: {e.code: += 1}}";
        assert(query_dsl::isDslQuery(q4));
        std::string jsCode4 = "(() => {\n" + query_dsl::compileToJsCode(q4, "entries") + "\nglobalThis.result = result;\n})();";
        std::cout << "Generated JS for {e.floor: {e.code: += 1}}:\n" << jsCode4 << std::endl;
        evalJs(ctx, jsCode4);
        std::string jsonRes4 = getJson(ctx, evalJs(ctx, "result"));
        std::cout << "Result JSON for {e.floor: {e.code: += 1}}: " << jsonRes4 << std::endl;
        JSValue floor1 = JS_GetPropertyStr(ctx, evalJs(ctx, "result"), "1F");
        assert(getPropNumber(ctx, floor1, "CT1") == 2.0);
        JS_FreeValue(ctx, floor1);
        JSValue floor2 = JS_GetPropertyStr(ctx, evalJs(ctx, "result"), "2F");
        assert(getPropNumber(ctx, floor2, "CT1") == 1.0);
        assert(getPropNumber(ctx, floor2, "CT2") == 1.0);
        JS_FreeValue(ctx, floor2);

        JS_FreeContext(ctx);
        std::cout << "  Direct scalar aggregation tests verified successfully!" << std::endl;
    }

    // [Test 30] Template placeholder detection & binding (_0, _1, ...)
    {
        std::cout << "\n[Test 30] Template query placeholders (_0, _1, ...)..." << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        std::string templateQuery = "{data: {floor: 'Lt' + _0}, clr: _1}";
        std::string wrappedCode = "(() => {\n"
                                  "  const _0 = 1;\n"
                                  "  const _1 = 2;\n"
                                  "  return " + templateQuery + ";\n"
                                  "})()";

        JSValue res = evalJs(ctx, wrappedCode);
        assert(JS_IsObject(res));
        assert(getPropNumber(ctx, res, "clr") == 2.0);

        JSValue dataVal = JS_GetPropertyStr(ctx, res, "data");
        assert(JS_IsObject(dataVal));
        assert(getPropString(ctx, dataVal, "floor") == "Lt1");
        JS_FreeValue(ctx, dataVal);
        JS_FreeValue(ctx, res);

        // String value for _0
        std::string wrappedCode2 = "(() => {\n"
                                   "  const _0 = \"B1\";\n"
                                   "  const _1 = 5;\n"
                                   "  return " + templateQuery + ";\n"
                                   "})()";
        JSValue res2 = evalJs(ctx, wrappedCode2);
        assert(JS_IsObject(res2));
        assert(getPropNumber(ctx, res2, "clr") == 5.0);

        JSValue dataVal2 = JS_GetPropertyStr(ctx, res2, "data");
        assert(JS_IsObject(dataVal2));
        assert(getPropString(ctx, dataVal2, "floor") == "LtB1");
        JS_FreeValue(ctx, dataVal2);
        JS_FreeValue(ctx, res2);

        JS_FreeContext(ctx);
        std::cout << "  Template query placeholders verified successfully!" << std::endl;
    }

    // [Test 31] Universal color parsing (HSL, HSV, CMYK, HCT, RGB, Hex, ACI)
    {
        std::cout << "\n[Test 31] Universal color parsing (HSL, HSV, CMYK, HCT, RGB, Hex, ACI)..." << std::endl;
        JSContext *ctx = JS_NewContext(rt);

        // 1. HSL object: { h: 200, s: 70, l: 50 } (User's exact query)
        {
            JSValue val = evalJs(ctx, "({ h: 200, s: 70, l: 50 })");
            color_utils::ParsedColor pc;
            std::string err;
            assert(color_utils::parseColorFromJs(ctx, val, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Rgb);
            assert(pc.r == 38 && pc.g == 157 && pc.b == 217);
            JS_FreeValue(ctx, val);
        }

        // 2. HSV object: { h: 200, s: 70, v: 50 }
        {
            JSValue val = evalJs(ctx, "({ h: 200, s: 70, v: 50 })");
            color_utils::ParsedColor pc;
            std::string err;
            assert(color_utils::parseColorFromJs(ctx, val, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Rgb);
            assert(pc.r == 38 && pc.g == 97 && pc.b == 128);
            JS_FreeValue(ctx, val);
        }

        // 3. CMYK object: { c: 0, m: 50, y: 100, k: 0 }
        {
            JSValue val = evalJs(ctx, "({ c: 0, m: 50, y: 100, k: 0 })");
            color_utils::ParsedColor pc;
            std::string err;
            assert(color_utils::parseColorFromJs(ctx, val, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Rgb);
            assert(pc.r == 255 && pc.g == 128 && pc.b == 0);
            JS_FreeValue(ctx, val);
        }

        // 4. HCT object: { h: 200, c: 70, t: 50 }
        {
            JSValue val = evalJs(ctx, "({ h: 200, c: 70, t: 50 })");
            color_utils::ParsedColor pc;
            std::string err;
            assert(color_utils::parseColorFromJs(ctx, val, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Rgb);
            // HCT with hue 200, chroma 70, tone 50
            assert(pc.b > pc.r);
            JS_FreeValue(ctx, val);
        }

        // 5. Functional and hex strings
        {
            JSValue sHsl = evalJs(ctx, "'hsl(200, 70%, 50%)'");
            color_utils::ParsedColor pc;
            std::string err;
            assert(color_utils::parseColorFromJs(ctx, sHsl, pc, err));
            assert(pc.r == 38 && pc.g == 157 && pc.b == 217);
            JS_FreeValue(ctx, sHsl);

            JSValue sHex = evalJs(ctx, "'#ff007f'");
            assert(color_utils::parseColorFromJs(ctx, sHex, pc, err));
            assert(pc.r == 255 && pc.g == 0 && pc.b == 127);
            JS_FreeValue(ctx, sHex);

            JSValue sName = evalJs(ctx, "'red'");
            assert(color_utils::parseColorFromJs(ctx, sName, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Aci && pc.aci == 1);
            JS_FreeValue(ctx, sName);
        }

        // 6. ACI number & array
        {
            JSValue n = evalJs(ctx, "5");
            color_utils::ParsedColor pc;
            std::string err;
            assert(color_utils::parseColorFromJs(ctx, n, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Aci && pc.aci == 5);
            JS_FreeValue(ctx, n);

            JSValue arr = evalJs(ctx, "[10, 20, 30]");
            assert(color_utils::parseColorFromJs(ctx, arr, pc, err));
            assert(pc.kind == color_utils::ParsedColor::Kind::Rgb);
            assert(pc.r == 10 && pc.g == 20 && pc.b == 30);
            JS_FreeValue(ctx, arr);
        }

        JS_FreeContext(ctx);
        std::cout << "  Universal color parsing (HSL, HSV, CMYK, HCT, RGB, Hex, ACI) verified successfully!" << std::endl;
    }

    JS_FreeRuntime(rt);

    std::cout << "\n=============================================" << std::endl;
    std::cout << " ALL QUICKJS COMPREHENSIVE TESTS PASSED!      " << std::endl;
    std::cout << "=============================================" << std::endl;

    return 0;
}

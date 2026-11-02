#pragma once

#include <string>
#include <vector>
#include <memory>

namespace query_dsl
{
    struct DslOptions
    {
        bool implied = true;
    };

    struct AggregationField
    {
        std::string targetField; // e.g. "a", "l", "__proto__", "total area"
        std::string op;          // "+=" (or extended: "-=", "*=", "/=", "%=", "**=", "push", etc.)
        std::string sourceExpr;  // e.g. "e.a", "(e.a ?? 0) * (e.quantity || 1)", "Math.abs(e.a)"
        std::string initValue;   // "0", "1", "null", "[]", etc.
    };

    enum class DslNodeType
    {
        Group,
        Selection,
        ArraySelection,
        ScalarSelection
    };

    struct DslNode
    {
        DslNodeType type = DslNodeType::Group;
        std::string keyExpr;       // For Group: e.g. "e.building"
        std::string subtotalField; // For Selection: e.g. "total", or "" for direct selection
        std::vector<AggregationField> aggregations;
        std::vector<std::shared_ptr<DslNode>> children;
    };

    struct DslParseResult
    {
        bool success = false;
        std::string errorMessage;
        DslOptions options;
        std::string whereExpr;                 // Optional filter condition (e.g. "e.a > 0" or "e.code != null")
        std::shared_ptr<DslNode> rootNode;
        std::vector<std::string> groupingKeys; // Backwards-compatible flattened grouping keys
        std::vector<AggregationField> aggregations; // Backwards-compatible flattened aggregations
    };

    // Detects whether an input string conforms to the SS-QUERY DSL grammar
    bool isDslQuery(const std::string &input);

    // Parses a DSL query string into options, grouping hierarchy, and aggregation fields
    DslParseResult parse(const std::string &dslQuery);

    // Compiles DSL query into JavaScript reduce code block supporting arbitrary nesting and subtotals
    std::string compileToJsCode(const std::string &dslQuery, const std::string &entriesVar = "entries");

    // Compiles DSL query into an arrow function: (entries) => { ... return result; }
    std::string compileToJsFunction(const std::string &dslQuery, const std::string &entriesVar = "entries");

    // Compiles DSL query into an options object for editor.ssQuery:
    // ({ implied: <bool>, query: (entries) => { ... } })
    std::string compileToSsQueryOptions(const std::string &dslQuery);
}

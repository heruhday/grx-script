#include "query_dsl.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <unordered_set>

namespace query_dsl
{
    namespace
    {
        std::string trim(const std::string &str)
        {
            size_t first = str.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            size_t last = str.find_last_not_of(" \t\r\n");
            return str.substr(first, last - first + 1);
        }

        std::string quoteJsString(const std::string &str)
        {
            std::string out = "\"";
            for (char c : str)
            {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else out += c;
            }
            out += "\"";
            return out;
        }

        std::string rewriteEntityExpression(const std::string &expr)
        {
            std::string out;
            size_t i = 0;
            const size_t n = expr.length();

            static const std::unordered_set<std::string> kReserved = {
                "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete",
                "do", "else", "export", "extends", "finally", "for", "function", "if", "import",
                "in", "instanceof", "new", "return", "super", "switch", "this", "throw", "try",
                "typeof", "var", "void", "while", "with", "yield", "let", "static", "enum",
                "await", "async", "true", "false", "null", "undefined", "NaN", "Infinity",
                "Math", "Number", "String", "Boolean", "Array", "Object", "JSON", "RegExp", "Date",
                "parseInt", "parseFloat", "isFinite", "isNaN", "console", "toNumber", "acc", "e"
            };

            while (i < n)
            {
                char c = expr[i];

                // Skip string literals
                if (c == '"' || c == '\'' || c == '`')
                {
                    char quote = c;
                    out += quote;
                    i++;
                    while (i < n)
                    {
                        char sc = expr[i++];
                        out += sc;
                        if (sc == '\\' && i < n)
                        {
                            out += expr[i++];
                        }
                        else if (sc == quote)
                        {
                            break;
                        }
                    }
                    continue;
                }

                // Skip numeric literals (including scientific notation like 1e6, 0.5)
                if (std::isdigit(static_cast<unsigned char>(c)))
                {
                    while (i < n && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '.'))
                    {
                        out += expr[i++];
                    }
                    continue;
                }

                // Check if start of an identifier (letter, _, $)
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$')
                {
                    // Look back to see if preceded by '.' (ignoring spaces)
                    bool precededByDot = false;
                    if (!out.empty())
                    {
                        size_t p = out.find_last_not_of(" \t\r\n");
                        if (p != std::string::npos && out[p] == '.')
                        {
                            precededByDot = true;
                        }
                    }

                    size_t start = i;
                    while (i < n && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_' || expr[i] == '$'))
                    {
                        i++;
                    }
                    std::string id = expr.substr(start, i - start);

                    // Look ahead to see if followed by '(' (function call)
                    bool followedByParen = false;
                    size_t a = expr.find_first_not_of(" \t\r\n", i);
                    if (a != std::string::npos && expr[a] == '(')
                    {
                        followedByParen = true;
                    }

                    if (precededByDot || followedByParen || kReserved.find(id) != kReserved.end())
                    {
                        out += id;
                    }
                    else
                    {
                        if (id == "area") out += "e.a";
                        else if (id == "len" || id == "length") out += "e.l";
                        else if (id == "layer") out += "e.lyr";
                        else if (id == "color") out += "e.clr";
                        else if (id == "type") out += "e.ty";
                        else if (id == "handle") out += "e.hand";
                        else out += "e." + id;
                    }
                    continue;
                }

                out += c;
                i++;
            }

            return out;
        }

        std::string sanitizeVarName(const std::string &keyExpr)
        {
            std::string name = keyExpr;
            size_t lastDot = name.rfind('.');
            if (lastDot != std::string::npos)
            {
                name = name.substr(lastDot + 1);
            }
            std::string clean;
            for (char ch : name)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
                {
                    clean += ch;
                }
                else
                {
                    clean += '_';
                }
            }
            if (clean.empty() || std::isdigit(static_cast<unsigned char>(clean[0])))
            {
                clean = "item_" + clean;
            }
            return clean;
        }

        bool hasEntityReference(const std::string &s)
        {
            if (s == "e") return true;
            if (s.find("e.") != std::string::npos) return true;
            if (s.find("e[") != std::string::npos) return true;
            if (s.find("e?") != std::string::npos) return true;
            if (s.find("(e") != std::string::npos) return true;
            return false;
        }

        std::string formatAccessor(const std::string &expr)
        {
            std::string s = trim(expr);
            if (s.empty()) return "\"\"";
            if (s.front() == '[' && s.back() == ']')
            {
                s = trim(s.substr(1, s.length() - 2));
            }
            if (!hasEntityReference(s))
            {
                if (s.front() != '"' && s.front() != '\'')
                {
                    return quoteJsString(s);
                }
            }
            return s;
        }

        // Ordered by descending length
        const char *const kExtendedOperators[] = {
            ">>>=", ">>=", "<<=", "**=",
            "&&=", "||=", "??=",
            "+=", "-=", "*=", "/=", "%=",
            "&=", "|=", "^=", "=",
            "++", "--",
            "+"
        };

        void emitAggOperation(const std::string &targetExpr, const AggregationField &agg, std::ostringstream &ss)
        {
            if (agg.op == "+=")
            {
                if (agg.sourceExpr == "1" || agg.sourceExpr.empty())
                {
                    ss << "  " << targetExpr << " += 1;\n";
                }
                else
                {
                    ss << "  " << targetExpr << " += toNumber(" << agg.sourceExpr << ");\n";
                }
            }
            else if (agg.op == "-=")
            {
                if (agg.sourceExpr == "1" || agg.sourceExpr.empty())
                {
                    ss << "  " << targetExpr << " -= 1;\n";
                }
                else
                {
                    ss << "  " << targetExpr << " -= toNumber(" << agg.sourceExpr << ");\n";
                }
            }
            else if (agg.op == "*=" || agg.op == "/=" || agg.op == "**=")
            {
                ss << "  " << targetExpr << " " << agg.op << " toNumber(" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "%=" || agg.op == "<<=" || agg.op == ">>=" ||
                     agg.op == ">>>=" || agg.op == "|=" || agg.op == "^=")
            {
                ss << "  " << targetExpr << " " << agg.op << " toNumber(" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "&=")
            {
                ss << "  " << targetExpr << " &= toNumber(" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "&&=")
            {
                ss << "  " << targetExpr << " &&= Boolean(" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "||=" || agg.op == "??=")
            {
                ss << "  " << targetExpr << " " << agg.op << " (" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "++")
            {
                ss << "  " << targetExpr << " += 1;\n";
            }
            else if (agg.op == "--")
            {
                ss << "  " << targetExpr << " -= 1;\n";
            }
            else if (agg.op == "push")
            {
                ss << "  " << targetExpr << ".push(" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "min")
            {
                ss << "  " << targetExpr << " = Math.min(" << targetExpr << ", toNumber(" << agg.sourceExpr << "));\n";
            }
            else if (agg.op == "max")
            {
                ss << "  " << targetExpr << " = Math.max(" << targetExpr << ", toNumber(" << agg.sourceExpr << "));\n";
            }
            else
            {
                ss << "  " << targetExpr << " = (" << agg.sourceExpr << ");\n";
            }
        }

        void emitAggLine(const std::string &targetVar, const AggregationField &agg, std::ostringstream &ss)
        {
            bool isNumericIndex = !agg.targetField.empty() &&
                std::all_of(agg.targetField.begin(), agg.targetField.end(), [](unsigned char c) { return std::isdigit(c); });
            const std::string qField = isNumericIndex ? ("[" + agg.targetField + "]") : ("[" + formatAccessor(agg.targetField) + "]");

            if (hasEntityReference(agg.targetField) && agg.op != "=")
            {
                ss << "  (" << targetVar << qField << " ??= " << agg.initValue << ");\n";
            }

            emitAggOperation(targetVar + qField, agg, ss);
        }

        void emitNode(const std::shared_ptr<DslNode> &node,
                      const std::string &parentVar,
                      int &varCounter,
                      std::ostringstream &ss)
        {
            if (!node || node->type != DslNodeType::Group) return;

            std::string varName = "g" + std::to_string(varCounter++);
            const std::string keyAccessor = formatAccessor(node->keyExpr);

            bool isLeafWithDirectScalarOnly = (node->children.size() == 1 &&
                                               node->children[0]->type == DslNodeType::ScalarSelection);

            if (isLeafWithDirectScalarOnly)
            {
                const auto &agg = node->children[0]->aggregations[0];
                const std::string targetExpr = parentVar + "[" + keyAccessor + "]";
                ss << "  (" << targetExpr << " ??= " << agg.initValue << ");\n";
                emitAggOperation(targetExpr, agg, ss);
                return;
            }

            bool isLeafWithDirectSelectionOnly = (node->children.size() == 1 &&
                                                  node->children[0]->type == DslNodeType::Selection &&
                                                  node->children[0]->subtotalField.empty());

            if (isLeafWithDirectSelectionOnly)
            {
                const auto &sel = node->children[0];
                std::vector<AggregationField> staticAggs;
                for (const auto &agg : sel->aggregations)
                {
                    if (!hasEntityReference(agg.targetField))
                    {
                        staticAggs.push_back(agg);
                    }
                }

                std::string init = "Object.create(null)";
                if (!staticAggs.empty())
                {
                    init = "Object.assign(Object.create(null), { ";
                    for (size_t i = 0; i < staticAggs.size(); ++i)
                    {
                        if (i > 0) init += ", ";
                        init += quoteJsString(staticAggs[i].targetField) + ": " + staticAggs[i].initValue;
                    }
                    init += " })";
                }

                ss << "  const " << varName << " = (" << parentVar << "[" << keyAccessor << "] ??= " << init << ");\n";
                for (const auto &agg : sel->aggregations)
                {
                    emitAggLine(varName, agg, ss);
                }
                return;
            }

            bool isLeafWithDirectArraySelectionOnly = (node->children.size() == 1 &&
                                                       node->children[0]->type == DslNodeType::ArraySelection &&
                                                       node->children[0]->subtotalField.empty());

            if (isLeafWithDirectArraySelectionOnly)
            {
                const auto &sel = node->children[0];
                std::string init = "[";
                for (size_t i = 0; i < sel->aggregations.size(); ++i)
                {
                    if (i > 0) init += ", ";
                    init += sel->aggregations[i].initValue;
                }
                init += "]";

                ss << "  const " << varName << " = (" << parentVar << "[" << keyAccessor << "] ??= " << init << ");\n";
                for (const auto &agg : sel->aggregations)
                {
                    emitAggLine(varName, agg, ss);
                }
                return;
            }

            std::vector<AggregationField> directAggs;
            for (const auto &child : node->children)
            {
                if (child->type == DslNodeType::Selection && child->subtotalField.empty())
                {
                    for (const auto &agg : child->aggregations)
                    {
                        directAggs.push_back(agg);
                    }
                }
            }

            std::string init = "Object.create(null)";
            if (!directAggs.empty())
            {
                init = "Object.assign(Object.create(null), { ";
                for (size_t i = 0; i < directAggs.size(); ++i)
                {
                    if (i > 0) init += ", ";
                    init += quoteJsString(directAggs[i].targetField) + ": " + directAggs[i].initValue;
                }
                init += " })";
            }

            ss << "  const " << varName << " = (" << parentVar << "[" << keyAccessor << "] ??= " << init << ");\n";

            for (const auto &child : node->children)
            {
                if (child->type == DslNodeType::Selection)
                {
                    if (!child->subtotalField.empty())
                    {
                        std::string subVar = varName + "_" + sanitizeVarName(child->subtotalField);
                        std::vector<AggregationField> staticSubAggs;
                        for (const auto &agg : child->aggregations)
                        {
                            if (!hasEntityReference(agg.targetField))
                            {
                                staticSubAggs.push_back(agg);
                            }
                        }

                        std::string init = "Object.create(null)";
                        if (!staticSubAggs.empty())
                        {
                            init = "Object.assign(Object.create(null), { ";
                            for (size_t i = 0; i < staticSubAggs.size(); ++i)
                            {
                                if (i > 0) init += ", ";
                                init += quoteJsString(staticSubAggs[i].targetField) + ": " + staticSubAggs[i].initValue;
                            }
                            init += " })";
                        }

                        ss << "  const " << subVar << " = (" << varName << "[" << quoteJsString(child->subtotalField) << "] ??= " << init << ");\n";
                        for (const auto &agg : child->aggregations)
                        {
                            emitAggLine(subVar, agg, ss);
                        }
                    }
                    else
                    {
                        for (const auto &agg : child->aggregations)
                        {
                            emitAggLine(varName, agg, ss);
                        }
                    }
                }
                else if (child->type == DslNodeType::ArraySelection)
                {
                    if (!child->subtotalField.empty())
                    {
                        std::string subVar = varName + "_" + sanitizeVarName(child->subtotalField);
                        std::string init = "[";
                        for (size_t i = 0; i < child->aggregations.size(); ++i)
                        {
                            if (i > 0) init += ", ";
                            init += child->aggregations[i].initValue;
                        }
                        init += "]";

                        ss << "  const " << subVar << " = (" << varName << "[" << quoteJsString(child->subtotalField) << "] ??= " << init << ");\n";
                        for (const auto &agg : child->aggregations)
                        {
                            emitAggLine(subVar, agg, ss);
                        }
                    }
                    else
                    {
                        for (const auto &agg : child->aggregations)
                        {
                            emitAggLine(varName, agg, ss);
                        }
                    }
                }
                else if (child->type == DslNodeType::ScalarSelection)
                {
                    if (!child->subtotalField.empty())
                    {
                        const auto &agg = child->aggregations[0];
                        const std::string targetExpr = varName + "[" + quoteJsString(child->subtotalField) + "]";
                        ss << "  (" << targetExpr << " ??= " << agg.initValue << ");\n";
                        emitAggOperation(targetExpr, agg, ss);
                    }
                    else
                    {
                        const auto &agg = child->aggregations[0];
                        ss << "  (" << varName << " ??= " << agg.initValue << ");\n";
                        emitAggOperation(varName, agg, ss);
                    }
                }
                else if (child->type == DslNodeType::Group)
                {
                    emitNode(child, varName, varCounter, ss);
                }
            }
        }

        void populateBackwardsCompatFields(const std::shared_ptr<DslNode> &node,
                                           std::vector<std::string> &keys,
                                           std::vector<AggregationField> &aggs)
        {
            if (!node) return;
            if (node->type == DslNodeType::Group)
            {
                if (!node->keyExpr.empty())
                {
                    keys.push_back(node->keyExpr);
                }
                for (const auto &child : node->children)
                {
                    populateBackwardsCompatFields(child, keys, aggs);
                }
            }
            else if (node->type == DslNodeType::Selection || node->type == DslNodeType::ArraySelection || node->type == DslNodeType::ScalarSelection)
            {
                for (const auto &agg : node->aggregations)
                {
                    aggs.push_back(agg);
                }
            }
        }

        class Parser
        {
        public:
            explicit Parser(const std::string &text) : m_text(text), m_pos(0) {}

            DslParseResult parse()
            {
                DslParseResult res;
                try
                {
                    skipWhitespaceAndComments();
                    parseHeader(res.options, res.whereExpr);

                    skipWhitespaceAndComments();
                    // Check for leading "where <expr>:"
                    if (matchWord("where"))
                    {
                        skipWhitespaceAndComments();
                        res.whereExpr = rewriteEntityExpression(trim(readJsExpression(":")));
                        consume(':');
                        skipWhitespaceAndComments();
                    }

                    if (peek() == '[')
                    {
                        res.rootNode = parseArraySelectionBlock("");
                    }
                    else if (peek() == '{')
                    {
                        bool isFlatObjectSelection = false;
                        size_t savedPos = m_pos;
                        try
                        {
                            consume('{');
                            skipWhitespaceAndComments();

                            std::string firstKey = readJsExpression(":");
                            consume(':');
                            skipWhitespaceAndComments();

                            if (firstKey == "where")
                            {
                                readJsExpression(",");
                                consume(',');
                                skipWhitespaceAndComments();
                                firstKey = readJsExpression(":");
                                consume(':');
                                skipWhitespaceAndComments();
                            }

                            char afterColon = peek();
                            if (afterColon != '{' && afterColon != '[' && !hasEntityReference(firstKey))
                            {
                                isFlatObjectSelection = true;
                            }
                        }
                        catch (...)
                        {
                            isFlatObjectSelection = false;
                        }
                        m_pos = savedPos; // rewind to '{'

                        if (isFlatObjectSelection)
                        {
                            consume('{');
                            skipWhitespaceAndComments();

                            // Check if first entry is where: <expr>
                            size_t checkPos = m_pos;
                            std::string potentialKey = trim(readJsExpression(":"));
                            if (potentialKey == "where")
                            {
                                consume(':');
                                skipWhitespaceAndComments();
                                std::string w = rewriteEntityExpression(trim(readJsExpression(",")));
                                consume(',');
                                skipWhitespaceAndComments();
                                if (!res.whereExpr.empty())
                                {
                                    res.whereExpr = "(" + res.whereExpr + ") && (" + w + ")";
                                }
                                else
                                {
                                    res.whereExpr = w;
                                }
                            }
                            else
                            {
                                m_pos = checkPos; // rewind to field
                            }

                            auto sel = std::make_shared<DslNode>();
                            sel->type = DslNodeType::Selection;
                            sel->subtotalField = "";

                            while (peek() != '}' && peek() != '\0')
                            {
                                std::string fieldName = trim(readJsExpression(":"));
                                consume(':');
                                skipWhitespaceAndComments();

                                sel->aggregations.push_back(parseAggregationField(fieldName));

                                skipWhitespaceAndComments();
                                if (peek() == ',')
                                {
                                    consume(',');
                                }
                                else if (peek() == '}')
                                {
                                    break;
                                }
                            }
                            consume('}');
                            res.rootNode = sel;
                        }
                        else
                        {
                            consume('{');
                            skipWhitespaceAndComments();

                            std::string rootKey = readJsExpression(":");
                            consume(':');
                            skipWhitespaceAndComments();

                            // Check if root entry is a where block: {where: <expr>, ...}
                            if (rootKey == "where" && res.whereExpr.empty())
                            {
                                res.whereExpr = rewriteEntityExpression(trim(readJsExpression(",")));
                                consume(',');
                                skipWhitespaceAndComments();
                                rootKey = readJsExpression(":");
                                consume(':');
                                skipWhitespaceAndComments();
                            }

                            if (peek() == '[')
                            {
                                auto arrSel = parseArraySelectionBlock("");
                                auto rootGroup = std::make_shared<DslNode>();
                                rootGroup->type = DslNodeType::Group;
                                rootGroup->keyExpr = rootKey;
                                rootGroup->children.push_back(arrSel);
                                res.rootNode = rootGroup;
                            }
                            else if (peek() == '{' && isSelectionAhead() && !hasEntityReference(rootKey))
                            {
                                auto rootSel = parseSelectionBlock(rootKey);
                                auto rootGroup = std::make_shared<DslNode>();
                                rootGroup->type = DslNodeType::Group;
                                rootGroup->keyExpr = "e.code";
                                rootGroup->children.push_back(rootSel);
                                res.rootNode = rootGroup;
                            }
                            else if (peek() == '{')
                            {
                                res.rootNode = parseGroupBlock(rootKey);
                            }
                            else
                            {
                                // Direct scalar aggregation for group, e.g. {e.code: += 1} or {e.code: +=e.a}
                                AggregationField agg = parseAggregationField("");
                                auto scalarSel = std::make_shared<DslNode>();
                                scalarSel->type = DslNodeType::ScalarSelection;
                                scalarSel->aggregations.push_back(agg);

                                auto rootGroup = std::make_shared<DslNode>();
                                rootGroup->type = DslNodeType::Group;
                                rootGroup->keyExpr = rootKey;
                                rootGroup->children.push_back(scalarSel);
                                res.rootNode = rootGroup;
                            }

                            skipWhitespaceAndComments();
                            if (peek() == ',')
                            {
                                consume(',');
                            }
                            if (peek() == '}')
                            {
                                consume('}');
                            }
                        }
                    }
                    else
                    {
                        // Bare scalar or array aggregation(s), e.g. "+=a", "++", "+=a/1e6, +=l/1e3"
                        std::vector<AggregationField> aggList;
                        int idx = 0;
                        while (peek() != '\0' && !hasWord("where"))
                        {
                            std::string idxStr = std::to_string(idx++);
                            aggList.push_back(parseAggregationField(idxStr));

                            skipWhitespaceAndComments();
                            if (peek() == ',')
                            {
                                consume(',');
                                skipWhitespaceAndComments();
                            }
                            else
                            {
                                break;
                            }
                        }

                        if (aggList.empty())
                        {
                            res.errorMessage = "Expected '{', '[', or aggregation operator (e.g. +=a, ++)";
                            return res;
                        }

                        auto root = std::make_shared<DslNode>();
                        if (aggList.size() == 1)
                        {
                            root->type = DslNodeType::ScalarSelection;
                        }
                        else
                        {
                            root->type = DslNodeType::ArraySelection;
                        }
                        root->aggregations = aggList;
                        res.rootNode = root;
                    }

                    skipWhitespaceAndComments();
                    // Check for trailing "where <expr>"
                    if (matchWord("where"))
                    {
                        skipWhitespaceAndComments();
                        std::string trailingWhere = trim(readJsExpression(""));
                        if (!trailingWhere.empty())
                        {
                            trailingWhere = rewriteEntityExpression(trailingWhere);
                            if (!res.whereExpr.empty())
                            {
                                res.whereExpr = "(" + res.whereExpr + ") && (" + trailingWhere + ")";
                            }
                            else
                            {
                                res.whereExpr = trailingWhere;
                            }
                        }
                    }

                    skipWhitespaceAndComments();
                    if (m_pos < m_text.length())
                    {
                        res.errorMessage = "Unexpected trailing characters after query";
                        return res;
                    }

                    if (res.rootNode)
                    {
                        populateBackwardsCompatFields(res.rootNode, res.groupingKeys, res.aggregations);
                        res.success = !res.aggregations.empty() || !res.groupingKeys.empty();
                    }
                }
                catch (const std::exception &ex)
                {
                    res.success = false;
                    res.errorMessage = ex.what();
                }
                return res;
            }

        private:
            void skipWhitespaceAndComments()
            {
                while (m_pos < m_text.length())
                {
                    char c = m_text[m_pos];
                    if (std::isspace(static_cast<unsigned char>(c)))
                    {
                        m_pos++;
                    }
                    else if (c == '/' && m_pos + 1 < m_text.length() && m_text[m_pos + 1] == '/')
                    {
                        m_pos += 2;
                        while (m_pos < m_text.length() && m_text[m_pos] != '\n')
                        {
                            m_pos++;
                        }
                    }
                    else if (c == '/' && m_pos + 1 < m_text.length() && m_text[m_pos + 1] == '*')
                    {
                        m_pos += 2;
                        while (m_pos + 1 < m_text.length() && !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/'))
                        {
                            m_pos++;
                        }
                        if (m_pos + 1 < m_text.length())
                        {
                            m_pos += 2;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }

            char peek()
            {
                skipWhitespaceAndComments();
                return m_pos < m_text.length() ? m_text[m_pos] : '\0';
            }

            char consume(char expected = '\0')
            {
                skipWhitespaceAndComments();
                if (m_pos >= m_text.length())
                {
                    throw std::runtime_error("Unexpected end of input");
                }
                char c = m_text[m_pos++];
                if (expected != '\0' && c != expected)
                {
                    std::string err = "Expected '";
                    err += expected;
                    err += "', found '";
                    err += c;
                    err += "'";
                    throw std::runtime_error(err);
                }
                return c;
            }

            bool hasPrefix(const std::string &prefix)
            {
                skipWhitespaceAndComments();
                if (m_pos + prefix.length() <= m_text.length())
                {
                    return (m_text.compare(m_pos, prefix.length(), prefix) == 0);
                }
                return false;
            }

            bool matchPrefix(const std::string &prefix)
            {
                if (hasPrefix(prefix))
                {
                    m_pos += prefix.length();
                    return true;
                }
                return false;
            }

            bool hasWord(const std::string &word) const
            {
                size_t p = m_pos;
                while (p < m_text.length() && std::isspace(static_cast<unsigned char>(m_text[p])))
                {
                    p++;
                }
                if (p + word.length() <= m_text.length())
                {
                    if (m_text.compare(p, word.length(), word) == 0)
                    {
                        size_t nextPos = p + word.length();
                        if (nextPos == m_text.length() || (!std::isalnum(static_cast<unsigned char>(m_text[nextPos])) && m_text[nextPos] != '_'))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool matchWord(const std::string &word)
            {
                skipWhitespaceAndComments();
                if (m_pos + word.length() <= m_text.length())
                {
                    if (m_text.compare(m_pos, word.length(), word) == 0)
                    {
                        size_t nextPos = m_pos + word.length();
                        if (nextPos == m_text.length() || (!std::isalnum(static_cast<unsigned char>(m_text[nextPos])) && m_text[nextPos] != '_'))
                        {
                            m_pos = nextPos;
                            return true;
                        }
                    }
                }
                return false;
            }

            void parseHeader(DslOptions &opts, std::string &whereExpr)
            {
                skipWhitespaceAndComments();
                if (matchWord("Query"))
                {
                    skipWhitespaceAndComments();
                    if (peek() == '<')
                    {
                        consume('<');
                        skipWhitespaceAndComments();
                        bool hasParen = (peek() == '(');
                        if (hasParen) consume('(');
                        skipWhitespaceAndComments();
                        bool hasBrace = (peek() == '{');
                        if (hasBrace) consume('{');

                        while (peek() != '}' && peek() != ')' && peek() != '>' && peek() != '\0')
                        {
                            std::string optName = readFieldName();
                            consume(':');
                            skipWhitespaceAndComments();
                            if (optName == "implied")
                            {
                                if (matchWord("true")) opts.implied = true;
                                else if (matchWord("false")) opts.implied = false;
                                else throw std::runtime_error("Expected boolean for 'implied' option");
                            }
                            else if (optName == "where" || optName == "filter")
                            {
                                whereExpr = rewriteEntityExpression(trim(readJsExpression(",})>")));
                            }
                            else
                            {
                                readJsExpression(",})>");
                            }

                            skipWhitespaceAndComments();
                            if (peek() == ',') consume(',');
                            else break;
                        }

                        if (hasBrace) consume('}');
                        skipWhitespaceAndComments();
                        if (hasParen) consume(')');
                        skipWhitespaceAndComments();
                        consume('>');
                    }
                    consume(':');
                }
            }

            std::string readStringLiteral()
            {
                skipWhitespaceAndComments();
                char quote = m_text[m_pos++];
                std::string val;
                while (m_pos < m_text.length())
                {
                    char c = m_text[m_pos++];
                    if (c == '\\' && m_pos < m_text.length())
                    {
                        char next = m_text[m_pos++];
                        if (next == 'n') val += '\n';
                        else if (next == 't') val += '\t';
                        else if (next == 'r') val += '\r';
                        else val += next;
                    }
                    else if (c == quote)
                    {
                        break;
                    }
                    else
                    {
                        val += c;
                    }
                }
                return val;
            }

            std::string readFieldName()
            {
                skipWhitespaceAndComments();
                if (peek() == '"' || peek() == '\'')
                {
                    return readStringLiteral();
                }

                size_t start = m_pos;
                while (m_pos < m_text.length())
                {
                    char c = m_text[m_pos];
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$')
                    {
                        m_pos++;
                    }
                    else
                    {
                        break;
                    }
                }
                if (m_pos == start)
                {
                    throw std::runtime_error("Expected field identifier name");
                }
                return m_text.substr(start, m_pos - start);
            }

            std::string readJsExpression(const std::string &terminators)
            {
                skipWhitespaceAndComments();
                size_t start = m_pos;
                int depthParen = 0;
                int depthBracket = 0;
                int depthBrace = 0;
                int ternaryDepth = 0;

                while (m_pos < m_text.length())
                {
                    char c = m_text[m_pos];

                    // Comment
                    if (c == '/' && m_pos + 1 < m_text.length() && m_text[m_pos + 1] == '/')
                    {
                        m_pos += 2;
                        while (m_pos < m_text.length() && m_text[m_pos] != '\n') m_pos++;
                        continue;
                    }
                    if (c == '/' && m_pos + 1 < m_text.length() && m_text[m_pos + 1] == '*')
                    {
                        m_pos += 2;
                        while (m_pos + 1 < m_text.length() && !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/')) m_pos++;
                        if (m_pos + 1 < m_text.length()) m_pos += 2;
                        continue;
                    }

                    // Quotes
                    if (c == '"' || c == '\'')
                    {
                        char quote = c;
                        m_pos++;
                        while (m_pos < m_text.length())
                        {
                            char sc = m_text[m_pos++];
                            if (sc == '\\' && m_pos < m_text.length()) m_pos++;
                            else if (sc == quote) break;
                        }
                        continue;
                    }

                    // Template literals
                    if (c == '`')
                    {
                        m_pos++;
                        while (m_pos < m_text.length())
                        {
                            char tc = m_text[m_pos++];
                            if (tc == '\\' && m_pos < m_text.length()) m_pos++;
                            else if (tc == '$' && m_pos < m_text.length() && m_text[m_pos] == '{')
                            {
                                m_pos++;
                                int templateBrace = 1;
                                while (m_pos < m_text.length() && templateBrace > 0)
                                {
                                    char inner = m_text[m_pos++];
                                    if (inner == '{') templateBrace++;
                                    else if (inner == '}') templateBrace--;
                                }
                            }
                            else if (tc == '`') break;
                        }
                        continue;
                    }

                    // Nesting
                    if (c == '(') depthParen++;
                    else if (c == ')') { if (depthParen > 0) depthParen--; }
                    else if (c == '[') depthBracket++;
                    else if (c == ']')
                    {
                        if (depthBracket == 0 && terminators.find(']') != std::string::npos) break;
                        if (depthBracket > 0) depthBracket--;
                    }
                    else if (c == '{') depthBrace++;
                    else if (c == '}')
                    {
                        if (depthBrace == 0 && terminators.find('}') != std::string::npos) break;
                        if (depthBrace > 0) depthBrace--;
                    }
                    else if (depthParen == 0 && depthBracket == 0 && depthBrace == 0)
                    {
                        if (ternaryDepth == 0 && hasWord("where"))
                        {
                            break;
                        }
                        if (c == '?')
                        {
                            if (m_pos + 1 < m_text.length() && m_text[m_pos + 1] == '?')
                            {
                                m_pos += 2;
                                if (m_pos < m_text.length() && m_text[m_pos] == '=') m_pos++;
                                continue;
                            }
                            else if (m_pos + 1 < m_text.length() && m_text[m_pos + 1] == '.')
                            {
                                m_pos += 2;
                                continue;
                            }
                            else
                            {
                                ternaryDepth++;
                            }
                        }
                        else if (c == ':')
                        {
                            if (ternaryDepth > 0)
                            {
                                ternaryDepth--;
                            }
                            else if (terminators.find(':') != std::string::npos)
                            {
                                break;
                            }
                        }
                        else if (ternaryDepth == 0 && terminators.find(c) != std::string::npos)
                        {
                            break;
                        }
                    }

                    m_pos++;
                }

                return trim(m_text.substr(start, m_pos - start));
            }

            bool isSelectionAhead()
            {
                size_t savedPos = m_pos;
                try
                {
                    if (peek() != '{') return false;
                    consume('{');
                    skipWhitespaceAndComments();
                    if (peek() == '}') { m_pos = savedPos; return false; }

                    std::string fieldName = trim(readJsExpression(":"));
                    skipWhitespaceAndComments();
                    if (peek() != ':') { m_pos = savedPos; return false; }
                    consume(':');
                    skipWhitespaceAndComments();

                    if (peek() != '{' && peek() != '[')
                    {
                        m_pos = savedPos;
                        return true;
                    }

                    for (const char *op : kExtendedOperators)
                    {
                        if (hasPrefix(op))
                        {
                            m_pos = savedPos;
                            return true;
                        }
                    }

                    if (hasPrefix("push(") || hasPrefix("Math.min(") || hasPrefix("Math.max("))
                    {
                        m_pos = savedPos;
                        return true;
                    }
                }
                catch (...)
                {
                }
                m_pos = savedPos;
                return false;
            }

            AggregationField parseAggregationField(const std::string &fieldName)
            {
                AggregationField field;
                field.targetField = fieldName;

                std::string matchedOp;
                for (const char *op : kExtendedOperators)
                {
                    if (matchPrefix(op))
                    {
                        matchedOp = op;
                        break;
                    }
                }

                std::string expr = readJsExpression(",]}");

                if (!matchedOp.empty())
                {
                    if (matchedOp == "+")
                    {
                        matchedOp = "+=";
                    }
                    field.op = matchedOp;
                    field.sourceExpr = rewriteEntityExpression(expr);
                    if (matchedOp == "+=" || matchedOp == "-=" || matchedOp == "++" || matchedOp == "--" ||
                        matchedOp == "%=" || matchedOp == "<<=" || matchedOp == ">>=" || matchedOp == ">>>=" ||
                        matchedOp == "|=" || matchedOp == "^=")
                    {
                        field.initValue = "0";
                    }
                    else if (matchedOp == "*=" || matchedOp == "/=" || matchedOp == "**=")
                    {
                        field.initValue = "1";
                    }
                    else if (matchedOp == "&=")
                    {
                        field.initValue = "-1";
                    }
                    else if (matchedOp == "&&=")
                    {
                        field.initValue = "true";
                    }
                    else
                    {
                        field.initValue = "null";
                    }
                }
                else
                {
                    if (expr.rfind("push(", 0) == 0 && expr.back() == ')')
                    {
                        field.op = "push";
                        field.initValue = "[]";
                        field.sourceExpr = rewriteEntityExpression(trim(expr.substr(5, expr.length() - 6)));
                    }
                    else if (expr.rfind("Math.min(", 0) == 0 && expr.back() == ')')
                    {
                        field.op = "min";
                        field.initValue = "Infinity";
                        field.sourceExpr = rewriteEntityExpression(trim(expr.substr(9, expr.length() - 10)));
                    }
                    else if (expr.rfind("Math.max(", 0) == 0 && expr.back() == ')')
                    {
                        field.op = "max";
                        field.initValue = "-Infinity";
                        field.sourceExpr = rewriteEntityExpression(trim(expr.substr(9, expr.length() - 10)));
                    }
                    else
                    {
                        field.op = "=";
                        field.initValue = "null";
                        field.sourceExpr = rewriteEntityExpression(expr);
                    }
                }
                return field;
            }

            std::shared_ptr<DslNode> parseSelectionBlock(const std::string &subtotalField)
            {
                consume('{');
                skipWhitespaceAndComments();
                auto sel = std::make_shared<DslNode>();
                sel->type = DslNodeType::Selection;
                sel->subtotalField = subtotalField;

                while (peek() != '}' && peek() != '\0')
                {
                    std::string fieldName = trim(readJsExpression(":"));
                    consume(':');
                    skipWhitespaceAndComments();

                    sel->aggregations.push_back(parseAggregationField(fieldName));

                    skipWhitespaceAndComments();
                    if (peek() == ',')
                    {
                        consume(',');
                    }
                    else if (peek() == '}')
                    {
                        break;
                    }
                }
                consume('}');
                return sel;
            }

            std::shared_ptr<DslNode> parseArraySelectionBlock(const std::string &subtotalField)
            {
                consume('[');
                skipWhitespaceAndComments();
                auto sel = std::make_shared<DslNode>();
                sel->type = DslNodeType::ArraySelection;
                sel->subtotalField = subtotalField;
                int idx = 0;

                while (peek() != ']' && peek() != '\0')
                {
                    std::string idxStr = std::to_string(idx++);
                    sel->aggregations.push_back(parseAggregationField(idxStr));

                    skipWhitespaceAndComments();
                    if (peek() == ',')
                    {
                        consume(',');
                    }
                    else if (peek() == ']')
                    {
                        break;
                    }
                }
                consume(']');
                return sel;
            }

            std::shared_ptr<DslNode> parseGroupBlock(const std::string &keyExpr)
            {
                consume('{');
                skipWhitespaceAndComments();
                auto group = std::make_shared<DslNode>();
                group->type = DslNodeType::Group;
                group->keyExpr = keyExpr;

                while (peek() != '}' && peek() != '\0')
                {
                    std::string rawKey = readJsExpression(":");
                    consume(':');
                    skipWhitespaceAndComments();

                    if (peek() == '[')
                    {
                        auto arraySel = parseArraySelectionBlock(hasEntityReference(rawKey) ? "" : rawKey);
                        if (hasEntityReference(rawKey))
                        {
                            auto leafGroup = std::make_shared<DslNode>();
                            leafGroup->type = DslNodeType::Group;
                            leafGroup->keyExpr = rawKey;
                            leafGroup->children.push_back(arraySel);
                            group->children.push_back(leafGroup);
                        }
                        else
                        {
                            group->children.push_back(arraySel);
                        }
                    }
                    else if (peek() == '{')
                    {
                        if (isSelectionAhead())
                        {
                            if (hasEntityReference(rawKey))
                            {
                                auto leafGroup = std::make_shared<DslNode>();
                                leafGroup->type = DslNodeType::Group;
                                leafGroup->keyExpr = rawKey;
                                auto sel = parseSelectionBlock("");
                                leafGroup->children.push_back(sel);
                                group->children.push_back(leafGroup);
                            }
                            else
                            {
                                auto sel = parseSelectionBlock(rawKey);
                                group->children.push_back(sel);
                            }
                        }
                        else
                        {
                            auto childGroup = parseGroupBlock(rawKey);
                            group->children.push_back(childGroup);
                        }
                    }
                    else
                    {
                        // Direct aggregate field at this level
                        AggregationField field = parseAggregationField(rawKey);

                        std::shared_ptr<DslNode> directSel = nullptr;
                        for (auto &c : group->children)
                        {
                            if (c->type == DslNodeType::Selection && c->subtotalField.empty())
                            {
                                directSel = c;
                                break;
                            }
                        }
                        if (!directSel)
                        {
                            directSel = std::make_shared<DslNode>();
                            directSel->type = DslNodeType::Selection;
                            directSel->subtotalField = "";
                            group->children.push_back(directSel);
                        }
                        directSel->aggregations.push_back(field);
                    }

                    skipWhitespaceAndComments();
                    if (peek() == ',')
                    {
                        consume(',');
                    }
                    else if (peek() == '}')
                    {
                        break;
                    }
                }
                consume('}');
                return group;
            }

            std::string m_text;
            size_t m_pos;
        };
    } // namespace

    bool isDslQuery(const std::string &input)
    {
        std::string s = trim(input);
        if (s.empty()) return false;

        // Raw JS object/function options e.g. "({ implied: false })" or "e => ({ a: e.a })"
        if (s.rfind("({", 0) == 0 || s.rfind("(e", 0) == 0 || s.rfind("e =>", 0) == 0 || s.rfind("function", 0) == 0)
        {
            return false;
        }

        Parser parser(s);
        DslParseResult res = parser.parse();
        return res.success;
    }

    DslParseResult parse(const std::string &dslQuery)
    {
        Parser parser(dslQuery);
        return parser.parse();
    }

    std::string compileToJsCode(const std::string &dslQuery, const std::string &entriesVar)
    {
        DslParseResult parsed = parse(dslQuery);
        if (!parsed.success || !parsed.rootNode)
        {
            return "";
        }

        std::ostringstream ss;
        ss << "\"use strict\";\n"
           << "const toNumber = (value) => {\n"
           << "  const number = Number(value);\n"
           << "  return Number.isFinite(number) ? number : 0;\n"
           << "};\n\n"
           << "const _rawResult = " << entriesVar << ".reduce((acc, e) => {\n";

        if (!parsed.whereExpr.empty())
        {
            ss << "  try {\n"
               << "    if (!(" << parsed.whereExpr << ")) return acc;\n"
               << "  } catch {\n"
               << "    return acc;\n"
               << "  }\n";
        }

        if (parsed.rootNode->type == DslNodeType::ScalarSelection)
        {
            const auto &agg = parsed.rootNode->aggregations[0];
            if (agg.op == "+=")
            {
                if (agg.sourceExpr.empty() || agg.sourceExpr == "1")
                {
                    ss << "  acc += 1;\n";
                }
                else
                {
                    ss << "  acc += toNumber(" << agg.sourceExpr << ");\n";
                }
            }
            else if (agg.op == "++")
            {
                ss << "  acc += 1;\n";
            }
            else if (agg.op == "-=" || agg.op == "--")
            {
                if (agg.sourceExpr.empty() || agg.sourceExpr == "1")
                {
                    ss << "  acc -= 1;\n";
                }
                else
                {
                    ss << "  acc -= toNumber(" << agg.sourceExpr << ");\n";
                }
            }
            else if (agg.op == "*=" || agg.op == "/=" || agg.op == "**=")
            {
                ss << "  acc " << agg.op << " toNumber(" << agg.sourceExpr << ");\n";
            }
            else if (agg.op == "min")
            {
                ss << "  acc = Math.min(acc, toNumber(" << agg.sourceExpr << "));\n";
            }
            else if (agg.op == "max")
            {
                ss << "  acc = Math.max(acc, toNumber(" << agg.sourceExpr << "));\n";
            }
            else
            {
                ss << "  acc += toNumber(" << agg.sourceExpr << ");\n";
            }

            ss << "\n"
               << "  return acc;\n"
               << "}, " << agg.initValue << ");";
        }
        else if (parsed.rootNode->type == DslNodeType::ArraySelection)
        {
            std::string initArr = "[";
            for (size_t i = 0; i < parsed.rootNode->aggregations.size(); ++i)
            {
                if (i > 0) initArr += ", ";
                initArr += parsed.rootNode->aggregations[i].initValue;
            }
            initArr += "]";

            for (const auto &agg : parsed.rootNode->aggregations)
            {
                emitAggLine("acc", agg, ss);
            }

            ss << "\n"
               << "  return acc;\n"
               << "}, " << initArr << ");";
        }
        else if (parsed.rootNode->type == DslNodeType::Selection)
        {
            std::vector<AggregationField> staticAggs;
            for (const auto &agg : parsed.rootNode->aggregations)
            {
                if (!hasEntityReference(agg.targetField))
                {
                    staticAggs.push_back(agg);
                }
            }

            std::string initObj = "Object.create(null)";
            if (!staticAggs.empty())
            {
                initObj = "Object.assign(Object.create(null), { ";
                for (size_t i = 0; i < staticAggs.size(); ++i)
                {
                    if (i > 0) initObj += ", ";
                    initObj += quoteJsString(staticAggs[i].targetField) + ": " + staticAggs[i].initValue;
                }
                initObj += " })";
            }

            for (const auto &agg : parsed.rootNode->aggregations)
            {
                emitAggLine("acc", agg, ss);
            }

            ss << "\n"
               << "  return acc;\n"
               << "}, " << initObj << ");";
        }
        else
        {
            int varCounter = 0;
            emitNode(parsed.rootNode, "acc", varCounter, ss);

            ss << "\n"
               << "  return acc;\n"
               << "}, Object.create(null));";
        }

        ss << "\n\n"
           << "const result = (() => {\n"
           << "  const _d = (typeof globalThis.ROUND === 'number' && Number.isFinite(globalThis.ROUND)) ? Math.floor(globalThis.ROUND) :\n"
           << "             (typeof globalThis.round === 'number' && Number.isFinite(globalThis.round)) ? Math.floor(globalThis.round) :\n"
           << "             (typeof globalThis.ROUND_DIGITS === 'number' && Number.isFinite(globalThis.ROUND_DIGITS)) ? Math.floor(globalThis.ROUND_DIGITS) :\n"
           << "             (typeof globalThis.round_digits === 'number' && Number.isFinite(globalThis.round_digits)) ? Math.floor(globalThis.round_digits) :\n"
           << "             (typeof globalThis.round === 'function' && typeof globalThis.round.digits === 'number' && Number.isFinite(globalThis.round.digits)) ? Math.floor(globalThis.round.digits) :\n"
           << "             undefined;\n"
           << "  if (_d === undefined || _d === null) return _rawResult;\n"
           << "  const _roundNum = (n) => {\n"
           << "    if (typeof n !== 'number' || !Number.isFinite(n)) return n;\n"
           << "    const factor = Math.pow(10, _d);\n"
           << "    return Math.round((n + Number.EPSILON) * factor) / factor;\n"
           << "  };\n"
           << "  const _deep = (v) => {\n"
           << "    if (typeof v === 'number') return _roundNum(v);\n"
           << "    if (Array.isArray(v)) return v.map(_deep);\n"
           << "    if (v !== null && typeof v === 'object') {\n"
           << "      const out = Object.create(Object.getPrototypeOf(v));\n"
           << "      for (const k of Object.keys(v)) out[k] = _deep(v[k]);\n"
           << "      return out;\n"
           << "    }\n"
           << "    return v;\n"
           << "  };\n"
           << "  return _deep(_rawResult);\n"
           << "})();";

        return ss.str();
    }

    std::string compileToJsFunction(const std::string &dslQuery, const std::string &entriesVar)
    {
        const std::string code = compileToJsCode(dslQuery, entriesVar);
        if (code.empty())
        {
            return "";
        }

        std::ostringstream ss;
        ss << "((" << entriesVar << ") => {\n"
           << code << "\n"
           << "  return result;\n"
           << "})";
        return ss.str();
    }

    std::string compileToSsQueryOptions(const std::string &dslQuery)
    {
        DslParseResult parsed = parse(dslQuery);
        const std::string fn = compileToJsFunction(dslQuery, "entries");
        if (fn.empty())
        {
            return "({ implied: true })";
        }

        std::ostringstream ss;
        ss << "({\n"
           << "  implied: " << (parsed.options.implied ? "true" : "false") << ",\n"
           << "  query: " << fn << "\n"
           << "})";
        return ss.str();
    }
}

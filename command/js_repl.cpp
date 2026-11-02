#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "aced.h"
#include "adscodes.h"
#include "acutads.h"
#include "acdocman.h"
#include "commands.h"
#include "text_util.h"
#include "../src/js_engine.h"

namespace
{
class DocumentLockGuard
{
public:
    explicit DocumentLockGuard(AcApDocument* document)
        : document_(document), locked_(false), status_(Acad::eOk)
    {
        if (document_ == nullptr || acDocManager == nullptr || document_->myLockMode() != AcAp::kNone)
        {
            return;
        }

        status_ = acDocManager->lockDocument(document_, AcAp::kWrite);
        locked_ = status_ == Acad::eOk;
    }

    ~DocumentLockGuard()
    {
        if (locked_ && acDocManager != nullptr)
        {
            acDocManager->unlockDocument(document_);
        }
    }

    bool ok() const
    {
        return status_ == Acad::eOk;
    }

    Acad::ErrorStatus status() const
    {
        return status_;
    }

private:
    AcApDocument* document_;
    bool locked_;
    Acad::ErrorStatus status_;
};

bool isCompleteJavaScript(const std::string& source)
{
    std::vector<char> stack;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inTemplate = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escaped = false;

    for (size_t i = 0; i < source.size(); ++i)
    {
        const char ch = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';

        if (inLineComment)
        {
            if (ch == '\n' || ch == '\r')
            {
                inLineComment = false;
            }
            continue;
        }

        if (inBlockComment)
        {
            if (ch == '*' && next == '/')
            {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (inSingleQuote || inDoubleQuote || inTemplate)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (inSingleQuote && ch == '\'')
            {
                inSingleQuote = false;
            }
            else if (inDoubleQuote && ch == '"')
            {
                inDoubleQuote = false;
            }
            else if (inTemplate && ch == '`')
            {
                inTemplate = false;
            }
            continue;
        }

        if (ch == '/' && next == '/')
        {
            inLineComment = true;
            ++i;
            continue;
        }

        if (ch == '/' && next == '*')
        {
            inBlockComment = true;
            ++i;
            continue;
        }

        if (ch == '\'')
        {
            inSingleQuote = true;
        }
        else if (ch == '"')
        {
            inDoubleQuote = true;
        }
        else if (ch == '`')
        {
            inTemplate = true;
        }
        else if (ch == '(' || ch == '[' || ch == '{')
        {
            stack.push_back(ch);
        }
        else if (ch == ')' || ch == ']' || ch == '}')
        {
            if (stack.empty())
            {
                return true;
            }

            const char open = stack.back();
            if ((open == '(' && ch == ')') || (open == '[' && ch == ']') || (open == '{' && ch == '}'))
            {
                stack.pop_back();
            }
        }
    }

    return stack.empty() && !inSingleQuote && !inDoubleQuote && !inTemplate && !inBlockComment;
}

void printReplResult(const js_engine::EvalResult& result)
{
    const std::basic_string<ACHAR> output = command_text::fromUtf8(result.text);
    if (result.ok)
    {
        acutPrintf(ACRX_T("\n%s"), output.c_str());
    }
    else
    {
        acutPrintf(ACRX_T("\nUncaught %s"), output.c_str());
    }
}
}

namespace commands
{
void jsRepl()
{
    acutPrintf(ACRX_T("\nQuickJS REPL. Type .quit to exit."));

    if (!js_engine::isInitialized())
    {
        printReplResult(js_engine::eval(std::string()));
        return;
    }

    AcApDocument* pDoc = acDocManager->curDocument();
    if (!pDoc)
        return;

    DocumentLockGuard documentLock(pDoc);
    if (!documentLock.ok())
    {
        acutPrintf(ACRX_T("\nUnable to lock current document for JS:REPL: %d"), documentLock.status());
        return;
    }

    js_engine::eval("globalThis.workingDb = {}; globalThis.transaction = null;");

    std::string source;
    for (;;)
    {
        ACHAR line[2048] = {};
        const ACHAR* prompt = source.empty() ? ACRX_T("\n> ") : ACRX_T("\n... ");
        const int status = acedGetString(1, prompt, line);
        if (status != RTNORM)
        {
            acutPrintf(ACRX_T("\nJS REPL canceled."));
            return;
        }

        const std::string utf8Line = command_text::toUtf8(line);
        if (source.empty() && utf8Line == ".quit")
        {
            acutPrintf(ACRX_T("\nJS REPL exited."));
            return;
        }

        source += utf8Line;
        source += "\n";

        if (!isCompleteJavaScript(source))
        {
            continue;
        }

        const js_engine::EvalResult result = js_engine::evalForRepl(source);
        printReplResult(result);

        if (result.ok)
        {
            js_engine::eval("if (globalThis.transaction !== null) { tm.commit(); };");
        }
        else
        {
            js_engine::eval("if (globalThis.transaction !== null) { tm.abort(); }");
        }

        source.clear();
    }
}

void jsEval()
{
    if (!js_engine::isInitialized())
    {
        printReplResult(js_engine::eval(std::string()));
        return;
    }

    AcApDocument* pDoc = acDocManager->curDocument();
    if (!pDoc)
        return;

    DocumentLockGuard documentLock(pDoc);
    if (!documentLock.ok())
    {
        acutPrintf(ACRX_T("\nUnable to lock document for JS:EVAL: %d"), documentLock.status());
        return;
    }

    ACHAR line[4096] = {};
    const int status = acedGetString(1, ACRX_T("\nJS Expression: "), line);
    if (status != RTNORM || line[0] == L'\0')
    {
        return;
    }

    const std::string utf8Line = command_text::toUtf8(line);
    const js_engine::EvalResult result = js_engine::evalJson(utf8Line);
    acutPrintf(ACRX_T("\n%s\n"), command_text::fromUtf8(result.text).c_str());

    if (result.ok)
    {
        js_engine::eval("if (globalThis.transaction !== null) { tm.commit(); };");
    }
    else
    {
        js_engine::eval("if (globalThis.transaction !== null) { tm.abort(); }");
    }
}

void jsExecute()
{
    if (!js_engine::isInitialized())
    {
        printReplResult(js_engine::eval(std::string()));
        return;
    }

    AcApDocument* pDoc = acDocManager->curDocument();
    if (!pDoc)
        return;

    DocumentLockGuard documentLock(pDoc);
    if (!documentLock.ok())
    {
        acutPrintf(ACRX_T("\nUnable to lock document for JS:EXECUTE: %d"), documentLock.status());
        return;
    }

    ACHAR filePath[MAX_PATH] = {};
    const int status = acedGetString(0, ACRX_T("\nJS Script or Code: "), filePath);
    if (status != RTNORM || filePath[0] == L'\0')
    {
        return;
    }

    std::string input = command_text::toUtf8(filePath);
    std::string source;
    // Check if ends with .js
    if (input.size() >= 3 && _stricmp(input.substr(input.size() - 3).c_str(), ".js") == 0)
    {
        std::string escaped;
        for (char c : input)
        {
            if (c == '\\') escaped += "/";
            else escaped += c;
        }
        source = "await load('" + escaped + "', false);";
    }
    else
    {
        source = input;
    }

    const js_engine::EvalResult result = js_engine::eval(source);

    if (result.ok)
    {
        js_engine::eval("if (globalThis.transaction !== null) { tm.commit(); };");
    }
    else
    {
        js_engine::eval("if (globalThis.transaction !== null) { tm.abort(); }");
        acutPrintf(ACRX_T("\nJS Error: %s\n"), command_text::fromUtf8(result.text).c_str());
    }
}

void helloArx()
{
    acutPrintf(ACRX_T("\narx01: AutoCAD JavaScript Runtime loaded."));
    acutPrintf(ACRX_T("\nCommands: HELLOARX, js:repl, js:eval, js:execute, js:server"));
    acutPrintf(ACRX_T("\nAutoLISP: (js-eval \"expr\") -> JSON, (js-execute \"code\") -> no return\n"));
}
}

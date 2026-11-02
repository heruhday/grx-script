#pragma once

#include <string>
#include <vector>

struct JSRuntime;
struct JSContext;

namespace js_engine
{
struct EvalResult
{
    bool ok;
    std::string text;
};

class Runtime
{
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool isValid() const;
    EvalResult eval(const std::string& source);
    EvalResult evalForRepl(const std::string& source);
    EvalResult evalForDisplay(const std::string& source);
    EvalResult emitSelectionEvent(const std::vector<std::string>& entityHandles);
    EvalResult evalJson(const std::string& source);

private:
    EvalResult evalInternal(const std::string& source, bool replDisplay, bool updateAnswerHistory = true);
    EvalResult evalJsonInternal(const std::string& source);

    JSRuntime* runtime_;
    JSContext* context_;
};

bool initialize();
void shutdown();
bool isInitialized();
EvalResult eval(const std::string& source);
EvalResult evalForRepl(const std::string& source);
EvalResult evalJson(const std::string& source);
EvalResult emitSelectionEvent(const std::vector<std::string>& entityHandles);
}

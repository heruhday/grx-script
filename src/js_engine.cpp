#include "js_engine.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4800)
#endif
#include "quickjs.h"
#include "quickjs-libc.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlobj.h>

#include <cctype>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "acedads.h"
#include "js_modules/js_modules.h"

namespace
{
const int kMaxInspectDepth = 4;
const size_t kMaxInlineInspectLength = 80;
const size_t kQuickJsMemoryLimit = 1024ULL * 1024 * 1024;

std::string valueToString(JSContext* context, JSValueConst value)
{
    const char* chars = JS_ToCString(context, value);
    if (chars == nullptr)
    {
        return "<failed to convert JavaScript value>";
    }

    std::string result(chars);
    JS_FreeCString(context, chars);
    return result;
}

std::string wideToUtf8(const wchar_t* text)
{
    if (text == nullptr || text[0] == L'\0')
    {
        return std::string();
    }

    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], length, nullptr, nullptr);
    return result;
}

bool readTextFile(const std::string& path, std::string& text)
{
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path.c_str(), "rb") != 0)
    {
        file = nullptr;
    }
#else
    file = std::fopen(path.c_str(), "rb");
#endif

    if (file == nullptr)
    {
        return false;
    }

    std::vector<char> buffer(4096);
    text.clear();
    for (;;)
    {
        const size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), file);
        if (bytesRead > 0)
        {
            text.append(buffer.data(), bytesRead);
        }

        if (bytesRead < buffer.size())
        {
            break;
        }
    }

    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

JSValue jsPickFolder(JSContext* context, JSValueConst, int, JSValueConst*)
{
    const HRESULT initStatus = OleInitialize(nullptr);
    const bool shouldUninitialize = initStatus == S_OK || initStatus == S_FALSE;
    if (FAILED(initStatus) && initStatus != RPC_E_CHANGED_MODE)
    {
        return JS_ThrowInternalError(context, "failed to initialize folder picker (HRESULT 0x%08X)", static_cast<unsigned int>(initStatus));
    }

    wchar_t selectedPath[MAX_PATH] = {};
    BROWSEINFOW browseInfo = {};
    browseInfo.hwndOwner = adsw_acadMainWnd();
    browseInfo.lpszTitle = L"Select current JavaScript working folder";
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    PIDLIST_ABSOLUTE itemList = SHBrowseForFolderW(&browseInfo);
    if (itemList == nullptr)
    {
        if (shouldUninitialize)
        {
            OleUninitialize();
        }
        return JS_NULL;
    }

    const BOOL ok = SHGetPathFromIDListW(itemList, selectedPath);
    CoTaskMemFree(itemList);
    if (shouldUninitialize)
    {
        OleUninitialize();
    }

    if (!ok)
    {
        return JS_NULL;
    }

    const std::string utf8 = wideToUtf8(selectedPath);
    return JS_NewStringLen(context, utf8.data(), utf8.size());
}

JSValue jsLoadScript(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc <= 0 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
    {
        return JS_ThrowTypeError(context, "load expects a JavaScript file path");
    }

    const char* pathChars = JS_ToCString(context, argv[0]);
    if (pathChars == nullptr)
    {
        return JS_EXCEPTION;
    }

    std::string path(pathChars);
    JS_FreeCString(context, pathChars);
    if (path.empty())
    {
        return JS_ThrowTypeError(context, "load expects a JavaScript file path");
    }

    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '\\')
        {
            path[i] = '/';
        }
    }

    std::string source;
    if (!readTextFile(path, source))
    {
        return JS_ThrowReferenceError(context, "could not load JavaScript file '%s'", path.c_str());
    }

    return JS_Eval(
        context,
        source.c_str(),
        source.size(),
        path.c_str(),
        JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC);
}

std::string indent(int depth)
{
    return std::string(static_cast<size_t>(depth * 2), ' ');
}

bool isIdentifierKey(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (!(std::isalpha(first) || text[0] == '_' || text[0] == '$'))
    {
        return false;
    }

    for (size_t i = 1; i < text.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (!(std::isalnum(ch) || text[i] == '_' || text[i] == '$'))
        {
            return false;
        }
    }

    return true;
}

std::string quoteString(const std::string& text)
{
    std::string result("'");
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '\'':
            result += "\\'";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\v':
            result += "\\v";
            break;
        case '\0':
            result += "\\0";
            break;
        default:
            if (ch < 0x20 || ch == 0x7f)
            {
                char escaped[5] = {};
                std::sprintf(escaped, "\\x%02X", static_cast<unsigned int>(ch));
                result += escaped;
            }
            else
            {
                result += static_cast<char>(ch);
            }
            break;
        }
    }
    result += "'";
    return result;
}

std::string jsonQuote(const std::string& text)
{
    std::string result("\"");
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        default:
            if (ch < 0x20)
            {
                char escaped[7] = {};
                std::sprintf(escaped, "\\u%04X", static_cast<unsigned int>(ch));
                result += escaped;
            }
            else
            {
                result += static_cast<char>(ch);
            }
            break;
        }
    }
    result += "\"";
    return result;
}

std::string formatKey(const std::string& key)
{
    return isIdentifierKey(key) ? key : quoteString(key);
}

bool containsNewline(const std::string& text)
{
    return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
}

std::string trim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.erase(text.begin());
    }

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.pop_back();
    }

    return text;
}

bool isIdentifierStart(char ch)
{
    const unsigned char c = static_cast<unsigned char>(ch);
    return std::isalpha(c) || ch == '_' || ch == '$';
}

bool isIdentifierPart(char ch)
{
    const unsigned char c = static_cast<unsigned char>(ch);
    return std::isalnum(c) || ch == '_' || ch == '$';
}

void skipSpaces(const std::string& text, size_t& index)
{
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
    {
        ++index;
    }
}

bool consumeWord(const std::string& text, size_t& index, const char* word)
{
    const size_t start = index;
    for (const char* p = word; *p != '\0'; ++p)
    {
        if (index >= text.size() || text[index] != *p)
        {
            index = start;
            return false;
        }
        ++index;
    }

    if (index < text.size() && isIdentifierPart(text[index]))
    {
        index = start;
        return false;
    }

    return true;
}

bool consumeIdentifier(const std::string& text, size_t& index, std::string& name)
{
    if (index >= text.size() || !isIdentifierStart(text[index]))
    {
        return false;
    }

    const size_t start = index++;
    while (index < text.size() && isIdentifierPart(text[index]))
    {
        ++index;
    }

    name = text.substr(start, index - start);
    return true;
}

std::string normalizeSimpleReplDeclaration(const std::string& source)
{
    std::string text = trim(source);
    size_t index = 0;
    if (!(consumeWord(text, index, "let") || consumeWord(text, index, "const") || consumeWord(text, index, "var")))
    {
        return source;
    }

    skipSpaces(text, index);

    std::string name;
    if (!consumeIdentifier(text, index, name))
    {
        return source;
    }

    skipSpaces(text, index);
    if (index >= text.size())
    {
        return "globalThis." + name + " = undefined;";
    }

    if (text[index] != '=')
    {
        return source;
    }

    ++index;
    std::string initializer = text.substr(index);
    initializer = trim(initializer);
    if (!initializer.empty() && initializer.back() == ';')
    {
        initializer.pop_back();
        initializer = trim(initializer);
    }

    if (initializer.empty() || initializer.find(',') != std::string::npos)
    {
        return source;
    }

    return "globalThis." + name + " = " + initializer + ";";
}

bool isAnswerHelperRead(const std::string& source)
{
    std::string text = trim(source);
    if (!text.empty() && text.back() == ';')
    {
        text.pop_back();
        text = trim(text);
    }

    return text == "ans" || text == "last";
}

uint32_t arrayLength(JSContext* context, JSValueConst array)
{
    uint32_t length = 0;
    JSValue lengthValue = JS_GetPropertyStr(context, array, "length");
    JS_ToUint32(context, &length, lengthValue);
    JS_FreeValue(context, lengthValue);
    return length;
}

void updateReplAnswerGlobals(JSContext* context, JSValueConst value)
{
    JSValue global = JS_GetGlobalObject(context);

    JS_SetPropertyStr(context, global, "last", JS_DupValue(context, value));

    JSValue oldHistory = JS_GetPropertyStr(context, global, "ans");
    JSValue newHistory = JS_NewArray(context);

    uint32_t newIndex = 0;
    if (JS_IsArray(oldHistory))
    {
        const uint32_t oldLength = arrayLength(context, oldHistory);
        const uint32_t start = oldLength > 9 ? oldLength - 9 : 0;

        for (uint32_t i = start; i < oldLength; ++i)
        {
            JSValue item = JS_GetPropertyUint32(context, oldHistory, i);
            JS_SetPropertyUint32(context, newHistory, newIndex, item);
            ++newIndex;
        }
    }

    JS_SetPropertyUint32(context, newHistory, newIndex, JS_DupValue(context, value));
    JS_SetPropertyStr(context, global, "ans", newHistory);

    JS_FreeValue(context, oldHistory);
    JS_FreeValue(context, global);
}

std::string inspectValue(JSContext* context, JSValueConst value, int depth, std::vector<void*>& seen);

std::string inspectArray(JSContext* context, JSValueConst value, int depth, std::vector<void*>& seen)
{
    uint32_t length = 0;
    length = arrayLength(context, value);

    if (length == 0)
    {
        return "[]";
    }

    std::string compact("[ ");
    bool canUseCompact = true;
    for (uint32_t i = 0; i < length; ++i)
    {
        JSValue item = JS_GetPropertyUint32(context, value, i);
        const std::string itemText = inspectValue(context, item, depth + 1, seen);
        JS_FreeValue(context, item);

        if (containsNewline(itemText))
        {
            canUseCompact = false;
            break;
        }

        compact += itemText;
        if (i + 1 < length)
        {
            compact += ", ";
        }
    }
    compact += " ]";

    if (canUseCompact && compact.size() <= kMaxInlineInspectLength)
    {
        return compact;
    }

    std::string result("[\n");
    for (uint32_t i = 0; i < length; ++i)
    {
        JSValue item = JS_GetPropertyUint32(context, value, i);
        result += indent(depth + 1);
        result += inspectValue(context, item, depth + 1, seen);
        JS_FreeValue(context, item);

        if (i + 1 < length)
        {
            result += ",";
        }
        result += "\n";
    }
    result += indent(depth);
    result += "]";
    return result;
}

std::string inspectObject(JSContext* context, JSValueConst value, int depth, std::vector<void*>& seen)
{
    JSPropertyEnum* properties = nullptr;
    uint32_t propertyCount = 0;
    const int flags = JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY;
    if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, value, flags) < 0)
    {
        return valueToString(context, value);
    }

    if (propertyCount == 0)
    {
        JS_FreePropertyEnum(context, properties, propertyCount);
        return "{}";
    }

    std::string compact("{ ");
    bool canUseCompact = true;
    for (uint32_t i = 0; i < propertyCount; ++i)
    {
        JSValue keyValue = JS_AtomToString(context, properties[i].atom);
        const std::string key = valueToString(context, keyValue);
        JS_FreeValue(context, keyValue);

        JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
        const std::string propertyText = inspectValue(context, propertyValue, depth + 1, seen);
        JS_FreeValue(context, propertyValue);

        if (containsNewline(propertyText))
        {
            canUseCompact = false;
            break;
        }

        compact += formatKey(key);
        compact += ": ";
        compact += propertyText;
        if (i + 1 < propertyCount)
        {
            compact += ", ";
        }
    }
    compact += " }";

    if (canUseCompact && compact.size() <= kMaxInlineInspectLength)
    {
        JS_FreePropertyEnum(context, properties, propertyCount);
        return compact;
    }

    std::string result("{\n");
    for (uint32_t i = 0; i < propertyCount; ++i)
    {
        JSValue keyValue = JS_AtomToString(context, properties[i].atom);
        const std::string key = valueToString(context, keyValue);
        JS_FreeValue(context, keyValue);

        JSValue propertyValue = JS_GetProperty(context, value, properties[i].atom);
        result += indent(depth + 1);
        result += formatKey(key);
        result += ": ";
        result += inspectValue(context, propertyValue, depth + 1, seen);
        JS_FreeValue(context, propertyValue);

        if (i + 1 < propertyCount)
        {
            result += ",";
        }
        result += "\n";
    }

    JS_FreePropertyEnum(context, properties, propertyCount);
    result += indent(depth);
    result += "}";
    return result;
}

std::string inspectValue(JSContext* context, JSValueConst value, int depth, std::vector<void*>& seen)
{
    if (JS_IsString(value))
    {
        return quoteString(valueToString(context, value));
    }

    if (!JS_IsObject(value) || JS_IsFunction(context, value))
    {
        return valueToString(context, value);
    }

    if (depth >= kMaxInspectDepth)
    {
        return JS_IsArray(value) ? "[Array]" : "[Object]";
    }

    void* ptr = JS_VALUE_GET_PTR(value);
    for (void* seenPtr : seen)
    {
        if (seenPtr == ptr)
        {
            return "[Circular]";
        }
    }

    seen.push_back(ptr);
    const std::string result = JS_IsArray(value)
        ? inspectArray(context, value, depth, seen)
        : inspectObject(context, value, depth, seen);
    seen.pop_back();
    return result;
}

std::string valueToReplString(JSContext* context, JSValueConst value)
{
    std::vector<void*> seen;
    return inspectValue(context, value, 0, seen);
}

JSValue unwrapAsyncEvalResult(JSContext* context, JSValue value)
{
    if (!JS_IsObject(value))
    {
        return value;
    }

    JSAtom valueAtom = JS_NewAtom(context, "value");
    if (valueAtom == JS_ATOM_NULL)
    {
        return value;
    }

    const int hasValue = JS_HasProperty(context, value, valueAtom);
    JS_FreeAtom(context, valueAtom);
    if (hasValue <= 0)
    {
        return value;
    }

    JSValue unwrapped = JS_GetPropertyStr(context, value, "value");
    if (JS_IsException(unwrapped))
    {
        JS_FreeValue(context, unwrapped);
        return value;
    }

    JS_FreeValue(context, value);
    return unwrapped;
}
}

namespace js_engine
{
namespace
{
Runtime* g_runtime = nullptr;
std::thread::id g_runtimeThreadId;
std::mutex g_runtimeMutex;
bool g_runtimePrepared = false;

EvalResult jsonRuntimeError(const char* message)
{
    return {false, std::string("{\"error\":") + jsonQuote(message) + "}"};
}

EvalResult textRuntimeError(const char* message)
{
    return {false, message};
}

bool ensureRuntimeForCurrentThreadLocked()
{
    if (g_runtime != nullptr)
    {
        return g_runtime->isValid();
    }

    g_runtime = new Runtime();
    if (!g_runtime->isValid())
    {
        delete g_runtime;
        g_runtime = nullptr;
        g_runtimeThreadId = std::thread::id();
        return false;
    }

    g_runtimeThreadId = std::this_thread::get_id();
    return true;
}

bool isRuntimeOwnerThreadLocked()
{
    return g_runtime == nullptr || g_runtimeThreadId == std::this_thread::get_id();
}
}

Runtime::Runtime()
    : runtime_(JS_NewRuntime())
    , context_(nullptr)
{
    if (runtime_ == nullptr)
    {
        return;
    }

    // Selection events may materialize thousands of complete entity
    // descriptors before running an array-level query. The previous 16 MB
    // limit made QuickJS report its uncatchable out-of-memory exception as
    // `null` for otherwise valid large selections.
    JS_SetMemoryLimit(runtime_, kQuickJsMemoryLimit);
    JS_SetMaxStackSize(runtime_, 512 * 1024);
    js_std_init_handlers(runtime_);
    JS_SetModuleLoaderFunc2(runtime_, nullptr, js_module_loader, js_module_check_attributes, nullptr);

    context_ = JS_NewContext(runtime_);
    if (context_ != nullptr)
    {
        js_init_module_std(context_, "std");
        js_init_module_os(context_, "os");

        JSValue initialGlobal = JS_GetGlobalObject(context_);
        JS_SetPropertyStr(context_, initialGlobal, "dialogState", JS_NewObject(context_));
        JS_FreeValue(context_, initialGlobal);

        js_modules::installUtilities(context_);
        js_modules::installConsole(context_);
        js_modules::installEditor(context_);
        js_modules::installEntity(context_);
        js_modules::installRequire(context_);
        js_modules::installMsgpack(context_);
        js_modules::installTransactionManager(context_);

        JSValue global = JS_GetGlobalObject(context_);
        JS_SetPropertyStr(context_, global, "__arxPickFolder", JS_NewCFunction(context_, jsPickFolder, "__arxPickFolder", 0));
        JS_SetPropertyStr(context_, global, "__arxLoadScript", JS_NewCFunction(context_, jsLoadScript, "__arxLoadScript", 1));
        JS_FreeValue(context_, global);

        const EvalResult preloadRuntimeModules = evalInternal(
            "globalThis.std = await import('std');"
            "globalThis.__quickjsOs = await import('os');"
            "globalThis.os = Object.assign({}, globalThis.__quickjsOs);"
            "globalThis.os.chdir = function(path) {"
            "  if (arguments.length === 0 || path === undefined || path === null || path === '') {"
            "    path = globalThis.__arxPickFolder();"
            "    if (path === null) return -1;"
            "  }"
            "  return globalThis.__quickjsOs.chdir(String(path));"
            "};"
            "globalThis.load = async function(path, options) {"
            "  if (arguments.length === 0 || path === undefined || path === null || path === '') {"
            "    throw new TypeError('load expects a JavaScript file path');"
            "  }"
            "  const modulePath = String(path).replace(/\\\\/g, '/');"
            "  let useCache = true;"
            "  if (typeof options === 'boolean') {"
            "    useCache = options;"
            "  } else if (options && typeof options === 'object' && options.cache === false) {"
            "    useCache = false;"
            "  }"
            "  if (useCache) {"
            "    return await import(modulePath);"
            "  }"
            "  const result = await globalThis.__arxLoadScript(modulePath);"
            "  return result === undefined ? {} : result;"
            "};"
            "globalThis.fs = require('fs');"
            "globalThis.path = require('path');"
            "globalThis.pathUtilities = require('pathUtilities');"
            "globalThis.sqlite = require('sqlite');"
            "globalThis.excel = require('excel');"
            "globalThis.dialog = require('dialog');"
            "globalThis.color = require('color');"
            "globalThis.layer = require('layer');"
            "globalThis.entity = require('entity');"
            "globalThis.process = require('process');"
            "globalThis.net = require('net');"
            "globalThis.any = function(val) { return val !== undefined && val !== null; };"
            "globalThis.rgb = function(r, g, b) { return { r: r, g: g, b: b }; };"
            "globalThis.TextEncoder = class TextEncoder {"
            "  encode(str) {"
            "    const bytes = [];"
            "    for (let i = 0; i < str.length; i++) {"
            "      const code = str.charCodeAt(i);"
            "      if (code <= 0x7F) {"
            "        bytes.push(code);"
            "      } else if (code <= 0x7FF) {"
            "        bytes.push(0xC0 | (code >> 6));"
            "        bytes.push(0x80 | (code & 0x3F));"
            "      } else if (code <= 0xFFFF) {"
            "        bytes.push(0xE0 | (code >> 12));"
            "        bytes.push(0x80 | ((code >> 6) & 0x3F));"
            "        bytes.push(0x80 | (code & 0x3F));"
            "      }"
            "    }"
            "    return new Uint8Array(bytes);"
            "  }"
            "};"
            "globalThis.TextDecoder = class TextDecoder {"
            "  decode(bytes) {"
            "    let str = '';"
            "    let i = 0;"
            "    while (i < bytes.length) {"
            "      const byte = bytes[i++];"
            "      if (byte <= 0x7F) {"
            "        str += String.fromCharCode(byte);"
            "      } else if ((byte & 0xE0) === 0xC0) {"
            "        const byte2 = bytes[i++];"
            "        const code = ((byte & 0x1F) << 6) | (byte2 & 0x3F);"
            "        str += String.fromCharCode(code);"
            "      } else if ((byte & 0xF0) === 0xE0) {"
            "        const byte2 = bytes[i++];"
            "        const byte3 = bytes[i++];"
            "        const code = ((byte & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);"
            "        str += String.fromCharCode(code);"
            "      }"
            "    }"
            "    return str;"
            "  }"
            "};",
            false);
        (void)preloadRuntimeModules;
    }
}

Runtime::~Runtime()
{
    js_modules::uninstallDialog();

    if (context_ != nullptr)
    {
        JS_FreeContext(context_);
    }

    if (runtime_ != nullptr)
    {
        js_std_free_handlers(runtime_);
        JS_FreeRuntime(runtime_);
    }
}

bool Runtime::isValid() const
{
    return runtime_ != nullptr && context_ != nullptr;
}

EvalResult Runtime::eval(const std::string& source)
{
    return evalInternal(source, false);
}

EvalResult Runtime::evalForRepl(const std::string& source)
{
    return evalInternal(normalizeSimpleReplDeclaration(source), true);
}

EvalResult Runtime::evalForDisplay(const std::string& source)
{
    return evalInternal(source, true, false);
}

EvalResult Runtime::emitSelectionEvent(const std::vector<std::string>& entityHandles)
{
    JSValue handles = JS_NewArray(context_);
    for (uint32_t i = 0; i < entityHandles.size(); ++i)
    {
        JS_SetPropertyUint32(
            context_,
            handles,
            i,
            JS_NewStringLen(context_, entityHandles[i].data(), entityHandles[i].size()));
    }

    JSValue global = JS_GetGlobalObject(context_);
    JS_SetPropertyStr(context_, global, "__arxSelectionHandles", handles);
    JS_FreeValue(context_, global);

    const char* source =
        "(() => {"
        "const root = globalThis.select;"
        "if (!root || typeof root !== 'object') return;"
        "const options = root.event;"
        "if (options === null || typeof options !== 'object' || Array.isArray(options)) return;"
        "const filter = options.filter;"
        "const out = options.out;"
        "const query = options.query;"
        "if (filter !== undefined && typeof filter !== 'function') throw new TypeError('select.event.filter must be a function or undefined');"
        "if (query !== undefined && typeof query !== 'function') throw new TypeError('select.event.query must be a function or undefined');"
        "if (query === undefined && out !== undefined && typeof out !== 'function') throw new TypeError('select.event.out must be a function or undefined');"
        "const entities = globalThis.__arxSelectionHandles"
        ".map(handle => entity.get(handle))"
        ".filter(value => value !== null && (filter === undefined || filter(value)));"
        "const values = query === undefined"
        "? entities.map(value => out === undefined ? value.id : out(value))"
        ": query(entities);"
        "if (options.file !== undefined) {"
        "if (typeof options.file !== 'string') throw new TypeError('select.event.file must be a string');"
        "let target = options.file;"
        "if (!path.isAbsolute(target)) {"
        "if (typeof root.path !== 'string') throw new TypeError('select.path must be a string when select.event.file is relative');"
        "target = path.resolve(root.path, target);"
        "}"
        "fs.writeFileSync(target, 'result = ' + inspect(values));"
        "return;"
        "}"
        "return values;"
        "})()";

    EvalResult result = evalForDisplay(source);
    global = JS_GetGlobalObject(context_);
    JS_SetPropertyStr(context_, global, "__arxSelectionHandles", JS_UNDEFINED);
    JS_FreeValue(context_, global);
    return result;
}

EvalResult Runtime::evalJson(const std::string& source)
{
    return evalJsonInternal(source);
}

EvalResult Runtime::evalInternal(const std::string& source, bool replDisplay, bool updateAnswerHistory)
{
    if (runtime_ == nullptr)
    {
        return {false, "failed to create JavaScript runtime"};
    }

    if (context_ == nullptr)
    {
        return {false, "failed to create JavaScript context"};
    }

    JSValue value = JS_Eval(
        context_,
        source.c_str(),
        source.size(),
        "<autocad-command>",
        JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC);

    if (!JS_IsException(value) && JS_IsPromise(value))
    {
        value = js_std_await(context_, value);
        if (!JS_IsException(value))
        {
            value = unwrapAsyncEvalResult(context_, value);
        }
    }

    JSContext* pendingContext = nullptr;
    while (JS_IsJobPending(runtime_))
    {
        if (JS_ExecutePendingJob(runtime_, &pendingContext) < 0)
        {
            break;
        }
    }

    EvalResult result;
    if (JS_IsException(value))
    {
        JSValue exception = JS_GetException(context_);
        result = {
            false,
            JS_IsNull(exception)
                ? "QuickJS ran out of memory while processing the selection"
                : valueToString(context_, exception)};
        JS_FreeValue(context_, exception);
    }
    else
    {
        if (replDisplay && updateAnswerHistory && !isAnswerHelperRead(source))
        {
            updateReplAnswerGlobals(context_, value);
        }

        result = {true, replDisplay ? valueToReplString(context_, value) : valueToString(context_, value)};
    }

    JS_FreeValue(context_, value);
    return result;
}

EvalResult Runtime::evalJsonInternal(const std::string& source)
{
    if (runtime_ == nullptr)
    {
        return {false, "{\"error\":\"failed to create JavaScript runtime\"}"};
    }

    if (context_ == nullptr)
    {
        return {false, "{\"error\":\"failed to create JavaScript context\"}"};
    }

    JSValue value = JS_Eval(
        context_,
        source.c_str(),
        source.size(),
        "<ipc-global-script>",
        JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC);

    if (!JS_IsException(value) && JS_IsPromise(value))
    {
        value = js_std_await(context_, value);
        if (!JS_IsException(value))
        {
            value = unwrapAsyncEvalResult(context_, value);
        }
    }

    JSContext* pendingContext = nullptr;
    while (JS_IsJobPending(runtime_))
    {
        if (JS_ExecutePendingJob(runtime_, &pendingContext) < 0)
        {
            break;
        }
    }

    EvalResult result;
    if (JS_IsException(value))
    {
        JSValue exception = JS_GetException(context_);
        const std::string message = valueToString(context_, exception);
        result = {false, std::string("{\"error\":") + jsonQuote(message) + "}"};
        JS_FreeValue(context_, exception);
    }
    else
    {
        JSValue json = JS_JSONStringify(context_, value, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json))
        {
            JSValue exception = JS_GetException(context_);
            const std::string message = valueToString(context_, exception);
            result = {false, std::string("{\"error\":") + jsonQuote(message) + "}"};
            JS_FreeValue(context_, exception);
        }
        else if (JS_IsUndefined(json))
        {
            result = {true, "null"};
        }
        else
        {
            result = {true, valueToString(context_, json)};
        }
        JS_FreeValue(context_, json);
    }

    JS_FreeValue(context_, value);
    return result;
}

EvalResult eval(const std::string& source)
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    if (!ensureRuntimeForCurrentThreadLocked())
    {
        return textRuntimeError("failed to create JavaScript runtime");
    }

    if (!isRuntimeOwnerThreadLocked())
    {
        return textRuntimeError("JavaScript runtime belongs to another thread");
    }

    return g_runtime->eval(source);
}

EvalResult evalForRepl(const std::string& source)
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    if (!ensureRuntimeForCurrentThreadLocked())
    {
        return textRuntimeError("failed to create JavaScript runtime");
    }

    if (!isRuntimeOwnerThreadLocked())
    {
        return textRuntimeError("JavaScript runtime belongs to another thread");
    }

    return g_runtime->evalForRepl(source);
}

EvalResult evalJson(const std::string& source)
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    if (!ensureRuntimeForCurrentThreadLocked())
    {
        return jsonRuntimeError("failed to create JavaScript runtime");
    }

    if (!isRuntimeOwnerThreadLocked())
    {
        return jsonRuntimeError("JavaScript runtime belongs to another thread");
    }

    return g_runtime->evalJson(source);
}

EvalResult emitSelectionEvent(const std::vector<std::string>& entityHandles)
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    if (!ensureRuntimeForCurrentThreadLocked())
    {
        return textRuntimeError("failed to create JavaScript runtime");
    }
    if (!isRuntimeOwnerThreadLocked())
    {
        return textRuntimeError("JavaScript runtime belongs to another thread");
    }

    EvalResult result = g_runtime->emitSelectionEvent(entityHandles);
    // Undefined means the event is disabled or output was written to a file.
    if (result.ok && result.text == "undefined")
    {
        result.text.clear();
    }
    return result;
}

bool initialize()
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    g_runtimePrepared = true;
    return true;
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    delete g_runtime;
    g_runtime = nullptr;
    g_runtimeThreadId = std::thread::id();
    g_runtimePrepared = false;
}

bool isInitialized()
{
    std::lock_guard<std::mutex> lock(g_runtimeMutex);
    return g_runtimePrepared && (g_runtime == nullptr || g_runtime->isValid());
}
}

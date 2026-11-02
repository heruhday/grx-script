#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

#include "aced.h"
#include "acutads.h"
#include "acedads.h"
#include "adscodes.h"
#include "acdocman.h"
#include "../command/commands.h"
#include "../command/text_util.h"
#include "js_engine.h"
#include "safe_commands.h"
#include "selection_event.h"
#include "rxdlinkr.h"
#include "rxregsvc.h"

namespace
{
    const ACHAR *const kCommandGroup = ACRX_T("ARX01_COMMANDS");

    enum LispFuncCodes
    {
        kLispJsEval = 1,
        kLispJsExecute = 2,
        kLispSsSet = 3,
        kLispESet = 4,
        kLispEGet = 5,
        kLispSsGet = 6,
        kLispSsQuery = 7,
        kLispEShow = 8
    };

    Acad::ErrorStatus registerCommand(const ACHAR *globalName, const ACHAR *localName, AcRxFunctionPtr function)
    {
        return acedRegCmds->addCommand(
            kCommandGroup,
            globalName,
            localName,
            ACRX_CMD_MODAL | ACRX_CMD_USEPICKSET | ACRX_CMD_REDRAW,
            function);
    }

    bool isScriptFilePath(const std::string &input)
    {
        if (input.empty())
        {
            return false;
        }

        // Check if ends with .js (case-insensitive)
        if (input.size() >= 3)
        {
            std::string ext = input.substr(input.size() - 3);
            if (_stricmp(ext.c_str(), ".js") == 0)
            {
                return true;
            }
        }

        // Check if file exists on disk
        const DWORD attr = GetFileAttributesA(input.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            return true;
        }

        return false;
    }

    std::string prepareSource(const std::string &input)
    {
        if (isScriptFilePath(input))
        {
            std::string escaped;
            for (char c : input)
            {
                if (c == '\\')
                {
                    escaped += "/";
                }
                else
                {
                    escaped += c;
                }
            }
            return "await load('" + escaped + "', false);";
        }
        return input;
    }

    // (js-eval "expression-or-file") -> returns JSON string (or nil on error)
    int lispJsEval(resbuf *rb)
    {
        if (rb == nullptr || rb->restype != RTSTR || rb->resval.rstring == nullptr)
        {
            acutPrintf(ACRX_T("\nError: (js-eval \"code-or-filepath\") expects a string argument."));
            acedRetNil();
            return RTERROR;
        }

        const std::string input = command_text::toUtf8(rb->resval.rstring);
        const std::string source = prepareSource(input);

        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (!pDoc)
        {
            acedRetNil();
            return RTERROR;
        }

        Acad::ErrorStatus lockStatus = acDocManager->lockDocument(pDoc, AcAp::kWrite);
        const bool locked = (lockStatus == Acad::eOk);

        const js_engine::EvalResult result = js_engine::evalJson(source);

        if (result.ok)
        {
            js_engine::eval("if (globalThis.transaction !== null) { tm.commit(); };");
            const std::basic_string<ACHAR> resStr = command_text::fromUtf8(result.text);
            acedRetStr(resStr.c_str());
        }
        else
        {
            js_engine::eval("if (globalThis.transaction !== null) { tm.abort(); }");
            acutPrintf(ACRX_T("\nJS Error in (js-eval): %s\n"), command_text::fromUtf8(result.text).c_str());
            acedRetNil();
        }

        if (locked)
        {
            acDocManager->unlockDocument(pDoc);
        }

        return result.ok ? RTNORM : RTERROR;
    }

    // (js-execute "code-or-file") -> NO RETURN (acedRetVoid)
    int lispJsExecute(resbuf *rb)
    {
        if (rb == nullptr || rb->restype != RTSTR || rb->resval.rstring == nullptr)
        {
            acutPrintf(ACRX_T("\nError: (js-execute \"code-or-filepath\") expects a string argument."));
            acedRetVoid();
            return RTERROR;
        }

        const std::string input = command_text::toUtf8(rb->resval.rstring);
        const std::string source = prepareSource(input);

        AcApDocument *pDoc = acDocManager ? acDocManager->curDocument() : nullptr;
        if (!pDoc)
        {
            acedRetVoid();
            return RTERROR;
        }

        Acad::ErrorStatus lockStatus = acDocManager->lockDocument(pDoc, AcAp::kWrite);
        const bool locked = (lockStatus == Acad::eOk);

        const js_engine::EvalResult result = js_engine::eval(source);

        if (result.ok)
        {
            js_engine::eval("if (globalThis.transaction !== null) { tm.commit(); };");
        }
        else
        {
            js_engine::eval("if (globalThis.transaction !== null) { tm.abort(); }");
            acutPrintf(ACRX_T("\nJS Error in (js-execute): %s\n"), command_text::fromUtf8(result.text).c_str());
        }

        if (locked)
        {
            acDocManager->unlockDocument(pDoc);
        }

        acedRetVoid();
        return result.ok ? RTNORM : RTERROR;
    }

    int dispatchLispSubr()
    {
        const int funcCode = acedGetFunCode();
        resbuf *rb = acedGetArgs();
        int status = RTERROR;

        switch (funcCode)
        {
        case kLispJsEval:
            status = lispJsEval(rb);
            break;
        case kLispJsExecute:
            status = lispJsExecute(rb);
            break;
        case kLispSsSet:
            status = safe_commands::lispSsSet(rb);
            break;
        case kLispESet:
            status = safe_commands::lispESet(rb);
            break;
        case kLispEGet:
            status = safe_commands::lispEGet(rb);
            break;
        case kLispSsGet:
            status = safe_commands::lispSsGet(rb);
            break;
        case kLispSsQuery:
            status = safe_commands::lispSsQuery(rb);
            break;
        case kLispEShow:
            status = safe_commands::lispEShow(rb);
            break;
        default:
            acutPrintf(ACRX_T("\nUnknown LISP function code: %d"), funcCode);
            acedRetNil();
            break;
        }

        if (rb != nullptr)
        {
            acutRelRb(rb);
        }

        return status;
    }

    void initApp(void *appId)
    {
        acrxDynamicLinker->unlockApplication(appId);
        acrxDynamicLinker->registerAppMDIAware(appId);

        const bool jsEngineReady = js_engine::initialize();
        selection_event::install();

        // Register AutoCAD Commands
        registerCommand(ACRX_T("HELLOARX"), ACRX_T("HELLOARX"), commands::helloArx);
        registerCommand(ACRX_T("js:eval"), ACRX_T("js:eval"), commands::jsEval);
        registerCommand(ACRX_T("js-eval"), ACRX_T("js-eval"), commands::jsEval);
        registerCommand(ACRX_T("js:execute"), ACRX_T("js:execute"), commands::jsExecute);
        registerCommand(ACRX_T("js-execute"), ACRX_T("js-execute"), commands::jsExecute);
        const Acad::ErrorStatus jsReplStatus = registerCommand(ACRX_T("js:repl"), ACRX_T("js:repl"), commands::jsRepl);
        const Acad::ErrorStatus jsIpcStatus = registerCommand(ACRX_T("js:server"), ACRX_T("js:server"), commands::jsServer);

        // Register Safe Transaction Commands
        registerCommand(ACRX_T("SS-SET"), ACRX_T("SS-SET"), safe_commands::cmdSsSet);
        registerCommand(ACRX_T("E-SET"), ACRX_T("E-SET"), safe_commands::cmdESet);
        registerCommand(ACRX_T("E-GET"), ACRX_T("E-GET"), safe_commands::cmdEGet);
        registerCommand(ACRX_T("SS-GET"), ACRX_T("SS-GET"), safe_commands::cmdSsGet);
        registerCommand(ACRX_T("SS-QUERY"), ACRX_T("SS-QUERY"), safe_commands::cmdSsQuery);
        registerCommand(ACRX_T("E-SHOW"), ACRX_T("E-SHOW"), safe_commands::cmdEShow);

        // Register AutoLISP callable functions:
        // (js-eval "...")    -> returns JSON value string
        // (js-execute "...") -> no return
        acedDefun(ACRX_T("js-eval"), kLispJsEval);
        acedDefun(ACRX_T("js:eval"), kLispJsEval);
        acedDefun(ACRX_T("js-execute"), kLispJsExecute);
        acedDefun(ACRX_T("js:execute"), kLispJsExecute);
        acedDefun(ACRX_T("ss-set"), kLispSsSet);
        acedDefun(ACRX_T("e-set"), kLispESet);
        acedDefun(ACRX_T("e-get"), kLispEGet);
        acedDefun(ACRX_T("ss-get"), kLispSsGet);
        acedDefun(ACRX_T("ss-query"), kLispSsQuery);
        acedDefun(ACRX_T("e-show"), kLispEShow);

        if (jsEngineReady && jsReplStatus == Acad::eOk && jsIpcStatus == Acad::eOk)
        {
            acutPrintf(ACRX_T("\narx01.arx loaded. AutoLISP: (js-eval \"expr\") -> JSON, (js-execute \"code\") -> no return.\n"));
        }
        else
        {
            acutPrintf(
                ACRX_T("\narx01.arx load issue. JS engine=%d js:repl=%d js:server=%d."),
                jsEngineReady ? 0 : 1,
                jsReplStatus,
                jsIpcStatus);
        }
    }

    void unloadApp()
    {
        // Unregister AutoLISP functions
        acedUndef(ACRX_T("js-eval"), kLispJsEval);
        acedUndef(ACRX_T("js:eval"), kLispJsEval);
        acedUndef(ACRX_T("js-execute"), kLispJsExecute);
        acedUndef(ACRX_T("js:execute"), kLispJsExecute);
        acedUndef(ACRX_T("ss-set"), kLispSsSet);
        acedUndef(ACRX_T("e-set"), kLispESet);
        acedUndef(ACRX_T("e-get"), kLispEGet);
        acedUndef(ACRX_T("ss-get"), kLispSsGet);
        acedUndef(ACRX_T("ss-query"), kLispSsQuery);
        acedUndef(ACRX_T("e-show"), kLispEShow);

        commands::stopJsIpcServer();
        selection_event::uninstall();
        js_engine::shutdown();
        acedRegCmds->removeGroup(kCommandGroup);
        acutPrintf(ACRX_T("\narx01.arx unloaded.\n"));
    }
}

extern "C" AcRx::AppRetCode acrxEntryPoint(AcRx::AppMsgCode msg, void *appId)
{
    switch (msg)
    {
    case AcRx::kInitAppMsg:
        initApp(appId);
        break;

    case AcRx::kUnloadAppMsg:
        unloadApp();
        break;

    case AcRx::kInvkSubrMsg:
        dispatchLispSubr();
        break;

    default:
        break;
    }

    return AcRx::kRetOK;
}

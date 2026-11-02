#pragma once

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
#include "acutads.h"
#include "acedads.h"
#include "adscodes.h"
#include "dbmain.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4800)
#endif
#include "quickjs.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace safe_commands
{
    // AutoCAD Commands (Modal, USEPICKSET, REDRAW)
    void cmdSsSet();
    void cmdESet();
    void cmdEGet();
    void cmdSsGet();
    void cmdSsQuery();
    void cmdEShow();

    // AutoLISP Callable Subroutines
    int lispSsSet(resbuf *rb);
    int lispESet(resbuf *rb);
    int lispEGet(resbuf *rb);
    int lispSsGet(resbuf *rb);
    int lispSsQuery(resbuf *rb);
    int lispEShow(resbuf *rb);

    // QuickJS Function Handlers
    JSValue jsSafeSsSet(JSContext *context, JSValueConst this_val, int argc, JSValueConst *argv);
    JSValue jsSafeESet(JSContext *context, JSValueConst this_val, int argc, JSValueConst *argv);
    JSValue jsSafeEGet(JSContext *context, JSValueConst this_val, int argc, JSValueConst *argv);
    JSValue jsSafeSsGet(JSContext *context, JSValueConst this_val, int argc, JSValueConst *argv);
    JSValue jsSafeSsQuery(JSContext *context, JSValueConst this_val, int argc, JSValueConst *argv);
    JSValue jsSafeEShow(JSContext *context, JSValueConst this_val, int argc, JSValueConst *argv);

    // Registration helpers for JS modules
    void registerSafeCommands(JSContext *context, JSValue targetObj, bool isEntityModule = false);
}

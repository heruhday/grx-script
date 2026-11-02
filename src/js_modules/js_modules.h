#pragma once

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4800)
#endif
#include "quickjs.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

struct JSContext;

namespace js_modules
{
    void installUtilities(JSContext *context);
    void installConsole(JSContext *context);
    void installEditor(JSContext *context);
    void installRequire(JSContext *context);
    void installMsgpack(JSContext *context);
    void installTransactionManager(JSContext *context);
    void installEntity(JSContext *context);
    void uninstallDialog();
    JSValue encodeMsgpackValue(JSContext *context, JSValueConst value);
    JSValue decodeMsgpackBytes(JSContext *context, const void *bytes, unsigned long long size);
    JSValue createPathModule(JSContext *context);
    JSValue createSqliteModule(JSContext *context);
    JSValue createProcessModule(JSContext *context);
    JSValue createExcelModule(JSContext *context);
    JSValue createColorModule(JSContext *context);
    JSValue createDialogModule(JSContext *context);
    JSValue createLayerModule(JSContext *context);
    JSValue createEntityModule(JSContext *context);
    JSValue createNetModule(JSContext *context);
}

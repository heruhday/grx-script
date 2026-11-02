#include "js_modules.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdio>
#include <string>

#include "aced.h"
#include "adscodes.h"
#include "dbapserv.h"
#include "dbmain.h"
#include "dbtrans.h"
#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        JSValue getGlobalProperty(JSContext* context, const char* name)
        {
            JSValue global = JS_GetGlobalObject(context);
            JSValue value = JS_GetPropertyStr(context, global, name);
            JS_FreeValue(context, global);
            return value;
        }

        void setGlobalProperty(JSContext* context, const char* name, JSValue value)
        {
            JSValue global = JS_GetGlobalObject(context);
            JS_SetPropertyStr(context, global, name, value);
            JS_FreeValue(context, global);
        }

        bool hasActiveJsTransaction(JSContext* context)
        {
            JSValue transaction = getGlobalProperty(context, "transaction");
            bool active = !JS_IsNull(transaction) && !JS_IsUndefined(transaction);
            JS_FreeValue(context, transaction);
            return active;
        }

        void setJsTransactionActive(JSContext* context)
        {
            JSValue transaction = JS_NewObject(context);
            setGlobalProperty(context, "transaction", transaction);
        }

        void clearJsTransaction(JSContext* context)
        {
            setGlobalProperty(context, "transaction", JS_NULL);
        }

        JSValue tmStart(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv)
        {
            (void)this_val;
            (void)argc;
            (void)argv;

            if (hasActiveJsTransaction(context))
            {
                return JS_UNDEFINED;
            }

            AcDbDatabase* workingDb = acdbHostApplicationServices()->workingDatabase();
            if (workingDb == nullptr)
            {
                return JS_ThrowInternalError(context, "No working database available");
            }

            AcDbTransactionManager* txManager = workingDb->transactionManager();
            if (txManager == nullptr)
            {
                return JS_ThrowInternalError(context, "Failed to get transaction manager");
            }

            AcTransaction* transaction = txManager->startTransaction();
            if (transaction == nullptr)
            {
                return JS_ThrowInternalError(context, "Failed to start transaction");
            }

            setJsTransactionActive(context);
            return JS_UNDEFINED;
        }

        JSValue tmCommit(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv)
        {
            (void)this_val;
            (void)argc;
            (void)argv;

            if (!hasActiveJsTransaction(context))
            {
                return JS_FALSE;
            }

            AcDbDatabase* workingDb = acdbHostApplicationServices()->workingDatabase();
            if (workingDb == nullptr)
            {
                return JS_FALSE;
            }

            AcDbTransactionManager* txManager = workingDb->transactionManager();
            if (txManager == nullptr)
            {
                return JS_FALSE;
            }

            AcTransaction* transaction = txManager->topTransaction();
            if (transaction == nullptr)
            {
                clearJsTransaction(context);
                return JS_FALSE;
            }

            txManager->endTransaction();
            clearJsTransaction(context);
            return JS_TRUE;
        }

        JSValue tmAbort(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv)
        {
            (void)this_val;
            (void)argc;
            (void)argv;

            if (!hasActiveJsTransaction(context))
            {
                return JS_FALSE;
            }

            AcDbDatabase* workingDb = acdbHostApplicationServices()->workingDatabase();
            if (workingDb == nullptr)
            {
                return JS_FALSE;
            }

            AcDbTransactionManager* txManager = workingDb->transactionManager();
            if (txManager == nullptr)
            {
                return JS_FALSE;
            }

            AcTransaction* transaction = txManager->topTransaction();
            if (transaction == nullptr)
            {
                clearJsTransaction(context);
                return JS_FALSE;
            }

            txManager->abortTransaction();
            clearJsTransaction(context);
            return JS_TRUE;
        }

        JSValue tmUpdate(JSContext* context, JSValueConst this_val, int argc, JSValueConst* argv)
        {
            (void)this_val;
            (void)argc;
            (void)argv;

            AcDbDatabase* workingDb = acdbHostApplicationServices()->workingDatabase();
            if (workingDb == nullptr)
            {
                return JS_ThrowInternalError(context, "No working database available");
            }

            AcDbTransactionManager* txManager = workingDb->transactionManager();
            if (txManager == nullptr)
            {
                return JS_ThrowInternalError(context, "Failed to get transaction manager");
            }

            if (hasActiveJsTransaction(context))
            {
                AcTransaction* transaction = txManager->topTransaction();
                if (transaction != nullptr)
                {
                    txManager->endTransaction();
                }
            }

            AcTransaction* transaction = txManager->startTransaction();
            if (transaction == nullptr)
            {
                return JS_ThrowInternalError(context, "Failed to start transaction");
            }

            setJsTransactionActive(context);
            return JS_TRUE;
        }

        JSValue createTransactionManager(JSContext* context)
        {
            JSValue tm = JS_NewObject(context);
            JS_SetPropertyStr(context, tm, "start", JS_NewCFunction(context, tmStart, "start", 0));
            JS_SetPropertyStr(context, tm, "commit", JS_NewCFunction(context, tmCommit, "commit", 0));
            JS_SetPropertyStr(context, tm, "abort", JS_NewCFunction(context, tmAbort, "abort", 0));
            JS_SetPropertyStr(context, tm, "update", JS_NewCFunction(context, tmUpdate, "update", 0));
            return tm;
        }
    }

    void installTransactionManager(JSContext* context)
    {
        JSValue global = JS_GetGlobalObject(context);
        
        JS_SetPropertyStr(context, global, "tm", JS_NULL);
        JS_SetPropertyStr(context, global, "workingDb", JS_NULL);
        JS_SetPropertyStr(context, global, "transaction", JS_NULL);
        
        JSValue tm = createTransactionManager(context);
        JS_SetPropertyStr(context, global, "tm", tm);
        
        JS_FreeValue(context, global);
    }
}

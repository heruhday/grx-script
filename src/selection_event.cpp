#include "selection_event.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tchar.h>

#include "AdAChar.h"
#include "aced.h"
#include "acedads.h"
#include "adscodes.h"
#include "acdbads.h"
#include "acutads.h"
#include "dbents.h"
#include "dbidmap.h"

#include "js_engine.h"
#include "../command/text_util.h"

#ifndef ACRX_T
#define _ACRX_T(x) L ## x
#define ACRX_T(x)  _ACRX_T(x)
#endif

namespace
{
    class SelectionEventReactor : public AcEditorReactor
    {
    public:
        void pickfirstModified() override
        {
            static bool s_inReactor = false;
            if (s_inReactor)
            {
                return;
            }

            // Never run while any command, script, or LISP is currently executing in AutoCAD!
            resbuf rb;
            if (acedGetVar(ACRX_T("CMDACTIVE"), &rb) == RTNORM)
            {
                if (rb.resval.rint != 0)
                {
                    return;
                }
            }

            s_inReactor = true;

            std::vector<std::string> handles;
            ads_name selectionSet;
            acdbNameClear(selectionSet);

            if (acedSSGet(ACRX_T("I"), nullptr, nullptr, nullptr, selectionSet) == RTNORM)
            {
                Adesk::Int32 count = 0;
                if (acedSSLength(selectionSet, &count) == RTNORM)
                {
                    handles.reserve(static_cast<size_t>(count));
                    for (Adesk::Int32 i = 0; i < count; ++i)
                    {
                        ads_name entityName;
                        acdbNameClear(entityName);
                        if (acedSSName(selectionSet, i, entityName) != RTNORM)
                        {
                            continue;
                        }

                        AcDbObjectId objectId;
                        if (acdbGetObjectId(objectId, entityName) != Acad::eOk)
                        {
                            continue;
                        }

                        char text[32] = {};
                        std::sprintf(
                            text,
                            "%llX",
                            static_cast<unsigned long long>(
                                static_cast<Adesk::UInt64>(objectId.handle())));
                        handles.push_back(text);
                    }
                }
                acedSSFree(selectionSet);
            }

            const js_engine::EvalResult result = js_engine::emitSelectionEvent(handles);
            if (result.ok)
            {
                if (!result.text.empty())
                {
                    const std::basic_string<ACHAR> output = command_text::fromUtf8(result.text);
                    acutPrintf(ACRX_T("\nresult: %s"), output.c_str());
                }
            }
            else
            {
                const std::basic_string<ACHAR> output = command_text::fromUtf8(result.text);
                acutPrintf(ACRX_T("\nselect.event error: %s"), output.c_str());
            }

            s_inReactor = false;
        }
    };

    SelectionEventReactor* g_reactor = nullptr;
}

namespace selection_event
{
    void install()
    {
        if (g_reactor == nullptr)
        {
            g_reactor = new SelectionEventReactor();
            acedEditor->addReactor(g_reactor);
        }
    }

    void uninstall()
    {
        if (g_reactor != nullptr)
        {
            acedEditor->removeReactor(g_reactor);
            delete g_reactor;
            g_reactor = nullptr;
        }
    }
}


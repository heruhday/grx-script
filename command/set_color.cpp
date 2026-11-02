#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "aced.h"
#include "adscodes.h"
#include "acutads.h"
#include "dbmain.h"
#include "commands.h"

namespace commands
{
void setEntityColor()
{
    ads_name entityName;
    ads_point pickPoint;

    if (acedEntSel(ACRX_T("\nSelect entity to set color: "), entityName, pickPoint) != RTNORM)
    {
        acutPrintf(ACRX_T("\nNo entity selected."));
        return;
    }

    int colorIndex = 0;
    if (acedGetInt(ACRX_T("\nEnter color index [0=BYBLOCK, 1-255=ACI, 256=BYLAYER]: "), &colorIndex) != RTNORM)
    {
        acutPrintf(ACRX_T("\nColor change canceled."));
        return;
    }

    if (colorIndex < 0 || colorIndex > 256)
    {
        acutPrintf(ACRX_T("\nInvalid color index. Use 0 through 256."));
        return;
    }

    AcDbObjectId objectId;
    Acad::ErrorStatus status = acdbGetObjectId(objectId, entityName);
    if (status != Acad::eOk)
    {
        acutPrintf(ACRX_T("\nFailed to get selected entity id. Error code: %d."), status);
        return;
    }

    AcDbEntity* entity = nullptr;
    status = acdbOpenObject(entity, objectId, AcDb::kForWrite);
    if (status != Acad::eOk || entity == nullptr)
    {
        acutPrintf(ACRX_T("\nFailed to open selected entity for write. Error code: %d."), status);
        return;
    }

    status = entity->setColorIndex(static_cast<Adesk::UInt16>(colorIndex));
    entity->close();

    if (status == Acad::eOk)
    {
        acutPrintf(ACRX_T("\nEntity color set to %d."), colorIndex);
    }
    else
    {
        acutPrintf(ACRX_T("\nFailed to set entity color. Error code: %d."), status);
    }
}
}

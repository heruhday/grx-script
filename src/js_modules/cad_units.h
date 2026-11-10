#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "adscodes.h"
#include "acedads.h"
#include "acdbads.h"
#include "dbmain.h"

#ifndef ACRX_T
#define _ACRX_T(x) L ## x
#define ACRX_T(x)  _ACRX_T(x)
#endif

namespace js_modules
{
    inline double getCadModelUnitScaleToMetre(AcDbDatabase *pDb = nullptr)
    {
        short insunits = 0;
        struct resbuf rb;
        if (acedGetVar(ACRX_T("INSUNITS"), &rb) == RTNORM)
        {
            if (rb.restype == RTSHORT)
            {
                insunits = rb.resval.rint;
            }
            else if (rb.restype == RTLONG)
            {
                insunits = static_cast<short>(rb.resval.rlong);
            }
        }
#ifndef GSTARCAD
        else if (pDb != nullptr)
        {
            insunits = static_cast<short>(pDb->insunits());
        }
#endif

        if (insunits == 0)
        {
            // Fallback: check LUNITS for Architectural (4) or Engineering (3) which use inches
            struct resbuf rbLunits;
            if (acedGetVar(ACRX_T("LUNITS"), &rbLunits) == RTNORM)
            {
                short lunits = (rbLunits.restype == RTSHORT) ? rbLunits.resval.rint : static_cast<short>(rbLunits.resval.rlong);
                if (lunits == 3 || lunits == 4)
                {
                    return 0.0254; // Inches -> Metre
                }
            }
            return 1.0; // Unspecified / Unitless -> 1.0
        }

        switch (insunits)
        {
        case 1:  return 0.0254;             // Inches -> Metre
        case 2:  return 0.3048;             // Feet -> Metre
        case 3:  return 1609.344;           // Miles -> Metre
        case 4:  return 0.001;              // Millimeters -> Metre
        case 5:  return 0.01;               // Centimeters -> Metre
        case 6:  return 1.0;                // Meters -> Metre
        case 7:  return 1000.0;             // Kilometers -> Metre
        case 8:  return 2.54e-8;            // Microinches -> Metre
        case 9:  return 2.54e-5;            // Mils -> Metre
        case 10: return 0.9144;             // Yards -> Metre
        case 11: return 1e-10;              // Angstroms -> Metre
        case 12: return 1e-9;               // Nanometers -> Metre
        case 13: return 1e-6;               // Microns -> Metre
        case 14: return 0.1;                // Decimeters -> Metre
        case 15: return 10.0;               // Decameters -> Metre
        case 16: return 100.0;              // Hectometers -> Metre
        case 17: return 1e9;                // Gigameters -> Metre
        case 18: return 149597870700.0;     // Astronomical Units -> Metre
        case 19: return 9.4607304725808e15; // Light Years -> Metre
        case 20: return 3.08567758149137e16;// Parsecs -> Metre
        default:
            return 1.0;
        }
    }
}

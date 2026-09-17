// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimDta2110.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - What the emulated DTA-2110 is: its identity, properties and functions
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "DtPcieAbi.h"  // PROPERTY_VALUE_TYPE_ values and UUID flags.
#include "SimDta2110.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The capabilities of the IP port.
static const char* const g_PortCaps[] = {
    "CAP_AVFIFO", "CAP_IP", "CAP_PTP", "CAP_SFP10G", "CAP_ST2110", "CAP_TS",
};

// The network function's UUID: the first driver function.
#define SIM_DTA2110_NW_UUID (DT_UUID_DF_FLAG | 1)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsPort(int PortIndex)
{
    return PortIndex >= 0 && PortIndex < SIM_DTA2110_PORT_COUNT;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2110_GetProperty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimDta2110_GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value)
{
    if (PortIndex == -1 &&
        (strcmp(Name, "PORT_COUNT") == 0 || strcmp(Name, "MAIN_PORT_COUNT") == 0))
    {
        *Type = PROPERTY_VALUE_TYPE_INT;
        *Value = SIM_DTA2110_PORT_COUNT;
        return true;
    }

    if (strncmp(Name, "CAP_", 4) == 0)
    {
        bool Has = false;

        for (size_t i = 0;
             IsPort(PortIndex) && i < sizeof(g_PortCaps) / sizeof(g_PortCaps[0]); i++)
        {
            Has = Has || strcmp(g_PortCaps[i], Name) == 0;
        }
        *Type = PROPERTY_VALUE_TYPE_BOOL;
        *Value = Has ? 1 : 0;
        return true;
    }

    if (!IsPort(PortIndex))
        return false;
    if (strcmp(Name, "DF_NW#1_TYPE") == 0)
    {
        *Type = PROPERTY_VALUE_TYPE_INT;
        *Value = DT_FUNC_TYPE_NW;
        return true;
    }
    if (strcmp(Name, "DF_NW#1_UUID") == 0)
    {
        *Type = PROPERTY_VALUE_TYPE_INT;
        *Value = SIM_DTA2110_NW_UUID;
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2110_GetString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// AF_NW#1 gives its role, AF_NW#1.1 its part, and DF_NW#1 the part's role.
//
bool SimDta2110_GetString(const char* Name, int PortIndex, const char** Str)
{
    if (!IsPort(PortIndex))
        return false;

    if (strcmp(Name, "AF_NW#1") == 0 || strcmp(Name, "DF_NW#1") == 0)
    {
        *Str = "";
        return true;
    }
    if (strcmp(Name, "AF_NW#1.1") == 0)
    {
        *Str = "DF_NW#1";
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2110_FindFunction -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDta2110_FindFunction(int Uuid, int* PortIndex, int* Type, const char** Role)
{
    if ((Uuid & (DT_UUID_FLAG_MASK | DT_UUID_INDEX_MASK)) != SIM_DTA2110_NW_UUID)
        return false;

    *PortIndex = 0;
    *Type = DT_FUNC_TYPE_NW;
    *Role = "";
    return true;
}

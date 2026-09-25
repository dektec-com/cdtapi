// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtFunc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Device layer: the objects of an API function - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "DtFunc.h"    // Interface being implemented.
#include "DtPcieAbi.h" // DT_FUNC_TYPE_ values.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Discovery +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindInstanceNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult FindInstanceNumber(OsDrv* Drv, int PortIndex, const char* Name,
                                      const char* Role, int* Number)
{
    char PropertyName[PROPERTY_NAME_MAX_SIZE];
    char InstanceRole[DT_PROPERTY_STR_SIZE];

    for (int InstanceNumber = 1;; InstanceNumber++)
    {
        if (snprintf(PropertyName, sizeof(PropertyName), "%s#%d", Name, InstanceNumber) >=
            (int)sizeof(PropertyName))
            return DTAPI_E_BUF_TOO_SMALL;
        DtapiResult Result = DtPcieCmd_GetPropertyStr(Drv, PropertyName, PortIndex,
                                                      InstanceRole, sizeof(InstanceRole));
        if (Result != DTAPI_OK)
            return Result;
        if (strcmp(InstanceRole, Role) == 0)
        {
            *Number = InstanceNumber;
            return DTAPI_OK;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadObject -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the role, the type and the UUID of the object Object->Name names, in that order,
// and tells a driver function from a building block by the name's prefix. False when any
// of it cannot be had, in which case the object is left out.
//
static bool ReadObject(OsDrv* Drv, int PortIndex, DtFuncObject* Object)
{
    if (DtPcieCmd_GetPropertyStr(Drv, Object->Name, PortIndex, Object->Role,
                                 sizeof(Object->Role)) != DTAPI_OK)
    {
        return false;
    }

    char PropertyName[PROPERTY_NAME_MAX_SIZE];
    if (snprintf(PropertyName, sizeof(PropertyName), "%s_TYPE", Object->Name) >=
            (int)sizeof(PropertyName) ||
        DtPcieCmd_GetPropertyInt(Drv, PropertyName, PortIndex,
                                 &Object->FuncOrBlockType) != DTAPI_OK)
    {
        return false;
    }

    if (strncmp(Object->Name, "DF_", 3) == 0)
        Object->IsDriverFunction = true;
    else if (strncmp(Object->Name, "BC_", 3) == 0)
        Object->IsDriverFunction = false;
    else
        return false;

    if (snprintf(PropertyName, sizeof(PropertyName), "%s_UUID", Object->Name) >=
            (int)sizeof(PropertyName) ||
        DtPcieCmd_GetPropertyInt(Drv, PropertyName, PortIndex, &Object->Object.Uuid) !=
            DTAPI_OK)
    {
        return false;
    }
    Object->Object.PortIndex = PortIndex;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFunc_Find -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtFunc_Find(OsDrv* Drv, int PortIndex, const char* Name, const char* Role,
                        DtFuncInstance* Instance)
{
    int Number = 0;

    DtVec_Init(&Instance->Objects, sizeof(DtFuncObject));

    DtapiResult Result = FindInstanceNumber(Drv, PortIndex, Name, Role, &Number);
    if (Result != DTAPI_OK)
        return Result;

    char PropertyName[PROPERTY_NAME_MAX_SIZE];
    for (int ObjectNumber = 1;; ObjectNumber++)
    {
        DtFuncObject Object;

        memset(&Object, 0, sizeof(Object));
        if (snprintf(PropertyName, sizeof(PropertyName), "%s#%d.%d", Name, Number,
                     ObjectNumber) >= (int)sizeof(PropertyName))
            Result = DTAPI_E_BUF_TOO_SMALL;
        else
            Result = DtPcieCmd_GetPropertyStr(Drv, PropertyName, PortIndex, Object.Name,
                                              sizeof(Object.Name));

        if (Result == DTAPI_E_NOT_FOUND)
            return DTAPI_OK;
        if (Result != DTAPI_OK)
            break;

        if (ReadObject(Drv, PortIndex, &Object) &&
            DtVec_Push(&Instance->Objects, &Object) != 0)
        {
            Result = DTAPI_E_OUT_OF_MEM;
            break;
        }
    }

    DtFunc_Release(Instance);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFunc_Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtFunc_Release(DtFuncInstance* Instance)
{
    DtVec_Free(&Instance->Objects);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFunc_FindObject -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtFuncObject* DtFunc_FindObject(const DtFuncInstance* Instance,
                                      bool IsDriverFunction, int FuncOrBlockType,
                                      const char* Role)
{
    size_t i = DtVec_Count(&Instance->Objects);

    while (i-- > 0)
    {
        const DtFuncObject* Object = &DT_VEC_AT(&Instance->Objects, DtFuncObject, i);

        if (Object->IsDriverFunction == IsDriverFunction &&
            Object->FuncOrBlockType == FuncOrBlockType && strcmp(Object->Role, Role) == 0)
            return Object;
    }
    return NULL;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exclusive access +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFunc_ExclAccess -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtFunc_ExclAccess(OsDrv* Drv, const DtFuncInstance* Instance, int Cmd)
{
    size_t Count = DtVec_Count(&Instance->Objects);
    DtapiResult Result = DTAPI_OK;
    size_t i;

    for (i = 0;
         i < Count && (Result == DTAPI_OK || Cmd == DT_EXCLUSIVE_ACCESS_CMD_RELEASE); i++)
    {
        const DtFuncObject* Object = &DT_VEC_AT(&Instance->Objects, DtFuncObject, i);
        DtapiResult ObjectResult = DtPcieCmd_ExclAccess(Drv, Object->Object, Cmd);

        if (Result == DTAPI_OK && ObjectResult != DTAPI_E_NOT_SUPPORTED)
            Result = ObjectResult;
    }

    if (Result != DTAPI_OK && Cmd == DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE)
    {
        size_t NumAcquired = i - 1;

        for (i = 0; i < NumAcquired; i++)
        {
            const DtFuncObject* Object = &DT_VEC_AT(&Instance->Objects, DtFuncObject, i);

            DtPcieCmd_ExclAccess(Drv, Object->Object, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        }
    }
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver versions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The oldest DtPcie driver each object works with.
static const struct
{
    bool IsDriverFunction;
    int Type;
    DtDriverVersion Minimum;
} g_MinDriverVersions[] = {
    {true, DT_FUNC_TYPE_ASIRX, {1, 0, 4, 48}},
    {true, DT_FUNC_TYPE_CHSDIRX, {2, 0, 2, 328}},
    {true, DT_FUNC_TYPE_GENLOCKCTRL, {1, 1, 0, 60}},
    {true, DT_FUNC_TYPE_NW, {2, 0, 0, 1}},
    {true, DT_FUNC_TYPE_SDIRX, {1, 4, 0, 111}},
    {true, DT_FUNC_TYPE_SDITXPHY, {1, 5, 4, 143}},
    {true, DT_FUNC_TYPE_TODCLKCTRL, {1, 13, 19, 296}},
    {false, DT_BLOCK_TYPE_ASITXG, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_ASITXSER, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_BURSTFIFO, {1, 0, 5, 50}},
    {false, DT_BLOCK_TYPE_CDMAC, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_CLKCNT, {3, 2, 0, 357}},
    {false, DT_BLOCK_TYPE_SDIDMX12G, {1, 2, 1, 68}},
    {false, DT_BLOCK_TYPE_SDITXF, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_SDITXP, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_SWITCH, {1, 0, 4, 48}},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFunc_CheckDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtFunc_CheckDriverVersion(const DtDriverVersion* Version,
                                      bool IsDriverFunction, int Type)
{
    for (size_t i = 0; i < sizeof(g_MinDriverVersions) / sizeof(g_MinDriverVersions[0]);
         i++)
    {
        const DtDriverVersion* Min = &g_MinDriverVersions[i].Minimum;

        if (g_MinDriverVersions[i].IsDriverFunction != IsDriverFunction ||
            g_MinDriverVersions[i].Type != Type)
            continue;
        return DtPcieCmd_VersionAtLeast(Version, Min->Major, Min->Minor, Min->Micro,
                                        Min->Build)
                   ? DTAPI_OK
                   : DTAPI_E_DRIVER_INCOMP;
    }
    return DTAPI_E_INTERNAL;
}

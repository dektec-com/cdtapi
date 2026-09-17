// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtFunc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Device layer: the parts of an API function - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDtapiLite includes
#include "DtFunc.h"    // Interface being implemented.
#include "DtPcieAbi.h" // DT_FUNC_TYPE_ values.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Discovery +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindInstance -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static unsigned int FindInstance(OsDrv* Drv, int PortIndex, const char* Name,
                                 const char* Role, int* Instance)
{
    char Key[PROPERTY_NAME_MAX_SIZE];
    char InstanceRole[DT_PROPERTY_STR_SIZE];
    int N;

    for (N = 1;; N++)
    {
        unsigned int Result;

        if (snprintf(Key, sizeof(Key), "%s#%d", Name, N) >= (int)sizeof(Key))
            return DTAPI_E_BUF_TOO_SMALL;
        Result = DtPcieCmdGetPropertyStr(Drv, Key, PortIndex, InstanceRole,
                                         sizeof(InstanceRole));
        if (Result != DTAPI_OK)
            return Result;
        if (strcmp(InstanceRole, Role) == 0)
        {
            *Instance = N;
            return DTAPI_OK;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtProxyFactory::CreateProxy: the role, the type and the UUID, in that order, and a
// name that says which kind of part it is. False when any of it cannot be had, in which
// case DTAPI makes no proxy.
//
static bool ReadPart(OsDrv* Drv, int PortIndex, DtFuncPart* Part)
{
    char Key[PROPERTY_NAME_MAX_SIZE];

    if (DtPcieCmdGetPropertyStr(Drv, Part->Name, PortIndex, Part->Role,
                                sizeof(Part->Role)) != DTAPI_OK)
    {
        return false;
    }

    if (snprintf(Key, sizeof(Key), "%s_TYPE", Part->Name) >= (int)sizeof(Key) ||
        DtPcieCmdGetPropertyInt(Drv, Key, PortIndex, &Part->Type) != DTAPI_OK)
    {
        return false;
    }

    if (strncmp(Part->Name, "DF_", 3) == 0)
        Part->IsDf = true;
    else if (strncmp(Part->Name, "BC_", 3) == 0)
        Part->IsDf = false;
    else
        return false;

    if (snprintf(Key, sizeof(Key), "%s_UUID", Part->Name) >= (int)sizeof(Key) ||
        DtPcieCmdGetPropertyInt(Drv, Key, PortIndex, &Part->Uuid) != DTAPI_OK)
    {
        return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFuncFind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtFuncFind(OsDrv* Drv, int PortIndex, const char* Name, const char* Role,
                        DtFuncInstance* Instance)
{
    char Key[PROPERTY_NAME_MAX_SIZE];
    int Number = 0;
    int K;
    unsigned int Result;

    Instance->PortIndex = PortIndex;
    DtVecInit(&Instance->Parts, sizeof(DtFuncPart));

    Result = FindInstance(Drv, PortIndex, Name, Role, &Number);
    if (Result != DTAPI_OK)
        return Result;

    for (K = 1;; K++)
    {
        DtFuncPart Part;

        memset(&Part, 0, sizeof(Part));
        if (snprintf(Key, sizeof(Key), "%s#%d.%d", Name, Number, K) >= (int)sizeof(Key))
            Result = DTAPI_E_BUF_TOO_SMALL;
        else
            Result = DtPcieCmdGetPropertyStr(Drv, Key, PortIndex, Part.Name,
                                             sizeof(Part.Name));

        if (Result == DTAPI_E_NOT_FOUND)
            return DTAPI_OK;
        if (Result != DTAPI_OK)
            break;

        if (ReadPart(Drv, PortIndex, &Part) && DtVecPush(&Instance->Parts, &Part) != 0)
        {
            Result = DTAPI_E_OUT_OF_MEM;
            break;
        }
    }

    DtFuncRelease(Instance);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFuncRelease -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtFuncRelease(DtFuncInstance* Instance)
{
    DtVecFree(&Instance->Parts);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFuncGet -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtFuncPart* DtFuncGet(const DtFuncInstance* Instance, bool IsDf, int Type,
                            const char* Role)
{
    size_t i = DtVecCount(&Instance->Parts);

    while (i-- > 0)
    {
        const DtFuncPart* Part = &DT_VEC_AT(&Instance->Parts, DtFuncPart, i);

        if (Part->IsDf == IsDf && Part->Type == Type && strcmp(Part->Role, Role) == 0)
            return Part;
    }
    return NULL;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exclusive access +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFuncExclAccess -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtFuncExclAccess(OsDrv* Drv, const DtFuncInstance* Instance, int Cmd)
{
    size_t Count = DtVecCount(&Instance->Parts);
    unsigned int Result = DTAPI_OK;
    size_t i;

    for (i = 0;
         i < Count && (Result == DTAPI_OK || Cmd == DT_EXCLUSIVE_ACCESS_CMD_RELEASE); i++)
    {
        const DtFuncPart* Part = &DT_VEC_AT(&Instance->Parts, DtFuncPart, i);
        unsigned int PartResult =
            DtPcieCmdExclAccess(Drv, Part->Uuid, Instance->PortIndex, Cmd);

        if (Result == DTAPI_OK && PartResult != DTAPI_E_NOT_SUPPORTED)
            Result = PartResult;
    }

    if (Result != DTAPI_OK && Cmd == DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE)
    {
        size_t Acquired = i - 1;

        for (i = 0; i < Acquired; i++)
        {
            const DtFuncPart* Part = &DT_VEC_AT(&Instance->Parts, DtFuncPart, i);

            DtPcieCmdExclAccess(Drv, Part->Uuid, Instance->PortIndex,
                                DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
        }
    }
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver versions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The oldest DtPcie driver each proxy works with, from DTAPI's PROXY_MIN_DRV_VERSIONS.
static const struct
{
    bool IsDf;
    int Type;
    DtDriverVersion Minimum;
} g_MinDriverVersions[] = {
    {true, DT_FUNC_TYPE_ASIRX, {1, 0, 4, 48}},
    {true, DT_FUNC_TYPE_CHSDIRX, {2, 0, 2, 328}},
    {true, DT_FUNC_TYPE_SDIRX, {1, 4, 0, 111}},
    {true, DT_FUNC_TYPE_SDITXPHY, {1, 5, 4, 143}},
    {false, DT_BLOCK_TYPE_BURSTFIFO, {1, 0, 5, 50}},
    {false, DT_BLOCK_TYPE_CDMAC, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_SDIDMX12G, {1, 2, 1, 68}},
    {false, DT_BLOCK_TYPE_SDITXF, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_SDITXP, {1, 0, 4, 48}},
    {false, DT_BLOCK_TYPE_SWITCH, {1, 0, 4, 48}},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFuncCheckDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtFuncCheckDriverVersion(const DtDriverVersion* Version, bool IsDf, int Type)
{
    size_t i;

    for (i = 0; i < sizeof(g_MinDriverVersions) / sizeof(g_MinDriverVersions[0]); i++)
    {
        const DtDriverVersion* Min = &g_MinDriverVersions[i].Minimum;

        if (g_MinDriverVersions[i].IsDf != IsDf || g_MinDriverVersions[i].Type != Type)
            continue;
        return DtPcieCmdVersionAtLeast(Version, Min->Major, Min->Minor, Min->Micro,
                                       Min->Build)
                   ? DTAPI_OK
                   : DTAPI_E_DRIVER_INCOMP;
    }
    return DTAPI_E_INTERNAL;
}

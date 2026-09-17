// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtIoConfig.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Translation between I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h" // DTAPI_IOCONFIG_ codes and result codes.
#include "DtIoConfig.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Table +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct IoConfigEntry
{
    const char* Name;
    int Code;
    int Kinds;
    int Parents[2];
} IoConfigEntry;

// Built from one X-macro list. The string and the code of each entry are expanded from
// the same token, so the name sent to the driver is the macro suffix by construction.
static const IoConfigEntry g_IoConfigs[] = {
#define X(Name, Kinds, Parent1, Parent2)                                                 \
    {#Name, DTAPI_IOCONFIG_##Name, Kinds, {Parent1, Parent2}},
#include "Tables/DtIoConfigList.inc"
#undef X
};

#define IO_CONFIG_COUNT ((int)(sizeof(g_IoConfigs) / sizeof(g_IoConfigs[0])))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lookups +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtIoConfig_Count -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtIoConfig_Count(void)
{
    return IO_CONFIG_COUNT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtIoConfig_GetCode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A linear search. There are about a hundred entries and configuration is set rarely, so
// a hash or a sorted index would add code without making anything measurably faster.
//
DtapiResult DtIoConfig_GetCode(const char* Name, int* Code)
{
    if (Code == NULL)
        return DTAPI_E_INVALID_ARG;

    *Code = -1;

    if (Name == NULL)
        return DTAPI_E_INVALID_ARG;

    if (Name[0] == '\0')
        return DTAPI_OK;

    for (int i = 0; i < IO_CONFIG_COUNT; i++)
    {
        if (strcmp(Name, g_IoConfigs[i].Name) == 0)
        {
            *Code = g_IoConfigs[i].Code;
            return DTAPI_OK;
        }
    }

    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtIoConfig_GetName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The table is in numeric order, so a code indexes it directly. The unit test that checks
// every code appears exactly once is what makes that safe.
//
DtapiResult DtIoConfig_GetName(int Code, char* Name, size_t Size)
{
    if (Name == NULL || Size == 0)
        return DTAPI_E_INVALID_ARG;

    Name[0] = '\0';

    if (Code == -1)
        return DTAPI_OK;

    if (Code < -1 || Code >= IO_CONFIG_COUNT)
        return DTAPI_E_INVALID_ARG;

    const char* Found = g_IoConfigs[Code].Name;
    size_t Length = strlen(Found);
    if (Length + 1 > Size)
        return DTAPI_E_BUF_TOO_SMALL;

    memcpy(Name, Found, Length + 1);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Validation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsCode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsCode(int Code)
{
    return Code >= 0 && Code < IO_CONFIG_COUNT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool IsKind(int Code, int Kinds)
{
    return (g_IoConfigs[Code].Kinds & Kinds) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasParent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when Code may appear under Parent. DT_IOCFG_ANY_BOOLIO matches the boolean I/O
// capabilities themselves, not TRUE and FALSE, which also carry the boolean I/O kind.
//
static bool HasParent(int Code, int Parent)
{
    for (int i = 0; i < 2; i++)
    {
        int Slot = g_IoConfigs[Code].Parents[i];

        if (Slot == Parent)
            return true;
        if (Slot == DT_IOCFG_ANY_BOOLIO && IsKind(Parent, DT_IOCFG_BOOLIO) &&
            !IsKind(Parent, DT_IOCFG_SUBVALUE))
        {
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasChildren -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool HasChildren(int Code)
{
    for (int i = 0; i < IO_CONFIG_COUNT; i++)
    {
        if (HasParent(i, Code))
            return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtIoConfig_IsValid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtIoConfig_IsValid(int Group, int Value, int SubValue)
{
    if (!IsCode(Group) || !IsKind(Group, DT_IOCFG_GROUP | DT_IOCFG_BOOLIO))
        return DTAPI_E_INVALID_ARG;

    if (!IsCode(Value) || !IsKind(Value, DT_IOCFG_VALUE) || !HasParent(Value, Group))
        return DTAPI_E_INVALID_ARG;

    if (SubValue == -1)
        return HasChildren(Value) ? DTAPI_E_INVALID_ARG : DTAPI_OK;

    // A boolean I/O capability takes TRUE or FALSE and nothing below it.
    if (IsKind(Group, DT_IOCFG_BOOLIO))
        return DTAPI_E_INVALID_ARG;

    if (!IsCode(SubValue) || !IsKind(SubValue, DT_IOCFG_SUBVALUE) ||
        !HasParent(SubValue, Value))
    {
        return DTAPI_E_INVALID_ARG;
    }

    return DTAPI_OK;
}

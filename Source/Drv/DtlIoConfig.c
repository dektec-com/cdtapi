// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlIoConfig.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Translation between I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"  // DTAPI_IOCONFIG_ codes and result codes.
#include "DtlIoConfig.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Table +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct IoConfigEntry
{
    const char* Name;
    int Code;
} IoConfigEntry;

// Built from one X-macro list. The string and the code of each entry are expanded from
// the same token, so the name sent to the driver is the macro suffix by construction.
static const IoConfigEntry g_IoConfigs[] = {
#define X(Name) {#Name, DTAPI_IOCONFIG_##Name},
#include "Tables/DtlIoConfigList.inc"
#undef X
};

#define IO_CONFIG_COUNT ((int)(sizeof(g_IoConfigs) / sizeof(g_IoConfigs[0])))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lookups +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlIoConfigCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtlIoConfigCount(void)
{
    return IO_CONFIG_COUNT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlIoConfigGetCode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A linear search. There are about a hundred entries and configuration is set rarely, so
// a hash or a sorted index would add code without making anything measurably faster.
//
unsigned int DtlIoConfigGetCode(const char* Name, int* Code)
{
    int i;

    if (Code == NULL)
        return DTAPI_E_INVALID_ARG;

    *Code = -1;

    if (Name == NULL)
        return DTAPI_E_INVALID_ARG;

    if (Name[0] == '\0')
        return DTAPI_OK;

    for (i = 0; i < IO_CONFIG_COUNT; i++)
    {
        if (strcmp(Name, g_IoConfigs[i].Name) == 0)
        {
            *Code = g_IoConfigs[i].Code;
            return DTAPI_OK;
        }
    }

    return DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlIoConfigGetName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The table is in numeric order, so a code indexes it directly. The unit test that checks
// every code appears exactly once is what makes that safe.
//
unsigned int DtlIoConfigGetName(int Code, char* Name, size_t Size)
{
    const char* Found;
    size_t Length;

    if (Name == NULL || Size == 0)
        return DTAPI_E_INVALID_ARG;

    Name[0] = '\0';

    if (Code == -1)
        return DTAPI_OK;

    if (Code < -1 || Code >= IO_CONFIG_COUNT)
        return DTAPI_E_INVALID_ARG;

    Found = g_IoConfigs[Code].Name;
    Length = strlen(Found);
    if (Length + 1 > Size)
        return DTAPI_E_BUF_TOO_SMALL;

    memcpy(Name, Found, Length + 1);
    return DTAPI_OK;
}

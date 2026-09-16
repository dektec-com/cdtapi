// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimDta2178.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - What the emulated DTA-2178 is: its properties and default configuration
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h" // DTAPI_IOCONFIG_ codes.
#include "DtDrvAbi.h"   // PROPERTY_VALUE_TYPE_ values.
#include "SimDtPcie.h"  // Port counts.
#include "SimDta2178.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Capabilities +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Per port, as the device description lists them for firmware variant 1. Ports 1 to 8
// share one list, extended by the direction each can take; ports 9 and 10 are the genlock
// reference ports, and port 10 is virtual.
//

static const char* const SdiPortCaps[] = {
    "CAP_1080I50",
    "CAP_1080I59_94",
    "CAP_1080I60",
    "CAP_1080P23_98",
    "CAP_1080P24",
    "CAP_1080P25",
    "CAP_1080P29_97",
    "CAP_1080P30",
    "CAP_1080P50",
    "CAP_1080P50B",
    "CAP_1080P59_94",
    "CAP_1080P59_94B",
    "CAP_1080P60",
    "CAP_1080P60B",
    "CAP_1080PSF23_98",
    "CAP_1080PSF24",
    "CAP_1080PSF25",
    "CAP_1080PSF29_97",
    "CAP_1080PSF30",
    "CAP_3GSDI",
    "CAP_525I59_94",
    "CAP_625I50",
    "CAP_720P23_98",
    "CAP_720P24",
    "CAP_720P25",
    "CAP_720P29_97",
    "CAP_720P30",
    "CAP_720P50",
    "CAP_720P59_94",
    "CAP_720P60",
    "CAP_ASI",
    "CAP_ASIPOL",
    "CAP_DBLBUF",
    "CAP_DMATESTMODE",
    "CAP_GENLOCKED",
    "CAP_HDSDI",
    "CAP_MATRIX2",
    "CAP_SCALE_12GTO3G",
    "CAP_SDI",
    "CAP_TODREF_INTERNAL",
    "CAP_TODREF_STEADYCLOCK",
    "CAP_TRPMODE",
    "CAP_TS",
    "CAP_TX_T2MI",
    "CAP_TXONTIME",
    "CAP_INPUT",
    "CAP_OUTPUT",
};

static const char* const GenRefPortCaps[] = {
    "CAP_1080I50",   "CAP_1080I59_94", "CAP_1080I60",      "CAP_1080P23_98",
    "CAP_1080P24",   "CAP_1080P25",    "CAP_1080P29_97",   "CAP_1080P30",
    "CAP_1080P50",   "CAP_1080P59_94", "CAP_1080P60",      "CAP_1080PSF23_98",
    "CAP_1080PSF24", "CAP_1080PSF25",  "CAP_1080PSF29_97", "CAP_1080PSF30",
    "CAP_3GSDI",     "CAP_525I59_94",  "CAP_625I50",       "CAP_720P23_98",
    "CAP_720P24",    "CAP_720P25",     "CAP_720P29_97",    "CAP_720P30",
    "CAP_720P50",    "CAP_720P59_94",  "CAP_720P60",       "CAP_GENREF",
    "CAP_HDSDI",     "CAP_SDI",
};

#define COUNT_OF(Array) (sizeof(Array) / sizeof((Array)[0]))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool InList(const char* const* List, size_t Count, const char* Name)
{
    size_t i;

    for (i = 0; i < Count; i++)
    {
        if (strcmp(List[i], Name) == 0)
            return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasCapability -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool HasCapability(const char* Name, int PortIndex)
{
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        return InList(SdiPortCaps, COUNT_OF(SdiPortCaps), Name);

    if (PortIndex == SIM_SDI_PORT_COUNT || PortIndex == SIM_SDI_PORT_COUNT + 1)
    {
        if (InList(GenRefPortCaps, COUNT_OF(GenRefPortCaps), Name))
            return true;
        return PortIndex == SIM_SDI_PORT_COUNT + 1 && strcmp(Name, "CAP_VIRTUAL") == 0;
    }

    return false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178GetProperty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDta2178GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value)
{
    if (PortIndex == -1 &&
        (strcmp(Name, "PORT_COUNT") == 0 || strcmp(Name, "MAIN_PORT_COUNT") == 0))
    {
        *Type = PROPERTY_VALUE_TYPE_INT;
        *Value = SIM_PORT_COUNT;
        return true;
    }

    if (strncmp(Name, "CAP_", 4) == 0)
    {
        *Type = PROPERTY_VALUE_TYPE_BOOL;
        *Value = HasCapability(Name, PortIndex) ? 1 : 0;
        return true;
    }

    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178DefaultConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDta2178DefaultConfig(int PortIndex, int Group, int* Value, int* SubValue)
{
    bool Sdi = PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT;
    bool Input = PortIndex % 2 == 0;

    *Value = -1;
    *SubValue = -1;

    switch (Group)
    {
    case DTAPI_IOCONFIG_IODIR:
        if (Sdi)
        {
            *Value = Input ? DTAPI_IOCONFIG_INPUT : DTAPI_IOCONFIG_OUTPUT;
            *SubValue = *Value;
        }
        break;
    case DTAPI_IOCONFIG_IOSTD:
        *Value = Sdi ? DTAPI_IOCONFIG_HDSDI : DTAPI_IOCONFIG_SDI;
        *SubValue = Sdi ? DTAPI_IOCONFIG_1080I50 : DTAPI_IOCONFIG_625I50;
        break;
    case DTAPI_IOCONFIG_IODOWNSCALE:
        if (Sdi)
            *Value = DTAPI_IOCONFIG_SCALE_12GTO3G;
        break;
    case DTAPI_IOCONFIG_TODREFSEL:
        if (Sdi)
            *Value = DTAPI_IOCONFIG_TODREF_INTERNAL;
        break;
    case DTAPI_IOCONFIG_DMATESTMODE:
    case DTAPI_IOCONFIG_GENLOCKED:
        if (Sdi)
            *Value = DTAPI_IOCONFIG_FALSE;
        break;
    case DTAPI_IOCONFIG_GENREF:
        if (!Sdi)
            *Value = PortIndex == SIM_SDI_PORT_COUNT + 1 ? DTAPI_IOCONFIG_TRUE
                                                         : DTAPI_IOCONFIG_FALSE;
        break;
    default:
        break;
    }
}

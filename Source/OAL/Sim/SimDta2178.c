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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The driver functions (DF_) and building blocks (BC_) of the ASI/SDI receiver API
// function, AF_ASISDIRX#1, in the order a DTA-2178 lists them for each port that has
// one. The names, roles and types are those a DTA-2178 with driver 3.6.4 reported for
// its two independent 12G ports. That card ran firmware variant 2; variant 1, which the
// emulator presents, is taken to list the same for each of its eight ports.
//

typedef struct SimFunction
{
    const char* Name;
    const char* Role;
    int Type; // DT_FUNC_TYPE_ or DT_BLOCK_TYPE_
    bool IsDf;
} SimFunction;

static const SimFunction g_AsiSdiRxFunctions[] = {
    {"BC_SWITCH#2", "SDI_MUX_IN", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_ST425LR#1", "", DT_BLOCK_TYPE_ST425LR, false},
    {"BC_SDIMUX12G#1", "", DT_BLOCK_TYPE_SDIMUX12G, false},
    {"BC_SWITCH#3", "SDI_MUX_OUT", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_SDIRXF#1", "", DT_BLOCK_TYPE_SDIRXF, false},
    {"DF_SDIRX#1", "", DT_FUNC_TYPE_SDIRX, true},
    {"DF_ASIRX#1", "", DT_FUNC_TYPE_ASIRX, true},
    {"DF_CHSDIRX#1", "", DT_FUNC_TYPE_CHSDIRX, true},
};

#define FUNCTIONS_PER_PORT ((int)COUNT_OF(g_AsiSdiRxFunctions))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FunctionUuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The emulator numbers the functions of all ports in one sequence, and flags each UUID as
// the driver does. The numbers are its own; a real card's depend on its whole layout.
//
static int FunctionUuid(int PortIndex, int Index)
{
    int Flag = g_AsiSdiRxFunctions[Index].IsDf ? DT_UUID_DF_FLAG : DT_UUID_BC_FLAG;
    return Flag | (PortIndex * FUNCTIONS_PER_PORT + Index + 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFunction -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The index in g_AsiSdiRxFunctions of the function whose name starts Name and is followed
// by Suffix, or -1.
//
static int FindFunction(const char* Name, const char* Suffix)
{
    int i;

    for (i = 0; i < FUNCTIONS_PER_PORT; i++)
    {
        size_t Length = strlen(g_AsiSdiRxFunctions[i].Name);

        if (strncmp(Name, g_AsiSdiRxFunctions[i].Name, Length) == 0 &&
            strcmp(Name + Length, Suffix) == 0)
        {
            return i;
        }
    }
    return -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasAsiSdiRx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool HasAsiSdiRx(int PortIndex)
{
    return PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT;
}

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

    if (HasAsiSdiRx(PortIndex))
    {
        int Index = FindFunction(Name, "_TYPE");

        if (Index >= 0)
        {
            *Type = PROPERTY_VALUE_TYPE_INT;
            *Value = (uint64_t)g_AsiSdiRxFunctions[Index].Type;
            return true;
        }

        Index = FindFunction(Name, "_UUID");
        if (Index >= 0)
        {
            *Type = PROPERTY_VALUE_TYPE_INT;
            *Value = (uint64_t)FunctionUuid(PortIndex, Index);
            return true;
        }
    }

    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178GetString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// AF_ASISDIRX#1 gives its role, AF_ASISDIRX#1.<n> the name of its n-th function, and the
// name of a function its role.
//
bool SimDta2178GetString(const char* Name, int PortIndex, const char** Str)
{
    static const char Prefix[] = "AF_ASISDIRX#1";
    size_t PrefixLength = sizeof(Prefix) - 1;
    int Index;

    if (!HasAsiSdiRx(PortIndex))
        return false;

    if (strcmp(Name, Prefix) == 0)
    {
        *Str = "";
        return true;
    }

    if (strncmp(Name, Prefix, PrefixLength) == 0 && Name[PrefixLength] == '.')
    {
        const char* Digits = Name + PrefixLength + 1;
        int Number = 0;

        // One or more digits and nothing else, without a leading zero.
        if (*Digits < '1' || *Digits > '9')
            return false;
        for (; *Digits >= '0' && *Digits <= '9' && Number <= FUNCTIONS_PER_PORT; Digits++)
            Number = Number * 10 + (*Digits - '0');
        if (*Digits != '\0' || Number > FUNCTIONS_PER_PORT)
            return false;

        *Str = g_AsiSdiRxFunctions[Number - 1].Name;
        return true;
    }

    Index = FindFunction(Name, "");
    if (Index < 0)
        return false;
    *Str = g_AsiSdiRxFunctions[Index].Role;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178FindFunction -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimDta2178FindFunction(int Uuid, int* PortIndex, int* Type)
{
    int Flat = (Uuid & DT_UUID_INDEX_MASK) - 1;
    int Port = Flat / FUNCTIONS_PER_PORT;
    int Index = Flat % FUNCTIONS_PER_PORT;

    if (Flat < 0 || !HasAsiSdiRx(Port) || FunctionUuid(Port, Index) != Uuid)
        return false;

    *PortIndex = Port;
    *Type = g_AsiSdiRxFunctions[Index].Type;
    return true;
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

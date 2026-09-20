// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimDta2178.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the emulated DTA-2178 is: its properties and default configuration
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h"  // PROPERTY_VALUE_TYPE_ values.
#include "SimDtPcie.h"  // Port counts.
#include "SimDta2178.h" // Interface being implemented.
#include "cdtapi.h"     // DTAPI_IOCONFIG_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Capabilities +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Per port, as the device description lists them for firmware variant 1. Ports 1 to 8
// share one list, extended by the direction each can take; ports 9 and 10 are the genlock
// reference ports, and port 10 is virtual. CAP_QUADLINK comes with the demultiplexer
// every port's transmitter lists below, as it does on ports 1 and 5 of a variant 2 card.
//
// The list leaves out CAP_SCALE_12GTO3G, which variant 1 has and variants 2 and 3 do
// not. A port that has it always scales 12G down to 3G, and detection then reports every
// 2160p signal as the 1080p standard of one link, which would leave a program that
// chooses its standard by detection unable to see 2160p on the emulator. A test that
// wants that behaviour adds the capability with SimDtPcie_OverrideProperty; the
// down-scale configuration is there either way.
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
    "CAP_SDI",
    "CAP_TODREF_INTERNAL",
    "CAP_TODREF_STEADYCLOCK",
    "CAP_TRPMODE",
    "CAP_TS",
    "CAP_TX_T2MI",
    "CAP_TXONTIME",
    "CAP_INPUT",
    "CAP_OUTPUT",
    "CAP_QUADLINK",
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The API functions of each SDI port and their driver functions (DF_) and building blocks
// (BC_), in the order a DTA-2178 lists them. The names, roles and types are those a
// DTA-2178 with driver 3.6.4 reported: the receiver's for its two independent 12G ports,
// the transmitter's and the DMA's for its bidirectional port 5. That card ran firmware
// variant 2; variant 1, which the emulator presents, is taken to list the same for each
// of its eight ports.
//

typedef struct SimFunction
{
    const char* Name;
    const char* Role;
    int Type; // DT_FUNC_TYPE_ or DT_BLOCK_TYPE_
    bool IsDf;
} SimFunction;

static const SimFunction g_AsiSdiRxParts[] = {
    {"BC_SWITCH#2", "SDI_MUX_IN", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_ST425LR#1", "", DT_BLOCK_TYPE_ST425LR, false},
    {"BC_SDIMUX12G#1", "", DT_BLOCK_TYPE_SDIMUX12G, false},
    {"BC_SWITCH#3", "SDI_MUX_OUT", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_SDIRXF#1", "", DT_BLOCK_TYPE_SDIRXF, false},
    {"DF_SDIRX#1", "", DT_FUNC_TYPE_SDIRX, true},
    {"DF_ASIRX#1", "", DT_FUNC_TYPE_ASIRX, true},
    {"DF_CHSDIRX#1", "", DT_FUNC_TYPE_CHSDIRX, true},
};

static const SimFunction g_AsiSdiTxParts[] = {
    {"BC_ASITXG#1", "", DT_BLOCK_TYPE_ASITXG, false},
    {"BC_SDITXF#1", "", DT_BLOCK_TYPE_SDITXF, false},
    {"BC_SWITCH#6", "SDI_DEMUX_IN", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_SDIDMX12G#1", "", DT_BLOCK_TYPE_SDIDMX12G, false},
    {"BC_SWITCH#7", "SDI_DEMUX_OUT", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_SDITXP#1", "", DT_BLOCK_TYPE_SDITXP, false},
    {"DF_SDITXPHY#1", "", DT_FUNC_TYPE_SDITXPHY, true},
};

static const SimFunction g_DmaParts[] = {
    {"BC_CDMAC#1", "", DT_BLOCK_TYPE_CDMAC, false},
    {"BC_BURSTFIFO#1", "", DT_BLOCK_TYPE_BURSTFIFO, false},
    {"BC_CONSTSOURCE#1", "", DT_BLOCK_TYPE_CONSTSOURCE, false},
    {"BC_CONSTSINK#1", "", DT_BLOCK_TYPE_CONSTSINK, false},
};

#define COUNT_OF(Array) (sizeof(Array) / sizeof((Array)[0]))

typedef struct SimApiFunction
{
    const char* Name; // The first instance, with the empty role
    const SimFunction* Parts;
    int NumParts;
} SimApiFunction;

static const SimApiFunction g_ApiFunctions[] = {
    {"AF_ASISDIRX#1", g_AsiSdiRxParts, (int)COUNT_OF(g_AsiSdiRxParts)},
    {"AF_ASISDITX#1", g_AsiSdiTxParts, (int)COUNT_OF(g_AsiSdiTxParts)},
    {"AF_DMA#1", g_DmaParts, (int)COUNT_OF(g_DmaParts)},
};

#define API_FUNCTION_COUNT ((int)COUNT_OF(g_ApiFunctions))

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FunctionUuid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The emulator numbers the parts of one API function for all ports, then those of the
// next, and flags each UUID as the driver does. The numbers are its own; a real card's
// depend on its whole layout.
//
static int FunctionUuid(int Af, int PortIndex, int Index)
{
    const SimApiFunction* Api = &g_ApiFunctions[Af];
    int Base = 0;
    int i;

    for (i = 0; i < Af; i++)
        Base += g_ApiFunctions[i].NumParts * SIM_SDI_PORT_COUNT;
    return (Api->Parts[Index].IsDf ? DT_UUID_DF_FLAG : DT_UUID_BC_FLAG) |
           (Base + PortIndex * Api->NumParts + Index + 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFunction -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The part whose name starts Name and is followed by Suffix: true with its API function
// in *Af and its index in *Index.
//
static bool FindFunction(const char* Name, const char* Suffix, int* Af, int* Index)
{
    for (int a = 0; a < API_FUNCTION_COUNT; a++)
    {
        for (int i = 0; i < g_ApiFunctions[a].NumParts; i++)
        {
            const char* PartName = g_ApiFunctions[a].Parts[i].Name;
            size_t Length = strlen(PartName);

            if (strncmp(Name, PartName, Length) == 0 &&
                strcmp(Name + Length, Suffix) == 0)
            {
                *Af = a;
                *Index = i;
                return true;
            }
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasFunctions -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The SDI ports have the API functions; the genlock ports have none.
//
static bool HasFunctions(int PortIndex)
{
    return PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool InList(const char* const* List, size_t Count, const char* Name)
{
    for (size_t i = 0; i < Count; i++)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178_GetProperty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimDta2178_GetProperty(const char* Name, int PortIndex, int* Type, uint64_t* Value)
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

    if (HasFunctions(PortIndex))
    {
        int Af;
        int Index;

        if (FindFunction(Name, "_TYPE", &Af, &Index))
        {
            *Type = PROPERTY_VALUE_TYPE_INT;
            *Value = (uint64_t)g_ApiFunctions[Af].Parts[Index].Type;
            return true;
        }

        if (FindFunction(Name, "_UUID", &Af, &Index))
        {
            *Type = PROPERTY_VALUE_TYPE_INT;
            *Value = (uint64_t)FunctionUuid(Af, PortIndex, Index);
            return true;
        }
    }

    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178_GetString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An API function's first instance, such as AF_ASISDIRX#1, gives its role,
// AF_ASISDIRX#1.<n> the name of its n-th part, and the name of a part its role.
//
bool SimDta2178_GetString(const char* Name, int PortIndex, const char** Str)
{
    if (!HasFunctions(PortIndex))
        return false;

    int a;
    for (a = 0; a < API_FUNCTION_COUNT; a++)
    {
        const SimApiFunction* Api = &g_ApiFunctions[a];
        size_t PrefixLength = strlen(Api->Name);
        const char* Digits = Name + PrefixLength;
        int Number = 0;

        if (strncmp(Name, Api->Name, PrefixLength) != 0)
            continue;
        if (*Digits == '\0')
        {
            *Str = "";
            return true;
        }
        if (*Digits++ != '.')
            continue;

        // One or more digits and nothing else, without a leading zero.
        if (*Digits < '1' || *Digits > '9')
            return false;
        for (; *Digits >= '0' && *Digits <= '9' && Number <= Api->NumParts; Digits++)
            Number = Number * 10 + (*Digits - '0');
        if (*Digits != '\0' || Number > Api->NumParts)
            return false;

        *Str = Api->Parts[Number - 1].Name;
        return true;
    }

    int Index;
    if (!FindFunction(Name, "", &a, &Index))
        return false;
    *Str = g_ApiFunctions[a].Parts[Index].Role;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178_FindFunction -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDta2178_FindFunction(int Uuid, int* PortIndex, int* Type, const char** Role)
{
    int Flat = (Uuid & DT_UUID_INDEX_MASK) - 1;

    for (int a = 0; a < API_FUNCTION_COUNT && Flat >= 0; a++)
    {
        const SimApiFunction* Api = &g_ApiFunctions[a];
        int Count = Api->NumParts * SIM_SDI_PORT_COUNT;

        if (Flat < Count)
        {
            int Port = Flat / Api->NumParts;
            int Index = Flat % Api->NumParts;

            if (FunctionUuid(a, Port, Index) != Uuid)
                return false;
            *PortIndex = Port;
            *Type = Api->Parts[Index].Type;
            *Role = Api->Parts[Index].Role;
            return true;
        }
        Flat -= Count;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178_PartCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int SimDta2178_PartCount(void)
{
    int Count = 0;

    for (int a = 0; a < API_FUNCTION_COUNT; a++)
        Count += g_ApiFunctions[a].NumParts * SIM_SDI_PORT_COUNT;
    return Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDta2178_DefaultConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDta2178_DefaultConfig(int PortIndex, int Group, int* Value, int* SubValue)
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
        // What a port that has the down-scaler reports; without the capability, which
        // the emulated ports lack, nothing reads it. See the capabilities above.
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

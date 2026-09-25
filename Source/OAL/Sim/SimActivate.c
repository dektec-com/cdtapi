// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimActivate.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The emulated card's activation object
//
// SPDX-License-Identifier: BSD-3-Clause
//
// As much of the object as the library uses: it reports whether it is ready, and is given
// data to become ready. The data it accepts is what its own EEPROM holds
// behind the sections, most significant byte first, which is the order the library hands
// it over in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h"   // Vendored driver structures and status codes.
#include "SimActivate.h" // Interface being implemented.
#include "SimVpd.h"      // The EEPROM the data comes from.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The board number the object reports; any number does, nothing reads it.
#define SIM_ACTIVATE_BOARD_ID 0x0037FC0502E25900LL

static bool g_Ready;
static int g_BusyCount;
static int g_BusyLeft;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TailWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The word at Index of the EEPROM's tail, in the order the object is given it.
//
static uint32_t TailWord(int Index)
{
    const uint8_t* Tail = SimVpd_Tail() + (size_t)Index * sizeof(uint32_t);

    return (uint32_t)Tail[0] << 24 | (uint32_t)Tail[1] << 16 | (uint32_t)Tail[2] << 8 |
           Tail[3];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsBlank -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A tail whose words are all the same is one the EEPROM was never given.
//
static bool IsBlank(void)
{
    const int Words = SIM_VPD_TAIL_BYTES / (int)sizeof(uint32_t);

    for (int i = 1; i < Words; i++)
    {
        if (TailWord(i) != TailWord(0))
            return false;
    }
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimActivate_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimActivate_Reset(void)
{
    g_Ready = false;
    g_BusyCount = 0;
    g_BusyLeft = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimActivate_IsReady -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimActivate_IsReady(void)
{
    return g_Ready && g_BusyLeft == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimActivate_SetBusyCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimActivate_SetBusyCount(int Count)
{
    g_BusyCount = Count > 0 ? Count : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimActivate_Handles -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimActivate_Handles(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_IPSECG_CMD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimActivate_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimActivate_Cmd(int Cmd, const void* In, size_t InSize, void* Out,
                         size_t* OutSize)
{
    if (Cmd == DT_IPSECG_CMD_GET_STATUS)
    {
        if (InSize < sizeof(DtIoctlIpSecGCmdGetStatusInput) || Out == NULL ||
            OutSize == NULL || *OutSize < sizeof(DtIoctlIpSecGCmdGetStatusOutput))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }

        DtIoctlIpSecGCmdGetStatusOutput* Status = (DtIoctlIpSecGCmdGetStatusOutput*)Out;
        memset(Status, 0, sizeof(*Status));
        Status->m_BoardId = SIM_ACTIVATE_BOARD_ID;
        Status->m_IsBusy = g_BusyLeft > 0 ? 1 : 0;
        Status->m_IsOk = SimActivate_IsReady() ? 1 : 0;
        if (g_BusyLeft > 0)
            g_BusyLeft--;
        *OutSize = sizeof(*Status);
        return DT_STATUS_OK;
    }

    if (Cmd == DT_IPSECG_CMD_CHECK)
    {
        if (InSize < sizeof(DtIoctlIpSecGCmdCheckInput))
            return DT_STATUS_INVALID_PARAMETER;

        const DtIoctlIpSecGCmdCheckInput* Check = (const DtIoctlIpSecGCmdCheckInput*)In;
        const int Words = SIM_VPD_TAIL_BYTES / (int)sizeof(uint32_t);
        if (Check->m_NumWords < 0 ||
            InSize < sizeof(*Check) + (size_t)Check->m_NumWords * sizeof(uint32_t))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }

        bool Matches = Check->m_NumWords == Words && !IsBlank();
        for (int i = 0; i < Check->m_NumWords && Matches; i++)
            Matches = Check->m_Data[i] == TailWord(i);

        g_Ready = Matches;
        g_BusyLeft = Matches ? g_BusyCount : 0;
        return DT_STATUS_OK;
    }

    return DT_STATUS_NOT_SUPPORTED;
}

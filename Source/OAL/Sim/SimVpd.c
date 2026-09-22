// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimVpd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The emulated card's Vital Product Data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// DtIoStubCORE_VPD as far as reading goes: the sections' sizes and a raw read of any
// stretch of the EEPROM. Writing, and the items a section holds by keyword, are not
// modelled; nothing in the library asks for them yet.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h" // Vendored driver structures and status codes.
#include "SimVpd.h"    // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static uint8_t g_Eeprom[SIM_VPD_EEPROM_SIZE];

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FillSections -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The sections hold something recognisable rather than a real VPD structure: nothing
// reads them by keyword.
//
static void FillSections(void)
{
    for (int i = 0; i < SIM_VPD_TAIL_OFFSET; i++)
        g_Eeprom[i] = (uint8_t)('A' + i % 26);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimVpd_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimVpd_Reset(void)
{
    memset(g_Eeprom, 0, sizeof(g_Eeprom));
    FillSections();
    SimVpd_SetTailBlank(false);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimVpd_SetTailBlank -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimVpd_SetTailBlank(bool Blank)
{
    for (int i = 0; i < SIM_VPD_TAIL_BYTES; i++)
    {
        g_Eeprom[SIM_VPD_TAIL_OFFSET + i] =
            Blank ? 0xFF : (uint8_t)(0x5A + i * 7 + i / 32);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimVpd_Tail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const uint8_t* SimVpd_Tail(void)
{
    return &g_Eeprom[SIM_VPD_TAIL_OFFSET];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimVpd_Takes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimVpd_Takes(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_VPD_CMD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. SimVpd_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimVpd_Cmd(int Cmd, const void* In, size_t InSize, void* Out, size_t* OutSize)
{
    if (Cmd == DT_VPD_CMD_GET_PROPERTIES)
    {
        if (InSize < sizeof(DtIoctlVpdCmdGetPropertiesInput) || Out == NULL ||
            OutSize == NULL || *OutSize < sizeof(DtIoctlVpdCmdGetPropertiesOutput))
        {
            return DT_STATUS_INVALID_PARAMETER;
        }

        DtIoctlVpdCmdGetPropertiesOutput* Props = (DtIoctlVpdCmdGetPropertiesOutput*)Out;
        memset(Props, 0, sizeof(*Props));
        Props->m_RoOffset = SIM_VPD_RO_OFFSET;
        Props->m_RoSize = SIM_VPD_RO_SIZE;
        Props->m_RwOffset = SIM_VPD_RW_OFFSET;
        Props->m_RwSize = SIM_VPD_RW_SIZE;
        Props->m_EepromSize = SIM_VPD_EEPROM_SIZE;
        Props->m_MaxItemLength = SIM_VPD_MAX_ITEM_LENGTH;
        *OutSize = sizeof(*Props);
        return DT_STATUS_OK;
    }

    if (Cmd == DT_VPD_CMD_RAW_READ)
    {
        if (InSize < sizeof(DtIoctlVpdCmdRawReadInput) || Out == NULL || OutSize == NULL)
            return DT_STATUS_INVALID_PARAMETER;

        const DtIoctlVpdCmdRawReadInput* Read = (const DtIoctlVpdCmdRawReadInput*)In;
        if (Read->m_NumToRead < 1 || Read->m_StartOffset >= SIM_VPD_EEPROM_SIZE ||
            (uint32_t)Read->m_NumToRead > SIM_VPD_EEPROM_SIZE - Read->m_StartOffset)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        if (*OutSize < sizeof(DtIoctlVpdCmdRawReadOutput) + (size_t)Read->m_NumToRead)
            return DT_STATUS_INVALID_PARAMETER;

        DtIoctlVpdCmdRawReadOutput* Answer = (DtIoctlVpdCmdRawReadOutput*)Out;
        Answer->m_NumRead = Read->m_NumToRead;
        memcpy(Answer->m_Buf, &g_Eeprom[Read->m_StartOffset], (size_t)Read->m_NumToRead);
        *OutSize = sizeof(*Answer) + (size_t)Read->m_NumToRead;
        return DT_STATUS_OK;
    }

    return DT_STATUS_NOT_SUPPORTED;
}

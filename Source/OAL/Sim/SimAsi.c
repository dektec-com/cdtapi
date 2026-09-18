// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SimAsi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated ASI blocks and their test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtPcieAbi.h" // The driver ABI the emulator answers in.
#include "SimAsi.h"    // Interface being implemented.
#include "SimDtPcie.h" // Port counts and the lock.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimAsiPort
{
    SimAsiState State;
    SimAsiSignal Signal;
} SimAsiPort;

static struct
{
    bool Initialised;
    SimAsiPort Ports[SIM_SDI_PORT_COUNT];
    int FailFunctionCode;
    int FailCmd;
    uint32_t FailStatus;
} g_Asi;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureAsi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EnsureAsi(void)
{
    if (!g_Asi.Initialised)
        SimAsi_Reset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void NoSignal(SimAsiSignal* Signal)
{
    memset(Signal, 0, sizeof(*Signal));
    Signal->PacketSize = DT_ASIRX_PCKSIZE_UNKNOWN;
    Signal->Polarity = DT_ASIRX_POLARITY_UNKNOWN;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a command is like to the driver: the sizes of its input and output, and whether
// it needs exclusive access (DtIoctlProperties.h). Every command needs its part enabled.
typedef struct SimAsiCmdProps
{
    int FunctionCode;
    int Cmd;
    size_t InSize;
    size_t OutSize;
    bool Exclusive;
} SimAsiCmdProps;

#define HDR sizeof(DtIoctlInputDataHdr)
#define ONE_INT (HDR + sizeof(Int))

static const SimAsiCmdProps g_Cmds[] = {
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_OPERATIONAL_STATUS, HDR, sizeof(Int),
     false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_PACKET_MODE, HDR, sizeof(Int), false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_POLARITY_CTRL, HDR, sizeof(Int), false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_STATUS, HDR,
     sizeof(DtIoctlAsiRxCmdGetStatusOutput), false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_SYNC_MODE, HDR, sizeof(Int), false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_TS_BITRATE, HDR, sizeof(Int), false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_VIOL_COUNT, HDR, sizeof(Int), false},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_SET_OPERATIONAL_MODE, ONE_INT, 0, true},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_SET_PACKET_MODE, ONE_INT, 0, true},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_SET_POLARITY_CTRL, ONE_INT, 0, true},
    {DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_SET_SYNC_MODE, ONE_INT, 0, true},

    {DT_FUNC_CODE_ASITXG_CMD, DT_ASITXG_CMD_CLEAR_INPUT_STATE, HDR, 0, true},
    {DT_FUNC_CODE_ASITXG_CMD, DT_ASITXG_CMD_GET_ASI_POLARITY, HDR, sizeof(Int), false},
    {DT_FUNC_CODE_ASITXG_CMD, DT_ASITXG_CMD_GET_OPERATIONAL_MODE, HDR, sizeof(Int),
     false},
    {DT_FUNC_CODE_ASITXG_CMD, DT_ASITXG_CMD_SET_ASI_POLARITY, ONE_INT, 0, true},
    {DT_FUNC_CODE_ASITXG_CMD, DT_ASITXG_CMD_SET_OPERATIONAL_MODE, ONE_INT, 0, true},
};

#undef ONE_INT
#undef HDR

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const SimAsiCmdProps* FindCmd(int FunctionCode, int Cmd)
{
    for (size_t i = 0; i < sizeof(g_Cmds) / sizeof(g_Cmds[0]); i++)
    {
        if (g_Cmds[i].FunctionCode == FunctionCode && g_Cmds[i].Cmd == Cmd)
            return &g_Cmds[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValueOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The one Int every setting carries after the header.
//
static int ValueOf(const void* In)
{
    return ((const DtIoctlAsiRxCmdSetOpModeInput*)In)->m_OpMode;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Answer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t Answer(int Value, void* Out, size_t* OutSize)
{
    *(Int*)Out = Value;
    *OutSize = sizeof(Int);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AsiRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t AsiRxCmd(SimAsiPort* Port, int Cmd, const void* In, void* Out,
                         size_t* OutSize)
{
    SimAsiState* S = &Port->State;
    const int Value = Cmd >= DT_ASIRX_CMD_SET_OPERATIONAL_MODE ? ValueOf(In) : 0;

    switch (Cmd)
    {
    case DT_ASIRX_CMD_GET_OPERATIONAL_STATUS:
        // Running only while there is something to receive, as on a DTA-2178.
        return Answer(S->RxMode == DT_FUNC_OPMODE_RUN && Port->Signal.CarrierDetect
                          ? DT_FUNC_OPSTATUS_RUN
                          : DT_FUNC_OPSTATUS_IDLE,
                      Out, OutSize);
    case DT_ASIRX_CMD_GET_PACKET_MODE:
        return Answer(S->RxPacketMode, Out, OutSize);
    case DT_ASIRX_CMD_GET_POLARITY_CTRL:
        return Answer(S->RxPolarityCtrl, Out, OutSize);
    case DT_ASIRX_CMD_GET_STATUS:
    {
        DtIoctlAsiRxCmdGetStatusOutput* Status = (DtIoctlAsiRxCmdGetStatusOutput*)Out;
        Status->m_PacketSize = Port->Signal.PacketSize;
        Status->m_CarrierDetect = Port->Signal.CarrierDetect ? 1 : 0;
        Status->m_AsiLock = Port->Signal.AsiLock ? 1 : 0;
        Status->m_AsiPolarity = Port->Signal.Polarity;
        *OutSize = sizeof(*Status);
        return DT_STATUS_OK;
    }
    case DT_ASIRX_CMD_GET_SYNC_MODE:
        return Answer(S->RxSyncMode, Out, OutSize);
    case DT_ASIRX_CMD_GET_TS_BITRATE:
        return Answer(Port->Signal.TsBitrate, Out, OutSize);
    case DT_ASIRX_CMD_GET_VIOL_COUNT:
        return Answer(Port->Signal.ViolCount, Out, OutSize);
    case DT_ASIRX_CMD_SET_OPERATIONAL_MODE:
        if (Value != DT_FUNC_OPMODE_IDLE && Value != DT_FUNC_OPMODE_RUN)
            return DT_STATUS_INVALID_PARAMETER;
        S->RxMode = Value;
        return DT_STATUS_OK;
    case DT_ASIRX_CMD_SET_PACKET_MODE:
        if (Value != DT_ASIRX_PCKMODE_AUTO && Value != DT_ASIRX_PCKMODE_RAW)
            return DT_STATUS_INVALID_PARAMETER;
        S->RxPacketMode = Value;
        return DT_STATUS_OK;
    case DT_ASIRX_CMD_SET_POLARITY_CTRL:
        if (Value != DT_ASIRX_POLARITY_AUTO && Value != DT_ASIRX_POLARITY_NORMAL &&
            Value != DT_ASIRX_POLARITY_INVERT)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        S->RxPolarityCtrl = Value;
        return DT_STATUS_OK;
    default: // DT_ASIRX_CMD_SET_SYNC_MODE
        if (Value != DT_ASIRX_SYNCMODE_AUTO && Value != DT_ASIRX_SYNCMODE_188 &&
            Value != DT_ASIRX_SYNCMODE_204)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        S->RxSyncMode = Value;
        return DT_STATUS_OK;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AsiTxGCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t AsiTxGCmd(SimAsiPort* Port, int Cmd, const void* In, void* Out,
                          size_t* OutSize)
{
    SimAsiState* S = &Port->State;

    switch (Cmd)
    {
    case DT_ASITXG_CMD_CLEAR_INPUT_STATE:
        S->TxgInputClears++;
        return DT_STATUS_OK;
    case DT_ASITXG_CMD_GET_ASI_POLARITY:
        return Answer(S->TxgPolarity, Out, OutSize);
    case DT_ASITXG_CMD_GET_OPERATIONAL_MODE:
        return Answer(S->TxgMode, Out, OutSize);
    case DT_ASITXG_CMD_SET_ASI_POLARITY:
    {
        int Polarity = ValueOf(In);
        if (Polarity != DT_ASITXG_POL_NORMAL && Polarity != DT_ASITXG_POL_INVERT)
            return DT_STATUS_INVALID_PARAMETER;
        S->TxgPolarity = Polarity;
        return DT_STATUS_OK;
    }
    default: // DT_ASITXG_CMD_SET_OPERATIONAL_MODE
    {
        int OpMode = ValueOf(In);
        if (OpMode != DT_BLOCK_OPMODE_IDLE && OpMode != DT_BLOCK_OPMODE_STANDBY &&
            OpMode != DT_BLOCK_OPMODE_RUN)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        S->TxgMode = OpMode;
        return DT_STATUS_OK;
    }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_Takes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimAsi_Takes(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_ASIRX_CMD ||
           FunctionCode == DT_FUNC_CODE_ASITXG_CMD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A part that is disabled forgets its settings, as the driver's does when it is enabled
// again.
//
uint32_t SimAsi_Cmd(void* Handle, int PortIndex, int FunctionCode, int Type, int Cmd,
                    uint32_t Access, bool Enabled, const void* In, size_t InSize,
                    void* Out, size_t* OutSize)
{
    (void)Handle;
    EnsureAsi();
    const bool IsRx = FunctionCode == DT_FUNC_CODE_ASIRX_CMD;
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT ||
        Type != (IsRx ? DT_FUNC_TYPE_ASIRX : DT_BLOCK_TYPE_ASITXG))
    {
        return DT_STATUS_NOT_SUPPORTED;
    }
    SimAsiPort* Port = &g_Asi.Ports[PortIndex];

    const SimAsiCmdProps* Props = FindCmd(FunctionCode, Cmd);
    if (Props == NULL)
        return DT_STATUS_NOT_SUPPORTED;
    if (InSize < Props->InSize ||
        (Props->OutSize > 0 &&
         (Out == NULL || OutSize == NULL || *OutSize < Props->OutSize)))
    {
        return DT_STATUS_INVALID_PARAMETER;
    }
    if (Props->Exclusive && Access != DT_STATUS_OK)
        return Access;
    if (g_Asi.FailFunctionCode == FunctionCode && g_Asi.FailCmd == Cmd &&
        g_Asi.FailStatus != 0)
    {
        return g_Asi.FailStatus;
    }
    if (!Enabled)
    {
        SimAsiState* S = &Port->State;
        if (IsRx)
        {
            S->RxMode = DT_FUNC_OPMODE_IDLE;
            S->RxPacketMode = DT_ASIRX_PCKMODE_AUTO;
            S->RxPolarityCtrl = DT_ASIRX_POLARITY_AUTO;
            S->RxSyncMode = DT_ASIRX_SYNCMODE_AUTO;
        }
        else
        {
            S->TxgMode = DT_BLOCK_OPMODE_IDLE;
            S->TxgPolarity = DT_ASITXG_POL_NORMAL;
        }
        return DT_STATUS_NOT_ENABLED;
    }

    return IsRx ? AsiRxCmd(Port, Cmd, In, Out, OutSize)
                : AsiTxGCmd(Port, Cmd, In, Out, OutSize);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimAsi_Reset(void)
{
    for (int i = 0; i < SIM_SDI_PORT_COUNT; i++)
    {
        SimAsiPort* Port = &g_Asi.Ports[i];
        memset(Port, 0, sizeof(*Port));
        Port->State.RxMode = DT_FUNC_OPMODE_IDLE;
        Port->State.RxPacketMode = DT_ASIRX_PCKMODE_AUTO;
        Port->State.RxPolarityCtrl = DT_ASIRX_POLARITY_AUTO;
        Port->State.RxSyncMode = DT_ASIRX_SYNCMODE_AUTO;
        Port->State.TxgMode = DT_BLOCK_OPMODE_IDLE;
        Port->State.TxgPolarity = DT_ASITXG_POL_NORMAL;
        NoSignal(&Port->Signal);
    }
    g_Asi.FailFunctionCode = -1;
    g_Asi.FailCmd = -1;
    g_Asi.FailStatus = 0;
    g_Asi.Initialised = true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetAsiSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetAsiSignal(int PortIndex, const SimAsiSignal* Signal)
{
    SimDtPcie_Lock();
    EnsureAsi();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
    {
        if (Signal != NULL)
            g_Asi.Ports[PortIndex].Signal = *Signal;
        else
            NoSignal(&g_Asi.Ports[PortIndex].Signal);
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetAsiState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_GetAsiState(int PortIndex, SimAsiState* State)
{
    SimDtPcie_Lock();
    EnsureAsi();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        *State = g_Asi.Ports[PortIndex].State;
    else
        memset(State, 0, sizeof(*State));
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_FailAsiCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_FailAsiCmd(int FunctionCode, int Cmd, uint32_t Status)
{
    SimDtPcie_Lock();
    EnsureAsi();
    g_Asi.FailFunctionCode = Status != 0 ? FunctionCode : -1;
    g_Asi.FailCmd = Status != 0 ? Cmd : -1;
    g_Asi.FailStatus = Status;
    SimDtPcie_Unlock();
}

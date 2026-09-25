// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SimAsi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated ASI blocks and their test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtPcieAbi.h"    // The driver ABI the emulator answers in.
#include "OAL/OsThread.h" // The clock the ports follow.
#include "SimAsi.h"       // Interface being implemented.
#include "SimDtPcie.h"    // Port counts and the lock.
#include "SimNw.h"        // The card's time of day.
#include "SimSdiTx.h"     // The DMA of the ports.
#include "Ts/DtAsiEnc.h"  // The code the sink decodes.
#include "Ts/DtTsTrp.h"   // The packets the card receives into.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A decoded symbol that is K28.5, and one that is not a code.
#define SIM_ASI_K28_5 0x100
#define SIM_ASI_NO_CODE (-1)

// The bytes the output takes from its buffer a millisecond: 27 M symbols a second, of 16
// bits each.
#define SIM_ASI_BYTES_PER_MS 54000

// The symbols the sink measures the rate over.
#define SIM_ASI_RATE_WINDOW 270000

// What the input of a loop finds packets in: three sync bytes of the larger packets.
#define SIM_ASI_FIND_BYTES (2 * 204 + 1)
#define SIM_ASI_LOOP_BUFFER 1024

typedef struct SimAsiRx
{
    bool HasSource;
    SimAsiSource Source;
    uint32_t NextPacketNumber; // Of the next packet
    int UnsyncedLeft;          // Pieces without sync still to come
    double PacketsDue;         // Packets due in real time, not yet received
    uint64_t LastMs;           // When the packets due were last counted
    bool WasReceiving;         // At the last count
    uint16_t NextSequence;     // Of the next transparent packet
    int Fault;                 // SIM_ASI_FAULT_ for the next packet
} SimAsiRx;

typedef struct SimAsiTx
{
    bool Running; // At the last SimAsi_SendFromBuffer
    bool HasSent; // Something went out since ASITXG started
    uint64_t LastMs;
    int Rd;
    bool RdKnown;
    SimAsiTxStats Stats;
    int64_t WindowSyms, WindowData;
    uint8_t* Kept; // SIM_ASI_KEPT_BYTES once a data byte came
    size_t KeptHead, KeptLen;
} SimAsiTx;

typedef struct SimAsiPort
{
    SimAsiState Settings;
    SimAsiSignal Signal;
    SimAsiRx Rx;
    SimAsiTx Tx;
} SimAsiPort;

// The input of a loop, finding packets in the bytes the output sends.
typedef struct SimAsiLoop
{
    int TxIndex, RxIndex; // -1 without a loop
    uint8_t Buf[SIM_ASI_LOOP_BUFFER];
    size_t Len;
    int PacketSize; // 0 while no packets are found
} SimAsiLoop;

static struct
{
    bool Initialised;
    SimAsiPort Ports[SIM_SDI_PORT_COUNT];
    SimAsiLoop Loop;
    int FailFunctionCode;
    int FailCmd;
    uint32_t FailStatus;
    bool TablesBuilt;
    int16_t DecodeTable[2][1024]; // Per running disparity; SIM_ASI_NO_CODE for none
    uint8_t NextRdTable[2][1024];
    uint8_t Scratch
        [65536]; // What SimAsi_SendFromBuffer takes from the transmit buffer at a time
} g_Asi;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureAsi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EnsureAsi(void)
{
    if (!g_Asi.Initialised)
        SimAsi_Reset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ClearSignal(SimAsiSignal* Signal)
{
    memset(Signal, 0, sizeof(*Signal));
    Signal->PacketSize = DT_ASIRX_PCKSIZE_UNKNOWN;
    Signal->Polarity = DT_ASIRX_POLARITY_UNKNOWN;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a command is like to the driver: the sizes of its input and output, and whether
// it needs exclusive access. Every command needs its object enabled.
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
    SimAsiState* Settings = &Port->Settings;
    const int Value = Cmd >= DT_ASIRX_CMD_SET_OPERATIONAL_MODE ? ValueOf(In) : 0;

    switch (Cmd)
    {
    case DT_ASIRX_CMD_GET_OPERATIONAL_STATUS:
        // Running only while there is something to receive, as on a DTA-2178.
        return Answer(Settings->RxMode == DT_FUNC_OPMODE_RUN && Port->Signal.CarrierDetect
                          ? DT_FUNC_OPSTATUS_RUN
                          : DT_FUNC_OPSTATUS_IDLE,
                      Out, OutSize);
    case DT_ASIRX_CMD_GET_PACKET_MODE:
        return Answer(Settings->RxPacketMode, Out, OutSize);
    case DT_ASIRX_CMD_GET_POLARITY_CTRL:
        return Answer(Settings->RxPolarityCtrl, Out, OutSize);
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
        return Answer(Settings->RxSyncMode, Out, OutSize);
    case DT_ASIRX_CMD_GET_TS_BITRATE:
        return Answer(Port->Signal.TsBitrate, Out, OutSize);
    case DT_ASIRX_CMD_GET_VIOL_COUNT:
        return Answer(Port->Signal.ViolCount, Out, OutSize);
    case DT_ASIRX_CMD_SET_OPERATIONAL_MODE:
        if (Value != DT_FUNC_OPMODE_IDLE && Value != DT_FUNC_OPMODE_RUN)
            return DT_STATUS_INVALID_PARAMETER;
        Settings->RxMode = Value;
        return DT_STATUS_OK;
    case DT_ASIRX_CMD_SET_PACKET_MODE:
        if (Value != DT_ASIRX_PCKMODE_AUTO && Value != DT_ASIRX_PCKMODE_RAW)
            return DT_STATUS_INVALID_PARAMETER;
        Settings->RxPacketMode = Value;
        return DT_STATUS_OK;
    case DT_ASIRX_CMD_SET_POLARITY_CTRL:
        if (Value != DT_ASIRX_POLARITY_AUTO && Value != DT_ASIRX_POLARITY_NORMAL &&
            Value != DT_ASIRX_POLARITY_INVERT)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        Settings->RxPolarityCtrl = Value;
        return DT_STATUS_OK;
    default: // DT_ASIRX_CMD_SET_SYNC_MODE
        if (Value != DT_ASIRX_SYNCMODE_AUTO && Value != DT_ASIRX_SYNCMODE_188 &&
            Value != DT_ASIRX_SYNCMODE_204)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        Settings->RxSyncMode = Value;
        return DT_STATUS_OK;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AsiTxGCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t AsiTxGCmd(SimAsiPort* Port, int Cmd, const void* In, void* Out,
                          size_t* OutSize)
{
    SimAsiState* Settings = &Port->Settings;

    switch (Cmd)
    {
    case DT_ASITXG_CMD_CLEAR_INPUT_STATE:
        Settings->TxgInputClears++;
        return DT_STATUS_OK;
    case DT_ASITXG_CMD_GET_ASI_POLARITY:
        return Answer(Settings->TxgPolarity, Out, OutSize);
    case DT_ASITXG_CMD_GET_OPERATIONAL_MODE:
        return Answer(Settings->TxgMode, Out, OutSize);
    case DT_ASITXG_CMD_SET_ASI_POLARITY:
    {
        int Polarity = ValueOf(In);
        if (Polarity != DT_ASITXG_POL_NORMAL && Polarity != DT_ASITXG_POL_INVERT)
            return DT_STATUS_INVALID_PARAMETER;
        Settings->TxgPolarity = Polarity;
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
        Settings->TxgMode = OpMode;
        return DT_STATUS_OK;
    }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_Handles -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimAsi_Handles(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_ASIRX_CMD ||
           FunctionCode == DT_FUNC_CODE_ASITXG_CMD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_Cmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An object that is disabled forgets its settings, as the driver's does when it is
// enabled again.
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
        SimAsiState* Settings = &Port->Settings;
        if (IsRx)
        {
            Settings->RxMode = DT_FUNC_OPMODE_IDLE;
            Settings->RxPacketMode = DT_ASIRX_PCKMODE_AUTO;
            Settings->RxPolarityCtrl = DT_ASIRX_POLARITY_AUTO;
            Settings->RxSyncMode = DT_ASIRX_SYNCMODE_AUTO;
        }
        else
        {
            Settings->TxgMode = DT_BLOCK_OPMODE_IDLE;
            Settings->TxgPolarity = DT_ASITXG_POL_NORMAL;
        }
        return DT_STATUS_NOT_ENABLED;
    }

    return IsRx ? AsiRxCmd(Port, Cmd, In, Out, OutSize)
                : AsiTxGCmd(Port, Cmd, In, Out, OutSize);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Receiving +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsReceiving -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsReceiving(int PortIndex)
{
    const SimAsiPort* Port = &g_Asi.Ports[PortIndex];
    return Port->Settings.RxMode == DT_FUNC_OPMODE_RUN && Port->Signal.CarrierDetect &&
           SimSdiTx_RxRuns(PortIndex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceivePacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The card receives Valid bytes of Payload, zeros for NULL, with Sync the first byte of
// the trailer, OffsetNs after the time of day now. A packet it has no room for is an
// overflow.
//
static void ReceivePacket(int PortIndex, const uint8_t* Payload, int Valid, uint8_t Sync,
                          uint64_t OffsetNs)
{
    SimAsiRx* Rx = &g_Asi.Ports[PortIndex].Rx;
    uint8_t Packet[DT_TRP_SIZE];
    const uint64_t Tod = SimDtPcie_Now() + OffsetNs;
    const uint32_t Seconds = (uint32_t)(Tod / 1000000000u);
    const uint32_t Nanoseconds = (uint32_t)(Tod % 1000000000u);

    memset(Packet, 0, sizeof(Packet));
    for (int i = 0; i < 4; i++)
    {
        Packet[i] = (uint8_t)(Seconds >> (8 * i));
        Packet[4 + i] = (uint8_t)(Nanoseconds >> (8 * i));
    }
    if (Payload != NULL)
        memcpy(Packet + 8, Payload, (size_t)Valid);
    Packet[212] = Sync;
    Packet[213] = (uint8_t)Valid;
    Packet[214] = (uint8_t)Rx->NextSequence;
    Packet[215] = (uint8_t)(Rx->NextSequence >> 8);
    Rx->NextSequence++;

    if (SimSdiTx_RxFreeBytes(PortIndex) < DT_TRP_SIZE)
        SimSdiTx_CountOverflow(PortIndex);
    else
        SimSdiTx_RxWrite(PortIndex, Packet, DT_TRP_SIZE);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiveFromSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The next piece or packet of a port's source, OffsetNs from now.
//
static void ReceiveFromSource(int PortIndex, uint64_t OffsetNs)
{
    SimAsiRx* Rx = &g_Asi.Ports[PortIndex].Rx;
    uint8_t Packet[204];

    if (Rx->UnsyncedLeft > 0)
    {
        // What a DTA-2178 wrote first: 204 bytes and no packet sync.
        Rx->UnsyncedLeft--;
        ReceivePacket(PortIndex, NULL, 204, 0x50, OffsetNs);
        return;
    }

    const int Size = Rx->Source.PacketSize;
    uint8_t Sync = 0x58;
    int Valid = Size;
    SimAsi_MakePacket(Rx->NextPacketNumber++, Size, Packet);
    switch (Rx->Fault)
    {
    case SIM_ASI_FAULT_NIBBLE:
        Sync = 0x08;
        break;
    case SIM_ASI_FAULT_VALID:
        Valid = 100;
        break;
    case SIM_ASI_FAULT_SEQUENCE:
        Rx->NextSequence++;
        break;
    case SIM_ASI_FAULT_NOSYNC:
        Sync = 0x50;
        break;
    default:
        break;
    }
    Rx->Fault = 0;
    ReceivePacket(PortIndex, Packet, Valid, Sync, OffsetNs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_ReceiveIntoBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A receiver that starts receiving begins with the source's pieces without sync, and in
// real time with no packets due.
//
void SimAsi_ReceiveIntoBuffer(int PortIndex)
{
    EnsureAsi();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;
    SimAsiRx* Rx = &g_Asi.Ports[PortIndex].Rx;
    if (!Rx->HasSource)
        return;

    const uint64_t NowMs = OsTime_MonotonicMs();
    if (!IsReceiving(PortIndex))
    {
        Rx->WasReceiving = false;
        return;
    }
    if (!Rx->WasReceiving)
    {
        Rx->WasReceiving = true;
        Rx->UnsyncedLeft = Rx->Source.UnsyncedAtStart;
        Rx->PacketsDue = 0;
        Rx->LastMs = NowMs;
    }

    const double PacketNs =
        (double)Rx->Source.PacketSize * 8 * 1e9 / (double)Rx->Source.Rate;
    int64_t Count;
    if (SimSdiTx_IsRealTime())
    {
        Rx->PacketsDue += (double)(NowMs - Rx->LastMs) * 1e6 / PacketNs;
        Rx->LastMs = NowMs;
        Count = (int64_t)Rx->PacketsDue;
        Rx->PacketsDue -= (double)Count;
    }
    else
    {
        Count = Rx->Source.PacketsPerRead;
    }
    for (int64_t i = 0; i < Count; i++)
        ReceiveFromSource(PortIndex, (uint64_t)((double)i * PacketNs));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Loop +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DropBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void DropBytes(SimAsiLoop* Loop, size_t Count)
{
    memmove(Loop->Buf, Loop->Buf + Count, Loop->Len - Count);
    Loop->Len -= Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindPackets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds and receives the packets in what the loop holds. Packets are found where three
// sync bytes follow each other at 188 or 204 bytes, and lost where one is missing.
//
static void FindPackets(SimAsiLoop* Loop)
{
    const bool Receiving = IsReceiving(Loop->RxIndex);
    for (;;)
    {
        if (Loop->PacketSize == 0)
        {
            if (Loop->Len < SIM_ASI_FIND_BYTES)
                return;
            const uint8_t* B = Loop->Buf;
            if (B[0] == 0x47 && B[188] == 0x47 && B[376] == 0x47)
                Loop->PacketSize = 188;
            else if (B[0] == 0x47 && B[204] == 0x47 && B[408] == 0x47)
                Loop->PacketSize = 204;
            else
                DropBytes(Loop, 1);
            continue;
        }
        if (Loop->Len < (size_t)Loop->PacketSize)
            return;
        if (Loop->Buf[0] != 0x47)
        {
            Loop->PacketSize = 0;
            continue;
        }
        if (Receiving)
            ReceivePacket(Loop->RxIndex, Loop->Buf, Loop->PacketSize, 0x58, 0);
        DropBytes(Loop, (size_t)Loop->PacketSize);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoopBackByte -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void LoopBackByte(uint8_t Byte)
{
    SimAsiLoop* Loop = &g_Asi.Loop;
    if (Loop->Len == SIM_ASI_LOOP_BUFFER)
        FindPackets(Loop);
    if (Loop->Len == SIM_ASI_LOOP_BUFFER)
        DropBytes(Loop, 1);
    Loop->Buf[Loop->Len++] = Byte;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UpdateLoopSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// What the input of the loop sees of the output.
//
static void UpdateLoopSignal(void)
{
    const SimAsiLoop* Loop = &g_Asi.Loop;
    const SimAsiPort* Tx = &g_Asi.Ports[Loop->TxIndex];
    SimAsiSignal* Signal = &g_Asi.Ports[Loop->RxIndex].Signal;

    ClearSignal(Signal);
    if (!Tx->Tx.Running)
        return;
    Signal->CarrierDetect = true;
    Signal->AsiLock = Loop->PacketSize != 0;
    Signal->PacketSize = Loop->PacketSize == 188   ? DT_ASIRX_PCKSIZE_188
                         : Loop->PacketSize == 204 ? DT_ASIRX_PCKSIZE_204
                                                   : DT_ASIRX_PCKSIZE_UNKNOWN;
    Signal->Polarity = Tx->Settings.TxgPolarity == DT_ASITXG_POL_INVERT
                           ? DT_ASIRX_POLARITY_INVERT
                           : DT_ASIRX_POLARITY_NORMAL;
    Signal->TsBitrate = (int)Tx->Tx.Stats.TsBitrate;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sending +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildTables -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The decoder, from the encoder's code.
//
static void BuildTables(void)
{
    if (g_Asi.TablesBuilt)
        return;
    for (int Rd = 0; Rd < 2; Rd++)
    {
        for (int s = 0; s < 1024; s++)
            g_Asi.DecodeTable[Rd][s] = SIM_ASI_NO_CODE;
        for (int b = 0; b < 256; b++)
        {
            int Next;
            uint16_t Code = DtAsiEnc_EncodeByte((uint8_t)b, Rd, &Next);
            g_Asi.DecodeTable[Rd][Code] = (int16_t)b;
            g_Asi.NextRdTable[Rd][Code] = (uint8_t)Next;
        }
    }
    g_Asi.DecodeTable[0][DT_ASI_K28_5_RDNEG] = SIM_ASI_K28_5;
    g_Asi.NextRdTable[0][DT_ASI_K28_5_RDNEG] = 1;
    g_Asi.DecodeTable[1][DT_ASI_K28_5_RDPOS] = SIM_ASI_K28_5;
    g_Asi.NextRdTable[1][DT_ASI_K28_5_RDPOS] = 0;
    g_Asi.TablesBuilt = true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Keep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Keep(SimAsiTx* Tx, uint8_t Byte)
{
    if (Tx->Kept == NULL)
    {
        Tx->Kept = (uint8_t*)DtAlloc_Malloc(SIM_ASI_KEPT_BYTES);
        if (Tx->Kept == NULL)
            return;
    }
    Tx->Kept[(Tx->KeptHead + Tx->KeptLen) % SIM_ASI_KEPT_BYTES] = Byte;
    if (Tx->KeptLen < SIM_ASI_KEPT_BYTES)
        Tx->KeptLen++;
    else
        Tx->KeptHead = (Tx->KeptHead + 1) % SIM_ASI_KEPT_BYTES;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DecodeSym -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Decodes one symbol for the running disparity; the first after the output starts sets
// it.
//
static void DecodeSym(SimAsiTx* Tx, uint16_t Sym, bool Looped)
{
    int Rd = Tx->Rd;
    int Value = g_Asi.DecodeTable[Rd][Sym];

    Tx->Stats.Symbols++;
    Tx->WindowSyms++;
    if (Value == SIM_ASI_NO_CODE)
    {
        Rd = 1 - Rd;
        Value = g_Asi.DecodeTable[Rd][Sym];
        if (Value == SIM_ASI_NO_CODE)
        {
            Tx->Stats.CodeErrors++;
            return;
        }
        if (Tx->RdKnown)
            Tx->Stats.DisparityErrors++;
    }
    Tx->Rd = g_Asi.NextRdTable[Rd][Sym];
    Tx->RdKnown = true;

    if (Value == SIM_ASI_K28_5)
    {
        Tx->Stats.K28++;
        return;
    }
    Tx->Stats.DataBytes++;
    Tx->WindowData++;
    Keep(Tx, (uint8_t)Value);
    if (Looped)
        LoopBackByte((uint8_t)Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_SendFromBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimAsi_SendFromBuffer(int PortIndex)
{
    EnsureAsi();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;
    SimAsiPort* Port = &g_Asi.Ports[PortIndex];
    SimAsiTx* Tx = &Port->Tx;
    const bool Looped = g_Asi.Loop.TxIndex == PortIndex;
    const uint64_t NowMs = OsTime_MonotonicMs();

    const bool Running =
        Port->Settings.TxgMode == DT_BLOCK_OPMODE_RUN && SimSdiTx_PhyRuns(PortIndex);
    if (!Running)
    {
        Tx->Running = Tx->HasSent = Tx->RdKnown = false;
        if (Looped)
            UpdateLoopSignal();
        return;
    }
    if (!Tx->Running)
    {
        Tx->Running = true;
        Tx->LastMs = NowMs;
    }
    if (Looped)
        UpdateLoopSignal(); // The carrier comes before the first byte

    BuildTables();
    const bool RealTime = SimSdiTx_IsRealTime();
    uint64_t Allowed =
        RealTime ? (NowMs - Tx->LastMs) * SIM_ASI_BYTES_PER_MS : UINT64_MAX;
    Tx->LastMs = NowMs;
    while (Allowed > 0)
    {
        size_t Chunk = sizeof(g_Asi.Scratch);
        if (Allowed < Chunk)
            Chunk = (size_t)Allowed & ~(size_t)1;
        if (Chunk == 0)
            break;
        const size_t Taken = SimSdiTx_TakeTxBytes(PortIndex, g_Asi.Scratch, Chunk);
        for (size_t i = 0; i + 1 < Taken; i += 2)
        {
            DecodeSym(Tx,
                      (uint16_t)((g_Asi.Scratch[i] | g_Asi.Scratch[i + 1] << 8) & 0x3FF),
                      Looped);
        }
        Allowed -= Taken;
        if (Taken > 0)
            Tx->HasSent = true;
        if (Taken < Chunk)
        {
            if (RealTime && Tx->HasSent)
                SimSdiTx_CountOverflow(PortIndex); // An underflow, on the same count
            break;
        }
    }

    if (Tx->WindowSyms >= SIM_ASI_RATE_WINDOW)
    {
        Tx->Stats.TsBitrate = Tx->WindowData * 8 * DT_ASI_SYMBOL_RATE / Tx->WindowSyms;
        Tx->WindowSyms = Tx->WindowData = 0;
    }
    if (Looped)
    {
        FindPackets(&g_Asi.Loop);
        UpdateLoopSignal();
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_MakePacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimAsi_MakePacket(uint32_t Number, int Size, uint8_t* Out)
{
    Out[0] = 0x47;
    Out[1] = 0x01;
    Out[2] = 0x00;
    Out[3] = (uint8_t)(0x10 | (Number & 0xF));
    for (int i = 0; i < 4; i++)
        Out[4 + i] = (uint8_t)(Number >> (24 - 8 * i));
    for (int i = 8; i < Size; i++)
        Out[i] = (uint8_t)(Number + (uint32_t)i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimAsi_Reset(void)
{
    for (int i = 0; i < SIM_SDI_PORT_COUNT; i++)
    {
        SimAsiPort* Port = &g_Asi.Ports[i];
        if (g_Asi.Initialised)
            DtAlloc_Free(Port->Tx.Kept);
        memset(Port, 0, sizeof(*Port));
        Port->Settings.RxMode = DT_FUNC_OPMODE_IDLE;
        Port->Settings.RxPacketMode = DT_ASIRX_PCKMODE_AUTO;
        Port->Settings.RxPolarityCtrl = DT_ASIRX_POLARITY_AUTO;
        Port->Settings.RxSyncMode = DT_ASIRX_SYNCMODE_AUTO;
        Port->Settings.TxgMode = DT_BLOCK_OPMODE_IDLE;
        Port->Settings.TxgPolarity = DT_ASITXG_POL_NORMAL;
        ClearSignal(&Port->Signal);
    }
    memset(&g_Asi.Loop, 0, sizeof(g_Asi.Loop));
    g_Asi.Loop.TxIndex = g_Asi.Loop.RxIndex = -1;
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
            ClearSignal(&g_Asi.Ports[PortIndex].Signal);
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
        *State = g_Asi.Ports[PortIndex].Settings;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimAsi_DefaultSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimAsi_DefaultSource(SimAsiSource* Source)
{
    Source->PacketSize = 188;
    Source->Rate = 10000000;
    Source->PacketsPerRead = 8;
    Source->UnsyncedAtStart = 3;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetAsiSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetAsiSource(int PortIndex, const SimAsiSource* Source)
{
    SimDtPcie_Lock();
    EnsureAsi();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
    {
        SimAsiPort* Port = &g_Asi.Ports[PortIndex];
        memset(&Port->Rx, 0, sizeof(Port->Rx));
        ClearSignal(&Port->Signal);
        if (Source != NULL && (Source->PacketSize == 188 || Source->PacketSize == 204) &&
            Source->Rate > 0)
        {
            Port->Rx.HasSource = true;
            Port->Rx.Source = *Source;
            Port->Signal.CarrierDetect = true;
            Port->Signal.AsiLock = true;
            Port->Signal.PacketSize =
                Source->PacketSize == 188 ? DT_ASIRX_PCKSIZE_188 : DT_ASIRX_PCKSIZE_204;
            Port->Signal.Polarity = DT_ASIRX_POLARITY_NORMAL;
            Port->Signal.TsBitrate = (int)Source->Rate;
        }
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_InjectAsiRxFault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_InjectAsiRxFault(int PortIndex, int Fault)
{
    SimDtPcie_Lock();
    EnsureAsi();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        g_Asi.Ports[PortIndex].Rx.Fault = Fault;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetAsiLoopback -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetAsiLoopback(int TxIndex, int RxIndex)
{
    SimDtPcie_Lock();
    EnsureAsi();
    SimAsiLoop* Loop = &g_Asi.Loop;
    if (Loop->RxIndex >= 0)
        ClearSignal(&g_Asi.Ports[Loop->RxIndex].Signal);
    memset(Loop, 0, sizeof(*Loop));
    Loop->TxIndex = Loop->RxIndex = -1;
    if (TxIndex >= 0 && TxIndex < SIM_SDI_PORT_COUNT && RxIndex >= 0 &&
        RxIndex < SIM_SDI_PORT_COUNT && TxIndex != RxIndex)
    {
        Loop->TxIndex = TxIndex;
        Loop->RxIndex = RxIndex;
        memset(&g_Asi.Ports[RxIndex].Rx, 0, sizeof(g_Asi.Ports[RxIndex].Rx));
        UpdateLoopSignal();
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetAsiTxStats -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_GetAsiTxStats(int PortIndex, SimAsiTxStats* Stats)
{
    SimDtPcie_Lock();
    EnsureAsi();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        *Stats = g_Asi.Ports[PortIndex].Tx.Stats;
    else
        memset(Stats, 0, sizeof(*Stats));
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_TakeAsiTxBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t SimDtPcie_TakeAsiTxBytes(int PortIndex, uint8_t* Out, size_t Max)
{
    size_t Taken = 0;
    SimDtPcie_Lock();
    EnsureAsi();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
    {
        SimAsiTx* Tx = &g_Asi.Ports[PortIndex].Tx;
        while (Taken < Max && Tx->KeptLen > 0)
        {
            Out[Taken++] = Tx->Kept[Tx->KeptHead];
            Tx->KeptHead = (Tx->KeptHead + 1) % SIM_ASI_KEPT_BYTES;
            Tx->KeptLen--;
        }
    }
    SimDtPcie_Unlock();
    return Taken;
}

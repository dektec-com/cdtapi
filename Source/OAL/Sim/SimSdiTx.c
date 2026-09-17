// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimSdiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The emulated SDI transmit blocks, their sink and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>
#include <time.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtPcieAbi.h"    // The driver ABI the emulator answers in.
#include "SimDtPcie.h"    // Port counts.
#include "SimSdiTx.h"     // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The largest buffer the DMA controller takes.
#define SIM_TX_MAX_BUFFER (256 * 1024 * 1024)

// What the card takes from the buffer while nothing goes out: the burst FIFO and 16 KB,
// as a DTA-2178's read offset showed.
#define SIM_TX_PIPELINE (SIM_TX_BURST_FIFO_SIZE + 16384)

// The words the card reads the buffer in.
#define SIM_TX_WORD (SIM_TX_PCIE_DATA_WIDTH / 8)

// The header's size before padding: five 32-bit words.
#define SIM_TX_HEADER_BYTES 20

// A frame whose header asks for more bytes than this is refused as a bad header.
#define SIM_TX_MAX_FRAME SIM_TX_MAX_BUFFER

typedef struct SimTxKept
{
    int FrameId;
    int NumLines;
    int SymsHanc;
    int SymsVideo;
    uint16_t* Symbols;
} SimTxKept;

typedef struct SimTxPort
{
    // CDMAC
    int CdmacMode;
    bool Registered;
    int Direction;
    uint8_t* Buffer;
    size_t BufferSize;
    void* BufferUser; // The handle that registered the buffer
    uint32_t ReadOffset;
    uint32_t WriteOffset;
    int TestMode;

    // The card's pipeline: bytes taken from the buffer and not yet sent.
    uint8_t* Pipeline; // SIM_TX_PIPELINE bytes while CDMAC is not idle
    size_t PipeHead;
    size_t PipeLoad;

    // BURSTFIFO
    int BurstMode;
    uint32_t OvfUflCount;
    int MaxLoad;
    int MaxFree;

    // SDITXF
    int TxfMode;
    int NumLinesPerEvent;
    int NumSofsBetweenTod;
    int SofCount;
    bool UflEnabled;
    bool UflLatched;

    // Switches, demultiplexer and encoder
    int SwitchInMode, SwitchOutMode, DmxMode, TxpMode;
    int SwitchIn[2], SwitchOut[2];
    bool Clamp, AncChecksum, LineCrc;

    // SDITXPHY
    int PhyMode;
    bool PhyUnderflow;
    int SofOffsetNs;

    // The sink
    bool InFrame;
    int FrameId;
    int NumLines;
    int SymsHanc, SymsVideo;
    size_t BytesHanc, BytesVideo;
    int LinesDone;
    int SeqNumber;
    uint16_t* Symbols; // The frame being received
    int Starve;
    int FramesSent;
    int HeaderErrors;
    SimTxKept Kept[SIM_TX_KEPT_FRAMES];
    int NumKept;
} SimTxPort;

static struct
{
    bool Initialised;
    SimTxPort Ports[SIM_SDI_PORT_COUNT];
    bool AsLinux;
    int Alignment;
    int FailFunctionCode;
    int FailCmd;
    uint32_t FailStatus;
} g_Tx;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureTx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void EnsureTx(void)
{
    if (!g_Tx.Initialised)
        SimSdiTxReset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Forgets the frame the sink is receiving.
//
static void ClearFrame(SimTxPort* Port)
{
    DtFree(Port->Symbols);
    Port->Symbols = NULL;
    Port->InFrame = false;
    Port->LinesDone = 0;
    Port->SeqNumber = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopPipeline -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The DMA controller goes idle: the pipeline empties and the read offset returns to 0.
//
static void StopPipeline(SimTxPort* Port)
{
    Port->CdmacMode = DT_BLOCK_OPMODE_IDLE;
    DtFree(Port->Pipeline);
    Port->Pipeline = NULL;
    Port->PipeHead = 0;
    Port->PipeLoad = 0;
    Port->ReadOffset = 0;
    ClearFrame(Port);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Disable -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The port is no SDI output: the blocks of the transmitter are idle. Those of the DMA
// stay as they are.
//
static void Disable(SimTxPort* Port)
{
    ClearFrame(Port);
    Port->TxfMode = DT_BLOCK_OPMODE_IDLE;
    Port->SwitchInMode = DT_BLOCK_OPMODE_IDLE;
    Port->SwitchOutMode = DT_BLOCK_OPMODE_IDLE;
    Port->DmxMode = DT_BLOCK_OPMODE_IDLE;
    Port->TxpMode = DT_BLOCK_OPMODE_IDLE;
    Port->PhyMode = DT_FUNC_OPMODE_IDLE;
    Port->PhyUnderflow = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipeline +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Advance -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes what the pipeline has room for from the buffer, in whole words.
//
static void Advance(SimTxPort* Port)
{
    size_t Load, Take, i;

    if (Port->Pipeline == NULL || Port->BurstMode == DT_BLOCK_OPMODE_IDLE ||
        !Port->Registered || Port->Direction != DT_CDMAC_DIR_TX)
    {
        return;
    }

    Load = ((size_t)Port->WriteOffset + Port->BufferSize - Port->ReadOffset) %
           Port->BufferSize;
    Take = SIM_TX_PIPELINE - Port->PipeLoad;
    if (Take > Load)
        Take = Load;
    Take = Take / SIM_TX_WORD * SIM_TX_WORD;

    for (i = 0; i < Take; i++)
    {
        size_t To = (Port->PipeHead + Port->PipeLoad + i) % SIM_TX_PIPELINE;
        Port->Pipeline[To] = Port->Buffer[(Port->ReadOffset + i) % Port->BufferSize];
    }
    Port->PipeLoad += Take;
    Port->ReadOffset = (uint32_t)((Port->ReadOffset + Take) % Port->BufferSize);
    if ((int)Port->PipeLoad > Port->MaxLoad)
        Port->MaxLoad = (int)Port->PipeLoad;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Peek -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The pipeline's byte at Offset from its head.
//
static uint8_t Peek(const SimTxPort* Port, size_t Offset)
{
    return Port->Pipeline[(Port->PipeHead + Offset) % SIM_TX_PIPELINE];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Consume -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Consume(SimTxPort* Port, size_t Count)
{
    Port->PipeHead = (Port->PipeHead + Count) % SIM_TX_PIPELINE;
    Port->PipeLoad -= Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Word32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t Word32(const SimTxPort* Port, size_t Offset)
{
    return (uint32_t)Peek(Port, Offset) | (uint32_t)Peek(Port, Offset + 1) << 8 |
           (uint32_t)Peek(Port, Offset + 2) << 16 |
           (uint32_t)Peek(Port, Offset + 3) << 24;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HeaderBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static size_t HeaderBytes(void)
{
    size_t Alignment = (size_t)g_Tx.Alignment / 8;
    return (SIM_TX_HEADER_BYTES + Alignment - 1) / Alignment * Alignment;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Padded -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes a section of Symbols packed 10-bit symbols takes with its padding.
//
static size_t Padded(int Symbols)
{
    size_t Alignment = (size_t)g_Tx.Alignment / 8;
    size_t Bytes = ((size_t)Symbols * 10 + 7) / 8;
    return (Bytes + Alignment - 1) / Alignment * Alignment;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks the header at the pipeline's head and takes the frame's geometry from it. False
// for a header that does not check.
//
static bool ReadHeader(SimTxPort* Port)
{
    size_t Alignment = (size_t)g_Tx.Alignment / 8;
    uint32_t Word1 = Word32(Port, 4);
    uint32_t Word2 = Word32(Port, 8);
    uint32_t Word3 = Word32(Port, 12);
    uint32_t Word4 = Word32(Port, 16);
    size_t Frame;

    if (Word32(Port, 0) != 0xFFEFFBFEu || (Word1 & 0xF) != 0 || (Word1 >> 4 & 0xF) != 0 ||
        (Word1 >> 8 & 1) != 1 || (Word1 >> 9 & 7) > DT_DRV_SDIRATE_12G)
    {
        return false;
    }

    Port->FrameId = (int)(Word2 & 0xFFFF);
    Port->NumLines = (int)(Word2 >> 16);
    Port->BytesHanc = (size_t)(Word3 & 0xFFFF) * Alignment;
    Port->SymsHanc = (int)(Word3 >> 16);
    Port->BytesVideo = (size_t)(Word4 & 0xFFFF) * Alignment;
    Port->SymsVideo = (int)(Word4 >> 16);

    if (Port->NumLines == 0 || Port->SymsHanc == 0 || Port->SymsVideo == 0 ||
        Port->BytesHanc != Padded(Port->SymsHanc) ||
        Port->BytesVideo != Padded(Port->SymsVideo))
    {
        return false;
    }
    Frame = HeaderBytes() + (size_t)Port->NumLines * (Port->BytesHanc + Port->BytesVideo);
    return Frame <= SIM_TX_MAX_FRAME;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UnpackSection -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Count packed 10-bit symbols, least significant bit first, from the pipeline at Offset.
//
static void UnpackSection(const SimTxPort* Port, size_t Offset, int Count, uint16_t* Out)
{
    uint32_t Accu = 0;
    int Have = 0, i;

    for (i = 0; i < Count; i++)
    {
        while (Have < 10)
        {
            Accu |= (uint32_t)Peek(Port, Offset++) << Have;
            Have += 8;
        }
        Out[i] = (uint16_t)(Accu & 0x3FF);
        Accu >>= 10;
        Have -= 10;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- KeepFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves the frame just received into the kept frames, dropping the oldest when full.
//
static void KeepFrame(SimTxPort* Port)
{
    SimTxKept* Slot;

    if (Port->NumKept == SIM_TX_KEPT_FRAMES)
    {
        DtFree(Port->Kept[0].Symbols);
        memmove(&Port->Kept[0], &Port->Kept[1],
                (SIM_TX_KEPT_FRAMES - 1) * sizeof(Port->Kept[0]));
        Port->NumKept--;
    }
    Slot = &Port->Kept[Port->NumKept++];
    Slot->FrameId = Port->FrameId;
    Slot->NumLines = Port->NumLines;
    Slot->SymsHanc = Port->SymsHanc;
    Slot->SymsVideo = Port->SymsVideo;
    Slot->Symbols = Port->Symbols;
    Port->Symbols = NULL;
    Port->FramesSent++;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Available -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What can be sent now: the pipeline, and the whole words of the buffer behind it.
//
static size_t Available(const SimTxPort* Port)
{
    size_t Load = ((size_t)Port->WriteOffset + Port->BufferSize - Port->ReadOffset) %
                  Port->BufferSize;
    return Port->PipeLoad + Load / SIM_TX_WORD * SIM_TX_WORD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Underflow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Underflow(SimTxPort* Port)
{
    Port->PhyUnderflow = true;
    Port->OvfUflCount++;
    if (Port->UflEnabled)
        Port->UflLatched = true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether data can leave the card: the whole chain runs, one link through the switches.
//
static bool IsSending(const SimTxPort* Port)
{
    return Port->Pipeline != NULL && Port->BurstMode != DT_BLOCK_OPMODE_IDLE &&
           Port->TxfMode == DT_BLOCK_OPMODE_RUN &&
           Port->SwitchInMode == DT_BLOCK_OPMODE_RUN &&
           Port->SwitchOutMode == DT_BLOCK_OPMODE_RUN && Port->SwitchIn[0] == 0 &&
           Port->SwitchIn[1] == 0 && Port->SwitchOut[0] == 0 && Port->SwitchOut[1] == 0 &&
           Port->DmxMode == DT_BLOCK_OPMODE_IDLE &&
           Port->TxpMode == DT_BLOCK_OPMODE_RUN && Port->PhyMode == DT_FUNC_OPMODE_RUN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends the next part of a frame and fills Event. False, sending nothing, when nothing
// can go out; an underflow is counted when the chain runs but the pipeline holds too
// little.
//
static bool NextEvent(SimTxPort* Port, DtIoctlSdiTxFCmdWaitForFmtEventOutput* Event)
{
    size_t Header = 0, Stride, Needed;
    int Lines, i;

    Advance(Port);
    if (!IsSending(Port))
        return false;
    if (Port->Starve > 0)
    {
        Port->Starve--;
        Underflow(Port);
        return false;
    }

    // Find a header that checks, skipping one alignment word after one that does not.
    while (!Port->InFrame)
    {
        Advance(Port);
        if (Port->PipeLoad < HeaderBytes())
        {
            Underflow(Port);
            return false;
        }
        if (ReadHeader(Port))
        {
            Header = HeaderBytes();
            break;
        }
        Port->HeaderErrors++;
        Consume(Port, (size_t)g_Tx.Alignment / 8);
    }

    // The card reads the buffer while it sends, so a part can exceed the pipeline.
    Stride = Port->BytesHanc + Port->BytesVideo;
    Lines =
        Port->NumLinesPerEvent > 0 ? Port->NumLinesPerEvent : (Port->NumLines + 3) / 4;
    if (Lines > Port->NumLines - Port->LinesDone)
        Lines = Port->NumLines - Port->LinesDone;
    Needed = Header + (size_t)Lines * Stride;
    if (Available(Port) < Needed)
    {
        Underflow(Port);
        return false;
    }

    if (!Port->InFrame)
    {
        size_t Symbols =
            (size_t)Port->NumLines * (size_t)(Port->SymsHanc + Port->SymsVideo);

        Port->Symbols = (uint16_t*)DtMalloc(Symbols * sizeof(uint16_t));
        if (Port->Symbols == NULL)
            return false;
        Consume(Port, Header);
        Port->InFrame = true;
        Port->LinesDone = 0;
        Port->SeqNumber = 0;
    }

    for (i = 0; i < Lines; i++)
    {
        uint16_t* Line = Port->Symbols + (size_t)Port->LinesDone *
                                             (size_t)(Port->SymsHanc + Port->SymsVideo);

        Advance(Port);
        UnpackSection(Port, 0, Port->SymsHanc, Line);
        UnpackSection(Port, Port->BytesHanc, Port->SymsVideo, Line + Port->SymsHanc);
        Consume(Port, Stride);
        Port->LinesDone++;
    }

    memset(Event, 0, sizeof(*Event));
    Event->m_FrameId = Port->FrameId;
    Event->m_SeqNumber = Port->SeqNumber;
    Event->m_Underflow = Port->UflLatched ? 1 : 0;
    Port->UflLatched = false;
    Port->UflEnabled = true;
    if (Port->SeqNumber == 0 && Port->NumSofsBetweenTod > 0 &&
        ++Port->SofCount >= Port->NumSofsBetweenTod)
    {
        struct timespec Now;

        timespec_get(&Now, TIME_UTC);
        Event->m_SofTimeValid = 1;
        Event->m_SofTime.m_Seconds = (UInt32)Now.tv_sec;
        Event->m_SofTime.m_Nanoseconds = (UInt32)Now.tv_nsec;
        Port->SofCount = 0;
    }

    Port->SeqNumber++;
    if (Port->LinesDone == Port->NumLines)
    {
        KeepFrame(Port);
        ClearFrame(Port);
    }
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What the driver's I/O stub knows of a command: its sizes, whether it needs exclusive
// access (DtIoctlProperties.h), and whether the block must be enabled.
typedef struct SimTxCmdProps
{
    int FunctionCode;
    int Cmd;
    size_t InSize;
    size_t OutSize;
    bool Exclusive;
    bool MustBeEnabled;
} SimTxCmdProps;

#define HDR sizeof(DtIoctlInputDataHdr)

static const SimTxCmdProps g_Cmds[] = {
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_GET_PROPERTIES, HDR,
     sizeof(DtIoctlCDmaCCmdGetPropertiesOutput), false, false},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER,
     sizeof(DtIoctlCDmaCCmdAllocateBufferInput),
     sizeof(DtIoctlCDmaCCmdAllocateBufferOutput), true, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_FREE_BUFFER, HDR, 0, true, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ISSUE_CHANNEL_FLUSH, HDR, 0, true, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlCDmaCCmdSetOpModeInput), 0, true, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_SET_TEST_MODE,
     sizeof(DtIoctlCDmaCCmdSetTestModeInput), 0, true, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_GET_TX_READ_OFFSET, HDR,
     sizeof(DtIoctlCDmaCCmdGetTxRdOffsetOutput), false, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_SET_TX_WRITE_OFFSET,
     sizeof(DtIoctlCDmaCCmdSetTxWrOffsetInput), 0, true, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_GET_REORDER_BUF_STATUS, HDR,
     sizeof(DtIoctlCDmaCCmdGetReorderBufStatusOutput), false, true},
    {DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_CLEAR_REORDER_BUF_MIN_MAX, HDR, 0, true, true},

    {DT_FUNC_CODE_BURSTFIFO_CMD, DT_BURSTFIFO_CMD_GET_PROPERTIES, HDR,
     sizeof(DtIoctlBurstFifoCmdGetPropertiesOutput), false, false},
    {DT_FUNC_CODE_BURSTFIFO_CMD, DT_BURSTFIFO_CMD_GET_FIFO_STATUS, HDR,
     sizeof(DtIoctlBurstFifoCmdGetFifoStatusOutput), false, true},
    {DT_FUNC_CODE_BURSTFIFO_CMD, DT_BURSTFIFO_CMD_CLEAR_FIFO_MAX,
     sizeof(DtIoctlBurstFifoCmdClearFifoMaxInput), 0, false, true},
    {DT_FUNC_CODE_BURSTFIFO_CMD, DT_BURSTFIFO_CMD_GET_OVFL_UFL_COUNT, HDR,
     sizeof(DtIoctlBurstFifoCmdGetOvfUflCountOutput), false, true},
    {DT_FUNC_CODE_BURSTFIFO_CMD, DT_BURSTFIFO_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlBurstFifoCmdSetOpModeInput), 0, true, true},

    {DT_FUNC_CODE_SDITXF_CMD, DT_SDITXF_CMD_GET_STREAM_ALIGNMENT, HDR,
     sizeof(DtIoctlSdiTxFCmdGetStreamAlignmentOutput), false, true},
    {DT_FUNC_CODE_SDITXF_CMD, DT_SDITXF_CMD_SET_FMT_EVENT_SETTING,
     sizeof(DtIoctlSdiTxFCmdSetFmtEventSettingInput), 0, true, true},
    {DT_FUNC_CODE_SDITXF_CMD, DT_SDITXF_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlSdiTxFCmdSetOpModeInput), 0, true, true},
    {DT_FUNC_CODE_SDITXF_CMD, DT_SDITXF_CMD_WAIT_FOR_FMT_EVENT,
     sizeof(DtIoctlSdiTxFCmdWaitForFmtEventInput),
     sizeof(DtIoctlSdiTxFCmdWaitForFmtEventOutput), true, true},

    {DT_FUNC_CODE_SWITCH_CMD, DT_SWITCH_CMD_SET_POSITION,
     sizeof(DtIoctlSwitchCmdSetPositionInput), 0, true, true},
    {DT_FUNC_CODE_SWITCH_CMD, DT_SWITCH_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlSwitchCmdSetOpModeInput), 0, true, true},

    {DT_FUNC_CODE_SDIDMX12G_CMD, DT_SDIDMX12G_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlSdiDmx12GCmdSetOpModeInput), 0, true, true},

    {DT_FUNC_CODE_SDITXP_CMD, DT_SDITXP_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlSdiTxPCmdSetOpModeInput), 0, false, true},
    {DT_FUNC_CODE_SDITXP_CMD, DT_SDITXP_CMD_SET_GENERATION_MODE,
     sizeof(DtIoctlSdiTxPCmdSetGenModeInput), 0, false, true},

    {DT_FUNC_CODE_SDITXPHY_CMD, DT_SDITXPHY_CMD_SET_OPERATIONAL_MODE,
     sizeof(DtIoctlSdiTxPhyCmdSetOpModeInput), 0, true, true},
    {DT_FUNC_CODE_SDITXPHY_CMD, DT_SDITXPHY_CMD_GET_UNDERFLOW_FLAG, HDR,
     sizeof(DtIoctlSdiTxPhyCmdGetUnderflowFlagOutput), false, true},
    {DT_FUNC_CODE_SDITXPHY_CMD, DT_SDITXPHY_CMD_CLEAR_UNDERFLOW_FLAG, HDR, 0, true, true},
    {DT_FUNC_CODE_SDITXPHY_CMD, DT_SDITXPHY_CMD_SET_START_OF_FRAME_OFFSET,
     sizeof(DtIoctlSdiTxPhyCmdSetStartOfFrameOffsetInput), 0, false, true},
};

#undef HDR

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const SimTxCmdProps* FindCmd(int FunctionCode, int Cmd)
{
    size_t i;

    for (i = 0; i < sizeof(g_Cmds) / sizeof(g_Cmds[0]); i++)
    {
        if (g_Cmds[i].FunctionCode == FunctionCode && g_Cmds[i].Cmd == Cmd)
            return &g_Cmds[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValidMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool ValidMode(int OpMode)
{
    return OpMode == DT_BLOCK_OPMODE_IDLE || OpMode == DT_BLOCK_OPMODE_STANDBY ||
           OpMode == DT_BLOCK_OPMODE_RUN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpModeOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The mode a request carries; every mode request has it after the header.
//
static int OpModeOf(const void* In)
{
    return ((const DtIoctlCDmaCCmdSetOpModeInput*)In)->m_OpMode;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AllocateBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtIoStubBcCDMAC_OnCmd and DtBcCDMAC_AllocateBuffer, for a buffer of the process.
//
static uint32_t AllocateBuffer(SimTxPort* Port, void* Handle, const void* In, void* Out,
                               size_t* OutSize)
{
    const DtIoctlCDmaCCmdAllocateBufferInput* Request =
        (const DtIoctlCDmaCCmdAllocateBufferInput*)In;
    const size_t Unit = SIM_TX_PREFETCH_PAGES * 4096;
    uint8_t* Buffer;
    size_t Size;

    if (g_Tx.AsLinux)
    {
        Buffer = (uint8_t*)(uintptr_t)Request->m_BufferAddr;
        Size = Request->m_BufferSize > 0 ? (size_t)Request->m_BufferSize : 0;
    }
    else
    {
        // The output grows by the buffer's size (DtIoStubBcCDMAC_AppendDynamicSize).
        if (Request->m_BufferSize <= 0 || *OutSize < (size_t)Request->m_BufferSize)
            return DT_STATUS_INVALID_PARAMETER;
        Buffer = (uint8_t*)Out;
        Size = *OutSize;
    }

    if (Request->m_Direction != DT_CDMAC_DIR_RX &&
        Request->m_Direction != DT_CDMAC_DIR_TX)
        return DT_STATUS_INVALID_PARAMETER;
    if (Buffer == NULL || (uintptr_t)Buffer % 4096 != 0 || Size == 0 || Size % Unit != 0)
        return DT_STATUS_INVALID_PARAMETER;
    if (Size > SIM_TX_MAX_BUFFER)
        return DT_STATUS_BUF_TOO_LARGE;
    if (Port->Registered)
        return DT_STATUS_IN_USE;

    Port->Registered = true;
    Port->Direction = Request->m_Direction;
    Port->Buffer = Buffer;
    Port->BufferSize = Size;
    Port->BufferUser = Handle;
    Port->ReadOffset = 0;
    Port->WriteOffset = 0;
    if (g_Tx.AsLinux)
        *OutSize = sizeof(DtIoctlCDmaCCmdAllocateBufferOutput);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CdmacCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t CdmacCmd(SimTxPort* Port, void* Handle, int Cmd, const void* In,
                         void* Out, size_t* OutSize)
{
    switch (Cmd)
    {
    case DT_CDMAC_CMD_GET_PROPERTIES:
    {
        DtIoctlCDmaCCmdGetPropertiesOutput* Props =
            (DtIoctlCDmaCCmdGetPropertiesOutput*)Out;

        memset(Props, 0, sizeof(*Props));
        Props->m_Capabilities = DT_CDMAC_CAP_RX | DT_CDMAC_CAP_TX;
        Props->m_PrefetchSize = SIM_TX_PREFETCH_PAGES;
        Props->m_PcieDataWidth = SIM_TX_PCIE_DATA_WIDTH;
        Props->m_ReorderBufSize = SIM_TX_REORDER_BUF_SIZE;
        *OutSize = sizeof(*Props);
        return DT_STATUS_OK;
    }
    case DT_CDMAC_CMD_ALLOCATE_BUFFER:
        return AllocateBuffer(Port, Handle, In, Out, OutSize);
    case DT_CDMAC_CMD_FREE_BUFFER:
        if (Port->CdmacMode != DT_BLOCK_OPMODE_IDLE)
            return DT_STATUS_INVALID_IN_OPMODE;
        Port->Registered = false;
        Port->Buffer = NULL;
        Port->BufferSize = 0;
        Port->BufferUser = NULL;
        return DT_STATUS_OK;
    case DT_CDMAC_CMD_ISSUE_CHANNEL_FLUSH:
        return Port->CdmacMode == DT_BLOCK_OPMODE_IDLE ? DT_STATUS_OK
                                                       : DT_STATUS_INVALID_IN_OPMODE;
    case DT_CDMAC_CMD_SET_OPERATIONAL_MODE:
    {
        int OpMode = OpModeOf(In);

        if (!ValidMode(OpMode))
            return DT_STATUS_INVALID_PARAMETER;
        if (OpMode == Port->CdmacMode)
            return OpMode == DT_BLOCK_OPMODE_RUN ? DT_STATUS_IN_USE : DT_STATUS_OK;
        if (OpMode == DT_BLOCK_OPMODE_IDLE)
        {
            StopPipeline(Port);
            return DT_STATUS_OK;
        }
        if (!Port->Registered)
            return DT_STATUS_NOT_INITIALISED;
        if (Port->Pipeline == NULL)
        {
            Port->Pipeline = (uint8_t*)DtMalloc(SIM_TX_PIPELINE);
            if (Port->Pipeline == NULL)
                return DT_STATUS_OUT_OF_MEMORY;
        }
        Port->CdmacMode = OpMode;
        return DT_STATUS_OK;
    }
    case DT_CDMAC_CMD_SET_TEST_MODE:
    {
        int TestMode = ((const DtIoctlCDmaCCmdSetTestModeInput*)In)->m_TestMode;

        if (TestMode != DT_CDMAC_TESTMODE_NORMAL &&
            TestMode != DT_CDMAC_TESTMODE_TEST_INT &&
            TestMode != DT_CDMAC_TESTMODE_TEST_EXT)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        if (Port->CdmacMode != DT_BLOCK_OPMODE_IDLE)
            return DT_STATUS_INVALID_IN_OPMODE;
        Port->TestMode = TestMode;
        return DT_STATUS_OK;
    }
    case DT_CDMAC_CMD_GET_TX_READ_OFFSET:
        Advance(Port);
        ((DtIoctlCDmaCCmdGetTxRdOffsetOutput*)Out)->m_TxReadOffset = Port->ReadOffset;
        *OutSize = sizeof(DtIoctlCDmaCCmdGetTxRdOffsetOutput);
        return DT_STATUS_OK;
    case DT_CDMAC_CMD_SET_TX_WRITE_OFFSET:
    {
        UInt Offset = ((const DtIoctlCDmaCCmdSetTxWrOffsetInput*)In)->m_TxWriteOffset;

        if (Offset != 0 && Offset >= Port->BufferSize)
            return DT_STATUS_INVALID_PARAMETER;
        if (!Port->Registered || Port->Direction != DT_CDMAC_DIR_TX)
            return DT_STATUS_NOT_SUPPORTED;
        Port->WriteOffset = Offset;
        Advance(Port);
        return DT_STATUS_OK;
    }
    case DT_CDMAC_CMD_GET_REORDER_BUF_STATUS:
    {
        DtIoctlCDmaCCmdGetReorderBufStatusOutput* Status =
            (DtIoctlCDmaCCmdGetReorderBufStatusOutput*)Out;
        size_t Reorder = Port->PipeLoad > SIM_TX_BURST_FIFO_SIZE
                             ? Port->PipeLoad - SIM_TX_BURST_FIFO_SIZE
                             : 0;

        Status->m_ReorderBufLoad =
            (Int)(Reorder > SIM_TX_REORDER_BUF_SIZE ? SIM_TX_REORDER_BUF_SIZE : Reorder);
        Status->m_ReorderBufMinMaxLoad = 0;
        *OutSize = sizeof(*Status);
        return DT_STATUS_OK;
    }
    default: // DT_CDMAC_CMD_CLEAR_REORDER_BUF_MIN_MAX
        return DT_STATUS_OK;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BurstFifoCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t BurstFifoCmd(SimTxPort* Port, int Cmd, const void* In, void* Out,
                             size_t* OutSize)
{
    switch (Cmd)
    {
    case DT_BURSTFIFO_CMD_GET_PROPERTIES:
    {
        DtIoctlBurstFifoCmdGetPropertiesOutput* Props =
            (DtIoctlBurstFifoCmdGetPropertiesOutput*)Out;

        Props->m_Capabilities = DT_BURSTFIFO_CAP_RX | DT_BURSTFIFO_CAP_TX;
        Props->m_DataWidth = SIM_TX_PCIE_DATA_WIDTH;
        Props->m_BurstFifoSize = SIM_TX_BURST_FIFO_SIZE;
        *OutSize = sizeof(*Props);
        return DT_STATUS_OK;
    }
    case DT_BURSTFIFO_CMD_GET_FIFO_STATUS:
    {
        DtIoctlBurstFifoCmdGetFifoStatusOutput* Status =
            (DtIoctlBurstFifoCmdGetFifoStatusOutput*)Out;
        int Load;

        Advance(Port);
        Load = (int)(Port->PipeLoad > SIM_TX_BURST_FIFO_SIZE ? SIM_TX_BURST_FIFO_SIZE
                                                             : Port->PipeLoad);
        if (SIM_TX_BURST_FIFO_SIZE - Load > Port->MaxFree)
            Port->MaxFree = SIM_TX_BURST_FIFO_SIZE - Load;
        Status->m_CurLoad = Load;
        Status->m_CurFree = SIM_TX_BURST_FIFO_SIZE - Load;
        Status->m_MaxLoad = Port->MaxLoad > SIM_TX_BURST_FIFO_SIZE
                                ? SIM_TX_BURST_FIFO_SIZE
                                : Port->MaxLoad;
        Status->m_MaxFree = Port->MaxFree;
        *OutSize = sizeof(*Status);
        return DT_STATUS_OK;
    }
    case DT_BURSTFIFO_CMD_CLEAR_FIFO_MAX:
    {
        const DtIoctlBurstFifoCmdClearFifoMaxInput* Request =
            (const DtIoctlBurstFifoCmdClearFifoMaxInput*)In;

        if (Request->m_ClearMaxFree != 0)
            Port->MaxFree = 0;
        if (Request->m_ClearMaxLoad != 0)
            Port->MaxLoad = 0;
        return DT_STATUS_OK;
    }
    case DT_BURSTFIFO_CMD_GET_OVFL_UFL_COUNT:
        ((DtIoctlBurstFifoCmdGetOvfUflCountOutput*)Out)->m_OvfUflCount =
            (Int)Port->OvfUflCount;
        *OutSize = sizeof(DtIoctlBurstFifoCmdGetOvfUflCountOutput);
        return DT_STATUS_OK;
    default: // DT_BURSTFIFO_CMD_SET_OPERATIONAL_MODE
        if (!ValidMode(OpModeOf(In)))
            return DT_STATUS_INVALID_PARAMETER;
        Port->BurstMode = OpModeOf(In);
        return DT_STATUS_OK;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiTxFCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A wait with a time-out outside -1 to 1000 ms is refused, as DtBcSDITXF_WaitForFmtEvent
// refuses it, and so is a wait while the formatter is not running.
//
static uint32_t SdiTxFCmd(SimTxPort* Port, int Cmd, const void* In, void* Out,
                          size_t* OutSize, int* SleepMs)
{
    switch (Cmd)
    {
    case DT_SDITXF_CMD_GET_STREAM_ALIGNMENT:
        ((DtIoctlSdiTxFCmdGetStreamAlignmentOutput*)Out)->m_StreamAlignment =
            g_Tx.Alignment;
        *OutSize = sizeof(DtIoctlSdiTxFCmdGetStreamAlignmentOutput);
        return DT_STATUS_OK;
    case DT_SDITXF_CMD_SET_FMT_EVENT_SETTING:
    {
        const DtIoctlSdiTxFCmdSetFmtEventSettingInput* Request =
            (const DtIoctlSdiTxFCmdSetFmtEventSettingInput*)In;

        if (Request->m_NumLinesPerEvent < 0 || Request->m_NumSofsBetweenTod < 0)
            return DT_STATUS_INVALID_PARAMETER;
        Port->NumLinesPerEvent = Request->m_NumLinesPerEvent;
        Port->NumSofsBetweenTod = Request->m_NumSofsBetweenTod;
        return DT_STATUS_OK;
    }
    case DT_SDITXF_CMD_SET_OPERATIONAL_MODE:
    {
        int OpMode = OpModeOf(In);

        if (OpMode != DT_BLOCK_OPMODE_IDLE && OpMode != DT_BLOCK_OPMODE_RUN)
            return DT_STATUS_INVALID_PARAMETER;
        if (OpMode == Port->TxfMode)
            return DT_STATUS_OK;
        if (OpMode == DT_BLOCK_OPMODE_RUN)
        {
            Port->UflEnabled = false;
            Port->UflLatched = false;
            Port->SofCount = 0;
        }
        ClearFrame(Port);
        Port->TxfMode = OpMode;
        return DT_STATUS_OK;
    }
    default: // DT_SDITXF_CMD_WAIT_FOR_FMT_EVENT
    {
        const DtIoctlSdiTxFCmdWaitForFmtEventInput* Request =
            (const DtIoctlSdiTxFCmdWaitForFmtEventInput*)In;
        DtIoctlSdiTxFCmdWaitForFmtEventOutput* Event =
            (DtIoctlSdiTxFCmdWaitForFmtEventOutput*)Out;

        if (Request->m_Timeout < -1 || Request->m_Timeout > 1000)
            return DT_STATUS_INVALID_PARAMETER;
        if (Port->TxfMode != DT_BLOCK_OPMODE_RUN)
            return DT_STATUS_INVALID_IN_OPMODE;
        if (!NextEvent(Port, Event))
        {
            *SleepMs = Request->m_Timeout > 0 ? Request->m_Timeout : 0;
            return DT_STATUS_TIMEOUT;
        }
        *OutSize = sizeof(*Event);
        return DT_STATUS_OK;
    }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SwitchCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SDI_DEMUX_IN has one input and two outputs, SDI_DEMUX_OUT two inputs and one output; a
// position outside them is refused, as DtBcSWITCH_SetPosition refuses it.
//
static uint32_t SwitchCmd(SimTxPort* Port, bool IsIn, int Cmd, const void* In)
{
    if (Cmd == DT_SWITCH_CMD_SET_POSITION)
    {
        const DtIoctlSwitchCmdSetPositionInput* Request =
            (const DtIoctlSwitchCmdSetPositionInput*)In;
        int Inputs = IsIn ? 1 : 2;
        int Outputs = IsIn ? 2 : 1;
        int* Position = IsIn ? Port->SwitchIn : Port->SwitchOut;

        if (Request->m_InputIndex < 0 || Request->m_InputIndex >= Inputs ||
            Request->m_OutputIndex < 0 || Request->m_OutputIndex >= Outputs)
        {
            return DT_STATUS_INVALID_PARAMETER;
        }
        Position[0] = Request->m_InputIndex;
        Position[1] = Request->m_OutputIndex;
        return DT_STATUS_OK;
    }

    if (!ValidMode(OpModeOf(In)))
        return DT_STATUS_INVALID_PARAMETER;
    if (IsIn)
        Port->SwitchInMode = OpModeOf(In);
    else
        Port->SwitchOutMode = OpModeOf(In);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiTxPCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t SdiTxPCmd(SimTxPort* Port, int Cmd, const void* In)
{
    if (Cmd == DT_SDITXP_CMD_SET_GENERATION_MODE)
    {
        const DtIoctlSdiTxPCmdSetGenModeInput* Request =
            (const DtIoctlSdiTxPCmdSetGenModeInput*)In;

        Port->Clamp = Request->m_ClampEnable != 0;
        Port->AncChecksum = Request->m_AdpChecksumEnable != 0;
        Port->LineCrc = Request->m_LineCrcEnable != 0;
        return DT_STATUS_OK;
    }
    if (!ValidMode(OpModeOf(In)))
        return DT_STATUS_INVALID_PARAMETER;
    Port->TxpMode = OpModeOf(In);
    return DT_STATUS_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiTxPhyCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Going idle clears the underflow flag, as a restart on the card showed.
//
static uint32_t SdiTxPhyCmd(SimTxPort* Port, int Cmd, const void* In, void* Out,
                            size_t* OutSize)
{
    switch (Cmd)
    {
    case DT_SDITXPHY_CMD_GET_UNDERFLOW_FLAG:
        ((DtIoctlSdiTxPhyCmdGetUnderflowFlagOutput*)Out)->m_UflFlag =
            Port->PhyUnderflow ? 1 : 0;
        *OutSize = sizeof(DtIoctlSdiTxPhyCmdGetUnderflowFlagOutput);
        return DT_STATUS_OK;
    case DT_SDITXPHY_CMD_CLEAR_UNDERFLOW_FLAG:
        Port->PhyUnderflow = false;
        return DT_STATUS_OK;
    case DT_SDITXPHY_CMD_SET_START_OF_FRAME_OFFSET:
        Port->SofOffsetNs = ((const DtIoctlSdiTxPhyCmdSetStartOfFrameOffsetInput*)In)
                                ->m_StartOfFrameOffsetNs;
        return DT_STATUS_OK;
    default: // DT_SDITXPHY_CMD_SET_OPERATIONAL_MODE
        if (!ValidMode(OpModeOf(In)))
            return DT_STATUS_INVALID_PARAMETER;
        Port->PhyMode = OpModeOf(In);
        if (Port->PhyMode == DT_FUNC_OPMODE_IDLE)
            Port->PhyUnderflow = false;
        return DT_STATUS_OK;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiTxTakes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimSdiTxTakes(int FunctionCode)
{
    return FunctionCode == DT_FUNC_CODE_CDMAC_CMD ||
           FunctionCode == DT_FUNC_CODE_BURSTFIFO_CMD ||
           FunctionCode == DT_FUNC_CODE_SDITXF_CMD ||
           FunctionCode == DT_FUNC_CODE_SWITCH_CMD ||
           FunctionCode == DT_FUNC_CODE_SDIDMX12G_CMD ||
           FunctionCode == DT_FUNC_CODE_SDITXP_CMD ||
           FunctionCode == DT_FUNC_CODE_SDITXPHY_CMD;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TypeMatches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a block of Type with Role is the emulated one that takes FunctionCode.
//
static bool TypeMatches(int FunctionCode, int Type, const char* Role)
{
    switch (FunctionCode)
    {
    case DT_FUNC_CODE_CDMAC_CMD:
        return Type == DT_BLOCK_TYPE_CDMAC;
    case DT_FUNC_CODE_BURSTFIFO_CMD:
        return Type == DT_BLOCK_TYPE_BURSTFIFO;
    case DT_FUNC_CODE_SDITXF_CMD:
        return Type == DT_BLOCK_TYPE_SDITXF;
    case DT_FUNC_CODE_SWITCH_CMD:
        return Type == DT_BLOCK_TYPE_SWITCH &&
               (strcmp(Role, "SDI_DEMUX_IN") == 0 || strcmp(Role, "SDI_DEMUX_OUT") == 0);
    case DT_FUNC_CODE_SDIDMX12G_CMD:
        return Type == DT_BLOCK_TYPE_SDIDMX12G;
    case DT_FUNC_CODE_SDITXP_CMD:
        return Type == DT_BLOCK_TYPE_SDITXP;
    case DT_FUNC_CODE_SDITXPHY_CMD:
        return Type == DT_FUNC_TYPE_SDITXPHY;
    default:
        return false;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiTxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimSdiTxCmd(void* Handle, int PortIndex, int FunctionCode, int Type,
                     const char* Role, int Cmd, uint32_t Access, bool Enabled,
                     const void* In, size_t InSize, void* Out, size_t* OutSize,
                     int* SleepMs)
{
    const SimTxCmdProps* Props;
    SimTxPort* Port;

    EnsureTx();
    *SleepMs = 0;
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT ||
        !TypeMatches(FunctionCode, Type, Role))
    {
        return DT_STATUS_NOT_SUPPORTED;
    }
    Port = &g_Tx.Ports[PortIndex];

    Props = FindCmd(FunctionCode, Cmd);
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
    if (g_Tx.FailFunctionCode == FunctionCode && g_Tx.FailCmd == Cmd &&
        g_Tx.FailStatus != 0)
        return g_Tx.FailStatus;
    if (!Enabled && FunctionCode != DT_FUNC_CODE_CDMAC_CMD &&
        FunctionCode != DT_FUNC_CODE_BURSTFIFO_CMD)
    {
        Disable(Port);
        if (Props->MustBeEnabled)
            return DT_STATUS_NOT_ENABLED;
    }

    switch (FunctionCode)
    {
    case DT_FUNC_CODE_CDMAC_CMD:
        return CdmacCmd(Port, Handle, Cmd, In, Out, OutSize);
    case DT_FUNC_CODE_BURSTFIFO_CMD:
        return BurstFifoCmd(Port, Cmd, In, Out, OutSize);
    case DT_FUNC_CODE_SDITXF_CMD:
        return SdiTxFCmd(Port, Cmd, In, Out, OutSize, SleepMs);
    case DT_FUNC_CODE_SWITCH_CMD:
        return SwitchCmd(Port, strcmp(Role, "SDI_DEMUX_IN") == 0, Cmd, In);
    case DT_FUNC_CODE_SDIDMX12G_CMD:
        if (!ValidMode(OpModeOf(In)))
            return DT_STATUS_INVALID_PARAMETER;
        Port->DmxMode = OpModeOf(In);
        return DT_STATUS_OK;
    case DT_FUNC_CODE_SDITXP_CMD:
        return SdiTxPCmd(Port, Cmd, In);
    default: // DT_FUNC_CODE_SDITXPHY_CMD
        return SdiTxPhyCmd(Port, Cmd, In, Out, OutSize);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiTxCloseHandle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtBcCDMAC_OnCloseFile: the controller goes idle and lets go of the buffer.
//
void SimSdiTxCloseHandle(void* Handle)
{
    int i;

    EnsureTx();
    for (i = 0; i < SIM_SDI_PORT_COUNT; i++)
    {
        SimTxPort* Port = &g_Tx.Ports[i];

        if (Handle != NULL && Port->Registered && Port->BufferUser == Handle)
        {
            StopPipeline(Port);
            Port->Registered = false;
            Port->Buffer = NULL;
            Port->BufferSize = 0;
            Port->BufferUser = NULL;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimSdiTxReset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimSdiTxReset(void)
{
    int i, k;

    for (i = 0; i < SIM_SDI_PORT_COUNT; i++)
    {
        SimTxPort* Port = &g_Tx.Ports[i];

        if (g_Tx.Initialised)
        {
            DtFree(Port->Pipeline);
            DtFree(Port->Symbols);
            for (k = 0; k < Port->NumKept; k++)
                DtFree(Port->Kept[k].Symbols);
        }
        memset(Port, 0, sizeof(*Port));
        Port->CdmacMode = DT_BLOCK_OPMODE_IDLE;
        Port->BurstMode = DT_BLOCK_OPMODE_IDLE;
        Port->TxfMode = DT_BLOCK_OPMODE_IDLE;
        Port->SwitchInMode = DT_BLOCK_OPMODE_IDLE;
        Port->SwitchOutMode = DT_BLOCK_OPMODE_IDLE;
        Port->DmxMode = DT_BLOCK_OPMODE_IDLE;
        Port->TxpMode = DT_BLOCK_OPMODE_IDLE;
        Port->PhyMode = DT_FUNC_OPMODE_IDLE;
        Port->Clamp = Port->AncChecksum = Port->LineCrc = true;
    }
#if defined(_WIN32) || defined(_WIN64)
    g_Tx.AsLinux = false;
#else
    g_Tx.AsLinux = true;
#endif
    g_Tx.Alignment = SIM_TX_STREAM_ALIGNMENT;
    g_Tx.FailFunctionCode = -1;
    g_Tx.FailCmd = -1;
    g_Tx.FailStatus = 0;
    g_Tx.Initialised = true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieRegisterTxBufferAsLinux -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieRegisterTxBufferAsLinux(bool AsLinux)
{
    EnsureTx();
    g_Tx.AsLinux = AsLinux;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetTxAlignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieSetTxAlignment(int AlignmentBits)
{
    EnsureTx();
    g_Tx.Alignment = AlignmentBits;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieRunTxEvents -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int SimDtPcieRunTxEvents(int PortIndex, int Events)
{
    DtIoctlSdiTxFCmdWaitForFmtEventOutput Event;
    int i;

    EnsureTx();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return 0;
    for (i = 0; i < Events && NextEvent(&g_Tx.Ports[PortIndex], &Event); i++)
    {
    }
    return i;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieStarveTx -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieStarveTx(int PortIndex, int Events)
{
    EnsureTx();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        g_Tx.Ports[PortIndex].Starve = Events;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieFailTxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieFailTxCmd(int FunctionCode, int Cmd, uint32_t Status)
{
    EnsureTx();
    g_Tx.FailFunctionCode = FunctionCode;
    g_Tx.FailCmd = Cmd;
    g_Tx.FailStatus = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieGetTxState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieGetTxState(int PortIndex, SimTxState* State)
{
    const SimTxPort* Port;

    EnsureTx();
    memset(State, 0, sizeof(*State));
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;
    Port = &g_Tx.Ports[PortIndex];

    State->CdmacMode = Port->CdmacMode;
    State->BurstMode = Port->BurstMode;
    State->TxfMode = Port->TxfMode;
    State->SwitchInMode = Port->SwitchInMode;
    State->SwitchOutMode = Port->SwitchOutMode;
    State->DmxMode = Port->DmxMode;
    State->TxpMode = Port->TxpMode;
    State->PhyMode = Port->PhyMode;
    memcpy(State->SwitchIn, Port->SwitchIn, sizeof(State->SwitchIn));
    memcpy(State->SwitchOut, Port->SwitchOut, sizeof(State->SwitchOut));
    State->Clamp = Port->Clamp;
    State->AncChecksum = Port->AncChecksum;
    State->LineCrc = Port->LineCrc;
    State->BufferRegistered = Port->Registered;
    State->BufferSize = Port->BufferSize;
    State->ReadOffset = Port->ReadOffset;
    State->WriteOffset = Port->WriteOffset;
    State->PipelineLoad = Port->PipeLoad;
    State->NumLinesPerEvent = Port->NumLinesPerEvent;
    State->NumSofsBetweenTod = Port->NumSofsBetweenTod;
    State->TestMode = Port->TestMode;
    State->StartOfFrameOffsetNs = Port->SofOffsetNs;
    State->PhyUnderflow = Port->PhyUnderflow;
    State->BurstOvfUflCount = Port->OvfUflCount;
    State->FramesSent = Port->FramesSent;
    State->HeaderErrors = Port->HeaderErrors;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieTxFrameCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int SimDtPcieTxFrameCount(int PortIndex)
{
    EnsureTx();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return 0;
    return g_Tx.Ports[PortIndex].NumKept;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieGetTxFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcieGetTxFrame(int PortIndex, int Index, SimTxFrame* Frame)
{
    const SimTxKept* Kept;

    EnsureTx();
    memset(Frame, 0, sizeof(*Frame));
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT || Index < 0 ||
        Index >= g_Tx.Ports[PortIndex].NumKept)
    {
        return false;
    }
    Kept = &g_Tx.Ports[PortIndex].Kept[Index];
    Frame->FrameId = Kept->FrameId;
    Frame->NumLines = Kept->NumLines;
    Frame->SymsHanc = Kept->SymsHanc;
    Frame->SymsVideo = Kept->SymsVideo;
    Frame->Symbols = Kept->Symbols;
    return true;
}

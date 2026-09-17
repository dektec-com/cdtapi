// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtOutpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The SDI output channel: DtOutpChannel on the transmit blocks of a port
//
// SPDX-License-Identifier: BSD-3-Clause
//
// DTAPI transmits through DtOutpChannel, AsiSdiOutpChannel_Bb2 and SdiTxImpl_Bb2, which
// fill a software FIFO that a Matrix row empties into MxChannelMemlessTx, which drives
// the port's transmit blocks. This file drives the blocks itself and converts the raw
// frames Write takes straight into the DMA buffer: it aligns the stream on frames as
// SdiTxImpl_Bb2 does, codes each line into its place behind a header, and moves the
// write offset on when a frame is complete. A thread keeps the signal while sending by
// writing a black frame whenever less than a frame is left. See
// Documentation/0008-transmit-channel.md for where it departs from DTAPI.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"         // Interface being implemented.
#include "Core/DtAlloc.h"       // Allocation seam.
#include "Device/DtDevice.h"    // The device and its port capabilities.
#include "Device/DtFunc.h"      // Finding the transmit blocks.
#include "DtIoConfig.h"         // Validating I/O configurations.
#include "DtPcieAbi.h"          // Operational modes, types and firmware statuses.
#include "OAL/OsDmaBuffer.h"    // The DMA buffer.
#include "OAL/OsThread.h"       // The lock, the thread, its event, sleeping.
#include "Video/DtFrameProps.h" // The frame rate.
#include "Video/DtSdiFrame.h"   // The buffer's format and the raw frame.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DTAPI's values for what CDTAPI.h does not define.
//

#define DT_INSTANT_DETACH 1  // DTAPI_INSTANT_DETACH
#define DT_WAIT_UNTIL_SENT 2 // DTAPI_WAIT_UNTIL_SENT

#define DT_TXMODE_TS 0x10                       // DTAPI_TXMODE_TS
#define DT_TXMODE_TS_MASK (DT_TXMODE_TS | 0x0F) // DTAPI_TXMODE_TS_MASK
#define DT_TXMODE_192 (DT_TXMODE_TS | 0x02)     // DTAPI_TXMODE_192

// SdiTxImpl_Bb2's typical and maximum FIFO size, reported for a port without a buffer.
#define DT_FIFO_SIZE_TYP (48 * 1024 * 1024)
#define DT_FIFO_SIZE_MAX (64 * 1024 * 1024)

// The bounds on the DMA buffer, the frames it is sized for, and the fewest it must hold.
#define DT_BUF_MIN (8 * 1024 * 1024)
#define DT_BUF_MAX (256 * 1024 * 1024)
#define DT_BUF_FRAMES 5
#define DT_BUF_MIN_FRAMES 2

// The format events per frame MxChannelMemlessTx asks for.
#define DT_FMT_EVENTS_PER_FRAME 4

// How long DtOutpChannel::Detach waits for users of the channel: ten times 10 ms.
#define DT_DETACH_TRIES 10
#define DT_DETACH_PAUSE_MS 10

// A detach that waits until everything is sent gives up after a second without a format
// event.
#define DT_SENT_STALL_MS 1000

// The deadline of a wait without a time limit.
#define DT_NO_DEADLINE UINT64_MAX

// DoStandbyToRunImpl reads the burst FIFO's load at most five times for 75 % full.
#define DT_BURST_POLLS 5

// The PHY's underflow flag is read every so many format events, as the Matrix does.
#define DT_PHY_POLL_EVENTS 50

// The largest header with its padding: 20 bytes padded to 512 bits.
#define DT_MAX_TX_HEADER 64

// SdiTxImpl_Bb2's search for the start of line 1 of an SD frame.
#define DT_SD_IN_SYNC 0
#define DT_SD_FIND_FIELD2 1
#define DT_SD_FIND_FRAME_START 2

// Where the stream is: looking for a frame, in its lines, or in the padding after them.
#define DT_STAGE_SEARCH 0
#define DT_STAGE_LINES 1
#define DT_STAGE_PADDING 2

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtOutpChannelC
{
    OsMutex* Lock; // Guards everything below
    bool Attached;
    int Detachers; // Detaches waiting for writes to leave
    int Writers;   // Write and WriteFrame calls between their start and their return

    DtDevice Device; // The channel's own handle to the device
    int Port;        // From 1
    int PortIndex;
    uint32_t Caps; // DT_CAP_ flags of the port

    // The transmit blocks, held exclusively while attached.
    DtFuncInstance AfTx, AfDma;
    int Cdmac, Burst, Txf, SwitchIn, SwitchOut, Dmx, Txp, Phy; // UUIDs

    int IoStdValue; // The port's I/O standard
    int IoStdSubValue;

    int TxMode;
    int SymbolBits; // 8, 10 or 16, from the transmit mode
    int TxControl;

    // Flags.
    bool FifoUfl, FifoUflLatched;
    bool DmaUfl, DmaUflLatched;

    // The configured standard; Layout.VidStd is DTAPI_VIDSTD_UNKNOWN without a buffer.
    DtSdiFrameLayout Layout;
    size_t CodedSize; // A coded frame with its header
    size_t RawSize;   // A raw frame in the current transmit mode
    OsDmaBuffer Buf;
    bool Registered;
    size_t MaxLoad;    // The buffer less the data word kept free
    size_t WordBytes;  // A PCIe data word, which the card reads the buffer in
    int BurstFifoSize; // Bytes
    int QuarterMs;     // A quarter frame period, at least 1 ms
    uint8_t* Black;    // The coded lines of a black frame
    uint8_t* LineBuf;  // A coded line that runs across the end of the buffer
    uint8_t* RawBuf;   // The raw bytes of a line not yet complete
    size_t RawBufSize;

    // The buffer while holding or sending.
    size_t WriteOffset; // Where the next frame's header goes, and the driver's offset
    uint64_t Committed; // Bytes committed since the DMA controller was set running
    int NextFrameId;

    // The frame being written.
    int Stage;
    int SdSync;
    bool Reserved;         // Its header is written and room for it was found
    int LinesDone;         // Lines coded into the buffer
    int Phase;             // The bit the next line starts at in its first byte
    size_t RawHave;        // Bytes in RawBuf
    size_t FrameBytesLeft; // Raw bytes of the frame, padding included, not yet taken

    // The thread that keeps the signal while sending.
    OsThread* Thread;
    bool StopThread;
    OsEvent* Room; // Set after every format event, and to wake a write for a detach
    int Events;
    bool Started;  // A format event came since the channel held
    int SendingId; // The frame ID of the last format event
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wrap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t Wrap(const DtOutpChannel* Chan, size_t Offset)
{
    return Offset % Chan->Buf.Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies Size bytes into the buffer from Offset on, across its end.
//
static void PutAt(DtOutpChannel* Chan, size_t Offset, const uint8_t* Data, size_t Size)
{
    size_t First = Chan->Buf.Size - Offset;

    if (First > Size)
        First = Size;
    memcpy(Chan->Buf.Data + Offset, Data, First);
    memcpy(Chan->Buf.Data, Data + First, Size - First);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyWithin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies Size bytes of the buffer from offset From to offset To, both across its end.
// The two ranges do not overlap.
//
static void CopyWithin(DtOutpChannel* Chan, size_t From, size_t To, size_t Size)
{
    while (Size > 0)
    {
        size_t Chunk = Size;

        if (Chunk > Chan->Buf.Size - From)
            Chunk = Chan->Buf.Size - From;
        if (Chunk > Chan->Buf.Size - To)
            Chunk = Chan->Buf.Size - To;
        memcpy(Chan->Buf.Data + To, Chan->Buf.Data + From, Chunk);
        From = Wrap(Chan, From + Chunk);
        To = Wrap(Chan, To + Chunk);
        Size -= Chunk;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the header of a frame with FrameId, padded with zeros, at Offset.
//
static void PutHeader(DtOutpChannel* Chan, size_t Offset, int FrameId)
{
    uint8_t Bytes[DT_MAX_TX_HEADER];

    memset(Bytes, 0, sizeof(Bytes));
    DtSdiFrameTxHeader Header;
    DtSdiFrame_TxHeaderInit(&Chan->Layout, FrameId, &Header);
    DtSdiFrame_EncodeTxHeader(&Header, Bytes);
    PutAt(Chan, Offset, Bytes, (size_t)Chan->Layout.TxHeaderBytes);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes of committed frames the card has not yet taken. Right after the DMA
// controller is set running a DTA-2178 reports a read offset of an earlier run for a
// while; the load is therefore never more than what was committed since.
//
static DtapiResult ReadLoad(DtOutpChannel* Chan, size_t* Load)
{
    uint32_t ReadOffset = 0;
    DtapiResult Result = DtPcieCmd_CdmacGetTxReadOffset(Chan->Device.Drv, Chan->Cdmac,
                                                        Chan->PortIndex, &ReadOffset);

    *Load = 0;
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Chan->Buf.Size)
        return DTAPI_E_DEV_DRIVER;

    *Load = Wrap(Chan, Chan->WriteOffset + Chan->Buf.Size - ReadOffset);
    if ((uint64_t)*Load > Chan->Committed)
        *Load = (size_t)Chan->Committed;
    if (*Load > Chan->MaxLoad)
        *Load = Chan->MaxLoad;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UnsentFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The committed frames that have not gone out, the frame going out included. Frame IDs
// count the frames committed since the channel held, and a format event names the frame
// going out; before the first event none has.
//
static int UnsentFrames(const DtOutpChannel* Chan)
{
    return (Chan->NextFrameId - (Chan->Started ? Chan->SendingId : 0)) & 0xFFFF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Forgets the frame being written, and starts looking for line 1 again.
//
static void ResetFrame(DtOutpChannel* Chan)
{
    Chan->Stage = DT_STAGE_SEARCH;
    Chan->SdSync = DT_SD_IN_SYNC;
    Chan->Reserved = false;
    Chan->LinesDone = 0;
    Chan->Phase = 0;
    Chan->RawHave = 0;
    Chan->FrameBytesLeft = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CommitFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frame at the write offset is complete: the card may take it.
//
static DtapiResult CommitFrame(DtOutpChannel* Chan)
{
    size_t Offset = Wrap(Chan, Chan->WriteOffset + Chan->CodedSize);
    DtapiResult Result = DtPcieCmd_CdmacSetTxWriteOffset(
        Chan->Device.Drv, Chan->Cdmac, Chan->PortIndex, (uint32_t)Offset);

    if (Result != DTAPI_OK)
        return Result;
    Chan->WriteOffset = Offset;
    Chan->Committed += Chan->CodedSize;
    Chan->NextFrameId = (Chan->NextFrameId + 1) & 0xFFFF;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InsertBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Commits a black frame at the write offset. The part of a frame a write has put there
// moves one frame further, with the next frame ID; when the buffer has no room for both,
// the black frame takes its place and the write looks for the next frame.
//
static DtapiResult InsertBlack(DtOutpChannel* Chan, size_t Load)
{
    const DtSdiFrameLayout* Layout = &Chan->Layout;
    size_t Coded = Chan->CodedSize;
    size_t Free = Chan->MaxLoad - Load;
    size_t Partial = 0;

    if (Free < Coded)
        return DTAPI_OK;
    if (Chan->Reserved)
    {
        Partial = (size_t)Layout->TxHeaderBytes +
                  (size_t)Chan->LinesDone * (size_t)Layout->Stride;
        if (Free < 2 * Coded)
        {
            ResetFrame(Chan);
            Partial = 0;
        }
    }

    if (Partial > 0)
    {
        CopyWithin(Chan, Chan->WriteOffset, Wrap(Chan, Chan->WriteOffset + Coded),
                   Partial);
        PutHeader(Chan, Wrap(Chan, Chan->WriteOffset + Coded),
                  (Chan->NextFrameId + 1) & 0xFFFF);
    }
    PutHeader(Chan, Chan->WriteOffset, Chan->NextFrameId);
    PutAt(Chan, Wrap(Chan, Chan->WriteOffset + (size_t)Layout->TxHeaderBytes),
          Chan->Black, (size_t)Layout->NumLines * (size_t)Layout->Stride);

    DtapiResult Result = CommitFrame(Chan);
    if (Result == DTAPI_OK)
        Chan->FifoUfl = Chan->FifoUflLatched = true;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Keeper -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// While sending: waits for the formatter's format events, the only waiter for them,
// takes their underflow flag and, now and then, the PHY's, and writes a black frame when
// the frame going out is the last one written. Wakes a write waiting for room after each
// event.
//
static void Keeper(void* Context)
{
    DtOutpChannel* Chan = (DtOutpChannel*)Context;

    OsThread_RaisePriority();
    OsMutex_Lock(Chan->Lock);
    OsDrv* Drv = Chan->Device.Drv;
    int Txf = Chan->Txf;
    int PortIndex = Chan->PortIndex;
    int WaitMs = Chan->QuarterMs < 1000 ? Chan->QuarterMs : 1000;
    OsMutex_Unlock(Chan->Lock);

    for (;;)
    {
        DtSdiTxFEvent Event;
        bool Failed = false;

        DtapiResult Result =
            DtPcieCmd_SdiTxFWaitForFmtEvent(Drv, Txf, PortIndex, WaitMs, &Event);

        OsMutex_Lock(Chan->Lock);
        if (Chan->StopThread)
        {
            OsMutex_Unlock(Chan->Lock);
            return;
        }
        if (Result == DTAPI_OK)
        {
            Chan->Started = true;
            Chan->SendingId = Event.FrameId;
            if (Event.Underflow)
                Chan->FifoUfl = Chan->FifoUflLatched = true;
        }
        if (Result != DTAPI_OK && Result != DTAPI_E_TIMEOUT)
            Failed = true;

        if (Result != DTAPI_OK || ++Chan->Events % DT_PHY_POLL_EVENTS == 0)
        {
            bool Underflow = false;

            if (DtPcieCmd_SdiTxPhyGetUnderflowFlag(Drv, Chan->Phy, PortIndex,
                                                   &Underflow) == DTAPI_OK)
            {
                Chan->DmaUfl = Underflow;
                if (Underflow)
                {
                    Chan->DmaUflLatched = true;
                    DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Chan->Phy, PortIndex);
                }
            }
        }

        // The frame going out is the last: a black frame follows it.
        size_t Load;
        if (Chan->Started && UnsentFrames(Chan) <= 1 && ReadLoad(Chan, &Load) == DTAPI_OK)
            InsertBlack(Chan, Load);
        OsEvent_Set(Chan->Room);
        OsMutex_Unlock(Chan->Lock);

        // A wait that fails at once would otherwise spin.
        if (Failed)
            OsTime_SleepMs(WaitMs);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopKeeper -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Stops the thread and waits for it, releasing the lock while it does, since the thread
// takes the lock to see that it must stop.
//
static void StopKeeper(DtOutpChannel* Chan)
{
    OsThread* Thread = Chan->Thread;

    if (Thread == NULL)
        return;
    Chan->StopThread = true;
    Chan->Thread = NULL;
    OsMutex_Unlock(Chan->Lock);
    OsThread_Join(Thread);
    OsMutex_Lock(Chan->Lock);
    Chan->StopThread = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= States +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BlocksToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every block idle, downstream first, as DoStandbyToIdleImpl. Returns the first failure;
// the blocks after it are set idle all the same.
//
static DtapiResult BlocksToIdle(DtOutpChannel* Chan)
{
    OsDrv* Drv = Chan->Device.Drv;
    int Index = Chan->PortIndex;
    DtapiResult Results[8];

    Results[0] = DtPcieCmd_SdiTxPhySetOpMode(Drv, Chan->Phy, Index, DT_FUNC_OPMODE_IDLE);
    Results[1] = DtPcieCmd_SdiTxPSetOpMode(Drv, Chan->Txp, Index, DT_BLOCK_OPMODE_IDLE);
    Results[2] =
        DtPcieCmd_SwitchSetOpMode(Drv, Chan->SwitchOut, Index, DT_BLOCK_OPMODE_IDLE);
    Results[3] =
        DtPcieCmd_SdiDmx12GSetOpMode(Drv, Chan->Dmx, Index, DT_BLOCK_OPMODE_IDLE);
    Results[4] =
        DtPcieCmd_SwitchSetOpMode(Drv, Chan->SwitchIn, Index, DT_BLOCK_OPMODE_IDLE);
    Results[5] = DtPcieCmd_SdiTxFSetOpMode(Drv, Chan->Txf, Index, DT_BLOCK_OPMODE_IDLE);
    Results[6] =
        DtPcieCmd_BurstFifoSetOpMode(Drv, Chan->Burst, Index, DT_BLOCK_OPMODE_IDLE);
    Results[7] = DtPcieCmd_CdmacSetOpMode(Drv, Chan->Cdmac, Index, DT_BLOCK_OPMODE_IDLE);

    for (size_t i = 0; i < sizeof(Results) / sizeof(Results[0]); i++)
    {
        if (Results[i] != DTAPI_OK)
            return Results[i];
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IdleToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DoIdleToStandyImpl: the pipeline runs and fills from the start of the buffer, and the
// PHY waits. With 8-bit symbols or a 4K standard holding fails as the Matrix's row
// validation fails DTAPI's (MxOutpDma::ValidateRowConfigRaw), before a block changes.
//
static DtapiResult IdleToHold(DtOutpChannel* Chan)
{
    OsDrv* Drv = Chan->Device.Drv;
    int Index = Chan->PortIndex;

    if (Chan->SymbolBits == 8 || !Chan->Registered)
        return DTAPI_E_CONFIG_RAW_SDI;

    DtapiResult Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Chan->Cdmac, Index);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTxWriteOffset(Drv, Chan->Cdmac, Index, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Chan->Cdmac, Index, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_BurstFifoSetOpMode(Drv, Chan->Burst, Index, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxFSetOpMode(Drv, Chan->Txf, Index, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_SwitchSetOpMode(Drv, Chan->SwitchIn, Index, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_SdiDmx12GSetOpMode(Drv, Chan->Dmx, Index, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_SwitchSetOpMode(Drv, Chan->SwitchOut, Index, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPSetOpMode(Drv, Chan->Txp, Index, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_SdiTxPhySetOpMode(Drv, Chan->Phy, Index, DT_FUNC_OPMODE_STANDBY);
    if (Result != DTAPI_OK)
    {
        BlocksToIdle(Chan);
        return Result;
    }

    Chan->WriteOffset = 0;
    Chan->Committed = 0;
    Chan->NextFrameId = 0;
    Chan->Started = false;
    Chan->SendingId = 0;
    ResetFrame(Chan);
    Chan->TxControl = DTAPI_TXCTRL_HOLD;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToSend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiTxImpl_Bb2::TxHold2Send's check for a frame that has not gone out, then
// DoStandbyToRunImpl: the burst FIFO is given five reads to fill to 75 %, its and the
// reorder buffer's statistics are cleared, and the PHY runs. The PHY's underflow flag of
// an earlier run is cleared too.
//
static DtapiResult HoldToSend(DtOutpChannel* Chan)
{
    OsDrv* Drv = Chan->Device.Drv;
    int Index = Chan->PortIndex;
    DtBurstFifoStatus Status = {0};

    if (UnsentFrames(Chan) < 1)
        return DTAPI_E_INSUF_LOAD;

    DtapiResult Result;
    for (int Poll = 0; Poll < DT_BURST_POLLS; Poll++)
    {
        Result = DtPcieCmd_BurstFifoGetStatus(Drv, Chan->Burst, Index, &Status);
        if (Result != DTAPI_OK)
            return Result;
        if (Status.CurLoad >= Chan->BurstFifoSize * 3 / 4)
            break;
    }

    Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Chan->Cdmac, Index);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Chan->Burst, Index, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Chan->Phy, Index);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Chan->Phy, Index, DT_FUNC_OPMODE_RUN);
    if (Result != DTAPI_OK)
        return Result;

    Chan->StopThread = false;
    Chan->Events = 0;
    Chan->Thread = OsThread_Start(Keeper, Chan);
    if (Chan->Thread == NULL)
    {
        DtPcieCmd_SdiTxPhySetOpMode(Drv, Chan->Phy, Index, DT_FUNC_OPMODE_STANDBY);
        return DTAPI_E_OUT_OF_MEM;
    }
    Chan->TxControl = DTAPI_TXCTRL_SEND;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DoRunToStandbyImpl, after the thread has stopped; the underflow status is cleared, the
// latched flag kept, as TxSend2Hold does.
//
static DtapiResult SendToHold(DtOutpChannel* Chan)
{
    StopKeeper(Chan);
    DtapiResult Result = DtPcieCmd_SdiTxPhySetOpMode(
        Chan->Device.Drv, Chan->Phy, Chan->PortIndex, DT_FUNC_OPMODE_STANDBY);
    Chan->FifoUfl = false;
    Chan->TxControl = DTAPI_TXCTRL_HOLD;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every block idle; what the buffer held is forgotten. The channel is idle afterwards
// even when a block refused.
//
static DtapiResult HoldToIdle(DtOutpChannel* Chan)
{
    DtapiResult Result = BlocksToIdle(Chan);

    ResetFrame(Chan);
    Chan->TxControl = DTAPI_TXCTRL_IDLE;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiTxImpl_Bb2::SetTxControl: IDLE to SEND goes through HOLD, and SEND to IDLE too.
//
static DtapiResult SetTxControl(DtOutpChannel* Chan, int TxControl)
{
    DtapiResult Result = DTAPI_OK;

    if (Chan->TxControl == TxControl)
        return DTAPI_OK;
    if (TxControl != DTAPI_TXCTRL_IDLE && TxControl != DTAPI_TXCTRL_HOLD &&
        TxControl != DTAPI_TXCTRL_SEND)
    {
        return DTAPI_E_INVALID_ARG;
    }

    if (Chan->TxControl == DTAPI_TXCTRL_IDLE)
        Result = IdleToHold(Chan);
    else if (Chan->TxControl == DTAPI_TXCTRL_SEND)
        Result = SendToHold(Chan);
    if (Result != DTAPI_OK || TxControl == DTAPI_TXCTRL_HOLD)
        return Result;

    if (TxControl == DTAPI_TXCTRL_SEND)
        return HoldToSend(Chan);
    return HoldToIdle(Chan);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SdiTxImpl_Bb2::Reset: idle, which forgets what the buffer held, and every flag cleared.
//
static DtapiResult ResetFifo(DtOutpChannel* Chan)
{
    DtapiResult Result = SetTxControl(Chan, DTAPI_TXCTRL_IDLE);

    if (Result != DTAPI_OK)
        return Result;
    Chan->FifoUfl = Chan->FifoUflLatched = false;
    Chan->DmaUfl = Chan->DmaUflLatched = false;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BufferSizeFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Room for five frames plus the raw frames DTAPI's 48 MB FIFO holds, rounded up to a
// power of two within the Matrix's bounds, and to whole prefetch units of pages.
//
static size_t BufferSizeFor(const DtOutpChannel* Chan, int PrefetchSize)
{
    size_t Raw = DtSdiFrame_RawSize(&Chan->Layout, 10);
    size_t Wanted = (DT_BUF_FRAMES + DT_FIFO_SIZE_TYP / Raw) * Chan->CodedSize;
    size_t Unit = 4096 * (size_t)(PrefetchSize > 0 ? PrefetchSize : 1);
    size_t Size = DT_BUF_MIN;

    while (Size < Wanted && Size < DT_BUF_MAX)
        Size *= 2;
    return (Size + Unit - 1) / Unit * Unit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The DMA controller lets go of the buffer, which is then freed, and the standard's
// buffers go too. Failures are ignored, as DtPalCDMAC_Tx::CleanUp ignores them.
//
static void FreeBuffer(DtOutpChannel* Chan)
{
    if (Chan->Registered)
    {
        DtPcieCmd_CdmacSetOpMode(Chan->Device.Drv, Chan->Cdmac, Chan->PortIndex,
                                 DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(Chan->Device.Drv, Chan->Cdmac, Chan->PortIndex);
    }
    Chan->Registered = false;
    OsDmaBuffer_Free(&Chan->Buf);
    DtAlloc_Free(Chan->Black);
    DtAlloc_Free(Chan->LineBuf);
    DtAlloc_Free(Chan->RawBuf);
    Chan->Black = Chan->LineBuf = Chan->RawBuf = NULL;
    Chan->RawBufSize = 0;
    memset(&Chan->Layout, 0, sizeof(Chan->Layout));
    Chan->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    Chan->CodedSize = Chan->RawSize = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// MxChannelMemlessTx::SetVidStd for the port's I/O standard, while idle: format events,
// the stream alignment and the format, the start-of-frame offset, the switches around
// the demultiplexer, and a buffer for the standard, registered anew only when its size
// changes. A 4K standard, which DTAPI's raw row does not take, leaves the channel without
// a buffer; see IdleToHold.
//
static DtapiResult ConfigureChannel(DtOutpChannel* Chan)
{
    OsDrv* Drv = Chan->Device.Drv;
    int Index = Chan->PortIndex;
    DtCdmacProps Props = {0};
    DtBurstFifoProps Burst = {0};
    DtSdiFrameLayout Layout = {0};
    int Alignment = 0;

    if (Chan->IoStdValue == DTAPI_IOCONFIG_6GSDI ||
        Chan->IoStdValue == DTAPI_IOCONFIG_12GSDI || DtVidStd_Is4k(Chan->IoStdSubValue))
    {
        FreeBuffer(Chan);
        return DTAPI_OK;
    }
    DtFrameProps Frame;
    if (!DtFrameProps_Init(&Frame, Chan->IoStdSubValue))
    {
        FreeBuffer(Chan);
        return DTAPI_E_INVALID_VIDSTD;
    }

    DtapiResult Result =
        DtPcieCmd_SdiTxFGetStreamAlignment(Drv, Chan->Txf, Index, &Alignment);
    if (Result == DTAPI_OK &&
        !DtSdiFrame_LayoutInit(&Layout, Chan->IoStdSubValue, Alignment))
        Result = DTAPI_E_INTERNAL;
    if (Result == DTAPI_OK && Layout.TxHeaderBytes > DT_MAX_TX_HEADER)
        Result = DTAPI_E_INTERNAL;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxFSetFmtEventSetting(
            Drv, Chan->Txf, Index,
            (Layout.NumLines + DT_FMT_EVENTS_PER_FRAME - 1) / DT_FMT_EVENTS_PER_FRAME + 1,
            1);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetStartOfFrameOffset(Drv, Chan->Phy, Index, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SwitchSetPosition(Drv, Chan->SwitchIn, Index, 0, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SwitchSetPosition(Drv, Chan->SwitchOut, Index, 0, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Chan->Cdmac, Index, &Props);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetProps(Drv, Chan->Burst, Index, &Burst);
    if (Result != DTAPI_OK)
    {
        FreeBuffer(Chan);
        return Result;
    }

    size_t Size = Chan->Buf.Size;
    if (Chan->Layout.VidStd == DTAPI_VIDSTD_UNKNOWN ||
        Chan->Layout.Stride != Layout.Stride ||
        Chan->Layout.NumLines != Layout.NumLines ||
        Chan->Layout.TxHeaderBytes != Layout.TxHeaderBytes)
    {
        Size = 0;
    }
    Chan->Layout = Layout;
    Chan->CodedSize = DtSdiFrame_TxCodedSize(&Layout);
    Chan->RawSize = DtSdiFrame_RawSize(&Layout, Chan->SymbolBits);

    // The standard's black frame and the buffers of a write.
    size_t Line = DtSdiFrame_RawLineBits(&Layout, 16) / 8 + 2;
    DtAlloc_Free(Chan->Black);
    DtAlloc_Free(Chan->LineBuf);
    DtAlloc_Free(Chan->RawBuf);
    Chan->Black =
        (uint8_t*)DtAlloc_Malloc((size_t)Layout.NumLines * (size_t)Layout.Stride);
    Chan->LineBuf = (uint8_t*)DtAlloc_Malloc((size_t)Layout.Stride);
    Chan->RawBuf = (uint8_t*)DtAlloc_Malloc(Line);
    Chan->RawBufSize = Line;
    if (Chan->Black == NULL || Chan->LineBuf == NULL || Chan->RawBuf == NULL)
    {
        FreeBuffer(Chan);
        return DTAPI_E_OUT_OF_MEM;
    }
    DtSdiFrame_BlackLines(&Layout, Chan->Black);

    // A buffer of another size replaces the registered one.
    if (!Chan->Registered || Size != BufferSizeFor(Chan, Props.PrefetchSize))
    {
        Size = BufferSizeFor(Chan, Props.PrefetchSize);
        if (Chan->Registered)
        {
            DtPcieCmd_CdmacSetOpMode(Drv, Chan->Cdmac, Index, DT_BLOCK_OPMODE_IDLE);
            DtPcieCmd_CdmacFreeBuffer(Drv, Chan->Cdmac, Index);
            Chan->Registered = false;
        }
        OsDmaBuffer_Free(&Chan->Buf);

        Result = OsDmaBuffer_Alloc(Size, &Chan->Buf) == 0 ? DTAPI_OK : DTAPI_E_OUT_OF_MEM;
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_CdmacAllocateBuffer(Drv, Chan->Cdmac, Index,
                                                   DT_CDMAC_DIR_TX, &Chan->Buf);
        Chan->Registered = Result == DTAPI_OK;
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_CdmacSetTestMode(Drv, Chan->Cdmac, Index,
                                                DT_CDMAC_TESTMODE_NORMAL);
        if (Result != DTAPI_OK)
        {
            FreeBuffer(Chan);
            return Result;
        }
    }

    Chan->WordBytes = (size_t)Props.PcieDataWidth / 8;
    Chan->MaxLoad = Chan->Buf.Size - Chan->WordBytes;
    if (Chan->MaxLoad / Chan->CodedSize < DT_BUF_MIN_FRAMES)
    {
        FreeBuffer(Chan);
        return DTAPI_E_INTERNAL;
    }
    Chan->BurstFifoSize = Burst.FifoSize;

    int Num;
    int Den;
    DtVidStd_Fps(Chan->IoStdSubValue, &Num, &Den);
    Chan->QuarterMs = Den * 1000 / Num / DT_FMT_EVENTS_PER_FRAME;
    if (Chan->QuarterMs < 1)
        Chan->QuarterMs = 1;
    ResetFrame(Chan);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Symbol Index of a raw line in the transmit mode, from its first byte.
//
static uint32_t StartSymbol(const DtOutpChannel* Chan, const uint8_t* Bytes, size_t Index)
{
    if (Chan->SymbolBits == 16)
        return ((uint32_t)Bytes[2 * Index] | (uint32_t)Bytes[2 * Index + 1] << 8) & 0x3FF;
    else
    {
        size_t Bit = Index * 10;
        uint32_t Value = (uint32_t)Bytes[Bit / 8] | (uint32_t)Bytes[Bit / 8 + 1] << 8;

        return (Value >> (Bit % 8)) & 0x3FF;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes the start of a frame is recognised by: twelve symbols in HD and 3G, four in
// SD.
//
static size_t StartBytes(const DtOutpChannel* Chan)
{
    size_t Symbols = Chan->Layout.SdiRate == DT_SDIRATE_SD ? 4 : 12;

    return Symbols * (size_t)Chan->SymbolBits / 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsFrameStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiTxImpl_Bb2::CheckEav, comparing the upper eight bits of each symbol. HD and 3G start
// with the EAV and line number of line 1. SD has no line number: its first frame starts
// at any line in the vertical blanking of field 1, and after a mismatch the search first
// needs an active line of field 2 and then again a blanking line of field 1.
//
static bool IsFrameStart(DtOutpChannel* Chan, const uint8_t* Bytes)
{
    static const uint32_t Hd[12] = {0x3FF, 0x3FF, 0,     0,     0,     0,
                                    0x2D8, 0x2D8, 0x204, 0x204, 0x200, 0x200};
    static const uint32_t SdFrameStart[4] = {0x3FF, 0, 0, 0x2D8};
    static const uint32_t SdField2[4] = {0x3FF, 0, 0, 0x368};
    bool IsSd = Chan->Layout.SdiRate == DT_SDIRATE_SD;
    const uint32_t* Symbols = !IsSd                               ? Hd
                              : Chan->SdSync == DT_SD_FIND_FIELD2 ? SdField2
                                                                  : SdFrameStart;
    size_t Count = IsSd ? 4 : 12;
    size_t i;

    for (i = 0; i < Count; i++)
    {
        if ((StartSymbol(Chan, Bytes, i) & 0x3FC) != (Symbols[i] & 0x3FC))
        {
            if (IsSd && Chan->SdSync == DT_SD_IN_SYNC)
                Chan->SdSync = DT_SD_FIND_FIELD2;
            return false;
        }
    }
    if (IsSd && Chan->SdSync == DT_SD_FIND_FIELD2)
    {
        Chan->SdSync = DT_SD_FIND_FRAME_START;
        return false;
    }
    if (IsSd)
        Chan->SdSync = DT_SD_IN_SYNC;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Line 1 starts here; Held of its bytes are in RawBuf already.
//
static void StartFrame(DtOutpChannel* Chan, size_t Held)
{
    Chan->Stage = DT_STAGE_LINES;
    Chan->Reserved = false;
    Chan->LinesDone = 0;
    Chan->Phase = 0;
    Chan->RawHave = Held;
    Chan->FrameBytesLeft = Chan->RawSize - Held;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFrameBoundary -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SdiTxImpl_Bb2::FindFrameBoundary: bytes that cannot start a frame are skipped four at a
// time, and bytes too few to judge are kept for the next write.
//
static void FindFrameBoundary(DtOutpChannel* Chan, const uint8_t** Data, size_t* Left)
{
    size_t Need = StartBytes(Chan);

    while (Chan->RawHave > 0)
    {
        size_t Extra = Need - Chan->RawHave;

        if (*Left < Extra)
        {
            memcpy(Chan->RawBuf + Chan->RawHave, *Data, *Left);
            Chan->RawHave += *Left;
            *Data += *Left;
            *Left = 0;
            return;
        }
        memcpy(Chan->RawBuf + Chan->RawHave, *Data, Extra);
        if (IsFrameStart(Chan, Chan->RawBuf))
        {
            *Data += Extra;
            *Left -= Extra;
            StartFrame(Chan, Need);
            return;
        }
        if (Chan->RawHave <= 4)
            Chan->RawHave = 0;
        else
        {
            Chan->RawHave -= 4;
            memmove(Chan->RawBuf, Chan->RawBuf + 4, Chan->RawHave);
        }
    }

    while (*Left >= Need)
    {
        if (IsFrameStart(Chan, *Data))
        {
            StartFrame(Chan, 0);
            return;
        }
        *Data += 4;
        *Left -= 4;
    }
    memcpy(Chan->RawBuf, *Data, *Left);
    Chan->RawHave = *Left;
    *Data += *Left;
    *Left = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitForRoom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits, without the lock, until the buffer has room for a whole frame, or until the
// monotonic clock reaches Deadline, DT_NO_DEADLINE for no limit. While sending the thread
// wakes the wait after every format event; while holding nothing goes out, and the wait
// looks again every quarter frame. Returns DTAPI_E_CANCELLED for a detach, DTAPI_E_IDLE
// when the channel went idle meanwhile, and DTAPI_E_TIMEOUT.
//
static DtapiResult WaitForRoom(DtOutpChannel* Chan, uint64_t Deadline)
{
    for (;;)
    {
        if (Chan->Detachers > 0)
            return DTAPI_E_CANCELLED;
        if (Chan->TxControl == DTAPI_TXCTRL_IDLE)
            return DTAPI_E_IDLE;
        size_t Load;
        DtapiResult Result = ReadLoad(Chan, &Load);
        if (Result != DTAPI_OK)
            return Result;
        if (Chan->MaxLoad - Load >= Chan->CodedSize)
            return DTAPI_OK;

        uint64_t Now = OsTime_MonotonicMs();
        if (Now >= Deadline)
            return DTAPI_E_TIMEOUT;
        int Wait = Chan->QuarterMs;
        if (Deadline - Now < (uint64_t)Wait)
            Wait = (int)(Deadline - Now);

        OsMutex_Unlock(Chan->Lock);
        if (Chan->Thread != NULL)
            OsEvent_Wait(Chan->Room, Wait);
        else
            OsTime_SleepMs(Wait);
        OsMutex_Lock(Chan->Lock);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Codes the next line into its place in the buffer once all of its bytes are there. The
// frame's header is written first, when there is room for the whole frame, which is
// waited for until Deadline. A line whose last byte is shared with the next line leaves
// that byte for the next.
//
static DtapiResult TakeLine(DtOutpChannel* Chan, const uint8_t** Data, size_t* Left,
                            uint64_t Deadline)
{
    const DtSdiFrameLayout* Layout = &Chan->Layout;
    size_t Bits = DtSdiFrame_RawLineBits(Layout, Chan->SymbolBits);
    size_t Need = ((size_t)Chan->Phase + Bits + 7) / 8;
    size_t Used = ((size_t)Chan->Phase + Bits) / 8;

    if (!Chan->Reserved)
    {
        DtapiResult Result = WaitForRoom(Chan, Deadline);

        if (Result != DTAPI_OK || Chan->Stage != DT_STAGE_LINES || Chan->Reserved)
            return Result;
        PutHeader(Chan, Chan->WriteOffset, Chan->NextFrameId);
        Chan->Reserved = true;
    }

    const uint8_t* Src;
    if (Chan->RawHave == 0 && *Left >= Need)
    {
        Src = *Data;
        *Data += Used;
        *Left -= Used;
        Chan->FrameBytesLeft -= Used;
    }
    else
    {
        size_t Copy = Need - Chan->RawHave;

        if (Copy > *Left)
            Copy = *Left;
        memcpy(Chan->RawBuf + Chan->RawHave, *Data, Copy);
        Chan->RawHave += Copy;
        *Data += Copy;
        *Left -= Copy;
        Chan->FrameBytesLeft -= Copy;
        if (Chan->RawHave < Need)
            return DTAPI_OK;
        Src = Chan->RawBuf;
    }

    size_t Offset = Wrap(Chan, Chan->WriteOffset + (size_t)Layout->TxHeaderBytes +
                                   (size_t)Chan->LinesDone * (size_t)Layout->Stride);
    uint8_t* Dst = Offset + (size_t)Layout->Stride <= Chan->Buf.Size
                       ? Chan->Buf.Data + Offset
                       : Chan->LineBuf;
    DtSdiFrame_CodeLine(Layout, Chan->SymbolBits, Src, Chan->Phase, Dst);
    if (Dst == Chan->LineBuf)
        PutAt(Chan, Offset, Chan->LineBuf, (size_t)Layout->Stride);

    if (Src == Chan->RawBuf)
    {
        Chan->RawHave = Need - Used;
        if (Chan->RawHave > 0)
            Chan->RawBuf[0] = Chan->RawBuf[Used];
    }
    Chan->Phase = (int)(((size_t)Chan->Phase + Bits) % 8);
    Chan->LinesDone++;

    // What is left of the last line's byte is padding.
    if (Chan->LinesDone == Layout->NumLines)
    {
        Chan->RawHave = 0;
        Chan->Stage = DT_STAGE_PADDING;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiTxImpl_Bb2::WriteSdi, into the buffer: every byte is taken, a frame at a time. The
// lock is released after every line, so that the thread is not kept waiting, and the
// state is looked at again after it was.
//
static DtapiResult WriteSdi(DtOutpChannel* Chan, const uint8_t* Data, size_t Left)
{
    DtapiResult Result = DTAPI_OK;

    while (Result == DTAPI_OK)
    {
        if (Chan->Detachers > 0)
            return DTAPI_E_CANCELLED;
        if (Chan->TxControl == DTAPI_TXCTRL_IDLE)
            return DTAPI_E_IDLE;

        if (Chan->Stage == DT_STAGE_PADDING)
        {
            size_t Skip = Left < Chan->FrameBytesLeft ? Left : Chan->FrameBytesLeft;

            Data += Skip;
            Left -= Skip;
            Chan->FrameBytesLeft -= Skip;
            if (Chan->FrameBytesLeft == 0)
            {
                Result = CommitFrame(Chan);
                if (Result == DTAPI_OK)
                    Chan->FifoUfl = false;
                Chan->Stage = DT_STAGE_SEARCH;
                Chan->Reserved = false;
                Chan->RawHave = 0;
            }
            continue;
        }
        if (Left == 0)
            break;

        if (Chan->Stage == DT_STAGE_SEARCH)
            FindFrameBoundary(Chan, &Data, &Left);
        else
        {
            Result = TakeLine(Chan, &Data, &Left, DT_NO_DEADLINE);
            OsMutex_Unlock(Chan->Lock);
            OsMutex_Lock(Chan->Lock);
        }
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// WriteFrame's checks of a frame against the channel's standard and transmit mode:
// DTAPI_E_INVALID_SIZE when FrameSize is not the size of a raw frame, and
// DTAPI_E_INVALID_FRAME when the frame does not start as line 1 does. SD has no line
// numbers, so there any line in the vertical blanking of field 1 passes.
//
static DtapiResult CheckFrame(DtOutpChannel* Chan, const uint8_t* Frame, int FrameSize)
{
    if ((size_t)FrameSize != Chan->RawSize)
        return DTAPI_E_INVALID_SIZE;

    Chan->SdSync = DT_SD_IN_SYNC;
    return IsFrameStart(Chan, Frame) ? DTAPI_OK : DTAPI_E_INVALID_FRAME;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteWhole -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Codes a frame into the buffer line by line, as WriteSdi does, and commits it, waiting
// for room until Deadline. The lock is released after every line. When the thread
// writes a black frame in the place of the lines written, or the channel was set idle
// and holding again meanwhile, the frame is checked again and written from its start.
// On a failure nothing of the frame is committed. Write then looks for a frame again.
//
static DtapiResult WriteWhole(DtOutpChannel* Chan, const uint8_t* Frame, int FrameSize,
                              uint64_t Deadline)
{
    const uint8_t* Data = Frame;
    size_t Left = 0;
    DtapiResult Result = DTAPI_OK;

    while (Result == DTAPI_OK && Chan->Stage != DT_STAGE_PADDING)
    {
        if (Chan->Detachers > 0)
            Result = DTAPI_E_CANCELLED;
        else if (Chan->TxControl == DTAPI_TXCTRL_IDLE)
            Result = DTAPI_E_IDLE;
        else if (Chan->Stage == DT_STAGE_SEARCH)
        {
            Result = CheckFrame(Chan, Frame, FrameSize);
            Data = Frame;
            Left = (size_t)FrameSize;
            if (Result == DTAPI_OK)
                StartFrame(Chan, 0);
        }
        else
        {
            Result = TakeLine(Chan, &Data, &Left, Deadline);
            OsMutex_Unlock(Chan->Lock);
            OsMutex_Lock(Chan->Lock);
        }
    }

    if (Result == DTAPI_OK)
    {
        Result = CommitFrame(Chan);
        if (Result == DTAPI_OK)
            Chan->FifoUfl = false;
    }
    ResetFrame(Chan);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LockAttached -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the lock of an attached channel, as DtOutpChannel's DetachLock admits a call.
// Returns DTAPI_E_NOT_ATTACHED, without the lock, otherwise.
//
static DtapiResult LockAttached(DtOutpChannel* Chan)
{
    OsMutex_Lock(Chan->Lock);
    if (!Chan->Attached)
    {
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Lets go of the buffer, the exclusive access and the device, ignoring failures, as
// MxChannelMemlessTx::Detach does.
//
static void ReleaseAll(DtOutpChannel* Chan)
{
    FreeBuffer(Chan);
    DtFunc_ExclAccess(Chan->Device.Drv, &Chan->AfTx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    DtFunc_ExclAccess(Chan->Device.Drv, &Chan->AfDma, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    DtFunc_Release(&Chan->AfTx);
    DtFunc_Release(&Chan->AfDma);
    DtDevice_Release(&Chan->Device);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PadToWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Commits zero bytes up to the next whole data word. The card reads the buffer in whole
// words, so without them the last bytes of the last frame would wait for more data.
//
static void PadToWord(DtOutpChannel* Chan)
{
    static const uint8_t Zeros[64] = {0};
    size_t Pad = (Chan->WordBytes - Chan->Committed % Chan->WordBytes) % Chan->WordBytes;
    size_t Offset = Wrap(Chan, Chan->WriteOffset + Pad);
    size_t Load;

    if (Pad == 0 || Pad > sizeof(Zeros) || ReadLoad(Chan, &Load) != DTAPI_OK ||
        Chan->MaxLoad - Load < Pad)
    {
        return;
    }
    PutAt(Chan, Chan->WriteOffset, Zeros, Pad);
    if (DtPcieCmd_CdmacSetTxWriteOffset(Chan->Device.Drv, Chan->Cdmac, Chan->PortIndex,
                                        (uint32_t)Offset) == DTAPI_OK)
    {
        Chan->WriteOffset = Offset;
        Chan->Committed += Pad;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtOutpChannel::Detach: asks writes on other threads to return and waits for them up to
// Tries pauses of 10 ms, or without a limit for -1. Then, with DTAPI_WAIT_UNTIL_SENT and
// while sending, waits until the card has taken what was written; with
// DTAPI_INSTANT_DETACH forgets it. Stops, and releases everything.
//
// A detach that gives up with DTAPI_E_TIMEOUT withdraws its request, so the channel stays
// attached and usable; one that finds the channel detached by another while it waited
// returns DTAPI_E_NOT_ATTACHED.
//
static DtapiResult Detach(DtOutpChannel* Chan, int DetachMode, int Tries)
{
    if (LockAttached(Chan) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;
    if ((DetachMode & DT_INSTANT_DETACH) != 0 && (DetachMode & DT_WAIT_UNTIL_SENT) != 0)
    {
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_INVALID_FLAGS;
    }

    Chan->Detachers++;
    for (int Try = 0; Chan->Attached && Chan->Writers > 0; Try++)
    {
        if (Try == Tries)
        {
            Chan->Detachers--;
            OsMutex_Unlock(Chan->Lock);
            return DTAPI_E_TIMEOUT;
        }
        OsEvent_Set(Chan->Room);
        OsMutex_Unlock(Chan->Lock);
        OsTime_SleepMs(DT_DETACH_PAUSE_MS);
        OsMutex_Lock(Chan->Lock);
    }
    if (!Chan->Attached)
    {
        Chan->Detachers--;
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    // With the thread stopped, the detach is the one that waits for format events: until
    // the last frame written has gone out and a wait finds nothing more, or no event came
    // for a second.
    if ((DetachMode & DT_WAIT_UNTIL_SENT) != 0 && Chan->TxControl == DTAPI_TXCTRL_SEND)
    {
        uint64_t Since = OsTime_MonotonicMs();
        int WaitMs = Chan->QuarterMs < 500 ? 2 * Chan->QuarterMs : 1000;

        StopKeeper(Chan);
        PadToWord(Chan);
        while (Chan->NextFrameId != 0)
        {
            OsDrv* Drv = Chan->Device.Drv;
            int Txf = Chan->Txf;
            int Index = Chan->PortIndex;

            OsMutex_Unlock(Chan->Lock);
            DtSdiTxFEvent Event;
            DtapiResult Result =
                DtPcieCmd_SdiTxFWaitForFmtEvent(Drv, Txf, Index, WaitMs, &Event);
            OsMutex_Lock(Chan->Lock);

            if (Result == DTAPI_OK)
            {
                Chan->Started = true;
                Chan->SendingId = Event.FrameId;
                Since = OsTime_MonotonicMs();
            }
            else if (Result != DTAPI_E_TIMEOUT)
                break;
            else if ((Chan->Started && UnsentFrames(Chan) <= 1) ||
                     OsTime_MonotonicMs() - Since >= DT_SENT_STALL_MS)
            {
                break;
            }
        }
    }
    if ((DetachMode & DT_INSTANT_DETACH) != 0)
        ResetFifo(Chan);
    SetTxControl(Chan, DTAPI_TXCTRL_IDLE);

    ReleaseAll(Chan);
    Chan->Attached = false;
    Chan->Detachers--;
    OsMutex_Unlock(Chan->Lock);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtOutpChannel* DtOutpChannel_Alloc(void)
{
    DtOutpChannel* Chan = (DtOutpChannel*)DtAlloc_Malloc(sizeof(DtOutpChannel));

    if (Chan == NULL)
        return NULL;
    memset(Chan, 0, sizeof(*Chan));
    Chan->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    DtVec_Init(&Chan->AfTx.Parts, sizeof(DtFuncPart));
    DtVec_Init(&Chan->AfDma.Parts, sizeof(DtFuncPart));

    Chan->Lock = OsMutex_Create();
    Chan->Room = OsEvent_Create();
    if (Chan->Lock == NULL || Chan->Room == NULL)
    {
        OsMutex_Destroy(Chan->Lock);
        OsEvent_Destroy(Chan->Room);
        DtAlloc_Free(Chan);
        return NULL;
    }
    return Chan;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// As DTAPI's destructor: an instant detach whose result is ignored. Unlike a detach, it
// waits for writes on other threads to return for as long as they take, so that none of
// them uses the channel after it is gone.
//
void DtOutpChannel_Free(DtOutpChannel* OutpChannel)
{
    if (OutpChannel == NULL)
        return;

    Detach(OutpChannel, DT_INSTANT_DETACH, -1);
    OsEvent_Destroy(OutpChannel->Room);
    OsMutex_Destroy(OutpChannel->Lock);
    DtAlloc_Free(OutpChannel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtOutpChannel_Freep(DtOutpChannel** OutpChannel)
{
    if (OutpChannel == NULL)
        return;

    DtOutpChannel_Free(*OutpChannel);
    *OutpChannel = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindParts -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The eight parts of AF_ASISDITX and AF_DMA the channel drives, and whether the driver is
// new enough for each.
//
static DtapiResult FindParts(DtOutpChannel* Chan)
{
    typedef struct
    {
        DtFuncInstance* Instance;
        bool IsDf;
        int Type;
        const char* Role;
        int* Uuid;
    } Wanted;
    const Wanted Parts[] = {
        {&Chan->AfDma, false, DT_BLOCK_TYPE_CDMAC, "", &Chan->Cdmac},
        {&Chan->AfDma, false, DT_BLOCK_TYPE_BURSTFIFO, "", &Chan->Burst},
        {&Chan->AfTx, false, DT_BLOCK_TYPE_SDITXF, "", &Chan->Txf},
        {&Chan->AfTx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_IN", &Chan->SwitchIn},
        {&Chan->AfTx, false, DT_BLOCK_TYPE_SDIDMX12G, "", &Chan->Dmx},
        {&Chan->AfTx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_OUT", &Chan->SwitchOut},
        {&Chan->AfTx, false, DT_BLOCK_TYPE_SDITXP, "", &Chan->Txp},
        {&Chan->AfTx, true, DT_FUNC_TYPE_SDITXPHY, "", &Chan->Phy},
    };

    DtapiResult Result =
        DtFunc_Find(Chan->Device.Drv, Chan->PortIndex, "AF_ASISDITX", "", &Chan->AfTx);
    if (Result == DTAPI_OK)
        Result =
            DtFunc_Find(Chan->Device.Drv, Chan->PortIndex, "AF_DMA", "", &Chan->AfDma);
    for (size_t i = 0; i < sizeof(Parts) / sizeof(Parts[0]) && Result == DTAPI_OK; i++)
    {
        const DtFuncPart* Part =
            DtFunc_Get(Parts[i].Instance, Parts[i].IsDf, Parts[i].Type, Parts[i].Role);

        if (Part == NULL)
            Result = DTAPI_E_NOT_FOUND;
        else
        {
            *Parts[i].Uuid = Part->Uuid;
            Result = DtFunc_CheckDriverVersion(&Chan->Device.DriverVersion, Parts[i].IsDf,
                                               Parts[i].Type);
        }
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// AttachToPort's steps once the channel has its own handle to the device. The caller
// releases everything when they fail.
//
static DtapiResult AttachPort(DtOutpChannel* Chan, int Port, uint32_t Caps)
{
    Chan->Port = Port;
    Chan->PortIndex = Port - 1;
    Chan->Caps = Caps;

    // The DMA-rate test mode is switched off first.
    DtIoConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Port = Port;
    Config.ParXtra[0] = Config.ParXtra[1] = -1;
    if ((Caps & DT_CAP_DMATESTMODE) != 0)
    {
        Config.Group = DTAPI_IOCONFIG_DMATESTMODE;
        Config.Value = DTAPI_IOCONFIG_FALSE;
        Config.SubValue = -1;
        DtapiResult Result = DtPcieCmd_SetIoConfig(Chan->Device.Drv, &Config);
        if (Result != DTAPI_OK)
            return Result;
    }

    Config.Group = DTAPI_IOCONFIG_IODIR;
    DtapiResult Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &Config);
    if (Result == DTAPI_OK && Config.Value != DTAPI_IOCONFIG_OUTPUT)
        Result = DTAPI_E_NO_DT_OUTPUT;
    if (Result != DTAPI_OK)
        return Result;

    Config.Group = DTAPI_IOCONFIG_IOSTD;
    Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &Config);
    if (Result == DTAPI_OK && Config.Value == DTAPI_IOCONFIG_ASI)
        Result = DTAPI_E_NOT_SUPPORTED;
    if (Result != DTAPI_OK)
        return Result;
    Chan->IoStdValue = Config.Value;
    Chan->IoStdSubValue = Config.SubValue;

    // The default transmit mode and cleared flags; the I/O standard is applied again.
    Chan->TxMode = DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B;
    Chan->SymbolBits = 10;
    Chan->TxControl = DTAPI_TXCTRL_IDLE;
    Chan->FifoUfl = Chan->FifoUflLatched = false;
    Chan->DmaUfl = Chan->DmaUflLatched = false;
    Result = DtPcieCmd_SetIoConfig(Chan->Device.Drv, &Config);
    if (Result != DTAPI_OK)
        return Result;

    // Exclusive access to the transmitter and the DMA, all blocks idle, the encoder's
    // corrections on, and the channel set up for the standard.
    Result = FindParts(Chan);
    if (Result == DTAPI_OK)
        Result = DtFunc_ExclAccess(Chan->Device.Drv, &Chan->AfTx,
                                   DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
        Result = DtFunc_ExclAccess(Chan->Device.Drv, &Chan->AfDma,
                                   DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
        Result = BlocksToIdle(Chan);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPSetGenerationMode(Chan->Device.Drv, Chan->Txp,
                                                   Chan->PortIndex, true, true, true);
    if (Result == DTAPI_OK)
        Result = ConfigureChannel(Chan);
    if (Result != DTAPI_OK)
        return Result;

    // A fail-safe port in fail-safe mode is reported, as a success.
    if ((Caps & DT_CAP_FAILSAFE) != 0)
    {
        DtIoConfig FailSafe = Config;

        FailSafe.Group = DTAPI_IOCONFIG_FAILSAFE;
        Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &FailSafe);
        if (Result != DTAPI_OK)
            return Result;
        if (FailSafe.Value == DTAPI_IOCONFIG_TRUE)
            return DTAPI_OK_FAILSAFE;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_AttachToPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtOutpChannel::AttachToPort, AsiSdiOutpChannel_Bb2::InitOutpChannel,
// SdiTxImpl_Bb2::SetIoConfig and MxChannelMemlessTx::Attach, in their order. A failure
// after the channel has its own handle lets go of all of it; releasing the exclusive
// access of a part the handle does not hold changes nothing.
//
static DtapiResult Attach(DtOutpChannel* Chan, DtDevice* Device, int Port)
{
    if (Device == NULL || Device->Drv == NULL)
        return DTAPI_E_DEVICE;
    if (Device->Info.FirmwareStatus == DT_FWSTATUS_OBSOLETE)
        return DTAPI_E_OBSOLETE_FW;
    if (Device->Info.FirmwareStatus == DT_FWSTATUS_TAINTED)
        return DTAPI_E_TAINTED_FW;
    if (Port < 1 || Port > Device->NumPublicPorts)
        return DTAPI_E_NO_SUCH_PORT;

    uint32_t Caps = Device->PortCaps[Port - 1];
    if ((Caps & DT_CAP_OUTPUT) == 0 && (Caps & DT_CAP_IP) == 0)
        return DTAPI_E_NO_DT_OUTPUT;
    if ((Caps & DT_CAP_MATRIX) != 0 || (Caps & DT_CAP_ASI) == 0)
        return DTAPI_E_NOT_SUPPORTED;

    DtapiResult Result =
        DtDevice_AttachIndex(&Chan->Device, Device->Index, true, Device->Info.Serial);
    if (Result != DTAPI_OK)
        return Result;

    Result = AttachPort(Chan, Port, Caps);
    if (Result >= DTAPI_E)
        ReleaseAll(Chan);
    return Result;
}

DtapiResult DtOutpChannel_AttachToPort(DtOutpChannel* OutpChannel, DtDevice* Device,
                                       int Port)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    OsMutex_Lock(OutpChannel->Lock);
    DtapiResult Result;
    if (OutpChannel->Attached)
        Result = DTAPI_E_ATTACHED;
    else
    {
        Result = Attach(OutpChannel, Device, Port);
        OutpChannel->Attached = Result < DTAPI_E;
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_Detach(DtOutpChannel* OutpChannel, int DetachMode)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    return Detach(OutpChannel, DetachMode, DT_DETACH_TRIES);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_ClearFifo(DtOutpChannel* OutpChannel)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = ResetFifo(OutpChannel);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_GetFifoLoad(DtOutpChannel* OutpChannel, int* FifoLoad)
{
    DtapiResult Result = DTAPI_OK;

    if (OutpChannel == NULL || FifoLoad == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // The complete frames that have not started going out, and what a write has taken of
    // the next, in raw bytes.
    *FifoLoad = 0;
    if (OutpChannel->TxControl != DTAPI_TXCTRL_IDLE)
    {
        int Frames = UnsentFrames(OutpChannel) - (OutpChannel->Started ? 1 : 0);
        size_t Size =
            OutpChannel->MaxLoad / OutpChannel->CodedSize * OutpChannel->RawSize;
        size_t Bytes = (size_t)(Frames > 0 ? Frames : 0) * OutpChannel->RawSize;

        if (OutpChannel->Stage != DT_STAGE_SEARCH)
            Bytes += OutpChannel->RawSize - OutpChannel->FrameBytesLeft;
        *FifoLoad = (int)(Bytes < Size ? Bytes : Size);
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The load GetFifoLoad reports for a full buffer. A channel without a buffer, on a 4K
// port, gives DTAPI's typical size, and its maximum size for GetMaxFifoSize.
//
static DtapiResult GetFifoSize(DtOutpChannel* Chan, int* FifoSize, int NoBuffer)
{
    if (Chan == NULL || FifoSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(Chan) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    if (!Chan->Registered)
        *FifoSize = NoBuffer;
    else
        *FifoSize = (int)(Chan->MaxLoad / Chan->CodedSize *
                          DtSdiFrame_RawSize(&Chan->Layout, Chan->SymbolBits));
    OsMutex_Unlock(Chan->Lock);
    return DTAPI_OK;
}

DtapiResult DtOutpChannel_GetFifoSize(DtOutpChannel* OutpChannel, int* FifoSize)
{
    return GetFifoSize(OutpChannel, FifoSize, DT_FIFO_SIZE_TYP);
}

DtapiResult DtOutpChannel_GetMaxFifoSize(DtOutpChannel* OutpChannel, int* MaxFifoSize)
{
    return GetFifoSize(OutpChannel, MaxFifoSize, DT_FIFO_SIZE_MAX);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_GetFlags(DtOutpChannel* OutpChannel, int* Status, int* Latched)
{
    if (OutpChannel == NULL || Status == NULL || Latched == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    *Status = (OutpChannel->FifoUfl ? DTAPI_TX_FIFO_UFL : 0) |
              (OutpChannel->DmaUfl ? DTAPI_TX_DMA_UFL : 0);
    *Latched = (OutpChannel->FifoUflLatched ? DTAPI_TX_FIFO_UFL : 0) |
               (OutpChannel->DmaUflLatched ? DTAPI_TX_DMA_UFL : 0);
    OsMutex_Unlock(OutpChannel->Lock);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtOutpChannel::SetIoConfig's checks, then AsiSdiOutpChannel_Bb2::SetIoConfig's. DTAPI
// also refuses a configuration the port lacks a capability for; here the driver does.
// The transmit mode is kept.
//
DtapiResult DtOutpChannel_SetIoConfig(DtOutpChannel* OutpChannel, int Group, int Value,
                                      int SubValue)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = DtIoConfig_IsValid(Group, Value, SubValue);
    if (Result != DTAPI_OK)
        return Result;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // An output that names another port needs that port in ParXtra, which CDTAPI.h has
    // no way to give.
    if (Group == DTAPI_IOCONFIG_IODIR &&
        (Value == DTAPI_IOCONFIG_INPUT ||
         (Value == DTAPI_IOCONFIG_OUTPUT &&
          (SubValue == DTAPI_IOCONFIG_DBLBUF || SubValue == DTAPI_IOCONFIG_LOOPS2L3 ||
           SubValue == DTAPI_IOCONFIG_LOOPS2TS || SubValue == DTAPI_IOCONFIG_LOOPTHR))))
    {
        Result = DTAPI_E_INVALID_ARG;
    }
    else if (OutpChannel->TxControl != DTAPI_TXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else if (Group == DTAPI_IOCONFIG_IOSTD && Value == DTAPI_IOCONFIG_ASI)
        Result = DTAPI_E_NOT_SUPPORTED;
    else
    {
        DtIoConfig Config;
        Config.Port = OutpChannel->Port;
        Config.Group = Group;
        Config.Value = Value;
        Config.SubValue = SubValue;
        Config.ParXtra[0] = Config.ParXtra[1] = -1;
        Result = DtPcieCmd_SetIoConfig(OutpChannel->Device.Drv, &Config);

        if (Result == DTAPI_OK && Group == DTAPI_IOCONFIG_IOSTD)
        {
            OutpChannel->IoStdValue = Value;
            OutpChannel->IoStdSubValue = SubValue;
            Result = ConfigureChannel(OutpChannel);
        }
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_SetTxControl(DtOutpChannel* OutpChannel, int TxControl)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = SetTxControl(OutpChannel, TxControl);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtOutpChannel::SetTxMode's checks, then SdiTxImpl_Bb2::SetTxMode's. The mode is kept
// for the channel's life, also across a change of I/O standard.
//
DtapiResult DtOutpChannel_SetTxMode(DtOutpChannel* OutpChannel, int TxMode, int StuffMode)
{
    DtapiResult Result = DTAPI_OK;

    (void)StuffMode;
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if ((TxMode & DT_TXMODE_TS) != 0 && (TxMode & DTAPI_TXMODE_SDI) != 0)
        return DTAPI_E_INVALID_MODE;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    if ((TxMode & DT_TXMODE_TS_MASK) == DT_TXMODE_192)
        Result = DTAPI_E_INVALID_MODE;
    if (Result == DTAPI_OK && (TxMode & DTAPI_TXMODE_SDI) != 0)
    {
        if ((TxMode & DTAPI_TXMODE_SDI_MASK) != DTAPI_TXMODE_SDI_FULL &&
            (TxMode & DTAPI_TXMODE_SDI_MASK) != DTAPI_TXMODE_SDI_ACTVID)
        {
            TxMode |= DTAPI_TXMODE_SDI_FULL;
        }
        if (((TxMode & DTAPI_TXMODE_SDI_HUFFMAN) != 0 &&
             (OutpChannel->Caps & DT_CAP_HUFFMAN) == 0) ||
            ((TxMode & DTAPI_TXMODE_SDI_10B_NBO) != 0 &&
             (OutpChannel->Caps & DT_CAP_SDI10BNBO) == 0))
        {
            Result = DTAPI_E_INVALID_MODE;
        }
    }

    if (Result == DTAPI_OK && OutpChannel->TxControl != DTAPI_TXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else if (Result == DTAPI_OK &&
             (TxMode & DTAPI_TXMODE_SDI_MASK) != DTAPI_TXMODE_SDI_FULL)
        Result = DTAPI_E_INVALID_MODE;
    else if (Result == DTAPI_OK)
    {
        OutpChannel->TxMode = TxMode;
        OutpChannel->SymbolBits = (TxMode & DTAPI_TXMODE_SDI_10B) != 0   ? 10
                                  : (TxMode & DTAPI_TXMODE_SDI_16B) != 0 ? 16
                                                                         : 8;
        if (OutpChannel->Registered)
            OutpChannel->RawSize =
                DtSdiFrame_RawSize(&OutpChannel->Layout, OutpChannel->SymbolBits);
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtOutpChannel::Write's checks, the four-byte checks always applied: the buffer's
// address and the size must be multiples of 4, and a failing check overrides an idle
// channel, as it does in DTAPI. Then one write at a time converts the bytes into the
// buffer, waiting for room without the lock.
//
DtapiResult DtOutpChannel_Write(DtOutpChannel* OutpChannel, const void* Buffer,
                                int NumBytesToWrite)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (NumBytesToWrite < 0)
        return DTAPI_E_INVALID_SIZE;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;
    if (OutpChannel->Detachers > 0)
    {
        OsMutex_Unlock(OutpChannel->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    DtapiResult Result =
        OutpChannel->TxControl == DTAPI_TXCTRL_IDLE ? DTAPI_E_IDLE : DTAPI_OK;
    if ((uintptr_t)Buffer % 4 != 0 || NumBytesToWrite % 4 != 0 ||
        (Buffer == NULL && NumBytesToWrite > 0))
    {
        Result = DTAPI_E_INVALID_BUF;
    }

    // A write on another thread goes first.
    while (Result == DTAPI_OK && OutpChannel->Writers > 0)
    {
        OsMutex_Unlock(OutpChannel->Lock);
        OsTime_SleepMs(1);
        OsMutex_Lock(OutpChannel->Lock);
        if (OutpChannel->Detachers > 0)
            Result = DTAPI_E_CANCELLED;
    }

    if (Result == DTAPI_OK)
    {
        OutpChannel->Writers++;
        Result = WriteSdi(OutpChannel, (const uint8_t*)Buffer, (size_t)NumBytesToWrite);
        OutpChannel->Writers--;
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_WriteFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The argument checks, then the channel's: a write on another thread, or bytes a Write
// left that are not yet in a frame, refuse the frame, so that it goes into the buffer
// whole, directly after the frames before it.
//
DtapiResult DtOutpChannel_WriteFrame(DtOutpChannel* OutpChannel, const void* Frame,
                                     int FrameSize, int TimeOut)
{
    uint64_t Start = OsTime_MonotonicMs();

    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (TimeOut != -1 && TimeOut <= 0)
        return DTAPI_E_INVALID_TIMEOUT;
    if (FrameSize <= 0 || FrameSize % 4 != 0)
        return DTAPI_E_INVALID_SIZE;
    if (Frame == NULL || (uintptr_t)Frame % 4 != 0)
        return DTAPI_E_INVALID_BUF;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = DTAPI_OK;
    if (OutpChannel->Detachers > 0)
        Result = DTAPI_E_NOT_ATTACHED;
    else if (OutpChannel->TxControl == DTAPI_TXCTRL_IDLE)
        Result = DTAPI_E_IDLE;
    else if (OutpChannel->Writers > 0)
        Result = DTAPI_E_IN_USE;
    else if (OutpChannel->Stage != DT_STAGE_SEARCH || OutpChannel->RawHave > 0)
        Result = DTAPI_E_INCOMP_FRAME;
    else
    {
        uint64_t Deadline = TimeOut == -1 ? DT_NO_DEADLINE : Start + (uint64_t)TimeOut;

        OutpChannel->Writers++;
        Result = WriteWhole(OutpChannel, (const uint8_t*)Frame, FrameSize, Deadline);
        OutpChannel->Writers--;
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The SDI side of an output channel - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "Core/DtWorkerPool.h"  // The threads a batch of lines is coded over.
#include "Device/DtFunc.h"      // Finding the transmit blocks.
#include "DtPcieAbi.h"          // Operational modes and types.
#include "DtSdiTx.h"            // Interface being implemented.
#include "OAL/OsDmaBuffer.h"    // The DMA buffer.
#include "OAL/OsThread.h"       // The thread, its event, sleeping.
#include "Video/DtFrameProps.h" // The frame rate.
#include "Video/DtSdiFrame.h"   // The buffer's format and the raw frame.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The typical and maximum FIFO size, reported for a port without a buffer. The buffer is
// sized to hold the typical one.
#define DT_SDITX_FIFO_SIZE_TYP (48 * 1024 * 1024)
#define DT_SDITX_FIFO_SIZE_MAX (64 * 1024 * 1024)

// The bounds on the DMA buffer, the frames it is sized for, and the fewest it must hold.
#define DT_SDITX_BUF_MIN_SIZE (8 * 1024 * 1024)
#define DT_SDITX_BUF_MAX_SIZE (256 * 1024 * 1024)
#define DT_SDITX_BUF_ROOM_FRAMES 5
#define DT_SDITX_BUF_MIN_FRAMES 2

// A detach that waits until everything is sent gives up after a second without a format
// event.
#define DT_SDITX_SENT_STALL_MS 1000

// The burst FIFO's load is read at most five times for 75 % full.
#define DT_SDITX_BURST_POLLS 5

// The format event of the first frame of a run from which the thread may write a black
// frame after it: the application has until then, half a frame, to write the second
// frame. The last quarter of a frame goes out only when data follows it.
#define DT_SDITX_FIRST_BLACK_EVENT_SEQ 2

// The PHY's underflow flag is read every so many format events.
#define DT_SDITX_PHY_POLL_EVENTS 50

// The largest header with its padding: 20 bytes padded to 512 bits.
#define DT_SDITX_MAX_HEADER_BYTES 64

// The search for the start of line 1 of an SD frame.
typedef enum DtSdiTxSdSearchState
{
    DT_SDITX_SD_IN_SYNC,
    DT_SDITX_SD_FIND_FIELD2,
    DT_SDITX_SD_FIND_FRAME_START
} DtSdiTxSdSearchState;

// Where the stream is: looking for a frame, in its lines, or in the padding after them.
typedef enum DtSdiTxWriteStage
{
    DT_SDITX_STAGE_SEARCH,
    DT_SDITX_STAGE_LINES,
    DT_SDITX_STAGE_PADDING
} DtSdiTxWriteStage;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct DtSdiTx
{
    DtTx Tx;

    // The transmit blocks, held exclusively while attached.
    DtFuncInstance TxFunction, DmaFunction;
    DtDrvObject Cdmac, BurstFifo, Txf, Txp, Phy;

    // The demultiplexer and its switches of a port with DT_CAP_QUADLINK, and the switch
    // from a quad-link master where the port has it; their UUIDs are 0 otherwise.
    bool HasQuadLink;
    DtDrvObject DemuxInSwitch, DemuxOutSwitch, Dmx;
    DtDrvObject QuadLinkMasterSwitch;

    int IoStdValue; // The port's I/O standard
    int IoStdSubValue;
    int BitsPerSymbol; // 8, 10 or 16, from the transmit mode

    // Flags.
    bool FifoUfl, FifoUflLatched;
    bool DmaUfl, DmaUflLatched;

    // The configured standard; Layout.VidStd is DTAPI_VIDSTD_UNKNOWN without a buffer.
    DtSdiFrameLayout FrameLayout;
    size_t CodedFrameSize; // A coded frame with its header
    size_t RawFrameSize;   // A raw frame in the current transmit mode
    OsDmaBuffer DmaBuffer;
    bool BufferRegistered;
    size_t MaxLoad;          // The buffer less the data word kept free
    size_t PcieWordBytes;    // A PCIe data word, which the card reads the buffer in
    int BurstFifoSize;       // Bytes
    int QuarterFrameMs;      // A quarter frame period, at least 1 ms
    uint8_t* BlackLines;     // The coded lines of a black frame, line headers included
    uint8_t* WrapLineBuffer; // A raw line's coded lines when they run across the end
    uint8_t* PartialLine;    // The raw bytes of a line not yet complete
    uint16_t* BandSymbols;   // The working symbols of a 4K line, one set a band

    // The pool the channel gave, and the pieces it asked for: 0 for as many as the
    // standard calls for. The channel holds the pool.
    DtWorkerPool* WorkerPool;
    int WorkerThreads;

    // The pieces a batch of lines is encoded in. BandSymbols holds
    // DtJobRunner_NumPieces(&JobRunner) sets of SymbolsPerBand symbols, so that a
    // band uses its own.
    DtJobRunner JobRunner;
    size_t SymbolsPerBand;

    // The buffer while holding or sending.
    size_t WriteOffset; // Where the next frame's header goes, and the driver's offset
    uint64_t CommittedBytes; // Bytes committed since the DMA controller was set running
    int NextFrameId;

    // The frame being written.
    DtSdiTxWriteStage WriteStage;
    DtSdiTxSdSearchState SdSearchState;
    bool FrameRoomReserved;  // Its header is written and room for it was found
    int LinesEncoded;        // Lines coded into the buffer
    int LineStartBit;        // The bit the next line starts at in its first byte
    size_t PartialLineBytes; // Bytes in PartialLine
    size_t FrameBytesLeft;   // Raw bytes of the frame, padding included, not yet taken

    // The thread that keeps the signal while sending.
    OsThread* KeeperThread;
    bool StopRequested;
    OsEvent* RoomEvent; // Set after every wait for a format event, and for a detach
    int NumFormatEvents;
    bool FirstEventSeen; // A format event came since the channel held
    bool BlackAllowed;   // Black frames may follow the first frame: see
                         // DT_SDITX_FIRST_BLACK_EVENT_SEQ
    int SendingFrameId;  // The frame ID of the last format event
} DtSdiTx;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrvOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static OsDrv* DrvOf(const DtSdiTx* Sdi)
{
    return Sdi->Tx.Port.Device->Drv;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WrapOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t WrapOffset(const DtSdiTx* Sdi, size_t Offset)
{
    return Offset % Sdi->DmaBuffer.Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyIntoBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies Size bytes into the buffer from Offset on, across its end.
//
static void CopyIntoBuffer(DtSdiTx* Sdi, size_t Offset, const uint8_t* Data, size_t Size)
{
    size_t First = Sdi->DmaBuffer.Size - Offset;

    if (First > Size)
        First = Size;
    memcpy(Sdi->DmaBuffer.Data + Offset, Data, First);
    memcpy(Sdi->DmaBuffer.Data, Data + First, Size - First);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MoveWithinBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies Size bytes of the buffer from offset From to offset To, both across its end.
// The two ranges do not overlap.
//
static void MoveWithinBuffer(DtSdiTx* Sdi, size_t From, size_t To, size_t Size)
{
    while (Size > 0)
    {
        size_t Chunk = Size;

        if (Chunk > Sdi->DmaBuffer.Size - From)
            Chunk = Sdi->DmaBuffer.Size - From;
        if (Chunk > Sdi->DmaBuffer.Size - To)
            Chunk = Sdi->DmaBuffer.Size - To;
        memcpy(Sdi->DmaBuffer.Data + To, Sdi->DmaBuffer.Data + From, Chunk);
        From = WrapOffset(Sdi, From + Chunk);
        To = WrapOffset(Sdi, To + Chunk);
        Size -= Chunk;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFrameHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the header of a frame with FrameId, padded with zeros, at Offset.
//
static void WriteFrameHeader(DtSdiTx* Sdi, size_t Offset, int FrameId)
{
    uint8_t Bytes[DT_SDITX_MAX_HEADER_BYTES];

    memset(Bytes, 0, sizeof(Bytes));
    DtSdiFrameTxHeader Header;
    DtSdiFrame_TxHeaderInit(&Sdi->FrameLayout, FrameId, &Header);
    DtSdiFrame_EncodeTxHeader(&Header, Bytes);
    CopyIntoBuffer(Sdi, Offset, Bytes, (size_t)Sdi->FrameLayout.TxHeaderNumBytes);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DmaBufferLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bytes of committed frames the card has not yet taken. Right after the DMA
// controller is set running a DTA-2178 reports a read offset of an earlier run for a
// while; the load is therefore never more than what was committed since.
//
static DtapiResult DmaBufferLoad(DtSdiTx* Sdi, size_t* Load)
{
    uint32_t ReadOffset = 0;
    DtapiResult Result =
        DtPcieCmd_CdmacGetTxReadOffset(DrvOf(Sdi), Sdi->Cdmac, &ReadOffset);

    *Load = 0;
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Sdi->DmaBuffer.Size)
        return DTAPI_E_DEV_DRIVER;

    *Load = WrapOffset(Sdi, Sdi->WriteOffset + Sdi->DmaBuffer.Size - ReadOffset);
    if ((uint64_t)*Load > Sdi->CommittedBytes)
        *Load = (size_t)Sdi->CommittedBytes;
    if (*Load > Sdi->MaxLoad)
        *Load = Sdi->MaxLoad;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UnsentFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The committed frames that have not gone out, the frame going out included. Frame IDs
// count the frames committed since the channel held, and a format event names the frame
// going out; before the first event none has.
//
static int UnsentFrames(const DtSdiTx* Sdi)
{
    return (Sdi->NextFrameId - (Sdi->FirstEventSeen ? Sdi->SendingFrameId : 0)) & 0xFFFF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ForgetPartialFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Forgets the frame being written, and starts looking for line 1 again.
//
static void ForgetPartialFrame(DtSdiTx* Sdi)
{
    Sdi->WriteStage = DT_SDITX_STAGE_SEARCH;
    Sdi->SdSearchState = DT_SDITX_SD_IN_SYNC;
    Sdi->FrameRoomReserved = false;
    Sdi->LinesEncoded = 0;
    Sdi->LineStartBit = 0;
    Sdi->PartialLineBytes = 0;
    Sdi->FrameBytesLeft = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CommitFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frame at the write offset is complete: the card may take it.
//
static DtapiResult CommitFrame(DtSdiTx* Sdi)
{
    size_t Offset = WrapOffset(Sdi, Sdi->WriteOffset + Sdi->CodedFrameSize);
    DtapiResult Result =
        DtPcieCmd_CdmacSetTxWriteOffset(DrvOf(Sdi), Sdi->Cdmac, (uint32_t)Offset);

    if (Result != DTAPI_OK)
        return Result;
    Sdi->WriteOffset = Offset;
    Sdi->CommittedBytes += Sdi->CodedFrameSize;
    Sdi->NextFrameId = (Sdi->NextFrameId + 1) & 0xFFFF;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InsertBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Commits a black frame at the write offset. The part of a frame a write has put there
// moves one frame further, with the next frame ID; when the buffer has no room for both,
// the black frame takes its place and the write looks for the next frame. A black frame
// is an underflow: it sets DTAPI_TX_FIFO_UFL. Without room for one frame nothing happens.
//
static DtapiResult InsertBlack(DtSdiTx* Sdi, size_t Load)
{
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    size_t Coded = Sdi->CodedFrameSize;
    size_t Free = Sdi->MaxLoad - Load;
    size_t Partial = 0;

    if (Free < Coded)
        return DTAPI_OK;
    if (Sdi->FrameRoomReserved)
    {
        Partial = (size_t)Layout->TxHeaderNumBytes +
                  (size_t)Sdi->LinesEncoded * DtSdiFrame_TxBytesPerLine(Layout);
        if (Free < 2 * Coded)
        {
            ForgetPartialFrame(Sdi);
            Partial = 0;
        }
    }

    if (Partial > 0)
    {
        MoveWithinBuffer(Sdi, Sdi->WriteOffset, WrapOffset(Sdi, Sdi->WriteOffset + Coded),
                         Partial);
        WriteFrameHeader(Sdi, WrapOffset(Sdi, Sdi->WriteOffset + Coded),
                         (Sdi->NextFrameId + 1) & 0xFFFF);
    }
    WriteFrameHeader(Sdi, Sdi->WriteOffset, Sdi->NextFrameId);
    CopyIntoBuffer(
        Sdi, WrapOffset(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderNumBytes),
        Sdi->BlackLines, (size_t)Layout->NumCodedLines * (size_t)Layout->TxStride);

    DtapiResult Result = CommitFrame(Sdi);
    if (Result == DTAPI_OK)
        Sdi->FifoUfl = Sdi->FifoUflLatched = true;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SignalKeeperThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// While sending: waits for the formatter's format events, the only waiter for them,
// takes their underflow flag and, now and then, the PHY's, and writes a black frame when
// the frame going out is the last one written. After the first frame of a run it waits
// with that until the frame's event DT_SDITX_FIRST_BLACK_EVENT_SEQ or a wait that times
// out, so that an application that wrote only one frame before sending has time to write
// the next. Wakes a write waiting for room after each wait, whether an event came or not.
//
static void SignalKeeperThread(void* Context)
{
    DtSdiTx* Sdi = (DtSdiTx*)Context;

    OsThread_SetName("DtSdiTxKeeper");

    OsThread_RaisePriority();
    OsMutex_Lock(Sdi->Tx.Port.Lock);
    OsDrv* Drv = DrvOf(Sdi);
    DtDrvObject Txf = Sdi->Txf;
    int WaitMs = Sdi->QuarterFrameMs < 1000 ? Sdi->QuarterFrameMs : 1000;
    OsMutex_Unlock(Sdi->Tx.Port.Lock);

    for (;;)
    {
        DtSdiTxFEvent Event;
        bool Failed = false;

        DtapiResult Result = DtPcieCmd_SdiTxFWaitForFmtEvent(Drv, Txf, WaitMs, &Event);

        OsMutex_Lock(Sdi->Tx.Port.Lock);
        if (Sdi->StopRequested)
        {
            OsMutex_Unlock(Sdi->Tx.Port.Lock);
            return;
        }
        if (Result == DTAPI_OK)
        {
            Sdi->FirstEventSeen = true;
            Sdi->SendingFrameId = Event.FrameId;
            if (Event.FrameId != 0 || Event.SeqNumber >= DT_SDITX_FIRST_BLACK_EVENT_SEQ)
                Sdi->BlackAllowed = true;
            if (Event.Underflow)
                Sdi->FifoUfl = Sdi->FifoUflLatched = true;
        }
        else if (Sdi->FirstEventSeen)
            Sdi->BlackAllowed = true;
        if (Result != DTAPI_OK && Result != DTAPI_E_TIMEOUT)
            Failed = true;

        if (Result != DTAPI_OK || ++Sdi->NumFormatEvents % DT_SDITX_PHY_POLL_EVENTS == 0)
        {
            bool Underflow = false;

            if (DtPcieCmd_SdiTxPhyGetUnderflowFlag(Drv, Sdi->Phy, &Underflow) == DTAPI_OK)
            {
                Sdi->DmaUfl = Underflow;
                if (Underflow)
                {
                    Sdi->DmaUflLatched = true;
                    DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Sdi->Phy);
                }
            }
        }

        // The frame going out is the last: a black frame follows it.
        size_t Load;
        if (Sdi->BlackAllowed && UnsentFrames(Sdi) <= 1 &&
            DmaBufferLoad(Sdi, &Load) == DTAPI_OK)
            InsertBlack(Sdi, Load);
        OsEvent_Set(Sdi->RoomEvent);
        OsMutex_Unlock(Sdi->Tx.Port.Lock);

        // A wait that fails at once would otherwise spin.
        if (Failed)
            OsTime_SleepMs(WaitMs);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopSignalKeeper -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Stops the thread and waits for it, releasing the lock while it does, since the thread
// takes the lock to see that it must stop.
//
static void StopSignalKeeper(DtSdiTx* Sdi)
{
    OsThread* Thread = Sdi->KeeperThread;

    if (Thread == NULL)
        return;
    Sdi->StopRequested = true;
    Sdi->KeeperThread = NULL;
    OsMutex_Unlock(Sdi->Tx.Port.Lock);
    OsThread_Join(Thread);
    OsMutex_Lock(Sdi->Tx.Port.Lock);
    Sdi->StopRequested = false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= States +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BlocksToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every block idle, downstream first: the switch from a quad-link master where the port
// has it, and the demultiplexer and its switches on a port with DT_CAP_QUADLINK. Returns
// the first failure; the blocks after it are set idle all the same.
//
static DtapiResult BlocksToIdle(DtSdiTx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    DtapiResult Results[9] = {DTAPI_OK, DTAPI_OK, DTAPI_OK, DTAPI_OK, DTAPI_OK,
                              DTAPI_OK, DTAPI_OK, DTAPI_OK, DTAPI_OK};

    Results[0] = DtPcieCmd_SdiTxPhySetOpMode(Drv, Sdi->Phy, DT_FUNC_OPMODE_IDLE);
    Results[1] = DtPcieCmd_SdiTxPSetOpMode(Drv, Sdi->Txp, DT_BLOCK_OPMODE_IDLE);
    if (Sdi->QuadLinkMasterSwitch.Uuid != 0)
        Results[2] = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->QuadLinkMasterSwitch,
                                               DT_BLOCK_OPMODE_IDLE);
    if (Sdi->HasQuadLink)
    {
        Results[3] =
            DtPcieCmd_SwitchSetOpMode(Drv, Sdi->DemuxOutSwitch, DT_BLOCK_OPMODE_IDLE);
        Results[4] = DtPcieCmd_SdiDmx12GSetOpMode(Drv, Sdi->Dmx, DT_BLOCK_OPMODE_IDLE);
        Results[5] =
            DtPcieCmd_SwitchSetOpMode(Drv, Sdi->DemuxInSwitch, DT_BLOCK_OPMODE_IDLE);
    }
    Results[6] = DtPcieCmd_SdiTxFSetOpMode(Drv, Sdi->Txf, DT_BLOCK_OPMODE_IDLE);
    Results[7] = DtPcieCmd_BurstFifoSetOpMode(Drv, Sdi->BurstFifo, DT_BLOCK_OPMODE_IDLE);
    Results[8] = DtPcieCmd_CdmacSetOpMode(Drv, Sdi->Cdmac, DT_BLOCK_OPMODE_IDLE);

    for (size_t i = 0; i < sizeof(Results) / sizeof(Results[0]); i++)
    {
        if (Results[i] != DTAPI_OK)
            return Results[i];
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IdleToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The pipeline runs and fills from the start of the buffer, the demultiplexer left out on
// a single link, and the PHY waits. A channel that registered no buffer, which is one
// whose configuration carries no raw frames, fails here before any block changes.
//
static DtapiResult IdleToHold(DtSdiTx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);

    if (!Sdi->BufferRegistered)
        return DTAPI_E_CONFIG_RAW_SDI;

    DtapiResult Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Sdi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTxWriteOffset(Drv, Sdi->Cdmac, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Sdi->Cdmac, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Sdi->BurstFifo, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxFSetOpMode(Drv, Sdi->Txf, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK && Sdi->QuadLinkMasterSwitch.Uuid != 0)
        Result = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->QuadLinkMasterSwitch,
                                           DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK && Sdi->HasQuadLink)
        Result = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->DemuxInSwitch, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK && Sdi->HasQuadLink)
        Result = DtPcieCmd_SdiDmx12GSetOpMode(Drv, Sdi->Dmx, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK && Sdi->HasQuadLink)
        Result = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->DemuxOutSwitch, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPSetOpMode(Drv, Sdi->Txp, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Sdi->Phy, DT_FUNC_OPMODE_STANDBY);
    if (Result != DTAPI_OK)
    {
        BlocksToIdle(Sdi);
        return Result;
    }

    Sdi->WriteOffset = 0;
    Sdi->CommittedBytes = 0;
    Sdi->NextFrameId = 0;
    Sdi->FirstEventSeen = false;
    Sdi->BlackAllowed = false;
    Sdi->SendingFrameId = 0;
    ForgetPartialFrame(Sdi);
    Sdi->Tx.TxControl = DTAPI_TXCTRL_HOLD;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToSend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Refused with DTAPI_E_INSUF_LOAD without a frame that has not gone out, and with
// DTAPI_E_CONFIG_RAW_SDI for 8-bit symbols. Then the burst FIFO's load is read up to five
// times, without a pause, for 75 %, and sending goes ahead either way; its and the
// reorder buffer's statistics are cleared, and the PHY runs. The PHY's underflow flag of
// an earlier run is cleared too.
//
static DtapiResult HoldToSend(DtSdiTx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    DtBurstFifoStatus Status = {0};

    if (UnsentFrames(Sdi) < 1)
        return DTAPI_E_INSUF_LOAD;
    if (Sdi->BitsPerSymbol == 8)
        return DTAPI_E_CONFIG_RAW_SDI;

    DtapiResult Result;
    for (int Poll = 0; Poll < DT_SDITX_BURST_POLLS; Poll++)
    {
        Result = DtPcieCmd_BurstFifoGetStatus(Drv, Sdi->BurstFifo, &Status);
        if (Result != DTAPI_OK)
            return Result;
        if (Status.CurLoad >= Sdi->BurstFifoSize * 3 / 4)
            break;
    }

    Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Sdi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Sdi->BurstFifo, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Sdi->Phy);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Sdi->Phy, DT_FUNC_OPMODE_RUN);
    if (Result != DTAPI_OK)
        return Result;

    Sdi->StopRequested = false;
    Sdi->NumFormatEvents = 0;
    Sdi->KeeperThread = OsThread_Start(SignalKeeperThread, Sdi);
    if (Sdi->KeeperThread == NULL)
    {
        DtPcieCmd_SdiTxPhySetOpMode(Drv, Sdi->Phy, DT_FUNC_OPMODE_STANDBY);
        return DTAPI_E_OUT_OF_MEM;
    }
    Sdi->Tx.TxControl = DTAPI_TXCTRL_SEND;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// After the thread has stopped: the PHY waits, the underflow status is cleared and the
// latched flag kept.
//
static DtapiResult SendToHold(DtSdiTx* Sdi)
{
    StopSignalKeeper(Sdi);
    DtapiResult Result =
        DtPcieCmd_SdiTxPhySetOpMode(DrvOf(Sdi), Sdi->Phy, DT_FUNC_OPMODE_STANDBY);
    Sdi->FifoUfl = false;
    Sdi->Tx.TxControl = DTAPI_TXCTRL_HOLD;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToIdle -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every block idle; what the buffer held is forgotten. The channel is idle afterwards
// even when a block refused.
//
static DtapiResult HoldToIdle(DtSdiTx* Sdi)
{
    DtapiResult Result = BlocksToIdle(Sdi);

    ForgetPartialFrame(Sdi);
    Sdi->Tx.TxControl = DTAPI_TXCTRL_IDLE;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ChangeTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// IDLE to SEND goes through HOLD, and SEND to IDLE too.
//
static DtapiResult ChangeTxControl(DtSdiTx* Sdi, int TxControl)
{
    DtapiResult Result = DTAPI_OK;

    if (Sdi->Tx.TxControl == TxControl)
        return DTAPI_OK;
    if (TxControl != DTAPI_TXCTRL_IDLE && TxControl != DTAPI_TXCTRL_HOLD &&
        TxControl != DTAPI_TXCTRL_SEND)
    {
        return DTAPI_E_INVALID_ARG;
    }

    if (Sdi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
        Result = IdleToHold(Sdi);
    else if (Sdi->Tx.TxControl == DTAPI_TXCTRL_SEND)
        Result = SendToHold(Sdi);
    if (Result != DTAPI_OK || TxControl == DTAPI_TXCTRL_HOLD)
        return Result;

    if (TxControl == DTAPI_TXCTRL_SEND)
        return HoldToSend(Sdi);
    return HoldToIdle(Sdi);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BufferSizeFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Room for five coded frames and as many more as a 48 MB FIFO holds of raw 10-bit
// frames, rounded up to a power of two within the buffer's bounds, and to whole prefetch
// units of pages.
//
static size_t BufferSizeFor(const DtSdiTx* Sdi, int PrefetchSize)
{
    size_t Raw = DtSdiFrame_RawSize(&Sdi->FrameLayout, 10);
    size_t DriverBlockSpec =
        (DT_SDITX_BUF_ROOM_FRAMES + DT_SDITX_FIFO_SIZE_TYP / Raw) * Sdi->CodedFrameSize;
    size_t Unit = 4096 * (size_t)(PrefetchSize > 0 ? PrefetchSize : 1);
    size_t Size = DT_SDITX_BUF_MIN_SIZE;

    while (Size < DriverBlockSpec && Size < DT_SDITX_BUF_MAX_SIZE)
        Size *= 2;
    return (Size + Unit - 1) / Unit * Unit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AllocBandSymbols -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The conversion's working symbols, one set for every band a batch of lines divides into.
// NULL for a standard that has none, which is every standard except 4K.
//
static uint16_t* AllocBandSymbols(DtSdiTx* Sdi)
{
    if (Sdi->SymbolsPerBand == 0)
        return NULL;
    return (uint16_t*)DtAlloc_Malloc((size_t)DtJobRunner_NumPieces(&Sdi->JobRunner) *
                                     Sdi->SymbolsPerBand * sizeof(uint16_t));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureJobRunner -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Divides the lines over the pool the channel gave, into the pieces it asked for or, with
// 0, the ones the standard calls for, and sizes the working symbols by them. Symbols that
// cannot be had for those pieces are taken for one, so that the side encodes in the
// writing thread rather than not at all, and DTAPI_E_OUT_OF_MEM says so; BandSymbols is
// NULL for a standard that needs it when not even those can be had.
//
static DtapiResult ConfigureJobRunner(DtSdiTx* Sdi)
{
    const int Pieces = Sdi->WorkerThreads > 0
                           ? Sdi->WorkerThreads
                           : DtSdiFrame_NumWorkPieces(&Sdi->FrameLayout);

    DtapiResult Result = DtJobRunner_SetPool(&Sdi->JobRunner, Sdi->WorkerPool, Pieces);
    DtAlloc_Free(Sdi->BandSymbols);
    Sdi->BandSymbols = Result == DTAPI_OK ? AllocBandSymbols(Sdi) : NULL;
    if (Result == DTAPI_OK && Sdi->SymbolsPerBand != 0 && Sdi->BandSymbols == NULL)
        Result = DTAPI_E_OUT_OF_MEM;
    if (Result != DTAPI_OK)
    {
        DtJobRunner_SetPool(&Sdi->JobRunner, NULL, 0);
        Sdi->BandSymbols = AllocBandSymbols(Sdi);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeStandardBuffers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The DMA controller lets go of the buffer, which is then freed, and the standard's
// buffers go too. Failures are ignored.
//
static void FreeStandardBuffers(DtSdiTx* Sdi)
{
    if (Sdi->BufferRegistered)
    {
        DtPcieCmd_CdmacSetOpMode(DrvOf(Sdi), Sdi->Cdmac, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(DrvOf(Sdi), Sdi->Cdmac);
    }
    Sdi->BufferRegistered = false;
    OsDmaBuffer_Free(&Sdi->DmaBuffer);
    DtAlloc_Free(Sdi->BlackLines);
    DtAlloc_Free(Sdi->WrapLineBuffer);
    DtAlloc_Free(Sdi->PartialLine);
    DtAlloc_Free(Sdi->BandSymbols);
    Sdi->BlackLines = Sdi->WrapLineBuffer = Sdi->PartialLine = NULL;
    Sdi->BandSymbols = NULL;
    memset(&Sdi->FrameLayout, 0, sizeof(Sdi->FrameLayout));
    Sdi->FrameLayout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    Sdi->CodedFrameSize = Sdi->RawFrameSize = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the channel up for the port's I/O standard, while idle: the layout from the
// formatter's stream alignment, format events, the start-of-frame offset, the switches
// around the demultiplexer, and a buffer for the standard, registered anew when its size
// or the frame's geometry changes. 2160p over one 6G or 12G link is sent as raw frames
// (plan 0014); a 4K standard over four links, or of level-B links, leaves the channel
// without a buffer; see IdleToHold.
//
static DtapiResult ConfigureChannel(DtSdiTx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    DtCdmacProps Props = {0};
    DtBurstFifoProps BurstProps = {0};
    DtSdiFrameLayout Layout = {0};
    int Alignment = 0;

    const DtVidStdInfo* Info = DtVidStd_Find(Sdi->IoStdSubValue);
    const bool OneLink = Sdi->IoStdValue == DTAPI_IOCONFIG_6GSDI ||
                         Sdi->IoStdValue == DTAPI_IOCONFIG_12GSDI;
    if ((OneLink || DtVidStd_Is4k(Sdi->IoStdSubValue)) &&
        (!OneLink || Info == NULL || Info->IsLevelB))
    {
        FreeStandardBuffers(Sdi);
        return DTAPI_OK;
    }
    DtFrameProps Frame;
    if (!DtFrameProps_Init(&Frame, Sdi->IoStdSubValue))
    {
        FreeStandardBuffers(Sdi);
        return DTAPI_E_INVALID_VIDSTD;
    }

    DtapiResult Result = DtPcieCmd_SdiTxFGetStreamAlignment(Drv, Sdi->Txf, &Alignment);
    if (Result == DTAPI_OK &&
        !DtSdiFrame_LayoutInit(&Layout, Sdi->IoStdSubValue, Alignment))
        Result = DTAPI_E_INTERNAL;
    if (Result == DTAPI_OK && Layout.TxHeaderNumBytes > DT_SDITX_MAX_HEADER_BYTES)
        Result = DTAPI_E_INTERNAL;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxFSetFmtEventSetting(
            Drv, Sdi->Txf,
            (Layout.NumCodedLines + DT_SDIFRAME_FMT_EVENTS_PER_FRAME - 1) /
                    DT_SDIFRAME_FMT_EVENTS_PER_FRAME +
                1,
            1);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetStartOfFrameOffset(Drv, Sdi->Phy, 0);
    if (Result == DTAPI_OK && Sdi->HasQuadLink)
        Result = DtPcieCmd_SwitchSetPosition(Drv, Sdi->DemuxInSwitch, 0, 0);
    if (Result == DTAPI_OK && Sdi->HasQuadLink)
        Result = DtPcieCmd_SwitchSetPosition(Drv, Sdi->DemuxOutSwitch, 0, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Sdi->Cdmac, &Props);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetProps(Drv, Sdi->BurstFifo, &BurstProps);
    if (Result != DTAPI_OK)
    {
        FreeStandardBuffers(Sdi);
        return Result;
    }

    size_t Size = Sdi->DmaBuffer.Size;
    if (Sdi->FrameLayout.VidStd == DTAPI_VIDSTD_UNKNOWN ||
        Sdi->FrameLayout.TxStride != Layout.TxStride ||
        Sdi->FrameLayout.NumCodedLines != Layout.NumCodedLines ||
        Sdi->FrameLayout.TxHeaderNumBytes != Layout.TxHeaderNumBytes)
    {
        Size = 0;
    }
    Sdi->FrameLayout = Layout;
    Sdi->CodedFrameSize = DtSdiFrame_TxCodedSize(&Layout);
    Sdi->RawFrameSize = DtSdiFrame_RawSize(&Layout, Sdi->BitsPerSymbol);

    // The standard's black frame and the buffers of a write.
    size_t Line = DtSdiFrame_RawLineNumBits(&Layout, 16) / 8 + 2;
    DtAlloc_Free(Sdi->BlackLines);
    DtAlloc_Free(Sdi->WrapLineBuffer);
    DtAlloc_Free(Sdi->PartialLine);
    DtAlloc_Free(Sdi->BandSymbols);
    Sdi->BandSymbols = NULL;
    Sdi->BlackLines =
        (uint8_t*)DtAlloc_Malloc((size_t)Layout.NumCodedLines * (size_t)Layout.TxStride);
    Sdi->WrapLineBuffer = (uint8_t*)DtAlloc_Malloc(DtSdiFrame_TxBytesPerLine(&Layout));
    Sdi->PartialLine = (uint8_t*)DtAlloc_Malloc(Line);
    Sdi->SymbolsPerBand = DtSdiFrame_NumScratchSymbols(&Layout);
    ConfigureJobRunner(Sdi);
    if (Sdi->BlackLines == NULL || Sdi->WrapLineBuffer == NULL ||
        Sdi->PartialLine == NULL || (Layout.Is4k && Sdi->BandSymbols == NULL) ||
        !DtSdiFrame_BlackLines(&Layout, Sdi->BlackLines))
    {
        FreeStandardBuffers(Sdi);
        return DTAPI_E_OUT_OF_MEM;
    }

    // A buffer of another size replaces the registered one.
    if (!Sdi->BufferRegistered || Size != BufferSizeFor(Sdi, Props.PrefetchSize))
    {
        Size = BufferSizeFor(Sdi, Props.PrefetchSize);
        if (Sdi->BufferRegistered)
        {
            DtPcieCmd_CdmacSetOpMode(Drv, Sdi->Cdmac, DT_BLOCK_OPMODE_IDLE);
            DtPcieCmd_CdmacFreeBuffer(Drv, Sdi->Cdmac);
            Sdi->BufferRegistered = false;
        }
        OsDmaBuffer_Free(&Sdi->DmaBuffer);

        Result =
            OsDmaBuffer_Alloc(Size, &Sdi->DmaBuffer) == 0 ? DTAPI_OK : DTAPI_E_OUT_OF_MEM;
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_CdmacAllocateBuffer(Drv, Sdi->Cdmac, DT_CDMAC_DIR_TX,
                                                   &Sdi->DmaBuffer);
        Sdi->BufferRegistered = Result == DTAPI_OK;
        if (Result == DTAPI_OK)
            Result =
                DtPcieCmd_CdmacSetTestMode(Drv, Sdi->Cdmac, DT_CDMAC_TESTMODE_NORMAL);
        if (Result != DTAPI_OK)
        {
            FreeStandardBuffers(Sdi);
            return Result;
        }
    }

    Sdi->PcieWordBytes = (size_t)Props.PcieDataWidth / 8;
    Sdi->MaxLoad = Sdi->DmaBuffer.Size - Sdi->PcieWordBytes;
    if (Sdi->MaxLoad / Sdi->CodedFrameSize < DT_SDITX_BUF_MIN_FRAMES)
    {
        FreeStandardBuffers(Sdi);
        return DTAPI_E_INTERNAL;
    }
    Sdi->BurstFifoSize = BurstProps.FifoSize;

    int Num;
    int Den;
    DtVidStd_Fps(Sdi->IoStdSubValue, &Num, &Den);
    Sdi->QuarterFrameMs = Den * 1000 / Num / DT_SDIFRAME_FMT_EVENTS_PER_FRAME;
    if (Sdi->QuarterFrameMs < 1)
        Sdi->QuarterFrameMs = 1;
    ForgetPartialFrame(Sdi);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SymbolAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Symbol Index of a raw line in the transmit mode, from its first byte.
//
static uint32_t SymbolAt(const DtSdiTx* Sdi, const uint8_t* Bytes, size_t Index)
{
    if (Sdi->BitsPerSymbol == 16)
        return ((uint32_t)Bytes[2 * Index] | (uint32_t)Bytes[2 * Index + 1] << 8) & 0x3FF;
    else if (Sdi->BitsPerSymbol == 8)
        return (uint32_t)Bytes[Index] << 2;
    else
    {
        size_t Bit = Index * 10;
        uint32_t Value = (uint32_t)Bytes[Bit / 8] | (uint32_t)Bytes[Bit / 8 + 1] << 8;

        return (Value >> (Bit % 8)) & 0x3FF;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FrameStartBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bytes the start of a frame is recognised by: twelve symbols in HD and 3G, four in
// SD, and 48 in 2160p, whose eight streams each hold the six words.
//
static size_t FrameStartBytes(const DtSdiTx* Sdi)
{
    size_t Symbols = Sdi->FrameLayout.SdiRate == DT_SDIRATE_SD ? 4
                     : Sdi->FrameLayout.Is4k                   ? 48
                                                               : 12;

    return Symbols * (size_t)Sdi->BitsPerSymbol / 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsFrameStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Checks for an EAV, comparing the upper eight bits of each symbol. HD and 3G start with
// the EAV and line number of line 1, each word in both streams; 2160p over one link with
// the same six words in each of its eight streams. SD has no line number: its first frame
// starts at any line in the vertical blanking of field 1, and after a mismatch the search
// first needs an active line of field 2 and then again a blanking line of field 1.
//
static bool IsFrameStart(DtSdiTx* Sdi, const uint8_t* Bytes)
{
    static const uint32_t Eav[6] = {0x3FF, 0, 0, 0x2D8, 0x204, 0x200};
    static const uint32_t Hd[12] = {0x3FF, 0x3FF, 0,     0,     0,     0,
                                    0x2D8, 0x2D8, 0x204, 0x204, 0x200, 0x200};
    static const uint32_t SdFrameStart[4] = {0x3FF, 0, 0, 0x2D8};
    static const uint32_t SdField2[4] = {0x3FF, 0, 0, 0x368};
    bool IsSd = Sdi->FrameLayout.SdiRate == DT_SDIRATE_SD;
    const uint32_t* Symbols = !IsSd ? Hd
                              : Sdi->SdSearchState == DT_SDITX_SD_FIND_FIELD2
                                  ? SdField2
                                  : SdFrameStart;
    size_t Count = IsSd ? 4 : Sdi->FrameLayout.Is4k ? 48 : 12;
    size_t i;

    for (i = 0; i < Count; i++)
    {
        uint32_t Want = Sdi->FrameLayout.Is4k ? Eav[i / 8] : Symbols[i];

        if ((SymbolAt(Sdi, Bytes, i) & 0x3FC) != (Want & 0x3FC))
        {
            if (IsSd && Sdi->SdSearchState == DT_SDITX_SD_IN_SYNC)
                Sdi->SdSearchState = DT_SDITX_SD_FIND_FIELD2;
            return false;
        }
    }
    if (IsSd && Sdi->SdSearchState == DT_SDITX_SD_FIND_FIELD2)
    {
        Sdi->SdSearchState = DT_SDITX_SD_FIND_FRAME_START;
        return false;
    }
    if (IsSd)
        Sdi->SdSearchState = DT_SDITX_SD_IN_SYNC;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Line 1 starts here; BytesAlreadyHeld of its bytes are in PartialLine already.
//
static void StartFrame(DtSdiTx* Sdi, size_t BytesAlreadyHeld)
{
    Sdi->WriteStage = DT_SDITX_STAGE_LINES;
    Sdi->FrameRoomReserved = false;
    Sdi->LinesEncoded = 0;
    Sdi->LineStartBit = 0;
    Sdi->PartialLineBytes = BytesAlreadyHeld;
    Sdi->FrameBytesLeft = Sdi->RawFrameSize - BytesAlreadyHeld;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFrameBoundary -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Bytes that cannot start a frame are skipped four at a time, and bytes too few to judge
// are kept for the next write.
//
static void FindFrameBoundary(DtSdiTx* Sdi, const uint8_t** Data, size_t* BytesLeft)
{
    size_t Need = FrameStartBytes(Sdi);

    while (Sdi->PartialLineBytes > 0)
    {
        size_t Extra = Need - Sdi->PartialLineBytes;

        if (*BytesLeft < Extra)
        {
            memcpy(Sdi->PartialLine + Sdi->PartialLineBytes, *Data, *BytesLeft);
            Sdi->PartialLineBytes += *BytesLeft;
            *Data += *BytesLeft;
            *BytesLeft = 0;
            return;
        }
        memcpy(Sdi->PartialLine + Sdi->PartialLineBytes, *Data, Extra);
        if (IsFrameStart(Sdi, Sdi->PartialLine))
        {
            *Data += Extra;
            *BytesLeft -= Extra;
            StartFrame(Sdi, Need);
            return;
        }
        if (Sdi->PartialLineBytes <= 4)
            Sdi->PartialLineBytes = 0;
        else
        {
            Sdi->PartialLineBytes -= 4;
            memmove(Sdi->PartialLine, Sdi->PartialLine + 4, Sdi->PartialLineBytes);
        }
    }

    while (*BytesLeft >= Need)
    {
        if (IsFrameStart(Sdi, *Data))
        {
            StartFrame(Sdi, 0);
            return;
        }
        *Data += 4;
        *BytesLeft -= 4;
    }
    memcpy(Sdi->PartialLine, *Data, *BytesLeft);
    Sdi->PartialLineBytes = *BytesLeft;
    *Data += *BytesLeft;
    *BytesLeft = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitForRoom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits, without the lock, until the buffer has room for a whole frame, or until the
// monotonic clock reaches Deadline, DT_TX_NO_DEADLINE for no limit. While sending the
// thread wakes the wait after every format event; while holding nothing goes out, and the
// wait looks again every quarter frame. Returns DTAPI_E_CANCELLED for a detach,
// DTAPI_E_IDLE when the channel went idle meanwhile, and DTAPI_E_TIMEOUT.
//
static DtapiResult WaitForRoom(DtSdiTx* Sdi, uint64_t Deadline)
{
    for (;;)
    {
        if (*Sdi->Tx.Port.WaitingDetaches > 0)
            return DTAPI_E_CANCELLED;
        if (Sdi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
            return DTAPI_E_IDLE;
        size_t Load;
        DtapiResult Result = DmaBufferLoad(Sdi, &Load);
        if (Result != DTAPI_OK)
            return Result;
        if (Sdi->MaxLoad - Load >= Sdi->CodedFrameSize)
            return DTAPI_OK;

        uint64_t Now = OsTime_MonotonicMs();
        if (Now >= Deadline)
            return DTAPI_E_TIMEOUT;
        int Wait = Sdi->QuarterFrameMs;
        if (Deadline - Now < (uint64_t)Wait)
            Wait = (int)(Deadline - Now);

        OsMutex_Unlock(Sdi->Tx.Port.Lock);
        if (Sdi->KeeperThread != NULL)
            OsEvent_Wait(Sdi->RoomEvent, Wait);
        else
            OsTime_SleepMs(Wait);
        OsMutex_Lock(Sdi->Tx.Port.Lock);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeOneLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Encodes the next line into its place in the buffer once all of its bytes are there. The
// frame's header is written first, when there is room for the whole frame, which is
// waited for until Deadline. A line whose last byte is shared with the next line leaves
// that byte for the next.
//
static DtapiResult EncodeOneLine(DtSdiTx* Sdi, const uint8_t** Data, size_t* BytesLeft,
                                 uint64_t Deadline)
{
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    size_t Bits = DtSdiFrame_RawLineNumBits(Layout, Sdi->BitsPerSymbol);
    size_t Need = ((size_t)Sdi->LineStartBit + Bits + 7) / 8;
    size_t Used = ((size_t)Sdi->LineStartBit + Bits) / 8;

    if (!Sdi->FrameRoomReserved)
    {
        DtapiResult Result = WaitForRoom(Sdi, Deadline);

        if (Result != DTAPI_OK || Sdi->WriteStage != DT_SDITX_STAGE_LINES ||
            Sdi->FrameRoomReserved)
            return Result;
        WriteFrameHeader(Sdi, Sdi->WriteOffset, Sdi->NextFrameId);
        Sdi->FrameRoomReserved = true;
    }

    const uint8_t* Src;
    if (Sdi->PartialLineBytes == 0 && *BytesLeft >= Need)
    {
        Src = *Data;
        *Data += Used;
        *BytesLeft -= Used;
        Sdi->FrameBytesLeft -= Used;
    }
    else
    {
        size_t Copy = Need - Sdi->PartialLineBytes;

        if (Copy > *BytesLeft)
            Copy = *BytesLeft;
        memcpy(Sdi->PartialLine + Sdi->PartialLineBytes, *Data, Copy);
        Sdi->PartialLineBytes += Copy;
        *Data += Copy;
        *BytesLeft -= Copy;
        Sdi->FrameBytesLeft -= Copy;
        if (Sdi->PartialLineBytes < Need)
            return DTAPI_OK;
        Src = Sdi->PartialLine;
    }

    // A raw line becomes one coded line, or the two coded lines of 4K, each of them
    // preceded by its line header.
    size_t Coded = DtSdiFrame_TxBytesPerLine(Layout);
    size_t Offset = WrapOffset(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderNumBytes +
                                        (size_t)Sdi->LinesEncoded * Coded);
    uint8_t* Dst = Offset + Coded <= Sdi->DmaBuffer.Size ? Sdi->DmaBuffer.Data + Offset
                                                         : Sdi->WrapLineBuffer;
    if (Layout->Is4k)
    {
        DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Sdi->LinesEncoded, Dst);
        DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Sdi->LinesEncoded + 1,
                                      Dst + Layout->TxStride);
        DtSdiFrame_EncodeLine4k(Layout, Sdi->BitsPerSymbol, Src, Sdi->LinesEncoded,
                                Dst + Layout->TxLineHeaderNumBytes,
                                Dst + Layout->TxStride + Layout->TxLineHeaderNumBytes,
                                Sdi->BandSymbols);
    }
    else
        DtSdiFrame_EncodeLine(Layout, Sdi->BitsPerSymbol, Src, Sdi->LineStartBit, Dst);
    if (Dst == Sdi->WrapLineBuffer)
        CopyIntoBuffer(Sdi, Offset, Sdi->WrapLineBuffer, Coded);

    if (Src == Sdi->PartialLine)
    {
        Sdi->PartialLineBytes = Need - Used;
        if (Sdi->PartialLineBytes > 0)
            Sdi->PartialLine[0] = Sdi->PartialLine[Used];
    }
    Sdi->LineStartBit = (int)(((size_t)Sdi->LineStartBit + Bits) % 8);
    Sdi->LinesEncoded++;

    // What is left of the last line's byte is padding.
    if (Sdi->LinesEncoded == Layout->NumLines)
    {
        Sdi->PartialLineBytes = 0;
        Sdi->WriteStage = DT_SDITX_STAGE_PADDING;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Encodes the band of raw lines this piece takes straight into the buffer. The bands are
// independent: a raw line's coded lines, one or the two of 4K with their headers, are
// its own, no coding writes past them, and the working symbols are per band.
//
typedef struct EncodeBand
{
    DtSdiTx* Sdi;
    const uint8_t* Data; // Where the first of Lines raw lines begins
    size_t Bits;         // Bits one raw line takes
    size_t LineStartBit; // Bit of that first byte the first line begins at, 0 to 7
    size_t Coded;        // What one raw line takes in the buffer, headers and all
    size_t Offset;       // Where the first of them goes
    int FirstLine;       // Its index in the frame, from 0
    int Lines;
} EncodeBand;

static void EncodeLines(void* Context, int Index, int Count)
{
    const EncodeBand* Band = (const EncodeBand*)Context;
    DtSdiTx* Sdi = Band->Sdi;
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    uint16_t* BandSymbols = Sdi->BandSymbols == NULL
                                ? NULL
                                : Sdi->BandSymbols + (size_t)Index * Sdi->SymbolsPerBand;
    int First;
    int Last;

    DtJobRunner_Split(Band->Lines, Index, Count, 1, &First, &Last);
    for (int i = First; i < Last; i++)
    {
        // Where line i begins in the raw frame. With 10-bit symbols a line that is not 4K
        // can begin part way through a byte, and the byte it shares is only read.
        const size_t At = Band->LineStartBit + (size_t)i * Band->Bits;
        const uint8_t* Src = Band->Data + At / 8;
        uint8_t* Dst = Sdi->DmaBuffer.Data + Band->Offset + (size_t)i * Band->Coded;
        const int Line = Band->FirstLine + i;

        if (!Layout->Is4k)
        {
            DtSdiFrame_EncodeLine(Layout, Sdi->BitsPerSymbol, Src, (int)(At % 8), Dst);
            continue;
        }
        DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Line, Dst);
        DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Line + 1, Dst + Layout->TxStride);
        DtSdiFrame_EncodeLine4k(
            Layout, Sdi->BitsPerSymbol, Src, Line, Dst + Layout->TxLineHeaderNumBytes,
            Dst + Layout->TxStride + Layout->TxLineHeaderNumBytes, BandSymbols);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeLineBatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Encodes as many whole lines as this call brings, over the threads the channel has, and
// returns how many it did. Zero where there is nothing to divide, and the caller then
// takes one line with EncodeOneLine: a channel of one thread; a frame whose room is not
// reserved yet, which is its first line; bytes of a line left over from the call before;
// and a batch that would be a single line.
//
// Every standard divides. The coded lines a band writes are its own whatever the
// standard, since each begins on a byte of its own; the raw lines a band reads can share
// a byte with the line before or after, which with 10-bit symbols they do, but reading
// the same byte on two threads is not a risk. So a band starts wherever it likes and the
// line's own bit is worked out from the phase the batch began at.
//
// Only the lines that lie in one piece before the end of the buffer are taken. The line
// that runs across the end goes through the line buffer and is copied in two, which is a
// line at a time by nature, and the lines after it start a batch of their own.
//
// A batch holds the channel's lock while it runs, so it is capped at a quarter of the
// frame: long enough that the threads earn their dispatch, short enough that a detach or
// a stop does not wait a whole frame's coding for the lock.
//
static int EncodeLineBatch(DtSdiTx* Sdi, const uint8_t** Data, size_t* BytesLeft)
{
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;

    if (DtJobRunner_NumPieces(&Sdi->JobRunner) < 2 || !Sdi->FrameRoomReserved ||
        Sdi->WriteStage != DT_SDITX_STAGE_LINES || Sdi->PartialLineBytes != 0)
    {
        return 0;
    }

    const size_t Bits = DtSdiFrame_RawLineNumBits(Layout, Sdi->BitsPerSymbol);
    const size_t LineStartBit = (size_t)Sdi->LineStartBit;
    const size_t Coded = DtSdiFrame_TxBytesPerLine(Layout);
    const size_t Offset =
        WrapOffset(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderNumBytes +
                            (size_t)Sdi->LinesEncoded * Coded);
    const int Cap = Layout->NumLines / 4 < 2 ? 2 : Layout->NumLines / 4;

    // The lines whose every bit this call brings: the last of them may end part way
    // through the last byte, which the line after it begins in and which stays.
    size_t Lines = Bits == 0 ? 0 : (8 * *BytesLeft - LineStartBit) / Bits;

    if (Lines > (size_t)(Layout->NumLines - Sdi->LinesEncoded))
        Lines = (size_t)(Layout->NumLines - Sdi->LinesEncoded);
    if (Lines > (size_t)Cap)
        Lines = (size_t)Cap;
    if (Offset + Lines * Coded > Sdi->DmaBuffer.Size)
        Lines = (Sdi->DmaBuffer.Size - Offset) / Coded;
    if (Lines < 2)
        return 0;

    EncodeBand Band;
    Band.Sdi = Sdi;
    Band.Data = *Data;
    Band.Bits = Bits;
    Band.LineStartBit = LineStartBit;
    Band.Coded = Coded;
    Band.Offset = Offset;
    Band.FirstLine = Sdi->LinesEncoded;
    Band.Lines = (int)Lines;
    DtJobRunner_Run(&Sdi->JobRunner, EncodeLines, &Band);

    // The bytes the batch used up are those it has no more bits left in; a byte the next
    // line begins in is left where it is, as it is for a single line.
    const size_t End = LineStartBit + Lines * Bits;
    *Data += End / 8;
    *BytesLeft -= End / 8;
    Sdi->FrameBytesLeft -= End / 8;
    Sdi->LinesEncoded += (int)Lines;
    Sdi->LineStartBit = (int)(End % 8);

    // What is left of the last line's byte is padding.
    if (Sdi->LinesEncoded == Layout->NumLines)
    {
        Sdi->PartialLineBytes = 0;
        Sdi->WriteStage = DT_SDITX_STAGE_PADDING;
    }
    return (int)Lines;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Into the buffer: every byte is taken, a frame at a time. The lock is released after
// every line or batch of lines, so that the thread is not kept waiting, and the state is
// looked at again after it was.
//
static DtapiResult Write(DtTx* Tx, const uint8_t* Data, size_t BytesLeft)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;
    DtapiResult Result = DTAPI_OK;

    while (Result == DTAPI_OK)
    {
        if (*Sdi->Tx.Port.WaitingDetaches > 0)
            return DTAPI_E_CANCELLED;
        if (Sdi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
            return DTAPI_E_IDLE;

        if (Sdi->WriteStage == DT_SDITX_STAGE_PADDING)
        {
            size_t Skip =
                BytesLeft < Sdi->FrameBytesLeft ? BytesLeft : Sdi->FrameBytesLeft;

            Data += Skip;
            BytesLeft -= Skip;
            Sdi->FrameBytesLeft -= Skip;
            if (Sdi->FrameBytesLeft == 0)
            {
                Result = CommitFrame(Sdi);
                if (Result == DTAPI_OK)
                    Sdi->FifoUfl = false;
                Sdi->WriteStage = DT_SDITX_STAGE_SEARCH;
                Sdi->FrameRoomReserved = false;
                Sdi->PartialLineBytes = 0;
            }
            continue;
        }
        if (BytesLeft == 0)
            break;

        if (Sdi->WriteStage == DT_SDITX_STAGE_SEARCH)
            FindFrameBoundary(Sdi, &Data, &BytesLeft);
        else
        {
            if (EncodeLineBatch(Sdi, &Data, &BytesLeft) == 0)
                Result = EncodeOneLine(Sdi, &Data, &BytesLeft, DT_TX_NO_DEADLINE);
            OsMutex_Unlock(Sdi->Tx.Port.Lock);
            OsMutex_Lock(Sdi->Tx.Port.Lock);
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
static DtapiResult CheckFrame(DtSdiTx* Sdi, const uint8_t* Frame, int FrameSize)
{
    if ((size_t)FrameSize != Sdi->RawFrameSize)
        return DTAPI_E_INVALID_SIZE;

    Sdi->SdSearchState = DT_SDITX_SD_IN_SYNC;
    return IsFrameStart(Sdi, Frame) ? DTAPI_OK : DTAPI_E_INVALID_FRAME;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteOneFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Encodes a frame into the buffer as Write does, and commits it, waiting for room
// until Deadline. The lock is released after every line or batch of lines. When the
// thread writes a black frame in the place of the lines written, or the channel was set
// idle and holding again meanwhile, the frame is checked again and written from its
// start. On a failure nothing of the frame is committed. Write then looks for a frame
// again.
//
static DtapiResult WriteOneFrame(DtSdiTx* Sdi, const uint8_t* Frame, int FrameSize,
                                 uint64_t Deadline)
{
    const uint8_t* Data = Frame;
    size_t BytesLeft = 0;
    DtapiResult Result = DTAPI_OK;

    while (Result == DTAPI_OK && Sdi->WriteStage != DT_SDITX_STAGE_PADDING)
    {
        if (*Sdi->Tx.Port.WaitingDetaches > 0)
            Result = DTAPI_E_CANCELLED;
        else if (Sdi->Tx.TxControl == DTAPI_TXCTRL_IDLE)
            Result = DTAPI_E_IDLE;
        else if (Sdi->WriteStage == DT_SDITX_STAGE_SEARCH)
        {
            Result = CheckFrame(Sdi, Frame, FrameSize);
            Data = Frame;
            BytesLeft = (size_t)FrameSize;
            if (Result == DTAPI_OK)
                StartFrame(Sdi, 0);
        }
        else
        {
            if (EncodeLineBatch(Sdi, &Data, &BytesLeft) == 0)
                Result = EncodeOneLine(Sdi, &Data, &BytesLeft, Deadline);
            OsMutex_Unlock(Sdi->Tx.Port.Lock);
            OsMutex_Lock(Sdi->Tx.Port.Lock);
        }
    }

    if (Result == DTAPI_OK)
    {
        Result = CommitFrame(Sdi);
        if (Result == DTAPI_OK)
            Sdi->FifoUfl = false;
    }
    ForgetPartialFrame(Sdi);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PadToPcieWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Commits zero bytes up to the next whole data word. The card reads the buffer in whole
// words, so without them the last bytes of the last frame would wait for more data.
//
static void PadToPcieWord(DtSdiTx* Sdi)
{
    static const uint8_t Zeros[64] = {0};
    size_t Pad = (Sdi->PcieWordBytes - Sdi->CommittedBytes % Sdi->PcieWordBytes) %
                 Sdi->PcieWordBytes;
    size_t Offset = WrapOffset(Sdi, Sdi->WriteOffset + Pad);
    size_t Load;

    if (Pad == 0 || Pad > sizeof(Zeros) || DmaBufferLoad(Sdi, &Load) != DTAPI_OK ||
        Sdi->MaxLoad - Load < Pad)
    {
        return;
    }
    CopyIntoBuffer(Sdi, Sdi->WriteOffset, Zeros, Pad);
    if (DtPcieCmd_CdmacSetTxWriteOffset(DrvOf(Sdi), Sdi->Cdmac, (uint32_t)Offset) ==
        DTAPI_OK)
    {
        Sdi->WriteOffset = Offset;
        Sdi->CommittedBytes += Pad;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindDriverBlocks -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The objects of AF_ASISDITX and AF_DMA the channel drives, and whether the driver is new
// enough for each: always the DMA controller, the burst FIFO, the formatter, the encoder
// and the PHY; the demultiplexer and its two switches on a port with DT_CAP_QUADLINK; and
// the switch from a quad-link master when the port has it.
//
static DtapiResult FindDriverBlocks(DtSdiTx* Sdi)
{
    typedef struct
    {
        DtFuncInstance* Instance;
        bool IsDriverFunction;
        int Type;
        const char* Role;
        DtDrvObject* Object;
        bool Needed;
    } DriverBlockSpec;
    const bool HasQuadLink = (Sdi->Tx.Port.Caps & DT_CAP_QUADLINK) != 0;
    const DriverBlockSpec Objects[] = {
        {&Sdi->DmaFunction, false, DT_BLOCK_TYPE_CDMAC, "", &Sdi->Cdmac, true},
        {&Sdi->DmaFunction, false, DT_BLOCK_TYPE_BURSTFIFO, "", &Sdi->BurstFifo, true},
        {&Sdi->TxFunction, false, DT_BLOCK_TYPE_SDITXF, "", &Sdi->Txf, true},
        {&Sdi->TxFunction, false, DT_BLOCK_TYPE_SWITCH, "FROM_QUAD_LINK_MASTER",
         &Sdi->QuadLinkMasterSwitch, false},
        {&Sdi->TxFunction, false, DT_BLOCK_TYPE_SDITXP, "", &Sdi->Txp, true},
        {&Sdi->TxFunction, true, DT_FUNC_TYPE_SDITXPHY, "", &Sdi->Phy, true},
        {&Sdi->TxFunction, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_IN",
         &Sdi->DemuxInSwitch, HasQuadLink},
        {&Sdi->TxFunction, false, DT_BLOCK_TYPE_SDIDMX12G, "", &Sdi->Dmx, HasQuadLink},
        {&Sdi->TxFunction, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_OUT",
         &Sdi->DemuxOutSwitch, HasQuadLink},
    };

    DtapiResult Result = DtFunc_Find(DrvOf(Sdi), Sdi->Tx.Port.Port - 1, "AF_ASISDITX", "",
                                     &Sdi->TxFunction);
    if (Result == DTAPI_OK)
        Result = DtFunc_Find(DrvOf(Sdi), Sdi->Tx.Port.Port - 1, "AF_DMA", "",
                             &Sdi->DmaFunction);
    for (size_t i = 0; i < sizeof(Objects) / sizeof(Objects[0]) && Result == DTAPI_OK;
         i++)
    {
        const bool Optional = Objects[i].Object == &Sdi->QuadLinkMasterSwitch;
        if (!Objects[i].Needed && !Optional)
            continue;
        const DtFuncObject* Object =
            DtFunc_Get(Objects[i].Instance, Objects[i].IsDriverFunction, Objects[i].Type,
                       Objects[i].Role);

        if (Object == NULL && !Optional)
            Result = DTAPI_E_NOT_FOUND;
        else if (Object != NULL)
        {
            *Objects[i].Object = Object->Ref;
            Result =
                DtFunc_CheckDriverVersion(&Sdi->Tx.Port.Device->DriverVersion,
                                          Objects[i].IsDriverFunction, Objects[i].Type);
        }
    }
    Sdi->HasQuadLink = HasQuadLink;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Lets go of the buffer and the exclusive access, ignoring failures. Releasing the
// exclusive access of an object the handle does not hold changes nothing.
//
static void Release(DtTx* Tx)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    StopSignalKeeper(Sdi);
    FreeStandardBuffers(Sdi);
    DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->TxFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->DmaFunction, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    DtFunc_Release(&Sdi->TxFunction);
    DtFunc_Release(&Sdi->DmaFunction);
    OsEvent_Destroy(Sdi->RoomEvent);
    DtJobRunner_Free(&Sdi->JobRunner);
    DtAlloc_Free(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult SetTxControl(DtTx* Tx, int TxControl)
{
    return ChangeTxControl((DtSdiTx*)Tx, TxControl);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Idle, which forgets what the buffer held, and every flag cleared.
//
static DtapiResult ClearFifo(DtTx* Tx)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;
    DtapiResult Result = ChangeTxControl(Sdi, DTAPI_TXCTRL_IDLE);

    if (Result != DTAPI_OK)
        return Result;
    Sdi->FifoUfl = Sdi->FifoUflLatched = false;
    Sdi->DmaUfl = Sdi->DmaUflLatched = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The complete frames that have not started going out, and what a write has taken of
// the next, in raw bytes.
//
static DtapiResult GetFifoLoad(DtTx* Tx, int* FifoLoad)
{
    const DtSdiTx* Sdi = (const DtSdiTx*)Tx;

    *FifoLoad = 0;
    if (Tx->TxControl != DTAPI_TXCTRL_IDLE)
    {
        int Frames = UnsentFrames(Sdi) - (Sdi->FirstEventSeen ? 1 : 0);
        size_t Size = Sdi->MaxLoad / Sdi->CodedFrameSize * Sdi->RawFrameSize;
        size_t Bytes = (size_t)(Frames > 0 ? Frames : 0) * Sdi->RawFrameSize;

        if (Sdi->WriteStage != DT_SDITX_STAGE_SEARCH)
            Bytes += Sdi->RawFrameSize - Sdi->FrameBytesLeft;
        *FifoLoad = (int)(Bytes < Size ? Bytes : Size);
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FifoSizeInRawBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The load GetFifoLoad reports for a full buffer. A channel without a buffer gives the
// typical FIFO size, and the maximum size for GetMaxFifoSize.
//
static int FifoSizeInRawBytes(const DtSdiTx* Sdi, int SizeWithoutBuffer)
{
    if (!Sdi->BufferRegistered)
        return SizeWithoutBuffer;
    return (int)(Sdi->MaxLoad / Sdi->CodedFrameSize *
                 DtSdiFrame_RawSize(&Sdi->FrameLayout, Sdi->BitsPerSymbol));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetFifoSize(DtTx* Tx, int* FifoSize)
{
    *FifoSize = FifoSizeInRawBytes((const DtSdiTx*)Tx, DT_SDITX_FIFO_SIZE_TYP);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetMaxFifoSize(DtTx* Tx, int* MaxFifoSize)
{
    *MaxFifoSize = FifoSizeInRawBytes((const DtSdiTx*)Tx, DT_SDITX_FIFO_SIZE_MAX);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetFlags(DtTx* Tx, int* Status, int* Latched)
{
    const DtSdiTx* Sdi = (const DtSdiTx*)Tx;

    *Status =
        (Sdi->FifoUfl ? DTAPI_TX_FIFO_UFL : 0) | (Sdi->DmaUfl ? DTAPI_TX_DMA_UFL : 0);
    *Latched = (Sdi->FifoUflLatched ? DTAPI_TX_FIFO_UFL : 0) |
               (Sdi->DmaUflLatched ? DTAPI_TX_DMA_UFL : 0);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Once DtOutpChannel.c has checked the mode: while idle, the full frame only. The mode is
// kept for the side's life, also across a change of SDI standard.
//
static DtapiResult SetTxMode(DtTx* Tx, int TxMode, int StuffMode)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;
    DtapiResult Result = DTAPI_OK;

    (void)StuffMode;
    if (Tx->TxControl != DTAPI_TXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else if ((TxMode & DTAPI_TXMODE_SDI_MASK) != DTAPI_TXMODE_SDI_FULL)
        Result = DTAPI_E_INVALID_MODE;
    else
    {
        Tx->TxMode = TxMode;
        Sdi->BitsPerSymbol = (TxMode & DTAPI_TXMODE_SDI_10B) != 0   ? 10
                             : (TxMode & DTAPI_TXMODE_SDI_16B) != 0 ? 16
                                                                    : 8;
        if (Sdi->BufferRegistered)
            Sdi->RawFrameSize = DtSdiFrame_RawSize(&Sdi->FrameLayout, Sdi->BitsPerSymbol);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A new standard sets the channel up for it; the transmit mode is kept.
//
static DtapiResult ApplyIoConfig(DtTx* Tx, const DtIoConfig* Config,
                                 DtapiResult SetResult)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    if (SetResult != DTAPI_OK || Config->Group != DTAPI_IOCONFIG_IOSTD)
        return SetResult;
    Sdi->IoStdValue = Config->Value;
    Sdi->IoStdSubValue = Config->SubValue;
    return ConfigureChannel(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Clears the underflow flags named in Latched.
//
static DtapiResult ClearFlags(DtTx* Tx, int Latched)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    if ((Latched & DTAPI_TX_FIFO_UFL) != 0)
        Sdi->FifoUfl = Sdi->FifoUflLatched = false;
    if ((Latched & DTAPI_TX_DMA_UFL) != 0)
        Sdi->DmaUfl = Sdi->DmaUflLatched = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SDI takes the normal polarity only.
//
static DtapiResult SetTxPolarity(DtTx* Tx, int TxPolarity)
{
    (void)Tx;
    return TxPolarity == DTAPI_TXPOL_NORMAL ? DTAPI_OK : DTAPI_E_INVALID_ARG;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame a Write began and did not finish, or bytes it left that are not yet in a
// frame, refuse the frame with DTAPI_E_INCOMP_FRAME, so that it goes into the buffer
// whole, directly after the frames before it.
//
static DtapiResult WriteFrame(DtTx* Tx, const uint8_t* Frame, int FrameSize,
                              uint64_t Deadline)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    if (Sdi->WriteStage != DT_SDITX_STAGE_SEARCH || Sdi->PartialLineBytes > 0)
        return DTAPI_E_INCOMP_FRAME;
    return WriteOneFrame(Sdi, Frame, FrameSize, Deadline);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WakeWaitingWrite -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WakeWaitingWrite(DtTx* Tx)
{
    OsEvent_Set(((DtSdiTx*)Tx)->RoomEvent);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitUntilSent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// With the thread stopped, the detach is the one that waits for format events: until
// the last frame written has gone out and a wait finds nothing more, or no event came
// for a second. The card sends a frame only when data follows it, so a black frame
// follows the last frame written, as soon as the buffer has room for it, and the part
// of a frame a write left is dropped.
//
static void WaitUntilSent(DtTx* Tx)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;
    uint64_t Since = OsTime_MonotonicMs();
    int WaitMs = Sdi->QuarterFrameMs < 500 ? 2 * Sdi->QuarterFrameMs : 1000;
    bool BlackToCome = Sdi->NextFrameId != 0;

    StopSignalKeeper(Sdi);
    ForgetPartialFrame(Sdi);
    while (Sdi->NextFrameId != 0)
    {
        size_t Load;
        if (BlackToCome && DmaBufferLoad(Sdi, &Load) == DTAPI_OK &&
            Sdi->MaxLoad - Load >= Sdi->CodedFrameSize)
        {
            InsertBlack(Sdi, Load);
            PadToPcieWord(Sdi);
            BlackToCome = false;
        }

        OsDrv* Drv = DrvOf(Sdi);
        DtDrvObject Txf = Sdi->Txf;

        OsMutex_Unlock(Tx->Port.Lock);
        DtSdiTxFEvent Event;
        DtapiResult Result = DtPcieCmd_SdiTxFWaitForFmtEvent(Drv, Txf, WaitMs, &Event);
        OsMutex_Lock(Tx->Port.Lock);

        if (Result == DTAPI_OK)
        {
            Sdi->FirstEventSeen = true;
            Sdi->SendingFrameId = Event.FrameId;
            Since = OsTime_MonotonicMs();
        }
        else if (Result != DTAPI_E_TIMEOUT)
            break;
        else if ((Sdi->FirstEventSeen && !BlackToCome && UnsentFrames(Sdi) <= 1) ||
                 OsTime_MonotonicMs() - Since >= DT_SDITX_SENT_STALL_MS)
        {
            break;
        }
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetWorkerPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A side with no standard yet keeps the pool for ConfigureChannel, which sets the job
// runner up.
//
static DtapiResult SetWorkerPool(DtTx* Tx, DtWorkerPool* Pool, int NumThreads)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    Sdi->WorkerPool = Pool;
    Sdi->WorkerThreads = NumThreads;
    if (Sdi->FrameLayout.VidStd == DTAPI_VIDSTD_UNKNOWN)
        return DtJobRunner_SetPool(&Sdi->JobRunner, NULL, 0);
    return ConfigureJobRunner(Sdi);
}

static const DtTxBackend g_SdiTxBackend = {
    .Release = Release,
    .SetWorkerPool = SetWorkerPool,
    .SetTxControl = SetTxControl,
    .ClearFifo = ClearFifo,
    .GetFifoLoad = GetFifoLoad,
    .GetFifoSize = GetFifoSize,
    .GetMaxFifoSize = GetMaxFifoSize,
    .GetFlags = GetFlags,
    .SetTxMode = SetTxMode,
    .ClearFlags = ClearFlags,
    .ApplyIoConfig = ApplyIoConfig,
    .SetTxPolarity = SetTxPolarity,
    .Write = Write,
    .WriteFrame = WriteFrame,
    .WakeWaitingWrite = WakeWaitingWrite,
    .WaitUntilSent = WaitUntilSent,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiTx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiTx_Attach(const DtTxAttachedPort* Port, const DtIoConfig* IoStd,
                           DtTx** Tx)
{
    *Tx = NULL;
    DtSdiTx* Sdi = (DtSdiTx*)DtAlloc_Malloc(sizeof(DtSdiTx));
    if (Sdi == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Sdi, 0, sizeof(*Sdi));
    Sdi->Tx.Backend = &g_SdiTxBackend;
    Sdi->Tx.Port = *Port;
    Sdi->FrameLayout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    DtJobRunner_Init(&Sdi->JobRunner);
    DtVec_Init(&Sdi->TxFunction.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Sdi->DmaFunction.Objects, sizeof(DtFuncObject));
    Sdi->RoomEvent = OsEvent_Create();
    if (Sdi->RoomEvent == NULL)
    {
        DtAlloc_Free(Sdi);
        return DTAPI_E_OUT_OF_MEM;
    }
    Sdi->IoStdValue = IoStd->Value;
    Sdi->IoStdSubValue = IoStd->SubValue;

    // The default transmit mode and cleared flags; the I/O standard is applied again.
    Sdi->Tx.TxMode = DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B;
    Sdi->BitsPerSymbol = 10;
    Sdi->Tx.TxControl = DTAPI_TXCTRL_IDLE;
    DtapiResult Result = DtPcieCmd_SetIoConfig(DrvOf(Sdi), IoStd);

    // Exclusive access to the transmitter and the DMA, all blocks idle, the encoder's
    // corrections on, and the channel set up for the standard.
    if (Result == DTAPI_OK)
        Result = FindDriverBlocks(Sdi);
    if (Result == DTAPI_OK)
        Result = DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->TxFunction,
                                   DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
        Result = DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->DmaFunction,
                                   DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
        Result = BlocksToIdle(Sdi);

    // The data comes from the channel, not from a quad-link master, where the port has
    // that switch.
    if (Result == DTAPI_OK && Sdi->QuadLinkMasterSwitch.Uuid != 0)
        Result = DtPcieCmd_SwitchSetPosition(DrvOf(Sdi), Sdi->QuadLinkMasterSwitch, 0, 0);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_SdiTxPSetGenerationMode(DrvOf(Sdi), Sdi->Txp, true, true, true);
    if (Result == DTAPI_OK)
        Result = ConfigureChannel(Sdi);
    if (Result != DTAPI_OK)
    {
        Release(&Sdi->Tx);
        return Result;
    }
    *Tx = &Sdi->Tx;
    return DTAPI_OK;
}

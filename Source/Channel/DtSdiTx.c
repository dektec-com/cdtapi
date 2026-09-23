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
#include "Device/DtFunc.h"      // Finding the transmit blocks.
#include "DtPcieAbi.h"          // Operational modes and types.
#include "DtSdiTx.h"            // Interface being implemented.
#include "OAL/OsDmaBuffer.h"    // The DMA buffer.
#include "OAL/OsThread.h"       // The thread, its event, sleeping.
#include "Video/DtFrameProps.h" // The frame rate.
#include "Video/DtSdiFrame.h"   // The buffer's format and the raw frame.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The typical and maximum FIFO size, reported for a port without a buffer.
#define DT_FIFO_SIZE_TYP (48 * 1024 * 1024)
#define DT_FIFO_SIZE_MAX (64 * 1024 * 1024)

// The bounds on the DMA buffer, the frames it is sized for, and the fewest it must hold.
#define DT_BUF_MIN (8 * 1024 * 1024)
#define DT_BUF_MAX (256 * 1024 * 1024)
#define DT_BUF_FRAMES 5
#define DT_BUF_MIN_FRAMES 2

// The format events per frame the channel asks for.
#define DT_FMT_EVENTS_PER_FRAME 4

// A detach that waits until everything is sent gives up after a second without a format
// event.
#define DT_SENT_STALL_MS 1000

// The burst FIFO's load is read at most five times for 75 % full.
#define DT_BURST_POLLS 5

// The format event of the first frame of a run from which the thread may write a black
// frame after it: the application has until then, half a frame, to write the second
// frame. The last quarter of a frame goes out only when data follows it.
#define DT_FIRST_BLACK_SEQ 2

// The PHY's underflow flag is read every so many format events.
#define DT_PHY_POLL_EVENTS 50

// The largest header with its padding: 20 bytes padded to 512 bits.
#define DT_MAX_TX_HEADER 64

// The search for the start of line 1 of an SD frame.
#define DT_SD_IN_SYNC 0
#define DT_SD_FIND_FIELD2 1
#define DT_SD_FIND_FRAME_START 2

// Where the stream is: looking for a frame, in its lines, or in the padding after them.
#define DT_STAGE_SEARCH 0
#define DT_STAGE_LINES 1
#define DT_STAGE_PADDING 2

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct DtSdiTx
{
    DtTx Base;

    // The transmit blocks, held exclusively while attached.
    DtFuncInstance AfTx, AfDma;
    DtDrvObject Cdmac, Burst, Txf, Txp, Phy;

    // The demultiplexer and its switches of a port with DT_CAP_QUADLINK, and the switch
    // from a quad-link master where the port has it; their UUIDs are 0 otherwise.
    bool QuadLink;
    DtDrvObject SwitchIn, SwitchOut, Dmx;
    DtDrvObject FromMaster;

    int IoStdValue; // The port's I/O standard
    int IoStdSubValue;
    int SymbolBits; // 8, 10 or 16, from the transmit mode

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
    uint8_t* Black;    // The coded lines of a black frame, line headers included
    uint8_t* LineBuf;  // A raw line's coded lines when they run across the end
    uint8_t* RawBuf;   // The raw bytes of a line not yet complete
    size_t RawBufSize;
    uint16_t* Scratch; // The working symbols of a 4K line

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
    bool Settled;  // Black frames may follow the first frame: see DT_FIRST_BLACK_SEQ
    int SendingId; // The frame ID of the last format event
} DtSdiTx;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrvOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static OsDrv* DrvOf(const DtSdiTx* Sdi)
{
    return Sdi->Base.Port.Device->Drv;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wrap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t Wrap(const DtSdiTx* Sdi, size_t Offset)
{
    return Offset % Sdi->Buf.Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies Size bytes into the buffer from Offset on, across its end.
//
static void PutAt(DtSdiTx* Sdi, size_t Offset, const uint8_t* Data, size_t Size)
{
    size_t First = Sdi->Buf.Size - Offset;

    if (First > Size)
        First = Size;
    memcpy(Sdi->Buf.Data + Offset, Data, First);
    memcpy(Sdi->Buf.Data, Data + First, Size - First);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyWithin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies Size bytes of the buffer from offset From to offset To, both across its end.
// The two ranges do not overlap.
//
static void CopyWithin(DtSdiTx* Sdi, size_t From, size_t To, size_t Size)
{
    while (Size > 0)
    {
        size_t Chunk = Size;

        if (Chunk > Sdi->Buf.Size - From)
            Chunk = Sdi->Buf.Size - From;
        if (Chunk > Sdi->Buf.Size - To)
            Chunk = Sdi->Buf.Size - To;
        memcpy(Sdi->Buf.Data + To, Sdi->Buf.Data + From, Chunk);
        From = Wrap(Sdi, From + Chunk);
        To = Wrap(Sdi, To + Chunk);
        Size -= Chunk;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the header of a frame with FrameId, padded with zeros, at Offset.
//
static void PutHeader(DtSdiTx* Sdi, size_t Offset, int FrameId)
{
    uint8_t Bytes[DT_MAX_TX_HEADER];

    memset(Bytes, 0, sizeof(Bytes));
    DtSdiFrameTxHeader Header;
    DtSdiFrame_TxHeaderInit(&Sdi->Layout, FrameId, &Header);
    DtSdiFrame_EncodeTxHeader(&Header, Bytes);
    PutAt(Sdi, Offset, Bytes, (size_t)Sdi->Layout.TxHeaderBytes);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes of committed frames the card has not yet taken. Right after the DMA
// controller is set running a DTA-2178 reports a read offset of an earlier run for a
// while; the load is therefore never more than what was committed since.
//
static DtapiResult ReadLoad(DtSdiTx* Sdi, size_t* Load)
{
    uint32_t ReadOffset = 0;
    DtapiResult Result =
        DtPcieCmd_CdmacGetTxReadOffset(DrvOf(Sdi), Sdi->Cdmac, &ReadOffset);

    *Load = 0;
    if (Result != DTAPI_OK)
        return Result;
    if (ReadOffset >= Sdi->Buf.Size)
        return DTAPI_E_DEV_DRIVER;

    *Load = Wrap(Sdi, Sdi->WriteOffset + Sdi->Buf.Size - ReadOffset);
    if ((uint64_t)*Load > Sdi->Committed)
        *Load = (size_t)Sdi->Committed;
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
    return (Sdi->NextFrameId - (Sdi->Started ? Sdi->SendingId : 0)) & 0xFFFF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Forgets the frame being written, and starts looking for line 1 again.
//
static void ResetFrame(DtSdiTx* Sdi)
{
    Sdi->Stage = DT_STAGE_SEARCH;
    Sdi->SdSync = DT_SD_IN_SYNC;
    Sdi->Reserved = false;
    Sdi->LinesDone = 0;
    Sdi->Phase = 0;
    Sdi->RawHave = 0;
    Sdi->FrameBytesLeft = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CommitFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frame at the write offset is complete: the card may take it.
//
static DtapiResult CommitFrame(DtSdiTx* Sdi)
{
    size_t Offset = Wrap(Sdi, Sdi->WriteOffset + Sdi->CodedSize);
    DtapiResult Result =
        DtPcieCmd_CdmacSetTxWriteOffset(DrvOf(Sdi), Sdi->Cdmac, (uint32_t)Offset);

    if (Result != DTAPI_OK)
        return Result;
    Sdi->WriteOffset = Offset;
    Sdi->Committed += Sdi->CodedSize;
    Sdi->NextFrameId = (Sdi->NextFrameId + 1) & 0xFFFF;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InsertBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Commits a black frame at the write offset. The part of a frame a write has put there
// moves one frame further, with the next frame ID; when the buffer has no room for both,
// the black frame takes its place and the write looks for the next frame.
//
static DtapiResult InsertBlack(DtSdiTx* Sdi, size_t Load)
{
    const DtSdiFrameLayout* Layout = &Sdi->Layout;
    size_t Coded = Sdi->CodedSize;
    size_t Free = Sdi->MaxLoad - Load;
    size_t Partial = 0;

    if (Free < Coded)
        return DTAPI_OK;
    if (Sdi->Reserved)
    {
        Partial = (size_t)Layout->TxHeaderBytes +
                  (size_t)Sdi->LinesDone * DtSdiFrame_TxLineBytes(Layout);
        if (Free < 2 * Coded)
        {
            ResetFrame(Sdi);
            Partial = 0;
        }
    }

    if (Partial > 0)
    {
        CopyWithin(Sdi, Sdi->WriteOffset, Wrap(Sdi, Sdi->WriteOffset + Coded), Partial);
        PutHeader(Sdi, Wrap(Sdi, Sdi->WriteOffset + Coded),
                  (Sdi->NextFrameId + 1) & 0xFFFF);
    }
    PutHeader(Sdi, Sdi->WriteOffset, Sdi->NextFrameId);
    PutAt(Sdi, Wrap(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderBytes), Sdi->Black,
          (size_t)Layout->CodedLines * (size_t)Layout->TxStride);

    DtapiResult Result = CommitFrame(Sdi);
    if (Result == DTAPI_OK)
        Sdi->FifoUfl = Sdi->FifoUflLatched = true;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Keeper -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// While sending: waits for the formatter's format events, the only waiter for them,
// takes their underflow flag and, now and then, the PHY's, and writes a black frame when
// the frame going out is the last one written. After the first frame of a run it waits
// with that until the frame's event DT_FIRST_BLACK_SEQ or a wait that times out, so that
// an application that wrote only one frame before sending has time to write the next.
// Wakes a write waiting for room after each event.
//
static void Keeper(void* Context)
{
    DtSdiTx* Sdi = (DtSdiTx*)Context;

    OsThread_RaisePriority();
    OsMutex_Lock(Sdi->Base.Port.Lock);
    OsDrv* Drv = DrvOf(Sdi);
    DtDrvObject Txf = Sdi->Txf;
    int WaitMs = Sdi->QuarterMs < 1000 ? Sdi->QuarterMs : 1000;
    OsMutex_Unlock(Sdi->Base.Port.Lock);

    for (;;)
    {
        DtSdiTxFEvent Event;
        bool Failed = false;

        DtapiResult Result = DtPcieCmd_SdiTxFWaitForFmtEvent(Drv, Txf, WaitMs, &Event);

        OsMutex_Lock(Sdi->Base.Port.Lock);
        if (Sdi->StopThread)
        {
            OsMutex_Unlock(Sdi->Base.Port.Lock);
            return;
        }
        if (Result == DTAPI_OK)
        {
            Sdi->Started = true;
            Sdi->SendingId = Event.FrameId;
            if (Event.FrameId != 0 || Event.SeqNumber >= DT_FIRST_BLACK_SEQ)
                Sdi->Settled = true;
            if (Event.Underflow)
                Sdi->FifoUfl = Sdi->FifoUflLatched = true;
        }
        else if (Sdi->Started)
            Sdi->Settled = true;
        if (Result != DTAPI_OK && Result != DTAPI_E_TIMEOUT)
            Failed = true;

        if (Result != DTAPI_OK || ++Sdi->Events % DT_PHY_POLL_EVENTS == 0)
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
        if (Sdi->Settled && UnsentFrames(Sdi) <= 1 && ReadLoad(Sdi, &Load) == DTAPI_OK)
            InsertBlack(Sdi, Load);
        OsEvent_Set(Sdi->Room);
        OsMutex_Unlock(Sdi->Base.Port.Lock);

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
static void StopKeeper(DtSdiTx* Sdi)
{
    OsThread* Thread = Sdi->Thread;

    if (Thread == NULL)
        return;
    Sdi->StopThread = true;
    Sdi->Thread = NULL;
    OsMutex_Unlock(Sdi->Base.Port.Lock);
    OsThread_Join(Thread);
    OsMutex_Lock(Sdi->Base.Port.Lock);
    Sdi->StopThread = false;
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
    if (Sdi->FromMaster.Uuid != 0)
        Results[2] =
            DtPcieCmd_SwitchSetOpMode(Drv, Sdi->FromMaster, DT_BLOCK_OPMODE_IDLE);
    if (Sdi->QuadLink)
    {
        Results[3] = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->SwitchOut, DT_BLOCK_OPMODE_IDLE);
        Results[4] = DtPcieCmd_SdiDmx12GSetOpMode(Drv, Sdi->Dmx, DT_BLOCK_OPMODE_IDLE);
        Results[5] = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->SwitchIn, DT_BLOCK_OPMODE_IDLE);
    }
    Results[6] = DtPcieCmd_SdiTxFSetOpMode(Drv, Sdi->Txf, DT_BLOCK_OPMODE_IDLE);
    Results[7] = DtPcieCmd_BurstFifoSetOpMode(Drv, Sdi->Burst, DT_BLOCK_OPMODE_IDLE);
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

    if (!Sdi->Registered)
        return DTAPI_E_CONFIG_RAW_SDI;

    DtapiResult Result = DtPcieCmd_CdmacIssueChannelFlush(Drv, Sdi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetTxWriteOffset(Drv, Sdi->Cdmac, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacSetOpMode(Drv, Sdi->Cdmac, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoSetOpMode(Drv, Sdi->Burst, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxFSetOpMode(Drv, Sdi->Txf, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK && Sdi->FromMaster.Uuid != 0)
        Result = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->FromMaster, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK && Sdi->QuadLink)
        Result = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->SwitchIn, DT_BLOCK_OPMODE_RUN);
    if (Result == DTAPI_OK && Sdi->QuadLink)
        Result = DtPcieCmd_SdiDmx12GSetOpMode(Drv, Sdi->Dmx, DT_BLOCK_OPMODE_IDLE);
    if (Result == DTAPI_OK && Sdi->QuadLink)
        Result = DtPcieCmd_SwitchSetOpMode(Drv, Sdi->SwitchOut, DT_BLOCK_OPMODE_RUN);
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
    Sdi->Committed = 0;
    Sdi->NextFrameId = 0;
    Sdi->Started = false;
    Sdi->Settled = false;
    Sdi->SendingId = 0;
    ResetFrame(Sdi);
    Sdi->Base.TxControl = DTAPI_TXCTRL_HOLD;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HoldToSend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Refused without a frame that has not gone out. Then the burst FIFO is given five reads
// to fill to 75 %, its and the reorder buffer's statistics are cleared, and the PHY runs.
// The PHY's underflow flag of an earlier run is cleared too.
//
static DtapiResult HoldToSend(DtSdiTx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    DtBurstFifoStatus Status = {0};

    if (UnsentFrames(Sdi) < 1)
        return DTAPI_E_INSUF_LOAD;
    if (Sdi->SymbolBits == 8)
        return DTAPI_E_CONFIG_RAW_SDI;

    DtapiResult Result;
    for (int Poll = 0; Poll < DT_BURST_POLLS; Poll++)
    {
        Result = DtPcieCmd_BurstFifoGetStatus(Drv, Sdi->Burst, &Status);
        if (Result != DTAPI_OK)
            return Result;
        if (Status.CurLoad >= Sdi->BurstFifoSize * 3 / 4)
            break;
    }

    Result = DtPcieCmd_CdmacClearReorderBufMinMax(Drv, Sdi->Cdmac);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoClearMax(Drv, Sdi->Burst, true, true);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhyClearUnderflowFlag(Drv, Sdi->Phy);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetOpMode(Drv, Sdi->Phy, DT_FUNC_OPMODE_RUN);
    if (Result != DTAPI_OK)
        return Result;

    Sdi->StopThread = false;
    Sdi->Events = 0;
    Sdi->Thread = OsThread_Start(Keeper, Sdi);
    if (Sdi->Thread == NULL)
    {
        DtPcieCmd_SdiTxPhySetOpMode(Drv, Sdi->Phy, DT_FUNC_OPMODE_STANDBY);
        return DTAPI_E_OUT_OF_MEM;
    }
    Sdi->Base.TxControl = DTAPI_TXCTRL_SEND;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendToHold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// After the thread has stopped: the PHY waits, the underflow status is cleared and the
// latched flag kept.
//
static DtapiResult SendToHold(DtSdiTx* Sdi)
{
    StopKeeper(Sdi);
    DtapiResult Result =
        DtPcieCmd_SdiTxPhySetOpMode(DrvOf(Sdi), Sdi->Phy, DT_FUNC_OPMODE_STANDBY);
    Sdi->FifoUfl = false;
    Sdi->Base.TxControl = DTAPI_TXCTRL_HOLD;
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

    ResetFrame(Sdi);
    Sdi->Base.TxControl = DTAPI_TXCTRL_IDLE;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// IDLE to SEND goes through HOLD, and SEND to IDLE too.
//
static DtapiResult SetTxControl(DtSdiTx* Sdi, int TxControl)
{
    DtapiResult Result = DTAPI_OK;

    if (Sdi->Base.TxControl == TxControl)
        return DTAPI_OK;
    if (TxControl != DTAPI_TXCTRL_IDLE && TxControl != DTAPI_TXCTRL_HOLD &&
        TxControl != DTAPI_TXCTRL_SEND)
    {
        return DTAPI_E_INVALID_ARG;
    }

    if (Sdi->Base.TxControl == DTAPI_TXCTRL_IDLE)
        Result = IdleToHold(Sdi);
    else if (Sdi->Base.TxControl == DTAPI_TXCTRL_SEND)
        Result = SendToHold(Sdi);
    if (Result != DTAPI_OK || TxControl == DTAPI_TXCTRL_HOLD)
        return Result;

    if (TxControl == DTAPI_TXCTRL_SEND)
        return HoldToSend(Sdi);
    return HoldToIdle(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Idle, which forgets what the buffer held, and every flag cleared.
//
static DtapiResult ResetFifo(DtSdiTx* Sdi)
{
    DtapiResult Result = SetTxControl(Sdi, DTAPI_TXCTRL_IDLE);

    if (Result != DTAPI_OK)
        return Result;
    Sdi->FifoUfl = Sdi->FifoUflLatched = false;
    Sdi->DmaUfl = Sdi->DmaUflLatched = false;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BufferSizeFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Room for five frames plus the raw frames a 48 MB FIFO holds, rounded up to a power of
// two within the buffer's bounds, and to whole prefetch units of pages.
//
static size_t BufferSizeFor(const DtSdiTx* Sdi, int PrefetchSize)
{
    size_t Raw = DtSdiFrame_RawSize(&Sdi->Layout, 10);
    size_t Wanted = (DT_BUF_FRAMES + DT_FIFO_SIZE_TYP / Raw) * Sdi->CodedSize;
    size_t Unit = 4096 * (size_t)(PrefetchSize > 0 ? PrefetchSize : 1);
    size_t Size = DT_BUF_MIN;

    while (Size < Wanted && Size < DT_BUF_MAX)
        Size *= 2;
    return (Size + Unit - 1) / Unit * Unit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The DMA controller lets go of the buffer, which is then freed, and the standard's
// buffers go too. Failures are ignored.
//
static void FreeBuffer(DtSdiTx* Sdi)
{
    if (Sdi->Registered)
    {
        DtPcieCmd_CdmacSetOpMode(DrvOf(Sdi), Sdi->Cdmac, DT_BLOCK_OPMODE_IDLE);
        DtPcieCmd_CdmacFreeBuffer(DrvOf(Sdi), Sdi->Cdmac);
    }
    Sdi->Registered = false;
    OsDmaBuffer_Free(&Sdi->Buf);
    DtAlloc_Free(Sdi->Black);
    DtAlloc_Free(Sdi->LineBuf);
    DtAlloc_Free(Sdi->RawBuf);
    DtAlloc_Free(Sdi->Scratch);
    Sdi->Black = Sdi->LineBuf = Sdi->RawBuf = NULL;
    Sdi->Scratch = NULL;
    Sdi->RawBufSize = 0;
    memset(&Sdi->Layout, 0, sizeof(Sdi->Layout));
    Sdi->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    Sdi->CodedSize = Sdi->RawSize = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the channel up for the port's I/O standard, while idle: format events, the stream
// alignment and the format, the start-of-frame offset, the switches around the
// demultiplexer, and a buffer for the standard, registered anew only when its size
// changes. 2160p over one 6G or 12G link is sent as raw frames (0014); a 4K standard over
// four links, or of level-B links, leaves the channel without a buffer; see IdleToHold.
//
static DtapiResult ConfigureChannel(DtSdiTx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    DtCdmacProps Props = {0};
    DtBurstFifoProps Burst = {0};
    DtSdiFrameLayout Layout = {0};
    int Alignment = 0;

    const DtVidStdInfo* Info = DtVidStd_Find(Sdi->IoStdSubValue);
    const bool OneLink = Sdi->IoStdValue == DTAPI_IOCONFIG_6GSDI ||
                         Sdi->IoStdValue == DTAPI_IOCONFIG_12GSDI;
    if ((OneLink || DtVidStd_Is4k(Sdi->IoStdSubValue)) &&
        (!OneLink || Info == NULL || Info->IsLevelB))
    {
        FreeBuffer(Sdi);
        return DTAPI_OK;
    }
    DtFrameProps Frame;
    if (!DtFrameProps_Init(&Frame, Sdi->IoStdSubValue))
    {
        FreeBuffer(Sdi);
        return DTAPI_E_INVALID_VIDSTD;
    }

    DtapiResult Result = DtPcieCmd_SdiTxFGetStreamAlignment(Drv, Sdi->Txf, &Alignment);
    if (Result == DTAPI_OK &&
        !DtSdiFrame_LayoutInit(&Layout, Sdi->IoStdSubValue, Alignment))
        Result = DTAPI_E_INTERNAL;
    if (Result == DTAPI_OK && Layout.TxHeaderBytes > DT_MAX_TX_HEADER)
        Result = DTAPI_E_INTERNAL;
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxFSetFmtEventSetting(
            Drv, Sdi->Txf,
            (Layout.CodedLines + DT_FMT_EVENTS_PER_FRAME - 1) / DT_FMT_EVENTS_PER_FRAME +
                1,
            1);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_SdiTxPhySetStartOfFrameOffset(Drv, Sdi->Phy, 0);
    if (Result == DTAPI_OK && Sdi->QuadLink)
        Result = DtPcieCmd_SwitchSetPosition(Drv, Sdi->SwitchIn, 0, 0);
    if (Result == DTAPI_OK && Sdi->QuadLink)
        Result = DtPcieCmd_SwitchSetPosition(Drv, Sdi->SwitchOut, 0, 0);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_CdmacGetProps(Drv, Sdi->Cdmac, &Props);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_BurstFifoGetProps(Drv, Sdi->Burst, &Burst);
    if (Result != DTAPI_OK)
    {
        FreeBuffer(Sdi);
        return Result;
    }

    size_t Size = Sdi->Buf.Size;
    if (Sdi->Layout.VidStd == DTAPI_VIDSTD_UNKNOWN ||
        Sdi->Layout.TxStride != Layout.TxStride ||
        Sdi->Layout.CodedLines != Layout.CodedLines ||
        Sdi->Layout.TxHeaderBytes != Layout.TxHeaderBytes)
    {
        Size = 0;
    }
    Sdi->Layout = Layout;
    Sdi->CodedSize = DtSdiFrame_TxCodedSize(&Layout);
    Sdi->RawSize = DtSdiFrame_RawSize(&Layout, Sdi->SymbolBits);

    // The standard's black frame and the buffers of a write.
    size_t Line = DtSdiFrame_RawLineBits(&Layout, 16) / 8 + 2;
    DtAlloc_Free(Sdi->Black);
    DtAlloc_Free(Sdi->LineBuf);
    DtAlloc_Free(Sdi->RawBuf);
    DtAlloc_Free(Sdi->Scratch);
    Sdi->Scratch = NULL;
    Sdi->Black =
        (uint8_t*)DtAlloc_Malloc((size_t)Layout.CodedLines * (size_t)Layout.TxStride);
    Sdi->LineBuf = (uint8_t*)DtAlloc_Malloc(DtSdiFrame_TxLineBytes(&Layout));
    Sdi->RawBuf = (uint8_t*)DtAlloc_Malloc(Line);
    Sdi->RawBufSize = Line;
    if (Layout.Is4k)
        Sdi->Scratch = (uint16_t*)DtAlloc_Malloc(DtSdiFrame_ScratchSymbols(&Layout) *
                                                 sizeof(uint16_t));
    if (Sdi->Black == NULL || Sdi->LineBuf == NULL || Sdi->RawBuf == NULL ||
        (Layout.Is4k && Sdi->Scratch == NULL) ||
        !DtSdiFrame_BlackLines(&Layout, Sdi->Black))
    {
        FreeBuffer(Sdi);
        return DTAPI_E_OUT_OF_MEM;
    }

    // A buffer of another size replaces the registered one.
    if (!Sdi->Registered || Size != BufferSizeFor(Sdi, Props.PrefetchSize))
    {
        Size = BufferSizeFor(Sdi, Props.PrefetchSize);
        if (Sdi->Registered)
        {
            DtPcieCmd_CdmacSetOpMode(Drv, Sdi->Cdmac, DT_BLOCK_OPMODE_IDLE);
            DtPcieCmd_CdmacFreeBuffer(Drv, Sdi->Cdmac);
            Sdi->Registered = false;
        }
        OsDmaBuffer_Free(&Sdi->Buf);

        Result = OsDmaBuffer_Alloc(Size, &Sdi->Buf) == 0 ? DTAPI_OK : DTAPI_E_OUT_OF_MEM;
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_CdmacAllocateBuffer(Drv, Sdi->Cdmac, DT_CDMAC_DIR_TX,
                                                   &Sdi->Buf);
        Sdi->Registered = Result == DTAPI_OK;
        if (Result == DTAPI_OK)
            Result =
                DtPcieCmd_CdmacSetTestMode(Drv, Sdi->Cdmac, DT_CDMAC_TESTMODE_NORMAL);
        if (Result != DTAPI_OK)
        {
            FreeBuffer(Sdi);
            return Result;
        }
    }

    Sdi->WordBytes = (size_t)Props.PcieDataWidth / 8;
    Sdi->MaxLoad = Sdi->Buf.Size - Sdi->WordBytes;
    if (Sdi->MaxLoad / Sdi->CodedSize < DT_BUF_MIN_FRAMES)
    {
        FreeBuffer(Sdi);
        return DTAPI_E_INTERNAL;
    }
    Sdi->BurstFifoSize = Burst.FifoSize;

    int Num;
    int Den;
    DtVidStd_Fps(Sdi->IoStdSubValue, &Num, &Den);
    Sdi->QuarterMs = Den * 1000 / Num / DT_FMT_EVENTS_PER_FRAME;
    if (Sdi->QuarterMs < 1)
        Sdi->QuarterMs = 1;
    ResetFrame(Sdi);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Symbol Index of a raw line in the transmit mode, from its first byte.
//
static uint32_t StartSymbol(const DtSdiTx* Sdi, const uint8_t* Bytes, size_t Index)
{
    if (Sdi->SymbolBits == 16)
        return ((uint32_t)Bytes[2 * Index] | (uint32_t)Bytes[2 * Index + 1] << 8) & 0x3FF;
    else if (Sdi->SymbolBits == 8)
        return (uint32_t)Bytes[Index] << 2;
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
// SD, and 48 in 2160p, whose eight streams each hold the six words.
//
static size_t StartBytes(const DtSdiTx* Sdi)
{
    size_t Symbols = Sdi->Layout.SdiRate == DT_SDIRATE_SD ? 4
                     : Sdi->Layout.Is4k                   ? 48
                                                          : 12;

    return Symbols * (size_t)Sdi->SymbolBits / 8;
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
    bool IsSd = Sdi->Layout.SdiRate == DT_SDIRATE_SD;
    const uint32_t* Symbols = !IsSd                              ? Hd
                              : Sdi->SdSync == DT_SD_FIND_FIELD2 ? SdField2
                                                                 : SdFrameStart;
    size_t Count = IsSd ? 4 : Sdi->Layout.Is4k ? 48 : 12;
    size_t i;

    for (i = 0; i < Count; i++)
    {
        uint32_t Want = Sdi->Layout.Is4k ? Eav[i / 8] : Symbols[i];

        if ((StartSymbol(Sdi, Bytes, i) & 0x3FC) != (Want & 0x3FC))
        {
            if (IsSd && Sdi->SdSync == DT_SD_IN_SYNC)
                Sdi->SdSync = DT_SD_FIND_FIELD2;
            return false;
        }
    }
    if (IsSd && Sdi->SdSync == DT_SD_FIND_FIELD2)
    {
        Sdi->SdSync = DT_SD_FIND_FRAME_START;
        return false;
    }
    if (IsSd)
        Sdi->SdSync = DT_SD_IN_SYNC;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Line 1 starts here; Held of its bytes are in RawBuf already.
//
static void StartFrame(DtSdiTx* Sdi, size_t Held)
{
    Sdi->Stage = DT_STAGE_LINES;
    Sdi->Reserved = false;
    Sdi->LinesDone = 0;
    Sdi->Phase = 0;
    Sdi->RawHave = Held;
    Sdi->FrameBytesLeft = Sdi->RawSize - Held;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFrameBoundary -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Bytes that cannot start a frame are skipped four at a time, and bytes too few to judge
// are kept for the next write.
//
static void FindFrameBoundary(DtSdiTx* Sdi, const uint8_t** Data, size_t* Left)
{
    size_t Need = StartBytes(Sdi);

    while (Sdi->RawHave > 0)
    {
        size_t Extra = Need - Sdi->RawHave;

        if (*Left < Extra)
        {
            memcpy(Sdi->RawBuf + Sdi->RawHave, *Data, *Left);
            Sdi->RawHave += *Left;
            *Data += *Left;
            *Left = 0;
            return;
        }
        memcpy(Sdi->RawBuf + Sdi->RawHave, *Data, Extra);
        if (IsFrameStart(Sdi, Sdi->RawBuf))
        {
            *Data += Extra;
            *Left -= Extra;
            StartFrame(Sdi, Need);
            return;
        }
        if (Sdi->RawHave <= 4)
            Sdi->RawHave = 0;
        else
        {
            Sdi->RawHave -= 4;
            memmove(Sdi->RawBuf, Sdi->RawBuf + 4, Sdi->RawHave);
        }
    }

    while (*Left >= Need)
    {
        if (IsFrameStart(Sdi, *Data))
        {
            StartFrame(Sdi, 0);
            return;
        }
        *Data += 4;
        *Left -= 4;
    }
    memcpy(Sdi->RawBuf, *Data, *Left);
    Sdi->RawHave = *Left;
    *Data += *Left;
    *Left = 0;
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
        if (*Sdi->Base.Port.Detachers > 0)
            return DTAPI_E_CANCELLED;
        if (Sdi->Base.TxControl == DTAPI_TXCTRL_IDLE)
            return DTAPI_E_IDLE;
        size_t Load;
        DtapiResult Result = ReadLoad(Sdi, &Load);
        if (Result != DTAPI_OK)
            return Result;
        if (Sdi->MaxLoad - Load >= Sdi->CodedSize)
            return DTAPI_OK;

        uint64_t Now = OsTime_MonotonicMs();
        if (Now >= Deadline)
            return DTAPI_E_TIMEOUT;
        int Wait = Sdi->QuarterMs;
        if (Deadline - Now < (uint64_t)Wait)
            Wait = (int)(Deadline - Now);

        OsMutex_Unlock(Sdi->Base.Port.Lock);
        if (Sdi->Thread != NULL)
            OsEvent_Wait(Sdi->Room, Wait);
        else
            OsTime_SleepMs(Wait);
        OsMutex_Lock(Sdi->Base.Port.Lock);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Codes the next line into its place in the buffer once all of its bytes are there. The
// frame's header is written first, when there is room for the whole frame, which is
// waited for until Deadline. A line whose last byte is shared with the next line leaves
// that byte for the next.
//
static DtapiResult TakeLine(DtSdiTx* Sdi, const uint8_t** Data, size_t* Left,
                            uint64_t Deadline)
{
    const DtSdiFrameLayout* Layout = &Sdi->Layout;
    size_t Bits = DtSdiFrame_RawLineBits(Layout, Sdi->SymbolBits);
    size_t Need = ((size_t)Sdi->Phase + Bits + 7) / 8;
    size_t Used = ((size_t)Sdi->Phase + Bits) / 8;

    if (!Sdi->Reserved)
    {
        DtapiResult Result = WaitForRoom(Sdi, Deadline);

        if (Result != DTAPI_OK || Sdi->Stage != DT_STAGE_LINES || Sdi->Reserved)
            return Result;
        PutHeader(Sdi, Sdi->WriteOffset, Sdi->NextFrameId);
        Sdi->Reserved = true;
    }

    const uint8_t* Src;
    if (Sdi->RawHave == 0 && *Left >= Need)
    {
        Src = *Data;
        *Data += Used;
        *Left -= Used;
        Sdi->FrameBytesLeft -= Used;
    }
    else
    {
        size_t Copy = Need - Sdi->RawHave;

        if (Copy > *Left)
            Copy = *Left;
        memcpy(Sdi->RawBuf + Sdi->RawHave, *Data, Copy);
        Sdi->RawHave += Copy;
        *Data += Copy;
        *Left -= Copy;
        Sdi->FrameBytesLeft -= Copy;
        if (Sdi->RawHave < Need)
            return DTAPI_OK;
        Src = Sdi->RawBuf;
    }

    // A raw line becomes one coded line, or the two coded lines of 4K, each of them
    // preceded by its line header.
    size_t Coded = DtSdiFrame_TxLineBytes(Layout);
    size_t Offset = Wrap(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderBytes +
                                  (size_t)Sdi->LinesDone * Coded);
    uint8_t* Dst =
        Offset + Coded <= Sdi->Buf.Size ? Sdi->Buf.Data + Offset : Sdi->LineBuf;
    if (Layout->Is4k)
    {
        DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Sdi->LinesDone, Dst);
        DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Sdi->LinesDone + 1,
                                      Dst + Layout->TxStride);
        DtSdiFrame_CodeLine4k(
            Layout, Sdi->SymbolBits, Src, Sdi->LinesDone, Dst + Layout->TxLineHeaderBytes,
            Dst + Layout->TxStride + Layout->TxLineHeaderBytes, Sdi->Scratch);
    }
    else
        DtSdiFrame_CodeLine(Layout, Sdi->SymbolBits, Src, Sdi->Phase, Dst);
    if (Dst == Sdi->LineBuf)
        PutAt(Sdi, Offset, Sdi->LineBuf, Coded);

    if (Src == Sdi->RawBuf)
    {
        Sdi->RawHave = Need - Used;
        if (Sdi->RawHave > 0)
            Sdi->RawBuf[0] = Sdi->RawBuf[Used];
    }
    Sdi->Phase = (int)(((size_t)Sdi->Phase + Bits) % 8);
    Sdi->LinesDone++;

    // What is left of the last line's byte is padding.
    if (Sdi->LinesDone == Layout->NumLines)
    {
        Sdi->RawHave = 0;
        Sdi->Stage = DT_STAGE_PADDING;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Into the buffer: every byte is taken, a frame at a time. The lock is released after
// every line, so that the thread is not kept waiting, and the state is looked at again
// after it was.
//
static DtapiResult WriteSdi(DtSdiTx* Sdi, const uint8_t* Data, size_t Left)
{
    DtapiResult Result = DTAPI_OK;

    while (Result == DTAPI_OK)
    {
        if (*Sdi->Base.Port.Detachers > 0)
            return DTAPI_E_CANCELLED;
        if (Sdi->Base.TxControl == DTAPI_TXCTRL_IDLE)
            return DTAPI_E_IDLE;

        if (Sdi->Stage == DT_STAGE_PADDING)
        {
            size_t Skip = Left < Sdi->FrameBytesLeft ? Left : Sdi->FrameBytesLeft;

            Data += Skip;
            Left -= Skip;
            Sdi->FrameBytesLeft -= Skip;
            if (Sdi->FrameBytesLeft == 0)
            {
                Result = CommitFrame(Sdi);
                if (Result == DTAPI_OK)
                    Sdi->FifoUfl = false;
                Sdi->Stage = DT_STAGE_SEARCH;
                Sdi->Reserved = false;
                Sdi->RawHave = 0;
            }
            continue;
        }
        if (Left == 0)
            break;

        if (Sdi->Stage == DT_STAGE_SEARCH)
            FindFrameBoundary(Sdi, &Data, &Left);
        else
        {
            Result = TakeLine(Sdi, &Data, &Left, DT_TX_NO_DEADLINE);
            OsMutex_Unlock(Sdi->Base.Port.Lock);
            OsMutex_Lock(Sdi->Base.Port.Lock);
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
    if ((size_t)FrameSize != Sdi->RawSize)
        return DTAPI_E_INVALID_SIZE;

    Sdi->SdSync = DT_SD_IN_SYNC;
    return IsFrameStart(Sdi, Frame) ? DTAPI_OK : DTAPI_E_INVALID_FRAME;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteWhole -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Codes a frame into the buffer line by line, as WriteSdi does, and commits it, waiting
// for room until Deadline. The lock is released after every line. When the thread
// writes a black frame in the place of the lines written, or the channel was set idle
// and holding again meanwhile, the frame is checked again and written from its start.
// On a failure nothing of the frame is committed. Write then looks for a frame again.
//
static DtapiResult WriteWhole(DtSdiTx* Sdi, const uint8_t* Frame, int FrameSize,
                              uint64_t Deadline)
{
    const uint8_t* Data = Frame;
    size_t Left = 0;
    DtapiResult Result = DTAPI_OK;

    while (Result == DTAPI_OK && Sdi->Stage != DT_STAGE_PADDING)
    {
        if (*Sdi->Base.Port.Detachers > 0)
            Result = DTAPI_E_CANCELLED;
        else if (Sdi->Base.TxControl == DTAPI_TXCTRL_IDLE)
            Result = DTAPI_E_IDLE;
        else if (Sdi->Stage == DT_STAGE_SEARCH)
        {
            Result = CheckFrame(Sdi, Frame, FrameSize);
            Data = Frame;
            Left = (size_t)FrameSize;
            if (Result == DTAPI_OK)
                StartFrame(Sdi, 0);
        }
        else
        {
            Result = TakeLine(Sdi, &Data, &Left, Deadline);
            OsMutex_Unlock(Sdi->Base.Port.Lock);
            OsMutex_Lock(Sdi->Base.Port.Lock);
        }
    }

    if (Result == DTAPI_OK)
    {
        Result = CommitFrame(Sdi);
        if (Result == DTAPI_OK)
            Sdi->FifoUfl = false;
    }
    ResetFrame(Sdi);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PadToWord -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Commits zero bytes up to the next whole data word. The card reads the buffer in whole
// words, so without them the last bytes of the last frame would wait for more data.
//
static void PadToWord(DtSdiTx* Sdi)
{
    static const uint8_t Zeros[64] = {0};
    size_t Pad = (Sdi->WordBytes - Sdi->Committed % Sdi->WordBytes) % Sdi->WordBytes;
    size_t Offset = Wrap(Sdi, Sdi->WriteOffset + Pad);
    size_t Load;

    if (Pad == 0 || Pad > sizeof(Zeros) || ReadLoad(Sdi, &Load) != DTAPI_OK ||
        Sdi->MaxLoad - Load < Pad)
    {
        return;
    }
    PutAt(Sdi, Sdi->WriteOffset, Zeros, Pad);
    if (DtPcieCmd_CdmacSetTxWriteOffset(DrvOf(Sdi), Sdi->Cdmac, (uint32_t)Offset) ==
        DTAPI_OK)
    {
        Sdi->WriteOffset = Offset;
        Sdi->Committed += Pad;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindObjects -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The objects of AF_ASISDITX and AF_DMA the channel drives, and whether the driver is new
// enough for each: always the DMA controller, the burst FIFO, the formatter, the encoder
// and the PHY; the demultiplexer and its two switches on a port with DT_CAP_QUADLINK; and
// the switch from a quad-link master when the port has it.
//
static DtapiResult FindObjects(DtSdiTx* Sdi)
{
    typedef struct
    {
        DtFuncInstance* Instance;
        bool IsDf;
        int Type;
        const char* Role;
        DtDrvObject* Ref;
        bool Needed;
    } Wanted;
    const bool QuadLink = (Sdi->Base.Port.Caps & DT_CAP_QUADLINK) != 0;
    const Wanted Objects[] = {
        {&Sdi->AfDma, false, DT_BLOCK_TYPE_CDMAC, "", &Sdi->Cdmac, true},
        {&Sdi->AfDma, false, DT_BLOCK_TYPE_BURSTFIFO, "", &Sdi->Burst, true},
        {&Sdi->AfTx, false, DT_BLOCK_TYPE_SDITXF, "", &Sdi->Txf, true},
        {&Sdi->AfTx, false, DT_BLOCK_TYPE_SWITCH, "FROM_QUAD_LINK_MASTER",
         &Sdi->FromMaster, false},
        {&Sdi->AfTx, false, DT_BLOCK_TYPE_SDITXP, "", &Sdi->Txp, true},
        {&Sdi->AfTx, true, DT_FUNC_TYPE_SDITXPHY, "", &Sdi->Phy, true},
        {&Sdi->AfTx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_IN", &Sdi->SwitchIn,
         QuadLink},
        {&Sdi->AfTx, false, DT_BLOCK_TYPE_SDIDMX12G, "", &Sdi->Dmx, QuadLink},
        {&Sdi->AfTx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_OUT", &Sdi->SwitchOut,
         QuadLink},
    };

    DtapiResult Result =
        DtFunc_Find(DrvOf(Sdi), Sdi->Base.Port.PortIndex, "AF_ASISDITX", "", &Sdi->AfTx);
    if (Result == DTAPI_OK)
        Result =
            DtFunc_Find(DrvOf(Sdi), Sdi->Base.Port.PortIndex, "AF_DMA", "", &Sdi->AfDma);
    for (size_t i = 0; i < sizeof(Objects) / sizeof(Objects[0]) && Result == DTAPI_OK;
         i++)
    {
        const bool Optional = Objects[i].Ref == &Sdi->FromMaster;
        if (!Objects[i].Needed && !Optional)
            continue;
        const DtFuncObject* Object = DtFunc_Get(Objects[i].Instance, Objects[i].IsDf,
                                                Objects[i].Type, Objects[i].Role);

        if (Object == NULL && !Optional)
            Result = DTAPI_E_NOT_FOUND;
        else if (Object != NULL)
        {
            *Objects[i].Ref = Object->Ref;
            Result = DtFunc_CheckDriverVersion(&Sdi->Base.Port.Device->DriverVersion,
                                               Objects[i].IsDf, Objects[i].Type);
        }
    }
    Sdi->QuadLink = QuadLink;
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

    StopKeeper(Sdi);
    FreeBuffer(Sdi);
    DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->AfTx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->AfDma, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    DtFunc_Release(&Sdi->AfTx);
    DtFunc_Release(&Sdi->AfDma);
    OsEvent_Destroy(Sdi->Room);
    DtAlloc_Free(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTxControlSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult SetTxControlSdi(DtTx* Tx, int TxControl)
{
    return SetTxControl((DtSdiTx*)Tx, TxControl);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult ClearFifo(DtTx* Tx)
{
    return ResetFifo((DtSdiTx*)Tx);
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
        int Frames = UnsentFrames(Sdi) - (Sdi->Started ? 1 : 0);
        size_t Size = Sdi->MaxLoad / Sdi->CodedSize * Sdi->RawSize;
        size_t Bytes = (size_t)(Frames > 0 ? Frames : 0) * Sdi->RawSize;

        if (Sdi->Stage != DT_STAGE_SEARCH)
            Bytes += Sdi->RawSize - Sdi->FrameBytesLeft;
        *FifoLoad = (int)(Bytes < Size ? Bytes : Size);
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FifoSizeOr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The load GetFifoLoad reports for a full buffer. A channel without a buffer, on a 4K
// port, gives the typical FIFO size, and the maximum size for GetMaxFifoSize.
//
static int FifoSizeOr(const DtSdiTx* Sdi, int NoBuffer)
{
    if (!Sdi->Registered)
        return NoBuffer;
    return (int)(Sdi->MaxLoad / Sdi->CodedSize *
                 DtSdiFrame_RawSize(&Sdi->Layout, Sdi->SymbolBits));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetFifoSize(DtTx* Tx, int* FifoSize)
{
    *FifoSize = FifoSizeOr((const DtSdiTx*)Tx, DT_FIFO_SIZE_TYP);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetMaxFifoSize(DtTx* Tx, int* MaxFifoSize)
{
    *MaxFifoSize = FifoSizeOr((const DtSdiTx*)Tx, DT_FIFO_SIZE_MAX);
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
        Sdi->SymbolBits = (TxMode & DTAPI_TXMODE_SDI_10B) != 0   ? 10
                          : (TxMode & DTAPI_TXMODE_SDI_16B) != 0 ? 16
                                                                 : 8;
        if (Sdi->Registered)
            Sdi->RawSize = DtSdiFrame_RawSize(&Sdi->Layout, Sdi->SymbolBits);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlagsSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Clears the underflow flags named in Latched.
//
static DtapiResult ClearFlagsSdi(DtTx* Tx, int Latched)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult Write(DtTx* Tx, const uint8_t* Data, size_t Size)
{
    return WriteSdi((DtSdiTx*)Tx, Data, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Bytes a Write left that are not yet in a frame refuse the frame, so that it goes into
// the buffer whole, directly after the frames before it.
//
static DtapiResult WriteFrame(DtTx* Tx, const uint8_t* Frame, int FrameSize,
                              uint64_t Deadline)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    if (Sdi->Stage != DT_STAGE_SEARCH || Sdi->RawHave > 0)
        return DTAPI_E_INCOMP_FRAME;
    return WriteWhole(Sdi, Frame, FrameSize, Deadline);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Wake(DtTx* Tx)
{
    OsEvent_Set(((DtSdiTx*)Tx)->Room);
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
    int WaitMs = Sdi->QuarterMs < 500 ? 2 * Sdi->QuarterMs : 1000;
    bool BlackToCome = Sdi->NextFrameId != 0;

    StopKeeper(Sdi);
    ResetFrame(Sdi);
    while (Sdi->NextFrameId != 0)
    {
        size_t Load;
        if (BlackToCome && ReadLoad(Sdi, &Load) == DTAPI_OK &&
            Sdi->MaxLoad - Load >= Sdi->CodedSize)
        {
            InsertBlack(Sdi, Load);
            PadToWord(Sdi);
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
            Sdi->Started = true;
            Sdi->SendingId = Event.FrameId;
            Since = OsTime_MonotonicMs();
        }
        else if (Result != DTAPI_E_TIMEOUT)
            break;
        else if ((Sdi->Started && !BlackToCome && UnsentFrames(Sdi) <= 1) ||
                 OsTime_MonotonicMs() - Since >= DT_SENT_STALL_MS)
        {
            break;
        }
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const DtTxBackend g_Ops = {
    .Release = Release,
    .SetTxControl = SetTxControlSdi,
    .ClearFifo = ClearFifo,
    .GetFifoLoad = GetFifoLoad,
    .GetFifoSize = GetFifoSize,
    .GetMaxFifoSize = GetMaxFifoSize,
    .GetFlags = GetFlags,
    .SetTxMode = SetTxMode,
    .ClearFlags = ClearFlagsSdi,
    .ApplyIoConfig = ApplyIoConfig,
    .SetTxPolarity = SetTxPolarity,
    .Write = Write,
    .WriteFrame = WriteFrame,
    .Wake = Wake,
    .WaitUntilSent = WaitUntilSent,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiTx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiTx_Attach(const DtTxPort* Port, const DtIoConfig* IoStd, DtTx** Tx)
{
    *Tx = NULL;
    DtSdiTx* Sdi = (DtSdiTx*)DtAlloc_Malloc(sizeof(DtSdiTx));
    if (Sdi == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Sdi, 0, sizeof(*Sdi));
    Sdi->Base.Ops = &g_Ops;
    Sdi->Base.Port = *Port;
    Sdi->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    DtVec_Init(&Sdi->AfTx.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Sdi->AfDma.Objects, sizeof(DtFuncObject));
    Sdi->Room = OsEvent_Create();
    if (Sdi->Room == NULL)
    {
        DtAlloc_Free(Sdi);
        return DTAPI_E_OUT_OF_MEM;
    }
    Sdi->IoStdValue = IoStd->Value;
    Sdi->IoStdSubValue = IoStd->SubValue;

    // The default transmit mode and cleared flags; the I/O standard is applied again.
    Sdi->Base.TxMode = DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B;
    Sdi->SymbolBits = 10;
    Sdi->Base.TxControl = DTAPI_TXCTRL_IDLE;
    DtapiResult Result = DtPcieCmd_SetIoConfig(DrvOf(Sdi), IoStd);

    // Exclusive access to the transmitter and the DMA, all blocks idle, the encoder's
    // corrections on, and the channel set up for the standard.
    if (Result == DTAPI_OK)
        Result = FindObjects(Sdi);
    if (Result == DTAPI_OK)
        Result =
            DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->AfTx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
        Result =
            DtFunc_ExclAccess(DrvOf(Sdi), &Sdi->AfDma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    if (Result == DTAPI_OK)
        Result = BlocksToIdle(Sdi);

    // The data comes from the channel, not from a quad-link master, where the port has
    // that switch.
    if (Result == DTAPI_OK && Sdi->FromMaster.Uuid != 0)
        Result = DtPcieCmd_SwitchSetPosition(DrvOf(Sdi), Sdi->FromMaster, 0, 0);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_SdiTxPSetGenerationMode(DrvOf(Sdi), Sdi->Txp, true, true, true);
    if (Result == DTAPI_OK)
        Result = ConfigureChannel(Sdi);
    if (Result != DTAPI_OK)
    {
        Release(&Sdi->Base);
        return Result;
    }
    *Tx = &Sdi->Base;
    return DTAPI_OK;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The SDI side of an output channel - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "Core/DtWorkerPool.h"  // The threads a batch of lines is encoded over.
#include "Device/DtFunc.h"      // Finding the transmit blocks.
#include "DtPcieAbi.h"          // Operational modes and types.
#include "DtSdiTx.h"            // Interface being implemented.
#include "OAL/OsDmaBuffer.h"    // The DMA buffer.
#include "OAL/OsThread.h"       // The thread, its event, sleeping.
#include "Sdi/DtSdiAnc.h"       // Finding the payload ID of a frame written.
#include "Sdi/DtSdiLevelB.h"    // Frames of 3G level B.
#include "Sdi/DtSdiView.h"      // The view of a lent frame.
#include "Video/DtFrameProps.h" // The frame rate.
#include "Video/DtSdiFrame.h"   // The buffer's format and the raw frame.
#include "Video/DtSmpte352.h"   // The signalling of the black frames.
#include "Video/DtVidStd.h"     // Which standards are 4K.
#include "cdtapi_sdi.h"         // The builder, which makes the black frames.

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

// The sequence number of the first frame's format event from which on the thread may
// follow that frame with a black one: the application has until then, half a frame, to
// write the second frame. The last quarter of a frame goes out only when data follows it.
#define DT_SDITX_FIRST_BLACK_EVENT_SEQ 2

// The PHY's underflow flag is read every so many format events.
#define DT_SDITX_PHY_POLL_EVENTS 50

// How far a frame's stamp may be from the one before, or from the grid of the frames'
// starts, and still count; the stamps over which the grid takes the earliest; and the
// stamps off the grid in a row after which the grid starts afresh.
#define DT_SDITX_STAMP_TOLERANCE_NS 1000000
#define DT_SDITX_STAMP_WINDOW 50
#define DT_SDITX_STAMP_RESYNC 3

// The largest header with its padding: DT_SDIFRAME_TX_HEADER_BYTES padded to 512 bits.
#define DT_SDITX_MAX_HEADER_BYTES 64

// Bounds the words of horizontal blanking that one stream of a line has between its
// timing references. The largest number is 4125 - 1280 - 12, in 720p23.98 and 720p24.
#define DT_SDITX_MAX_HANC_WORDS 4096

// The most raw lines of a black frame that carry a payload ID which the channel keeps to
// change its signalling: a line in each field, and in 2160p the lines of the four
// sub-images, fit with room.
#define DT_SDITX_MAX_VPID_LINES 8

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

    // The configured standard; FrameLayout.VidStd is DTAPI_VIDSTD_UNKNOWN without a
    // buffer.
    DtSdiFrameLayout FrameLayout;
    size_t CodedFrameSize; // A coded frame with its header
    size_t RawFrameSize;   // A raw frame in the current transmit mode
    OsDmaBuffer DmaBuffer;
    bool BufferRegistered;
    size_t MaxLoad;       // The buffer less the data word kept free
    size_t PcieWordBytes; // A PCIe data word, which the card reads the buffer in
    int BurstFifoSize;    // Bytes
    int QuarterFrameMs;   // A quarter frame period, at least 1 ms
    uint8_t* BlackLines;  // The coded lines of a black frame, line headers included;
                          // of 3G level B, of a black picture of field 1
    uint8_t* BlackLines2; // Of 3G level B, those of field 2; NULL otherwise
    // The signalling of the black frames: that of the last frame written, whole; the
    // standard's default before the first.
    DtSdiVideoSignalling BlackSignalling;
    // The raw lines of the black frames that carry a payload ID, 16 bits a symbol, so
    // that a change of signalling codes only those again: [0] of BlackLines, [1] of
    // BlackLines2. NumVpidLines is -1 when they could not be kept; the black frame is
    // then built again whole.
    uint8_t* VpidRaw[2];
    int NumVpidLines[2];
    int VpidLines[2][DT_SDITX_MAX_VPID_LINES]; // Their numbers in the raw frame, from 0
    DtSdiGeometry Geo;       // Where a raw line's payload ID lies; see GeoValid
    bool GeoValid;           // Geo describes the configured standard
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
    int LinesEncoded;        // Lines encoded into the buffer
    int LineStartBit;        // The bit the next line starts at in its first byte
    size_t PartialLineBytes; // Bytes in PartialLine
    size_t FrameBytesLeft;   // Raw bytes of the frame, padding included, not yet taken
    bool FrameVpidFound;     // The frame's lines so far held its payload ID
    uint32_t FrameVpid;      // That payload ID, with byte 1 in the least significant bits

    // A program can borrow room for a frame in the buffer and build the frame there. A
    // lending run starts at the first lend and ends when the channel goes idle or its
    // buffer is freed. During a run the program is responsible for sending on time, so
    // the channel inserts no black frames.
    bool Lending;        // A lending run is going on
    DtSdiView* LentView; // The view that holds the lent room; NULL if none

    // The thread that keeps the signal while sending.
    OsThread* KeeperThread;
    bool StopRequested;
    OsEvent* RoomEvent; // Set after every wait for a format event, and for a detach
    int NumFormatEvents;
    bool FirstEventSeen; // A format event came since the channel held
    bool BlackAllowed;   // Black frames may follow the first frame: see
                         // DT_SDITX_FIRST_BLACK_EVENT_SEQ
    int SendingFrameId;  // The frame ID of the last format event

    // 3G level B: a frame written is one of the interface, whose two pictures go to the
    // card one after the other.
    DtSdiLevelB* LevelB;       // The converter; NULL for any other standard
    uint8_t* Pictures[2];      // The pictures of a frame, room for 16 bits a symbol
    uint8_t* BlackPictures[2]; // A black picture of field 1 and of field 2,
                               // BlackPictureBits bits a symbol
    int BlackPictureBits;      // 0 while BlackPictures hold none

    // The grid on which the frames start on the cable, from the driver's stamps; see
    // NoteStamp.
    bool LastStampValid; // A stamp was seen since the channel held
    int LastStampId;     // Its frame ID, 16 bits
    int64_t LastStampNs; // Its time of day in nanoseconds
    bool GridValid;      // The grid is known
    int GridId;          // A frame ID of the grid, 16 bits
    int64_t GridNs;      // The start of that frame on the cable, in nanoseconds
    int WindowStamps;    // The stamps that agreed with the grid in this window
    int64_t WindowMinNs; // Their smallest distance after the grid, in nanoseconds
    int OffGridStamps;   // The stamps off the grid in a row that agree with each other
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EndLending -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Drops the lent frame without sending it, and ends the lending run. The view that held
// the frame then describes no frame.
//
static void EndLending(DtSdiTx* Sdi)
{
    if (Sdi->LentView != NULL)
        DtSdiView_Forget(Sdi->LentView);
    Sdi->LentView = NULL;
    Sdi->Lending = false;
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
                  (size_t)Sdi->LinesEncoded * DtSdiFrame_TxCodedBytesPerLine(Layout);
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
    // Of 3G level B, the black picture of the field of the frame ID: field 1 is odd.
    const uint8_t* Black = Sdi->BlackLines;
    if (Sdi->BlackLines2 != NULL && Sdi->NextFrameId % 2 == 0)
        Black = Sdi->BlackLines2;
    WriteFrameHeader(Sdi, Sdi->WriteOffset, Sdi->NextFrameId);
    CopyIntoBuffer(Sdi,
                   WrapOffset(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderNumBytes),
                   Black, (size_t)Layout->NumCodedLines * (size_t)Layout->TxStride);

    DtapiResult Result = CommitFrame(Sdi);
    if (Result == DTAPI_OK)
        Sdi->FifoUfl = Sdi->FifoUflLatched = true;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FramesToNs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns the duration of Frames frames of the channel's standard in nanoseconds, from
// its exact rate.
//
static int64_t FramesToNs(const DtSdiTx* Sdi, int64_t Frames)
{
    int Num = 0;
    int Den = 0;
    DtVidStd_FrameRate(Sdi->FrameLayout.VidStd, &Num, &Den);
    if (Num <= 0)
        return 0;
    return (Frames * Den * 1000000000LL + Num / 2) / Num;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoteStamp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes the stamp Ns of frame Id into the grid on which the frames start on the cable.
//
// The driver stamps a frame when the formatter reads its header, just before the frame
// goes out. The delay of the interrupt only makes a stamp later, so the grid follows the
// earliest stamps:
// - A stamp before the grid moves the grid to it at once.
// - After DT_SDITX_STAMP_WINDOW stamps on the grid, the grid moves to the earliest of
//   them, also when that is later, so that it follows the card's clock.
//
// A stamp counts as on the grid within DT_SDITX_STAMP_TOLERANCE_NS:
// - At the start of a run the first frame waits for the genlock reference, up to a
//   period of it, so its stamp is early. The grid starts only at a stamp that agrees
//   with the one before.
// - A stamp off the grid, such as one of an interrupt handled late, is left out.
// - DT_SDITX_STAMP_RESYNC stamps off the grid in a row that agree with each other mean
//   that the card took a grid of its own, as after an underflow: the grid starts afresh
//   at the last of them.
//
// It takes a few operations on integers per frame.
//
static void NoteStamp(DtSdiTx* Sdi, int Id, int64_t Ns)
{
    const bool Agrees =
        Sdi->LastStampValid && llabs(Ns - Sdi->LastStampNs -
                                     FramesToNs(Sdi, (Id - Sdi->LastStampId) & 0xFFFF)) <=
                                   DT_SDITX_STAMP_TOLERANCE_NS;
    Sdi->LastStampValid = true;
    Sdi->LastStampId = Id;
    Sdi->LastStampNs = Ns;

    const int64_t After =
        Sdi->GridValid ? Ns - Sdi->GridNs - FramesToNs(Sdi, (Id - Sdi->GridId) & 0xFFFF)
                       : 0;
    if (!Sdi->GridValid || llabs(After) > DT_SDITX_STAMP_TOLERANCE_NS)
    {
        Sdi->OffGridStamps =
            Agrees && Sdi->OffGridStamps > 0 ? Sdi->OffGridStamps + 1 : 1;
        const bool Start =
            !Sdi->GridValid ? Agrees : Sdi->OffGridStamps >= DT_SDITX_STAMP_RESYNC;
        if (Start)
        {
            Sdi->GridValid = true;
            Sdi->GridId = Id;
            Sdi->GridNs = Ns;
            Sdi->WindowStamps = 0;
            Sdi->WindowMinNs = 0;
            Sdi->OffGridStamps = 0;
        }
        return;
    }
    Sdi->OffGridStamps = 0;
    if (After < 0)
    {
        Sdi->GridId = Id;
        Sdi->GridNs = Ns;
        Sdi->WindowMinNs = 0;
    }
    else if (Sdi->WindowStamps == 0 || After < Sdi->WindowMinNs)
        Sdi->WindowMinNs = After;
    if (++Sdi->WindowStamps >= DT_SDITX_STAMP_WINDOW)
    {
        Sdi->GridNs += Sdi->WindowMinNs;
        Sdi->WindowStamps = 0;
        Sdi->WindowMinNs = 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoteEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes the frame a format event names as the one going out. The first event of a frame
// with a valid stamp gives that frame's start; see NoteStamp.
//
static void NoteEvent(DtSdiTx* Sdi, const DtSdiTxFEvent* Event)
{
    Sdi->FirstEventSeen = true;
    Sdi->SendingFrameId = Event->FrameId;
    if (Event->SeqNumber == 0 && Event->SofTimeValid)
        NoteStamp(Sdi, Event->FrameId,
                  (int64_t)Event->SofSeconds * 1000000000LL + Event->SofNanoseconds);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SignalKeeperThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// While sending: waits for the formatter's format events, the only waiter for them,
// takes their underflow flag and, every DT_SDITX_PHY_POLL_EVENTS events and after a wait
// without an event, the PHY's, and writes a black frame when the frame going out is the
// last one written. After the first frame of a run it waits
// with that until the frame's event DT_SDITX_FIRST_BLACK_EVENT_SEQ or a wait that times
// out, so that an application that wrote only one frame before sending has time to write
// the next. Wakes a write waiting for room after each wait, whether an event came or not.
//
static void SignalKeeperThread(void* Context)
{
    DtSdiTx* Sdi = (DtSdiTx*)Context;

    OsThread_SetName("DtSdiTxKeeper");

    OsThread_RaisePriority(OS_THREAD_PRIORITY_RING);
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
            NoteEvent(Sdi, &Event);
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

        // When the frame going out is the last one, put a black frame after it. Not
        // during a lending run: then sending on time is the program's task.
        size_t Load;
        if (Sdi->BlackAllowed && !Sdi->Lending && UnsentFrames(Sdi) <= 1 &&
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

    DtapiResult Result = DtPcieCmd_CdmacFlushChannel(Drv, Sdi->Cdmac);
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
    Sdi->LastStampValid = false;
    Sdi->GridValid = false;
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
    EndLending(Sdi);
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
// Room for DT_SDITX_BUF_ROOM_FRAMES coded frames and as many more as
// DT_SDITX_FIFO_SIZE_TYP holds of raw 10-bit frames, rounded up to a power of two within
// the buffer's bounds, and to whole prefetch units of pages.
//
static size_t BufferSizeFor(const DtSdiTx* Sdi, int PrefetchSize)
{
    size_t Raw = DtSdiFrame_RawSize(&Sdi->FrameLayout, 10);
    size_t NeededSize =
        (DT_SDITX_BUF_ROOM_FRAMES + DT_SDITX_FIFO_SIZE_TYP / Raw) * Sdi->CodedFrameSize;
    size_t Unit = 4096 * (size_t)(PrefetchSize > 0 ? PrefetchSize : 1);
    size_t Size = DT_SDITX_BUF_MIN_SIZE;

    while (Size < NeededSize && Size < DT_SDITX_BUF_MAX_SIZE)
        Size *= 2;
    return (Size + Unit - 1) / Unit * Unit;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AllocBandSymbols -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The encoder's working symbols, one set for every band a batch of lines divides into.
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
                           : DtSdiFrame_NumJobPieces(&Sdi->FrameLayout);

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BuildBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Builds a black frame of the configured standard into Raw, a raw frame of Bits bits a
// symbol in the layout of the configured standard, Size bytes: the builder's frame
// without an image, audio or packets, so that it carries the payload ID as every frame
// the builder makes does, with the signalling of the last frame written. Of 3G level B it
// is a picture of field Field.
//
static DtapiResult BuildBlack(const DtSdiTx* Sdi, int Field, int Bits, uint8_t* Raw,
                              size_t Size)
{
    const int VidStd = Sdi->FrameLayout.VidStd;
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtapiResult Result = View == NULL || Builder == NULL ? DTAPI_E_OUT_OF_MEM : DTAPI_OK;

    if (Result == DTAPI_OK)
        Result = DtSdiBuilder_SetVideoSignalling(Builder, &Sdi->BlackSignalling);
    if (Result == DTAPI_OK)
        Result = Sdi->LevelB != NULL
                     ? DtSdiView_SetRawPicture(View, Raw, Size, VidStd, Bits, Field)
                     : DtSdiView_SetRawFrame(View, Raw, Size, VidStd, Bits);
    if (Result == DTAPI_OK)
        Result = DtSdiBuilder_Build(Builder, View, NULL, NULL, NULL);
    DtSdiBuilder_Free(Builder);
    DtSdiView_Free(View);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RawSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Symbol Index of a raw line of Bits bits a symbol, 10 or 16, that starts at bit StartBit
// of Bytes. Only 10-bit symbols start part-way through a byte.
//
static uint16_t RawSymbol(const uint8_t* Bytes, size_t StartBit, int Bits, size_t Index)
{
    if (Bits == 16)
        return (
            uint16_t)(((uint32_t)Bytes[2 * Index] | (uint32_t)Bytes[2 * Index + 1] << 8) &
                      0x3FF);
    const size_t Bit = StartBit + Index * 10;
    const uint32_t Value = (uint32_t)Bytes[Bit / 8] | (uint32_t)Bytes[Bit / 8 + 1] << 8;
    return (uint16_t)((Value >> (Bit % 8)) & 0x3FF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRawSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets symbol Index of a raw line as RawSymbol reads it to Value.
//
static void SetRawSymbol(uint8_t* Bytes, size_t StartBit, int Bits, size_t Index,
                         uint16_t Value)
{
    if (Bits == 16)
    {
        Bytes[2 * Index] = (uint8_t)(Value & 0xFF);
        Bytes[2 * Index + 1] = (uint8_t)(Value >> 8);
        return;
    }
    // A line's bits are a multiple of 10, so a symbol starts at an even bit and lies
    // within two bytes.
    const size_t Bit = StartBit + Index * 10;
    const unsigned Shift = (unsigned)(Bit % 8);
    uint32_t Word = (uint32_t)Bytes[Bit / 8] | (uint32_t)Bytes[Bit / 8 + 1] << 8;
    Word = (Word & ~(0x3FFu << Shift)) | ((uint32_t)Value & 0x3FF) << Shift;
    Bytes[Bit / 8] = (uint8_t)(Word & 0xFF);
    Bytes[Bit / 8 + 1] = (uint8_t)(Word >> 8 & 0xFF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PatchLineVpid -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Finds the payload ID packets in the horizontal blanking of every stream of a raw line
// of Bits bits a symbol, 10 or 16, that starts at bit StartBit of Bytes, and flips the
// bits Delta has set in their four bytes, with byte 1 in Delta's least significant bits;
// the bytes' parity and the checksum follow. A checksum the transmitter is to fill in
// stays so. Returns the number of packets found; a Delta of 0 only counts them.
//
static int PatchLineVpid(const DtSdiTx* Sdi, uint8_t* Bytes, size_t StartBit, int Bits,
                         uint32_t Delta)
{
    const DtSdiGeometry* Geo = &Sdi->Geo;
    const int Words = Geo->StreamHancWords - Geo->StreamEavWords - Geo->StreamSavWords;
    if (!Sdi->GeoValid || Words <= 0 || Words > DT_SDITX_MAX_HANC_WORDS)
        return 0;

    int Packets = 0;
    uint16_t Hanc[DT_SDITX_MAX_HANC_WORDS];
    for (int Stream = 0; Stream < Geo->NumStreams; Stream++)
    {
        const size_t First = (size_t)Geo->StreamFirst[Stream] +
                             (size_t)Geo->StreamEavWords * (size_t)Geo->NumStreams;
        for (int k = 0; k < Words; k++)
            Hanc[k] = RawSymbol(Bytes, StartBit, Bits,
                                First + (size_t)k * (size_t)Geo->NumStreams);

        bool Changed = false;
        int Pos = 0;
        DtSdiAncFound Found;
        while (DtSdiAnc_Find(Hanc, Words, &Pos, &Found))
        {
            if (Found.Did != DT_SDIANC_DID_PAYLOAD_ID ||
                Found.SdidOrDbn != DT_SDIANC_SDID_PAYLOAD_ID || Found.NumWords != 4)
                continue;
            Packets++;
            if (Delta == 0)
                continue;
            uint16_t Data[4];
            for (int b = 0; b < 4; b++)
                Data[b] =
                    DtSdiAnc_WithParity8((Found.Words[b] ^ Delta >> (8 * b)) & 0xFF);
            const bool Checksum = Found.Words[4] != DT_SDIANC_NO_CHECKSUM;
            DtSdiAnc_Put(Hanc, Found.Start, Found.Did, Found.SdidOrDbn, Data, 4,
                         Checksum);
            Changed = true;
        }
        for (int k = 0; Changed && k < Words; k++)
            SetRawSymbol(Bytes, StartBit, Bits,
                         First + (size_t)k * (size_t)Geo->NumStreams, Hanc[k]);
    }
    return Packets;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeBlackLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Codes raw line Line of a black frame, 16 bits a symbol at Src, into the coded lines at
// Lines, as BlackLines holds them, line headers included.
//
static void EncodeBlackLine(DtSdiTx* Sdi, const uint8_t* Src, int Line, uint8_t* Lines)
{
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    if (!Layout->Is4k)
    {
        DtSdiFrame_EncodeLine(Layout, 16, Src, 0,
                              Lines + (size_t)Line * (size_t)Layout->TxStride);
        return;
    }
    uint8_t* Dst = Lines + (size_t)(2 * Line) * (size_t)Layout->TxStride;
    DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Line, Dst);
    DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Line + 1, Dst + Layout->TxStride);
    DtSdiFrame_EncodeLine4k(Layout, 16, Src, Line, Dst + Layout->TxLineHeaderNumBytes,
                            Dst + Layout->TxStride + Layout->TxLineHeaderNumBytes,
                            Sdi->BandSymbols);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the coded lines of the black frame that BuildBlack makes at Lines, as
// BlackLines holds them, line headers included, and keeps its raw lines that carry a
// payload ID. Of 3G level B it is a picture of field Field.
//
static DtapiResult EncodeBlack(DtSdiTx* Sdi, int Field, uint8_t* Lines)
{
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    const size_t Size = DtSdiFrame_RawSize(Layout, 16);
    const size_t RawLine = DtSdiFrame_RawLineNumBits(Layout, 16) / 8;
    uint8_t* Raw = (uint8_t*)DtAlloc_Malloc(Size);
    DtapiResult Result = Raw == NULL ? DTAPI_E_OUT_OF_MEM : DTAPI_OK;

    const int Slot = Field - 1;
    if (Sdi->VpidRaw[Slot] == NULL)
        Sdi->VpidRaw[Slot] =
            (uint8_t*)DtAlloc_Malloc((size_t)DT_SDITX_MAX_VPID_LINES * RawLine);
    Sdi->NumVpidLines[Slot] = Sdi->VpidRaw[Slot] == NULL ? -1 : 0;

    if (Result == DTAPI_OK)
        Result = BuildBlack(Sdi, Field, 16, Raw, Size);
    if (Result == DTAPI_OK)
    {
        memset(Lines, 0, (size_t)Layout->NumCodedLines * (size_t)Layout->TxStride);
        const int RawLines =
            Layout->Is4k ? Layout->NumCodedLines / 2 : Layout->NumCodedLines;
        for (int Line = 0; Line < RawLines; Line++)
        {
            uint8_t* Src = Raw + (size_t)Line * RawLine;
            EncodeBlackLine(Sdi, Src, Line, Lines);
            int* Num = &Sdi->NumVpidLines[Slot];
            if (*Num < 0 || PatchLineVpid(Sdi, Src, 0, 16, 0) == 0)
                continue;
            if (*Num == DT_SDITX_MAX_VPID_LINES)
            {
                *Num = -1;
                continue;
            }
            memcpy(Sdi->VpidRaw[Slot] + (size_t)*Num * RawLine, Src, RawLine);
            Sdi->VpidLines[Slot][(*Num)++] = Line;
        }
    }
    DtAlloc_Free(Raw);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeVpidLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Frees the raw lines with a payload ID that EncodeBlack kept for black frame Slot.
//
static void FreeVpidLines(DtSdiTx* Sdi, int Slot)
{
    DtAlloc_Free(Sdi->VpidRaw[Slot]);
    Sdi->VpidRaw[Slot] = NULL;
    Sdi->NumVpidLines[Slot] = -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Frees what a write of 3G level B uses.
//
static void FreeLevelB(DtSdiTx* Sdi)
{
    if (Sdi->LevelB != NULL)
        DtSdiLevelB_Free(Sdi->LevelB);
    DtAlloc_Free(Sdi->LevelB);
    DtAlloc_Free(Sdi->Pictures[0]);
    DtAlloc_Free(Sdi->Pictures[1]);
    DtAlloc_Free(Sdi->BlackPictures[0]);
    DtAlloc_Free(Sdi->BlackPictures[1]);
    DtAlloc_Free(Sdi->BlackLines2);
    Sdi->BlackLines2 = NULL;
    FreeVpidLines(Sdi, 1);
    Sdi->LevelB = NULL;
    Sdi->Pictures[0] = Sdi->Pictures[1] = NULL;
    Sdi->BlackPictures[0] = Sdi->BlackPictures[1] = NULL;
    Sdi->BlackPictureBits = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AllocLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sets up what a write of 3G level B uses, when the configured standard is level B.
//
static DtapiResult AllocLevelB(DtSdiTx* Sdi)
{
    FreeLevelB(Sdi);
    const DtVidStdEntry* Info = DtVidStd_Find(Sdi->FrameLayout.VidStd);
    if (Info == NULL || !Info->IsLevelB || Sdi->FrameLayout.Is4k)
        return DTAPI_OK;
    const size_t Size = DtSdiFrame_RawSize(&Sdi->FrameLayout, 16);
    Sdi->LevelB = (DtSdiLevelB*)DtAlloc_Malloc(sizeof(DtSdiLevelB));
    Sdi->Pictures[0] = (uint8_t*)DtAlloc_Malloc(Size);
    Sdi->Pictures[1] = (uint8_t*)DtAlloc_Malloc(Size);
    Sdi->BlackPictures[0] = (uint8_t*)DtAlloc_Malloc(Size);
    Sdi->BlackPictures[1] = (uint8_t*)DtAlloc_Malloc(Size);
    if (Sdi->LevelB == NULL || Sdi->Pictures[0] == NULL || Sdi->Pictures[1] == NULL ||
        Sdi->BlackPictures[0] == NULL || Sdi->BlackPictures[1] == NULL)
    {
        FreeLevelB(Sdi);
        return DTAPI_E_OUT_OF_MEM;
    }
    const DtapiResult Result = DtSdiLevelB_Init(Sdi->LevelB, Sdi->FrameLayout.VidStd);
    if (Result != DTAPI_OK)
    {
        FreeLevelB(Sdi);
        return Result;
    }

    // The black picture of field 2 that InsertBlack commits for an even frame ID;
    // BlackLines holds that of field 1.
    const size_t Lines =
        (size_t)Sdi->FrameLayout.NumCodedLines * (size_t)Sdi->FrameLayout.TxStride;
    Sdi->BlackLines2 = (uint8_t*)DtAlloc_Malloc(Lines);
    const DtapiResult Black = Sdi->BlackLines2 == NULL
                                  ? DTAPI_E_OUT_OF_MEM
                                  : EncodeBlack(Sdi, 2, Sdi->BlackLines2);
    if (Black != DTAPI_OK)
        FreeLevelB(Sdi);
    return Black;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FreeStandardBuffers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The DMA controller lets go of the buffer, which is then freed, and the standard's
// buffers go too. Failures are ignored.
//
static void FreeStandardBuffers(DtSdiTx* Sdi)
{
    EndLending(Sdi);
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
    FreeLevelB(Sdi);
    FreeVpidLines(Sdi, 0);
    DtSmpte352_TakeSignalling(NULL, &Sdi->BlackSignalling);
    Sdi->GeoValid = false;
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

    const DtVidStdEntry* Info = DtVidStd_Find(Sdi->IoStdSubValue);
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
    FreeVpidLines(Sdi, 0);
    DtAlloc_Free(Sdi->BlackLines);
    DtAlloc_Free(Sdi->WrapLineBuffer);
    DtAlloc_Free(Sdi->PartialLine);
    DtAlloc_Free(Sdi->BandSymbols);
    Sdi->BandSymbols = NULL;
    Sdi->BlackLines =
        (uint8_t*)DtAlloc_Malloc((size_t)Layout.NumCodedLines * (size_t)Layout.TxStride);
    Sdi->WrapLineBuffer =
        (uint8_t*)DtAlloc_Malloc(DtSdiFrame_TxCodedBytesPerLine(&Layout));
    Sdi->PartialLine = (uint8_t*)DtAlloc_Malloc(Line);
    Sdi->SymbolsPerBand = DtSdiFrame_NumBandSymbols(&Layout);
    Sdi->GeoValid = DtSdiGeometry_Init(&Sdi->Geo, Layout.VidStd) == DTAPI_OK;
    DtSmpte352_DefaultSignalling(Layout.VidStd, &Sdi->BlackSignalling);
    ConfigureJobRunner(Sdi);
    if (Sdi->BlackLines == NULL || Sdi->WrapLineBuffer == NULL ||
        Sdi->PartialLine == NULL || (Layout.Is4k && Sdi->BandSymbols == NULL) ||
        AllocLevelB(Sdi) != DTAPI_OK || EncodeBlack(Sdi, 1, Sdi->BlackLines) != DTAPI_OK)
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
    DtVidStd_FrameRate(Sdi->IoStdSubValue, &Num, &Den);
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
    Sdi->FrameVpidFound = false;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LineSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Symbol Index of a raw line in the transmit mode that starts at bit StartBit of Bytes.
// Only 10-bit symbols start part-way through a byte.
//
static uint16_t LineSymbol(const DtSdiTx* Sdi, const uint8_t* Bytes, size_t StartBit,
                           size_t Index)
{
    if (Sdi->BitsPerSymbol != 10)
        return (uint16_t)SymbolAt(Sdi, Bytes, Index);
    const size_t Bit = StartBit + Index * 10;
    const uint32_t Value = (uint32_t)Bytes[Bit / 8] | (uint32_t)Bytes[Bit / 8 + 1] << 8;
    return (uint16_t)((Value >> (Bit % 8)) & 0x3FF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NoteLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Looks for the payload ID of the frame being written in raw line Line, which starts at
// bit StartBit of Bytes, and keeps it in FrameVpid. Only the lines before the picture are
// searched, in the horizontal blanking of link 1's Y stream, and only until it is found.
// Of 3G level B the payload ID of link A is taken, not that of link B.
//
static void NoteLine(DtSdiTx* Sdi, const uint8_t* Bytes, size_t StartBit, int Line)
{
    const DtSdiGeometry* Geo = &Sdi->Geo;
    if (Sdi->FrameVpidFound || !Sdi->GeoValid)
        return;
    const int FirstActive = Geo->Is4k ? Geo->PictureFirstIndex : Geo->FieldFirstIndex[0];
    if (Line >= FirstActive)
        return;

    const int Stream = Geo->NumStreams == 1 ? 0 : 1;
    const int Words = Geo->StreamHancWords - Geo->StreamEavWords - Geo->StreamSavWords;
    uint16_t Hanc[DT_SDITX_MAX_HANC_WORDS];
    if (Words <= 0 || Words > DT_SDITX_MAX_HANC_WORDS)
        return;
    for (int k = 0; k < Words; k++)
    {
        const size_t Symbol = (size_t)Geo->StreamFirst[Stream] +
                              (size_t)(Geo->StreamEavWords + k) * (size_t)Geo->NumStreams;
        Hanc[k] = LineSymbol(Sdi, Bytes, StartBit, Symbol);
    }

    int Pos = 0;
    DtSdiAncFound Found;
    while (DtSdiAnc_Find(Hanc, Words, &Pos, &Found))
    {
        if (Found.Did != DT_SDIANC_DID_PAYLOAD_ID ||
            Found.SdidOrDbn != DT_SDIANC_SDID_PAYLOAD_ID || Found.NumWords < 4)
            continue;
        const uint32_t Vpid = (uint32_t)(Found.Words[0] & 0xFF) |
                              (uint32_t)(Found.Words[1] & 0xFF) << 8 |
                              (uint32_t)(Found.Words[2] & 0xFF) << 16 |
                              (uint32_t)(Found.Words[3] & 0xFF) << 24;
        if (DtSmpte352_PayloadId(Vpid) == DT_S352_ID_S425_1080_B &&
            (Vpid & DT_S352_LEVELB_LINK_B) != 0)
            continue;
        Sdi->FrameVpid = Vpid;
        Sdi->FrameVpidFound = true;
        return;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FollowSignalling -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// After a frame the program wrote, makes the black frames carry its signalling: when the
// frame's payload ID says something else than the black frames do, the payload ID
// packets of the black frames change. Only the lines that carry them are coded again, as
// a black frame is sent when the program is already late. Where those lines could not be
// kept, the black frames are built again whole.
//
// A frame without a payload ID, or with one the library cannot read, leaves the black
// frames as they are. So does signalling the builder does not write, such as the Color
// VANC packet's. A field the standard cannot carry gets the standard's default, as on the
// builder.
//
static void FollowSignalling(DtSdiTx* Sdi)
{
    DtSdiVideoSignalling Signalling;
    if (!Sdi->FrameVpidFound || Sdi->BlackLines == NULL ||
        !DtSmpte352_ReadSignalling(Sdi->FrameVpid, &Signalling) ||
        Signalling.Colorimetry == DT_SDI_COLORIMETRY_COLOR_VANC ||
        memcmp(&Signalling, &Sdi->BlackSignalling, sizeof(Signalling)) == 0)
        return;

    // What the builder writes for the two, and the bits in which they differ.
    const int VidStd = Sdi->FrameLayout.VidStd;
    DtSdiVideoSignalling NoFrame, Old, New;
    DtSmpte352_TakeSignalling(NULL, &NoFrame);
    const bool Resolved =
        DtSmpte352_ResolveSignalling(VidStd, &NoFrame, &Sdi->BlackSignalling, &Old) ==
            DTAPI_OK &&
        DtSmpte352_ResolveSignalling(VidStd, &NoFrame, &Signalling, &New) == DTAPI_OK;
    const int NumSlots = Sdi->BlackLines2 != NULL ? 2 : 1;
    bool Kept = Resolved;
    for (int Slot = 0; Slot < NumSlots; Slot++)
        Kept = Kept && Sdi->NumVpidLines[Slot] >= 0;
    if (Kept)
    {
        const uint32_t Delta = DtSmpte352_MakeSignalling(VidStd, &Old) ^
                               DtSmpte352_MakeSignalling(VidStd, &New);
        const size_t RawLine = DtSdiFrame_RawLineNumBits(&Sdi->FrameLayout, 16) / 8;
        const size_t PictureLine =
            Sdi->BlackPictureBits == 0
                ? 0
                : DtSdiFrame_RawLineNumBits(&Sdi->FrameLayout, Sdi->BlackPictureBits);
        for (int Slot = 0; Slot < NumSlots && Delta != 0; Slot++)
        {
            uint8_t* Lines = Slot == 0 ? Sdi->BlackLines : Sdi->BlackLines2;
            for (int i = 0; i < Sdi->NumVpidLines[Slot]; i++)
            {
                const int Line = Sdi->VpidLines[Slot][i];
                uint8_t* Raw = Sdi->VpidRaw[Slot] + (size_t)i * RawLine;
                PatchLineVpid(Sdi, Raw, 0, 16, Delta);
                EncodeBlackLine(Sdi, Raw, Line, Lines);
                // The black pictures of 3G level B, when made, have the same lines.
                if (Sdi->BlackPictureBits != 0)
                {
                    const size_t At = (size_t)Line * PictureLine;
                    PatchLineVpid(Sdi, Sdi->BlackPictures[Slot] + At / 8, At % 8,
                                  Sdi->BlackPictureBits, Delta);
                }
            }
        }
        Sdi->BlackSignalling = Signalling;
        return;
    }

    const DtSdiVideoSignalling Before = Sdi->BlackSignalling;
    Sdi->BlackSignalling = Signalling;
    DtapiResult Result = EncodeBlack(Sdi, 1, Sdi->BlackLines);
    if (Result == DTAPI_OK && Sdi->BlackLines2 != NULL)
    {
        Result = EncodeBlack(Sdi, 2, Sdi->BlackLines2);
        if (Result != DTAPI_OK)
        {
            Sdi->BlackSignalling = Before;
            EncodeBlack(Sdi, 1, Sdi->BlackLines);
        }
    }
    if (Result != DTAPI_OK)
        Sdi->BlackSignalling = Before;
    else
        Sdi->BlackPictureBits = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitForRoom -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits, without the lock, until the buffer has room for a whole frame, or until the
// monotonic clock reaches Deadline, DT_TX_NO_DEADLINE for no limit. While sending the
// thread wakes the wait after every format event; while holding nothing goes out, and the
// wait looks again every quarter frame. Returns DTAPI_E_CANCELLED for a detach,
// DTAPI_E_IDLE when the channel went idle meanwhile, DTAPI_E_TIMEOUT, or the failure of
// reading the read offset.
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

    // A raw line becomes one coded line, or for 4K two coded lines, each behind its line
    // header.
    size_t Coded = DtSdiFrame_TxCodedBytesPerLine(Layout);
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
    NoteLine(Sdi, Src, (size_t)Sdi->LineStartBit, Sdi->LinesEncoded);

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
// its own, no encoding writes past them, and the working symbols are per band.
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
// a stop does not wait a whole frame's encoding for the lock.
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
    const size_t Coded = DtSdiFrame_TxCodedBytesPerLine(Layout);
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
    for (size_t i = 0; i < Lines; i++)
    {
        const size_t At = LineStartBit + i * Bits;
        NoteLine(Sdi, *Data + At / 8, At % 8, Band.FirstLine + (int)i);
    }

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

    // A program that lends frames does not also write them. A frame of 3G level B goes
    // through WriteFrame, which takes its pictures apart.
    if (Sdi->Lending)
        return DTAPI_E_IN_USE;
    if (Sdi->LevelB != NULL)
        return DTAPI_E_NOT_SUPPORTED;

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
                {
                    Sdi->FifoUfl = false;
                    FollowSignalling(Sdi);
                }
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
        {
            Sdi->FifoUfl = false;
            FollowSignalling(Sdi);
        }
    }
    ForgetPartialFrame(Sdi);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteLevelB -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a frame of the interface of 3G level B: takes its two pictures out, and writes
// them one after the other, each as WriteOneFrame writes a frame. The frame IDs count the
// frames the card sends from the start of the run, and the first is field 2: field 1 must
// have an odd ID and field 2 an even one. When the next ID is not the picture's, at the
// start of a run or after a black frame of the thread, a black picture goes first. The
// black pictures carry the signalling of field 1's picture, which is looked for before
// they are made, so that a black picture in front of the frame carries it too.
//
static DtapiResult WriteLevelB(DtSdiTx* Sdi, const uint8_t* Frame, int FrameSize,
                               uint64_t Deadline)
{
    const int Bits = Sdi->BitsPerSymbol;
    if (Bits != 10 && Bits != 16)
        return DTAPI_E_INVALID_MODE;
    if ((size_t)FrameSize != DtSdiLevelB_FrameSize(&Sdi->LevelB->Geo, Bits))
        return DTAPI_E_INVALID_SIZE;

    for (int Field = 1; Field <= 2; Field++)
        DtSdiLevelB_TakeField(Sdi->LevelB, Field, Frame, Bits, Sdi->Pictures[Field - 1],
                              Bits, &Sdi->JobRunner);
    const size_t LineBits = DtSdiFrame_RawLineNumBits(&Sdi->FrameLayout, Bits);
    Sdi->FrameVpidFound = false;
    for (int Line = 0; Line < Sdi->FrameLayout.NumLines && !Sdi->FrameVpidFound; Line++)
    {
        const size_t At = (size_t)Line * LineBits;
        NoteLine(Sdi, Sdi->Pictures[0] + At / 8, At % 8, Line);
    }
    FollowSignalling(Sdi);

    if (Sdi->BlackPictureBits != Bits)
    {
        for (int Field = 1; Field <= 2; Field++)
        {
            const DtapiResult Black =
                BuildBlack(Sdi, Field, Bits, Sdi->BlackPictures[Field - 1],
                           DtSdiFrame_RawSize(&Sdi->FrameLayout, Bits));
            if (Black != DTAPI_OK)
                return Black;
        }
        Sdi->BlackPictureBits = Bits;
    }

    const int PictureSize = (int)Sdi->RawFrameSize;
    DtapiResult Result = DTAPI_OK;
    for (int Field = 1; Field <= 2 && Result == DTAPI_OK; Field++)
    {
        const bool Odd = Sdi->NextFrameId % 2 != 0;
        // The black picture goes in the other field's place.
        if (Odd != (Field == 1))
            Result =
                WriteOneFrame(Sdi, Sdi->BlackPictures[2 - Field], PictureSize, Deadline);
        if (Result == DTAPI_OK)
            Result = WriteOneFrame(Sdi, Sdi->Pictures[Field - 1], PictureSize, Deadline);
    }
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
            DtFunc_FindObject(Objects[i].Instance, Objects[i].IsDriverFunction,
                              Objects[i].Type, Objects[i].Role);

        if (Object == NULL && !Optional)
            Result = DTAPI_E_NOT_FOUND;
        else if (Object != NULL)
        {
            *Objects[i].Object = Object->Object;
            Result =
                DtFunc_CheckDriverVersion(&Sdi->Tx.Port.Device->DriverVersion,
                                          Objects[i].IsDriverFunction, Objects[i].Type);
        }
    }
    Sdi->HasQuadLink = HasQuadLink;
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LendFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Lends the room for the next frame, at the write offset, to View. The frame header is
// written at once, and in 2160p the line header of every coded line, so that the builder
// only writes the lines' sections. The write offset stays where it is until the frame is
// committed.
//
static DtapiResult LendFrame(DtTx* Tx, DtSdiView* View, void* Holder, uint64_t Deadline)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;

    if (Sdi->LentView != NULL)
        return DTAPI_E_IN_USE;
    if (Sdi->WriteStage != DT_SDITX_STAGE_SEARCH || Sdi->PartialLineBytes > 0)
        return DTAPI_E_INCOMP_FRAME;

    // The run starts now, so that no black frame follows while this call waits.
    Sdi->Lending = true;
    DtapiResult Result = WaitForRoom(Sdi, Deadline);
    if (Result != DTAPI_OK)
        return Result;

    WriteFrameHeader(Sdi, Sdi->WriteOffset, Sdi->NextFrameId);
    const size_t LinesStart =
        WrapOffset(Sdi, Sdi->WriteOffset + (size_t)Layout->TxHeaderNumBytes);
    Result = DtSdiView_SetTxFrame(View, Layout, Sdi->DmaBuffer.Data, Sdi->DmaBuffer.Size,
                                  LinesStart, Holder);
    if (Result != DTAPI_OK)
        return Result;
    if (Layout->TxLineHeaderNumBytes > 0)
    {
        for (int Line = 0; Line < Layout->NumLines; Line++)
        {
            uint8_t* Coded = DtSdiView_TxCodedLines(View, Line);
            DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Line, Coded);
            DtSdiFrame_EncodeTxLineHeader(Layout, 2 * Line + 1, Coded + Layout->TxStride);
        }
    }
    // The IDs count the frames the card sends from 0 at the start of the run, and the
    // first frame of a run is field 2 of 3G level B.
    DtSdiView_SetLevelBField(View, Sdi->NextFrameId % 2 == 0 ? 2 : 1);
    Sdi->LentView = View;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetNextFrameTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The next frame has ID NextFrameId: a lent frame, or the frame being written, keeps it
// until it is committed. Adds the frames from the grid's frame to that frame, times the
// frame period, to the grid's start; see NoteStamp.
//
static DtapiResult GetNextFrameTime(DtTx* Tx, DtTimeOfDay* StartTime)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    if (!Sdi->GridValid)
        return DTAPI_E_NOT_STARTED;
    const int64_t Ns =
        Sdi->GridNs + FramesToNs(Sdi, (Sdi->NextFrameId - Sdi->GridId) & 0xFFFF);
    StartTime->Seconds = (uint32_t)(Ns / 1000000000LL);
    StartTime->Nanoseconds = (uint32_t)(Ns % 1000000000LL);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CommitLentFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Copies the line that wraps around the ring's end into place, then moves the write
// offset past the frame, so that the card sends it.
//
static DtapiResult CommitLentFrame(DtTx* Tx, DtSdiView* View)
{
    DtSdiTx* Sdi = (DtSdiTx*)Tx;

    if (Sdi->LentView == NULL || Sdi->LentView != View)
        return DTAPI_E_INVALID_ARG;
    if (!View->IsBuilt)
        return DTAPI_E_STATE;

    DtSdiView_StoreWrapLine(View);
    DtapiResult Result = CommitFrame(Sdi);
    if (Result == DTAPI_OK)
        Sdi->FifoUfl = false;
    Sdi->LentView = NULL;
    DtSdiView_Forget(View);
    return Result;
}

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

    // A program that lends frames does not also write them.
    if (Sdi->Lending)
        return DTAPI_E_IN_USE;
    if (Sdi->WriteStage != DT_SDITX_STAGE_SEARCH || Sdi->PartialLineBytes > 0)
        return DTAPI_E_INCOMP_FRAME;
    if (Sdi->LevelB != NULL)
        return WriteLevelB(Sdi, Frame, FrameSize, Deadline);
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
    EndLending(Sdi);
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
            NoteEvent(Sdi, &Event);
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
    .LendFrame = LendFrame,
    .CommitLentFrame = CommitLentFrame,
    .GetNextFrameTime = GetNextFrameTime,
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

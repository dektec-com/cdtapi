// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The SDI side of an input channel - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "Core/DtRing.h"        // Reading the ring.
#include "Core/DtWorkerPool.h"  // The threads a frame's lines are converted over.
#include "Device/DtAvInput.h"   // Detecting the signal's standard.
#include "Device/DtFunc.h"      // Finding the receive channel.
#include "DtPcieAbi.h"          // DT_FUNC_OPMODE_ and SDI rate values.
#include "DtSdiRx.h"            // Interface being implemented.
#include "OAL/OsThread.h"       // The process.
#include "Video/DtFrameProps.h" // The frame rate and geometry.
#include "Video/DtSdiFrame.h"   // The ring's format and the raw frame.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The FIFO size the ring is sized for and a frame size is checked against, and what a
// channel without a ring reports as its maximum.
#define DT_SDIRX_FIFO_SIZE (48 * 1024 * 1024)

// The bounds on the ring, the frames it asks room for, and the fewest frames a ring must
// hold.
#define DT_SDIRX_RING_MIN_SIZE (8 * 1024 * 1024)
#define DT_SDIRX_RING_MAX_SIZE (256 * 1024 * 1024)
#define DT_SDIRX_RING_ROOM_FRAMES 5
#define DT_SDIRX_RING_MIN_FRAMES 2

// The delay in microseconds from the start of a frame to its first format event.
#define DT_SDIRX_FMT_EVENT_DELAY_US 200

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct DtSdiRx
{
    DtRx Rx;
    DtDrvObject ChSdiRx; // The receive channel
    bool Scale12GTo3G;

    int IoStdValue; // The port's I/O standard
    int IoStdSubValue;

    int BitsPerSymbol; // 8, 10 or 16, from the receive mode
    bool FifoOvf;
    bool FifoOvfLatched;

    // The configured channel.
    bool ChannelAttached;
    DtSdiFrameLayout FrameLayout;
    DtRing Ring;             // Base NULL without a ring
    bool RingMappedByCdtapi; // Mapped by CDTAPI rather than by the driver
    uint8_t*
        WrapLineBuffer; // Coded lines that run across the end of the ring, one a band
    uint16_t*
        BandSymbols;    // The conversion's working symbols of a 4K line, one set a band
    int QuarterFrameMs; // A quarter frame period, at least 1 ms

    // The pool the channel gave, and the pieces it asked for: 0 for as many as the
    // standard calls for. The channel holds the pool.
    DtWorkerPool* WorkerPool;
    int WorkerThreads;

    // The pieces a frame's lines are converted in, and what one band of them needs. The
    // buffers above hold DtJobRunner_NumPieces(&Work) sets, so a band uses its own.
    DtJobRunner JobRunner;
    size_t WrapLineBufferBytes;
    size_t SymbolsPerBand;

    // Reading.
    bool InSync;
    int ExpectedFrameId;
} DtSdiRx;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrvOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static OsDrv* DrvOf(const DtSdiRx* Sdi)
{
    return Sdi->Rx.Port.Device->Drv;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BitsPerSymbolOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The symbol size of a receive mode.
//
static int BitsPerSymbolOf(int RxMode)
{
    if ((RxMode & DTAPI_RXMODE_SDI_10B) != 0)
        return 10;
    if ((RxMode & DTAPI_RXMODE_SDI_16B) != 0)
        return 16;
    return 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RingSizeFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Room for five coded frames and as many more as a 48 MB FIFO holds of raw 10-bit
// frames, rounded up to a power of two within the ring's bounds.
//
static int RingSizeFor(const DtSdiFrameLayout* Layout)
{
    size_t CodedSize = DtSdiFrame_RxCodedSize(Layout);
    size_t RawSize = DtSdiFrame_RawSize(Layout, 10);
    size_t NeededSize =
        (DT_SDIRX_RING_ROOM_FRAMES + DT_SDIRX_FIFO_SIZE / RawSize) * CodedSize;
    size_t Size = DT_SDIRX_RING_MIN_SIZE;

    while (Size < NeededSize && Size < DT_SDIRX_RING_MAX_SIZE)
        Size *= 2;
    return (int)Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FramesInRing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The complete frames the ring holds at the largest load the driver allows, which is the
// ring less the data word the driver keeps free. A frame in the ring takes its coded
// size, header and padding included; what is left over holds no whole frame.
//
static size_t FramesInRing(const DtSdiRx* Sdi)
{
    return Sdi->Ring.MaxLoad / DtSdiFrame_RxCodedSize(&Sdi->FrameLayout);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Unmaps the ring, frees the band buffers, forgets the layout, and sets the receive
// channel idle and detaches from it, ignoring failures.
//
static void ReleaseChannel(DtSdiRx* Sdi)
{
    DtPcieCmd_ChSdiRxUnmapDmaBuf(DrvOf(Sdi), Sdi->Ring.Base, (int)Sdi->Ring.Size,
                                 Sdi->RingMappedByCdtapi);
    memset(&Sdi->Ring, 0, sizeof(Sdi->Ring));
    Sdi->RingMappedByCdtapi = false;
    DtAlloc_Free(Sdi->WrapLineBuffer);
    Sdi->WrapLineBuffer = NULL;
    DtAlloc_Free(Sdi->BandSymbols);
    Sdi->BandSymbols = NULL;
    memset(&Sdi->FrameLayout, 0, sizeof(Sdi->FrameLayout));
    Sdi->FrameLayout.VidStd = DTAPI_VIDSTD_UNKNOWN;

    if (Sdi->ChannelAttached)
    {
        DtPcieCmd_ChSdiRxSetOpMode(DrvOf(Sdi), Sdi->ChSdiRx, DT_FUNC_OPMODE_IDLE);
        DtPcieCmd_ChSdiRxDetach(DrvOf(Sdi), Sdi->ChSdiRx);
    }
    Sdi->ChannelAttached = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AllocBandBuffers -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The working buffers of the configured layout, one set for every band a frame's lines
// divide into. Replaces what was there, so a change in the number of bands comes through
// it too.
//
static DtapiResult AllocBandBuffers(DtSdiRx* Sdi)
{
    const size_t Bands = (size_t)DtJobRunner_NumPieces(&Sdi->JobRunner);

    DtAlloc_Free(Sdi->WrapLineBuffer);
    DtAlloc_Free(Sdi->BandSymbols);
    Sdi->BandSymbols = NULL;
    Sdi->WrapLineBufferBytes = DtSdiFrame_RxCodedBytesPerLine(&Sdi->FrameLayout);
    Sdi->SymbolsPerBand = DtSdiFrame_NumBandSymbols(&Sdi->FrameLayout);
    Sdi->WrapLineBuffer = (uint8_t*)DtAlloc_Malloc(Bands * Sdi->WrapLineBufferBytes);
    if (Sdi->FrameLayout.Is4k)
        Sdi->BandSymbols =
            (uint16_t*)DtAlloc_Malloc(Bands * Sdi->SymbolsPerBand * sizeof(uint16_t));
    if (Sdi->WrapLineBuffer == NULL ||
        (Sdi->FrameLayout.Is4k && Sdi->BandSymbols == NULL))
        return DTAPI_E_OUT_OF_MEM;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureJobRunner -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Divides the lines over the pool the channel gave, into the pieces it asked for or, with
// 0, the ones the standard calls for, and sizes the working buffers by them. Buffers that
// cannot be had for those pieces are taken for one, so that the side decodes in the
// reading thread rather than not at all, and DTAPI_E_OUT_OF_MEM says so; LineBuf is NULL
// when not even those can be had.
//
static DtapiResult ConfigureJobRunner(DtSdiRx* Sdi)
{
    const int Pieces = Sdi->WorkerThreads > 0
                           ? Sdi->WorkerThreads
                           : DtSdiFrame_NumJobPieces(&Sdi->FrameLayout);

    DtapiResult Result = DtJobRunner_SetPool(&Sdi->JobRunner, Sdi->WorkerPool, Pieces);
    if (Result == DTAPI_OK)
        Result = AllocBandBuffers(Sdi);
    if (Result != DTAPI_OK)
    {
        DtJobRunner_SetPool(&Sdi->JobRunner, NULL, 0);
        AllocBandBuffers(Sdi);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches exclusively to the receive channel under a friendly name, and configures and
// maps it for the port's I/O standard. A channel already attached is released first.
//
static DtapiResult ConfigureChannel(DtSdiRx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    const int PortIndex = Sdi->Rx.Port.Port - 1;
    DtChSdiRxProps Props;
    uint8_t* RingBase = NULL;
    int BufSize = 0;
    int MaxLoad = 0;
    bool MappedByCdtapi = false;

    memset(&Props, 0, sizeof(Props));
    ReleaseChannel(Sdi);

    // The sub-value of an SDI standard is its video standard.
    DtFrameProps FrameProps;
    if (!DtFrameProps_Init(&FrameProps, Sdi->IoStdSubValue))
        return DTAPI_E_INVALID_VIDSTD;

    // A read waits a quarter frame at a time, also on a channel without a ring.
    int FpsNum;
    int FpsDen;
    DtVidStd_FrameRate(Sdi->IoStdSubValue, &FpsNum, &FpsDen);
    Sdi->QuarterFrameMs = FpsDen * 1000 / FpsNum / DT_SDIFRAME_FMT_EVENTS_PER_FRAME;
    if (Sdi->QuarterFrameMs < 1)
        Sdi->QuarterFrameMs = 1;

    // The friendly name is the process name and ID, cut to the longest name the driver
    // takes. A process without a name gets the library's.
    char Process[128];
    OsProcess_Name(Process, sizeof(Process));
    char FullName[160];
    snprintf(FullName, sizeof(FullName), "%s:%" PRIu32,
             Process[0] != '\0' ? Process : "CDTAPI", OsProcess_Id());
    char Name[DT_CHAN_FRIENDLY_NAME_MAX_LENGTH + 1];
    memcpy(Name, FullName, sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = '\0';

    DtapiResult Result = DtPcieCmd_ChSdiRxAttach(Drv, Sdi->ChSdiRx, true, Name);
    if (Result != DTAPI_OK)
        return Result;
    Sdi->ChannelAttached = true;

    Result = DtPcieCmd_ChSdiRxSetOpMode(Drv, Sdi->ChSdiRx, DT_FUNC_OPMODE_IDLE);

    // 2160p over one 6G or 12G link receives as raw frames (plan 0014); a 4K standard
    // over four links, or of level-B links, attaches without a ring; see SetRxControl.
    const DtVidStdEntry* StdInfo = DtVidStd_Find(Sdi->IoStdSubValue);
    const bool OneLink = Sdi->IoStdValue == DTAPI_IOCONFIG_6GSDI ||
                         Sdi->IoStdValue == DTAPI_IOCONFIG_12GSDI;
    if (Result == DTAPI_OK && (OneLink || DtVidStd_Is4k(Sdi->IoStdSubValue)) &&
        (!OneLink || StdInfo == NULL || StdInfo->IsLevelB))
    {
        return DTAPI_OK;
    }

    if (Result == DTAPI_OK)
        Result = DtPcieCmd_ChSdiRxGetProps(Drv, Sdi->ChSdiRx, &Props);
    if (Result == DTAPI_OK &&
        !DtSdiFrame_LayoutInit(&Sdi->FrameLayout, Sdi->IoStdSubValue,
                               Props.StreamAlignment))
    {
        Result = DTAPI_E_INTERNAL;
    }
    if (Result != DTAPI_OK)
    {
        ReleaseChannel(Sdi);
        return Result;
    }

    DtChSdiRxConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.NumPorts = 1;
    Config.PortIndices[0] = PortIndex;
    Config.DmaMinSize = RingSizeFor(&Sdi->FrameLayout);
    Config.FmtIntInterval =
        (FpsDen * 1000000 / FpsNum) / DT_SDIFRAME_FMT_EVENTS_PER_FRAME;
    Config.FmtIntDelay = DT_SDIRX_FMT_EVENT_DELAY_US;
    Config.FmtNumIntsPerFrame = DT_SDIFRAME_FMT_EVENTS_PER_FRAME;
    Config.NumSymsHanc = Sdi->FrameLayout.LineNumSymsHanc;
    Config.NumSymsVidVanc = Sdi->FrameLayout.LineNumSymsActive;
    Config.NumLines = Sdi->FrameLayout.NumLines;
    Config.SdiRate = Sdi->FrameLayout.SdiRate == DT_SDIRATE_12G  ? DT_DRV_SDIRATE_12G
                     : Sdi->FrameLayout.SdiRate == DT_SDIRATE_6G ? DT_DRV_SDIRATE_6G
                     : DtFrameProps_IsSd(&FrameProps)            ? DT_DRV_SDIRATE_SD
                     : DtFrameProps_Is3g(&FrameProps)            ? DT_DRV_SDIRATE_3G
                                                                 : DT_DRV_SDIRATE_HD;
    Config.AssumeInterlaced = DtFrameProps_IsInterlaced(&FrameProps);
    Config.Scale12GTo3G = Sdi->Scale12GTo3G;

    Result = DtPcieCmd_ChSdiRxConfigure(Drv, Sdi->ChSdiRx, &Config);
    if (Result == DTAPI_OK)
        Result = DtPcieCmd_ChSdiRxMapDmaBuf(Drv, Sdi->ChSdiRx, &RingBase, &BufSize,
                                            &MaxLoad, &MappedByCdtapi);

    // The driver keeps a data word of the ring free; a maximum load that keeps nothing
    // free, or leaves no room, describes no ring that can be read.
    if (Result == DTAPI_OK &&
        (MaxLoad >= BufSize || DtRing_Init(&Sdi->Ring, RingBase, (size_t)BufSize,
                                           (size_t)(BufSize - MaxLoad)) != 0))
    {
        DtPcieCmd_ChSdiRxUnmapDmaBuf(Drv, RingBase, BufSize, MappedByCdtapi);
        Result = DTAPI_E_DEV_DRIVER;
    }
    if (Result == DTAPI_OK)
    {
        Sdi->RingMappedByCdtapi = MappedByCdtapi;
        ConfigureJobRunner(Sdi);
        if (Sdi->WrapLineBuffer == NULL ||
            (Sdi->FrameLayout.Is4k && Sdi->BandSymbols == NULL))
            Result = DTAPI_E_OUT_OF_MEM;
    }
    if (Result == DTAPI_OK && FramesInRing(Sdi) < DT_SDIRX_RING_MIN_FRAMES)
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
    {
        ReleaseChannel(Sdi);
        return Result;
    }

    Sdi->InSync = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AdvanceReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves the read offset on by Bytes, which the ring holds, and tells the driver.
//
static DtapiResult AdvanceReadOffset(DtSdiRx* Sdi, size_t Bytes)
{
    if (DtRing_Skip(&Sdi->Ring, Bytes) != 0)
        return DTAPI_E_INTERNAL;
    return DtPcieCmd_ChSdiRxSetReadOffset(DrvOf(Sdi), Sdi->ChSdiRx,
                                          (uint32_t)DtRing_ReadOffset(&Sdi->Ring));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DiscardTo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Skips everything written up to WriteOffset, rounded down to the alignment, so that
// the next read searches for a header again.
//
static DtapiResult DiscardTo(DtSdiRx* Sdi, uint32_t WriteOffset)
{
    size_t Alignment = (size_t)Sdi->FrameLayout.AlignmentInBytes;
    size_t Aligned = (size_t)WriteOffset / Alignment * Alignment;

    Sdi->InSync = false;
    if (DtRing_Restart(&Sdi->Ring, Aligned) != 0 ||
        DtRing_SetWriteOffset(&Sdi->Ring, WriteOffset) != 0)
    {
        return DTAPI_E_DEV_DRIVER;
    }
    return DtPcieCmd_ChSdiRxSetReadOffset(DrvOf(Sdi), Sdi->ChSdiRx, (uint32_t)Aligned);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SyncWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Brings the ring up to how far the channel has written. A write offset that would put
// more in the ring than the driver allows means the two disagree about where reading
// starts, and what the ring holds is discarded; one outside the ring is a driver fault.
//
static DtapiResult SyncWriteOffset(DtSdiRx* Sdi)
{
    uint32_t WriteOffset = 0;
    DtapiResult Result =
        DtPcieCmd_ChSdiRxGetWriteOffset(DrvOf(Sdi), Sdi->ChSdiRx, &WriteOffset);

    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Sdi->Ring.Size)
        return DTAPI_E_DEV_DRIVER;
    if (DtRing_SetWriteOffset(&Sdi->Ring, WriteOffset) != 0)
        return DiscardTo(Sdi, WriteOffset);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DiscardAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Everything written so far is skipped, after an out-of-sync event.
//
static DtapiResult DiscardAll(DtSdiRx* Sdi)
{
    uint32_t WriteOffset = 0;
    DtapiResult Result =
        DtPcieCmd_ChSdiRxGetWriteOffset(DrvOf(Sdi), Sdi->ChSdiRx, &WriteOffset);

    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Sdi->Ring.Size)
        return DTAPI_E_DEV_DRIVER;
    return DiscardTo(Sdi, WriteOffset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Searches the data in the ring for a valid header of any frame, stepping by the
// alignment. The positions searched without finding one are skipped. Returns true with
// the read offset at the header.
//
static bool FindHeader(DtSdiRx* Sdi, DtapiResult* Result)
{
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    size_t Available = DtRing_Load(&Sdi->Ring);

    *Result = DTAPI_OK;
    size_t Offset;
    for (Offset = 0; Offset + (size_t)Layout->RxHeaderNumBytes <= Available;
         Offset += (size_t)Layout->AlignmentInBytes)
    {
        uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES];
        DtRing_PeekAt(&Sdi->Ring, Offset, Bytes, sizeof(Bytes));
        DtSdiFrameRxHeader Header;
        DtSdiFrame_DecodeRxHeader(Bytes, &Header);
        if (DtSdiFrame_CheckRxHeader(Layout, &Header, -1) == DTAPI_OK)
        {
            *Result = AdvanceReadOffset(Sdi, Offset);
            Sdi->InSync = *Result == DTAPI_OK;
            Sdi->ExpectedFrameId = Header.FrameId;
            return Sdi->InSync;
        }
    }
    if (Offset > 0)
        *Result = AdvanceReadOffset(Sdi, Offset);
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Anything but idle receives, and the value is kept as given. Receiving starts reading at
// the start of the ring. With 8-bit symbols, as in DTAPI, and without a ring, receiving
// fails with DTAPI_E_CONFIG_RAW_SDI before the channel runs. A 4K standard over four
// links or of level-B links leaves the channel without a ring, since raw frames of one
// link cannot carry it; 2160p over one 6G or 12G link receives (plan 0014).
//
static DtapiResult ApplyRxControl(DtSdiRx* Sdi, int RxControl)
{
    OsDrv* Drv = DrvOf(Sdi);

    if (Sdi->Rx.RxControl == RxControl)
        return DTAPI_OK;

    DtapiResult Result;
    if (RxControl == DTAPI_RXCTRL_IDLE)
        Result = DtPcieCmd_ChSdiRxSetOpMode(Drv, Sdi->ChSdiRx, DT_FUNC_OPMODE_IDLE);
    else if (Sdi->BitsPerSymbol == 8 || Sdi->Ring.Base == NULL)
        return DTAPI_E_CONFIG_RAW_SDI;
    else
    {
        DtRing_Restart(&Sdi->Ring, 0);
        Sdi->InSync = false;
        Result = DtPcieCmd_ChSdiRxSetReadOffset(Drv, Sdi->ChSdiRx, 0);
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_ChSdiRxSetOpMode(Drv, Sdi->ChSdiRx, DT_FUNC_OPMODE_RUN);
    }
    if (Result != DTAPI_OK)
        return Result;

    Sdi->Rx.RxControl = RxControl;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Release(DtRx* Rx)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    ReleaseChannel(Sdi);
    DtJobRunner_Free(&Sdi->JobRunner);
    DtAlloc_Free(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The full frame without time stamps of any kind, while idle. A raw frame carries none;
// ReadFrame2 gives its time of arrival.
//
static DtapiResult SetRxMode(DtRx* Rx, int RxMode)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;

    if ((RxMode & DTAPI_RXMODE_SDI_MASK) != DTAPI_RXMODE_SDI_FULL)
        return DTAPI_E_INVALID_MODE;
    if ((RxMode & (DTAPI_RXMODE_TIMESTAMP32 | DTAPI_RXMODE_TIMESTAMP64 |
                   DTAPI_RXMODE_TIMESTAMP_TOD)) != 0)
        return DTAPI_E_INVALID_MODE;
    if (Rx->RxControl != DTAPI_RXCTRL_IDLE)
        return DTAPI_E_NOT_IDLE;
    Rx->RxMode = RxMode;
    Sdi->BitsPerSymbol = BitsPerSymbolOf(RxMode);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Without a receive channel, which an I/O standard that failed leaves it, every value
// gives DTAPI_E_NOT_INITIALIZED until a standard sets one up.
//
static DtapiResult SetRxControl(DtRx* Rx, int RxControl)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    return Sdi->ChannelAttached ? ApplyRxControl(Sdi, RxControl)
                                : DTAPI_E_NOT_INITIALIZED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Stops, clears the latched DTAPI_RX_FIFO_OVF and forgets the sync; the next start reads
// from the start of the ring.
//
static DtapiResult ClearFifo(DtRx* Rx)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    DtapiResult Result = ApplyRxControl(Sdi, DTAPI_RXCTRL_IDLE);

    if (Result != DTAPI_OK)
        return Result;
    Sdi->FifoOvfLatched = false;
    Sdi->InSync = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult ClearFlags(DtRx* Rx, int Latched)
{
    if ((Latched & DTAPI_RX_FIFO_OVF) != 0)
        ((DtSdiRx*)Rx)->FifoOvfLatched = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult GetFlags(DtRx* Rx, int* Flags, int* Latched)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;
    *Flags = Sdi->FifoOvf ? DTAPI_RX_FIFO_OVF : 0;
    *Latched = Sdi->FifoOvfLatched ? DTAPI_RX_FIFO_OVF : 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The complete frames from the read offset on, in raw bytes of the receive mode, counted
// also before a read has found the first header; 0 while not receiving.
//
static DtapiResult GetFifoLoad(DtRx* Rx, int* FifoLoad)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;

    *FifoLoad = 0;
    if (Rx->RxControl != DTAPI_RXCTRL_RCV || Sdi->Ring.Base == NULL)
        return DTAPI_OK;
    DtapiResult Result = SyncWriteOffset(Sdi);
    if (Result == DTAPI_OK)
    {
        size_t Frames =
            DtRing_Load(&Sdi->Ring) / DtSdiFrame_RxCodedSize(&Sdi->FrameLayout);
        *FifoLoad =
            (int)(Frames * DtSdiFrame_RawSize(&Sdi->FrameLayout, Sdi->BitsPerSymbol));
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The load GetFifoLoad reports for a full ring. A channel without a ring gives the FIFO
// size.
//
static DtapiResult GetMaxFifoSize(DtRx* Rx, int* MaxFifoSize)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;

    if (Sdi->Ring.Base == NULL)
        *MaxFifoSize = DT_SDIRX_FIFO_SIZE;
    else
        *MaxFifoSize = (int)(FramesInRing(Sdi) *
                             DtSdiFrame_RawSize(&Sdi->FrameLayout, Sdi->BitsPerSymbol));
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Once the configuration is set: the down-scaling is kept, and a new standard
// reconfigures the receive channel.
//
static DtapiResult ApplyIoConfig(DtRx* Rx, const DtIoConfig* Config)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;

    if (Config->Group == DTAPI_IOCONFIG_IODOWNSCALE)
        Sdi->Scale12GTo3G = Config->Value == DTAPI_IOCONFIG_SCALE_12GTO3G;
    if (Config->Group != DTAPI_IOCONFIG_IOSTD)
        return DTAPI_OK;
    Sdi->IoStdValue = Config->Value;
    Sdi->IoStdSubValue = Config->SubValue;
    return ConfigureChannel(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DetectIoStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The video standard, detected with the port's receiver and converted.
//
static DtapiResult DetectIoStd(DtRx* Rx, int* Value, int* SubValue)
{
    DtDetVidStd Info;
    DtAvInput Input;

    DtDetVidStd_SetUnknown(&Info);
    DtapiResult Result = DtAvInput_Attach(&Input, Rx->Port.Device, Rx->Port.Port);
    if (Result == DTAPI_OK)
        Result = DtAvInput_DetectVidStd(&Input, &Info);
    if (Result == DTAPI_OK)
        Result = DtapiVidStd2IoStd(Info.VidStd, Info.LinkStd, Value, SubValue);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFrameBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame read's last checks of the buffer: DTAPI_E_BUF_TOO_SMALL when it cannot hold a
// frame of the channel's standard in its receive mode, and DTAPI_E_INVALID_SIZE for a
// frame larger than the FIFO. Sets *RawSize to the size of that frame.
//
static DtapiResult CheckFrameBuffer(DtRx* Rx, int FrameSize, size_t* RawSize)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;

    *RawSize = DtSdiFrame_RawSize(&Sdi->FrameLayout, Sdi->BitsPerSymbol);
    if ((size_t)FrameSize < *RawSize)
        return DTAPI_E_BUF_TOO_SMALL;
    if (*RawSize > DT_SDIRX_FIFO_SIZE)
        return DTAPI_E_INVALID_SIZE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DecodeLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Decodes the band of lines this piece takes, from the coded lines of the frame at the
// head of the ring into the raw frame. The bands are independent: a line's coded lines
// are its own, the working buffers are per band, and the only thing every band reads is
// the ring. What a band writes is its own as well, for which the bands start on a
// multiple of DtSdiFrame_BandLineStep lines: with 10-bit symbols a line of a frame
// that is not 4K can share a byte of the raw frame with the line after it, and two
// threads must not have the same byte.
//
typedef struct DecodeBand
{
    DtSdiRx* Sdi;
    uint8_t* RawFrame;
    size_t CodedBytesPerLine;
    size_t RawBytesPerLine;
} DecodeBand;

static void DecodeLines(void* Context, int Index, int Count)
{
    const DecodeBand* Band = (const DecodeBand*)Context;
    DtSdiRx* Sdi = Band->Sdi;
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    uint8_t* LineBuf = Sdi->WrapLineBuffer + (size_t)Index * Sdi->WrapLineBufferBytes;
    uint16_t* BandSymbols = Sdi->BandSymbols == NULL
                                ? NULL
                                : Sdi->BandSymbols + (size_t)Index * Sdi->SymbolsPerBand;
    int First;
    int Last;

    DtJobRunner_Split(Layout->NumLines, Index, Count,
                      DtSdiFrame_BandLineStep(Layout, Sdi->BitsPerSymbol), &First, &Last);
    for (int Line = First; Line < Last; Line++)
    {
        size_t Offset =
            (size_t)Layout->RxHeaderNumBytes + (size_t)Line * Band->CodedBytesPerLine;
        const uint8_t* Coded = DtRing_Span(&Sdi->Ring, Offset, Band->CodedBytesPerLine);

        if (Coded == NULL)
        {
            DtRing_PeekAt(&Sdi->Ring, Offset, LineBuf, Band->CodedBytesPerLine);
            Coded = LineBuf;
        }
        if (Layout->Is4k)
            DtSdiFrame_DecodeLine4k(
                Layout, Sdi->BitsPerSymbol, Coded, Coded + Layout->RxStride, Line,
                Band->RawFrame + (size_t)Line * Band->RawBytesPerLine, BandSymbols);
        else
            DtSdiFrame_DecodeLine(Layout, Sdi->BitsPerSymbol, Coded, Line,
                                  Band->RawFrame);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DeliverFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Delivers the next frame into Buffer when the ring holds all of it, and the time of
// arrival its header gives into *ArrivalTime. Returns DTAPI_OK with *Delivered true for a
// frame, DTAPI_OK with *Delivered false when there is none yet, or a driver failure.
//
static DtapiResult DeliverFrame(DtRx* Rx, uint8_t* Buffer, DtTimeOfDay* ArrivalTime,
                                bool* Delivered)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    const DtSdiFrameLayout* Layout = &Sdi->FrameLayout;
    size_t CodedFrameSize = DtSdiFrame_RxCodedSize(Layout);

    *Delivered = false;
    DtapiResult Result = SyncWriteOffset(Sdi);
    if (Result != DTAPI_OK)
        return Result;
    size_t Available = DtRing_Load(&Sdi->Ring);

    // A ring that has filled up has lost data.
    if (Available + (size_t)Layout->RxStride >= Sdi->Ring.MaxLoad)
    {
        Sdi->FifoOvf = true;
        Sdi->FifoOvfLatched = true;
    }

    // Out of sync, a header is searched for; a header that is not the one expected puts
    // the channel out of sync, and the search starts again from that header. A frame
    // whose first or last line is not where it should be lost lines when the ring was
    // full, and holds the start of a later frame: it is not delivered, and the search
    // starts again after its header.
    uint8_t HeaderBytes[DT_SDIFRAME_HEADER_BYTES];
    DtSdiFrameRxHeader Header;
    for (;;)
    {
        if (!Sdi->InSync)
        {
            if (!FindHeader(Sdi, &Result))
                return Result;
            Available = DtRing_Load(&Sdi->Ring);
        }
        if (Available < CodedFrameSize)
            return DTAPI_OK;

        DtRing_PeekAt(&Sdi->Ring, 0, HeaderBytes, sizeof(HeaderBytes));
        DtSdiFrame_DecodeRxHeader(HeaderBytes, &Header);
        if (DtSdiFrame_CheckRxHeader(Layout, &Header, Sdi->ExpectedFrameId) == DTAPI_OK)
        {
            uint8_t FirstLineStart[DT_SDIFRAME_LINE_START_BYTES];

            DtRing_PeekAt(&Sdi->Ring, (size_t)Layout->RxHeaderNumBytes, FirstLineStart,
                          sizeof(FirstLineStart));
            uint8_t LastLineStart[DT_SDIFRAME_LINE_START_BYTES];
            DtRing_PeekAt(&Sdi->Ring,
                          (size_t)Layout->RxHeaderNumBytes +
                              (size_t)(Layout->NumCodedLines - 1) *
                                  (size_t)Layout->RxStride,
                          LastLineStart, sizeof(LastLineStart));
            if (DtSdiFrame_CheckLineNumbers(Layout, FirstLineStart, LastLineStart) ==
                DTAPI_OK)
                break;

            Result = AdvanceReadOffset(Sdi, (size_t)Layout->AlignmentInBytes);
            if (Result != DTAPI_OK)
                return Result;
            Available = DtRing_Load(&Sdi->Ring);
        }
        Sdi->InSync = false;
    }

    // A line that runs across the end of the ring is copied into one piece first. A raw
    // 4K line takes two coded lines and whole bytes, so its lines need no clearing.
    size_t CodedBytesPerLine = DtSdiFrame_RxCodedBytesPerLine(Layout);
    size_t RawBytesPerLine = DtSdiFrame_RawLineNumBits(Layout, Sdi->BitsPerSymbol) / 8;
    if (!Layout->Is4k)
        memset(Buffer, 0, DtSdiFrame_RawSize(Layout, Sdi->BitsPerSymbol));
    DecodeBand Band;
    Band.Sdi = Sdi;
    Band.RawFrame = Buffer;
    Band.CodedBytesPerLine = CodedBytesPerLine;
    Band.RawBytesPerLine = RawBytesPerLine;

    DtJobRunner_Run(&Sdi->JobRunner, DecodeLines, &Band);

    Result = AdvanceReadOffset(Sdi, CodedFrameSize);
    if (Result != DTAPI_OK)
        return Result;

    if (Available - CodedFrameSize + (size_t)Layout->RxStride < Sdi->Ring.MaxLoad)
        Sdi->FifoOvf = false;
    Sdi->ExpectedFrameId = (Header.FrameId + 1) & 0xFFFF;
    ArrivalTime->Seconds = Header.PtpSeconds;
    ArrivalTime->Nanoseconds = Header.PtpNanoseconds;
    *Delivered = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrepareWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A read waits for the receive channel's next format event, a quarter frame at most.
//
static void PrepareWait(DtRx* Rx, DtRxWaitState* State)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;

    memset(State, 0, sizeof(*State));
    State->Backend = Rx->Backend;
    State->Drv = DrvOf(Sdi);
    State->WaitObject = Sdi->ChSdiRx;
    State->MaxMs = Sdi->QuarterFrameMs;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits for the next format event, without the channel's lock. No event in time is no
// failure.
//
static DtapiResult Wait(DtRxWaitState* State, int Ms)
{
    DtChSdiRxEvent Event;
    DtapiResult Result =
        DtPcieCmd_ChSdiRxWaitForFmtEvent(State->Drv, State->WaitObject, Ms, &Event);

    State->EventOutOfSync = Result == DTAPI_OK && !Event.InSync;
    return Result == DTAPI_E_TIMEOUT ? DTAPI_OK : Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AfterWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An event out of sync discards everything written so far.
//
static DtapiResult AfterWait(DtRx* Rx, const DtRxWaitState* State)
{
    return State->EventOutOfSync ? DiscardAll((DtSdiRx*)Rx) : DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetWorkerPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A side with no layout yet keeps the pool for ConfigureChannel, which sizes the work.
//
static DtapiResult SetWorkerPool(DtRx* Rx, DtWorkerPool* Pool, int NumThreads)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;

    Sdi->WorkerPool = Pool;
    Sdi->WorkerThreads = NumThreads;
    if (Sdi->WrapLineBuffer == NULL)
        return DtJobRunner_SetPool(&Sdi->JobRunner, NULL, 0);
    return ConfigureJobRunner(Sdi);
}

static const DtRxBackend g_SdiRxBackend = {
    .Release = Release,
    .SetRxMode = SetRxMode,
    .SetRxControl = SetRxControl,
    .ClearFifo = ClearFifo,
    .ClearFlags = ClearFlags,
    .GetFlags = GetFlags,
    .GetFifoLoad = GetFifoLoad,
    .GetMaxFifoSize = GetMaxFifoSize,
    .ApplyIoConfig = ApplyIoConfig,
    .DetectIoStd = DetectIoStd,
    .SetWorkerPool = SetWorkerPool,
    .CheckFrameBuffer = CheckFrameBuffer,
    .DeliverFrame = DeliverFrame,
    .PrepareWait = PrepareWait,
    .Wait = Wait,
    .AfterWait = AfterWait,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiRx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiRx_Attach(const DtRxAttachedPort* Port, const DtIoConfig* IoStd,
                           DtRx** Rx)
{
    OsDrv* Drv = Port->Device->Drv;

    *Rx = NULL;
    DtSdiRx* Sdi = (DtSdiRx*)DtAlloc_Malloc(sizeof(DtSdiRx));
    if (Sdi == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Sdi, 0, sizeof(*Sdi));
    Sdi->Rx.Backend = &g_SdiRxBackend;
    Sdi->Rx.Port = *Port;
    Sdi->IoStdValue = IoStd->Value;
    Sdi->IoStdSubValue = IoStd->SubValue;
    Sdi->FrameLayout.VidStd = DTAPI_VIDSTD_UNKNOWN;
    DtJobRunner_Init(&Sdi->JobRunner);

    // The receiver and the receive channel, in the port's AF_ASISDIRX.
    DtFuncInstance Instance;
    DtapiResult Result = DtFunc_Find(Drv, Port->Port - 1, "AF_ASISDIRX", "", &Instance);
    if (Result != DTAPI_OK)
    {
        DtAlloc_Free(Sdi);
        return Result;
    }
    const DtFuncObject* SdiRx =
        DtFunc_FindObject(&Instance, true, DT_FUNC_TYPE_SDIRX, "");
    const DtFuncObject* ChSdiRx =
        DtFunc_FindObject(&Instance, true, DT_FUNC_TYPE_CHSDIRX, "");
    Result = SdiRx == NULL || ChSdiRx == NULL ? DTAPI_E_NOT_FOUND : DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtFunc_CheckDriverVersion(&Port->Device->DriverVersion, true,
                                           DT_FUNC_TYPE_SDIRX);
    if (Result == DTAPI_OK)
        Result = DtFunc_CheckDriverVersion(&Port->Device->DriverVersion, true,
                                           DT_FUNC_TYPE_CHSDIRX);
    if (Result == DTAPI_OK)
        Sdi->ChSdiRx = ChSdiRx->Object;
    DtFunc_Release(&Instance);
    if (Result != DTAPI_OK)
    {
        DtAlloc_Free(Sdi);
        return Result;
    }

    // The down-scaling read, and the default receive mode.
    if ((Port->Caps & DT_CAP_SCALE_12GTO3G) != 0)
    {
        DtIoConfig ScaleConfig = *IoStd;

        ScaleConfig.Group = DTAPI_IOCONFIG_IODOWNSCALE;
        Sdi->Scale12GTo3G = DtPcieCmd_GetIoConfig(Drv, &ScaleConfig) == DTAPI_OK &&
                            ScaleConfig.Value == DTAPI_IOCONFIG_SCALE_12GTO3G;
    }
    Sdi->Rx.RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    Sdi->BitsPerSymbol = 10;
    Sdi->Rx.RxControl = DTAPI_RXCTRL_IDLE;

    // The I/O standard is applied again, and the receive channel set up for it.
    Result = DtPcieCmd_SetIoConfig(Drv, IoStd);
    if (Result == DTAPI_OK)
        Result = ConfigureChannel(Sdi);
    if (Result != DTAPI_OK)
    {
        Release(&Sdi->Rx);
        return Result;
    }
    *Rx = &Sdi->Rx;
    return DTAPI_OK;
}

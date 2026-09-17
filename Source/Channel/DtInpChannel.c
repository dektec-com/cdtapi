// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtInpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The SDI input channel: DtInpChannel on a DtPcie receive channel
//
// SPDX-License-Identifier: BSD-3-Clause
//
// DTAPI receives through DtInpChannel, AsiSdiInpChannel_Bb2 and SdiRxImpl_Bb2, which
// run a Matrix row whose MxChannelMemlessRx reads the card's CHSDIRX ring. This file
// does what those layers do for raw SDI frames, directly on the ring: it follows the
// write offset the driver reports, checks each frame's header, converts the frame's
// coded lines into the caller's buffer and sets the read offset past it. See
// Documentation/0007-receive-channel.md for where it departs from DTAPI.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"         // Interface being implemented.
#include "Core/DtAlloc.h"       // Allocation seam.
#include "Core/DtRing.h"        // Reading the ring.
#include "Device/DtAvInput.h"   // Detecting the signal's standard.
#include "Device/DtDevice.h"    // The device and its port capabilities.
#include "Device/DtFunc.h"      // Finding the receive channel.
#include "DtIoConfig.h"         // Validating I/O configurations.
#include "DtPcieAbi.h"          // DT_FWSTATUS_, DT_FUNC_OPMODE_ and SDI rate values.
#include "OAL/OsThread.h"       // The lock, sleeping, the clock, the process.
#include "Video/DtFrameProps.h" // The frame rate and geometry.
#include "Video/DtSdiFrame.h"   // The ring's format and the raw frame.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DTAPI's values for what CDTAPI.h does not define.
//

#define DT_INSTANT_DETACH 1 // DTAPI_INSTANT_DETACH

#define DT_RXMODE_TS 0x10       // DTAPI_RXMODE_TS
#define DT_RXMODE_TS_MASK 0x1F  // DTAPI_RXMODE_TS_MASK
#define DT_RXMODE_STRAW 0x14    // DTAPI_RXMODE_STRAW
#define DT_RXMODE_STL3 0x15     // DTAPI_RXMODE_STL3
#define DT_RXMODE_STL3FULL 0x16 // DTAPI_RXMODE_STL3FULL
#define DT_RXMODE_STTRP 0x19    // DTAPI_RXMODE_STTRP
#define DT_RXMODE_TIMESTAMP32 0x01000000
#define DT_RXMODE_TIMESTAMP64 0x02000000
#define DT_RXMODE_TIMESTAMP_TOD 0x04000000

// SdiRxImpl_Bb2's FIFO_SIZE_MAX.
#define DT_FIFO_SIZE_MAX (48 * 1024 * 1024)

// MxProcessMemless's bounds on the ring, the frames it asks room for, and the fewest
// frames a ring must hold (MIN_DMASIZE_NUMFRAMES).
#define DT_RING_MIN (8 * 1024 * 1024)
#define DT_RING_MAX (256 * 1024 * 1024)
#define DT_RING_FRAMES 5
#define DT_RING_MIN_FRAMES 2

// MxChannelMemlessRx's format event settings: events per frame, and the delay in
// microseconds from the start of a frame to the first.
#define DT_FMT_EVENTS_PER_FRAME 4
#define DT_FMT_EVENT_DELAY 200

// How long DtInpChannel::Detach waits for users of the channel: ten times 10 ms.
#define DT_DETACH_TRIES 10
#define DT_DETACH_PAUSE_MS 10

// How often a channel that is not receiving looks again, as DTAPI's ReadWithTimeOut.
#define DT_IDLE_POLL_MS 10

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtInpChannelC
{
    OsMutex* Lock; // Guards everything below
    bool Attached;
    int Detachers; // Detaches waiting for readers to leave
    int Readers;   // ReadFrame calls between their start and their return

    DtDevice Device; // The channel's own handle to the device
    int Port;        // From 1
    int PortIndex;
    uint32_t Caps; // DT_CAP_ flags of the port
    int Uuid;      // The receive channel's UUID
    bool Scale12GTo3G;

    int IoStdValue; // The port's I/O standard
    int IoStdSubValue;

    int RxMode;
    int RxControl;
    int SymbolBits; // 8, 10 or 16, from the receive mode
    bool FifoOvf;
    bool FifoOvfLatched;

    // The configured channel.
    bool ChannelAttached;
    DtSdiFrameLayout Layout;
    DtRing Ring;      // Base NULL without a ring
    bool RingMapped;  // Mapped by CDtapiLite rather than by the driver
    uint8_t* LineBuf; // A coded line that runs across the end of the ring
    int QuarterMs;    // A quarter frame period, at least 1 ms

    // Reading.
    bool InSync;
    int ExpectedId;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SymbolBitsOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The symbol size of a receive mode, as SdiRxImpl_Bb2::SetRxMode picks it.
//
static int SymbolBitsOf(int RxMode)
{
    if ((RxMode & DTAPI_RXMODE_SDI_10B) != 0)
        return 10;
    if ((RxMode & DTAPI_RXMODE_SDI_16B) != 0)
        return 16;
    return 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RingSizeFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Room for DTAPI's five frames plus the raw frames DTAPI's 48 MB FIFO holds, rounded up
// to a power of two within MxProcessMemless's bounds.
//
static int RingSizeFor(const DtSdiFrameLayout* Layout)
{
    size_t Coded = DtSdiFrameCodedSize(Layout);
    size_t Raw = DtSdiFrameRawSize(Layout, 10);
    size_t Wanted = (DT_RING_FRAMES + DT_FIFO_SIZE_MAX / Raw) * Coded;
    size_t Size = DT_RING_MIN;

    while (Size < Wanted && Size < DT_RING_MAX)
        Size *= 2;
    return (int)Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FramesInRing -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The complete frames the ring holds at the largest load the driver allows. Each frame
// in the ring carries its header and the padding of every section to the alignment, and
// the driver keeps one data word free, so the rest of the ring never holds a frame.
//
static size_t FramesInRing(const DtInpChannel* Chan)
{
    return Chan->Ring.MaxLoad / DtSdiFrameCodedSize(&Chan->Layout);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Unmaps the ring and detaches from the receive channel, ignoring failures, as a detach
// in DTAPI does.
//
static void ReleaseChannel(DtInpChannel* Chan)
{
    DtPcieCmdChSdiRxUnmapDmaBuf(Chan->Device.Drv, Chan->Ring.Base, (int)Chan->Ring.Size,
                                Chan->RingMapped);
    memset(&Chan->Ring, 0, sizeof(Chan->Ring));
    Chan->RingMapped = false;
    DtFree(Chan->LineBuf);
    Chan->LineBuf = NULL;
    memset(&Chan->Layout, 0, sizeof(Chan->Layout));
    Chan->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;

    if (Chan->ChannelAttached)
    {
        DtPcieCmdChSdiRxSetOpMode(Chan->Device.Drv, Chan->Uuid, Chan->PortIndex,
                                  DT_FUNC_OPMODE_IDLE);
        DtPcieCmdChSdiRxDetach(Chan->Device.Drv, Chan->Uuid, Chan->PortIndex);
    }
    Chan->ChannelAttached = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches exclusively to the receive channel under DTAPI's friendly name, and
// configures and maps it for the port's I/O standard, as MxChannelMemlessRx::Attach and
// SetVidStd do. A channel already attached is released first.
//
static DtapiResult ConfigureChannel(DtInpChannel* Chan)
{
    OsDrv* Drv = Chan->Device.Drv;
    DtChSdiRxProps Props;
    uint8_t* Base = NULL;
    int BufSize = 0;
    int MaxLoad = 0;
    bool Mapped = false;

    memset(&Props, 0, sizeof(Props));
    ReleaseChannel(Chan);

    // DtapiIoStd2VidStd: the sub-value of an SDI standard is its video standard.
    DtFrameProps Frame;
    if (!DtFramePropsInit(&Frame, Chan->IoStdSubValue))
        return DTAPI_E_INVALID_VIDSTD;

    // A read waits a quarter frame at a time, also on a channel without a ring.
    int Num;
    int Den;
    DtVidStdFps(Chan->IoStdSubValue, &Num, &Den);
    Chan->QuarterMs = Den * 1000 / Num / DT_FMT_EVENTS_PER_FRAME;
    if (Chan->QuarterMs < 1)
        Chan->QuarterMs = 1;

    // DtPalCHSDIRX::Attach: the process name and ID, cut to the longest name the driver
    // takes. A process without a name gets the library's.
    char Process[128];
    OsProcessName(Process, sizeof(Process));
    char Full[160];
    snprintf(Full, sizeof(Full), "%s:%" PRIu32,
             Process[0] != '\0' ? Process : "CDtapiLite", OsProcessId());
    char Name[DT_CHAN_FRIENDLY_NAME_MAX_LENGTH + 1];
    memcpy(Name, Full, sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = '\0';

    DtapiResult Result =
        DtPcieCmdChSdiRxAttach(Drv, Chan->Uuid, Chan->PortIndex, true, Name);
    if (Result != DTAPI_OK)
        return Result;
    Chan->ChannelAttached = true;

    Result =
        DtPcieCmdChSdiRxSetOpMode(Drv, Chan->Uuid, Chan->PortIndex, DT_FUNC_OPMODE_IDLE);

    // A 4K standard, which DTAPI's raw row does not take, attaches without a ring; see
    // SetRxControl.
    if (Result == DTAPI_OK &&
        (Chan->IoStdValue == DTAPI_IOCONFIG_6GSDI ||
         Chan->IoStdValue == DTAPI_IOCONFIG_12GSDI || DtVidStdIs4k(Chan->IoStdSubValue)))
    {
        return DTAPI_OK;
    }

    if (Result == DTAPI_OK)
        Result = DtPcieCmdChSdiRxGetProps(Drv, Chan->Uuid, Chan->PortIndex, &Props);
    if (Result == DTAPI_OK &&
        !DtSdiFrameLayoutInit(&Chan->Layout, Chan->IoStdSubValue, Props.StreamAlignment))
    {
        Result = DTAPI_E_INTERNAL;
    }
    if (Result != DTAPI_OK)
    {
        ReleaseChannel(Chan);
        return Result;
    }

    DtChSdiRxConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.NumPorts = 1;
    Config.PortIndices[0] = Chan->PortIndex;
    Config.DmaMinSize = RingSizeFor(&Chan->Layout);
    Config.FmtIntInterval = (Den * 1000000 / Num) / DT_FMT_EVENTS_PER_FRAME;
    Config.FmtIntDelay = DT_FMT_EVENT_DELAY;
    Config.FmtNumIntsPerFrame = DT_FMT_EVENTS_PER_FRAME;
    Config.NumSymsHanc = Chan->Layout.LineSymsHanc;
    Config.NumSymsVidVanc = Chan->Layout.LineSymsVideo;
    Config.NumLines = Chan->Layout.NumLines;
    Config.SdiRate = DtFramePropsIsSd(&Frame)   ? DT_DRV_SDIRATE_SD
                     : DtFramePropsIs3g(&Frame) ? DT_DRV_SDIRATE_3G
                                                : DT_DRV_SDIRATE_HD;
    Config.AssumeInterlaced = DtFramePropsIsInterlaced(&Frame);
    Config.Scale12GTo3G = Chan->Scale12GTo3G;

    Result = DtPcieCmdChSdiRxConfigure(Drv, Chan->Uuid, Chan->PortIndex, &Config);
    if (Result == DTAPI_OK)
        Result = DtPcieCmdChSdiRxMapDmaBuf(Drv, Chan->Uuid, Chan->PortIndex, &Base,
                                           &BufSize, &MaxLoad, &Mapped);

    // The driver keeps a data word of the ring free; a maximum load that keeps nothing
    // free, or leaves no room, describes no ring that can be read.
    if (Result == DTAPI_OK &&
        (MaxLoad >= BufSize || DtRingInit(&Chan->Ring, Base, (size_t)BufSize,
                                          (size_t)(BufSize - MaxLoad)) != 0))
    {
        DtPcieCmdChSdiRxUnmapDmaBuf(Drv, Base, BufSize, Mapped);
        Result = DTAPI_E_DEV_DRIVER;
    }
    if (Result == DTAPI_OK)
    {
        Chan->RingMapped = Mapped;
        Chan->LineBuf = (uint8_t*)DtMalloc((size_t)Chan->Layout.Stride);
        if (Chan->LineBuf == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }
    if (Result == DTAPI_OK && FramesInRing(Chan) < DT_RING_MIN_FRAMES)
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
    {
        ReleaseChannel(Chan);
        return Result;
    }

    Chan->InSync = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Advance -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves the read offset on by Bytes, which the ring holds, and tells the driver.
//
static DtapiResult Advance(DtInpChannel* Chan, size_t Bytes)
{
    if (DtRingSkip(&Chan->Ring, Bytes) != 0)
        return DTAPI_E_INTERNAL;
    return DtPcieCmdChSdiRxSetReadOffset(Chan->Device.Drv, Chan->Uuid, Chan->PortIndex,
                                         (uint32_t)DtRingReadOffset(&Chan->Ring));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DiscardTo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Everything written up to WriteOffset is skipped, to an aligned offset, as
// MxChannelMemlessRx::MarkAsProcessed(-1) does after an out-of-sync event.
//
static DtapiResult DiscardTo(DtInpChannel* Chan, uint32_t WriteOffset)
{
    size_t Alignment = (size_t)Chan->Layout.Alignment;
    size_t Aligned = (size_t)WriteOffset / Alignment * Alignment;

    Chan->InSync = false;
    if (DtRingRestart(&Chan->Ring, Aligned) != 0 ||
        DtRingSetWriteOffset(&Chan->Ring, WriteOffset) != 0)
    {
        return DTAPI_E_DEV_DRIVER;
    }
    return DtPcieCmdChSdiRxSetReadOffset(Chan->Device.Drv, Chan->Uuid, Chan->PortIndex,
                                         (uint32_t)Aligned);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Brings the ring up to how far the channel has written. A write offset that would put
// more in the ring than the driver allows means the two disagree about where reading
// starts, and what the ring holds is discarded; one outside the ring is a driver fault.
//
static DtapiResult ReadWriteOffset(DtInpChannel* Chan)
{
    uint32_t WriteOffset = 0;
    DtapiResult Result = DtPcieCmdChSdiRxGetWriteOffset(Chan->Device.Drv, Chan->Uuid,
                                                        Chan->PortIndex, &WriteOffset);

    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Chan->Ring.Size)
        return DTAPI_E_DEV_DRIVER;
    if (DtRingSetWriteOffset(&Chan->Ring, WriteOffset) != 0)
        return DiscardTo(Chan, WriteOffset);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DiscardAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Everything written so far is skipped, after an out-of-sync event.
//
static DtapiResult DiscardAll(DtInpChannel* Chan)
{
    uint32_t WriteOffset = 0;
    DtapiResult Result = DtPcieCmdChSdiRxGetWriteOffset(Chan->Device.Drv, Chan->Uuid,
                                                        Chan->PortIndex, &WriteOffset);

    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Chan->Ring.Size)
        return DTAPI_E_DEV_DRIVER;
    return DiscardTo(Chan, WriteOffset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Searches the data in the ring for a valid header of any frame, stepping by the
// alignment, as MxChannelMemlessRx::FindFrameHeader does. The positions searched without
// finding one are skipped. Returns true with the read offset at the header.
//
static bool FindHeader(DtInpChannel* Chan, DtapiResult* Result)
{
    const DtSdiFrameLayout* Layout = &Chan->Layout;
    size_t Available = DtRingLoad(&Chan->Ring);

    *Result = DTAPI_OK;
    size_t Offset;
    for (Offset = 0; Offset + (size_t)Layout->HeaderBytes <= Available;
         Offset += (size_t)Layout->Alignment)
    {
        uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES];
        DtRingPeekAt(&Chan->Ring, Offset, Bytes, sizeof(Bytes));
        DtSdiFrameHeader Header;
        DtSdiFrameDecodeHeader(Bytes, &Header);
        if (DtSdiFrameCheckHeader(Layout, &Header, -1) == DTAPI_OK)
        {
            *Result = Advance(Chan, Offset);
            Chan->InSync = *Result == DTAPI_OK;
            Chan->ExpectedId = Header.FrameId;
            return Chan->InSync;
        }
    }
    if (Offset > 0)
        *Result = Advance(Chan, Offset);
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Delivers the next frame into Buffer when the ring holds all of it, and the time of
// arrival its header gives into *ArrivalTime. Returns DTAPI_OK with *Taken true for a
// frame, DTAPI_OK with *Taken false when there is none yet, or a driver failure.
//
static DtapiResult TakeFrame(DtInpChannel* Chan, uint8_t* Buffer,
                             DtTimeOfDay* ArrivalTime, bool* Taken)
{
    const DtSdiFrameLayout* Layout = &Chan->Layout;
    size_t Frame = DtSdiFrameCodedSize(Layout);

    *Taken = false;
    DtapiResult Result = ReadWriteOffset(Chan);
    if (Result != DTAPI_OK)
        return Result;
    size_t Available = DtRingLoad(&Chan->Ring);

    // A ring that has filled up has lost data.
    if (Available + (size_t)Layout->Stride >= Chan->Ring.MaxLoad)
    {
        Chan->FifoOvf = true;
        Chan->FifoOvfLatched = true;
    }

    // Out of sync, a header is searched for; a header that is not the one expected puts
    // the channel out of sync, and the search starts again from that header. A frame
    // whose first or last line is not where it should be lost lines when the ring was
    // full, and holds the start of a later frame: it is not delivered, and the search
    // starts again after its header.
    uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES];
    DtSdiFrameHeader Header;
    for (;;)
    {
        if (!Chan->InSync)
        {
            if (!FindHeader(Chan, &Result))
                return Result;
            Available = DtRingLoad(&Chan->Ring);
        }
        if (Available < Frame)
            return DTAPI_OK;

        DtRingPeekAt(&Chan->Ring, 0, Bytes, sizeof(Bytes));
        DtSdiFrameDecodeHeader(Bytes, &Header);
        if (DtSdiFrameCheckHeader(Layout, &Header, Chan->ExpectedId) == DTAPI_OK)
        {
            uint8_t First[DT_SDIFRAME_LINE_START_BYTES];

            DtRingPeekAt(&Chan->Ring, (size_t)Layout->HeaderBytes, First, sizeof(First));
            uint8_t Last[DT_SDIFRAME_LINE_START_BYTES];
            DtRingPeekAt(&Chan->Ring,
                         (size_t)Layout->HeaderBytes +
                             (size_t)(Layout->NumLines - 1) * (size_t)Layout->Stride,
                         Last, sizeof(Last));
            if (DtSdiFrameCheckLines(Layout, First, Last) == DTAPI_OK)
                break;

            Result = Advance(Chan, (size_t)Layout->Alignment);
            if (Result != DTAPI_OK)
                return Result;
            Available = DtRingLoad(&Chan->Ring);
        }
        Chan->InSync = false;
    }

    // A line that runs across the end of the ring is copied into one piece first.
    memset(Buffer, 0, DtSdiFrameRawSize(Layout, Chan->SymbolBits));
    for (int Line = 0; Line < Layout->NumLines; Line++)
    {
        size_t Offset =
            (size_t)Layout->HeaderBytes + (size_t)Line * (size_t)Layout->Stride;
        const uint8_t* Coded = DtRingSpan(&Chan->Ring, Offset, (size_t)Layout->Stride);

        if (Coded == NULL)
        {
            DtRingPeekAt(&Chan->Ring, Offset, Chan->LineBuf, (size_t)Layout->Stride);
            Coded = Chan->LineBuf;
        }
        DtSdiFrameConvertLine(Layout, Chan->SymbolBits, Coded, Line, Buffer);
    }

    Result = Advance(Chan, Frame);
    if (Result != DTAPI_OK)
        return Result;

    if (Available - Frame + (size_t)Layout->Stride < Chan->Ring.MaxLoad)
        Chan->FifoOvf = false;
    Chan->ExpectedId = (Header.FrameId + 1) & 0xFFFF;
    ArrivalTime->Seconds = Header.PtpSeconds;
    ArrivalTime->Nanoseconds = Header.PtpNanoseconds;
    *Taken = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiRxImpl_Bb2::SetRxControl: anything but idle receives, and the value is kept as
// given. Receiving starts reading at the start of the ring, as DtPalCHSDIRX does. With
// 8-bit symbols or a 4K standard receiving fails as the Matrix's row validation fails it
// (MxPreProcess::ValidateRowConfigRaw accepts only 10- and 16-bit raw data of one logical
// link), before the channel runs.
//
static DtapiResult SetRxControl(DtInpChannel* Chan, int RxControl)
{
    OsDrv* Drv = Chan->Device.Drv;

    if (Chan->RxControl == RxControl)
        return DTAPI_OK;

    DtapiResult Result;
    if (RxControl == DTAPI_RXCTRL_IDLE)
        Result = DtPcieCmdChSdiRxSetOpMode(Drv, Chan->Uuid, Chan->PortIndex,
                                           DT_FUNC_OPMODE_IDLE);
    else if (Chan->SymbolBits == 8 || Chan->Ring.Base == NULL)
        return DTAPI_E_CONFIG_RAW_SDI;
    else
    {
        DtRingRestart(&Chan->Ring, 0);
        Chan->InSync = false;
        Result = DtPcieCmdChSdiRxSetReadOffset(Drv, Chan->Uuid, Chan->PortIndex, 0);
        if (Result == DTAPI_OK)
            Result = DtPcieCmdChSdiRxSetOpMode(Drv, Chan->Uuid, Chan->PortIndex,
                                               DT_FUNC_OPMODE_RUN);
    }
    if (Result != DTAPI_OK)
        return Result;

    Chan->RxControl = RxControl;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResetFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SdiRxImpl_Bb2::Reset for DTAPI_FIFO_RESET: stop, clear the flags, drop what is held.
//
static DtapiResult ResetFifo(DtInpChannel* Chan)
{
    DtapiResult Result = SetRxControl(Chan, DTAPI_RXCTRL_IDLE);

    if (Result != DTAPI_OK)
        return Result;
    Chan->FifoOvfLatched = false;
    Chan->InSync = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LockAttached -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the lock of an attached channel, as DtInpChannel's DetachLock admits a call.
// Returns DTAPI_E_NOT_ATTACHED, without the lock, otherwise.
//
static DtapiResult LockAttached(DtInpChannel* Chan)
{
    OsMutexLock(Chan->Lock);
    if (!Chan->Attached)
    {
        OsMutexUnlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Asks reads on other threads to return, waits for them up to Tries pauses of 10 ms, or
// without a limit for -1, then stops and releases the receive channel and the device.
//
// A detach that gives up with DTAPI_E_TIMEOUT withdraws its request, so the channel stays
// attached and usable. The count of waiting detaches keeps a detach that gives up from
// withdrawing the request of another that still waits, and a detach that finds the
// channel detached by another while it waited returns DTAPI_E_NOT_ATTACHED.
//
static DtapiResult Detach(DtInpChannel* Chan, int DetachMode, int Tries)
{
    if (LockAttached(Chan) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    Chan->Detachers++;
    for (int Try = 0; Chan->Attached && Chan->Readers > 0; Try++)
    {
        if (Try == Tries)
        {
            Chan->Detachers--;
            OsMutexUnlock(Chan->Lock);
            return DTAPI_E_TIMEOUT;
        }
        OsMutexUnlock(Chan->Lock);
        OsSleepMs(DT_DETACH_PAUSE_MS);
        OsMutexLock(Chan->Lock);
    }
    Chan->Detachers--;

    if (!Chan->Attached)
    {
        OsMutexUnlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    if ((DetachMode & DT_INSTANT_DETACH) != 0)
        ResetFifo(Chan);
    SetRxControl(Chan, DTAPI_RXCTRL_IDLE);

    ReleaseChannel(Chan);
    DtDeviceRelease(&Chan->Device);
    Chan->Attached = false;
    OsMutexUnlock(Chan->Lock);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtInpChannel* DtInpChannel_Alloc(void)
{
    DtInpChannel* Chan = (DtInpChannel*)DtMalloc(sizeof(DtInpChannel));

    if (Chan == NULL)
        return NULL;
    memset(Chan, 0, sizeof(*Chan));

    Chan->Lock = OsMutexCreate();
    if (Chan->Lock == NULL)
    {
        DtFree(Chan);
        return NULL;
    }
    return Chan;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// As DTAPI's destructor: an instant detach whose result is ignored. Unlike a detach, it
// waits for reads on other threads to return for as long as they take, so that none of
// them uses the channel after it is gone.
//
void DtInpChannel_Free(DtInpChannel* InpChannel)
{
    if (InpChannel == NULL)
        return;

    Detach(InpChannel, DT_INSTANT_DETACH, -1);
    OsMutexDestroy(InpChannel->Lock);
    DtFree(InpChannel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtInpChannel_Freep(DtInpChannel** InpChannel)
{
    if (InpChannel == NULL)
        return;

    DtInpChannel_Free(*InpChannel);
    *InpChannel = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// AttachToPort's steps once the channel has its own handle to the device, which the
// caller releases when they fail.
//
static DtapiResult AttachPort(DtInpChannel* Chan, int Port, uint32_t Caps)
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
        DtapiResult Result = DtPcieCmdSetIoConfig(Chan->Device.Drv, &Config);
        if (Result != DTAPI_OK)
            return Result;
    }

    Config.Group = DTAPI_IOCONFIG_IODIR;
    DtapiResult Result = DtPcieCmdGetIoConfig(Chan->Device.Drv, &Config);
    if (Result == DTAPI_OK && Config.Value != DTAPI_IOCONFIG_INPUT)
        Result = DTAPI_E_NO_DT_INPUT;
    if (Result != DTAPI_OK)
        return Result;

    Config.Group = DTAPI_IOCONFIG_IOSTD;
    Result = DtPcieCmdGetIoConfig(Chan->Device.Drv, &Config);
    if (Result != DTAPI_OK)
        return Result;
    if (Config.Value == DTAPI_IOCONFIG_ASI)
        return DTAPI_E_NOT_SUPPORTED;
    Chan->IoStdValue = Config.Value;
    Chan->IoStdSubValue = Config.SubValue;

    // The receiver and the receive channel of the port's ASI/SDI receiver.
    DtFuncInstance Instance;
    Result = DtFuncFind(Chan->Device.Drv, Chan->PortIndex, "AF_ASISDIRX", "", &Instance);
    if (Result != DTAPI_OK)
        return Result;
    const DtFuncPart* SdiRx = DtFuncGet(&Instance, true, DT_FUNC_TYPE_SDIRX, "");
    const DtFuncPart* ChSdiRx = DtFuncGet(&Instance, true, DT_FUNC_TYPE_CHSDIRX, "");
    Result = SdiRx == NULL || ChSdiRx == NULL ? DTAPI_E_NOT_FOUND : DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtFuncCheckDriverVersion(&Chan->Device.DriverVersion, true,
                                          DT_FUNC_TYPE_SDIRX);
    if (Result == DTAPI_OK)
        Result = DtFuncCheckDriverVersion(&Chan->Device.DriverVersion, true,
                                          DT_FUNC_TYPE_CHSDIRX);
    if (Result == DTAPI_OK)
        Chan->Uuid = ChSdiRx->Uuid;
    DtFuncRelease(&Instance);
    if (Result != DTAPI_OK)
        return Result;

    // Exclusive access: flags cleared, the down-scaling read, the default receive mode.
    Chan->FifoOvf = Chan->FifoOvfLatched = false;
    Chan->Scale12GTo3G = false;
    if ((Caps & DT_CAP_SCALE_12GTO3G) != 0)
    {
        DtIoConfig Scale = Config;

        Scale.Group = DTAPI_IOCONFIG_IODOWNSCALE;
        Chan->Scale12GTo3G = DtPcieCmdGetIoConfig(Chan->Device.Drv, &Scale) == DTAPI_OK &&
                             Scale.Value == DTAPI_IOCONFIG_SCALE_12GTO3G;
    }
    Chan->RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    Chan->SymbolBits = 10;
    Chan->RxControl = DTAPI_RXCTRL_IDLE;

    // The I/O standard is applied again, and the receive channel set up for it.
    Result = DtPcieCmdSetIoConfig(Chan->Device.Drv, &Config);
    if (Result == DTAPI_OK)
        Result = ConfigureChannel(Chan);
    if (Result != DTAPI_OK)
        return Result;

    // A fail-safe port in fail-safe mode is reported, as a success.
    if ((Caps & DT_CAP_FAILSAFE) != 0)
    {
        DtIoConfig FailSafe = Config;

        FailSafe.Group = DTAPI_IOCONFIG_FAILSAFE;
        Result = DtPcieCmdGetIoConfig(Chan->Device.Drv, &FailSafe);
        if (Result != DTAPI_OK)
        {
            ReleaseChannel(Chan);
            return Result;
        }
        if (FailSafe.Value == DTAPI_IOCONFIG_TRUE)
            return DTAPI_OK_FAILSAFE;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_AttachToPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtInpChannel::AttachToPort, AsiSdiInpChannel_Bb2::InitInpChannel and
// SdiRxImpl_Bb2::InitInpChannel, in their order.
//
static DtapiResult Attach(DtInpChannel* Chan, DtDevice* Device, int Port)
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
    if ((Caps & DT_CAP_INPUT) == 0 && (Caps & DT_CAP_IP) == 0)
        return DTAPI_E_NO_DT_INPUT;
    if ((Caps & DT_CAP_MATRIX) != 0 || (Caps & DT_CAP_ASI) == 0)
        return DTAPI_E_NOT_SUPPORTED;

    DtapiResult Result =
        DtDeviceAttachIndex(&Chan->Device, Device->Index, true, Device->Info.Serial);
    if (Result != DTAPI_OK)
        return Result;

    Result = AttachPort(Chan, Port, Caps);
    if (Result >= DTAPI_E)
        DtDeviceRelease(&Chan->Device);
    return Result;
}

DtapiResult DtInpChannel_AttachToPort(DtInpChannel* InpChannel, DtDevice* Device,
                                      int Port)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    OsMutexLock(InpChannel->Lock);
    DtapiResult Result;
    if (InpChannel->Attached)
        Result = DTAPI_E_ATTACHED;
    else
    {
        Result = Attach(InpChannel, Device, Port);
        InpChannel->Attached = Result < DTAPI_E;
    }
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtInpChannel::Detach: waits up to 100 ms for readers to leave, then stops, releases
// the receive channel and the device.
//
DtapiResult DtInpChannel_Detach(DtInpChannel* InpChannel, int DetachMode)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    return Detach(InpChannel, DetachMode, DT_DETACH_TRIES);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_ClearFifo(DtInpChannel* InpChannel)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = ResetFifo(InpChannel);
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_ClearFlags(DtInpChannel* InpChannel, int Latched)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    if ((Latched & DTAPI_RX_FIFO_OVF) != 0)
        InpChannel->FifoOvfLatched = false;
    OsMutexUnlock(InpChannel->Lock);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_DetectIoStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtInpChannel::DetectIoStd needs an ASI, SDI or SPI capability; SdiRxImpl_Bb2 detects
// the video standard with the port's receiver and converts it.
//
DtapiResult DtInpChannel_DetectIoStd(DtInpChannel* InpChannel, int* Value, int* SubValue)
{
    const uint32_t Usable = DT_CAP_ASI | DT_CAP_SDI | DT_CAP_HDSDI | DT_CAP_3GSDI |
                            DT_CAP_SPI | DT_CAP_SPISDI;

    if (InpChannel == NULL || Value == NULL || SubValue == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtDetVidStd Info;
    DtAvInputSetUnknown(&Info);
    DtapiResult Result;
    if ((InpChannel->Caps & Usable) == 0)
        Result = DTAPI_E_NOT_SUPPORTED;
    else
    {
        DtAvInput Input;
        Result = DtAvInputAttach(&Input, &InpChannel->Device, InpChannel->Port);
        if (Result == DTAPI_OK)
            Result = DtAvInputDetectVidStd(&Input, &Info);
        if (Result == DTAPI_OK)
            Result = DtapiVidStd2IoStd(Info.VidStd, Info.LinkStd, Value, SubValue);
    }
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_GetFifoLoad(DtInpChannel* InpChannel, int* FifoLoad)
{
    DtapiResult Result = DTAPI_OK;

    if (InpChannel == NULL || FifoLoad == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // The complete frames from the read offset on, counted also before a read has found
    // the first header.
    *FifoLoad = 0;
    if (InpChannel->RxControl == DTAPI_RXCTRL_RCV && InpChannel->Ring.Base != NULL)
    {
        Result = ReadWriteOffset(InpChannel);
        if (Result == DTAPI_OK)
        {
            size_t Frames =
                DtRingLoad(&InpChannel->Ring) / DtSdiFrameCodedSize(&InpChannel->Layout);
            *FifoLoad = (int)(Frames * DtSdiFrameRawSize(&InpChannel->Layout,
                                                         InpChannel->SymbolBits));
        }
    }
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_GetMaxFifoSize(DtInpChannel* InpChannel, int* MaxFifoSize)
{
    if (InpChannel == NULL || MaxFifoSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // The load GetFifoLoad reports for a full ring. A channel without a ring, on a 4K
    // port, gives DTAPI's size.
    if (InpChannel->Ring.Base == NULL)
        *MaxFifoSize = DT_FIFO_SIZE_MAX;
    else
        *MaxFifoSize =
            (int)(FramesInRing(InpChannel) *
                  DtSdiFrameRawSize(&InpChannel->Layout, InpChannel->SymbolBits));
    OsMutexUnlock(InpChannel->Lock);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_GetFlags(DtInpChannel* InpChannel, int* Flags, int* Latched)
{
    if (InpChannel == NULL || Flags == NULL || Latched == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    *Flags = InpChannel->FifoOvf ? DTAPI_RX_FIFO_OVF : 0;
    *Latched = InpChannel->FifoOvfLatched ? DTAPI_RX_FIFO_OVF : 0;
    OsMutexUnlock(InpChannel->Lock);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtInpChannel::SetIoConfig's checks, then AsiSdiInpChannel_Bb2::SetIoConfig's. DTAPI
// also refuses a configuration the port lacks a capability for; here the driver does.
//
DtapiResult DtInpChannel_SetIoConfig(DtInpChannel* InpChannel, int Group, int Value,
                                     int SubValue)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = DtIoConfigIsValid(Group, Value, SubValue);
    if (Result != DTAPI_OK)
        return Result;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    if (Group == DTAPI_IOCONFIG_IOSTD &&
        (Value == DTAPI_IOCONFIG_6GSDI || Value == DTAPI_IOCONFIG_12GSDI))
    {
        Result = DTAPI_E_NOT_SUPPORTED;
    }
    else if (Group == DTAPI_IOCONFIG_IODIR && Value == DTAPI_IOCONFIG_OUTPUT)
        Result = DTAPI_E_INVALID_ARG;
    else if (Group == DTAPI_IOCONFIG_IODIR)
        Result = DTAPI_E_NOT_SUPPORTED;
    else if (InpChannel->RxControl != DTAPI_RXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else if (Group == DTAPI_IOCONFIG_IOSTD && Value == DTAPI_IOCONFIG_ASI)
        Result = DTAPI_E_NOT_SUPPORTED;
    else
    {
        DtIoConfig Config;
        Config.Port = InpChannel->Port;
        Config.Group = Group;
        Config.Value = Value;
        Config.SubValue = SubValue;
        Config.ParXtra[0] = Config.ParXtra[1] = -1;
        Result = DtPcieCmdSetIoConfig(InpChannel->Device.Drv, &Config);

        if (Result == DTAPI_OK && Group == DTAPI_IOCONFIG_IODOWNSCALE)
            InpChannel->Scale12GTo3G = Value == DTAPI_IOCONFIG_SCALE_12GTO3G;
        if (Result == DTAPI_OK && Group == DTAPI_IOCONFIG_IOSTD)
        {
            InpChannel->IoStdValue = Value;
            InpChannel->IoStdSubValue = SubValue;
            Result = ConfigureChannel(InpChannel);
        }
    }
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_SetRxControl(DtInpChannel* InpChannel, int RxControl)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->ChannelAttached ? SetRxControl(InpChannel, RxControl)
                                                     : DTAPI_E_NOT_INITIALIZED;
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetRxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtInpChannel::SetRxMode's checks, then SdiRxImpl_Bb2::SetRxMode's.
//
DtapiResult DtInpChannel_SetRxMode(DtInpChannel* InpChannel, int RxMode)
{
    DtapiResult Result = DTAPI_OK;

    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    // The old meaning of DTAPI_RXMODE_SDI alone is the full frame.
    if ((RxMode & DTAPI_RXMODE_SDI) != 0 && (RxMode & DTAPI_RXMODE_SDI_MODE_BITS) == 0)
        RxMode |= DTAPI_RXMODE_SDI_FULL;
    if ((RxMode & DT_RXMODE_TS) != 0 && (RxMode & DTAPI_RXMODE_SDI) != 0)
        return DTAPI_E_INVALID_MODE;
    if ((RxMode & DT_RXMODE_TIMESTAMP32) != 0 && (RxMode & DT_RXMODE_TIMESTAMP64) != 0 &&
        (RxMode & DT_RXMODE_TIMESTAMP_TOD) != 0)
    {
        return DTAPI_E_INVALID_MODE;
    }
    if ((RxMode & (DT_RXMODE_TIMESTAMP32 | DT_RXMODE_TIMESTAMP64 |
                   DT_RXMODE_TIMESTAMP_TOD)) != 0 &&
        (RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STRAW)
    {
        return DTAPI_E_INVALID_MODE;
    }
    if ((RxMode & DTAPI_RXMODE_SDI_MASK) == DTAPI_RXMODE_SDI_ACTVID &&
        (RxMode & DTAPI_RXMODE_SDI_HUFFMAN) != DTAPI_RXMODE_SDI_HUFFMAN)
    {
        return DTAPI_E_INVALID_MODE;
    }

    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    uint32_t Caps = InpChannel->Caps;
    if (((RxMode & DT_RXMODE_TS) == DT_RXMODE_TS && (Caps & DT_CAP_TS) == 0) ||
        ((RxMode & DT_RXMODE_TIMESTAMP64) == DT_RXMODE_TIMESTAMP64 &&
         (Caps & DT_CAP_TIMESTAMP64) == 0) ||
        ((RxMode & DTAPI_RXMODE_SDI_HUFFMAN) == DTAPI_RXMODE_SDI_HUFFMAN &&
         (Caps & DT_CAP_HUFFMAN) == 0) ||
        ((RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STL3 && (Caps & DT_CAP_L3MODE) == 0) ||
        ((RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STL3FULL &&
         (Caps & DT_CAP_L3MODE) == 0) ||
        ((RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STTRP &&
         (Caps & DT_CAP_TRPMODE) == 0) ||
        ((RxMode & DTAPI_RXMODE_SDI_10B_NBO) == DTAPI_RXMODE_SDI_10B_NBO &&
         (Caps & DT_CAP_SDI10BNBO) == 0))
    {
        Result = DTAPI_E_INVALID_MODE;
    }
    else if ((RxMode & DTAPI_RXMODE_SDI_MASK) != DTAPI_RXMODE_SDI_FULL)
        Result = DTAPI_E_INVALID_MODE;
    else if ((RxMode & (DT_RXMODE_TIMESTAMP32 | DT_RXMODE_TIMESTAMP64)) != 0)
        Result = DTAPI_E_INVALID_MODE;
    else if (InpChannel->RxControl != DTAPI_RXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else
    {
        InpChannel->RxMode = RxMode;
        InpChannel->SymbolBits = SymbolBitsOf(RxMode);
    }
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reading +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckBuffer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtInpChannel::ReadFrame's last checks of the buffer: DTAPI_E_BUF_TOO_SMALL when it
// cannot hold a frame of the channel's standard in its receive mode, and
// DTAPI_E_INVALID_SIZE for a frame larger than DTAPI's FIFO. Sets *RawSize to the size
// of that frame.
//
static DtapiResult CheckBuffer(const DtInpChannel* Chan, int FrameSize, size_t* RawSize)
{
    *RawSize = DtSdiFrameRawSize(&Chan->Layout, Chan->SymbolBits);
    if ((size_t)FrameSize < *RawSize)
        return DTAPI_E_BUF_TOO_SMALL;
    if (*RawSize > DT_FIFO_SIZE_MAX)
        return DTAPI_E_INVALID_SIZE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ReadFrame2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtInpChannel::ReadFrame's checks. Then, until a frame is taken, the time is up or the
// channel is being detached: take a frame if the ring holds one, and otherwise wait for
// the next format event without the lock, as ReadWithTimeOut releases its lock while it
// waits. An event out of sync discards everything written so far.
//
DtapiResult DtInpChannel_ReadFrame2(DtInpChannel* InpChannel, void* FrameBuffer,
                                    int* FrameSize, int TimeOut, DtTimeOfDay* ArrivalTime)
{
    uint64_t Start = OsMonotonicMs();
    DtTimeOfDay Arrival = {0, 0};

    if (ArrivalTime != NULL)
        *ArrivalTime = Arrival;
    if (InpChannel == NULL || FrameSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (*FrameSize == 0)
        return DTAPI_E_BUF_TOO_SMALL;
    if (TimeOut != -1 && TimeOut <= 0)
        return DTAPI_E_INVALID_TIMEOUT;
    if (*FrameSize < 0 || *FrameSize % 4 != 0)
        return DTAPI_E_INVALID_SIZE;
    if (FrameBuffer == NULL || (uintptr_t)FrameBuffer % 4 != 0)
        return DTAPI_E_INVALID_BUF;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;
    if (InpChannel->Detachers > 0)
    {
        OsMutexUnlock(InpChannel->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    size_t RawSize;
    DtapiResult Result = CheckBuffer(InpChannel, *FrameSize, &RawSize);

    InpChannel->Readers++;
    while (Result == DTAPI_OK)
    {
        bool Taken = false;

        // While this read waited without the lock, another thread may have stopped the
        // channel, changed its standard or receive mode, and started it again.
        if (InpChannel->RxControl != DTAPI_RXCTRL_IDLE)
        {
            Result = CheckBuffer(InpChannel, *FrameSize, &RawSize);
            if (Result == DTAPI_OK)
                Result = TakeFrame(InpChannel, (uint8_t*)FrameBuffer, &Arrival, &Taken);
            if (Result != DTAPI_OK || Taken)
                break;
        }

        uint64_t Elapsed = OsMonotonicMs() - Start;
        if (TimeOut != -1 && Elapsed >= (uint64_t)TimeOut)
        {
            Result = DTAPI_E_TIMEOUT;
            break;
        }
        int Wait = InpChannel->QuarterMs;
        if (TimeOut != -1 && (uint64_t)TimeOut - Elapsed < (uint64_t)Wait)
            Wait = (int)((uint64_t)TimeOut - Elapsed);

        if (InpChannel->RxControl == DTAPI_RXCTRL_IDLE)
        {
            OsMutexUnlock(InpChannel->Lock);
            OsSleepMs(Wait < DT_IDLE_POLL_MS ? Wait : DT_IDLE_POLL_MS);
            OsMutexLock(InpChannel->Lock);
        }
        else
        {
            OsDrv* Drv = InpChannel->Device.Drv;
            int Uuid = InpChannel->Uuid;
            int PortIndex = InpChannel->PortIndex;

            OsMutexUnlock(InpChannel->Lock);
            DtChSdiRxEvent Event;
            Result = DtPcieCmdChSdiRxWaitForFmtEvent(Drv, Uuid, PortIndex, Wait, &Event);
            OsMutexLock(InpChannel->Lock);

            if (Result == DTAPI_OK && !Event.InSync && InpChannel->Detachers == 0)
                Result = DiscardAll(InpChannel);
            else if (Result == DTAPI_E_TIMEOUT)
                Result = DTAPI_OK;
        }

        if (InpChannel->Detachers > 0)
            Result = DTAPI_E_CANCELLED;
    }
    InpChannel->Readers--;

    *FrameSize = Result == DTAPI_OK ? (int)RawSize : 0;
    if (ArrivalTime != NULL && Result == DTAPI_OK)
        *ArrivalTime = Arrival;
    OsMutexUnlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ReadFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_ReadFrame(DtInpChannel* InpChannel, void* FrameBuffer,
                                   int* FrameSize, int TimeOut)
{
    return DtInpChannel_ReadFrame2(InpChannel, FrameBuffer, FrameSize, TimeOut, NULL);
}

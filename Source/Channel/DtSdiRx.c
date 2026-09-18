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
#include "Device/DtAvInput.h"   // Detecting the signal's standard.
#include "Device/DtFunc.h"      // Finding the receive channel.
#include "DtPcieAbi.h"          // DT_FUNC_OPMODE_ and SDI rate values.
#include "DtSdiRx.h"            // Interface being implemented.
#include "OAL/OsThread.h"       // The process.
#include "Video/DtFrameProps.h" // The frame rate and geometry.
#include "Video/DtSdiFrame.h"   // The ring's format and the raw frame.
#include "Video/DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct DtSdiRx
{
    DtRx Base;
    DtPartRef Ch; // The receive channel
    bool Scale12GTo3G;

    int IoStdValue; // The port's I/O standard
    int IoStdSubValue;

    int SymbolBits; // 8, 10 or 16, from the receive mode
    bool FifoOvf;
    bool FifoOvfLatched;

    // The configured channel.
    bool ChannelAttached;
    DtSdiFrameLayout Layout;
    DtRing Ring;      // Base NULL without a ring
    bool RingMapped;  // Mapped by CDTAPI rather than by the driver
    uint8_t* LineBuf; // A coded line that runs across the end of the ring
    int QuarterMs;    // A quarter frame period, at least 1 ms

    // Reading.
    bool InSync;
    int ExpectedId;
} DtSdiRx;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrvOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static OsDrv* DrvOf(const DtSdiRx* Sdi)
{
    return Sdi->Base.Port.Device->Drv;
}

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
    size_t Coded = DtSdiFrame_CodedSize(Layout);
    size_t Raw = DtSdiFrame_RawSize(Layout, 10);
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
static size_t FramesInRing(const DtSdiRx* Sdi)
{
    return Sdi->Ring.MaxLoad / DtSdiFrame_CodedSize(&Sdi->Layout);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Unmaps the ring and detaches from the receive channel, ignoring failures, as a detach
// in DTAPI does.
//
static void ReleaseChannel(DtSdiRx* Sdi)
{
    DtPcieCmd_ChSdiRxUnmapDmaBuf(DrvOf(Sdi), Sdi->Ring.Base, (int)Sdi->Ring.Size,
                                 Sdi->RingMapped);
    memset(&Sdi->Ring, 0, sizeof(Sdi->Ring));
    Sdi->RingMapped = false;
    DtAlloc_Free(Sdi->LineBuf);
    Sdi->LineBuf = NULL;
    memset(&Sdi->Layout, 0, sizeof(Sdi->Layout));
    Sdi->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;

    if (Sdi->ChannelAttached)
    {
        DtPcieCmd_ChSdiRxSetOpMode(DrvOf(Sdi), Sdi->Ch, DT_FUNC_OPMODE_IDLE);
        DtPcieCmd_ChSdiRxDetach(DrvOf(Sdi), Sdi->Ch);
    }
    Sdi->ChannelAttached = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigureChannel -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches exclusively to the receive channel under DTAPI's friendly name, and
// configures and maps it for the port's I/O standard, as MxChannelMemlessRx::Attach and
// SetVidStd do. A channel already attached is released first.
//
static DtapiResult ConfigureChannel(DtSdiRx* Sdi)
{
    OsDrv* Drv = DrvOf(Sdi);
    const int PortIndex = Sdi->Base.Port.PortIndex;
    DtChSdiRxProps Props;
    uint8_t* Base = NULL;
    int BufSize = 0;
    int MaxLoad = 0;
    bool Mapped = false;

    memset(&Props, 0, sizeof(Props));
    ReleaseChannel(Sdi);

    // DtapiIoStd2VidStd: the sub-value of an SDI standard is its video standard.
    DtFrameProps Frame;
    if (!DtFrameProps_Init(&Frame, Sdi->IoStdSubValue))
        return DTAPI_E_INVALID_VIDSTD;

    // A read waits a quarter frame at a time, also on a channel without a ring.
    int Num;
    int Den;
    DtVidStd_Fps(Sdi->IoStdSubValue, &Num, &Den);
    Sdi->QuarterMs = Den * 1000 / Num / DT_FMT_EVENTS_PER_FRAME;
    if (Sdi->QuarterMs < 1)
        Sdi->QuarterMs = 1;

    // DtPalCHSDIRX::Attach: the process name and ID, cut to the longest name the driver
    // takes. A process without a name gets the library's.
    char Process[128];
    OsProcess_Name(Process, sizeof(Process));
    char Full[160];
    snprintf(Full, sizeof(Full), "%s:%" PRIu32, Process[0] != '\0' ? Process : "CDTAPI",
             OsProcess_Id());
    char Name[DT_CHAN_FRIENDLY_NAME_MAX_LENGTH + 1];
    memcpy(Name, Full, sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = '\0';

    DtapiResult Result = DtPcieCmd_ChSdiRxAttach(Drv, Sdi->Ch, true, Name);
    if (Result != DTAPI_OK)
        return Result;
    Sdi->ChannelAttached = true;

    Result = DtPcieCmd_ChSdiRxSetOpMode(Drv, Sdi->Ch, DT_FUNC_OPMODE_IDLE);

    // A 4K standard, which DTAPI's raw row does not take, attaches without a ring; see
    // SetRxControl.
    if (Result == DTAPI_OK &&
        (Sdi->IoStdValue == DTAPI_IOCONFIG_6GSDI ||
         Sdi->IoStdValue == DTAPI_IOCONFIG_12GSDI || DtVidStd_Is4k(Sdi->IoStdSubValue)))
    {
        return DTAPI_OK;
    }

    if (Result == DTAPI_OK)
        Result = DtPcieCmd_ChSdiRxGetProps(Drv, Sdi->Ch, &Props);
    if (Result == DTAPI_OK &&
        !DtSdiFrame_LayoutInit(&Sdi->Layout, Sdi->IoStdSubValue, Props.StreamAlignment))
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
    Config.DmaMinSize = RingSizeFor(&Sdi->Layout);
    Config.FmtIntInterval = (Den * 1000000 / Num) / DT_FMT_EVENTS_PER_FRAME;
    Config.FmtIntDelay = DT_FMT_EVENT_DELAY;
    Config.FmtNumIntsPerFrame = DT_FMT_EVENTS_PER_FRAME;
    Config.NumSymsHanc = Sdi->Layout.LineSymsHanc;
    Config.NumSymsVidVanc = Sdi->Layout.LineSymsVideo;
    Config.NumLines = Sdi->Layout.NumLines;
    Config.SdiRate = DtFrameProps_IsSd(&Frame)   ? DT_DRV_SDIRATE_SD
                     : DtFrameProps_Is3g(&Frame) ? DT_DRV_SDIRATE_3G
                                                 : DT_DRV_SDIRATE_HD;
    Config.AssumeInterlaced = DtFrameProps_IsInterlaced(&Frame);
    Config.Scale12GTo3G = Sdi->Scale12GTo3G;

    Result = DtPcieCmd_ChSdiRxConfigure(Drv, Sdi->Ch, &Config);
    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_ChSdiRxMapDmaBuf(Drv, Sdi->Ch, &Base, &BufSize, &MaxLoad, &Mapped);

    // The driver keeps a data word of the ring free; a maximum load that keeps nothing
    // free, or leaves no room, describes no ring that can be read.
    if (Result == DTAPI_OK &&
        (MaxLoad >= BufSize || DtRing_Init(&Sdi->Ring, Base, (size_t)BufSize,
                                           (size_t)(BufSize - MaxLoad)) != 0))
    {
        DtPcieCmd_ChSdiRxUnmapDmaBuf(Drv, Base, BufSize, Mapped);
        Result = DTAPI_E_DEV_DRIVER;
    }
    if (Result == DTAPI_OK)
    {
        Sdi->RingMapped = Mapped;
        Sdi->LineBuf = (uint8_t*)DtAlloc_Malloc((size_t)Sdi->Layout.Stride);
        if (Sdi->LineBuf == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }
    if (Result == DTAPI_OK && FramesInRing(Sdi) < DT_RING_MIN_FRAMES)
        Result = DTAPI_E_DEV_DRIVER;
    if (Result != DTAPI_OK)
    {
        ReleaseChannel(Sdi);
        return Result;
    }

    Sdi->InSync = false;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Advance -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Moves the read offset on by Bytes, which the ring holds, and tells the driver.
//
static DtapiResult Advance(DtSdiRx* Sdi, size_t Bytes)
{
    if (DtRing_Skip(&Sdi->Ring, Bytes) != 0)
        return DTAPI_E_INTERNAL;
    return DtPcieCmd_ChSdiRxSetReadOffset(DrvOf(Sdi), Sdi->Ch,
                                          (uint32_t)DtRing_ReadOffset(&Sdi->Ring));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DiscardTo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Everything written up to WriteOffset is skipped, to an aligned offset, as
// MxChannelMemlessRx::MarkAsProcessed(-1) does after an out-of-sync event.
//
static DtapiResult DiscardTo(DtSdiRx* Sdi, uint32_t WriteOffset)
{
    size_t Alignment = (size_t)Sdi->Layout.Alignment;
    size_t Aligned = (size_t)WriteOffset / Alignment * Alignment;

    Sdi->InSync = false;
    if (DtRing_Restart(&Sdi->Ring, Aligned) != 0 ||
        DtRing_SetWriteOffset(&Sdi->Ring, WriteOffset) != 0)
    {
        return DTAPI_E_DEV_DRIVER;
    }
    return DtPcieCmd_ChSdiRxSetReadOffset(DrvOf(Sdi), Sdi->Ch, (uint32_t)Aligned);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Brings the ring up to how far the channel has written. A write offset that would put
// more in the ring than the driver allows means the two disagree about where reading
// starts, and what the ring holds is discarded; one outside the ring is a driver fault.
//
static DtapiResult ReadWriteOffset(DtSdiRx* Sdi)
{
    uint32_t WriteOffset = 0;
    DtapiResult Result =
        DtPcieCmd_ChSdiRxGetWriteOffset(DrvOf(Sdi), Sdi->Ch, &WriteOffset);

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
        DtPcieCmd_ChSdiRxGetWriteOffset(DrvOf(Sdi), Sdi->Ch, &WriteOffset);

    if (Result != DTAPI_OK)
        return Result;
    if (WriteOffset >= Sdi->Ring.Size)
        return DTAPI_E_DEV_DRIVER;
    return DiscardTo(Sdi, WriteOffset);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Searches the data in the ring for a valid header of any frame, stepping by the
// alignment, as MxChannelMemlessRx::FindFrameHeader does. The positions searched without
// finding one are skipped. Returns true with the read offset at the header.
//
static bool FindHeader(DtSdiRx* Sdi, DtapiResult* Result)
{
    const DtSdiFrameLayout* Layout = &Sdi->Layout;
    size_t Available = DtRing_Load(&Sdi->Ring);

    *Result = DTAPI_OK;
    size_t Offset;
    for (Offset = 0; Offset + (size_t)Layout->HeaderBytes <= Available;
         Offset += (size_t)Layout->Alignment)
    {
        uint8_t Bytes[DT_SDIFRAME_HEADER_BYTES];
        DtRing_PeekAt(&Sdi->Ring, Offset, Bytes, sizeof(Bytes));
        DtSdiFrameHeader Header;
        DtSdiFrame_DecodeHeader(Bytes, &Header);
        if (DtSdiFrame_CheckHeader(Layout, &Header, -1) == DTAPI_OK)
        {
            *Result = Advance(Sdi, Offset);
            Sdi->InSync = *Result == DTAPI_OK;
            Sdi->ExpectedId = Header.FrameId;
            return Sdi->InSync;
        }
    }
    if (Offset > 0)
        *Result = Advance(Sdi, Offset);
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiRxImpl_Bb2::SetRxControl: anything but idle receives, and the value is kept as
// given. Receiving starts reading at the start of the ring, as DtPalCHSDIRX does. With
// 8-bit symbols or a 4K standard receiving fails as the Matrix's row validation fails it
// (MxPreProcess::ValidateRowConfigRaw accepts only 10- and 16-bit raw data of one logical
// link), before the channel runs.
//
static DtapiResult SetRxControl(DtSdiRx* Sdi, int RxControl)
{
    OsDrv* Drv = DrvOf(Sdi);

    if (Sdi->Base.RxControl == RxControl)
        return DTAPI_OK;

    DtapiResult Result;
    if (RxControl == DTAPI_RXCTRL_IDLE)
        Result = DtPcieCmd_ChSdiRxSetOpMode(Drv, Sdi->Ch, DT_FUNC_OPMODE_IDLE);
    else if (Sdi->SymbolBits == 8 || Sdi->Ring.Base == NULL)
        return DTAPI_E_CONFIG_RAW_SDI;
    else
    {
        DtRing_Restart(&Sdi->Ring, 0);
        Sdi->InSync = false;
        Result = DtPcieCmd_ChSdiRxSetReadOffset(Drv, Sdi->Ch, 0);
        if (Result == DTAPI_OK)
            Result = DtPcieCmd_ChSdiRxSetOpMode(Drv, Sdi->Ch, DT_FUNC_OPMODE_RUN);
    }
    if (Result != DTAPI_OK)
        return Result;

    Sdi->Base.RxControl = RxControl;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Release(DtRx* Rx)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    ReleaseChannel(Sdi);
    DtAlloc_Free(Sdi);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxModeSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SdiRxImpl_Bb2::SetRxMode: the full frame without time stamps, while idle.
//
static DtapiResult SetRxModeSdi(DtRx* Rx, int RxMode)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;

    if ((RxMode & DTAPI_RXMODE_SDI_MASK) != DTAPI_RXMODE_SDI_FULL)
        return DTAPI_E_INVALID_MODE;
    if ((RxMode & (DTAPI_RXMODE_TIMESTAMP32 | DTAPI_RXMODE_TIMESTAMP64)) != 0)
        return DTAPI_E_INVALID_MODE;
    if (Rx->RxControl != DTAPI_RXCTRL_IDLE)
        return DTAPI_E_NOT_IDLE;
    Rx->RxMode = RxMode;
    Sdi->SymbolBits = SymbolBitsOf(RxMode);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRxControlSdi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A channel that lost its receive channel, after an I/O standard that failed, receives
// nothing until one is set up.
//
static DtapiResult SetRxControlSdi(DtRx* Rx, int RxControl)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    return Sdi->ChannelAttached ? SetRxControl(Sdi, RxControl) : DTAPI_E_NOT_INITIALIZED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SdiRxImpl_Bb2::Reset for DTAPI_FIFO_RESET: stop, clear the flags, drop what is held.
//
static DtapiResult ClearFifo(DtRx* Rx)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    DtapiResult Result = SetRxControl(Sdi, DTAPI_RXCTRL_IDLE);

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
// The complete frames from the read offset on, counted also before a read has found the
// first header.
//
static DtapiResult GetFifoLoad(DtRx* Rx, int* FifoLoad)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;

    *FifoLoad = 0;
    if (Rx->RxControl != DTAPI_RXCTRL_RCV || Sdi->Ring.Base == NULL)
        return DTAPI_OK;
    DtapiResult Result = ReadWriteOffset(Sdi);
    if (Result == DTAPI_OK)
    {
        size_t Frames = DtRing_Load(&Sdi->Ring) / DtSdiFrame_CodedSize(&Sdi->Layout);
        *FifoLoad = (int)(Frames * DtSdiFrame_RawSize(&Sdi->Layout, Sdi->SymbolBits));
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The load GetFifoLoad reports for a full ring. A channel without a ring, on a 4K port,
// gives DTAPI's size.
//
static DtapiResult GetMaxFifoSize(DtRx* Rx, int* MaxFifoSize)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;

    if (Sdi->Ring.Base == NULL)
        *MaxFifoSize = DT_FIFO_SIZE_MAX;
    else
        *MaxFifoSize =
            (int)(FramesInRing(Sdi) * DtSdiFrame_RawSize(&Sdi->Layout, Sdi->SymbolBits));
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SdiRxImpl_Bb2::SetIoConfig once the configuration is set: the down-scaling is kept, and
// a new standard reconfigures the receive channel.
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
// SdiRxImpl_Bb2::DetectIoStd: the video standard, detected with the port's receiver and
// converted.
//
static DtapiResult DetectIoStd(DtRx* Rx, int* Value, int* SubValue)
{
    DtDetVidStd Info;
    DtAvInput Input;

    DtAvInput_SetUnknown(&Info);
    DtapiResult Result = DtAvInput_Attach(&Input, Rx->Port.Device, Rx->Port.Port);
    if (Result == DTAPI_OK)
        Result = DtAvInput_DetectVidStd(&Input, &Info);
    if (Result == DTAPI_OK)
        Result = DtapiVidStd2IoStd(Info.VidStd, Info.LinkStd, Value, SubValue);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtInpChannel::ReadFrame's last checks of the buffer: DTAPI_E_BUF_TOO_SMALL when it
// cannot hold a frame of the channel's standard in its receive mode, and
// DTAPI_E_INVALID_SIZE for a frame larger than DTAPI's FIFO. Sets *RawSize to the size
// of that frame.
//
static DtapiResult CheckFrame(DtRx* Rx, int FrameSize, size_t* RawSize)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;

    *RawSize = DtSdiFrame_RawSize(&Sdi->Layout, Sdi->SymbolBits);
    if ((size_t)FrameSize < *RawSize)
        return DTAPI_E_BUF_TOO_SMALL;
    if (*RawSize > DT_FIFO_SIZE_MAX)
        return DTAPI_E_INVALID_SIZE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Delivers the next frame into Buffer when the ring holds all of it, and the time of
// arrival its header gives into *ArrivalTime. Returns DTAPI_OK with *Taken true for a
// frame, DTAPI_OK with *Taken false when there is none yet, or a driver failure.
//
static DtapiResult TakeFrame(DtRx* Rx, uint8_t* Buffer, DtTimeOfDay* ArrivalTime,
                             bool* Taken)
{
    DtSdiRx* Sdi = (DtSdiRx*)Rx;
    const DtSdiFrameLayout* Layout = &Sdi->Layout;
    size_t Frame = DtSdiFrame_CodedSize(Layout);

    *Taken = false;
    DtapiResult Result = ReadWriteOffset(Sdi);
    if (Result != DTAPI_OK)
        return Result;
    size_t Available = DtRing_Load(&Sdi->Ring);

    // A ring that has filled up has lost data.
    if (Available + (size_t)Layout->Stride >= Sdi->Ring.MaxLoad)
    {
        Sdi->FifoOvf = true;
        Sdi->FifoOvfLatched = true;
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
        if (!Sdi->InSync)
        {
            if (!FindHeader(Sdi, &Result))
                return Result;
            Available = DtRing_Load(&Sdi->Ring);
        }
        if (Available < Frame)
            return DTAPI_OK;

        DtRing_PeekAt(&Sdi->Ring, 0, Bytes, sizeof(Bytes));
        DtSdiFrame_DecodeHeader(Bytes, &Header);
        if (DtSdiFrame_CheckHeader(Layout, &Header, Sdi->ExpectedId) == DTAPI_OK)
        {
            uint8_t First[DT_SDIFRAME_LINE_START_BYTES];

            DtRing_PeekAt(&Sdi->Ring, (size_t)Layout->HeaderBytes, First, sizeof(First));
            uint8_t Last[DT_SDIFRAME_LINE_START_BYTES];
            DtRing_PeekAt(&Sdi->Ring,
                          (size_t)Layout->HeaderBytes +
                              (size_t)(Layout->NumLines - 1) * (size_t)Layout->Stride,
                          Last, sizeof(Last));
            if (DtSdiFrame_CheckLines(Layout, First, Last) == DTAPI_OK)
                break;

            Result = Advance(Sdi, (size_t)Layout->Alignment);
            if (Result != DTAPI_OK)
                return Result;
            Available = DtRing_Load(&Sdi->Ring);
        }
        Sdi->InSync = false;
    }

    // A line that runs across the end of the ring is copied into one piece first.
    memset(Buffer, 0, DtSdiFrame_RawSize(Layout, Sdi->SymbolBits));
    for (int Line = 0; Line < Layout->NumLines; Line++)
    {
        size_t Offset =
            (size_t)Layout->HeaderBytes + (size_t)Line * (size_t)Layout->Stride;
        const uint8_t* Coded = DtRing_Span(&Sdi->Ring, Offset, (size_t)Layout->Stride);

        if (Coded == NULL)
        {
            DtRing_PeekAt(&Sdi->Ring, Offset, Sdi->LineBuf, (size_t)Layout->Stride);
            Coded = Sdi->LineBuf;
        }
        DtSdiFrame_ConvertLine(Layout, Sdi->SymbolBits, Coded, Line, Buffer);
    }

    Result = Advance(Sdi, Frame);
    if (Result != DTAPI_OK)
        return Result;

    if (Available - Frame + (size_t)Layout->Stride < Sdi->Ring.MaxLoad)
        Sdi->FifoOvf = false;
    Sdi->ExpectedId = (Header.FrameId + 1) & 0xFFFF;
    ArrivalTime->Seconds = Header.PtpSeconds;
    ArrivalTime->Nanoseconds = Header.PtpNanoseconds;
    *Taken = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrepareWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A read waits for the receive channel's next format event, a quarter frame at most.
//
static void PrepareWait(DtRx* Rx, DtRxWait* Wait)
{
    const DtSdiRx* Sdi = (const DtSdiRx*)Rx;

    memset(Wait, 0, sizeof(*Wait));
    Wait->Ops = Rx->Ops;
    Wait->Drv = DrvOf(Sdi);
    Wait->Part = Sdi->Ch;
    Wait->MaxMs = Sdi->QuarterMs;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Wait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits for the next format event, as ReadWithTimeOut waits without its lock. No event
// in time is no failure.
//
static DtapiResult Wait(DtRxWait* Wait, int Ms)
{
    DtChSdiRxEvent Event;
    DtapiResult Result =
        DtPcieCmd_ChSdiRxWaitForFmtEvent(Wait->Drv, Wait->Part, Ms, &Event);

    Wait->OutOfSync = Result == DTAPI_OK && !Event.InSync;
    return Result == DTAPI_E_TIMEOUT ? DTAPI_OK : Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AfterWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An event out of sync discards everything written so far.
//
static DtapiResult AfterWait(DtRx* Rx, const DtRxWait* Wait)
{
    return Wait->OutOfSync ? DiscardAll((DtSdiRx*)Rx) : DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const DtRxBackend g_Ops = {
    .Release = Release,
    .SetRxMode = SetRxModeSdi,
    .SetRxControl = SetRxControlSdi,
    .ClearFifo = ClearFifo,
    .ClearFlags = ClearFlags,
    .GetFlags = GetFlags,
    .GetFifoLoad = GetFifoLoad,
    .GetMaxFifoSize = GetMaxFifoSize,
    .ApplyIoConfig = ApplyIoConfig,
    .DetectIoStd = DetectIoStd,
    .CheckFrame = CheckFrame,
    .TakeFrame = TakeFrame,
    .PrepareWait = PrepareWait,
    .Wait = Wait,
    .AfterWait = AfterWait,
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiRx_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiRx_Attach(const DtRxPort* Port, const DtIoConfig* IoStd, DtRx** Rx)
{
    OsDrv* Drv = Port->Device->Drv;

    *Rx = NULL;
    DtSdiRx* Sdi = (DtSdiRx*)DtAlloc_Malloc(sizeof(DtSdiRx));
    if (Sdi == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Sdi, 0, sizeof(*Sdi));
    Sdi->Base.Ops = &g_Ops;
    Sdi->Base.Port = *Port;
    Sdi->IoStdValue = IoStd->Value;
    Sdi->IoStdSubValue = IoStd->SubValue;
    Sdi->Layout.VidStd = DTAPI_VIDSTD_UNKNOWN;

    // The receiver and the receive channel of the port's ASI/SDI receiver.
    DtFuncInstance Instance;
    DtapiResult Result = DtFunc_Find(Drv, Port->PortIndex, "AF_ASISDIRX", "", &Instance);
    if (Result != DTAPI_OK)
    {
        DtAlloc_Free(Sdi);
        return Result;
    }
    const DtFuncPart* SdiRx = DtFunc_Get(&Instance, true, DT_FUNC_TYPE_SDIRX, "");
    const DtFuncPart* ChSdiRx = DtFunc_Get(&Instance, true, DT_FUNC_TYPE_CHSDIRX, "");
    Result = SdiRx == NULL || ChSdiRx == NULL ? DTAPI_E_NOT_FOUND : DTAPI_OK;
    if (Result == DTAPI_OK)
        Result = DtFunc_CheckDriverVersion(&Port->Device->DriverVersion, true,
                                           DT_FUNC_TYPE_SDIRX);
    if (Result == DTAPI_OK)
        Result = DtFunc_CheckDriverVersion(&Port->Device->DriverVersion, true,
                                           DT_FUNC_TYPE_CHSDIRX);
    if (Result == DTAPI_OK)
        Sdi->Ch = ChSdiRx->Ref;
    DtFunc_Release(&Instance);
    if (Result != DTAPI_OK)
    {
        DtAlloc_Free(Sdi);
        return Result;
    }

    // Exclusive access: the down-scaling read, the default receive mode.
    if ((Port->Caps & DT_CAP_SCALE_12GTO3G) != 0)
    {
        DtIoConfig Scale = *IoStd;

        Scale.Group = DTAPI_IOCONFIG_IODOWNSCALE;
        Sdi->Scale12GTo3G = DtPcieCmd_GetIoConfig(Drv, &Scale) == DTAPI_OK &&
                            Scale.Value == DTAPI_IOCONFIG_SCALE_12GTO3G;
    }
    Sdi->Base.RxMode = DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B;
    Sdi->SymbolBits = 10;
    Sdi->Base.RxControl = DTAPI_RXCTRL_IDLE;

    // The I/O standard is applied again, and the receive channel set up for it.
    Result = DtPcieCmd_SetIoConfig(Drv, IoStd);
    if (Result == DTAPI_OK)
        Result = ConfigureChannel(Sdi);
    if (Result != DTAPI_OK)
    {
        Release(&Sdi->Base);
        return Result;
    }
    *Rx = &Sdi->Base;
    return DTAPI_OK;
}

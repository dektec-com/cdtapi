// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSimChSdiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The receive channel commands against the emulated channel and its source
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtFunc.h"                 // Finding the channel's UUID.
#include "DtPcieAbi.h"              // Types, commands and driver statuses.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/Sim/SimChSdiRx.h"     // The emulated channels and their controls.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "Video/DtFrameProps.h"     // Geometry for configurations.
#include "Video/DtSdiFrame.h"       // The frame format in the ring.
#include "cdtapi.h"                 // DTAPI_VIDSTD_ codes and results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The port the tests use: port 1, an input by default.
#define PORT 0

// A 64-KB ring, the smallest the channel takes.
#define SMALL_RING (16 * 4096)

typedef struct Fixture
{
    OsDrv* Drv;
    DtDrvObject Ch; // The receive channel
    int Live;
} Fixture;

// Opens the emulated device in its power-on state and finds the channel. Returns
// false, having recorded a failure, when that is not possible.
static bool Open(Fixture* Fix, int* DtFailures)
{
    DtFuncInstance Instance;
    const DtFuncObject* Object;

    SimDtPcie_Reset();
    Fix->Live = DtAlloc_NumLive();
    Fix->Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    memset(&Fix->Ch, 0, sizeof(Fix->Ch));
    if (Fix->Drv == NULL || !OsDrv_IsEmulated(Fix->Drv) ||
        DtFunc_Find(Fix->Drv, PORT, "AF_ASISDIRX", "", &Instance) != DTAPI_OK)
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Fix->Drv);
        return false;
    }
    Object = DtFunc_FindObject(&Instance, true, DT_FUNC_TYPE_CHSDIRX, "");
    if (Object != NULL)
        Fix->Ch = Object->Object;
    DtFunc_Release(&Instance);
    return Object != NULL;
}

// Closes the device and checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        OsDrv_Close((Fix).Drv);                                                          \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// A configuration for VidStd with a ring of at least RingSize bytes.
static DtChSdiRxConfig ConfigFor(int VidStd, int RingSize)
{
    DtChSdiRxConfig Config;

    memset(&Config, 0, sizeof(Config));
    DtFrameProps Props;
    DtFrameProps_Init(&Props, VidStd);
    Config.NumPorts = 1;
    Config.PortIndices[0] = PORT;
    Config.DmaMinSize = RingSize;
    Config.FmtIntInterval = 10000;
    Config.FmtIntDelay = 200;
    Config.FmtNumIntsPerFrame = 4;
    Config.NumSymsHanc = DtFrameProps_LineNumSymHancInclTiming(&Props);
    Config.NumSymsVidVanc = Props.LineNumSymActive;
    Config.NumLines = DtFrameProps_NumLines(&Props);
    Config.SdiRate = DtFrameProps_IsSd(&Props) ? DT_DRV_SDIRATE_SD : DT_DRV_SDIRATE_HD;
    Config.AssumeInterlaced = DtFrameProps_IsInterlaced(&Props);
    return Config;
}

// Attaches, configures for VidStd, maps and runs. Returns the ring, or NULL having
// recorded a failure.
static uint8_t* Run(Fixture* Fix, int VidStd, int RingSize, int* Size, int* MaxLoad,
                    int* DtFailures)
{
    DtChSdiRxConfig Config = ConfigFor(VidStd, RingSize);
    uint8_t* Ring = NULL;
    bool Mapped = false;

    if (DtPcieCmd_ChSdiRxAttach(Fix->Drv, Fix->Ch, true, "test:1") != DTAPI_OK ||
        DtPcieCmd_ChSdiRxConfigure(Fix->Drv, Fix->Ch, &Config) != DTAPI_OK ||
        DtPcieCmd_ChSdiRxMapDmaBuf(Fix->Drv, Fix->Ch, &Ring, Size, MaxLoad, &Mapped) !=
            DTAPI_OK ||
        DtPcieCmd_ChSdiRxSetReadOffset(Fix->Drv, Fix->Ch, 0) != DTAPI_OK ||
        DtPcieCmd_ChSdiRxSetOpMode(Fix->Drv, Fix->Ch, DT_FUNC_OPMODE_RUN) != DTAPI_OK)
    {
        printf("    FAIL: cannot start the channel\n");
        (*DtFailures)++;
        return NULL;
    }
    return Ring;
}

// Checks that the ring holds line Line of frame Frame of VidStd at Offset.
static bool LineAt(const uint8_t* Ring, size_t Offset, const DtSdiFrameLayout* Layout,
                   uint32_t Frame, int Line)
{
    uint16_t Symbols[SIM_RX_MAX_LINE_SYMBOLS];
    int Count = SimChSdiRx_Line(Layout->VidStd, Frame, Line, Symbols);
    int i;

    if (Count != Layout->LineNumSymsHanc + Layout->LineNumSymsActive)
        return false;
    for (i = 0; i < Count; i++)
    {
        bool Video = i >= Layout->LineNumSymsHanc;
        size_t Bit = (size_t)(Video ? i - Layout->LineNumSymsHanc : i) * 10;
        const uint8_t* Section = Ring + Offset + (Video ? Layout->SectionBytesHanc : 0);
        uint32_t Value =
            (uint32_t)(Section[Bit / 8] | Section[Bit / 8 + 1] << 8) >> (Bit % 8) & 0x3FF;

        if (Value != Symbols[i])
            return false;
    }
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Channel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What the DTA-2178's channel reported.
DT_TEST(ReportsTheCardsProperties)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DtChSdiRxProps Props;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetProps(Fix.Drv, Fix.Ch, &Props));
    DT_ASSERT_EQ(Props.DmaCaps, DT_CDMAC_CAP_RX | DT_CDMAC_CAP_TX);
    DT_ASSERT_EQ(Props.PrefetchSize, 16);
    DT_ASSERT_EQ(Props.PcieDataWidth, 256);
    DT_ASSERT_EQ(Props.ReorderBufSize, 8192);
    DT_ASSERT_EQ(Props.StreamAlignment, 128);

    SimDtPcie_SetRxAlignment(32);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetProps(Fix.Drv, Fix.Ch, &Props));
    DT_ASSERT_EQ(Props.StreamAlignment, 32);
    FINISH(Fix);
}

// Exclusive access excludes every other user, and a handle that is not a user is not
// found.
DT_TEST(AttachesUsers)
{
    Fixture Fix;
    int OpMode = -2;

    if (!Open(&Fix, DtFailures))
        return;
    OsDrv* Other = OsDrv_Open(SIM_DEVICE_INDEX);

    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxGetOpMode(Fix.Drv, Fix.Ch, &OpMode), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, ""), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, "test:1"));
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, "test:1"),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxAttach(Other, Fix.Ch, true, "other:2"), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxAttach(Other, Fix.Ch, false, "other:2"),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetOpMode(Fix.Drv, Fix.Ch, &OpMode));
    DT_ASSERT_EQ(OpMode, DT_FUNC_OPMODE_IDLE);

    DT_ASSERT_OK(DtPcieCmd_ChSdiRxDetach(Fix.Drv, Fix.Ch));
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxDetach(Fix.Drv, Fix.Ch), DTAPI_E_NOT_FOUND);

    // Shared users go together, and a handle that is a user already cannot attach again.
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, false, "test:1"));
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Other, Fix.Ch, false, "other:2"));
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxAttach(Other, Fix.Ch, false, "other:2"),
                 DTAPI_E_IN_USE);

    OsDrv_Close(Other);
    FINISH(Fix);
}

// Closing a handle detaches it, and the last user takes the ring along.
DT_TEST(ClosingDetaches)
{
    Fixture Fix;
    DtChSdiRxConfig Config = ConfigFor(DTAPI_VIDSTD_625I50, SMALL_RING);

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, "test:1"));
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config));
    SimRxState State;
    SimDtPcie_GetRxState(PORT, &State);
    DT_ASSERT(State.Configured);
    DT_ASSERT_EQ(State.NumUsers, 1);
    FINISH(Fix);

    SimDtPcie_GetRxState(PORT, &State);
    DT_ASSERT(!State.Configured);
    DT_ASSERT_EQ(State.NumUsers, 0);
}

// The ring is a multiple of 64 KB from 64 KB to 256 MB; outside that the channel ends up
// unconfigured. The same configuration again keeps the ring.
DT_TEST(ConfiguresTheRing)
{
    Fixture Fix;
    DtChSdiRxConfig Config = ConfigFor(DTAPI_VIDSTD_625I50, SMALL_RING + 1);
    uint8_t* Ring = NULL;
    uint8_t* Again = NULL;
    int Size = 0;
    int MaxLoad = 0;
    bool Mapped = true;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, "test:1"));

    DT_ASSERT_EQ(
        DtPcieCmd_ChSdiRxMapDmaBuf(Fix.Drv, Fix.Ch, &Ring, &Size, &MaxLoad, &Mapped),
        DTAPI_E_NOT_INITIALIZED);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config));
    DT_ASSERT_OK(
        DtPcieCmd_ChSdiRxMapDmaBuf(Fix.Drv, Fix.Ch, &Ring, &Size, &MaxLoad, &Mapped));
    DT_ASSERT(Ring != NULL);
    DT_ASSERT(!Mapped);
    DT_ASSERT_EQ(Size, 2 * SMALL_RING);
    DT_ASSERT_EQ(MaxLoad, 2 * SMALL_RING - 32);

    DT_ASSERT_OK(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config));
    DT_ASSERT_OK(
        DtPcieCmd_ChSdiRxMapDmaBuf(Fix.Drv, Fix.Ch, &Again, &Size, &MaxLoad, &Mapped));
    DT_ASSERT(Again == Ring);

    Config.DmaMinSize = SMALL_RING - 1;
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config),
                 DTAPI_E_INVALID_ARG);
    SimRxState State;
    SimDtPcie_GetRxState(PORT, &State);
    DT_ASSERT(!State.Configured);

    Config.DmaMinSize = 256 * 1024 * 1024 + 1;
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config),
                 DTAPI_E_INVALID_ARG);

    // A test limit shrinks the ring to a multiple of 64 KB.
    SimDtPcie_LimitRxRing(3 * SMALL_RING + 5);
    Config.DmaMinSize = 8 * 1024 * 1024;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config));
    SimDtPcie_GetRxState(PORT, &State);
    DT_ASSERT_EQ(State.RingSize, 3 * SMALL_RING);

    Config.NumPorts = 4;
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config),
                 DTAPI_E_NOT_SUPPORTED);
    FINISH(Fix);
}

// As on Linux the ring comes from mapping the port's segment of the device.
DT_TEST(MapsAsLinux)
{
    Fixture Fix;
    DtChSdiRxConfig Config = ConfigFor(DTAPI_VIDSTD_625I50, SMALL_RING);
    uint8_t* Ring = NULL;
    uint8_t* Again = NULL;
    int Size = 0;
    int MaxLoad = 0;
    bool Mapped = false;

    if (!Open(&Fix, DtFailures))
        return;
    SimDtPcie_MapRxRingAsLinux(true);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, "test:1"));
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config));
    DT_ASSERT_OK(
        DtPcieCmd_ChSdiRxMapDmaBuf(Fix.Drv, Fix.Ch, &Ring, &Size, &MaxLoad, &Mapped));
    DT_ASSERT(Ring != NULL);
    DT_ASSERT(Mapped);

    // Once mapped, the driver returns the address.
    DT_ASSERT_OK(
        DtPcieCmd_ChSdiRxMapDmaBuf(Fix.Drv, Fix.Ch, &Again, &Size, &MaxLoad, &Mapped));
    DT_ASSERT(Again == Ring);
    DT_ASSERT(!Mapped);
    DtPcieCmd_ChSdiRxUnmapDmaBuf(Fix.Drv, Ring, Size, true);

    // Only the port's segment with the ring's size maps.
    DT_ASSERT(OsDrv_MapMemory(Fix.Drv, 256ull * 1024 * 1024 * 2, (size_t)Size) == NULL);
    DT_ASSERT(OsDrv_MapMemory(Fix.Drv, 256ull * 1024 * 1024, (size_t)Size + 1) == NULL);
    DT_ASSERT(OsDrv_MapMemory(Fix.Drv, 0, (size_t)Size) == NULL);
    DT_ASSERT(OsDrv_MapMemory(Fix.Drv, 256ull * 1024 * 1024, (size_t)Size) == Ring);
    FINISH(Fix);
}

// Only configured channels run, and waits need a running channel.
DT_TEST(RunsWhenConfigured)
{
    Fixture Fix;
    DtChSdiRxConfig Config = ConfigFor(DTAPI_VIDSTD_625I50, SMALL_RING);
    uint32_t Offset = 1;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxAttach(Fix.Drv, Fix.Ch, true, "test:1"));
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxSetOpMode(Fix.Drv, Fix.Ch, DT_FUNC_OPMODE_IDLE));
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxSetOpMode(Fix.Drv, Fix.Ch, DT_FUNC_OPMODE_RUN),
                 DTAPI_E_NOT_INITIALIZED);
    DtChSdiRxEvent Event;
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 0, &Event),
                 DTAPI_E_NOT_INITIALIZED);
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset),
                 DTAPI_E_NOT_INITIALIZED);

    DT_ASSERT_OK(DtPcieCmd_ChSdiRxConfigure(Fix.Drv, Fix.Ch, &Config));
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 0, &Event),
                 DTAPI_E_TIMEOUT);
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxSetOpMode(Fix.Drv, Fix.Ch, 7), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxSetOpMode(Fix.Drv, Fix.Ch, DT_FUNC_OPMODE_RUN));
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 0, &Event));
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
    DT_ASSERT_EQ(Offset, 0);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Without a source the events go on, out of sync, and nothing is written.
DT_TEST(EventsWithoutSource)
{
    Fixture Fix;
    int Size;
    int MaxLoad;
    uint32_t Offset = 1;

    if (!Open(&Fix, DtFailures) ||
        Run(&Fix, DTAPI_VIDSTD_625I50, SMALL_RING, &Size, &MaxLoad, DtFailures) == NULL)
    {
        return;
    }
    DtChSdiRxEvent Event;
    for (int i = 0; i < 8; i++)
    {
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
        DT_ASSERT_EQ(Event.FrameId, i / 4);
        DT_ASSERT_EQ(Event.SeqNumber, i % 4);
        DT_ASSERT(!Event.InSync);
    }
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
    DT_ASSERT_EQ(Offset, 0);
    FINISH(Fix);
}

// With a source a frame arrives in four quarters: its header and its lines, in order.
DT_TEST(WritesFramesInQuarters)
{
    Fixture Fix;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures))
        return;
    SimDtPcie_SetRxSource(PORT, DTAPI_VIDSTD_625I50);
    int Size;
    int MaxLoad;
    const uint8_t* Ring =
        Run(&Fix, DTAPI_VIDSTD_625I50, 4 * 1024 * 1024, &Size, &MaxLoad, DtFailures);
    if (Ring == NULL)
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));

    DtChSdiRxEvent Event;
    for (int Quarter = 0; Quarter < 4; Quarter++)
    {
        int Lines = (625 * (Quarter + 1) + 3) / 4;

        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
        DT_ASSERT_EQ(Event.FrameId, 0);
        DT_ASSERT_EQ(Event.SeqNumber, Quarter);
        DT_ASSERT(Event.InSync);
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
        DT_ASSERT_EQ(Offset, 16 + Lines * Layout.RxStride);
    }

    DtSdiFrameRxHeader Header;
    DtSdiFrame_DecodeRxHeader(Ring, &Header);
    DT_ASSERT_OK(DtSdiFrame_CheckRxHeader(&Layout, &Header, 0));
    DT_ASSERT_EQ(Header.PtpSeconds, 0);
    DT_ASSERT(LineAt(Ring, 16, &Layout, 0, 1));
    DT_ASSERT(LineAt(Ring, 16 + 312 * (size_t)Layout.RxStride, &Layout, 0, 313));
    DT_ASSERT(LineAt(Ring, 16 + 624 * (size_t)Layout.RxStride, &Layout, 0, 625));

    // The next frame follows directly.
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT_EQ(Event.FrameId, 1);
    DtSdiFrame_DecodeRxHeader(Ring + Offset, &Header);
    DT_ASSERT_OK(DtSdiFrame_CheckRxHeader(&Layout, &Header, 1));
    DT_ASSERT(LineAt(Ring, Offset + 16, &Layout, 1, 1));
    FINISH(Fix);
}

// A source whose geometry the channel is not configured for is out of sync.
DT_TEST(MismatchedSourceIsOutOfSync)
{
    Fixture Fix;
    uint32_t Offset = 1;

    if (!Open(&Fix, DtFailures))
        return;
    SimDtPcie_SetRxSource(PORT, DTAPI_VIDSTD_525I59_94);
    int Size;
    int MaxLoad;
    if (Run(&Fix, DTAPI_VIDSTD_625I50, SMALL_RING, &Size, &MaxLoad, DtFailures) == NULL)
        return;
    DtChSdiRxEvent Event;
    for (int i = 0; i < 4; i++)
    {
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
        DT_ASSERT(!Event.InSync);
    }
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
    DT_ASSERT_EQ(Offset, 0);
    FINISH(Fix);
}

// The faults each change the next frame, once.
DT_TEST(InjectsFaults)
{
    Fixture Fix;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures))
        return;
    SimDtPcie_SetRxSource(PORT, DTAPI_VIDSTD_625I50);
    int Size;
    int MaxLoad;
    const uint8_t* Ring =
        Run(&Fix, DTAPI_VIDSTD_625I50, 16 * 1024 * 1024, &Size, &MaxLoad, DtFailures);
    if (Ring == NULL)
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));

    SimDtPcie_InjectRxFault(PORT, SIM_RX_FAULT_SYNC_WORD);
    SimDtPcie_InjectRxFault(PORT, SIM_RX_FAULT_SKIP_FRAME);
    DtChSdiRxEvent Event;
    int i;
    for (i = 0; i < 4; i++)
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT_EQ(Event.FrameId, 1);
    DtSdiFrameRxHeader Header;
    DtSdiFrame_DecodeRxHeader(Ring, &Header);
    DT_ASSERT_EQ(DtSdiFrame_CheckRxHeader(&Layout, &Header, 1), DTAPI_E_OUT_OF_SYNC);

    SimDtPcie_InjectRxFault(PORT, SIM_RX_FAULT_FORMAT);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
    for (i = 0; i < 4; i++)
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT_EQ(Event.FrameId, 2);
    DtSdiFrame_DecodeRxHeader(Ring + Offset, &Header);
    DT_ASSERT_EQ(DtSdiFrame_CheckRxHeader(&Layout, &Header, 2), DTAPI_E_INVALID_FORMAT);

    SimDtPcie_InjectRxFault(PORT, SIM_RX_FAULT_OUT_OF_SYNC);
    uint32_t Before;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Before));
    for (i = 0; i < 4; i++)
    {
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
        DT_ASSERT(!Event.InSync);
    }
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
    DT_ASSERT_EQ(Offset, Before);

    // And the frame after them is whole again.
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT(Event.InSync);
    DT_ASSERT_EQ(Event.FrameId, 4);
    DtSdiFrame_DecodeRxHeader(Ring + Offset, &Header);
    DT_ASSERT_OK(DtSdiFrame_CheckRxHeader(&Layout, &Header, 4));
    FINISH(Fix);
}

// A frame that does not fit is cut off and out of sync; reading makes room again, and the
// ring wraps.
DT_TEST(FullRingDropsAndWraps)
{
    Fixture Fix;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures))
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));
    size_t Frame = DtSdiFrame_RxCodedSize(&Layout);
    SimDtPcie_SetRxSource(PORT, DTAPI_VIDSTD_625I50);
    SimDtPcie_LimitRxRing(Frame + Frame / 2);
    int Size;
    int MaxLoad;
    const uint8_t* Ring =
        Run(&Fix, DTAPI_VIDSTD_625I50, 8 * 1024 * 1024, &Size, &MaxLoad, DtFailures);
    if (Ring == NULL)
        return;
    DT_ASSERT((size_t)Size < 2 * Frame);

    // The first frame fits, the second not.
    DtChSdiRxEvent Event;
    int i;
    for (i = 0; i < 4; i++)
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT(Event.InSync);
    for (i = 0; i < 4; i++)
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT(!Event.InSync);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &Offset));
    DT_ASSERT((size_t)Offset <= (size_t)MaxLoad);

    // Read everything, and the third frame wraps around the end.
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxSetReadOffset(Fix.Drv, Fix.Ch, Offset));
    for (i = 0; i < 4; i++)
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxWaitForFmtEvent(Fix.Drv, Fix.Ch, 1, &Event));
    DT_ASSERT(Event.InSync);
    DT_ASSERT_EQ(Event.FrameId, 2);
    {
        uint32_t End = 0;
        DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetWriteOffset(Fix.Drv, Fix.Ch, &End));
        DT_ASSERT_EQ(End, (Offset + Frame) % (size_t)Size);
        DT_ASSERT(End < Offset);
    }
    FINISH(Fix);
}

// A refused command, and the channel's status is its receiver's, carrier included.
DT_TEST(RefusesAndReportsStatus)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    SimDtPcie_FailRxCmd(DT_CHSDIRX_CMD_GET_PROPS, DT_STATUS_NOT_SUPPORTED);
    DtChSdiRxProps Props;
    DT_ASSERT_EQ(DtPcieCmd_ChSdiRxGetProps(Fix.Drv, Fix.Ch, &Props),
                 DTAPI_E_NOT_SUPPORTED);
    SimDtPcie_FailRxCmd(DT_CHSDIRX_CMD_GET_PROPS, 0);
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetProps(Fix.Drv, Fix.Ch, &Props));

    SimSdiSignal Signal;
    memset(&Signal, 0, sizeof(Signal));
    Signal.CarrierDetect = 1;
    Signal.SdiLock = 1;
    Signal.Valid = 1;
    Signal.NumLinesF1 = 312;
    Signal.SdiRate = DT_DRV_SDIRATE_SD;
    SimDtPcie_SetSdiSignal(PORT, &Signal);
    DtSdiRxStatus Status;
    DT_ASSERT_OK(DtPcieCmd_ChSdiRxGetSdiStatus(Fix.Drv, Fix.Ch, &Status));
    DT_ASSERT(Status.CarrierDetect);
    DT_ASSERT(Status.SdiLock);
    DT_ASSERT(Status.Valid);
    DT_ASSERT_EQ(Status.NumLinesF1, 312);
    DT_ASSERT_EQ(Status.SdiRate, DT_DRV_SDIRATE_SD);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbols +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// SD's timing references on the first and last line, as a frame check expects them.
DT_TEST(SdTimingReferences)
{
    uint16_t Symbols[SIM_RX_MAX_LINE_SYMBOLS];

    int Count = SimChSdiRx_Line(DTAPI_VIDSTD_625I50, 3, 1, Symbols);
    DT_ASSERT_EQ(Count, 1728);
    DT_ASSERT_EQ(Symbols[0], 0x3FF);
    DT_ASSERT_EQ(Symbols[1], 0x000);
    DT_ASSERT_EQ(Symbols[2], 0x000);
    DT_ASSERT_EQ(Symbols[3], 0x2D8);
    DT_ASSERT_EQ(Symbols[284], 0x3FF);
    DT_ASSERT_EQ(Symbols[287], 0x2AC);
    int i;
    for (i = 4; i < 284; i++)
        DT_ASSERT(Symbols[i] >= 0x040 && Symbols[i] <= 0x3BF);
    for (i = 288; i < Count; i++)
        DT_ASSERT(Symbols[i] >= 0x040 && Symbols[i] <= 0x3BF);

    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_625I50, 3, 625, Symbols), 1728);
    DT_ASSERT_EQ(Symbols[3], 0x3C4);
    // Line 100 of field 1 carries video.
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_625I50, 3, 100, Symbols), 1728);
    DT_ASSERT_EQ(Symbols[3], 0x274);
}

// HD's timing references carry the line number and a CRC per channel.
DT_TEST(HdTimingReferences)
{
    uint16_t Line1[SIM_RX_MAX_LINE_SYMBOLS];

    int Count = SimChSdiRx_Line(DTAPI_VIDSTD_1080I50, 9, 1, Line1);
    DT_ASSERT_EQ(Count, 5280);
    uint16_t Again[SIM_RX_MAX_LINE_SYMBOLS];
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_1080I50, 9, 1, Again), Count);
    DT_ASSERT_MEM(Line1, Again, sizeof(uint16_t) * (size_t)Count);
    for (int c = 0; c < 2; c++)
    {
        DT_ASSERT_EQ(Line1[0 + c], 0x3FF);
        DT_ASSERT_EQ(Line1[2 + c], 0x000);
        DT_ASSERT_EQ(Line1[4 + c], 0x000);
        DT_ASSERT_EQ(Line1[6 + c], 0x2D8);
        DT_ASSERT_EQ(Line1[8 + c], 0x204);
        DT_ASSERT_EQ(Line1[10 + c], 0x200);
        DT_ASSERT_EQ((Line1[12 + c] >> 9 & 1) ^ (Line1[12 + c] >> 8 & 1), 1);
        DT_ASSERT_EQ((Line1[14 + c] >> 9 & 1) ^ (Line1[14 + c] >> 8 & 1), 1);
        DT_ASSERT_EQ(Line1[1432 + c], 0x3FF);
        DT_ASSERT_EQ(Line1[1438 + c], 0x2AC);
    }
    // The two channels' CRCs differ, and a line number above 127 uses LN1.
    DT_ASSERT(Line1[12] != Line1[13] || Line1[14] != Line1[15]);
    uint16_t Line2[SIM_RX_MAX_LINE_SYMBOLS];
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_1080I50, 9, 200, Line2), Count);
    DT_ASSERT_EQ(Line2[8], 0x120);
    DT_ASSERT_EQ(Line2[10], 0x204);
}

// A 4K line is the four links' lines word by word, links 4, 2, 3 and 1, each link
// carrying the line of a frame number of its own.
DT_TEST(FourKLine)
{
    static uint16_t Raw[21120];
    static uint16_t Links[4][SIM_RX_MAX_LINE_SYMBOLS];
    static const int Order[4] = {3, 1, 2, 0};

    int Count = SimChSdiRx_Line(DTAPI_VIDSTD_2160P50, 7, 42, Raw);
    DT_ASSERT_EQ(Count, 4 * 5280);
    for (int L = 0; L < 4; L++)
        DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_1080P50, 7 + (uint32_t)L, 42, Links[L]),
                     5280);

    for (int p = 0; p < 5280 / 2; p++)
    {
        for (int i = 0; i < 4; i++)
        {
            DT_ASSERT_EQ(Raw[8 * p + i], Links[Order[i]][2 * p]);
            DT_ASSERT_EQ(Raw[8 * p + 4 + i], Links[Order[i]][2 * p + 1]);
        }
    }
    // Every link's line starts with its own EAV, and the links differ.
    DT_ASSERT_EQ(Raw[0], 0x3FF);
    DT_ASSERT_EQ(Raw[3], 0x3FF);
    DT_ASSERT(Links[0][100] != Links[1][100]);
}

DT_TEST(LineRefusesWhatIsNot)
{
    uint16_t Symbols[SIM_RX_MAX_LINE_SYMBOLS];

    Symbols[0] = 0x123;
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_2160P50B, 0, 1, Symbols), 0);
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_UNKNOWN, 0, 1, Symbols), 0);
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_625I50, 0, 0, Symbols), 0);
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_625I50, 0, 626, Symbols), 0);
    DT_ASSERT_EQ(Symbols[0], 0x123);
    DT_ASSERT_EQ(SimChSdiRx_Line(DTAPI_VIDSTD_720P23_98, 0, 750, Symbols), 8250);
}

DT_TEST_MAIN("SimChSdiRx", DT_RUN(ReportsTheCardsProperties), DT_RUN(AttachesUsers),
             DT_RUN(ClosingDetaches), DT_RUN(ConfiguresTheRing), DT_RUN(MapsAsLinux),
             DT_RUN(RunsWhenConfigured), DT_RUN(EventsWithoutSource),
             DT_RUN(WritesFramesInQuarters), DT_RUN(MismatchedSourceIsOutOfSync),
             DT_RUN(InjectsFaults), DT_RUN(FullRingDropsAndWraps),
             DT_RUN(RefusesAndReportsStatus), DT_RUN(SdTimingReferences),
             DT_RUN(HdTimingReferences), DT_RUN(FourKLine), DT_RUN(LineRefusesWhatIsNot))

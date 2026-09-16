// #*#*#*#*#*#*#*#*#*#*#*#*# TestSimInpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtInpChannel against the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open. The frames a channel
// reads are compared with frames built here, bit by bit, from the symbols the emulator's
// source sends.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // Public API under test.
#include "Core/DtAlloc.h"           // Live allocations and allocation failures.
#include "Device/DtDevice.h"        // The device object, to change its firmware status.
#include "DtDrvAbi.h"               // Driver statuses and commands.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles.
#include "OAL/OsThread.h"           // A reader on another thread, and the clock.
#include "OAL/Sim/SimChSdiRx.h"     // The emulated receive channels.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "Video/DtSdiFrame.h"       // Frame sizes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Port 1, an input by default, and port 2, an output by default.
#define PORT 1
#define PORT_OUTPUT 2

// Large enough for a 1080-line frame with 16-bit symbols.
#define BUFFER_SIZE (16 * 1024 * 1024)

typedef struct Fixture
{
    long Live;
    DtDevice* Device;
    DtInpChannel* Channel;
    char* Buffer; // BUFFER_SIZE bytes, aligned to 8
} Fixture;

// Resets the emulator, attaches a device object and allocates a channel and a buffer.
// Returns false, having recorded a failure, when that is not possible.
static bool Start(Fixture* Fix, int* DtFailures)
{
    OsDrv* Drv;

    SimDtPcieReset();
    Fix->Live = DtAllocLive();
    Fix->Device = NULL;
    Fix->Channel = NULL;
    Fix->Buffer = NULL;

    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrvClose(Drv);
        return false;
    }
    OsDrvClose(Drv);

    Fix->Device = DtDevice_Alloc();
    Fix->Channel = DtInpChannel_Alloc();
    Fix->Buffer = (char*)malloc(BUFFER_SIZE);
    if (Fix->Device == NULL || Fix->Channel == NULL || Fix->Buffer == NULL ||
        DtDevice_AttachToSerial(Fix->Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot set up\n");
        (*DtFailures)++;
        return false;
    }
    return true;
}

// Frees what Start made and checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtInpChannel_Free((Fix).Channel);                                                \
        DtDevice_Free((Fix).Device);                                                     \
        free((Fix).Buffer);                                                              \
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);                                         \
        DT_ASSERT_EQ(DtAllocLive(), (Fix).Live);                                         \
    } while (0)

// Sets the I/O standard of the port at Port to VidStd.
static unsigned int SetStandard(Fixture* Fix, int Port, int VidStd)
{
    int Value, SubValue;
    unsigned int Result = DtapiVidStd2IoStd(VidStd, -1, &Value, &SubValue);

    if (Result != DTAPI_OK)
        return Result;
    return DtDevice_SetIoConfig(Fix->Device, Port, DTAPI_IOCONFIG_IOSTD, Value, SubValue);
}

// Builds frame FrameNumber of VidStd as a raw frame with Bits bits per symbol, into a new
// buffer of *Size bytes.
static uint8_t* ExpectedFrame(int VidStd, uint32_t FrameNumber, int Bits, size_t* Size)
{
    DtSdiFrameLayout Layout;
    uint16_t Symbols[8250];
    uint8_t* Frame;
    size_t Symbol = 0;
    int Line;

    *Size = 0;
    if (!DtSdiFrameLayoutInit(&Layout, VidStd, 32))
        return NULL;
    *Size = DtSdiFrameRawSize(&Layout, Bits);
    Frame = (uint8_t*)calloc(*Size, 1);
    if (Frame == NULL)
        return NULL;

    for (Line = 1; Line <= Layout.NumLines; Line++)
    {
        int Count = SimChSdiRxLine(VidStd, FrameNumber, Line, Symbols);
        int i;

        for (i = 0; i < Count; i++, Symbol++)
        {
            unsigned Value = Symbols[i];
            int b;

            if (Bits == 8)
                Frame[Symbol] = (uint8_t)(Value >> 2);
            else if (Bits == 16)
            {
                Frame[2 * Symbol] = (uint8_t)Value;
                Frame[2 * Symbol + 1] = (uint8_t)(Value >> 8);
            }
            else
            {
                for (b = 0; b < 10; b++)
                {
                    if (Value >> b & 1)
                        Frame[(Symbol * 10 + (size_t)b) / 8] |=
                            (uint8_t)(1u << ((Symbol * 10 + (size_t)b) % 8));
                }
            }
        }
    }
    return Frame;
}

// Reads a frame and checks that it is frame FrameNumber of VidStd with Bits bits per
// symbol. Returns false, having recorded a failure, when it is not.
static bool ReadsFrame(Fixture* Fix, int VidStd, uint32_t FrameNumber, int Bits,
                       int* DtFailures)
{
    size_t Size;
    uint8_t* Expected = ExpectedFrame(VidStd, FrameNumber, Bits, &Size);
    int FrameSize = BUFFER_SIZE;
    unsigned int Result =
        DtInpChannel_ReadFrame(Fix->Channel, Fix->Buffer, &FrameSize, 2000);
    bool Same = Result == DTAPI_OK && Expected != NULL && (size_t)FrameSize == Size &&
                memcmp(Fix->Buffer, Expected, Size) == 0;

    if (!Same)
    {
        printf("    FAIL: standard %d, frame %u, %d bits: %s, %d bytes\n", VidStd,
               FrameNumber, Bits, DtapiResult2Str(Result), FrameSize);
        (*DtFailures)++;
    }
    free(Expected);
    return Same;
}

// Attaches the channel to PORT for VidStd with a source of that standard, and starts
// receiving. Returns false, having recorded a failure, when that fails.
static bool Receive(Fixture* Fix, int VidStd, int RxMode, int* DtFailures)
{
    unsigned int Result = SetStandard(Fix, PORT, VidStd);

    SimDtPcieSetRxSource(PORT - 1, VidStd);
    if (Result == DTAPI_OK)
        Result = DtInpChannel_AttachToPort(Fix->Channel, Fix->Device, PORT);
    if (Result == DTAPI_OK)
        Result = DtInpChannel_SetRxMode(Fix->Channel, RxMode);
    if (Result == DTAPI_OK)
        Result = DtInpChannel_SetRxControl(Fix->Channel, DTAPI_RXCTRL_RCV);
    if (Result != DTAPI_OK)
    {
        printf("    FAIL: cannot receive: %s\n", DtapiResult2Str(Result));
        (*DtFailures)++;
    }
    return Result == DTAPI_OK;
}

// The symbol size of a receive mode.
static int BitsOf(int RxMode)
{
    return (RxMode & DTAPI_RXMODE_SDI_10B) != 0   ? 10
           : (RxMode & DTAPI_RXMODE_SDI_16B) != 0 ? 16
                                                  : 8;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(NullAndDetached)
{
    Fixture Fix;
    DtInpChannel* Freed;
    int A = 0, B = 0;
    char Frame[8];
    int Size = 8;

    DtInpChannel_Free(NULL);
    DtInpChannel_Freep(NULL);
    if (!Start(&Fix, DtFailures))
        return;

    DT_ASSERT_EQ(DtInpChannel_AttachToPort(NULL, Fix.Device, PORT), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_ClearFifo(NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_ClearFlags(NULL, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_Detach(NULL, 0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(NULL, &A, &B), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(Fix.Channel, NULL, &B), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(Fix.Channel, &A, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetFifoLoad(NULL, &A), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetFifoLoad(Fix.Channel, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetMaxFifoSize(NULL, &A), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetMaxFifoSize(Fix.Channel, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetFlags(NULL, &A, &B), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetFlags(Fix.Channel, NULL, &B), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_GetFlags(Fix.Channel, &A, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(NULL, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(NULL, DTAPI_RXCTRL_RCV), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(NULL, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(NULL, Frame, &Size, 10), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Frame, NULL, 10),
                 DTAPI_E_INVALID_ARG);

    DT_ASSERT_EQ(DtInpChannel_ClearFifo(Fix.Channel), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_ClearFlags(Fix.Channel, -1), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_Detach(Fix.Channel, 0), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(Fix.Channel, &A, &B), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_GetFifoLoad(Fix.Channel, &A), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_GetMaxFifoSize(Fix.Channel, &A), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_GetFlags(Fix.Channel, &A, &B), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Frame, &Size, 10),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(Size, 8);

    Freed = DtInpChannel_Alloc();
    DT_ASSERT(Freed != NULL);
    DtInpChannel_Freep(&Freed);
    DT_ASSERT(Freed == NULL);
    FINISH(Fix);
}

// The checks of DtInpChannel::AttachToPort and the input channel, in their order.
DT_TEST(AttachChecks)
{
    Fixture Fix;
    DtDevice* Detached;
    DtInpChannel* Second;

    if (!Start(&Fix, DtFailures))
        return;
    Detached = DtDevice_Alloc();
    Second = DtInpChannel_Alloc();

    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, NULL, PORT), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Detached, PORT), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, 0),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, SIM_PORT_COUNT + 1),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, 9),
                 DTAPI_E_NO_DT_INPUT);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT_OUTPUT),
                 DTAPI_E_NO_DT_INPUT);

    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Second, Fix.Device, PORT), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 2);

    // Another port works alongside.
    DT_ASSERT_OK(DtDevice_SetToInput(Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Second, Fix.Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtInpChannel_Detach(Second, 0));
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    DT_ASSERT_EQ(DtInpChannel_Detach(Fix.Channel, 0), DTAPI_E_NOT_ATTACHED);

    DtInpChannel_Free(Second);
    DtDevice_Free(Detached);
    FINISH(Fix);
}

// The firmware status, a port without an ASI/SDI receiver or with the old Matrix API, and
// a driver too old for the receive channel.
DT_TEST(AttachRefusals)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;

    Fix.Device->Info.FirmwareStatus = DT_FWSTATUS_OBSOLETE;
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_OBSOLETE_FW);
    Fix.Device->Info.FirmwareStatus = DT_FWSTATUS_TAINTED;
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_TAINTED_FW);
    Fix.Device->Info.FirmwareStatus = DT_FWSTATUS_UPTODATE;

    SimDtPcieOverrideProperty("CAP_ASI", PORT - 1, true, 0);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_NOT_SUPPORTED);

    SimDtPcieReset();
    SimDtPcieOverrideProperty("CAP_MATRIX", PORT - 1, true, 1);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_NOT_SUPPORTED);

    SimDtPcieReset();
    SimDtPcieSetDriverVersion(2, 0, 2, 327);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_DRIVER_INCOMP);

    SimDtPcieReset();
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_OK(DtDevice_SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                                      DTAPI_IOCONFIG_ASI, -1));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 1);
    FINISH(Fix);
}

// A refused receive-channel command fails the attach and leaves nothing behind.
DT_TEST(AttachCleansUpAfterFailures)
{
    static const int Commands[] = {DT_CHSDIRX_CMD_ATTACH, DT_CHSDIRX_CMD_GET_PROPS,
                                   DT_CHSDIRX_CMD_CONFIGURE,
                                   DT_CHSDIRX_CMD_MAP_DMA_BUF_TO_USER};
    Fixture Fix;
    SimRxState State;
    size_t i;

    if (!Start(&Fix, DtFailures))
        return;

    for (i = 0; i < sizeof(Commands) / sizeof(Commands[0]); i++)
    {
        SimDtPcieFailRxCmd(Commands[i], DT_STATUS_NOT_SUPPORTED);
        DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                     DTAPI_E_NOT_SUPPORTED);
        SimDtPcieGetRxState(PORT - 1, &State);
        DT_ASSERT_EQ(State.NumUsers, 0);
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 1);
    }
    SimDtPcieFailRxCmd(-1, 0);

    // An allocation failing on the way.
    for (i = 0; i < 40; i++)
    {
        unsigned int Result;

        DtAllocFailAfter((long)i);
        Result = DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT);
        DtAllocFailAfter(-1);
        if (Result == DTAPI_OK)
            break;
        SimDtPcieGetRxState(PORT - 1, &State);
        DT_ASSERT_EQ(State.NumUsers, 0);
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 1);
    }
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    FINISH(Fix);
}

// The channel has its own handle: the device object may go.
DT_TEST(OwnHandle)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures) ||
        !Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL, DtFailures))
    {
        return;
    }
    DT_ASSERT_OK(DtDevice_Detach(Fix.Device));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 8, DtFailures));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Consecutive frames, bit for bit, in every format, for SD and HD.
DT_TEST(ReadsFramesBitForBit)
{
    static const int Modes[] = {DTAPI_RXMODE_SDI_FULL,
                                DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B,
                                DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B};
    static const int Standards[] = {DTAPI_VIDSTD_625I50, DTAPI_VIDSTD_525I59_94,
                                    DTAPI_VIDSTD_720P50, DTAPI_VIDSTD_1080I50};
    size_t m, s;

    for (s = 0; s < sizeof(Standards) / sizeof(Standards[0]); s++)
    {
        for (m = 0; m < sizeof(Modes) / sizeof(Modes[0]); m++)
        {
            Fixture Fix;
            uint32_t Frame;

            if (!Start(&Fix, DtFailures))
                return;
            SimDtPcieLimitRxRing(40 * 1024 * 1024);
            if (!Receive(&Fix, Standards[s], Modes[m], DtFailures))
                return;
            for (Frame = 0; Frame < 2; Frame++)
            {
                if (!ReadsFrame(&Fix, Standards[s], Frame, BitsOf(Modes[m]), DtFailures))
                    break;
            }
            FINISH(Fix);
        }
    }
}

// A ring a little over two frames: lines and headers across its end, with the card's
// alignment and with 32 bits, mapped as on Windows and as on Linux.
DT_TEST(ReadsAcrossTheEndOfTheRing)
{
    static const int Alignments[] = {128, 32};
    static const size_t Rings[] = {2769000, 2900000, 3200000};
    size_t a, r;

    for (a = 0; a < sizeof(Alignments) / sizeof(Alignments[0]); a++)
    {
        for (r = 0; r < sizeof(Rings) / sizeof(Rings[0]); r++)
        {
            Fixture Fix;
            uint32_t Frame;

            if (!Start(&Fix, DtFailures))
                return;
            SimDtPcieSetRxAlignment(Alignments[a]);
            SimDtPcieMapRxRingAsLinux(r % 2 == 1);
            SimDtPcieLimitRxRing(Rings[r]);
            if (!Receive(&Fix, DTAPI_VIDSTD_625I50,
                         DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B, DtFailures))
            {
                return;
            }
            for (Frame = 0; Frame < 7; Frame++)
            {
                if (!ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, Frame, 10, DtFailures))
                    break;
            }
            FINISH(Fix);
        }
    }
}

// A frame with a broken header or the wrong format is skipped; a skipped frame number is
// followed; an out-of-sync frame is lost.
DT_TEST(RecoversFromFaults)
{
    static const struct
    {
        SimRxFault Fault;
        uint32_t Next; // The frame read after frame 0
    } Cases[] = {
        {SIM_RX_FAULT_SYNC_WORD, 2},
        {SIM_RX_FAULT_FORMAT, 2},
        {SIM_RX_FAULT_SKIP_FRAME, 2},
        {SIM_RX_FAULT_OUT_OF_SYNC, 2},
    };
    size_t i;

    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        Fixture Fix;

        if (!Start(&Fix, DtFailures))
            return;
        SimDtPcieLimitRxRing(8 * 1024 * 1024);
        if (!Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL, DtFailures))
            return;
        DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 8, DtFailures));
        SimDtPcieInjectRxFault(PORT - 1, Cases[i].Fault);
        DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, Cases[i].Next, 8, DtFailures));
        DT_ASSERT(
            ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, Cases[i].Next + 1, 8, DtFailures));
        FINISH(Fix);
    }
}

// A reader that falls behind fills the ring: frames are lost and the overflow flag is
// set, then cleared again once frames can be read.
DT_TEST(FullRingSetsOverflow)
{
    Fixture Fix;
    DtSdiFrameLayout Layout;
    int Flags = -1, Latched = -1, Load = -1;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT(DtSdiFrameLayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));
    SimDtPcieLimitRxRing(5 * DtSdiFrameCodedSize(&Layout) / 2);
    if (!Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL, DtFailures))
        return;

    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 8, DtFailures));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Flags, 0);
    DT_ASSERT_EQ(Latched, 0);

    // Two whole frames wait.
    SimDtPcieRunRxEvents(PORT - 1, 8);
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 2 * (int)DtSdiFrameRawSize(&Layout, 8));

    // Three more do not fit.
    SimDtPcieRunRxEvents(PORT - 1, 12);
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 1, 8, DtFailures));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched, DTAPI_RX_FIFO_OVF);

    DT_ASSERT_OK(DtInpChannel_ClearFlags(Fix.Channel, DTAPI_RX_FIFO_OVF));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched, 0);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ReadFrame +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// DtInpChannel::ReadFrame's checks in their order, and the frame size after them.
DT_TEST(ReadFrameChecks)
{
    Fixture Fix;
    int Size;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    Size = 0;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 0),
                 DTAPI_E_BUF_TOO_SMALL);
    Size = BUFFER_SIZE;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 0),
                 DTAPI_E_INVALID_TIMEOUT);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, -2),
                 DTAPI_E_INVALID_TIMEOUT);
    Size = 1001;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 10),
                 DTAPI_E_INVALID_SIZE);
    Size = -4;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 10),
                 DTAPI_E_INVALID_SIZE);
    Size = BUFFER_SIZE;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer + 1, &Size, 10),
                 DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, NULL, &Size, 10),
                 DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(Size, BUFFER_SIZE);

    // 1080i50 in 10 bits takes 7,425,000 bytes.
    Size = 7425000 - 4;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 10),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Size, 0);
    FINISH(Fix);
}

// Not receiving, or receiving nothing, the read waits and times out.
DT_TEST(ReadFrameTimesOut)
{
    Fixture Fix;
    uint64_t Before;
    int Size = BUFFER_SIZE;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    Before = OsMonotonicMs();
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 40),
                 DTAPI_E_TIMEOUT);
    DT_ASSERT(OsMonotonicMs() - Before >= 40);
    DT_ASSERT_EQ(Size, 0);

    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    Size = BUFFER_SIZE;
    Before = OsMonotonicMs();
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 40),
                 DTAPI_E_TIMEOUT);
    DT_ASSERT(OsMonotonicMs() - Before >= 40);
    FINISH(Fix);
}

typedef struct Reader
{
    DtInpChannel* Channel;
    char* Buffer;
    unsigned int Result;
} Reader;

static void ReadForever(void* Context)
{
    Reader* R = (Reader*)Context;
    int Size = BUFFER_SIZE;

    R->Result = DtInpChannel_ReadFrame(R->Channel, R->Buffer, &Size, -1);
}

// A detach ends a read waiting on another thread.
DT_TEST(DetachCancelsARead)
{
    Fixture Fix;
    Reader R;
    OsThread* Thread;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

    R.Channel = Fix.Channel;
    R.Buffer = Fix.Buffer;
    R.Result = DTAPI_OK;
    Thread = OsThreadStart(ReadForever, &R);
    DT_ASSERT(Thread != NULL);
    OsSleepMs(60);

    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 1));
    OsThreadJoin(Thread);
    DT_ASSERT_EQ(R.Result, DTAPI_E_CANCELLED);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// SdiRxImpl_Bb2's receive modes: the full frame only, in 8, 10 or 16 bits, while idle.
DT_TEST(ReceiveModes)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_ACTVID),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_ACTVID |
                                                         DTAPI_RXMODE_SDI_HUFFMAN),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_RAWDMA),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL | 0x10),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, 0x11), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B_NBO),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL | 0x02000000),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL | 0x01000000),
                 DTAPI_E_INVALID_MODE);

    // DTAPI_RXMODE_SDI alone means the full frame.
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI));
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_STAT));

    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_NOT_IDLE);
    FINISH(Fix);
}

// Configuring through the channel.
DT_TEST(IoConfiguration)
{
    Fixture Fix;
    SimRxState State;
    int Value = 0, SubValue = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_625I50),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                          DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                          DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_ASI, -1),
                 DTAPI_E_NOT_SUPPORTED);

    // A new standard reconfigures the channel.
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_SDI, DTAPI_IOCONFIG_625I50));
    SimDtPcieSetRxSource(PORT - 1, DTAPI_VIDSTD_625I50);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 8, DtFailures));
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50),
                 DTAPI_E_NOT_IDLE);

    // Clearing the FIFO stops receiving.
    DT_ASSERT_OK(DtInpChannel_ClearFifo(Fix.Channel));
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 0);
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_720P50));
    SimDtPcieSetRxSource(PORT - 1, DTAPI_VIDSTD_720P50);
    SimDtPcieGetRxState(PORT - 1, &State);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_720P50, State.NextFrame, 8, DtFailures));

    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 48 * 1024 * 1024);
    (void)SubValue;
    FINISH(Fix);
}

// The I/O standard of the signal, from the port's receiver.
DT_TEST(DetectsTheIoStandard)
{
    Fixture Fix;
    SimSdiSignal Signal;
    int Value = 0, SubValue = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(Fix.Channel, &Value, &SubValue),
                 DTAPI_E_INVALID_VIDSTD);
    DT_ASSERT_EQ(Value, -1);

    memset(&Signal, 0, sizeof(Signal));
    Signal.CarrierDetect = Signal.SdiLock = Signal.LineLock = Signal.Valid = 1;
    Signal.NumSymsHanc = 1400;
    Signal.NumSymsVidVanc = 2560;
    Signal.NumLinesF1 = 750;
    Signal.FramePeriod = 20000000;
    Signal.SdiRate = DT_DRV_SDIRATE_HD;
    SimDtPcieSetSdiSignal(PORT - 1, &Signal);
    DT_ASSERT_OK(DtInpChannel_DetectIoStd(Fix.Channel, &Value, &SubValue));
    DT_ASSERT_EQ(Value, DTAPI_IOCONFIG_HDSDI);
    DT_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_720P50);
    FINISH(Fix);
}

DT_TEST_MAIN("SimInpChannel", DT_RUN(NullAndDetached), DT_RUN(AttachChecks),
             DT_RUN(AttachRefusals), DT_RUN(AttachCleansUpAfterFailures),
             DT_RUN(OwnHandle), DT_RUN(ReadsFramesBitForBit),
             DT_RUN(ReadsAcrossTheEndOfTheRing), DT_RUN(RecoversFromFaults),
             DT_RUN(FullRingSetsOverflow), DT_RUN(ReadFrameChecks),
             DT_RUN(ReadFrameTimesOut), DT_RUN(DetachCancelsARead), DT_RUN(ReceiveModes),
             DT_RUN(IoConfiguration), DT_RUN(DetectsTheIoStandard))

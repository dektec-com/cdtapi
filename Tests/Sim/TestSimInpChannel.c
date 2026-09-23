// #*#*#*#*#*#*#*#*#*#*#*#*# TestSimInpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtInpChannel against the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open. The frames a channel
// reads are compared with frames built here, bit by bit, from the symbols the emulator's
// source sends.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations and allocation failures.
#include "Device/DtDevice.h"        // The device object, to change its firmware status.
#include "DtPcieAbi.h"              // Driver statuses and commands.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles.
#include "OAL/OsThread.h"           // A reader on another thread, and the clock.
#include "OAL/Sim/SimChSdiRx.h"     // The emulated receive channels.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "Video/DtSdiFrame.h"       // Frame sizes.
#include "cdtapi.h"                 // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Port 1, an input by default, and port 2, an output by default.
#define PORT 1
#define PORT_OUTPUT 2

// Large enough for a 1080-line frame with 16-bit symbols.
#define BUFFER_SIZE (16 * 1024 * 1024)

// Symbols in the longest line the emulated source makes: a raw 2160p50 line.
#define SIM_RX_LINE_SYMBOLS 21120

typedef struct Fixture
{
    int Live;
    DtDevice* Device;
    DtInpChannel* Channel;
    char* Buffer; // BUFFER_SIZE bytes, aligned to 8
} Fixture;

// One I/O configuration through the device, which takes a list.
static DtapiResult SetIoConfig(DtDevice* Device, int Port, int Group, int Value,
                               int SubValue)
{
    DtIoConfig Config = {Port, Group, Value, SubValue, {-1, -1}};
    return DtDevice_SetIoConfig(Device, &Config, 1);
}

// Resets the emulator, attaches a device object and allocates a channel and a buffer.
// Returns false, having recorded a failure, when that is not possible.
static bool Start(Fixture* Fix, int* DtFailures)
{
    OsDrv* Drv;

    SimDtPcie_Reset();
    Fix->Live = DtAlloc_Live();
    Fix->Device = NULL;
    Fix->Channel = NULL;
    Fix->Buffer = NULL;

    Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return false;
    }
    OsDrv_Close(Drv);

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
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        DT_ASSERT_EQ(DtAlloc_Live(), (Fix).Live);                                        \
    } while (0)

// Sets the I/O standard of the port at Port to VidStd.
static DtapiResult SetStandard(Fixture* Fix, int Port, int VidStd)
{
    int Value;
    int SubValue;
    DtapiResult Result = DtapiVidStd2IoStd(VidStd, -1, &Value, &SubValue);

    if (Result != DTAPI_OK)
        return Result;
    return SetIoConfig(Fix->Device, Port, DTAPI_IOCONFIG_IOSTD, Value, SubValue);
}

// Builds frame FrameNumber of VidStd as a raw frame with Bits bits per symbol, into a new
// buffer of *Size bytes.
static uint8_t* ExpectedFrame(int VidStd, uint32_t FrameNumber, int Bits, size_t* Size)
{
    size_t Symbol = 0;

    *Size = 0;
    DtSdiFrameLayout Layout;
    if (!DtSdiFrame_LayoutInit(&Layout, VidStd, 32))
        return NULL;
    *Size = DtSdiFrame_RawSize(&Layout, Bits);
    uint8_t* Frame = (uint8_t*)calloc(*Size, 1);
    if (Frame == NULL)
        return NULL;

    uint16_t* Symbols = (uint16_t*)malloc(SIM_RX_LINE_SYMBOLS * sizeof(uint16_t));
    if (Symbols == NULL)
    {
        free(Frame);
        return NULL;
    }
    for (int Line = 1; Line <= Layout.NumLines; Line++)
    {
        int Count = SimChSdiRx_Line(VidStd, FrameNumber, Line, Symbols);

        for (int i = 0; i < Count; i++, Symbol++)
        {
            uint32_t Value = Symbols[i];

            if (Bits == 8)
                Frame[Symbol] = (uint8_t)(Value >> 2);
            else if (Bits == 16)
            {
                Frame[2 * Symbol] = (uint8_t)Value;
                Frame[2 * Symbol + 1] = (uint8_t)(Value >> 8);
            }
            else
            {
                for (int b = 0; b < 10; b++)
                {
                    if (Value >> b & 1)
                        Frame[(Symbol * 10 + (size_t)b) / 8] |=
                            (uint8_t)(1u << ((Symbol * 10 + (size_t)b) % 8));
                }
            }
        }
    }
    free(Symbols);
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
    DtapiResult Result =
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
    DtapiResult Result = SetStandard(Fix, PORT, VidStd);

    SimDtPcie_SetRxSource(PORT - 1, VidStd);
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
    int A = 0;
    int B = 0;
    int Size = 8;

    DtInpChannel_Free(NULL);
    DtInpChannel_Freep(NULL);
    Fixture Fix;
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
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50,
                                          -1, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(NULL, DTAPI_RXCTRL_RCV), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(NULL, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_INVALID_ARG);
    char Frame[8];
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
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50,
                                          -1, -1),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Frame, &Size, 10),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(Size, 8);

    DtInpChannel* Freed = DtInpChannel_Alloc();
    DT_ASSERT(Freed != NULL);
    DtInpChannel_Freep(&Freed);
    DT_ASSERT(Freed == NULL);
    FINISH(Fix);
}

// The checks DtInpChannel_AttachToPort makes, and those of the input channel, in their
// order.
DT_TEST(AttachChecks)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DtDevice* Detached = DtDevice_Alloc();
    DtInpChannel* Second = DtInpChannel_Alloc();

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
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 2);

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

    SimDtPcie_OverrideProperty("CAP_ASI", PORT - 1, true, 0);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_NOT_SUPPORTED);

    SimDtPcie_Reset();
    SimDtPcie_OverrideProperty("CAP_MATRIX", PORT - 1, true, 1);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_NOT_SUPPORTED);

    SimDtPcie_Reset();
    SimDtPcie_SetDriverVersion(2, 0, 2, 327);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_DRIVER_INCOMP);

    // 2160p over one 12G link receives (0014), but a 4K standard of level-B links
    // attaches without receiving.
    SimDtPcie_Reset();
    SimDtPcie_OverrideProperty("CAP_12GSDI", PORT - 1, true, 1);
    SimDtPcie_OverrideProperty("CAP_2160P50", PORT - 1, true, 1);
    SimDtPcie_OverrideProperty("CAP_2160P50B", PORT - 1, true, 1);
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                             DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                             DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50B));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV),
                 DTAPI_E_CONFIG_RAW_SDI);
    {
        int Size = BUFFER_SIZE;
        DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                     DTAPI_E_TIMEOUT);
    }
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));

    SimDtPcie_Reset();
    DtDevice_Detach(Fix.Device);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Fix.Device, SIM_SERIAL));
    // An ASI port attaches, and reads no frames.
    DT_ASSERT_OK(
        SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    {
        int Size = BUFFER_SIZE;
        DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                     DTAPI_E_NOT_SDI_MODE);
    }
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 1);
    FINISH(Fix);
}

// A refused receive-channel command fails the attach and leaves nothing behind.
DT_TEST(AttachCleansUpAfterFailures)
{
    static const int Commands[] = {DT_CHSDIRX_CMD_ATTACH, DT_CHSDIRX_CMD_GET_PROPS,
                                   DT_CHSDIRX_CMD_CONFIGURE,
                                   DT_CHSDIRX_CMD_MAP_DMA_BUF_TO_USER};
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;

    SimRxState State;
    size_t i;
    for (i = 0; i < sizeof(Commands) / sizeof(Commands[0]); i++)
    {
        SimDtPcie_FailRxCmd(Commands[i], DT_STATUS_NOT_SUPPORTED);
        DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                     DTAPI_E_NOT_SUPPORTED);
        SimDtPcie_GetRxState(PORT - 1, &State);
        DT_ASSERT_EQ(State.NumUsers, 0);
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 1);
    }
    SimDtPcie_FailRxCmd(-1, 0);

    // An allocation failing on the way.
    for (i = 0; i < 40; i++)
    {
        DtAlloc_FailAfter((int)i);
        DtapiResult Result = DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT);
        DtAlloc_FailAfter(-1);
        if (Result == DTAPI_OK)
            break;
        SimDtPcie_GetRxState(PORT - 1, &State);
        DT_ASSERT_EQ(State.NumUsers, 0);
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 1);
    }
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    FINISH(Fix);
}

// A ring whose largest load holds fewer than two frames fails the attach; one that holds
// two attaches, and the channel's FIFO size is those two frames in the receive mode.
DT_TEST(RingHoldsTwoFramesAtLeast)
{
    const size_t Unit = SIM_RX_PREFETCH_PAGES * 4096;
    const size_t Word = SIM_RX_PCIE_DATA_WIDTH / 8;
    Fixture Fix;
    int Max = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_625I50));
    DtSdiFrameLayout Layout;
    DT_ASSERT(
        DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, SIM_RX_STREAM_ALIGNMENT));
    size_t Coded = DtSdiFrame_CodedSize(&Layout);

    // Two frames of ring, but not of load.
    SimDtPcie_LimitRxRing(2 * Coded);
    DT_ASSERT_EQ(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_DEV_DRIVER);
    SimRxState State;
    SimDtPcie_GetRxState(PORT - 1, &State);
    DT_ASSERT_EQ(State.NumUsers, 0);

    SimDtPcie_LimitRxRing((2 * Coded + Word + Unit - 1) / Unit * Unit);
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    SimDtPcie_GetRxState(PORT - 1, &State);
    DT_ASSERT((State.RingSize - Word) / Coded == 2);

    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &Max));
    DT_ASSERT_EQ(Max, 2 * (int)DtSdiFrame_RawSize(&Layout, 10));
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B));
    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &Max));
    DT_ASSERT_EQ(Max, 2 * (int)DtSdiFrame_RawSize(&Layout, 16));
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    FINISH(Fix);
}

// A channel on a 4K port has no ring and no frame size: a read waits for its time-out,
// A port of 2160p over one link wants a buffer for a whole raw frame, 29.7 MB in 10 bits,
// also after the channel was attached to the port with an HD standard before; without a
// signal nothing comes.
DT_TEST(FourKPortWantsWholeFrames)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));

    SimDtPcie_OverrideProperty("CAP_12GSDI", PORT - 1, true, 1);
    SimDtPcie_OverrideProperty("CAP_2160P50", PORT - 1, true, 1);
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                             DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

    int Size = BUFFER_SIZE;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 30),
                 DTAPI_E_BUF_TOO_SMALL);
    int MaxFifo = 0;
    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &MaxFifo));
    DT_ASSERT(MaxFifo >= 2 * 29700000);

    char* Frame = (char*)malloc(29700000);
    DT_ASSERT(Frame != NULL);
    Size = 29700000;
    uint64_t Before = OsTime_MonotonicMs();
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Frame, &Size, 30), DTAPI_E_TIMEOUT);
    DT_ASSERT(OsTime_MonotonicMs() - Before >= 30);
    free(Frame);
    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
    FINISH(Fix);
}

// 2160p over one 12G link: the emulated card writes its ring as the card does, and a
// read gives the raw frame of the four links, in 10 and in 16 bits.
DT_TEST(ReceivesFourKFrames)
{
    static const int Modes[] = {DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B,
                                DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B};

    for (size_t m = 0; m < sizeof(Modes) / sizeof(Modes[0]); m++)
    {
        Fixture Fix;
        int Bits = BitsOf(Modes[m]);

        if (!Start(&Fix, DtFailures))
            return;
        SimDtPcie_OverrideProperty("CAP_12GSDI", PORT - 1, true, 1);
        SimDtPcie_OverrideProperty("CAP_2160P50", PORT - 1, true, 1);
        DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                                 DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50));
        SimDtPcie_SetRxSource(PORT - 1, DTAPI_VIDSTD_2160P50);
        DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
        DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, Modes[m]));
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

        size_t Size = 0;
        uint8_t* Expected = ExpectedFrame(DTAPI_VIDSTD_2160P50, 0, Bits, &Size);
        char* Buffer = (char*)malloc(Size);
        DT_ASSERT(Expected != NULL && Buffer != NULL);
        int FrameSize = (int)Size;
        DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.Channel, Buffer, &FrameSize, 20000));
        DT_ASSERT_EQ((size_t)FrameSize, Size);
        DT_ASSERT_MEM(Buffer, Expected, Size);
        free(Expected);
        free(Buffer);
        DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
        FINISH(Fix);
    }
}

// The channel has its own handle: the device object may go.
DT_TEST(OwnHandle)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures) ||
        !Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B,
                 DtFailures))
    {
        return;
    }
    DT_ASSERT_OK(DtDevice_Detach(Fix.Device));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 16, DtFailures));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Consecutive frames, bit for bit, with 10- and 16-bit symbols, for SD and HD.
DT_TEST(ReadsFramesBitForBit)
{
    static const int Modes[] = {DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B,
                                DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B};
    static const int Standards[] = {DTAPI_VIDSTD_625I50, DTAPI_VIDSTD_525I59_94,
                                    DTAPI_VIDSTD_720P50, DTAPI_VIDSTD_1080I50};

    for (size_t s = 0; s < sizeof(Standards) / sizeof(Standards[0]); s++)
    {
        for (size_t m = 0; m < sizeof(Modes) / sizeof(Modes[0]); m++)
        {
            Fixture Fix;

            if (!Start(&Fix, DtFailures))
                return;
            SimDtPcie_LimitRxRing(40 * 1024 * 1024);
            if (!Receive(&Fix, Standards[s], Modes[m], DtFailures))
                return;
            for (uint32_t Frame = 0; Frame < 2; Frame++)
            {
                if (!ReadsFrame(&Fix, Standards[s], Frame, BitsOf(Modes[m]), DtFailures))
                    break;
            }
            FINISH(Fix);
        }
    }
}

// ReadFrame2 gives each frame's time of arrival from its header, which the emulated card
// sets to the frame number in seconds, and zero after a failure.
DT_TEST(ReadFrame2GivesTheArrivalTime)
{
    Fixture Fix;
    DtTimeOfDay Arrival = {7, 7};

    if (!Start(&Fix, DtFailures))
        return;
    if (!Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B,
                 DtFailures))
        return;

    int Size = BUFFER_SIZE;
    DT_ASSERT_OK(DtInpChannel_ReadFrame2(Fix.Channel, Fix.Buffer, &Size, 2000, &Arrival));
    DT_ASSERT(Arrival.Seconds == 0 && Arrival.Nanoseconds == 0);
    Size = BUFFER_SIZE;
    DT_ASSERT_OK(DtInpChannel_ReadFrame2(Fix.Channel, Fix.Buffer, &Size, 2000, &Arrival));
    DT_ASSERT(Arrival.Seconds == 1 && Arrival.Nanoseconds == 0);
    Size = BUFFER_SIZE;
    DT_ASSERT_OK(DtInpChannel_ReadFrame2(Fix.Channel, Fix.Buffer, &Size, 2000, NULL));

    Arrival.Seconds = Arrival.Nanoseconds = 7;
    Size = 4;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame2(Fix.Channel, Fix.Buffer, &Size, 2000, &Arrival),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT(Arrival.Seconds == 0 && Arrival.Nanoseconds == 0);
    Arrival.Seconds = 7;
    Size = 0;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame2(NULL, Fix.Buffer, &Size, 2000, &Arrival),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Arrival.Seconds, 0);
    FINISH(Fix);
}

// A ring a little over two frames: lines and headers across its end, with the card's
// alignment and with 32 bits, mapped as on Windows and as on Linux.
DT_TEST(ReadsAcrossTheEndOfTheRing)
{
    static const int Alignments[] = {128, 32};
    static const size_t Rings[] = {2769000, 2900000, 3200000};

    for (size_t a = 0; a < sizeof(Alignments) / sizeof(Alignments[0]); a++)
    {
        for (size_t r = 0; r < sizeof(Rings) / sizeof(Rings[0]); r++)
        {
            Fixture Fix;

            if (!Start(&Fix, DtFailures))
                return;
            SimDtPcie_SetRxAlignment(Alignments[a]);
            SimDtPcie_MapRxRingAsLinux(r % 2 == 1);
            SimDtPcie_LimitRxRing(Rings[r]);
            if (!Receive(&Fix, DTAPI_VIDSTD_625I50,
                         DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B, DtFailures))
            {
                return;
            }
            for (uint32_t Frame = 0; Frame < 7; Frame++)
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

    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        Fixture Fix;

        if (!Start(&Fix, DtFailures))
            return;
        SimDtPcie_LimitRxRing(8 * 1024 * 1024);
        if (!Receive(&Fix, DTAPI_VIDSTD_625I50,
                     DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B, DtFailures))
            return;
        DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 16, DtFailures));
        SimDtPcie_InjectRxFault(PORT - 1, Cases[i].Fault);
        DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, Cases[i].Next, 16, DtFailures));
        DT_ASSERT(
            ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, Cases[i].Next + 1, 16, DtFailures));
        FINISH(Fix);
    }
}

// A reader that falls behind fills the ring: frames are lost and the overflow flag is
// set, then cleared again once frames can be read.
DT_TEST(FullRingSetsOverflow)
{
    Fixture Fix;
    int Flags = -1;
    int Latched = -1;
    int Load = -1;
    int Max = -1;

    if (!Start(&Fix, DtFailures))
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, 128));
    SimDtPcie_LimitRxRing(5 * DtSdiFrame_CodedSize(&Layout) / 2);
    if (!Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B,
                 DtFailures))
        return;

    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 16, DtFailures));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Flags, 0);
    DT_ASSERT_EQ(Latched, 0);

    // Two whole frames wait.
    SimDtPcie_RunRxEvents(PORT - 1, 8);
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 2 * (int)DtSdiFrame_RawSize(&Layout, 16));

    // That fills the ring's load, so the load is the FIFO size: the rest of the ring,
    // less than a frame, never counts.
    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &Max));
    DT_ASSERT_EQ(Load, Max);

    // Three more do not fit, and the load stays within the FIFO size.
    SimDtPcie_RunRxEvents(PORT - 1, 12);
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT(Load <= Max);
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 1, 16, DtFailures));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched, DTAPI_RX_FIFO_OVF);

    DT_ASSERT_OK(DtInpChannel_ClearFlags(Fix.Channel, DTAPI_RX_FIFO_OVF));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched, 0);
    FINISH(Fix);
}

// A full ring cuts a frame short and the source goes on with later frames after it. That
// frame is not delivered: the next frame read is a whole later one.
DT_TEST(SkipsAFrameThatLostLines)
{
    Fixture Fix;
    int Flags = -1;
    int Latched = -1;
    int Size = BUFFER_SIZE;
    bool Whole = false;

    if (!Start(&Fix, DtFailures))
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(
        DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, SIM_RX_STREAM_ALIGNMENT));
    SimDtPcie_LimitRxRing(5 * DtSdiFrame_CodedSize(&Layout) / 2);
    if (!Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B,
                 DtFailures))
        return;

    // Frames 0 and 1 fit; frame 2 is cut short, and later frames lose everything.
    SimDtPcie_RunRxEvents(PORT - 1, 20);
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 16, DtFailures));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 1, 16, DtFailures));

    DT_ASSERT_OK(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 2000));
    for (uint32_t Number = 2; Number < 40 && !Whole; Number++)
    {
        size_t Expected;
        uint8_t* Frame = ExpectedFrame(DTAPI_VIDSTD_625I50, Number, 16, &Expected);

        Whole = Frame != NULL && (size_t)Size == Expected &&
                memcmp(Fix.Buffer, Frame, Expected) == 0;
        if (Whole)
            DT_ASSERT(Number > 2);
        free(Frame);
    }
    DT_ASSERT(Whole);

    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched, DTAPI_RX_FIFO_OVF);
    FINISH(Fix);
}

// The load counts the frames waiting before any has been read.
DT_TEST(FifoLoadBeforeTheFirstRead)
{
    Fixture Fix;
    int Load = -1;

    if (!Start(&Fix, DtFailures))
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(
        DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, SIM_RX_STREAM_ALIGNMENT));
    if (!Receive(&Fix, DTAPI_VIDSTD_625I50, DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B,
                 DtFailures))
        return;

    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 0);
    SimDtPcie_RunRxEvents(PORT - 1, 12);
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 3 * (int)DtSdiFrame_RawSize(&Layout, 10));

    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 10, DtFailures));
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 2 * (int)DtSdiFrame_RawSize(&Layout, 10));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ReadFrame +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// DtInpChannel_ReadFrame's checks in their order, and the frame size after them.
DT_TEST(ReadFrameChecks)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    int Size = 0;
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
    int Size = BUFFER_SIZE;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    uint64_t Before = OsTime_MonotonicMs();
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 40),
                 DTAPI_E_TIMEOUT);
    DT_ASSERT(OsTime_MonotonicMs() - Before >= 40);
    DT_ASSERT_EQ(Size, 0);

    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    Size = BUFFER_SIZE;
    Before = OsTime_MonotonicMs();
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 40),
                 DTAPI_E_TIMEOUT);
    DT_ASSERT(OsTime_MonotonicMs() - Before >= 40);
    FINISH(Fix);
}

typedef struct Reader
{
    DtInpChannel* Channel;
    char* Buffer;
    DtapiResult Result;
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

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

    Reader R;
    R.Channel = Fix.Channel;
    R.Buffer = Fix.Buffer;
    R.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(ReadForever, &R);
    DT_ASSERT(Thread != NULL);
    OsTime_SleepMs(60);

    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 1));
    OsThread_Join(Thread);
    DT_ASSERT_EQ(R.Result, DTAPI_E_CANCELLED);
    FINISH(Fix);
}

// A read while a read on another thread waits is refused, whatever its time-out, and the
// waiting read goes on until the channel is detached.
DT_TEST(SecondReadIsRefused)
{
    Fixture Fix;
    int Size = BUFFER_SIZE;
    DtTimeOfDay Arrival = {1, 1};

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

    Reader R;
    R.Channel = Fix.Channel;
    R.Buffer = Fix.Buffer;
    R.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(ReadForever, &R);
    DT_ASSERT(Thread != NULL);
    OsTime_SleepMs(60);

    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, -1),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame2(Fix.Channel, Fix.Buffer, &Size, 20, &Arrival),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(Arrival.Seconds, 0);
    DT_ASSERT_EQ(Arrival.Nanoseconds, 0);

    DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 1));
    OsThread_Join(Thread);
    DT_ASSERT_EQ(R.Result, DTAPI_E_CANCELLED);
    FINISH(Fix);
}

// A detach that gives up while a read waits leaves the channel attached and usable, and a
// later detach, once the read can return, ends it.
DT_TEST(DetachThatTimesOutLeavesTheChannelUsable)
{
    Fixture Fix;
    int Size = BUFFER_SIZE;
    DtapiResult Result = DTAPI_E_TIMEOUT;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    SimDtPcie_SlowRxCmd(DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT, 400);

    Reader R;
    R.Channel = Fix.Channel;
    R.Buffer = Fix.Buffer;
    R.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(ReadForever, &R);
    DT_ASSERT(Thread != NULL);
    OsTime_SleepMs(60);

    DT_ASSERT_EQ(DtInpChannel_Detach(Fix.Channel, 0), DTAPI_E_TIMEOUT);
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                 DTAPI_E_IN_USE);

    SimDtPcie_SlowRxCmd(DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT, 0);
    for (int Tries = 0; Tries < 10 && Result == DTAPI_E_TIMEOUT; Tries++)
        Result = DtInpChannel_Detach(Fix.Channel, 0);
    DT_ASSERT_OK(Result);
    OsThread_Join(Thread);
    DT_ASSERT_EQ(R.Result, DTAPI_E_CANCELLED);
    DT_ASSERT_EQ(DtInpChannel_Detach(Fix.Channel, 0), DTAPI_E_NOT_ATTACHED);
    FINISH(Fix);
}

// Freeing a channel waits for a read on another thread, however long that read takes to
// return.
DT_TEST(FreeWaitsForARead)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    SimDtPcie_SlowRxCmd(DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT, 300);

    Reader R;
    R.Channel = Fix.Channel;
    R.Buffer = Fix.Buffer;
    R.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(ReadForever, &R);
    DT_ASSERT(Thread != NULL);
    OsTime_SleepMs(60);

    DtInpChannel_Free(Fix.Channel);
    Fix.Channel = NULL;
    OsThread_Join(Thread);
    DT_ASSERT_EQ(R.Result, DTAPI_E_CANCELLED);
    FINISH(Fix);
}

typedef struct SizedReader
{
    DtInpChannel* Channel;
    char* Buffer;
    int Size;
    DtapiResult Result;
} SizedReader;

static void ReadSized(void* Context)
{
    SizedReader* R = (SizedReader*)Context;

    R->Result = DtInpChannel_ReadFrame(R->Channel, R->Buffer, &R->Size, 3000);
}

// A read waiting with a buffer for a 10-bit frame, while another thread switches the
// channel to 16 bits and starts it again, finds its buffer too small and writes nothing.
DT_TEST(ReadAfterAModeChangeChecksTheBuffer)
{
    Fixture Fix;
    bool Untouched = true;

    if (!Start(&Fix, DtFailures))
        return;
    DtSdiFrameLayout Layout;
    DT_ASSERT(
        DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_625I50, SIM_RX_STREAM_ALIGNMENT));
    size_t Raw10 = DtSdiFrame_RawSize(&Layout, 10);
    DT_ASSERT_OK(SetStandard(&Fix, PORT, DTAPI_VIDSTD_625I50));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

    memset(Fix.Buffer, 0xA5, BUFFER_SIZE);
    SizedReader R;
    R.Channel = Fix.Channel;
    R.Buffer = Fix.Buffer;
    R.Size = (int)Raw10;
    R.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(ReadSized, &R);
    DT_ASSERT(Thread != NULL);
    OsTime_SleepMs(60);

    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_IDLE));
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B));
    SimDtPcie_SetRxSource(PORT - 1, DTAPI_VIDSTD_625I50);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    OsThread_Join(Thread);

    DT_ASSERT_EQ(R.Result, DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(R.Size, 0);
    for (size_t i = 0; i < BUFFER_SIZE; i++)
        Untouched = Untouched && (uint8_t)Fix.Buffer[i] == 0xA5;
    DT_ASSERT(Untouched);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// An SDI port's receive modes: the full frame only, in 8, 10 or 16 bits, while idle.
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

    // The 8-bit mode is set, but receiving in it fails and leaves the channel idle.
    SimDtPcie_SetRxSource(PORT - 1, DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV),
                 DTAPI_E_CONFIG_RAW_SDI);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B));

    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_NOT_IDLE);
    FINISH(Fix);
}

// Configuring through the channel.
DT_TEST(IoConfiguration)
{
    Fixture Fix;
    int Value = 0;
    int SubValue = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_625I50, -1,
                                          -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    // 2160p over one link goes through to the driver, which refuses it on a port without
    // the capability and takes it on one with (0014). Ports 1 and 5 have it, so the
    // refusal needs the capability taken away.
    SimDtPcie_OverrideProperty("CAP_12GSDI", PORT - 1, false, 0);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50,
                                          -1, -1),
                 DTAPI_E_CONFIG);
    SimDtPcie_OverrideProperty("CAP_12GSDI", PORT - 1, true, 1);
    SimDtPcie_OverrideProperty("CAP_2160P50", PORT - 1, true, 1);
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50,
                                          -1, -1));
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50,
                                          -1, -1));
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                          DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT,
                                          -1, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                          DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT, -1,
                                          -1),
                 DTAPI_E_NOT_SUPPORTED);

    // An input sharing another port's antenna needs that port, before the direction is
    // refused.
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                          DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_SHAREDANT,
                                          0, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                          DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_SHAREDANT,
                                          2, -1),
                 DTAPI_E_NOT_SUPPORTED);

    // Reading: the direction, and a group that is none.
    int64_t ParXtra0 = 0;
    DT_ASSERT_OK(DtInpChannel_GetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR, &Value,
                                          &SubValue, &ParXtra0, NULL));
    DT_ASSERT_EQ(Value, DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(ParXtra0, -1);
    DT_ASSERT_EQ(DtInpChannel_GetIoConfig(Fix.Channel, DTAPI_IOCONFIG_INPUT, &Value, NULL,
                                          NULL, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Value, -1);

    // ASI switches the channel over, and a new SDI standard back.
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_ASI, -1, -1, -1));
    {
        int Size = BUFFER_SIZE;
        DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                     DTAPI_E_NOT_SDI_MODE);
    }

    // A new standard reconfigures the channel.
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_SDI, DTAPI_IOCONFIG_625I50, -1,
                                          -1));
    SimDtPcie_SetRxSource(PORT - 1, DTAPI_VIDSTD_625I50);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_16B));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_625I50, 0, 16, DtFailures));
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50,
                                          -1, -1),
                 DTAPI_E_NOT_IDLE);

    // Clearing the FIFO stops receiving.
    DT_ASSERT_OK(DtInpChannel_ClearFifo(Fix.Channel));
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 0);
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_720P50, -1,
                                          -1));
    SimDtPcie_SetRxSource(PORT - 1, DTAPI_VIDSTD_720P50);
    SimRxState State;
    SimDtPcie_GetRxState(PORT - 1, &State);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT(ReadsFrame(&Fix, DTAPI_VIDSTD_720P50, State.NextFrame, 16, DtFailures));

    // The FIFO size follows the standard's ring.
    DtSdiFrameLayout Layout;
    DT_ASSERT(
        DtSdiFrame_LayoutInit(&Layout, DTAPI_VIDSTD_720P50, SIM_RX_STREAM_ALIGNMENT));
    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value,
                 (int)((State.RingSize - SIM_RX_PCIE_DATA_WIDTH / 8) /
                       DtSdiFrame_CodedSize(&Layout) * DtSdiFrame_RawSize(&Layout, 16)));
    (void)SubValue;
    FINISH(Fix);
}

// The I/O standard of the signal, from the port's receiver.
DT_TEST(DetectsTheIoStandard)
{
    Fixture Fix;
    int Value = 0;
    int SubValue = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(Fix.Channel, &Value, &SubValue),
                 DTAPI_E_INVALID_VIDSTD);
    DT_ASSERT_EQ(Value, -1);

    SimSdiSignal Signal;
    memset(&Signal, 0, sizeof(Signal));
    Signal.CarrierDetect = Signal.SdiLock = Signal.LineLock = Signal.Valid = 1;
    Signal.NumSymsHanc = 1400;
    Signal.NumSymsVidVanc = 2560;
    Signal.NumLinesF1 = 750;
    Signal.FramePeriod = 20000000;
    Signal.SdiRate = DT_DRV_SDIRATE_HD;
    SimDtPcie_SetSdiSignal(PORT - 1, &Signal);
    DT_ASSERT_OK(DtInpChannel_DetectIoStd(Fix.Channel, &Value, &SubValue));
    DT_ASSERT_EQ(Value, DTAPI_IOCONFIG_HDSDI);
    DT_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_720P50);
    FINISH(Fix);
}

DT_TEST_MAIN("SimInpChannel", DT_RUN(NullAndDetached), DT_RUN(AttachChecks),
             DT_RUN(AttachRefusals), DT_RUN(AttachCleansUpAfterFailures),
             DT_RUN(RingHoldsTwoFramesAtLeast), DT_RUN(FourKPortWantsWholeFrames),
             DT_RUN(ReceivesFourKFrames), DT_RUN(OwnHandle), DT_RUN(ReadsFramesBitForBit),
             DT_RUN(ReadFrame2GivesTheArrivalTime), DT_RUN(ReadsAcrossTheEndOfTheRing),
             DT_RUN(RecoversFromFaults), DT_RUN(FullRingSetsOverflow),
             DT_RUN(SkipsAFrameThatLostLines), DT_RUN(FifoLoadBeforeTheFirstRead),
             DT_RUN(ReadFrameChecks), DT_RUN(ReadFrameTimesOut),
             DT_RUN(DetachCancelsARead), DT_RUN(SecondReadIsRefused),
             DT_RUN(DetachThatTimesOutLeavesTheChannelUsable), DT_RUN(FreeWaitsForARead),
             DT_RUN(ReadAfterAModeChangeChecksTheBuffer), DT_RUN(ReceiveModes),
             DT_RUN(IoConfiguration), DT_RUN(DetectsTheIoStandard))

// #*#*#*#*#*#*#*#*#*#*#*#* TestSimOutpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtOutpChannel against the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, whose output follows the clock, and ends with no handle to it and no allocation
// left open. The frames written are those the emulator's receive source sends, and the
// frames the emulated card sent are compared with them symbol by symbol.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // Public API under test.
#include "Core/DtAlloc.h"           // Live allocations.
#include "Device/DtDevice.h"        // The device object, to change its firmware status.
#include "DtPcieAbi.h"              // Operational modes, commands and statuses.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles.
#include "OAL/OsThread.h"           // A writer on another thread, and the clock.
#include "OAL/Sim/SimChSdiRx.h"     // The symbols of the frames written.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "OAL/Sim/SimSdiTx.h"       // The emulated transmit blocks.
#include "Video/DtSdiFrame.h"       // Frame sizes and black frames.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Port 2, an output by default with HD-SDI 1080i50, and port 1, an input.
#define PORT 2
#define PORT_INPUT 1

// How long a case waits for frames to be sent.
#define SEND_TIMEOUT_MS 5000

typedef struct Fixture
{
    int Live;
    DtDevice* Device;
    DtOutpChannel* Channel;
} Fixture;

// Resets the emulator, attaches a device object and allocates a channel. Returns false,
// having recorded a failure, when that is not possible.
static bool Start(Fixture* Fix, int* DtFailures)
{
    OsDrv* Drv;

    SimDtPcie_Reset();
    Fix->Live = DtAlloc_Live();
    Fix->Device = NULL;
    Fix->Channel = NULL;

    Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return false;
    }
    OsDrv_Close(Drv);

    Fix->Device = DtDevice_Alloc();
    Fix->Channel = DtOutpChannel_Alloc();
    if (Fix->Device == NULL || Fix->Channel == NULL ||
        DtDevice_AttachToSerial(Fix->Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot set up\n");
        (*DtFailures)++;
        return false;
    }
    return true;
}

// Frees what Start made, lets the emulator free the frames it kept, and checks that
// nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtOutpChannel_Free((Fix).Channel);                                               \
        DtDevice_Free((Fix).Device);                                                     \
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_Live(), (Fix).Live);                                        \
    } while (0)

// Sets the I/O standard of PORT to VidStd through the device.
static DtapiResult SetStandard(Fixture* Fix, int VidStd)
{
    int Value;
    int SubValue;
    DtapiResult Result = DtapiVidStd2IoStd(VidStd, -1, &Value, &SubValue);

    if (Result != DTAPI_OK)
        return Result;
    return DtDevice_SetIoConfig(Fix->Device, PORT, DTAPI_IOCONFIG_IOSTD, Value, SubValue);
}

// Frame FrameNumber of VidStd as the emulator's receive source makes it, as a raw frame
// with Bits bits per symbol, in a new buffer of *Size bytes aligned to 8.
static uint8_t* MakeFrame(int VidStd, uint32_t FrameNumber, int Bits, size_t* Size)
{
    uint64_t Accu = 0;
    int Have = 0;

    *Size = 0;
    DtSdiFrameLayout Layout;
    if (!DtSdiFrame_LayoutInit(&Layout, VidStd, 32))
        return NULL;
    *Size = DtSdiFrame_RawSize(&Layout, Bits);
    uint8_t* Frame = (uint8_t*)calloc(*Size, 1);
    if (Frame == NULL)
        return NULL;

    uint8_t* Out = Frame;
    uint16_t Symbols[8250];
    for (int Line = 1; Line <= Layout.NumLines; Line++)
    {
        int Count = SimChSdiRx_Line(VidStd, FrameNumber, Line, Symbols);

        for (int i = 0; i < Count; i++)
        {
            if (Bits == 16)
            {
                *Out++ = (uint8_t)Symbols[i];
                *Out++ = (uint8_t)(Symbols[i] >> 8);
                continue;
            }
            Accu |= (uint64_t)Symbols[i] << Have;
            for (Have += 10; Have >= 8; Have -= 8)
            {
                *Out++ = (uint8_t)Accu;
                Accu >>= 8;
            }
        }
    }
    if (Have > 0)
        *Out = (uint8_t)Accu;
    return Frame;
}

// Writes Size bytes of Data in pieces whose sizes go through Pieces in turn, each a
// multiple of 4. Returns the first failure.
static DtapiResult WriteInPieces(DtOutpChannel* Channel, const uint8_t* Data, size_t Size,
                                 const size_t* Pieces, size_t NumPieces)
{
    size_t Done = 0;
    size_t p = 0;

    while (Done < Size)
    {
        size_t Piece = Pieces[p++ % NumPieces];

        if (Piece > Size - Done)
            Piece = Size - Done;
        DtapiResult Result = DtOutpChannel_Write(Channel, (char*)Data + Done, (int)Piece);
        if (Result != DTAPI_OK)
            return Result;
        Done += Piece;
    }
    return DTAPI_OK;
}

// Writes frame FrameNumber of VidStd in one piece.
static DtapiResult WriteFrame(DtOutpChannel* Channel, int VidStd, uint32_t FrameNumber,
                              int Bits)
{
    size_t Size;
    uint8_t* Frame = MakeFrame(VidStd, FrameNumber, Bits, &Size);
    DtapiResult Result = Frame == NULL
                             ? DTAPI_E_OUT_OF_MEM
                             : DtOutpChannel_Write(Channel, (char*)Frame, (int)Size);

    free(Frame);
    return Result;
}

// Writes frame FrameNumber of VidStd with WriteFrame.
static DtapiResult WriteWholeFrame(DtOutpChannel* Channel, int VidStd,
                                   uint32_t FrameNumber, int Bits, int TimeOut)
{
    size_t Size;
    uint8_t* Frame = MakeFrame(VidStd, FrameNumber, Bits, &Size);
    DtapiResult Result =
        Frame == NULL ? DTAPI_E_OUT_OF_MEM
                      : DtOutpChannel_WriteFrame(Channel, Frame, (int)Size, TimeOut);

    free(Frame);
    return Result;
}

// Waits until the card has sent Count frames. Returns false after SEND_TIMEOUT_MS.
static bool WaitForFrames(int Count)
{
    uint64_t Start = OsTime_MonotonicMs();

    for (;;)
    {
        SimTxState State;

        SimDtPcie_GetTxState(PORT - 1, &State);
        if (State.FramesSent >= Count)
            return true;
        if (OsTime_MonotonicMs() - Start > SEND_TIMEOUT_MS)
            return false;
        OsTime_SleepMs(2);
    }
}

// Waits until the card has sent Count frames and holds, so that the frames the emulator
// keeps are not replaced by black frames while a slow test compares them.
static bool SentAndHeld(DtOutpChannel* Channel, int Count)
{
    return WaitForFrames(Count) &&
           DtOutpChannel_SetTxControl(Channel, DTAPI_TXCTRL_HOLD) == DTAPI_OK;
}

// The symbols of the kept frame with FrameId, in a new buffer; NULL when there is none.
static uint16_t* SentFrame(int FrameId, int VidStd, SimTxFrame* Frame)
{
    DtSdiFrameLayout Layout;

    if (!DtSdiFrame_LayoutInit(&Layout, VidStd, SIM_TX_STREAM_ALIGNMENT))
        return NULL;
    size_t Count =
        (size_t)Layout.NumLines * (size_t)(Layout.LineSymsHanc + Layout.LineSymsVideo);
    uint16_t* Symbols = (uint16_t*)malloc(Count * sizeof(uint16_t));
    if (Symbols != NULL &&
        !SimDtPcie_CopyTxFrame(PORT - 1, FrameId, Symbols, Count, Frame))
    {
        free(Symbols);
        Symbols = NULL;
    }
    return Symbols;
}

// Whether the card sent frame FrameNumber of VidStd with FrameId.
static bool SentFrameIs(int FrameId, int VidStd, uint32_t FrameNumber)
{
    SimTxFrame Frame;
    uint16_t* Symbols = SentFrame(FrameId, VidStd, &Frame);
    uint16_t Line[8250];
    bool Same = Symbols != NULL;
    int n;

    for (n = 1; Same && n <= Frame.NumLines; n++)
    {
        int Count = SimChSdiRx_Line(VidStd, FrameNumber, n, Line);
        size_t At = (size_t)(n - 1) * (size_t)(Frame.SymsHanc + Frame.SymsVideo);

        Same = Count == Frame.SymsHanc + Frame.SymsVideo &&
               memcmp(Symbols + At, Line, (size_t)Count * sizeof(uint16_t)) == 0;
    }
    free(Symbols);
    return Same;
}

// Whether the card sent a black frame of VidStd with FrameId: the coded lines of
// DtSdiFrame_BlackLines, symbol by symbol.
static bool SentFrameIsBlack(int FrameId, int VidStd)
{
    DtSdiFrameLayout Layout = {0};
    SimTxFrame Frame;
    uint16_t* Symbols = SentFrame(FrameId, VidStd, &Frame);
    uint8_t* Black;
    bool Same = Symbols != NULL &&
                DtSdiFrame_LayoutInit(&Layout, VidStd, SIM_TX_STREAM_ALIGNMENT);
    int n;

    Black =
        Same ? (uint8_t*)malloc((size_t)Layout.NumLines * (size_t)Layout.Stride) : NULL;
    Same = Same && Black != NULL;
    if (Same)
        DtSdiFrame_BlackLines(&Layout, Black);

    for (n = 0; Same && n < Layout.NumLines; n++)
    {
        const uint8_t* Coded = Black + (size_t)n * (size_t)Layout.Stride;
        size_t At = (size_t)n * (size_t)(Layout.LineSymsHanc + Layout.LineSymsVideo);

        for (int s = 0; Same && s < Layout.LineSymsHanc + Layout.LineSymsVideo; s++)
        {
            const uint8_t* Section =
                s < Layout.LineSymsHanc ? Coded : Coded + Layout.LineBytesHanc;
            size_t Index =
                (size_t)(s < Layout.LineSymsHanc ? s : s - Layout.LineSymsHanc);
            size_t Bit = Index * 10;
            uint32_t Value =
                ((uint32_t)Section[Bit / 8] | (uint32_t)Section[Bit / 8 + 1] << 8) >>
                    (Bit % 8) &
                0x3FF;

            Same = Symbols[At + (size_t)s] == Value;
        }
    }
    free(Black);
    free(Symbols);
    return Same;
}

// Attaches the channel to PORT for VidStd in TxMode and holds. Returns false, having
// recorded a failure, when that fails.
static bool Hold(Fixture* Fix, int VidStd, int TxMode, int* DtFailures)
{
    DtapiResult Result = SetStandard(Fix, VidStd);

    if (Result == DTAPI_OK)
        Result = DtOutpChannel_AttachToPort(Fix->Channel, Fix->Device, PORT);
    if (Result == DTAPI_OK)
        Result = DtOutpChannel_SetTxMode(Fix->Channel, TxMode, 0);
    if (Result == DTAPI_OK)
        Result = DtOutpChannel_SetTxControl(Fix->Channel, DTAPI_TXCTRL_HOLD);
    if (Result != DTAPI_OK)
    {
        printf("    FAIL: cannot hold: %s\n", DtapiResult2Str(Result));
        (*DtFailures)++;
    }
    return Result == DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(NullAndDetached)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;

    DtOutpChannel_Free(NULL);
    DtOutpChannel_Freep(NULL);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(NULL, Fix.Device, PORT), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_ClearFifo(NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_Detach(NULL, 0), DTAPI_E_INVALID_ARG);
    int Value;
    DT_ASSERT_EQ(DtOutpChannel_GetFifoLoad(NULL, &Value), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_GetFifoLoad(Fix.Channel, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_GetFifoSize(Fix.Channel, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_GetMaxFifoSize(Fix.Channel, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_GetFlags(Fix.Channel, &Value, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetIoConfig(NULL, DTAPI_IOCONFIG_IOSTD,
                                           DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(NULL, DTAPI_TXCTRL_HOLD),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(NULL, DTAPI_TXMODE_SDI_FULL, 0),
                 DTAPI_E_INVALID_ARG);
    char Data[8];
    DT_ASSERT_EQ(DtOutpChannel_Write(NULL, Data, 8), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(NULL, Data, 8, 10), DTAPI_E_INVALID_ARG);

    // Detached: the checks DTAPI makes before it looks first.
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, Data, -4), DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, 0x10 | DTAPI_TXMODE_SDI_FULL, 0),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtOutpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD, 12345, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, Data, 8), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Data, 8, 10),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_Detach(Fix.Channel, 3), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_ClearFifo(Fix.Channel), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_GetFifoLoad(Fix.Channel, &Value), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_GetFifoSize(Fix.Channel, &Value), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_GetMaxFifoSize(Fix.Channel, &Value), DTAPI_E_NOT_ATTACHED);
    int Other;
    DT_ASSERT_EQ(DtOutpChannel_GetFlags(Fix.Channel, &Value, &Other),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD),
                 DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_SDI_FULL, 0),
                 DTAPI_E_NOT_ATTACHED);
    FINISH(Fix);
}

DT_TEST(AttachChecks)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DtDevice* Detached = DtDevice_Alloc();
    DtOutpChannel* Second = DtOutpChannel_Alloc();

    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, NULL, PORT), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Detached, PORT), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, 0),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, SIM_PORT_COUNT + 1),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT_INPUT),
                 DTAPI_E_NO_DT_OUTPUT);

    Fix.Device->Info.FirmwareStatus = DT_FWSTATUS_OBSOLETE;
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_OBSOLETE_FW);
    Fix.Device->Info.FirmwareStatus = DT_FWSTATUS_UPTODATE;

    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_ATTACHED);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Second, Fix.Device, PORT), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 2);

    // The blocks as MxChannelMemlessTx leaves them for 1080i50: idle, the encoder's
    // corrections on, the bypass of the demultiplexer, and a buffer of 128 MB.
    SimTxState State;
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT(State.CdmacMode == DT_BLOCK_OPMODE_IDLE &&
              State.TxfMode == DT_BLOCK_OPMODE_IDLE &&
              State.PhyMode == DT_FUNC_OPMODE_IDLE);
    DT_ASSERT(State.Clamp && State.AncChecksum && State.LineCrc);
    DT_ASSERT(State.SwitchIn[0] == 0 && State.SwitchIn[1] == 0 &&
              State.SwitchOut[0] == 0 && State.SwitchOut[1] == 0);
    DT_ASSERT(State.BufferRegistered);
    DT_ASSERT_EQ(State.BufferSize, 128 * 1024 * 1024);
    DT_ASSERT_EQ(State.NumLinesPerEvent, 283);
    DT_ASSERT_EQ(State.NumSofsBetweenTod, 1);
    DT_ASSERT_EQ(State.TestMode, DT_CDMAC_TESTMODE_NORMAL);

    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, 0));
    DT_ASSERT_EQ(DtOutpChannel_Detach(Fix.Channel, 0), DTAPI_E_NOT_ATTACHED);
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT(!State.BufferRegistered);

    DtOutpChannel_Free(Second);
    DtDevice_Free(Detached);
    FINISH(Fix);
}

// An ASI standard, a failing command, and a 4K standard, which attaches but does not
// leave idle.
DT_TEST(AttachRefusals)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;

    DT_ASSERT_OK(DtDevice_SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                                      DTAPI_IOCONFIG_ASI, -1));
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_OK(SetStandard(&Fix, DTAPI_VIDSTD_1080I50));

    SimDtPcie_FailTxCmd(DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER,
                        DT_STATUS_OUT_OF_MEMORY);
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_OUT_OF_MEM);
    SimDtPcie_FailTxCmd(DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER, 0);
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 1);

    // The exclusive access was released: another attach succeeds.
    SimDtPcie_OverrideProperty("CAP_12GSDI", PORT - 1, true, 1);
    SimDtPcie_OverrideProperty("CAP_2160P50", PORT - 1, true, 1);
    DT_ASSERT_OK(DtDevice_SetIoConfig(Fix.Device, PORT, DTAPI_IOCONFIG_IOSTD,
                                      DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD),
                 DTAPI_E_CONFIG_RAW_SDI);
    int Size;
    DT_ASSERT_OK(DtOutpChannel_GetFifoSize(Fix.Channel, &Size));
    DT_ASSERT_EQ(Size, 48 * 1024 * 1024);
    DT_ASSERT_OK(DtOutpChannel_GetMaxFifoSize(Fix.Channel, &Size));
    DT_ASSERT_EQ(Size, 64 * 1024 * 1024);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(TransmitModes)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    // 1080i50 in a 128 MB buffer: 18 coded frames of 7,434,032 bytes.
    int Size;
    DT_ASSERT_OK(DtOutpChannel_GetFifoSize(Fix.Channel, &Size));
    DT_ASSERT_EQ(Size, 18 * 7425000);
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Channel, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_16B, 0));
    DT_ASSERT_OK(DtOutpChannel_GetMaxFifoSize(Fix.Channel, &Size));
    DT_ASSERT_EQ(Size, 18 * 11880000);

    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_SDI_ACTVID, 0),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, 0x10, 0), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel,
                                         DTAPI_TXMODE_SDI | DTAPI_TXMODE_SDI_HUFFMAN, 0),
                 DTAPI_E_INVALID_MODE);

    // DTAPI_TXMODE_SDI alone is the full frame in 8 bits, which holds and takes frames
    // but does not send them, as DTAPI does on the card: the load is checked first.
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_SDI, 0));
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND),
                 DTAPI_E_INSUF_LOAD);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND),
                 DTAPI_E_INSUF_LOAD);
    size_t Size16;
    uint8_t* Frame16 = MakeFrame(DTAPI_VIDSTD_1080I50, 0, 16, &Size16);
    DT_ASSERT(Frame16 != NULL);
    for (size_t i = 0; i < Size16 / 2; i++)
        Frame16[i] = (uint8_t)((Frame16[2 * i] | Frame16[2 * i + 1] << 8) >> 2);
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Frame16, (int)(Size16 / 2)));
    free(Frame16);
    int Load;
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 5940000);
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND),
                 DTAPI_E_CONFIG_RAW_SDI);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_IDLE));

    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_SDI_FULL, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Channel, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_SDI_FULL, 0),
                 DTAPI_E_NOT_IDLE);
    FINISH(Fix);
}

// The checks of SetIoConfig, a new standard with a new buffer, and the transmit mode kept
// across it.
DT_TEST(IoConfiguration)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(
        Fix.Channel, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_16B, 0));

    DT_ASSERT_EQ(DtOutpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                           DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IODIR,
                                           DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                           DTAPI_IOCONFIG_ASI, -1),
                 DTAPI_E_NOT_SUPPORTED);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_EQ(DtOutpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                           DTAPI_IOCONFIG_SDI, DTAPI_IOCONFIG_525I59_94),
                 DTAPI_E_NOT_IDLE);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_IDLE));

    // 525i59.94: 1,801,800 bytes per 16-bit frame, 1,134,032 per coded frame, 128 MB.
    DT_ASSERT_OK(DtOutpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                           DTAPI_IOCONFIG_SDI, DTAPI_IOCONFIG_525I59_94));
    SimTxState State;
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT(State.BufferRegistered);
    DT_ASSERT_EQ(State.NumLinesPerEvent, 133);
    int Size;
    DT_ASSERT_OK(DtOutpChannel_GetFifoSize(Fix.Channel, &Size));
    DT_ASSERT_EQ(Size, (int)((State.BufferSize - 32) / 1134032 * 1801800));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= States +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(States)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(SetStandard(&Fix, DTAPI_VIDSTD_525I59_94));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    char Data[8];
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, Data, 8), DTAPI_E_IDLE);
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, 7), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_IDLE));

    // Sending from idle holds first, and then needs a frame.
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND),
                 DTAPI_E_INSUF_LOAD);
    SimTxState State;
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT(
        State.CdmacMode == DT_BLOCK_OPMODE_RUN &&
        State.BurstMode == DT_BLOCK_OPMODE_RUN && State.TxfMode == DT_BLOCK_OPMODE_RUN &&
        State.SwitchInMode == DT_BLOCK_OPMODE_RUN &&
        State.DmxMode == DT_BLOCK_OPMODE_IDLE &&
        State.SwitchOutMode == DT_BLOCK_OPMODE_RUN &&
        State.TxpMode == DT_BLOCK_OPMODE_RUN && State.PhyMode == DT_FUNC_OPMODE_STANDBY);
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Data, 0));
    int Load;
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 0);

    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 0, 10));
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 1126128);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT_EQ(State.PhyMode, DT_FUNC_OPMODE_RUN);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT_EQ(State.PhyMode, DT_FUNC_OPMODE_STANDBY);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_IDLE));
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT(State.CdmacMode == DT_BLOCK_OPMODE_IDLE &&
              State.TxfMode == DT_BLOCK_OPMODE_IDLE &&
              State.PhyMode == DT_FUNC_OPMODE_IDLE);
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 0);
    FINISH(Fix);
}

// A refused command leaves the blocks idle and the channel idle.
DT_TEST(RefusedCommands)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    SimDtPcie_FailTxCmd(DT_FUNC_CODE_SDITXP_CMD, DT_SDITXP_CMD_SET_OPERATIONAL_MODE,
                        DT_STATUS_OUT_OF_MEMORY);
    DT_ASSERT(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD) >= DTAPI_E);
    SimDtPcie_FailTxCmd(DT_FUNC_CODE_SDITXP_CMD, DT_SDITXP_CMD_SET_OPERATIONAL_MODE, 0);
    SimTxState State;
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT(State.CdmacMode == DT_BLOCK_OPMODE_IDLE &&
              State.TxfMode == DT_BLOCK_OPMODE_IDLE);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(WriteChecks)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(SetStandard(&Fix, DTAPI_VIDSTD_525I59_94));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));

    // A misaligned buffer or size overrides idle, as in DTAPI.
    uint8_t Data[16];
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, (char*)Data + 1, 8),
                 DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, (char*)Data, 6), DTAPI_E_INVALID_BUF);

    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, NULL, 8), DTAPI_E_IDLE);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, NULL, 8), DTAPI_E_INVALID_BUF);
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, NULL, 0));
    memset(Data, 0, sizeof(Data));
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, (char*)Data, 16));
    FINISH(Fix);
}

// Frames written in pieces of many sizes reach the card bit for bit, with frame IDs from
// 0. In HD and 3G bytes before the first frame and between frames are skipped; in SD
// they would make the search wait for field 2 of the next frame, see SdStartsAtField1.
static void FramesInPieces(int VidStd, int Bits, int NumFrames, int* DtFailures)
{
    static const size_t Pieces[] = {1000, 4, 8, 12, 20, 65536, 36, 4, 3000000, 16};
    int TxMode = DTAPI_TXMODE_SDI_FULL |
                 (Bits == 16 ? DTAPI_TXMODE_SDI_16B : DTAPI_TXMODE_SDI_10B);
    int Load;
    int i;

    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, VidStd, TxMode, DtFailures))
    {
        FINISH(Fix);
        return;
    }

    for (i = 0; i < NumFrames; i++)
    {
        size_t Skip = VidStd == DTAPI_VIDSTD_525I59_94 ? 0 : 4 * (size_t)i + 28;
        size_t Size;
        uint8_t* Frame = MakeFrame(VidStd, (uint32_t)i, Bits, &Size);
        uint8_t* Data = (uint8_t*)calloc(Size + Skip, 1);

        // Zeros first, in the same writes as the frame.
        DT_ASSERT(Frame != NULL && Data != NULL);
        memcpy(Data + Skip, Frame, Size);
        DT_ASSERT_OK(WriteInPieces(Fix.Channel, Data, Size + Skip, Pieces + i, 4));
        free(Data);
        free(Frame);
    }
    DtSdiFrameLayout Layout;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, VidStd, SIM_TX_STREAM_ALIGNMENT));
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, NumFrames * (int)DtSdiFrame_RawSize(&Layout, Bits));

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(SentAndHeld(Fix.Channel, NumFrames));
    for (i = 0; i < NumFrames; i++)
    {
        if (!SentFrameIs(i, VidStd, (uint32_t)i))
        {
            printf("    FAIL: standard %d, %d bits: frame %d differs\n", VidStd, Bits, i);
            (*DtFailures)++;
        }
    }
    FINISH(Fix);
}

DT_TEST(FramesInPieces525i)
{
    FramesInPieces(DTAPI_VIDSTD_525I59_94, 10, 3, DtFailures);
    FramesInPieces(DTAPI_VIDSTD_525I59_94, 16, 2, DtFailures);
}

DT_TEST(FramesInPieces720p24)
{
    FramesInPieces(DTAPI_VIDSTD_720P24, 10, 2, DtFailures);
}

DT_TEST(FramesInPieces1080p50)
{
    FramesInPieces(DTAPI_VIDSTD_1080P50, 16, 2, DtFailures);
}

// An SD stream that starts in the active part of field 1 is picked up at line 1 of the
// next frame, not at the blanking lines at the end of field 1, which match line 1 too.
DT_TEST(SdStartsAtField1)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }

    // From line 30 of frame 0, on a four-byte boundary from its end, then frame 1. Line
    // 261 of frame 0 then starts on such a boundary.
    size_t Size;
    uint8_t* Frame = MakeFrame(DTAPI_VIDSTD_525I59_94, 0, 10, &Size);
    DT_ASSERT(Frame != NULL);
    size_t Tail = (Size - 29 * 2145) / 4 * 4;
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, (char*)Frame + Size - Tail, (int)Tail));
    free(Frame);
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 1, 10));

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(SentAndHeld(Fix.Channel, 1));
    DT_ASSERT(SentFrameIs(0, DTAPI_VIDSTD_525I59_94, 1));
    FINISH(Fix);
}

// Frames go on across the end of the buffer, and a write waits for room while sending.
DT_TEST(AcrossTheEndOfTheBuffer)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_1080I50, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }

    // The buffer holds 18 frames; the 19th and 20th wait while the first go out, and the
    // 19th has a line across the end of the buffer.
    int i;
    for (i = 0; i < 18; i++)
        DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, (uint32_t)i, 10));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    for (i = 18; i < 20; i++)
        DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, (uint32_t)i, 10));

    DT_ASSERT(SentAndHeld(Fix.Channel, 20));
    DT_ASSERT(SentFrameIs(18, DTAPI_VIDSTD_1080I50, 18));
    DT_ASSERT(SentFrameIs(19, DTAPI_VIDSTD_1080I50, 19));
    SimTxState State;
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT_EQ(State.HeaderErrors, 0);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= WriteFrame +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// WriteFrame's checks in their order. A refused frame leaves nothing in the buffer.
DT_TEST(WriteFrameChecks)
{
    Fixture Fix;
    int Load = -1;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(SetStandard(&Fix, DTAPI_VIDSTD_1080I50));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    size_t Size;
    uint8_t* Frame = MakeFrame(DTAPI_VIDSTD_1080I50, 0, 10, &Size);
    DT_ASSERT(Frame != NULL);

    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, 0),
                 DTAPI_E_INVALID_TIMEOUT);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, -2),
                 DTAPI_E_INVALID_TIMEOUT);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, 0, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, -4, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size - 2, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, NULL, (int)Size, 10),
                 DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame + 1, (int)Size, 10),
                 DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, 10),
                 DTAPI_E_IDLE);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size - 4, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size + 4, 10),
                 DTAPI_E_INVALID_SIZE);

    // Line 2 first, and line 1 with another line number.
    uint8_t* Shifted = (uint8_t*)calloc(Size, 1);
    DT_ASSERT(Shifted != NULL);
    memcpy(Shifted, Frame + 6600, Size - 6600);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Shifted, (int)Size, 10),
                 DTAPI_E_INVALID_FRAME);
    memcpy(Shifted, Frame, Size);
    Shifted[10] ^= 0x10;
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Shifted, (int)Size, 10),
                 DTAPI_E_INVALID_FRAME);
    free(Shifted);
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 0);

    // Part of a frame from Write, until Write completes it.
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Frame, 4000));
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, 10),
                 DTAPI_E_INCOMP_FRAME);
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Frame + 4000, (int)Size - 4000));
    DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, 10));

    // Bytes too few to tell whether they start a frame, until ClearFifo.
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Frame, 8));
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, 10),
                 DTAPI_E_INCOMP_FRAME);
    DT_ASSERT_OK(DtOutpChannel_ClearFifo(Fix.Channel));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Channel, Frame, (int)Size, 10));
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, (int)Size);
    free(Frame);
    FINISH(Fix);
}

// In SD, where lines have no numbers, a frame that starts in the active part of field 1
// is refused, and the frame after it goes out as frame 0.
DT_TEST(WriteFrameChecksTheSdStart)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }

    // From line 30, which starts at a byte boundary.
    size_t Size;
    uint8_t* Frame = MakeFrame(DTAPI_VIDSTD_525I59_94, 0, 10, &Size);
    uint8_t* Shifted = (uint8_t*)calloc(Size, 1);
    DT_ASSERT(Frame != NULL && Shifted != NULL);
    memcpy(Shifted, Frame + 29 * 2145, Size - 29 * 2145);
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Shifted, (int)Size, 10),
                 DTAPI_E_INVALID_FRAME);
    free(Shifted);
    free(Frame);

    DT_ASSERT_OK(WriteWholeFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 1, 10, 100));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(SentAndHeld(Fix.Channel, 1));
    DT_ASSERT(SentFrameIs(0, DTAPI_VIDSTD_525I59_94, 1));
    FINISH(Fix);
}

// Frames from WriteFrame, alternated with frames from Write, reach the card bit for bit
// with consecutive frame IDs.
static void WholeFrames(int VidStd, int Bits, int NumFrames, int* DtFailures)
{
    int TxMode = DTAPI_TXMODE_SDI_FULL |
                 (Bits == 16 ? DTAPI_TXMODE_SDI_16B : DTAPI_TXMODE_SDI_10B);
    int Load;
    int i;

    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, VidStd, TxMode, DtFailures))
    {
        FINISH(Fix);
        return;
    }

    for (i = 0; i < NumFrames; i++)
    {
        if (i % 2 == 0)
            DT_ASSERT_OK(WriteWholeFrame(Fix.Channel, VidStd, (uint32_t)i, Bits, 100));
        else
            DT_ASSERT_OK(WriteFrame(Fix.Channel, VidStd, (uint32_t)i, Bits));
    }
    DtSdiFrameLayout Layout;
    DT_ASSERT(DtSdiFrame_LayoutInit(&Layout, VidStd, SIM_TX_STREAM_ALIGNMENT));
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, NumFrames * (int)DtSdiFrame_RawSize(&Layout, Bits));

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(SentAndHeld(Fix.Channel, NumFrames));
    for (i = 0; i < NumFrames; i++)
    {
        if (!SentFrameIs(i, VidStd, (uint32_t)i))
        {
            printf("    FAIL: standard %d, %d bits: frame %d differs\n", VidStd, Bits, i);
            (*DtFailures)++;
        }
    }
    FINISH(Fix);
}

DT_TEST(WholeFrames525i)
{
    WholeFrames(DTAPI_VIDSTD_525I59_94, 10, 3, DtFailures);
    WholeFrames(DTAPI_VIDSTD_525I59_94, 16, 2, DtFailures);
}

DT_TEST(WholeFrames720p24)
{
    WholeFrames(DTAPI_VIDSTD_720P24, 10, 3, DtFailures);
}

DT_TEST(WholeFrames1080p50)
{
    WholeFrames(DTAPI_VIDSTD_1080P50, 16, 2, DtFailures);
}

// A frame for which the buffer has no room within the time-out is refused, nothing of it
// is written and its frame ID stays free; once there is room it is written.
DT_TEST(WriteFrameTimesOut)
{
    Fixture Fix;
    int Load = 0;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_1080I50, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }

    // The buffer holds 18 frames.
    int i;
    for (i = 0; i < 18; i++)
        DT_ASSERT_OK(
            WriteWholeFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, (uint32_t)i, 10, 10));
    uint64_t Start0 = OsTime_MonotonicMs();
    DT_ASSERT_EQ(WriteWholeFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, 18, 10, 60),
                 DTAPI_E_TIMEOUT);
    uint64_t Took = OsTime_MonotonicMs() - Start0;
    DT_ASSERT(Took >= 60 && Took < 1000);
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 18 * 7425000);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(WriteWholeFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, 18, 10, -1));
    DT_ASSERT(SentAndHeld(Fix.Channel, 19));
    DT_ASSERT(SentFrameIs(17, DTAPI_VIDSTD_1080I50, 17));
    DT_ASSERT(SentFrameIs(18, DTAPI_VIDSTD_1080I50, 18));
    FINISH(Fix);
}

typedef struct FrameWriter
{
    DtOutpChannel* Channel;
    int Written;
    DtapiResult Result;
} FrameWriter;

// Writes 1080i50 frames with WriteFrame, without a time-out, until one fails.
static void WriteFramesUntilFailure(void* Context)
{
    FrameWriter* W = (FrameWriter*)Context;
    size_t Size;
    uint8_t* Frame = MakeFrame(DTAPI_VIDSTD_1080I50, 0, 10, &Size);

    W->Result = DTAPI_E_OUT_OF_MEM;
    while (Frame != NULL)
    {
        W->Result = DtOutpChannel_WriteFrame(W->Channel, Frame, (int)Size, -1);
        if (W->Result != DTAPI_OK)
            break;
        W->Written++;
    }
    free(Frame);
}

// While a WriteFrame on another thread waits for room, Write and WriteFrame are refused;
// a detach ends the wait.
DT_TEST(WriteFrameInUseAndCancelled)
{
    Fixture Fix;
    int Load = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));

    FrameWriter W;
    W.Channel = Fix.Channel;
    W.Written = 0;
    W.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(WriteFramesUntilFailure, &W);
    DT_ASSERT(Thread != NULL);

    uint64_t Start0 = OsTime_MonotonicMs();
    while (Load < 18 * 7425000 && OsTime_MonotonicMs() - Start0 < SEND_TIMEOUT_MS)
    {
        OsTime_SleepMs(10);
        DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    }
    DT_ASSERT_EQ(Load, 18 * 7425000);
    OsTime_SleepMs(20);

    DT_ASSERT_EQ(WriteWholeFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, 1, 10, 20),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(WriteFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, 1, 10), DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, 1));
    OsThread_Join(Thread);
    DT_ASSERT_EQ(W.Result, DTAPI_E_CANCELLED);
    DT_ASSERT_EQ(W.Written, 18);
    FINISH(Fix);
}

// The number of frames the card has sent.
static int FramesSent(void)
{
    SimTxState State;

    SimDtPcie_GetTxState(PORT - 1, &State);
    return State.FramesSent;
}

// Frames written one at a time, each after the card sent two more frames, so that black
// frames from the thread come between them, go out whole: every frame the card sent is
// black or one of them, in the order written.
DT_TEST(WholeFramesAmongBlackFrames)
{
    Fixture Fix;
    int VidStd = DTAPI_VIDSTD_525I59_94;
    uint8_t* Frames[4] = {NULL, NULL, NULL, NULL};
    size_t Size = 0;
    int Found = 0;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, VidStd, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B, DtFailures))
    {
        FINISH(Fix);
        return;
    }
    for (uint32_t n = 0; n < 4; n++)
    {
        Frames[n] = MakeFrame(VidStd, n, 10, &Size);
        DT_ASSERT(Frames[n] != NULL);
    }

    DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Channel, Frames[0], (int)Size, 100));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    for (int n = 1; n < 4; n++)
    {
        DT_ASSERT(WaitForFrames(FramesSent() + 2));
        DT_ASSERT_OK(DtOutpChannel_WriteFrame(Fix.Channel, Frames[n], (int)Size, 1000));
    }
    DT_ASSERT(SentAndHeld(Fix.Channel, FramesSent() + 3));
    for (int n = 0; n < 4; n++)
        free(Frames[n]);

    int Count = SimDtPcie_TxFrameCount(PORT - 1);
    uint32_t Next = 0;
    for (int Index = 0; Index < Count; Index++)
    {
        SimTxFrame Frame;
        DT_ASSERT(SimDtPcie_GetTxFrame(PORT - 1, Index, &Frame));
        int Id = Frame.FrameId;
        if (SentFrameIsBlack(Id, VidStd))
            continue;
        while (Next < 4 && !SentFrameIs(Id, VidStd, Next))
            Next++;
        DT_ASSERT(Next < 4);
        Next++;
        Found++;
    }
    DT_ASSERT(Found >= 2);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Signal +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// When the application stops writing, black frames follow with consecutive frame IDs,
// and the underflow flag is latched until ClearFifo.
DT_TEST(BlackFramesWhenWritingStops)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 0, 10));
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 1, 10));
    int Status;
    int Latched;
    DT_ASSERT_OK(DtOutpChannel_GetFlags(Fix.Channel, &Status, &Latched));
    DT_ASSERT(Status == 0 && Latched == 0);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(SentAndHeld(Fix.Channel, 4));
    DT_ASSERT(SentFrameIs(0, DTAPI_VIDSTD_525I59_94, 0));
    DT_ASSERT(SentFrameIs(1, DTAPI_VIDSTD_525I59_94, 1));
    DT_ASSERT(SentFrameIsBlack(2, DTAPI_VIDSTD_525I59_94));
    DT_ASSERT(SentFrameIsBlack(3, DTAPI_VIDSTD_525I59_94));

    DT_ASSERT_OK(DtOutpChannel_GetFlags(Fix.Channel, &Status, &Latched));
    DT_ASSERT((Latched & DTAPI_TX_FIFO_UFL) != 0);
    DT_ASSERT_OK(DtOutpChannel_ClearFifo(Fix.Channel));
    DT_ASSERT_OK(DtOutpChannel_GetFlags(Fix.Channel, &Status, &Latched));
    DT_ASSERT(Status == 0 && Latched == 0);
    FINISH(Fix);
}

// A frame the application has only partly written when the card runs short follows the
// black frames whole, with the next frame ID.
DT_TEST(BlackFrameBeforeAPartlyWrittenFrame)
{
    Fixture Fix;
    int FirstNotBlack = -1;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 0, 10));
    size_t Size;
    uint8_t* Frame = MakeFrame(DTAPI_VIDSTD_525I59_94, 1, 10, &Size);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, (char*)Frame, (int)(Size / 2 / 4 * 4)));

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(WaitForFrames(2));
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, (char*)Frame + Size / 2 / 4 * 4,
                                     (int)(Size - Size / 2 / 4 * 4)));
    free(Frame);

    DT_ASSERT(SentAndHeld(Fix.Channel, SIM_TX_KEPT_FRAMES - 2));
    DT_ASSERT(SentFrameIs(0, DTAPI_VIDSTD_525I59_94, 0));
    DT_ASSERT(SentFrameIsBlack(1, DTAPI_VIDSTD_525I59_94));
    for (int Id = 2; Id < SIM_TX_KEPT_FRAMES - 2 && FirstNotBlack < 0; Id++)
    {
        if (!SentFrameIsBlack(Id, DTAPI_VIDSTD_525I59_94))
            FirstNotBlack = Id;
    }
    DT_ASSERT(FirstNotBlack > 1);
    DT_ASSERT(SentFrameIs(FirstNotBlack, DTAPI_VIDSTD_525I59_94, 1));
    FINISH(Fix);
}

// The formatter's and the transmitter's underflow give the two flags.
DT_TEST(UnderflowFlags)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 0, 10));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(WaitForFrames(1));
    SimDtPcie_StarveTx(PORT - 1, 3);

    uint64_t Start0 = OsTime_MonotonicMs();
    int Status;
    int Latched;
    do
    {
        OsTime_SleepMs(10);
        DT_ASSERT_OK(DtOutpChannel_GetFlags(Fix.Channel, &Status, &Latched));
    } while ((Latched & DTAPI_TX_DMA_UFL) == 0 &&
             OsTime_MonotonicMs() - Start0 < SEND_TIMEOUT_MS);
    DT_ASSERT((Latched & DTAPI_TX_DMA_UFL) != 0);
    DT_ASSERT((Latched & DTAPI_TX_FIFO_UFL) != 0);
    FINISH(Fix);
}

// A read offset of an earlier run, as the card reports right after the DMA controller
// starts, does not keep a write waiting.
DT_TEST(StaleReadOffset)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(SetStandard(&Fix, DTAPI_VIDSTD_525I59_94));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    SimDtPcie_StaleTxReadOffset(PORT - 1, 24576, 1000);

    uint64_t Start0 = OsTime_MonotonicMs();
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 0, 10));
    DT_ASSERT(OsTime_MonotonicMs() - Start0 < 500);
    int Load;
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 1126128);
    SimDtPcie_StaleTxReadOffset(PORT - 1, 0, 0);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Detach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct Writer
{
    DtOutpChannel* Channel;
    DtapiResult Result;
} Writer;

// Writes 1080i50 frames until a write fails.
static void WriteUntilFailure(void* Context)
{
    Writer* W = (Writer*)Context;
    size_t Size;
    uint8_t* Frame = MakeFrame(DTAPI_VIDSTD_1080I50, 0, 10, &Size);

    W->Result = DTAPI_E_OUT_OF_MEM;
    while (Frame != NULL)
    {
        W->Result = DtOutpChannel_Write(W->Channel, (char*)Frame, (int)Size);
        if (W->Result != DTAPI_OK)
            break;
    }
    free(Frame);
}

// A write waiting for room while holding is ended by a detach. While it waits, Write and
// WriteFrame are refused.
DT_TEST(DetachCancelsAWrite)
{
    Fixture Fix;
    int Load = 0;

    if (!Start(&Fix, DtFailures))
        return;
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));

    Writer W;
    W.Channel = Fix.Channel;
    W.Result = DTAPI_OK;
    OsThread* Thread = OsThread_Start(WriteUntilFailure, &W);
    DT_ASSERT(Thread != NULL);

    uint64_t Start0 = OsTime_MonotonicMs();
    while (Load < 18 * 7425000 && OsTime_MonotonicMs() - Start0 < SEND_TIMEOUT_MS)
    {
        OsTime_SleepMs(10);
        DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
    }
    DT_ASSERT_EQ(Load, 18 * 7425000);
    OsTime_SleepMs(20);

    uint8_t Data[8] = {0};
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, Data, 8), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(WriteWholeFrame(Fix.Channel, DTAPI_VIDSTD_1080I50, 1, 10, 20),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, 1));
    OsThread_Join(Thread);
    DT_ASSERT_EQ(W.Result, DTAPI_E_CANCELLED);
    FINISH(Fix);
}

// Detaching when everything is sent, and the flags that cannot go together.
DT_TEST(DetachWaitsUntilSent)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }
    for (int i = 0; i < 3; i++)
        DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, (uint32_t)i, 10));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));

    DT_ASSERT_EQ(DtOutpChannel_Detach(Fix.Channel, 3), DTAPI_E_INVALID_FLAGS);
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, 2));
    SimTxState State;
    SimDtPcie_GetTxState(PORT - 1, &State);
    DT_ASSERT_EQ(State.FramesSent, 3);
    DT_ASSERT(SentFrameIs(2, DTAPI_VIDSTD_525I59_94, 2));
    FINISH(Fix);
}

// Freeing a sending channel stops its thread.
DT_TEST(FreeWhileSending)
{
    Fixture Fix;

    if (!Start(&Fix, DtFailures))
        return;
    if (!Hold(&Fix, DTAPI_VIDSTD_525I59_94, DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B,
              DtFailures))
    {
        FINISH(Fix);
        return;
    }
    DT_ASSERT_OK(WriteFrame(Fix.Channel, DTAPI_VIDSTD_525I59_94, 0, 10));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT(WaitForFrames(3));
    FINISH(Fix);
}

DT_TEST_MAIN("SimOutpChannel", DT_RUN(NullAndDetached), DT_RUN(AttachChecks),
             DT_RUN(AttachRefusals), DT_RUN(TransmitModes), DT_RUN(IoConfiguration),
             DT_RUN(States), DT_RUN(RefusedCommands), DT_RUN(WriteChecks),
             DT_RUN(FramesInPieces525i), DT_RUN(FramesInPieces720p24),
             DT_RUN(FramesInPieces1080p50), DT_RUN(SdStartsAtField1),
             DT_RUN(AcrossTheEndOfTheBuffer), DT_RUN(BlackFramesWhenWritingStops),
             DT_RUN(BlackFrameBeforeAPartlyWrittenFrame), DT_RUN(UnderflowFlags),
             DT_RUN(StaleReadOffset), DT_RUN(DetachCancelsAWrite),
             DT_RUN(DetachWaitsUntilSent), DT_RUN(FreeWhileSending),
             DT_RUN(WriteFrameChecks), DT_RUN(WriteFrameChecksTheSdStart),
             DT_RUN(WholeFrames525i), DT_RUN(WholeFrames720p24),
             DT_RUN(WholeFrames1080p50), DT_RUN(WriteFrameTimesOut),
             DT_RUN(WriteFrameInUseAndCancelled), DT_RUN(WholeFramesAmongBlackFrames))

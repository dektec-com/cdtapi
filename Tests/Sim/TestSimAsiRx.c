// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimAsiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtInpChannel on an ASI port of the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state with port 1 an ASI input receiving the emulator's numbered packets, one read of
// the write offset at a time, and ends with no handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtPcieAbi.h"              // Driver values.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles.
#include "OAL/OsThread.h"           // A reader on another thread.
#include "OAL/Sim/SimAsi.h"         // The emulated ASI blocks and their source.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "OAL/Sim/SimSdiTx.h"       // Real time.
#include "cdtapi.h"                 // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define PORT 1
#define BUFFER_SIZE (9 * 1024 * 1024)

typedef struct Fixture
{
    int Live;
    DtDevice* Device;
    DtInpChannel* Channel;
    uint8_t* Buffer; // BUFFER_SIZE bytes
} Fixture;

// One I/O configuration through the device, which takes a list.
static DtapiResult SetIoConfig(DtDevice* Device, int Group, int Value, int SubValue)
{
    DtIoConfig Config = {PORT, Group, Value, SubValue, {-1, -1}};
    return DtDevice_SetIoConfig(Device, &Config, 1);
}

// Resets the emulator without real time, makes the port an ASI input with the default
// source, and attaches a channel to it. Returns false, having recorded a failure, when
// that is not possible.
static bool Start(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    SimDtPcie_SetTxRealTime(false);
    Fix->Live = DtAlloc_Live();
    Fix->Device = DtDevice_Alloc();
    Fix->Channel = DtInpChannel_Alloc();
    Fix->Buffer = (uint8_t*)malloc(BUFFER_SIZE);

    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    bool Emulated = Drv != NULL && OsDrv_IsEmulated(Drv);
    OsDrv_Close(Drv);
    SimAsiSource Source;
    SimAsi_DefaultSource(&Source);
    SimDtPcie_SetAsiSource(PORT - 1, &Source);
    if (!Emulated || Fix->Device == NULL || Fix->Channel == NULL || Fix->Buffer == NULL ||
        DtDevice_AttachToSerial(Fix->Device, SIM_SERIAL) != DTAPI_OK ||
        SetIoConfig(Fix->Device, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1) !=
            DTAPI_OK ||
        DtInpChannel_AttachToPort(Fix->Channel, Fix->Device, PORT) != DTAPI_OK)
    {
        printf("    FAIL: no emulated ASI input; is CDTAPI_SIM=1 set?\n");
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

// Restarts the source with packets of Size bytes from number 0, pieces without sync
// first.
static void Restart(int Size, int PacketsPerRead)
{
    SimAsiSource Source;
    SimAsi_DefaultSource(&Source);
    Source.PacketSize = Size;
    Source.PacketsPerRead = PacketsPerRead;
    SimDtPcie_SetAsiSource(PORT - 1, &Source);
}

// Whether the Count packets of Size bytes at Data are numbered from First, each Stride
// bytes after the one before and Offset bytes into its stride.
static bool ArePackets(const uint8_t* Data, int Count, uint32_t First, int Size,
                       int Stride, int Offset)
{
    uint8_t Want[204];
    for (int i = 0; i < Count; i++)
    {
        SimAsi_MakePacket(First + (uint32_t)i, Size, Want);
        if (memcmp(Data + (size_t)i * (size_t)Stride + (size_t)Offset, Want,
                   (size_t)Size) != 0)
            return false;
    }
    return true;
}

// The number of the 188-byte or larger packet at P.
static uint32_t NumberOf(const uint8_t* P)
{
    return (uint32_t)P[4] << 24 | (uint32_t)P[5] << 16 | (uint32_t)P[6] << 8 | P[7];
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// An ASI port attaches with DTAPI's defaults; the SDI functions are refused and Read's
// arguments are checked in DTAPI's order.
DT_TEST(AttachesWithDefaults)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    int Value = 0, SubValue = 0;

    DT_ASSERT_OK(DtInpChannel_GetMaxFifoSize(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 8 * 1024 * 1024);
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 0);
    DT_ASSERT_EQ(DtInpChannel_DetectIoStd(Fix.Channel, &Value, &SubValue),
                 DTAPI_E_NOT_SUPPORTED);
    int Size = BUFFER_SIZE;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                 DTAPI_E_NOT_SDI_MODE);
    SimAsiState State;
    SimDtPcie_GetAsiState(PORT - 1, &State);
    DT_ASSERT_EQ(State.RxMode, DT_FUNC_OPMODE_IDLE);
    DT_ASSERT_EQ(State.RxPacketMode, DT_ASIRX_PCKMODE_AUTO);
    DT_ASSERT_EQ(State.RxPolarityCtrl, DT_ASIRX_POLARITY_AUTO);
    DT_ASSERT_EQ(State.RxSyncMode, DT_ASIRX_SYNCMODE_AUTO);

    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, NULL, 0, -5));
    DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 188, -2),
                 DTAPI_E_INVALID_TIMEOUT);
    DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 190, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer + 2, 188, 10),
                 DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 8 * 1024 * 1024 + 4, 10),
                 DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 188, 20), DTAPI_E_TIMEOUT);

    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_SDI_FULL),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_ST188 | DTAPI_RXMODE_TIMESTAMP64),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_SetRxControl(Fix.Channel, 7), DTAPI_E_INVALID_ARG);
    FINISH(Fix);
}

// Every receive mode delivers what DTAPI's converter does of the packets the card
// received: whole packets, 204-byte ones, the valid bytes, the card's pieces as they
// came, or the transparent packets, with a time stamp before each when asked.
DT_TEST(ReadsEveryMode)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    const int N = 40;

    static const struct
    {
        int RxMode, Size, Stride, Offset;
    } Modes[] = {
        {DTAPI_RXMODE_ST188, 188, 188, 0},
        {DTAPI_RXMODE_ST204, 188, 204, 0},
        {DTAPI_RXMODE_STMP2, 188, 188, 0},
        {DTAPI_RXMODE_ST188 | DTAPI_RXMODE_TIMESTAMP32, 188, 192, 4},
        {DTAPI_RXMODE_ST188 | DTAPI_RXMODE_TIMESTAMP_TOD, 188, 196, 8},
        {DTAPI_RXMODE_STTRP, 188, 208, 0},
    };
    for (size_t m = 0; m < sizeof(Modes) / sizeof(Modes[0]); m++)
    {
        const int Stride = Modes[m].Stride;
        Restart(188, 8);
        DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, Modes[m].RxMode));
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

        // The transparent mode delivers the card's three pieces without sync as well.
        const int Skip = Modes[m].RxMode == DTAPI_RXMODE_STTRP ? 3 : 0;
        DT_ASSERT_OK(
            DtInpChannel_Read(Fix.Channel, Fix.Buffer, (N + Skip) * Stride, 1000));
        DT_ASSERT(ArePackets(Fix.Buffer + Skip * Stride, N, 0, Modes[m].Size, Stride,
                             Modes[m].Offset));
        if (Modes[m].RxMode == DTAPI_RXMODE_ST204)
        {
            static const uint8_t Zeros[16] = {0};
            DT_ASSERT(memcmp(Fix.Buffer + 188, Zeros, 16) == 0);
        }
        if (Modes[m].RxMode == DTAPI_RXMODE_STTRP)
        {
            DT_ASSERT_EQ(Fix.Buffer[204], 0x50); // A piece without sync
            DT_ASSERT_EQ(Fix.Buffer[3 * 208 + 204], 0x58);
            DT_ASSERT_EQ(Fix.Buffer[3 * 208 + 205], 188);
        }

        // Pieces without sync set the synchronisation error, as on the card.
        int Flags, Latched;
        DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
        DT_ASSERT((Latched & DTAPI_RX_SYNC_ERR) != 0);
        DT_ASSERT_OK(DtInpChannel_ClearFlags(Fix.Channel, DTAPI_RX_SYNC_ERR));
        DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
        DT_ASSERT_EQ(Latched & DTAPI_RX_SYNC_ERR, 0);
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_IDLE));
    }

    // Raw: every piece, as many bytes as it holds, and no error for the pieces.
    Restart(188, 8);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_STRAW));
    SimAsiState State;
    SimDtPcie_GetAsiState(PORT - 1, &State);
    DT_ASSERT_EQ(State.RxPacketMode, DT_ASIRX_PCKMODE_RAW);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 3 * 204 + N * 188, 1000));
    DT_ASSERT(ArePackets(Fix.Buffer + 3 * 204, N, 0, 188, 188, 0));
    int Flags, Latched;
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched & DTAPI_RX_SYNC_ERR, 0);

    // A receive mode is set only while idle.
    DT_ASSERT_EQ(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_ST188),
                 DTAPI_E_NOT_IDLE);
    FINISH(Fix);
}

// 204-byte packets: whole in DTAPI_RXMODE_ST204, their first 188 bytes in ST188.
DT_TEST(Reads204BytePackets)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;

    Restart(204, 8);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_ST204));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 20 * 204, 1000));
    DT_ASSERT(ArePackets(Fix.Buffer, 20, 0, 204, 204, 0));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_IDLE));

    Restart(204, 8);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_ST188));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 20 * 188, 1000));
    DT_ASSERT(ArePackets(Fix.Buffer, 20, 0, 188, 188, 0));
    FINISH(Fix);
}

// Reads of any size that is a multiple of 4 give the same stream, a packet's output
// cut where a read ends and continued in the next; without a time-out a read takes what
// it can.
DT_TEST(ReadsInPieces)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    static const int Sizes[] = {4, 100, 188, 212, 376, 1000, 4096, 12};
    const int Total = 300 * 192;

    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel,
                                        DTAPI_RXMODE_ST188 | DTAPI_RXMODE_TIMESTAMP32));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    int Done = 0;
    for (int i = 0; Done < Total; i++)
    {
        int Size = Sizes[(size_t)i % (sizeof(Sizes) / sizeof(Sizes[0]))];
        if (Size > Total - Done)
            Size = Total - Done;
        DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer + Done, Size,
                                       i % 2 == 0 ? 0 : 1000));
        Done += Size;
    }
    DT_ASSERT(ArePackets(Fix.Buffer, 300, 0, 188, 192, 4));
    FINISH(Fix);
}

// After a packet the card wrote wrongly the stream is found again, and packets follow on
// in order; a gap in the sequence alone loses nothing.
DT_TEST(RecoversFromFaults)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    static const int Faults[] = {SIM_ASI_FAULT_NIBBLE, SIM_ASI_FAULT_VALID,
                                 SIM_ASI_FAULT_SEQUENCE, SIM_ASI_FAULT_NOSYNC};

    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 10 * 188, 1000));
    uint32_t Last = NumberOf(Fix.Buffer + 9 * 188);
    for (size_t f = 0; f < sizeof(Faults) / sizeof(Faults[0]); f++)
    {
        int Flags, Latched;
        DT_ASSERT_OK(DtInpChannel_ClearFlags(Fix.Channel, DTAPI_RX_SYNC_ERR));
        SimDtPcie_AsiRxFault(PORT - 1, Faults[f]);
        DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 40 * 188, 1000));
        int Lost = 0;
        for (int i = 0; i < 40; i++)
        {
            const uint8_t* P = Fix.Buffer + i * 188;
            uint32_t Number = NumberOf(P);
            DT_ASSERT(ArePackets(P, 1, Number, 188, 188, 0));
            DT_ASSERT(Number > Last);
            Lost += (int)(Number - Last - 1);
            Last = Number;
        }
        if (Faults[f] == SIM_ASI_FAULT_SEQUENCE)
            DT_ASSERT_EQ(Lost, 0);
        else
            DT_ASSERT(Lost >= 1 && Lost <= 4);
        DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
        DT_ASSERT_EQ((Latched & DTAPI_RX_SYNC_ERR) != 0,
                     Faults[f] == SIM_ASI_FAULT_NOSYNC);
    }
    FINISH(Fix);
}

// The load never exceeds DTAPI's FIFO: what does not fit is dropped with
// DTAPI_RX_FIFO_OVF, and so is what the card could not write. Stopping clears the flag.
DT_TEST(OverflowsAsDtapi)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    int Load, Flags, Latched;

    Restart(188, 50000);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT(Load <= 8 * 1024 * 1024 && Load > 8 * 1024 * 1024 - 188);
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT((Latched & DTAPI_RX_FIFO_OVF) != 0);
    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 1000 * 188, 0));
    DT_ASSERT(ArePackets(Fix.Buffer, 1000, 0, 188, 188, 0));
    DT_ASSERT_OK(DtInpChannel_ClearFlags(Fix.Channel, DTAPI_RX_FIFO_OVF));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_IDLE));

    // More than the buffer holds overflows the card.
    Restart(188, 100000);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT((Flags & DTAPI_RX_FIFO_OVF) != 0);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_IDLE));
    DT_ASSERT_OK(DtInpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched & DTAPI_RX_FIFO_OVF, 0);
    DT_ASSERT_OK(DtInpChannel_GetFifoLoad(Fix.Channel, &Load));
    DT_ASSERT_EQ(Load, 0);
    FINISH(Fix);
}

// The status in DTAPI's values, the rate of 188-byte packets but in raw mode, the
// violations, and the polarity control.
DT_TEST(StatusAndRate)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    int PacketSize, NumInv, ClkDet, AsiLock, RateOk, AsiInv, Rate, Viol;

    SimAsiSource Source;
    SimAsi_DefaultSource(&Source);
    Source.PacketSize = 204;
    Source.Rate = 20400000;
    SimDtPcie_SetAsiSource(PORT - 1, &Source);
    DT_ASSERT_OK(DtInpChannel_GetStatus(Fix.Channel, &PacketSize, &NumInv, &ClkDet,
                                        &AsiLock, &RateOk, &AsiInv));
    DT_ASSERT_EQ(PacketSize, DTAPI_PCKSIZE_204);
    DT_ASSERT_EQ(NumInv, DTAPI_NOT_SUPPORTED);
    DT_ASSERT_EQ(ClkDet, DTAPI_CLKDET_OK);
    DT_ASSERT_EQ(AsiLock, DTAPI_ASI_INLOCK);
    DT_ASSERT_EQ(RateOk, DTAPI_INPRATE_OK);
    DT_ASSERT_EQ(AsiInv, DTAPI_ASIINV_NORMAL);
    DT_ASSERT_OK(DtInpChannel_GetTsRateBps(Fix.Channel, &Rate));
    DT_ASSERT_EQ(Rate, 18800000);
    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_STRAW));
    DT_ASSERT_OK(DtInpChannel_GetTsRateBps(Fix.Channel, &Rate));
    DT_ASSERT_EQ(Rate, 20400000);
    DT_ASSERT_OK(DtInpChannel_GetViolCount(Fix.Channel, &Viol));
    DT_ASSERT_EQ(Viol, 0);

    SimDtPcie_SetAsiSource(PORT - 1, NULL);
    DT_ASSERT_OK(DtInpChannel_GetStatus(Fix.Channel, &PacketSize, &NumInv, &ClkDet,
                                        &AsiLock, &RateOk, &AsiInv));
    DT_ASSERT_EQ(PacketSize, DTAPI_PCKSIZE_INV);
    DT_ASSERT_EQ(ClkDet, DTAPI_CLKDET_FAIL);
    DT_ASSERT_EQ(AsiLock, 0);
    DT_ASSERT_EQ(RateOk, DTAPI_INPRATE_LOW);
    DT_ASSERT_EQ(AsiInv, DTAPI_NOT_SUPPORTED);

    DT_ASSERT_EQ(DtInpChannel_PolarityControl(Fix.Channel, 1), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtInpChannel_PolarityControl(NULL, 1), DTAPI_E_INVALID_MODE);
    DT_ASSERT_OK(DtInpChannel_PolarityControl(Fix.Channel, DTAPI_POLARITY_INVERT));
    SimAsiState State;
    SimDtPcie_GetAsiState(PORT - 1, &State);
    DT_ASSERT_EQ(State.RxPolarityCtrl, DT_ASIRX_POLARITY_INVERT);
    FINISH(Fix);
}

// The I/O standard switches the channel between ASI and SDI and back, each side with its
// defaults; the ASI functions give DTAPI_E_NOT_SUPPORTED on SDI.
DT_TEST(SwitchesBetweenAsiAndSdi)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures))
        return;
    int Rate;

    DT_ASSERT_OK(DtInpChannel_SetRxMode(Fix.Channel, DTAPI_RXMODE_STRAW));
    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50));
    DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 188, 10),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_GetTsRateBps(Fix.Channel, &Rate), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtInpChannel_PolarityControl(Fix.Channel, DTAPI_POLARITY_AUTO),
                 DTAPI_E_NOT_SUPPORTED);
    int Size = 16;
    DT_ASSERT_EQ(DtInpChannel_ReadFrame(Fix.Channel, Fix.Buffer, &Size, 20),
                 DTAPI_E_BUF_TOO_SMALL);

    DT_ASSERT_OK(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_ASI, -1));
    SimAsiState State;
    SimDtPcie_GetAsiState(PORT - 1, &State);
    DT_ASSERT_EQ(State.RxPacketMode, DT_ASIRX_PCKMODE_AUTO); // ST188 again
    Restart(188, 8);
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 10 * 188, 1000));
    DT_ASSERT(ArePackets(Fix.Buffer, 10, 0, 188, 188, 0));
    DT_ASSERT_EQ(DtInpChannel_SetIoConfig(Fix.Channel, DTAPI_IOCONFIG_IOSTD,
                                          DTAPI_IOCONFIG_ASI, -1),
                 DTAPI_E_NOT_IDLE);
    FINISH(Fix);
}

typedef struct Reader
{
    DtInpChannel* Channel;
    uint8_t* Buffer;
    int TimeOut;
    DtapiResult Result;
} Reader;

static void ReadForever(void* Context)
{
    Reader* R = (Reader*)Context;
    R->Result = DtInpChannel_Read(R->Channel, R->Buffer, 188, R->TimeOut);
}

// A read waiting without data refuses a second one and ends with DTAPI_E_CANCELLED when
// the channel is detached, with a time-out and without.
DT_TEST(DetachEndsAWaitingRead)
{
    static const int TimeOuts[] = {-1, 0};
    for (size_t t = 0; t < sizeof(TimeOuts) / sizeof(TimeOuts[0]); t++)
    {
        Fixture Fix;
        if (!Start(&Fix, DtFailures))
            return;
        SimDtPcie_SetAsiSource(PORT - 1, NULL);
        DT_ASSERT_OK(DtInpChannel_SetRxControl(Fix.Channel, DTAPI_RXCTRL_RCV));

        Reader R = {Fix.Channel, Fix.Buffer, TimeOuts[t], DTAPI_OK};
        OsThread* Thread = OsThread_Start(ReadForever, &R);
        DT_ASSERT(Thread != NULL);
        OsTime_SleepMs(40);
        DT_ASSERT_EQ(DtInpChannel_Read(Fix.Channel, Fix.Buffer, 188, 10), DTAPI_E_IN_USE);
        DT_ASSERT_OK(DtInpChannel_Detach(Fix.Channel, 0));
        OsThread_Join(Thread);
        DT_ASSERT_EQ(R.Result, DTAPI_E_CANCELLED);
        FINISH(Fix);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("SimAsiRx", DT_RUN(AttachesWithDefaults), DT_RUN(ReadsEveryMode),
             DT_RUN(Reads204BytePackets), DT_RUN(ReadsInPieces),
             DT_RUN(RecoversFromFaults), DT_RUN(OverflowsAsDtapi), DT_RUN(StatusAndRate),
             DT_RUN(SwitchesBetweenAsiAndSdi), DT_RUN(DetachEndsAWaitingRead))

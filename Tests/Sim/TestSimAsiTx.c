// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimAsiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtOutpChannel on an ASI port of the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state without real time, so that the card takes whatever the channel has put in its
// buffer as soon as the channel looks, with port 2 an ASI output; it ends with no
// handle to the emulator and no allocation left open. What the port sends is decoded
// by the emulator's sink, or received by port 1 through its loop.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"      // Live allocations.
#include "DtPcieAbi.h"         // Driver values.
#include "DtTest.h"            // Test framework.
#include "OAL/OsThread.h"      // A writer on another thread, and the clock.
#include "OAL/Sim/SimAsi.h"    // The emulated ASI blocks, the sink and the loop.
#include "OAL/Sim/SimDtPcie.h" // The emulated card and its test controls.
#include "OAL/Sim/SimSdiTx.h"  // Real time, and the transmitter's state.
#include "cdtapi.h"            // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define PORT 2
#define PORT_INPUT 1
#define PORT_SLAVE 3
#define MAX_PACKETS 4000

typedef struct Fixture
{
    int Live;
    DtDevice* Device;
    DtOutpChannel* Channel;
    uint8_t* Data; // What is written
    uint8_t* Got;  // What the sink decoded, or the input read
} Fixture;

// One I/O configuration through the device.
static DtapiResult SetIoConfig(DtDevice* Device, int Port, int Group, int Value,
                               int SubValue, int64_t ParXtra0)
{
    DtIoConfig Config = {Port, Group, Value, SubValue, {ParXtra0, -1}};
    return DtDevice_SetIoConfig(Device, &Config, 1);
}

// Resets the emulator without real time, makes PORT an ASI output and attaches a channel
// to it. Returns false, having recorded a failure, when that is not possible.
static bool Start(Fixture* Fix, int* DtFailures, bool Attach)
{
    SimDtPcie_Reset();
    SimDtPcie_SetTxRealTime(false);
    Fix->Live = DtAlloc_NumLive();
    Fix->Device = DtDevice_Alloc();
    Fix->Channel = DtOutpChannel_Alloc();
    Fix->Data = (uint8_t*)malloc(MAX_PACKETS * 204);
    Fix->Got = (uint8_t*)malloc(MAX_PACKETS * 204);
    if (Fix->Device == NULL || Fix->Channel == NULL || Fix->Data == NULL ||
        Fix->Got == NULL ||
        DtDevice_AttachToSerial(Fix->Device, SIM_SERIAL) != DTAPI_OK ||
        SetIoConfig(Fix->Device, PORT, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1,
                    -1) != DTAPI_OK ||
        (Attach &&
         DtOutpChannel_AttachToPort(Fix->Channel, Fix->Device, PORT) != DTAPI_OK))
    {
        printf("    FAIL: no emulated ASI output; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        return false;
    }
    return true;
}

// Frees what Start made and checks that nothing is left open or allocated, once the
// emulator has let go of the bytes its sink kept.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtOutpChannel_Free((Fix).Channel);                                               \
        DtDevice_Free((Fix).Device);                                                     \
        free((Fix).Data);                                                                \
        free((Fix).Got);                                                                 \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// Count numbered packets of Size bytes from First, one after the other.
static void MakePackets(uint8_t* Out, uint32_t First, int Count, int Size)
{
    for (int i = 0; i < Count; i++)
        SimAsi_MakePacket(First + (uint32_t)i, Size, Out + (size_t)i * (size_t)Size);
}

// Takes what the sink decoded until Size bytes are there, or five seconds have passed.
static size_t TakeSent(uint8_t* Out, size_t Size)
{
    size_t Got = 0;
    const uint64_t Start = OsTime_MonotonicMs();
    while (Got < Size && OsTime_MonotonicMs() - Start < 5000)
    {
        Got += SimDtPcie_TakeAsiTxBytes(PORT - 1, Out + Got, Size - Got);
        if (Got < Size)
            OsTime_SleepMs(5);
    }
    return Got;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// An ASI output attaches with its defaults: the gate in standby, where a card sends
// K28.5, and the PHY running.
DT_TEST(AttachesInStandby)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, true))
        return;
    int Value = 0;

    SimAsiState State;
    SimDtPcie_GetAsiState(PORT - 1, &State);
    DT_ASSERT_EQ(State.TxgMode, DT_BLOCK_OPMODE_STANDBY);
    DT_ASSERT_EQ(State.TxgPolarity, DT_ASITXG_POL_NORMAL);
    SimTxState Tx;
    SimDtPcie_GetTxState(PORT - 1, &Tx);
    DT_ASSERT_EQ(Tx.PhyMode, DT_FUNC_OPMODE_RUN);
    DT_ASSERT(Tx.BufferRegistered);

    DT_ASSERT_OK(DtOutpChannel_GetTsRateBps(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 10000000);
    DT_ASSERT_OK(DtOutpChannel_GetFifoSize(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 8 * 1024 * 1024);
    DT_ASSERT_OK(DtOutpChannel_GetMaxFifoSize(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 8 * 1024 * 1024);
    DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Value));
    DT_ASSERT_EQ(Value, 0);

    DT_ASSERT_EQ(DtOutpChannel_SetTxPolarity(Fix.Channel, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtOutpChannel_SetTxPolarity(Fix.Channel, DTAPI_TXPOL_INVERTED));
    SimDtPcie_GetAsiState(PORT - 1, &State);
    DT_ASSERT_EQ(State.TxgPolarity, DT_ASITXG_POL_INVERT);

    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    static uint32_t Frame[4];
    DT_ASSERT_EQ(DtOutpChannel_WriteFrame(Fix.Channel, Frame, sizeof(Frame), 10),
                 DTAPI_E_NOT_SDI_MODE);
    FINISH(Fix);
}

// SetTxMode and SetTsRateBps check their arguments in any state.
DT_TEST(ModeAndRateChecks)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, true))
        return;

    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_188, 2),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_RAW, 1),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_192, 0),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_RAWASI, 0),
                 DTAPI_E_NOT_IMPLEMENTED);
    DT_ASSERT_EQ(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_SDI_FULL, 0),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtOutpChannel_SetTsRateBps(Fix.Channel, 0), DTAPI_E_INVALID_RATE);
    DT_ASSERT_EQ(DtOutpChannel_SetTsRateBps(Fix.Channel, 300000000),
                 DTAPI_E_INVALID_RATE);

    // A mode in hold. A rate that fits 188-byte packets is taken, and a packet size it
    // does not fit then too; holding refuses the two together.
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_204, 1));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_IDLE));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_188, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTsRateBps(Fix.Channel, 213000000));
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_204, 0));
    DT_ASSERT_EQ(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD),
                 DTAPI_E_INVALID_RATE);
    int Rate = 0;
    DT_ASSERT_OK(DtOutpChannel_GetTsRateBps(Fix.Channel, &Rate));
    DT_ASSERT_EQ(Rate, 213000000);
    FINISH(Fix);
}

// Every transmit mode sends what the mode makes of the stream: whole packets, 204-byte
// ones, 188-byte ones with 16 zeros, 204-byte ones less 16, or the bytes as they are.
DT_TEST(SendsEveryMode)
{
    static const struct
    {
        int TxMode, InSize, OutSize;
    } Modes[] = {
        {DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST, 188, 188},
        {DTAPI_TXMODE_188, 188, 188},
        {DTAPI_TXMODE_204 | DTAPI_TXMODE_BURST, 204, 204},
        {DTAPI_TXMODE_ADD16 | DTAPI_TXMODE_BURST, 188, 204},
        {DTAPI_TXMODE_MIN16 | DTAPI_TXMODE_BURST, 204, 188},
        {DTAPI_TXMODE_RAW, 188, 188},
    };
    const int N = 300;

    for (size_t m = 0; m < sizeof(Modes) / sizeof(Modes[0]); m++)
    {
        Fixture Fix;
        if (!Start(&Fix, DtFailures, true))
            return;
        DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, Modes[m].TxMode, 0));
        DT_ASSERT_OK(DtOutpChannel_SetTsRateBps(Fix.Channel, 50000000));
        MakePackets(Fix.Data, 0, N, Modes[m].InSize);

        // Written while holding, converted at once, and sent once sending.
        DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
        DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Fix.Data, N * Modes[m].InSize));
        int Load = 0;
        DT_ASSERT_OK(DtOutpChannel_GetFifoLoad(Fix.Channel, &Load));
        DT_ASSERT_EQ(Load, N * Modes[m].InSize);
        DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));

        const size_t Want = (size_t)N * (size_t)Modes[m].OutSize;
        DT_ASSERT_EQ(TakeSent(Fix.Got, Want), Want);
        for (int i = 0; i < N; i++)
        {
            uint8_t Packet[204];
            SimAsi_MakePacket((uint32_t)i, Modes[m].InSize, Packet);
            const size_t Common =
                (size_t)(Modes[m].InSize < Modes[m].OutSize ? Modes[m].InSize
                                                            : Modes[m].OutSize);
            const uint8_t* Sent = Fix.Got + (size_t)i * (size_t)Modes[m].OutSize;
            DT_ASSERT(memcmp(Sent, Packet, Common) == 0);
            if (Modes[m].OutSize > Modes[m].InSize)
            {
                static const uint8_t Zeros[16] = {0};
                DT_ASSERT(memcmp(Sent + 188, Zeros, 16) == 0);
            }
        }
        SimAsiTxStats Stats;
        SimDtPcie_GetAsiTxStats(PORT - 1, &Stats);
        DT_ASSERT_EQ(Stats.CodeErrors, 0);
        DT_ASSERT_EQ(Stats.DisparityErrors, 0);
        DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, DTAPI_WAIT_UNTIL_SENT));
        FINISH(Fix);
    }
}

// The rate the symbols carry is the rate set, as the sink measures it.
DT_TEST(SendsAtTheRate)
{
    static const int Rates[] = {2000000, 20000000, 150000000};

    for (size_t r = 0; r < sizeof(Rates) / sizeof(Rates[0]); r++)
    {
        Fixture Fix;
        if (!Start(&Fix, DtFailures, true))
            return;
        // A fifth of a second of the stream, at most MAX_PACKETS packets, which the sink
        // decodes symbol by symbol.
        const int N =
            Rates[r] / 5 / 1504 < MAX_PACKETS ? Rates[r] / 5 / 1504 : MAX_PACKETS;
        DT_ASSERT_OK(DtOutpChannel_SetTsRateBps(Fix.Channel, Rates[r]));
        MakePackets(Fix.Data, 0, N, 188);
        DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
        DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Fix.Data, N * 188));
        DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
        DT_ASSERT_EQ(TakeSent(Fix.Got, (size_t)N * 188), (size_t)N * 188);

        SimAsiTxStats Stats;
        SimDtPcie_GetAsiTxStats(PORT - 1, &Stats);
        const double Rate = (double)Stats.DataBytes * 8 * 27e6 / (double)Stats.Symbols;
        DT_ASSERT(Rate > Rates[r] * 0.99 && Rate < Rates[r] * 1.01);
        FINISH(Fix);
    }
}

// Through the loop to port 1, an input channel receives what the output sent: 188-byte
// packets, the usual, and 204-byte ones.
static void LoopsToAnInput(int Size, int* DtFailures)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, true))
        return;
    DtInpChannel* Input = DtInpChannel_Alloc();
    DT_ASSERT(Input != NULL);
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT_INPUT, DTAPI_IOCONFIG_IODIR,
                             DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT, -1));
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT_INPUT, DTAPI_IOCONFIG_IOSTD,
                             DTAPI_IOCONFIG_ASI, -1, -1));
    DT_ASSERT_OK(DtInpChannel_AttachToPort(Input, Fix.Device, PORT_INPUT));
    SimDtPcie_SetAsiLoopback(PORT - 1, PORT_INPUT - 1);

    const int N = 500;
    const bool Is188 = Size == 188;
    MakePackets(Fix.Data, 0, N, Size);
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel,
                                         Is188 ? DTAPI_TXMODE_188 : DTAPI_TXMODE_204, 0));
    DT_ASSERT_OK(
        DtInpChannel_SetRxMode(Input, Is188 ? DTAPI_RXMODE_ST188 : DTAPI_RXMODE_ST204));
    DT_ASSERT_OK(DtInpChannel_SetRxControl(Input, DTAPI_RXCTRL_RCV));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Fix.Data, N * Size));
    DT_ASSERT_OK(DtInpChannel_Read(Input, Fix.Got, N * Size, 2000));
    DT_ASSERT(memcmp(Fix.Got, Fix.Data, (size_t)N * (size_t)Size) == 0);

    int PacketSize, NumInv, ClkDet, AsiLock, RateOk, AsiInv;
    DT_ASSERT_OK(DtInpChannel_GetStatus(Input, &PacketSize, &NumInv, &ClkDet, &AsiLock,
                                        &RateOk, &AsiInv));
    DT_ASSERT_EQ(PacketSize, Is188 ? DTAPI_PCKSIZE_188 : DTAPI_PCKSIZE_204);
    DT_ASSERT_EQ(AsiLock, DTAPI_ASI_INLOCK);

    SimDtPcie_SetAsiLoopback(-1, -1);
    DtInpChannel_Free(Input);
    FINISH(Fix);
}

DT_TEST(LoopsToAnInput188)
{
    LoopsToAnInput(188, DtFailures);
}

DT_TEST(LoopsToAnInput204)
{
    LoopsToAnInput(204, DtFailures);
}

// With stuffing a channel with nothing to send sends null packets, and says so with
// DTAPI_TX_FIFO_UFL; ClearFlags clears it.
DT_TEST(StuffsNullPackets)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, true))
        return;
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_188, 1));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT_EQ(TakeSent(Fix.Got, 10 * 188), (size_t)10 * 188);
    for (int i = 0; i < 10; i++)
    {
        const uint8_t* P = Fix.Got + i * 188;
        DT_ASSERT(P[0] == 0x47 && P[1] == 0x1F && P[2] == 0xFF);
    }
    int Flags, Latched;
    DT_ASSERT_OK(DtOutpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT((Latched & DTAPI_TX_FIFO_UFL) != 0);
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_IDLE));
    DT_ASSERT_OK(DtOutpChannel_ClearFlags(Fix.Channel, DTAPI_TX_FIFO_UFL));
    DT_ASSERT_OK(DtOutpChannel_GetFlags(Fix.Channel, &Flags, &Latched));
    DT_ASSERT_EQ(Latched & DTAPI_TX_FIFO_UFL, 0);
    FINISH(Fix);
}

typedef struct Writer
{
    DtOutpChannel* Channel;
    const uint8_t* Data;
    int Size;
    DtapiResult Result;
} Writer;

static void WriteAll(void* Context)
{
    Writer* W = (Writer*)Context;
    W->Result = DtOutpChannel_Write(W->Channel, W->Data, W->Size);
}

// A write that waits for room while holding refuses a second one, and a detach ends it
// with DTAPI_E_CANCELLED.
DT_TEST(DetachEndsAWaitingWrite)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, true))
        return;
    uint8_t* Big = (uint8_t*)calloc(9, 1024 * 1024);
    DT_ASSERT(Big != NULL);
    DT_ASSERT_OK(DtOutpChannel_SetTxMode(Fix.Channel, DTAPI_TXMODE_RAW, 0));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));

    Writer W = {Fix.Channel, Big, 9 * 1024 * 1024, DTAPI_OK};
    OsThread* Thread = OsThread_Start(WriteAll, &W);
    DT_ASSERT(Thread != NULL);
    OsTime_SleepMs(60);
    DT_ASSERT_EQ(DtOutpChannel_Write(Fix.Channel, Big, 188 * 4), DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, 0));
    OsThread_Join(Thread);
    DT_ASSERT_EQ(W.Result, DTAPI_E_CANCELLED);
    free(Big);
    FINISH(Fix);
}

// A detach that waits until everything is sent waits for the burst FIFO as well, and
// not for the buffer alone: the last packet reaches the sink.
DT_TEST(DetachWaitsForTheLastPacket)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, true))
        return;
    SimDtPcie_SetTxRealTime(true);
    const int N = 500;
    MakePackets(Fix.Data, 0, N, 188);
    DT_ASSERT_OK(DtOutpChannel_SetTsRateBps(Fix.Channel, 20000000));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_HOLD));
    DT_ASSERT_OK(DtOutpChannel_Write(Fix.Channel, Fix.Data, N * 188));
    DT_ASSERT_OK(DtOutpChannel_SetTxControl(Fix.Channel, DTAPI_TXCTRL_SEND));
    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, DTAPI_WAIT_UNTIL_SENT));
    DT_ASSERT_EQ(TakeSent(Fix.Got, (size_t)N * 188), (size_t)N * 188);
    DT_ASSERT(memcmp(Fix.Got, Fix.Data, (size_t)N * 188) == 0);
    FINISH(Fix);
}

// A double-buffered output naming the port as its master is taken with it, set to ASI,
// its PHY run and stopped with the master's; one another user holds refuses the attach.
DT_TEST(DrivesItsSlave)
{
    Fixture Fix;
    if (!Start(&Fix, DtFailures, false))
        return;
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT_SLAVE, DTAPI_IOCONFIG_IODIR,
                             DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF, PORT));

    // Held by another user first.
    DtOutpChannel* Other = DtOutpChannel_Alloc();
    DT_ASSERT(Other != NULL);
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT_SLAVE, DTAPI_IOCONFIG_IOSTD,
                             DTAPI_IOCONFIG_ASI, -1, -1));
    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Other, Fix.Device, PORT_SLAVE));
    DT_ASSERT_EQ(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT),
                 DTAPI_E_IN_USE);
    DtOutpChannel_Free(Other);
    DT_ASSERT_OK(SetIoConfig(Fix.Device, PORT_SLAVE, DTAPI_IOCONFIG_IOSTD,
                             DTAPI_IOCONFIG_HDSDI, DTAPI_IOCONFIG_1080I50, -1));

    DT_ASSERT_OK(DtOutpChannel_AttachToPort(Fix.Channel, Fix.Device, PORT));
    DtIoConfig Std = {PORT_SLAVE, DTAPI_IOCONFIG_IOSTD, -1, -1, {-1, -1}};
    DT_ASSERT_OK(DtDevice_GetIoConfig(Fix.Device, &Std, 1));
    DT_ASSERT_EQ(Std.Value, DTAPI_IOCONFIG_ASI);
    SimTxState Slave;
    SimDtPcie_GetTxState(PORT_SLAVE - 1, &Slave);
    DT_ASSERT_EQ(Slave.PhyMode, DT_FUNC_OPMODE_RUN);

    DT_ASSERT_OK(DtOutpChannel_Detach(Fix.Channel, 0));
    SimDtPcie_GetTxState(PORT_SLAVE - 1, &Slave);
    DT_ASSERT_EQ(Slave.PhyMode, DT_FUNC_OPMODE_IDLE);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("SimAsiTx", DT_RUN(AttachesInStandby), DT_RUN(ModeAndRateChecks),
             DT_RUN(SendsEveryMode), DT_RUN(SendsAtTheRate), DT_RUN(LoopsToAnInput188),
             DT_RUN(LoopsToAnInput204), DT_RUN(StuffsNullPackets),
             DT_RUN(DetachEndsAWaitingWrite), DT_RUN(DetachWaitsForTheLastPacket),
             DT_RUN(DrivesItsSlave))

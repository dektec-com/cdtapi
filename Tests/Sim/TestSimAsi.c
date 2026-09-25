// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSimAsi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The ASI commands against the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state with port 1 an ASI input and port 2 an ASI output, and ends with no handle to it
// and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtFunc.h"                 // Finding the objects.
#include "DtPcieAbi.h"              // Types, commands and driver statuses.
#include "DtPcieCmd.h"              // Commands under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/OsDmaBuffer.h"        // Buffers to register.
#include "OAL/OsThread.h"           // The clock.
#include "OAL/Sim/SimAsi.h"         // The emulated ASI blocks and their controls.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "OAL/Sim/SimNw.h"          // The card's time of day.
#include "OAL/Sim/SimSdiTx.h"       // Real time.
#include "Ts/DtAsiEnc.h"            // Symbols to send.
#include "cdtapi.h"                 // Results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The ports the tests use, by index: port 1 receives, port 2 transmits.
#define RX 0
#define TX 1

typedef struct Fixture
{
    OsDrv* Drv;
    DtFuncInstance Rx;    // AF_ASISDIRX of RX
    DtFuncInstance RxDma; // AF_DMA of RX
    DtFuncInstance Tx;    // AF_ASISDITX of TX
    DtFuncInstance TxDma; // AF_DMA of TX
    DtDrvObject AsiRx, RxCdmac, RxBurst, AsiTxG, TxPhy, TxCdmac, TxBurst;
    int Live;
} Fixture;

// Sets one I/O configuration of the port at Index.
static DtapiResult Configure(OsDrv* Drv, int Index, int Group, int Value, int SubValue)
{
    DtIoConfig Config = {Index + 1, Group, Value, SubValue, {-1, -1}};
    return DtPcieCmd_SetIoConfig(Drv, &Config);
}

// Opens the emulated device in its power-on state, makes RX an ASI input and TX an ASI
// output, and finds their objects. Returns false, having recorded a failure, when that is
// not possible.
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    Fix->Live = DtAlloc_NumLive();
    Fix->Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    DtVec_Init(&Fix->Rx.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Fix->RxDma.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Fix->Tx.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Fix->TxDma.Objects, sizeof(DtFuncObject));
    if (Fix->Drv == NULL || !OsDrv_IsEmulated(Fix->Drv) ||
        Configure(Fix->Drv, RX, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                  DTAPI_IOCONFIG_INPUT) != DTAPI_OK ||
        Configure(Fix->Drv, RX, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1) !=
            DTAPI_OK ||
        Configure(Fix->Drv, TX, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                  DTAPI_IOCONFIG_OUTPUT) != DTAPI_OK ||
        Configure(Fix->Drv, TX, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1) !=
            DTAPI_OK ||
        DtFunc_Find(Fix->Drv, RX, "AF_ASISDIRX", "", &Fix->Rx) != DTAPI_OK ||
        DtFunc_Find(Fix->Drv, RX, "AF_DMA", "", &Fix->RxDma) != DTAPI_OK ||
        DtFunc_Find(Fix->Drv, TX, "AF_ASISDITX", "", &Fix->Tx) != DTAPI_OK ||
        DtFunc_Find(Fix->Drv, TX, "AF_DMA", "", &Fix->TxDma) != DTAPI_OK)
    {
        printf("    FAIL: no emulated ASI ports; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        DtFunc_Release(&Fix->Rx);
        DtFunc_Release(&Fix->RxDma);
        DtFunc_Release(&Fix->Tx);
        DtFunc_Release(&Fix->TxDma);
        OsDrv_Close(Fix->Drv);
        return false;
    }
    Fix->AsiRx = DtFunc_FindObject(&Fix->Rx, true, DT_FUNC_TYPE_ASIRX, "")->Object;
    Fix->RxCdmac = DtFunc_FindObject(&Fix->RxDma, false, DT_BLOCK_TYPE_CDMAC, "")->Object;
    Fix->RxBurst =
        DtFunc_FindObject(&Fix->RxDma, false, DT_BLOCK_TYPE_BURSTFIFO, "")->Object;
    Fix->AsiTxG = DtFunc_FindObject(&Fix->Tx, false, DT_BLOCK_TYPE_ASITXG, "")->Object;
    Fix->TxPhy = DtFunc_FindObject(&Fix->Tx, true, DT_FUNC_TYPE_SDITXPHY, "")->Object;
    Fix->TxCdmac = DtFunc_FindObject(&Fix->TxDma, false, DT_BLOCK_TYPE_CDMAC, "")->Object;
    Fix->TxBurst =
        DtFunc_FindObject(&Fix->TxDma, false, DT_BLOCK_TYPE_BURSTFIFO, "")->Object;
    return true;
}

// Takes exclusive access to every object the tests use.
static bool Acquire(Fixture* Fix)
{
    return DtFunc_ExclAccess(Fix->Drv, &Fix->Rx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK &&
           DtFunc_ExclAccess(Fix->Drv, &Fix->RxDma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK &&
           DtFunc_ExclAccess(Fix->Drv, &Fix->Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK &&
           DtFunc_ExclAccess(Fix->Drv, &Fix->TxDma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK;
}

// Frees the objects, closes the device and checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtFunc_Release(&(Fix).Rx);                                                       \
        DtFunc_Release(&(Fix).RxDma);                                                    \
        DtFunc_Release(&(Fix).Tx);                                                       \
        DtFunc_Release(&(Fix).TxDma);                                                    \
        OsDrv_Close((Fix).Drv);                                                          \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// Whether the last command was Cmd of FunctionCode for Object, with an input of
// Size bytes; when Value is not NULL, the input is a header and one Int, which must
// equal *Value.
static bool LastWas(int FunctionCode, DtDrvObject Object, int Cmd, size_t Size,
                    const int* Value)
{
    uint8_t In[SIM_MAX_RECORDED_INPUT];
    int Code = -1;
    size_t Got = SimDtPcie_LastInput(&Code, In, sizeof(In));

    DtIoctlInputDataHdr Hdr;
    memcpy(&Hdr, In, sizeof(Hdr));
    if (Value != NULL)
    {
        Int Sent;
        memcpy(&Sent, In + sizeof(Hdr), sizeof(Sent));
        if (Sent != *Value)
            return false;
    }
    return Got == Size && Code == FunctionCode && Hdr.m_Uuid == Object.Uuid &&
           Hdr.m_PortIndex == Object.PortIndex && Hdr.m_Cmd == Cmd &&
           Hdr.m_CmdEx == DT_IOCTL_CMD_NOP;
}

#define HDR sizeof(DtIoctlInputDataHdr)
#define ONE_INT (HDR + sizeof(Int))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every command goes to its object with the fields it carries, and what is set
// reads back.
DT_TEST(RequestsCarryTheirFields)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    const DtDrvObject Rx = Fix.AsiRx, TxG = Fix.AsiTxG;
    int Value = -1;

    static const struct
    {
        int Set, Get, Value;
    } RxSettings[] = {
        {DT_ASIRX_CMD_SET_PACKET_MODE, DT_ASIRX_CMD_GET_PACKET_MODE,
         DT_ASIRX_PCKMODE_RAW},
        {DT_ASIRX_CMD_SET_POLARITY_CTRL, DT_ASIRX_CMD_GET_POLARITY_CTRL,
         DT_ASIRX_POLARITY_INVERT},
        {DT_ASIRX_CMD_SET_SYNC_MODE, DT_ASIRX_CMD_GET_SYNC_MODE, DT_ASIRX_SYNCMODE_204},
    };
    for (size_t i = 0; i < sizeof(RxSettings) / sizeof(RxSettings[0]); i++)
    {
        const int Set = RxSettings[i].Set, Want = RxSettings[i].Value;
        DtapiResult Result = DTAPI_E;
        if (Set == DT_ASIRX_CMD_SET_PACKET_MODE)
            Result = DtPcieCmd_AsiRxSetPacketMode(Fix.Drv, Rx, Want);
        else if (Set == DT_ASIRX_CMD_SET_POLARITY_CTRL)
            Result = DtPcieCmd_AsiRxSetPolarityCtrl(Fix.Drv, Rx, Want);
        else
            Result = DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, Want);
        DT_ASSERT_OK(Result);
        DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, Set, ONE_INT, &Want));

        if (Set == DT_ASIRX_CMD_SET_PACKET_MODE)
            Result = DtPcieCmd_AsiRxGetPacketMode(Fix.Drv, Rx, &Value);
        else if (Set == DT_ASIRX_CMD_SET_POLARITY_CTRL)
            Result = DtPcieCmd_AsiRxGetPolarityCtrl(Fix.Drv, Rx, &Value);
        else
            Result = DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, &Value);
        DT_ASSERT_OK(Result);
        DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RxSettings[i].Get, HDR, NULL));
        DT_ASSERT_EQ(Value, Want);
    }

    DT_ASSERT_OK(DtPcieCmd_AsiRxGetOpStatus(Fix.Drv, Rx, &Value));
    DT_ASSERT_EQ(Value, DT_FUNC_OPSTATUS_IDLE);
    const int Run = DT_FUNC_OPMODE_RUN;
    DT_ASSERT_OK(DtPcieCmd_AsiRxSetOpMode(Fix.Drv, Rx, Run));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, DT_ASIRX_CMD_SET_OPERATIONAL_MODE,
                      ONE_INT, &Run));
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetOpStatus(Fix.Drv, Rx, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, DT_ASIRX_CMD_GET_OPERATIONAL_STATUS,
                      HDR, NULL));
    DT_ASSERT_EQ(Value, DT_FUNC_OPSTATUS_IDLE); // Running, but nothing to receive
    SimAsiSignal Signal = {true, true, DT_ASIRX_PCKSIZE_188, DT_ASIRX_POLARITY_NORMAL,
                           0,    0};
    SimDtPcie_SetAsiSignal(RX, &Signal);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetOpStatus(Fix.Drv, Rx, &Value));
    DT_ASSERT_EQ(Value, DT_FUNC_OPSTATUS_RUN);

    const int Standby = DT_BLOCK_OPMODE_STANDBY;
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetOpMode(Fix.Drv, TxG, Standby));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, DT_ASITXG_CMD_SET_OPERATIONAL_MODE,
                      ONE_INT, &Standby));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetOpMode(Fix.Drv, TxG, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, DT_ASITXG_CMD_GET_OPERATIONAL_MODE,
                      HDR, NULL));
    DT_ASSERT_EQ(Value, Standby);

    const int Invert = DT_ASITXG_POL_INVERT;
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetPolarity(Fix.Drv, TxG, Invert));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, DT_ASITXG_CMD_SET_ASI_POLARITY,
                      ONE_INT, &Invert));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, &Value));
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, DT_ASITXG_CMD_GET_ASI_POLARITY, HDR, NULL));
    DT_ASSERT_EQ(Value, Invert);

    DT_ASSERT_OK(DtPcieCmd_AsiTxGClearInputState(Fix.Drv, TxG));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, DT_ASITXG_CMD_CLEAR_INPUT_STATE, HDR,
                      NULL));

    SimAsiState State;
    SimDtPcie_GetAsiState(TX, &State);
    DT_ASSERT_EQ(State.TxgInputClears, 1);
    SimDtPcie_GetAsiState(RX, &State);
    DT_ASSERT_EQ(State.RxMode, DT_FUNC_OPMODE_RUN);
    DT_ASSERT_EQ(State.RxPacketMode, DT_ASIRX_PCKMODE_RAW);

    // The DTA-2178 has no serialiser; the request is still made.
    DT_ASSERT_EQ(DtPcieCmd_AsiTxSerSetOpMode(Fix.Drv, TxG, Standby),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXSER_CMD, TxG,
                      DT_ASITXSER_CMD_SET_OPERATIONAL_MODE, ONE_INT, &Standby));
    DT_ASSERT_EQ(DtPcieCmd_AsiTxSerGetOpMode(Fix.Drv, TxG, &Value),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXSER_CMD, TxG,
                      DT_ASITXSER_CMD_GET_OPERATIONAL_MODE, HDR, NULL));
    DT_ASSERT_EQ(Value, 0);

    FINISH(Fix);
}

// A value outside the range a command takes is refused here, and nothing reaches the
// driver.
DT_TEST(InvalidValuesSendNothing)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    const DtDrvObject Rx = Fix.AsiRx, TxG = Fix.AsiTxG;

    int Value;
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, &Value));
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetOpMode(Fix.Drv, Rx, DT_FUNC_OPMODE_STANDBY),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetPacketMode(Fix.Drv, Rx, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetPolarityCtrl(Fix.Drv, Rx, DT_ASIRX_POLARITY_UNKNOWN),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGSetOpMode(Fix.Drv, TxG, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGSetPolarity(Fix.Drv, TxG, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxSerSetOpMode(Fix.Drv, TxG, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, DT_ASIRX_CMD_GET_SYNC_MODE, HDR, NULL));

    DtAsiRxStatus Status;
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetViolCount(Fix.Drv, Rx, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetStatus(NULL, Rx, &Status), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGClearInputState(NULL, TxG), DTAPI_E_INVALID_ARG);

    FINISH(Fix);
}

// Settings need exclusive access and readings do not; an object of a port that is not ASI
// in its direction refuses both, and forgets what was set.
DT_TEST(ObjectsCheckAccessAndPort)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    const DtDrvObject Rx = Fix.AsiRx, TxG = Fix.AsiTxG;
    int Value;

    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, DT_ASIRX_SYNCMODE_188),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGClearInputState(Fix.Drv, TxG), DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, &Value));
    DT_ASSERT_EQ(Value, DT_ASIRX_SYNCMODE_AUTO);
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, &Value));
    DT_ASSERT_EQ(Value, DT_ASITXG_POL_NORMAL);

    DT_ASSERT(Acquire(&Fix));
    DT_ASSERT_OK(DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, DT_ASIRX_SYNCMODE_188));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetPolarity(Fix.Drv, TxG, DT_ASITXG_POL_INVERT));

    // SDI instead of ASI.
    DT_ASSERT_OK(Configure(Fix.Drv, RX, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_HDSDI,
                           DTAPI_IOCONFIG_1080I50));
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, &Value), DTAPI_E_INVALID_MODE);
    DT_ASSERT_OK(Configure(Fix.Drv, RX, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1));
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, &Value));
    DT_ASSERT_EQ(Value, DT_ASIRX_SYNCMODE_AUTO);

    // The other direction.
    DT_ASSERT_OK(Configure(Fix.Drv, TX, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                           DTAPI_IOCONFIG_INPUT));
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, &Value), DTAPI_E_INVALID_MODE);
    DT_ASSERT_OK(Configure(Fix.Drv, TX, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                           DTAPI_IOCONFIG_OUTPUT));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, &Value));
    DT_ASSERT_EQ(Value, DT_ASITXG_POL_NORMAL);

    // A refusal passes through with its code.
    SimDtPcie_FailAsiCmd(DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_TS_BITRATE,
                         DT_STATUS_BUSY);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetTsBitrate(Fix.Drv, Rx, &Value), DTAPI_E_BUSY);
    DT_ASSERT_EQ(Value, 0);
    SimDtPcie_FailAsiCmd(DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_TS_BITRATE, 0);

    FINISH(Fix);
}

// What ASIRX reports is the signal's; without one, no carrier and no lock.
DT_TEST(StatusComesFromTheSignal)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    const DtDrvObject Rx = Fix.AsiRx;
    DtAsiRxStatus Status;
    int Bitrate, Viol;

    DT_ASSERT_OK(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, &Status));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, DT_ASIRX_CMD_GET_STATUS, HDR, NULL));
    DT_ASSERT(!Status.CarrierDetect);
    DT_ASSERT(!Status.AsiLock);
    DT_ASSERT_EQ(Status.PacketSize, DT_ASIRX_PCKSIZE_UNKNOWN);
    DT_ASSERT_EQ(Status.Polarity, DT_ASIRX_POLARITY_UNKNOWN);

    SimAsiSignal Signal = {true,     true, DT_ASIRX_PCKSIZE_204, DT_ASIRX_POLARITY_INVERT,
                           10851064, 3};
    SimDtPcie_SetAsiSignal(RX, &Signal);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, &Status));
    DT_ASSERT(Status.CarrierDetect);
    DT_ASSERT(Status.AsiLock);
    DT_ASSERT_EQ(Status.PacketSize, DT_ASIRX_PCKSIZE_204);
    DT_ASSERT_EQ(Status.Polarity, DT_ASIRX_POLARITY_INVERT);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetTsBitrate(Fix.Drv, Rx, &Bitrate));
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, DT_ASIRX_CMD_GET_TS_BITRATE, HDR, NULL));
    DT_ASSERT_EQ(Bitrate, 10851064);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetViolCount(Fix.Drv, Rx, &Viol));
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, DT_ASIRX_CMD_GET_VIOL_COUNT, HDR, NULL));
    DT_ASSERT_EQ(Viol, 3);

    // A packet size that is not one of the known ones is the driver's fault.
    Signal.PacketSize = 7;
    SimDtPcie_SetAsiSignal(RX, &Signal);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, &Status), DTAPI_E_DEV_DRIVER);
    DT_ASSERT(!Status.AsiLock);
    SimDtPcie_SetAsiSignal(RX, NULL);

    FINISH(Fix);
}

// The receive buffer's offsets: the card's write offset and the process's read offset,
// for a buffer registered for receiving.
DT_TEST(ReceiveOffsets)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    const DtDrvObject Cdmac = Fix.RxCdmac;
    uint32_t Offset = 5;

    // Without a receive buffer, as a DTA-2178 answers.
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Cdmac, &Offset));
    DT_ASSERT_EQ(Offset, 0u);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetRxReadOffset(Fix.Drv, Cdmac, 0));
    Offset = 5;

    OsDmaBuffer Buf;
    DT_ASSERT(OsDmaBuffer_Alloc(2 * 1024 * 1024, &Buf) == 0);
    DT_ASSERT_OK(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, Cdmac, DT_CDMAC_DIR_RX, &Buf));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Cdmac, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, Cdmac, DT_CDMAC_CMD_GET_RX_WRITE_OFFSET,
                      HDR, NULL));
    DT_ASSERT_EQ(Offset, 0u);

    const int At = 216 * 10;
    DT_ASSERT_OK(DtPcieCmd_CdmacSetRxReadOffset(Fix.Drv, Cdmac, (uint32_t)At));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, Cdmac, DT_CDMAC_CMD_SET_RX_READ_OFFSET,
                      ONE_INT, &At));
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetRxReadOffset(Fix.Drv, Cdmac, 2 * 1024 * 1024),
                 DTAPI_E_INVALID_ARG);

    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, Cdmac));
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Data path +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define RX_BUFFER (2 * 1024 * 1024)
#define TX_BUFFER (1024 * 1024)

// Makes RX receive into Buf: a buffer registered for receiving, CDMAC, the burst FIFO
// and ASIRX running.
static bool StartRx(Fixture* Fix, OsDmaBuffer* Buf)
{
    return OsDmaBuffer_Alloc(RX_BUFFER, Buf) == 0 &&
           DtPcieCmd_CdmacAllocateBuffer(Fix->Drv, Fix->RxCdmac, DT_CDMAC_DIR_RX, Buf) ==
               DTAPI_OK &&
           DtPcieCmd_CdmacSetOpMode(Fix->Drv, Fix->RxCdmac, DT_BLOCK_OPMODE_RUN) ==
               DTAPI_OK &&
           DtPcieCmd_BurstFifoSetOpMode(Fix->Drv, Fix->RxBurst, DT_BLOCK_OPMODE_RUN) ==
               DTAPI_OK &&
           DtPcieCmd_AsiRxSetOpMode(Fix->Drv, Fix->AsiRx, DT_FUNC_OPMODE_RUN) == DTAPI_OK;
}

// Makes TX send from Buf: a buffer registered for sending, CDMAC, the burst FIFO,
// SDITXPHY and ASITXG running.
static bool StartTx(Fixture* Fix, OsDmaBuffer* Buf)
{
    return OsDmaBuffer_Alloc(TX_BUFFER, Buf) == 0 &&
           DtPcieCmd_CdmacAllocateBuffer(Fix->Drv, Fix->TxCdmac, DT_CDMAC_DIR_TX, Buf) ==
               DTAPI_OK &&
           DtPcieCmd_CdmacSetTxWriteOffset(Fix->Drv, Fix->TxCdmac, 0) == DTAPI_OK &&
           DtPcieCmd_CdmacSetOpMode(Fix->Drv, Fix->TxCdmac, DT_BLOCK_OPMODE_RUN) ==
               DTAPI_OK &&
           DtPcieCmd_BurstFifoSetOpMode(Fix->Drv, Fix->TxBurst, DT_BLOCK_OPMODE_RUN) ==
               DTAPI_OK &&
           DtPcieCmd_SdiTxPhySetOpMode(Fix->Drv, Fix->TxPhy, DT_FUNC_OPMODE_RUN) ==
               DTAPI_OK &&
           DtPcieCmd_AsiTxGSetOpMode(Fix->Drv, Fix->AsiTxG, DT_BLOCK_OPMODE_RUN) ==
               DTAPI_OK;
}

// Stops the DMA of Cdmac and frees its buffer.
static void Stop(Fixture* Fix, DtDrvObject Cdmac, OsDmaBuffer* Buf)
{
    DtPcieCmd_CdmacSetOpMode(Fix->Drv, Cdmac, DT_BLOCK_OPMODE_IDLE);
    DtPcieCmd_CdmacFreeBuffer(Fix->Drv, Cdmac);
    OsDmaBuffer_Free(Buf);
}

// Encodes Count numbered packets of Size bytes from First as the card sends them, padded
// to whole data words, into Buf at *Offset, and hands them to TX.
static bool Send(Fixture* Fix, OsDmaBuffer* Buf, DtAsiEnc* Enc, uint32_t First, int Count,
                 int Size, uint32_t* Offset)
{
    uint8_t Packet[204];
    uint16_t Syms[16384];
    size_t n = 0;

    for (int i = 0; i < Count; i++)
    {
        SimAsi_MakePacket(First + (uint32_t)i, Size, Packet);
        size_t Taken, Written;
        DtAsiEnc_Encode(Enc, Packet, (size_t)Size, Syms + n,
                        sizeof(Syms) / sizeof(Syms[0]) - n, &Taken, &Written);
        if (Taken != (size_t)Size)
            return false;
        n += Written;
    }
    const size_t Word = 16; // Symbols in a data word of 256 bits
    size_t Padding = (Word - n % Word) % Word;
    DtAsiEnc_Pad(Enc, Syms + n, Padding);
    n += Padding;
    for (size_t i = 0; i < n; i++)
    {
        Buf->Data[(*Offset + 2 * i) % TX_BUFFER] = (uint8_t)Syms[i];
        Buf->Data[(*Offset + 2 * i + 1) % TX_BUFFER] = (uint8_t)(Syms[i] >> 8);
    }
    *Offset = (uint32_t)((*Offset + 2 * n) % TX_BUFFER);
    return DtPcieCmd_CdmacSetTxWriteOffset(Fix->Drv, Fix->TxCdmac, *Offset) == DTAPI_OK;
}

// Whether the transparent packet at P is a synchronised one with packet Number of Size
// bytes and sequence number Sequence.
static bool IsPacket(const uint8_t* P, uint32_t Number, int Size, int Sequence)
{
    uint8_t Want[204];
    SimAsi_MakePacket(Number, Size, Want);
    return memcmp(P + 8, Want, (size_t)Size) == 0 && P[212] == 0x58 && P[213] == Size &&
           (P[214] | P[215] << 8) == Sequence;
}

// A source writes transparent packets while the port receives: pieces without sync
// first, then the numbered packets, each with the card's time and the next sequence
// number.
DT_TEST(SourceWritesTransparentPackets)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    SimDtPcie_SetTxRealTime(false);
    SimAsiSource Source;
    SimAsi_DefaultSource(&Source);
    SimDtPcie_SetAsiSource(RX, &Source);
    uint32_t Offset = 1;

    // Nothing while the DMA does not receive.
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_EQ(Offset, 0u);

    OsDmaBuffer Buf;
    DT_ASSERT(StartRx(&Fix, &Buf));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_EQ(Offset, 8u * 216);
    for (int i = 0; i < 3; i++)
    {
        const uint8_t* P = Buf.Data + 216 * i;
        DT_ASSERT_EQ(P[212], 0x50);
        DT_ASSERT_EQ(P[213], 204);
        DT_ASSERT_EQ(P[214] | P[215] << 8, i);
    }
    for (int i = 3; i < 8; i++)
        DT_ASSERT(IsPacket(Buf.Data + 216 * i, (uint32_t)(i - 3), 188, i));
    const uint8_t* P = Buf.Data + 216 * 3;
    const uint64_t Seconds = (uint64_t)P[0] | (uint64_t)P[1] << 8 | (uint64_t)P[2] << 16 |
                             (uint64_t)P[3] << 24;
    DT_ASSERT(Seconds > 0 && Seconds <= SimDtPcie_Now() / 1000000000u);

    // A fault in the next packet.
    SimDtPcie_InjectAsiRxFault(RX, SIM_ASI_FAULT_SEQUENCE);
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_EQ(Offset, 16u * 216);
    DT_ASSERT(IsPacket(Buf.Data + 216 * 8, 5, 188, 9));
    DT_ASSERT(IsPacket(Buf.Data + 216 * 9, 6, 188, 10));
    SimDtPcie_InjectAsiRxFault(RX, SIM_ASI_FAULT_NOSYNC);
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_EQ(Buf.Data[216 * 16 + 212], 0x50);
    DT_ASSERT_EQ(Buf.Data[216 * 17 + 212], 0x58);

    // Packets the buffer has no room for are overflows.
    uint32_t Count = 1;
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetOvfUflCount(Fix.Drv, Fix.RxBurst, &Count));
    DT_ASSERT_EQ(Count, 0u);
    Source.PacketsPerRead = RX_BUFFER / 216 + 10;
    SimDtPcie_SetAsiSource(RX, &Source);
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetOvfUflCount(Fix.Drv, Fix.RxBurst, &Count));
    DT_ASSERT(Count > 0);

    // The write offset restarts at 0 when CDMAC goes idle.
    DT_ASSERT_OK(DtPcieCmd_CdmacSetOpMode(Fix.Drv, Fix.RxCdmac, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_EQ(Offset, 0u);

    Stop(&Fix, Fix.RxCdmac, &Buf);
    FINISH(Fix);
}

// In real time a source brings the packets its rate makes.
DT_TEST(SourceFollowsTheClock)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    SimAsiSource Source;
    SimAsi_DefaultSource(&Source);
    Source.UnsyncedAtStart = 0;
    Source.Rate = 15040000; // 10,000 packets a second
    SimDtPcie_SetAsiSource(RX, &Source);

    OsDmaBuffer Buf;
    uint32_t Offset;
    DT_ASSERT(StartRx(&Fix, &Buf));

    // The source starts counting during the first read and stops during the second.
    const uint64_t T0 = OsTime_MonotonicMs();
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    const uint64_t T1 = OsTime_MonotonicMs();
    OsTime_SleepMs(50);
    const uint64_t T2 = OsTime_MonotonicMs();
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    const uint64_t T3 = OsTime_MonotonicMs();
    const uint64_t Packets = Offset / 216;
    DT_ASSERT(Packets + 10 >= (T2 - T1) * 10 && Packets <= (T3 - T0) * 10 + 10);

    Stop(&Fix, Fix.RxCdmac, &Buf);
    FINISH(Fix);
}

// The sink decodes what TX sends: the data bytes in order, K28.5 between them, and no
// errors in what the encoder made.
DT_TEST(SinkDecodesTheSymbols)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    SimDtPcie_SetTxRealTime(false);
    OsDmaBuffer Buf;
    DT_ASSERT(StartTx(&Fix, &Buf));

    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 100000000));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    uint32_t Write = 0, Read = 0;
    DT_ASSERT(Send(&Fix, &Buf, &Enc, 0, 10, 188, &Write));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, Fix.TxCdmac, &Read));
    DT_ASSERT_EQ(Read, Write);

    uint8_t Got[10 * 188 + 1];
    DT_ASSERT_EQ(SimDtPcie_TakeAsiTxBytes(TX, Got, sizeof(Got)), (size_t)10 * 188);
    for (int i = 0; i < 10; i++)
    {
        uint8_t Want[188];
        SimAsi_MakePacket((uint32_t)i, 188, Want);
        DT_ASSERT(memcmp(Got + 188 * i, Want, 188) == 0);
    }
    SimAsiTxStats Stats;
    SimDtPcie_GetAsiTxStats(TX, &Stats);
    DT_ASSERT_EQ(Stats.DataBytes, 10 * 188);
    DT_ASSERT_EQ(Stats.Symbols, (int64_t)Write / 2);
    DT_ASSERT_EQ(Stats.K28 + Stats.DataBytes, Stats.Symbols);
    DT_ASSERT_EQ(Stats.CodeErrors, 0);
    DT_ASSERT_EQ(Stats.DisparityErrors, 0);

    // A symbol that is not a code, then K28.5 of one disparity only.
    Buf.Data[Write] = 0xFF;
    Buf.Data[Write + 1] = 0x03;
    for (uint32_t i = 2; i < 32; i += 2)
    {
        Buf.Data[Write + i] = (uint8_t)(DT_ASI_K28_5_RDNEG & 0xFF);
        Buf.Data[Write + i + 1] = (uint8_t)(DT_ASI_K28_5_RDNEG >> 8);
    }
    Write += 32;
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, Fix.TxCdmac, Write));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, Fix.TxCdmac, &Read));
    SimDtPcie_GetAsiTxStats(TX, &Stats);
    DT_ASSERT_EQ(Stats.CodeErrors, 1);
    DT_ASSERT(Stats.DisparityErrors > 0);

    // Nothing goes out while ASITXG does not run.
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetOpMode(Fix.Drv, Fix.AsiTxG, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT(Send(&Fix, &Buf, &Enc, 10, 2, 188, &Write));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, Fix.TxCdmac, &Read));
    DT_ASSERT_EQ(SimDtPcie_TakeAsiTxBytes(TX, Got, sizeof(Got)), (size_t)0);

    Stop(&Fix, Fix.TxCdmac, &Buf);
    FINISH(Fix);
}

// A loop from TX to RX: the input finds the 204-byte packets TX sends and receives
// them, with a carrier, lock and the packet size.
DT_TEST(LoopbackReceivesWhatIsSent)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    SimDtPcie_SetTxRealTime(false);
    SimDtPcie_SetAsiLoopback(TX, RX);
    OsDmaBuffer RxBuf, TxBuf;
    DT_ASSERT(StartTx(&Fix, &TxBuf));
    DT_ASSERT(StartRx(&Fix, &RxBuf));

    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_204 | DTAPI_TXMODE_BURST));
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 100000000));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    uint32_t Write = 0, Read, Offset;
    DT_ASSERT(Send(&Fix, &TxBuf, &Enc, 0, 12, 204, &Write));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, Fix.TxCdmac, &Read));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Fix.RxCdmac, &Offset));
    DT_ASSERT_EQ(Offset, 12u * 216);
    for (int i = 0; i < 12; i++)
        DT_ASSERT(IsPacket(RxBuf.Data + 216 * i, (uint32_t)i, 204, i));

    DtAsiRxStatus Status;
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Fix.AsiRx, &Status));
    DT_ASSERT(Status.CarrierDetect);
    DT_ASSERT(Status.AsiLock);
    DT_ASSERT_EQ(Status.PacketSize, DT_ASIRX_PCKSIZE_204);

    // Without the output running, no carrier.
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetOpMode(Fix.Drv, Fix.AsiTxG, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, Fix.TxCdmac, &Read));
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Fix.AsiRx, &Status));
    DT_ASSERT(!Status.CarrierDetect);

    SimDtPcie_SetAsiLoopback(-1, -1);
    Stop(&Fix, Fix.RxCdmac, &RxBuf);
    Stop(&Fix, Fix.TxCdmac, &TxBuf);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("SimAsi", DT_RUN(RequestsCarryTheirFields), DT_RUN(InvalidValuesSendNothing),
             DT_RUN(ObjectsCheckAccessAndPort), DT_RUN(StatusComesFromTheSignal),
             DT_RUN(ReceiveOffsets), DT_RUN(SourceWritesTransparentPackets),
             DT_RUN(SourceFollowsTheClock), DT_RUN(SinkDecodesTheSymbols),
             DT_RUN(LoopbackReceivesWhatIsSent))

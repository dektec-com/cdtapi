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
#include "DtFunc.h"                 // Finding the parts.
#include "DtPcieAbi.h"              // Types, commands and driver statuses.
#include "DtPcieCmd.h"              // Commands under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/OsDmaBuffer.h"        // Buffers to register.
#include "OAL/Sim/SimAsi.h"         // The emulated ASI blocks and their controls.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
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
    int AsiRx, RxCdmac, AsiTxG;
    int Live;
} Fixture;

// Sets one I/O configuration of the port at Index.
static DtapiResult Configure(OsDrv* Drv, int Index, int Group, int Value, int SubValue)
{
    DtIoConfig Config = {Index + 1, Group, Value, SubValue, {-1, -1}};
    return DtPcieCmd_SetIoConfig(Drv, &Config);
}

// Opens the emulated device in its power-on state, makes RX an ASI input and TX an ASI
// output, and finds their parts. Returns false, having recorded a failure, when that is
// not possible.
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    Fix->Live = DtAlloc_Live();
    Fix->Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    DtVec_Init(&Fix->Rx.Parts, sizeof(DtFuncPart));
    DtVec_Init(&Fix->RxDma.Parts, sizeof(DtFuncPart));
    DtVec_Init(&Fix->Tx.Parts, sizeof(DtFuncPart));
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
        DtFunc_Find(Fix->Drv, TX, "AF_ASISDITX", "", &Fix->Tx) != DTAPI_OK)
    {
        printf("    FAIL: no emulated ASI ports; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        DtFunc_Release(&Fix->Rx);
        DtFunc_Release(&Fix->RxDma);
        DtFunc_Release(&Fix->Tx);
        OsDrv_Close(Fix->Drv);
        return false;
    }
    Fix->AsiRx = DtFunc_Get(&Fix->Rx, true, DT_FUNC_TYPE_ASIRX, "")->Uuid;
    Fix->RxCdmac = DtFunc_Get(&Fix->RxDma, false, DT_BLOCK_TYPE_CDMAC, "")->Uuid;
    Fix->AsiTxG = DtFunc_Get(&Fix->Tx, false, DT_BLOCK_TYPE_ASITXG, "")->Uuid;
    return true;
}

// Takes exclusive access to every part the tests use.
static bool Acquire(Fixture* Fix)
{
    return DtFunc_ExclAccess(Fix->Drv, &Fix->Rx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK &&
           DtFunc_ExclAccess(Fix->Drv, &Fix->RxDma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK &&
           DtFunc_ExclAccess(Fix->Drv, &Fix->Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) ==
               DTAPI_OK;
}

// Frees the parts, closes the device and checks that nothing is left open or allocated.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtFunc_Release(&(Fix).Rx);                                                       \
        DtFunc_Release(&(Fix).RxDma);                                                    \
        DtFunc_Release(&(Fix).Tx);                                                       \
        OsDrv_Close((Fix).Drv);                                                          \
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_Live(), (Fix).Live);                                        \
    } while (0)

// Whether the last command was Cmd of FunctionCode for Uuid on the port at Index, with
// an input of Size bytes; when Value is not NULL, the input is a header and one Int,
// which must equal *Value.
static bool LastWas(int FunctionCode, int Uuid, int Index, int Cmd, size_t Size,
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
    return Got == Size && Code == FunctionCode && Hdr.m_Uuid == Uuid &&
           Hdr.m_PortIndex == Index && Hdr.m_Cmd == Cmd &&
           Hdr.m_CmdEx == DT_IOCTL_CMD_NOP;
}

#define HDR sizeof(DtIoctlInputDataHdr)
#define ONE_INT (HDR + sizeof(Int))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every command goes to its part as DTAPI's proxies send it, and what is set reads back.
DT_TEST(RequestsCarryTheirFields)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    const int Rx = Fix.AsiRx, TxG = Fix.AsiTxG;
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
            Result = DtPcieCmd_AsiRxSetPacketMode(Fix.Drv, Rx, RX, Want);
        else if (Set == DT_ASIRX_CMD_SET_POLARITY_CTRL)
            Result = DtPcieCmd_AsiRxSetPolarityCtrl(Fix.Drv, Rx, RX, Want);
        else
            Result = DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, RX, Want);
        DT_ASSERT_OK(Result);
        DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, Set, ONE_INT, &Want));

        if (Set == DT_ASIRX_CMD_SET_PACKET_MODE)
            Result = DtPcieCmd_AsiRxGetPacketMode(Fix.Drv, Rx, RX, &Value);
        else if (Set == DT_ASIRX_CMD_SET_POLARITY_CTRL)
            Result = DtPcieCmd_AsiRxGetPolarityCtrl(Fix.Drv, Rx, RX, &Value);
        else
            Result = DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, RX, &Value);
        DT_ASSERT_OK(Result);
        DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, RxSettings[i].Get, HDR, NULL));
        DT_ASSERT_EQ(Value, Want);
    }

    DT_ASSERT_OK(DtPcieCmd_AsiRxGetOpStatus(Fix.Drv, Rx, RX, &Value));
    DT_ASSERT_EQ(Value, DT_FUNC_OPSTATUS_IDLE);
    const int Run = DT_FUNC_OPMODE_RUN;
    DT_ASSERT_OK(DtPcieCmd_AsiRxSetOpMode(Fix.Drv, Rx, RX, Run));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, DT_ASIRX_CMD_SET_OPERATIONAL_MODE,
                      ONE_INT, &Run));
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetOpStatus(Fix.Drv, Rx, RX, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, DT_ASIRX_CMD_GET_OPERATIONAL_STATUS,
                      HDR, NULL));
    DT_ASSERT_EQ(Value, DT_FUNC_OPSTATUS_IDLE); // Running, but nothing to receive
    SimAsiSignal Signal = {true, true, DT_ASIRX_PCKSIZE_188, DT_ASIRX_POLARITY_NORMAL,
                           0,    0};
    SimDtPcie_SetAsiSignal(RX, &Signal);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetOpStatus(Fix.Drv, Rx, RX, &Value));
    DT_ASSERT_EQ(Value, DT_FUNC_OPSTATUS_RUN);

    const int Standby = DT_BLOCK_OPMODE_STANDBY;
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetOpMode(Fix.Drv, TxG, TX, Standby));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, TX,
                      DT_ASITXG_CMD_SET_OPERATIONAL_MODE, ONE_INT, &Standby));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetOpMode(Fix.Drv, TxG, TX, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, TX,
                      DT_ASITXG_CMD_GET_OPERATIONAL_MODE, HDR, NULL));
    DT_ASSERT_EQ(Value, Standby);

    const int Invert = DT_ASITXG_POL_INVERT;
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetPolarity(Fix.Drv, TxG, TX, Invert));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, TX, DT_ASITXG_CMD_SET_ASI_POLARITY,
                      ONE_INT, &Invert));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, TX, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, TX, DT_ASITXG_CMD_GET_ASI_POLARITY,
                      HDR, NULL));
    DT_ASSERT_EQ(Value, Invert);

    DT_ASSERT_OK(DtPcieCmd_AsiTxGClearInputState(Fix.Drv, TxG, TX));
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXG_CMD, TxG, TX, DT_ASITXG_CMD_CLEAR_INPUT_STATE,
                      HDR, NULL));

    SimAsiState State;
    SimDtPcie_GetAsiState(TX, &State);
    DT_ASSERT_EQ(State.TxgInputClears, 1);
    SimDtPcie_GetAsiState(RX, &State);
    DT_ASSERT_EQ(State.RxMode, DT_FUNC_OPMODE_RUN);
    DT_ASSERT_EQ(State.RxPacketMode, DT_ASIRX_PCKMODE_RAW);

    // The DTA-2178 has no serialiser; the request is still DTAPI's.
    DT_ASSERT_EQ(DtPcieCmd_AsiTxSerSetOpMode(Fix.Drv, TxG, TX, Standby),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXSER_CMD, TxG, TX,
                      DT_ASITXSER_CMD_SET_OPERATIONAL_MODE, ONE_INT, &Standby));
    DT_ASSERT_EQ(DtPcieCmd_AsiTxSerGetOpMode(Fix.Drv, TxG, TX, &Value),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(LastWas(DT_FUNC_CODE_ASITXSER_CMD, TxG, TX,
                      DT_ASITXSER_CMD_GET_OPERATIONAL_MODE, HDR, NULL));
    DT_ASSERT_EQ(Value, 0);

    FINISH(Fix);
}

// A value DTAPI's proxy would not send is refused here, and nothing reaches the driver.
DT_TEST(InvalidValuesSendNothing)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    DT_ASSERT(Acquire(&Fix));
    const int Rx = Fix.AsiRx, TxG = Fix.AsiTxG;

    int Value;
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, RX, &Value));
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetOpMode(Fix.Drv, Rx, RX, DT_FUNC_OPMODE_STANDBY),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetPacketMode(Fix.Drv, Rx, RX, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(
        DtPcieCmd_AsiRxSetPolarityCtrl(Fix.Drv, Rx, RX, DT_ASIRX_POLARITY_UNKNOWN),
        DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, RX, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGSetOpMode(Fix.Drv, TxG, TX, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGSetPolarity(Fix.Drv, TxG, TX, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxSerSetOpMode(Fix.Drv, TxG, TX, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, DT_ASIRX_CMD_GET_SYNC_MODE, HDR, NULL));

    DtAsiRxStatus Status;
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, RX, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetViolCount(Fix.Drv, Rx, RX, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetStatus(NULL, Rx, RX, &Status), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGClearInputState(NULL, TxG, TX), DTAPI_E_INVALID_ARG);

    FINISH(Fix);
}

// Settings need exclusive access and readings do not; a part of a port that is not ASI in
// its direction refuses both, and forgets what was set.
DT_TEST(PartsCheckAccessAndPort)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    const int Rx = Fix.AsiRx, TxG = Fix.AsiTxG;
    int Value;

    DT_ASSERT_EQ(DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, RX, DT_ASIRX_SYNCMODE_188),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGClearInputState(Fix.Drv, TxG, TX),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, RX, &Value));
    DT_ASSERT_EQ(Value, DT_ASIRX_SYNCMODE_AUTO);
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, TX, &Value));
    DT_ASSERT_EQ(Value, DT_ASITXG_POL_NORMAL);

    DT_ASSERT(Acquire(&Fix));
    DT_ASSERT_OK(DtPcieCmd_AsiRxSetSyncMode(Fix.Drv, Rx, RX, DT_ASIRX_SYNCMODE_188));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGSetPolarity(Fix.Drv, TxG, TX, DT_ASITXG_POL_INVERT));

    // SDI instead of ASI.
    DT_ASSERT_OK(Configure(Fix.Drv, RX, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_HDSDI,
                           DTAPI_IOCONFIG_1080I50));
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, RX, &Value),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_OK(Configure(Fix.Drv, RX, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1));
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetSyncMode(Fix.Drv, Rx, RX, &Value));
    DT_ASSERT_EQ(Value, DT_ASIRX_SYNCMODE_AUTO);

    // The other direction.
    DT_ASSERT_OK(Configure(Fix.Drv, TX, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                           DTAPI_IOCONFIG_INPUT));
    DT_ASSERT_EQ(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, TX, &Value),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_OK(Configure(Fix.Drv, TX, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                           DTAPI_IOCONFIG_OUTPUT));
    DT_ASSERT_OK(DtPcieCmd_AsiTxGGetPolarity(Fix.Drv, TxG, TX, &Value));
    DT_ASSERT_EQ(Value, DT_ASITXG_POL_NORMAL);

    // A refusal passes through with its code.
    SimDtPcie_FailAsiCmd(DT_FUNC_CODE_ASIRX_CMD, DT_ASIRX_CMD_GET_TS_BITRATE,
                         DT_STATUS_BUSY);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetTsBitrate(Fix.Drv, Rx, RX, &Value), DTAPI_E_BUSY);
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
    const int Rx = Fix.AsiRx;
    DtAsiRxStatus Status;
    int Bitrate, Viol;

    DT_ASSERT_OK(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, RX, &Status));
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, DT_ASIRX_CMD_GET_STATUS, HDR, NULL));
    DT_ASSERT(!Status.CarrierDetect);
    DT_ASSERT(!Status.AsiLock);
    DT_ASSERT_EQ(Status.PacketSize, DT_ASIRX_PCKSIZE_UNKNOWN);
    DT_ASSERT_EQ(Status.Polarity, DT_ASIRX_POLARITY_UNKNOWN);

    SimAsiSignal Signal = {true,     true, DT_ASIRX_PCKSIZE_204, DT_ASIRX_POLARITY_INVERT,
                           10851064, 3};
    SimDtPcie_SetAsiSignal(RX, &Signal);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, RX, &Status));
    DT_ASSERT(Status.CarrierDetect);
    DT_ASSERT(Status.AsiLock);
    DT_ASSERT_EQ(Status.PacketSize, DT_ASIRX_PCKSIZE_204);
    DT_ASSERT_EQ(Status.Polarity, DT_ASIRX_POLARITY_INVERT);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetTsBitrate(Fix.Drv, Rx, RX, &Bitrate));
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, DT_ASIRX_CMD_GET_TS_BITRATE, HDR, NULL));
    DT_ASSERT_EQ(Bitrate, 10851064);
    DT_ASSERT_OK(DtPcieCmd_AsiRxGetViolCount(Fix.Drv, Rx, RX, &Viol));
    DT_ASSERT(
        LastWas(DT_FUNC_CODE_ASIRX_CMD, Rx, RX, DT_ASIRX_CMD_GET_VIOL_COUNT, HDR, NULL));
    DT_ASSERT_EQ(Viol, 3);

    // A packet size DTAPI does not know is the driver's fault.
    Signal.PacketSize = 7;
    SimDtPcie_SetAsiSignal(RX, &Signal);
    DT_ASSERT_EQ(DtPcieCmd_AsiRxGetStatus(Fix.Drv, Rx, RX, &Status), DTAPI_E_DEV_DRIVER);
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
    const int Cdmac = Fix.RxCdmac;
    uint32_t Offset = 5;

    // Without a receive buffer, as a DTA-2178 answers.
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Cdmac, RX, &Offset));
    DT_ASSERT_EQ(Offset, 0u);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetRxReadOffset(Fix.Drv, Cdmac, RX, 0));
    Offset = 5;

    OsDmaBuffer Buf;
    DT_ASSERT(OsDmaBuffer_Alloc(2 * 1024 * 1024, &Buf) == 0);
    DT_ASSERT_OK(
        DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, Cdmac, RX, DT_CDMAC_DIR_RX, &Buf));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetRxWriteOffset(Fix.Drv, Cdmac, RX, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, Cdmac, RX, DT_CDMAC_CMD_GET_RX_WRITE_OFFSET,
                      HDR, NULL));
    DT_ASSERT_EQ(Offset, 0u);

    const int At = 216 * 10;
    DT_ASSERT_OK(DtPcieCmd_CdmacSetRxReadOffset(Fix.Drv, Cdmac, RX, (uint32_t)At));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, Cdmac, RX, DT_CDMAC_CMD_SET_RX_READ_OFFSET,
                      ONE_INT, &At));
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetRxReadOffset(Fix.Drv, Cdmac, RX, 2 * 1024 * 1024),
                 DTAPI_E_INVALID_ARG);

    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, Cdmac, RX));
    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("SimAsi", DT_RUN(RequestsCarryTheirFields), DT_RUN(InvalidValuesSendNothing),
             DT_RUN(PartsCheckAccessAndPort), DT_RUN(StatusComesFromTheSignal),
             DT_RUN(ReceiveOffsets))

// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimSdiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Exclusive access and the transmit blocks against the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, with the output going out as fast as waits come rather than on the clock, and
// ends with no handle to it and no allocation left open.

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
#include "OAL/Sim/SimChSdiRx.h"     // Lines of a test pattern.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "OAL/Sim/SimSdiTx.h"       // The emulated transmit blocks and their controls.
#include "Video/DtSdiFrame.h"       // The geometry of coded lines.
#include "cdtapi.h"                 // Results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The index of the port the tests use: port 2, an output by default.
#define PORT 1

typedef struct Fixture
{
    OsDrv* Drv;
    DtFuncInstance Tx;  // AF_ASISDITX
    DtFuncInstance Dma; // AF_DMA
    int Live;
} Fixture;

// Opens the emulated device in its power-on state and finds the port's transmitter and
// DMA. Returns false, having recorded a failure, when that is not possible.
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcie_Reset();
    SimDtPcie_SetTxRealTime(false);
    Fix->Live = DtAlloc_NumLive();
    Fix->Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    DtVec_Init(&Fix->Tx.Objects, sizeof(DtFuncObject));
    DtVec_Init(&Fix->Dma.Objects, sizeof(DtFuncObject));
    if (Fix->Drv == NULL || !OsDrv_IsEmulated(Fix->Drv) ||
        DtFunc_Find(Fix->Drv, PORT, "AF_ASISDITX", "", &Fix->Tx) != DTAPI_OK ||
        DtFunc_Find(Fix->Drv, PORT, "AF_DMA", "", &Fix->Dma) != DTAPI_OK)
    {
        printf("    FAIL: no emulated transmitter; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        DtFunc_Release(&Fix->Tx);
        DtFunc_Release(&Fix->Dma);
        OsDrv_Close(Fix->Drv);
        return false;
    }
    return true;
}

// Frees the objects, closes the device and checks that nothing is left open or allocated.
// The frames the sink kept are the emulator's until a reset, which comes first.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtFunc_Release(&(Fix).Tx);                                                       \
        DtFunc_Release(&(Fix).Dma);                                                      \
        OsDrv_Close((Fix).Drv);                                                          \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        SimDtPcie_Reset();                                                               \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Fix).Live);                                     \
    } while (0)

// The object of Instance at Index.
static DtDrvObject ObjectAt(const DtFuncInstance* Instance, size_t Index)
{
    return DT_VEC_AT(&Instance->Objects, DtFuncObject, Index).Object;
}

// The object of Instance with IsDriverFunction, Type and Role; with UUID 0 when there is
// none.
static DtDrvObject ObjectOf(const DtFuncInstance* Instance, bool IsDriverFunction,
                            int Type, const char* Role)
{
    const DtFuncObject* Object =
        DtFunc_FindObject(Instance, IsDriverFunction, Type, Role);
    const DtDrvObject None = {0, PORT};
    return Object != NULL ? Object->Object : None;
}

// The objects a transmit channel drives.
typedef struct Objects
{
    DtDrvObject Cdmac, Burst, Txf, SwitchIn, SwitchOut, Dmx, Txp, Phy;
} Objects;

static Objects ObjectsOf(const Fixture* Fix)
{
    Objects P;

    P.Cdmac = ObjectOf(&Fix->Dma, false, DT_BLOCK_TYPE_CDMAC, "");
    P.Burst = ObjectOf(&Fix->Dma, false, DT_BLOCK_TYPE_BURSTFIFO, "");
    P.Txf = ObjectOf(&Fix->Tx, false, DT_BLOCK_TYPE_SDITXF, "");
    P.SwitchIn = ObjectOf(&Fix->Tx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_IN");
    P.SwitchOut = ObjectOf(&Fix->Tx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_OUT");
    P.Dmx = ObjectOf(&Fix->Tx, false, DT_BLOCK_TYPE_SDIDMX12G, "");
    P.Txp = ObjectOf(&Fix->Tx, false, DT_BLOCK_TYPE_SDITXP, "");
    P.Phy = ObjectOf(&Fix->Tx, true, DT_FUNC_TYPE_SDITXPHY, "");
    return P;
}

// Whether the last command was Cmd of FunctionCode for Object, with an input of Size
// bytes, which are copied to Input when it is not NULL.
static bool LastWas(int FunctionCode, DtDrvObject Object, int Cmd, size_t Size,
                    void* Input)
{
    DtIoctlInputDataHdr Hdr;
    int Code = -1;
    uint8_t In[SIM_MAX_RECORDED_INPUT];
    size_t Got = SimDtPcie_LastInput(&Code, In, sizeof(In));

    memcpy(&Hdr, In, sizeof(Hdr));
    if (Input != NULL)
        memcpy(Input, In, Size);
    return Got == Size && Code == FunctionCode && Hdr.m_Uuid == Object.Uuid &&
           Hdr.m_PortIndex == Object.PortIndex && Hdr.m_Cmd == Cmd &&
           Hdr.m_CmdEx == DT_IOCTL_CMD_NOP;
}

// The video standard of the frames the tests send, and its coded geometry.
#define VIDSTD DTAPI_VIDSTD_525I59_94

// A buffer of 8 MB, room for seven coded 525i frames.
#define BUFFER_SIZE (8 * 1024 * 1024)

// Acquires every object and brings the port to what a transmit channel in HOLD is: a
// registered buffer, every block running, the switches bypassing the demultiplexer and
// the PHY in standby. Returns false, having recorded a failure, when that fails.
static bool Hold(Fixture* Fix, Objects* P, OsDmaBuffer* Buf, int* DtFailures)
{
    *P = ObjectsOf(Fix);
    DtSdiFrameLayout Layout;
    DtSdiFrame_LayoutInit(&Layout, VIDSTD, SIM_TX_STREAM_ALIGNMENT);
    if (OsDmaBuffer_Alloc(BUFFER_SIZE, Buf) != 0 ||
        DtFunc_ExclAccess(Fix->Drv, &Fix->Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) !=
            DTAPI_OK ||
        DtFunc_ExclAccess(Fix->Drv, &Fix->Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) !=
            DTAPI_OK ||
        DtPcieCmd_CdmacAllocateBuffer(Fix->Drv, P->Cdmac, DT_CDMAC_DIR_TX, Buf) !=
            DTAPI_OK ||
        DtPcieCmd_SdiTxFSetFmtEventSetting(
            Fix->Drv, P->Txf, (Layout.NumLines + 3) / 4 + 1, 1) != DTAPI_OK ||
        DtPcieCmd_CdmacFlushChannel(Fix->Drv, P->Cdmac) != DTAPI_OK ||
        DtPcieCmd_CdmacSetTxWriteOffset(Fix->Drv, P->Cdmac, 0) != DTAPI_OK ||
        DtPcieCmd_CdmacSetOpMode(Fix->Drv, P->Cdmac, DT_BLOCK_OPMODE_RUN) != DTAPI_OK ||
        DtPcieCmd_BurstFifoSetOpMode(Fix->Drv, P->Burst, DT_BLOCK_OPMODE_RUN) !=
            DTAPI_OK ||
        DtPcieCmd_SdiTxFSetOpMode(Fix->Drv, P->Txf, DT_BLOCK_OPMODE_RUN) != DTAPI_OK ||
        DtPcieCmd_SwitchSetPosition(Fix->Drv, P->SwitchIn, 0, 0) != DTAPI_OK ||
        DtPcieCmd_SwitchSetPosition(Fix->Drv, P->SwitchOut, 0, 0) != DTAPI_OK ||
        DtPcieCmd_SwitchSetOpMode(Fix->Drv, P->SwitchIn, DT_BLOCK_OPMODE_RUN) !=
            DTAPI_OK ||
        DtPcieCmd_SdiDmx12GSetOpMode(Fix->Drv, P->Dmx, DT_BLOCK_OPMODE_IDLE) !=
            DTAPI_OK ||
        DtPcieCmd_SwitchSetOpMode(Fix->Drv, P->SwitchOut, DT_BLOCK_OPMODE_RUN) !=
            DTAPI_OK ||
        DtPcieCmd_SdiTxPSetOpMode(Fix->Drv, P->Txp, DT_BLOCK_OPMODE_RUN) != DTAPI_OK ||
        DtPcieCmd_SdiTxPhySetOpMode(Fix->Drv, P->Phy, DT_FUNC_OPMODE_STANDBY) != DTAPI_OK)
    {
        printf("    FAIL: cannot bring the transmit blocks to hold\n");
        (*DtFailures)++;
        return false;
    }
    return true;
}

// Packs Count 10-bit symbols, least significant bit first, into Bytes bytes at Out.
static void Pack(const uint16_t* Symbols, int Count, uint8_t* Out, int Bytes)
{
    uint32_t Accu = 0;
    int Have = 0;
    int At = 0;

    memset(Out, 0, (size_t)Bytes);
    for (int i = 0; i < Count; i++)
    {
        Accu |= (uint32_t)(Symbols[i] & 0x3FF) << Have;
        for (Have += 10; Have >= 8; Have -= 8)
        {
            Out[At++] = (uint8_t)Accu;
            Accu >>= 8;
        }
    }
    if (Have > 0)
        Out[At] = (uint8_t)Accu;
}

// Copies Size bytes into the buffer at *Offset, across its end, and moves *Offset on.
static void Put(OsDmaBuffer* Buf, uint32_t* Offset, const uint8_t* Data, size_t Size)
{
    for (size_t i = 0; i < Size; i++)
        Buf->Data[(*Offset + i) % Buf->Size] = Data[i];
    *Offset = (uint32_t)((*Offset + Size) % Buf->Size);
}

// Writes the header of a frame with FrameId and the lines of frame FrameNumber of the
// test pattern at *Offset.
static void PutFrame(OsDmaBuffer* Buf, uint32_t* Offset, uint32_t FrameNumber,
                     int FrameId)
{
    DtSdiFrameLayout L;

    DtSdiFrame_LayoutInit(&L, VIDSTD, SIM_TX_STREAM_ALIGNMENT);
    uint32_t Words[5];
    Words[0] = 0xFFEFFBFEu;
    Words[1] = 1u << 8 | (uint32_t)DT_DRV_SDIRATE_SD << 9;
    Words[2] = (uint32_t)FrameId | (uint32_t)L.NumLines << 16;
    Words[3] = (uint32_t)(L.SectionBytesHanc / L.AlignmentInBytes) |
               (uint32_t)L.LineNumSymsHanc << 16;
    Words[4] = (uint32_t)(L.SectionBytesActive / L.AlignmentInBytes) |
               (uint32_t)L.LineNumSymsActive << 16;
    uint8_t Header[32];
    memset(Header, 0, sizeof(Header));
    for (int i = 0; i < 5; i++)
        memcpy(Header + 4 * i, &Words[i], 4);
    Put(Buf, Offset, Header, sizeof(Header));

    uint16_t Symbols[2000];
    uint8_t Line[4096];
    for (int n = 1; n <= L.NumLines; n++)
    {
        SimChSdiRx_Line(VIDSTD, FrameNumber, n, Symbols);
        Pack(Symbols, L.LineNumSymsHanc, Line, L.SectionBytesHanc);
        Pack(Symbols + L.LineNumSymsHanc, L.LineNumSymsActive, Line + L.SectionBytesHanc,
             L.SectionBytesActive);
        Put(Buf, Offset, Line, (size_t)L.RxStride);
    }
}

// Whether the kept frame at Index holds frame FrameNumber of the test pattern with
// FrameId.
static bool Received(int Index, uint32_t FrameNumber, int FrameId)
{
    SimTxFrame Frame;

    if (!SimDtPcie_GetTxFrame(PORT, Index, &Frame) || Frame.FrameId != FrameId)
        return false;
    int Width = Frame.SymsHanc + Frame.SymsVideo;
    uint16_t Symbols[2000];
    for (int n = 1; n <= Frame.NumLines; n++)
    {
        if (SimChSdiRx_Line(VIDSTD, FrameNumber, n, Symbols) != Width ||
            memcmp(Symbols, Frame.Symbols + (size_t)(n - 1) * (size_t)Width,
                   (size_t)Width * sizeof(uint16_t)) != 0)
        {
            return false;
        }
    }
    return true;
}

// Asks for the next event without waiting: a time-out of 0.
static DtapiResult Wait(const Fixture* Fix, const Objects* P, DtSdiTxFEvent* Event)
{
    return DtPcieCmd_SdiTxFWaitForFmtEvent(Fix->Drv, P->Txf, 0, Event);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exclusive access +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// One handle holds an object at a time; checking, probing and releasing answer as the
// driver's building blocks do.
DT_TEST(OneHandleHoldsAnObject)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    OsDrv* Other = OsDrv_Open(SIM_DEVICE_INDEX);
    DtDrvObject Uuid = ObjectOf(&Fix.Dma, false, DT_BLOCK_TYPE_CDMAC, "");
    DT_ASSERT(Other != NULL && Uuid.Uuid != 0);

    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_CHECK),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_PROBE));
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));

    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_PROBE),
                 DTAPI_E_IN_USE);

    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Other, Uuid, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Other, Uuid, DT_EXCLUSIVE_ACCESS_CMD_CHECK),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Other, Uuid, DT_EXCLUSIVE_ACCESS_CMD_RELEASE),
                 DTAPI_E_IN_USE);

    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Other, Uuid, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    // Closing a handle lets go of what it holds.
    OsDrv_Close(Other);
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, Uuid, DT_EXCLUSIVE_ACCESS_CMD_PROBE));

    FINISH(Fix);
}

// Every object of both functions, driver function included, has exclusive access of its
// own; a UUID the card does not have takes no command, and an unknown exclusive-access
// command is not supported.
DT_TEST(EveryObjectHasExclusiveAccess)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;

    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    size_t i;
    for (i = 0; i < DtVec_Count(&Fix.Tx.Objects); i++)
    {
        DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Tx, i),
                                          DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    }
    for (i = 0; i < DtVec_Count(&Fix.Dma.Objects); i++)
    {
        DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Dma, i),
                                          DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    }
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Tx, 0),
                                      DT_EXCLUSIVE_ACCESS_CMD_CHECK),
                 DTAPI_E_EXCL_ACCESS_REQD);

    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Fix.Drv,
                                      (DtDrvObject){DT_UUID_BC_FLAG | 0xFFFF, PORT},
                                      DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_NOT_IMPLEMENTED);
    DT_ASSERT_EQ(DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Tx, 0), 99),
                 DTAPI_E_NOT_SUPPORTED);

    FINISH(Fix);
}

// Acquiring every object stops at the first object another handle holds, and lets go of
// the objects before it; an object without exclusive access is passed over.
DT_TEST(AcquiringAllRollsBack)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    OsDrv* Other = OsDrv_Open(SIM_DEVICE_INDEX);
    DT_ASSERT(Other != NULL && DtVec_Count(&Fix.Tx.Objects) == 7);

    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Other, ObjectAt(&Fix.Tx, 3),
                                      DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtFunc_ExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_IN_USE);
    for (size_t i = 0; i < DtVec_Count(&Fix.Tx.Objects); i++)
    {
        DtapiResult Probe = DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Tx, i),
                                                 DT_EXCLUSIVE_ACCESS_CMD_PROBE);

        if (Probe != (i == 3 ? (DtapiResult)DTAPI_E_IN_USE : (DtapiResult)DTAPI_OK))
            DT_FAIL("part %zu: %s", i, DtapiResult2Str(Probe));
    }

    // Releasing all goes on past the object another handle holds, and reports it.
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Tx, 6),
                                      DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtFunc_ExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, ObjectAt(&Fix.Tx, 6),
                                      DT_EXCLUSIVE_ACCESS_CMD_PROBE));
    OsDrv_Close(Other);

    SimDtPcie_FailWithStatus(DT_FUNC_CODE_EXCL_ACCESS_CMD, DT_STATUS_NOT_SUPPORTED);
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Requests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Every command goes to its block with the driver's command number and structure.
DT_TEST(RequestsCarryTheirFields)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);

    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, P.Cdmac, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT(LastWas(DT_FUNC_CODE_EXCL_ACCESS_CMD, P.Cdmac,
                      DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE, sizeof(DtIoctlExclAccessCmdInput),
                      NULL));

    DtCdmacProps CdmacProps;
    DT_ASSERT_OK(DtPcieCmd_CdmacGetProps(Fix.Drv, P.Cdmac, &CdmacProps));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_GET_PROPERTIES,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_EQ(CdmacProps.Caps, DT_CDMAC_CAP_RX | DT_CDMAC_CAP_TX);
    DT_ASSERT_EQ(CdmacProps.PrefetchSize, SIM_TX_PREFETCH_PAGES);
    DT_ASSERT_EQ(CdmacProps.PcieDataWidth, SIM_TX_PCIE_DATA_WIDTH);
    DT_ASSERT_EQ(CdmacProps.ReorderBufSize, SIM_TX_REORDER_BUF_SIZE);

    DT_ASSERT_OK(DtPcieCmd_CdmacFlushChannel(Fix.Drv, P.Cdmac));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_ISSUE_CHANNEL_FLUSH,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_FREE_BUFFER,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_OK(DtPcieCmd_CdmacClearReorderBufMinMax(Fix.Drv, P.Cdmac));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                      DT_CDMAC_CMD_CLEAR_REORDER_BUF_MIN_MAX, sizeof(DtIoctlInputDataHdr),
                      NULL));
    {
        DT_ASSERT_EQ(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_RUN),
                     DTAPI_E_NOT_INITIALIZED);
        DtIoctlCDmaCCmdSetOpModeInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                          DT_CDMAC_CMD_SET_OPERATIONAL_MODE, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_OpMode, DT_BLOCK_OPMODE_RUN);
    }
    {
        DT_ASSERT_OK(
            DtPcieCmd_CdmacSetTestMode(Fix.Drv, P.Cdmac, DT_CDMAC_TESTMODE_TEST_EXT));
        DtIoctlCDmaCCmdSetTestModeInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_SET_TEST_MODE,
                          sizeof(In), &In));
        DT_ASSERT_EQ(In.m_TestMode, DT_CDMAC_TESTMODE_TEST_EXT);
    }
    {
        DT_ASSERT_EQ(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, 0x12340),
                     DTAPI_E_INVALID_ARG);
        DtIoctlCDmaCCmdSetTxWrOffsetInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                          DT_CDMAC_CMD_SET_TX_WRITE_OFFSET, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_TxWriteOffset, 0x12340);
    }
    uint32_t Offset;
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, P.Cdmac, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_GET_TX_READ_OFFSET,
                      sizeof(DtIoctlInputDataHdr), NULL));
    int Value;
    int MinMax;
    DT_ASSERT_OK(DtPcieCmd_CdmacGetReorderBufStatus(Fix.Drv, P.Cdmac, &Value, &MinMax));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                      DT_CDMAC_CMD_GET_REORDER_BUF_STATUS, sizeof(DtIoctlInputDataHdr),
                      NULL));

    DtBurstFifoProps BurstProps;
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetProps(Fix.Drv, P.Burst, &BurstProps));
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_GET_PROPERTIES, sizeof(DtIoctlInputDataHdr),
                      NULL));
    DT_ASSERT_EQ(BurstProps.FifoSize, SIM_TX_BURST_FIFO_SIZE);
    DT_ASSERT_EQ(BurstProps.DataWidth, SIM_TX_PCIE_DATA_WIDTH);
    DtBurstFifoStatus BurstStatus;
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetStatus(Fix.Drv, P.Burst, &BurstStatus));
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_GET_FIFO_STATUS, sizeof(DtIoctlInputDataHdr),
                      NULL));
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetOvfUflCount(Fix.Drv, P.Burst, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_GET_OVFL_UFL_COUNT, sizeof(DtIoctlInputDataHdr),
                      NULL));
    {
        DT_ASSERT_OK(DtPcieCmd_BurstFifoClearMax(Fix.Drv, P.Burst, false, true));
        DtIoctlBurstFifoCmdClearFifoMaxInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                          DT_BURSTFIFO_CMD_CLEAR_FIFO_MAX, sizeof(In), &In));
        DT_ASSERT(In.m_ClearMaxFree == 0 && In.m_ClearMaxLoad == 1);
    }
    DT_ASSERT_EQ(DtPcieCmd_BurstFifoSetOpMode(Fix.Drv, P.Burst, DT_BLOCK_OPMODE_STANDBY),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlBurstFifoCmdSetOpModeInput), NULL));

    DT_ASSERT_OK(DtPcieCmd_SdiTxFGetStreamAlignment(Fix.Drv, P.Txf, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf, DT_SDITXF_CMD_GET_STREAM_ALIGNMENT,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_EQ(Value, SIM_TX_STREAM_ALIGNMENT);
    {
        DT_ASSERT_EQ(DtPcieCmd_SdiTxFSetFmtEventSetting(Fix.Drv, P.Txf, 283, 7),
                     DTAPI_E_EXCL_ACCESS_REQD);
        DtIoctlSdiTxFCmdSetFmtEventSettingInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf,
                          DT_SDITXF_CMD_SET_FMT_EVENT_SETTING, sizeof(In), &In));
        DT_ASSERT(In.m_NumLinesPerEvent == 283 && In.m_NumSofsBetweenTod == 7);
    }
    {
        DtSdiTxFEvent Event;
        DT_ASSERT_EQ(DtPcieCmd_SdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, 40, &Event),
                     DTAPI_E_EXCL_ACCESS_REQD);
        DtIoctlSdiTxFCmdWaitForFmtEventInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf,
                          DT_SDITXF_CMD_WAIT_FOR_FMT_EVENT, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_Timeout, 40);
    }
    DT_ASSERT_EQ(DtPcieCmd_SdiTxFSetOpMode(Fix.Drv, P.Txf, DT_BLOCK_OPMODE_RUN),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf, DT_SDITXF_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiTxFCmdSetOpModeInput), NULL));

    {
        DT_ASSERT_EQ(DtPcieCmd_SwitchSetPosition(Fix.Drv, P.SwitchIn, 0, 1),
                     DTAPI_E_EXCL_ACCESS_REQD);
        DtIoctlSwitchCmdSetPositionInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_SWITCH_CMD, P.SwitchIn, DT_SWITCH_CMD_SET_POSITION,
                          sizeof(In), &In));
        DT_ASSERT(In.m_InputIndex == 0 && In.m_OutputIndex == 1);
    }
    DT_ASSERT_EQ(DtPcieCmd_SwitchSetOpMode(Fix.Drv, P.SwitchOut, DT_BLOCK_OPMODE_RUN),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SWITCH_CMD, P.SwitchOut,
                      DT_SWITCH_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSwitchCmdSetOpModeInput), NULL));
    DT_ASSERT_EQ(DtPcieCmd_SdiDmx12GSetOpMode(Fix.Drv, P.Dmx, DT_BLOCK_OPMODE_IDLE),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDIDMX12G_CMD, P.Dmx,
                      DT_SDIDMX12G_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiDmx12GCmdSetOpModeInput), NULL));

    // The encoder's commands need no exclusive access.
    DT_ASSERT_OK(DtPcieCmd_SdiTxPSetOpMode(Fix.Drv, P.Txp, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXP_CMD, P.Txp, DT_SDITXP_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiTxPCmdSetOpModeInput), NULL));
    {
        DT_ASSERT_OK(
            DtPcieCmd_SdiTxPSetGenerationMode(Fix.Drv, P.Txp, true, false, true));
        DtIoctlSdiTxPCmdSetGenModeInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXP_CMD, P.Txp,
                          DT_SDITXP_CMD_SET_GENERATION_MODE, sizeof(In), &In));
        DT_ASSERT(In.m_ClampEnable == 1 && In.m_AdpChecksumEnable == 0 &&
                  In.m_LineCrcEnable == 1);
        SimTxState State;
        SimDtPcie_GetTxState(PORT, &State);
        DT_ASSERT(State.ClampEnabled && !State.AdpChecksumEnabled &&
                  State.LineCrcEnabled);
    }

    DT_ASSERT_EQ(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, DT_FUNC_OPMODE_STANDBY),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                      DT_SDITXPHY_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiTxPhyCmdSetOpModeInput), NULL));
    bool Flag;
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, &Flag));
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                      DT_SDITXPHY_CMD_GET_UNDERFLOW_FLAG, sizeof(DtIoctlInputDataHdr),
                      NULL));
    DT_ASSERT_EQ(DtPcieCmd_SdiTxPhyClearUnderflowFlag(Fix.Drv, P.Phy),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                      DT_SDITXPHY_CMD_CLEAR_UNDERFLOW_FLAG, sizeof(DtIoctlInputDataHdr),
                      NULL));
    {
        DT_ASSERT_OK(DtPcieCmd_SdiTxPhySetStartOfFrameOffset(Fix.Drv, P.Phy, 1500));
        DtIoctlSdiTxPhyCmdSetStartOfFrameOffsetInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                          DT_SDITXPHY_CMD_SET_START_OF_FRAME_OFFSET, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_StartOfFrameOffsetNs, 1500);
    }

    FINISH(Fix);
}

// Arguments the DtPcieCmd functions refuse send nothing.
DT_TEST(InvalidArgumentsSendNothing)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Fix.Drv, P.Cdmac, DT_EXCLUSIVE_ACCESS_CMD_PROBE));

    DT_ASSERT_EQ(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetTestMode(Fix.Drv, P.Cdmac, 3), DTAPI_E_INVALID_ARG);
    OsDmaBuffer Buf;
    memset(&Buf, 0, sizeof(Buf));
    DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_INVALID_ARG);
    DtSdiTxFEvent Event;
    Buf.Data = (uint8_t*)&Event;
    Buf.Size = 4096;
    DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, 2, &Buf),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_SdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, 0, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_CdmacGetProps(NULL, P.Cdmac, NULL), DTAPI_E_INVALID_ARG);

    int Code;
    uint8_t In[64];
    SimDtPcie_LastInput(&Code, In, sizeof(In));
    DT_ASSERT_EQ(Code, DT_FUNC_CODE_EXCL_ACCESS_CMD);

    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The buffer travels as the output, as on Windows, or as the address in the input, as on
// Linux, and the controller takes it only the way its driver does.
DT_TEST(BufferIsRegisteredBothWays)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    OsDmaBuffer Buf;
    DT_ASSERT(OsDmaBuffer_Alloc(BUFFER_SIZE, &Buf) == 0);

    SimTxState State;
    for (int Round = 0; Round < 2; Round++)
    {
        bool AsLinux = Round == 1;

        SimDtPcie_RegisterTxBufferAsLinux(AsLinux);
        DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX,
                                                     &Buf, AsLinux),
                     DTAPI_E_INVALID_ARG);
        DT_ASSERT_OK(DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX,
                                                     &Buf, !AsLinux));
        DtIoctlCDmaCCmdAllocateBufferInput In;
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_ALLOCATE_BUFFER,
                          sizeof(In), &In));
        DT_ASSERT_EQ(In.m_Direction, DT_CDMAC_DIR_TX);
        DT_ASSERT_EQ(In.m_BufferSize, BUFFER_SIZE);
        DT_ASSERT_EQ(In.m_BufferAddr, AsLinux ? (uint64_t)(uintptr_t)Buf.Data : 0);
        SimDtPcie_GetTxState(PORT, &State);
        DT_ASSERT(State.BufferRegistered && State.BufferSize == BUFFER_SIZE);

        DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX,
                                                     &Buf, !AsLinux),
                     DTAPI_E_IN_USE);
        DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac));
        SimDtPcie_GetTxState(PORT, &State);
        DT_ASSERT(!State.BufferRegistered);
    }

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// A buffer must start on a page, be a whole number of prefetch units, and be at most
// SIM_TX_MAX_BUFFER; one registered for receiving takes no transmit write offset.
DT_TEST(BufferRules)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    OsDmaBuffer Buf;
    DT_ASSERT(OsDmaBuffer_Alloc(BUFFER_SIZE, &Buf) == 0);
    SimDtPcie_RegisterTxBufferAsLinux(true);

    OsDmaBuffer Fake = Buf;
    Fake.Data = Buf.Data + 16;
    DT_ASSERT_EQ(
        DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Fake, false),
        DTAPI_E_INVALID_ARG);
    Fake = Buf;
    Fake.Size = BUFFER_SIZE - 4096;
    DT_ASSERT_EQ(
        DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Fake, false),
        DTAPI_E_INVALID_ARG);
    Fake.Size = 512u * 1024 * 1024;
    DT_ASSERT_EQ(
        DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Fake, false),
        DTAPI_E_BUF_TOO_LARGE);

    DT_ASSERT_OK(
        DtPcieCmd_CdmacAllocateBufferAs(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_RX, &Buf, false));
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, 32),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac));

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Modes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The controller runs only with a buffer, runs once, and is idle to flush, change its
// test mode or let go of the buffer; going idle returns the read offset to 0.
DT_TEST(ModesOfTheDmaController)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_STANDBY),
                 DTAPI_E_NOT_INITIALIZED);
    OsDmaBuffer Buf;
    DT_ASSERT(OsDmaBuffer_Alloc(BUFFER_SIZE, &Buf) == 0);
    DT_ASSERT_OK(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf));

    DT_ASSERT_OK(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_RUN),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtPcieCmd_CdmacFlushChannel(Fix.Drv, P.Cdmac), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtPcieCmd_CdmacSetTestMode(Fix.Drv, P.Cdmac, DT_CDMAC_TESTMODE_NORMAL),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac), DTAPI_E_INVALID_MODE);

    DT_ASSERT_OK(DtPcieCmd_BurstFifoSetOpMode(Fix.Drv, P.Burst, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, 4096));
    SimTxState State;
    SimDtPcie_GetTxState(PORT, &State);
    DT_ASSERT(State.ReadOffset == 4096 && State.PipelineLoad == 4096);

    DT_ASSERT_OK(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_IDLE));
    SimDtPcie_GetTxState(PORT, &State);
    DT_ASSERT(State.ReadOffset == 0 && State.PipelineLoad == 0 &&
              State.CdmacMode == DT_BLOCK_OPMODE_IDLE);
    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac));

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// The blocks refuse what changes them without exclusive access, the encoder excepted.
// On a port that is not an output the transmitter's blocks refuse their commands, the
// stream alignment included, while the DMA controller's go on and its buffer stays.
DT_TEST(BlocksCheckAccessAndPort)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);

    DT_ASSERT_EQ(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_IDLE),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_OK(DtPcieCmd_SdiTxPSetGenerationMode(Fix.Drv, P.Txp, true, true, true));
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_IDLE));
    OsDmaBuffer Buf;
    DT_ASSERT(OsDmaBuffer_Alloc(BUFFER_SIZE, &Buf) == 0);
    DT_ASSERT_OK(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf));

    DtIoConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Port = PORT + 1;
    Config.Group = DTAPI_IOCONFIG_IODIR;
    Config.Value = DTAPI_IOCONFIG_INPUT;
    Config.SubValue = DTAPI_IOCONFIG_INPUT;
    Config.ParXtra[0] = Config.ParXtra[1] = -1;
    DT_ASSERT_OK(DtPcieCmd_SetIoConfig(Fix.Drv, &Config));

    DT_ASSERT_OK(DtPcieCmd_CdmacSetOpMode(Fix.Drv, P.Cdmac, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT_EQ(DtPcieCmd_SdiTxPSetOpMode(Fix.Drv, P.Txp, DT_BLOCK_OPMODE_IDLE),
                 DTAPI_E_INVALID_MODE);
    DtCdmacProps Props;
    DT_ASSERT_OK(DtPcieCmd_CdmacGetProps(Fix.Drv, P.Cdmac, &Props));
    int Alignment;
    DT_ASSERT_EQ(DtPcieCmd_SdiTxFGetStreamAlignment(Fix.Drv, P.Txf, &Alignment),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_IN_USE);

    Config.Value = Config.SubValue = DTAPI_IOCONFIG_OUTPUT;
    DT_ASSERT_OK(DtPcieCmd_SetIoConfig(Fix.Drv, &Config));
    DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac));
    OsDmaBuffer_Free(&Buf);

    FINISH(Fix);
}

// A wait needs a time-out from -1 to 1000 ms and a running formatter.
DT_TEST(WaitRules)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);
    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    DtSdiTxFEvent Event;
    DT_ASSERT_EQ(DtPcieCmd_SdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, 1001, &Event),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_SdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, -2, &Event),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtPcieCmd_SdiTxFSetOpMode(Fix.Drv, P.Txf, DT_BLOCK_OPMODE_STANDBY),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtPcieCmd_SdiTxFSetOpMode(Fix.Drv, P.Txf, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);

    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Output +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// In standby the card fills its pipeline from the buffer and sends nothing.
DT_TEST(StandbyFillsThePipeline)
{
    Fixture Fix;
    Objects P;
    OsDmaBuffer Buf;
    uint32_t Offset = 0;
    uint32_t Read = 0;
    bool Underflow = true;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;

    PutFrame(&Buf, &Offset, 0, 0);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, Offset));
    DT_ASSERT_OK(DtPcieCmd_CdmacGetTxReadOffset(Fix.Drv, P.Cdmac, &Read));
    DT_ASSERT_EQ(Read, SIM_TX_BURST_FIFO_SIZE + 16384);
    DtBurstFifoStatus Burst;
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetStatus(Fix.Drv, P.Burst, &Burst));
    DT_ASSERT_EQ(Burst.CurLoad, SIM_TX_BURST_FIFO_SIZE);

    DtSdiTxFEvent Event;
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, &Underflow));
    DT_ASSERT(!Underflow);
    SimTxState State;
    SimDtPcie_GetTxState(PORT, &State);
    DT_ASSERT(State.FramesSent == 0 &&
              State.PipelineLoad == SIM_TX_BURST_FIFO_SIZE + 16384);

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// In RUN each wait sends a part of a frame, the lines of one event; the sink receives the
// frames whole. The last 16 bytes of a frame whose size is not a whole number of 32-byte
// words leave the buffer only with the next data, so that frame completes after more is
// written.
DT_TEST(FramesReachTheSink)
{
    Fixture Fix;
    Objects P;
    OsDmaBuffer Buf;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;

    int f;
    for (f = 0; f < 3; f++)
        PutFrame(&Buf, &Offset, (uint32_t)(100 + f), f);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, Offset));
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, DT_FUNC_OPMODE_RUN));

    DtSdiTxFEvent Event;
    int e;
    for (f = 0; f < 2; f++)
    {
        for (e = 0; e < 4; e++)
        {
            DT_ASSERT_OK(Wait(&Fix, &P, &Event));
            DT_ASSERT(Event.FrameId == f && Event.SeqNumber == e && !Event.Underflow);
            DT_ASSERT_EQ(Event.SofTimeValid, e == 0);
        }
    }
    for (e = 0; e < 3; e++)
        DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    SimTxState State;
    SimDtPcie_GetTxState(PORT, &State);
    DT_ASSERT(State.FramesSent == 2 && State.HeaderErrors == 0);
    DT_ASSERT_EQ((State.WriteOffset + BUFFER_SIZE - State.ReadOffset) % BUFFER_SIZE, 16);

    PutFrame(&Buf, &Offset, 103, 3);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, Offset));
    DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT(Event.FrameId == 2 && Event.SeqNumber == 3 && Event.Underflow);

    DT_ASSERT_EQ(SimDtPcie_TxFrameCount(PORT), 3);
    for (f = 0; f < 3; f++)
    {
        if (!Received(f, (uint32_t)(100 + f), f))
            DT_FAIL("frame %d did not arrive as written", f);
    }

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// On the clock a frame goes out in parts of the coded lines of one event. 2160p50 over
// one link has 2250 coded lines, twice its raw ones, so with the event setting a channel
// gives it, 564 lines an event, it takes four parts, as 1080i50 does.
DT_TEST(PartsCountCodedLines)
{
    DT_ASSERT_EQ(SimSdiTx_NumPartsPerFrame(DTAPI_VIDSTD_2160P50, (2250 + 3) / 4 + 1), 4);
    DT_ASSERT_EQ(SimSdiTx_NumPartsPerFrame(DTAPI_VIDSTD_2160P50, 0), 4);
    DT_ASSERT_EQ(SimSdiTx_NumPartsPerFrame(DTAPI_VIDSTD_1080I50, (1125 + 3) / 4 + 1), 4);
    DT_ASSERT_EQ(SimSdiTx_NumPartsPerFrame(DTAPI_VIDSTD_1080I50, 1125), 1);
    DT_ASSERT_EQ(SimSdiTx_NumPartsPerFrame(DTAPI_VIDSTD_UNKNOWN, 0), 0);
}

// An underflow sends nothing and sets the PHY's flag and the burst FIFO's count; the
// formatter reports it with the first event after it, and frames flow again once written,
// without a restart. The PHY's flag stays set until cleared, or until the PHY goes idle.
DT_TEST(UnderflowAndRecovery)
{
    Fixture Fix;
    Objects P;
    OsDmaBuffer Buf;
    uint32_t Offset = 0;
    uint32_t Count = 0;
    uint32_t CountAfter = 0;
    bool Underflow = false;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, DT_FUNC_OPMODE_RUN));

    // The first event enables the formatter's flag.
    PutFrame(&Buf, &Offset, 0, 0);
    PutFrame(&Buf, &Offset, 1, 1);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, Offset));
    DtSdiTxFEvent Event;
    for (int e = 0; e < 4; e++)
        DT_ASSERT_OK(Wait(&Fix, &P, &Event));

    SimDtPcie_StarveTx(PORT, 1);
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetOvfUflCount(Fix.Drv, P.Burst, &Count));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtPcieCmd_BurstFifoGetOvfUflCount(Fix.Drv, P.Burst, &CountAfter));
    DT_ASSERT(CountAfter != Count);
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, &Underflow));
    DT_ASSERT(Underflow);

    DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT(Event.FrameId == 1 && Event.SeqNumber == 0 && Event.Underflow);
    DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT(!Event.Underflow);
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, &Underflow));
    DT_ASSERT(Underflow);
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyClearUnderflowFlag(Fix.Drv, P.Phy));
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, &Underflow));
    DT_ASSERT(!Underflow);

    SimDtPcie_StarveTx(PORT, 1);
    DT_ASSERT_EQ(SimDtPcie_RunTxEvents(PORT, 5), 0);
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, DT_FUNC_OPMODE_IDLE));
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, &Underflow));
    DT_ASSERT(!Underflow);

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// Nothing goes out while the switches do not bypass the demultiplexer or it runs; a
// position a switch does not have is refused.
DT_TEST(SwitchesMustBypassTheDemux)
{
    Fixture Fix;
    Objects P;
    OsDmaBuffer Buf;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;
    PutFrame(&Buf, &Offset, 0, 0);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, Offset));
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, DT_FUNC_OPMODE_RUN));

    DT_ASSERT_EQ(DtPcieCmd_SwitchSetPosition(Fix.Drv, P.SwitchIn, 1, 0),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_SwitchSetPosition(Fix.Drv, P.SwitchOut, 0, 1),
                 DTAPI_E_INVALID_ARG);

    DT_ASSERT_OK(DtPcieCmd_SwitchSetPosition(Fix.Drv, P.SwitchIn, 0, 1));
    DtSdiTxFEvent Event;
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtPcieCmd_SwitchSetPosition(Fix.Drv, P.SwitchIn, 0, 0));
    DT_ASSERT_OK(DtPcieCmd_SdiDmx12GSetOpMode(Fix.Drv, P.Dmx, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtPcieCmd_SdiDmx12GSetOpMode(Fix.Drv, P.Dmx, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT_OK(Wait(&Fix, &P, &Event));

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// A header that does not check is skipped one alignment word at a time.
DT_TEST(BadHeadersAreSkipped)
{
    Fixture Fix;
    Objects P;
    OsDmaBuffer Buf;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;
    uint8_t Garbage[32];
    memset(Garbage, 0xA5, sizeof(Garbage));
    Put(&Buf, &Offset, Garbage, sizeof(Garbage));
    PutFrame(&Buf, &Offset, 7, 7);
    PutFrame(&Buf, &Offset, 8, 8);
    DT_ASSERT_OK(DtPcieCmd_CdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, Offset));
    DT_ASSERT_OK(DtPcieCmd_SdiTxPhySetOpMode(Fix.Drv, P.Phy, DT_FUNC_OPMODE_RUN));

    DT_ASSERT_EQ(SimDtPcie_RunTxEvents(PORT, 4), 4);
    SimTxState State;
    SimDtPcie_GetTxState(PORT, &State);
    DT_ASSERT(State.FramesSent == 1 && State.HeaderErrors == 2);
    DT_ASSERT(Received(0, 7, 7));

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

// Closing the handle that registered the buffer stops the controller and lets go of the
// buffer. A refusal the emulator is told to give reaches the caller, and the command
// succeeds again once the refusal is withdrawn.
DT_TEST(ClosingTheHandleStopsTheDma)
{
    Fixture Fix;

    if (!Open(&Fix, DtFailures))
        return;
    Objects P = ObjectsOf(&Fix);
    OsDrv* Other = OsDrv_Open(SIM_DEVICE_INDEX);
    OsDmaBuffer Buf;
    DT_ASSERT(Other != NULL && OsDmaBuffer_Alloc(BUFFER_SIZE, &Buf) == 0);
    DT_ASSERT_OK(DtFunc_ExclAccess(Other, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtPcieCmd_CdmacAllocateBuffer(Other, P.Cdmac, DT_CDMAC_DIR_TX, &Buf));
    DT_ASSERT_OK(DtPcieCmd_CdmacSetOpMode(Other, P.Cdmac, DT_BLOCK_OPMODE_RUN));
    OsDrv_Close(Other);
    SimTxState State;
    SimDtPcie_GetTxState(PORT, &State);
    DT_ASSERT(!State.BufferRegistered && State.CdmacMode == DT_BLOCK_OPMODE_IDLE);

    DT_ASSERT_OK(DtFunc_ExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    SimDtPcie_FailTxCmd(DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER,
                        DT_STATUS_OUT_OF_MEMORY);
    DT_ASSERT_EQ(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_OUT_OF_MEM);
    SimDtPcie_FailTxCmd(DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER, 0);
    DT_ASSERT_OK(DtPcieCmd_CdmacAllocateBuffer(Fix.Drv, P.Cdmac, DT_CDMAC_DIR_TX, &Buf));
    DT_ASSERT_OK(DtPcieCmd_CdmacFreeBuffer(Fix.Drv, P.Cdmac));

    OsDmaBuffer_Free(&Buf);
    FINISH(Fix);
}

DT_TEST_MAIN("SimSdiTx", DT_RUN(OneHandleHoldsAnObject),
             DT_RUN(EveryObjectHasExclusiveAccess), DT_RUN(AcquiringAllRollsBack),
             DT_RUN(RequestsCarryTheirFields), DT_RUN(InvalidArgumentsSendNothing),
             DT_RUN(BufferIsRegisteredBothWays), DT_RUN(BufferRules),
             DT_RUN(ModesOfTheDmaController), DT_RUN(BlocksCheckAccessAndPort),
             DT_RUN(WaitRules), DT_RUN(StandbyFillsThePipeline),
             DT_RUN(FramesReachTheSink), DT_RUN(PartsCountCodedLines),
             DT_RUN(UnderflowAndRecovery), DT_RUN(SwitchesMustBypassTheDemux),
             DT_RUN(BadHeadersAreSkipped), DT_RUN(ClosingTheHandleStopsTheDma))

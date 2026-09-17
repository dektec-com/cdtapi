// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimSdiTx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Exclusive access and the transmit blocks against the emulated card
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // Results.
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtDrv.h"                  // Commands under test.
#include "DtDrvAbi.h"               // Types, commands and driver statuses.
#include "DtFunc.h"                 // Finding the parts.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/OsDmaBuffer.h"        // Buffers to register.
#include "OAL/Sim/SimChSdiRx.h"     // Lines of a test pattern.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "OAL/Sim/SimSdiTx.h"       // The emulated transmit blocks and their controls.
#include "Video/DtSdiFrame.h"       // The geometry of coded lines.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The port the tests use: port 2, an output by default.
#define PORT 1

typedef struct Fixture
{
    OsDrv* Drv;
    DtFuncInstance Tx;  // AF_ASISDITX
    DtFuncInstance Dma; // AF_DMA
    long Live;
} Fixture;

// Opens the emulated device in its power-on state and finds the port's transmitter and
// DMA. Returns false, having recorded a failure, when that is not possible.
static bool Open(Fixture* Fix, int* DtFailures)
{
    SimDtPcieReset();
    Fix->Live = DtAllocLive();
    Fix->Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    DtVecInit(&Fix->Tx.Parts, sizeof(DtFuncPart));
    DtVecInit(&Fix->Dma.Parts, sizeof(DtFuncPart));
    if (Fix->Drv == NULL || !OsDrvIsEmulated(Fix->Drv) ||
        DtFuncFind(Fix->Drv, PORT, "AF_ASISDITX", "", &Fix->Tx) != DTAPI_OK ||
        DtFuncFind(Fix->Drv, PORT, "AF_DMA", "", &Fix->Dma) != DTAPI_OK)
    {
        printf("    FAIL: no emulated transmitter; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        DtFuncRelease(&Fix->Tx);
        DtFuncRelease(&Fix->Dma);
        OsDrvClose(Fix->Drv);
        return false;
    }
    return true;
}

// Frees the parts, closes the device and checks that nothing is left open or allocated.
// The frames the sink kept are the emulator's until a reset, which comes first.
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtFuncRelease(&(Fix).Tx);                                                        \
        DtFuncRelease(&(Fix).Dma);                                                       \
        OsDrvClose((Fix).Drv);                                                           \
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);                                         \
        SimDtPcieReset();                                                                \
        DT_ASSERT_EQ(DtAllocLive(), (Fix).Live);                                         \
    } while (0)

// The UUID of the part of Instance at Index.
static int UuidAt(const DtFuncInstance* Instance, size_t Index)
{
    return DT_VEC_AT(&Instance->Parts, DtFuncPart, Index).Uuid;
}

// The UUID of the part of Instance with IsDf, Type and Role; 0 when there is none.
static int UuidOf(const DtFuncInstance* Instance, bool IsDf, int Type, const char* Role)
{
    const DtFuncPart* Part = DtFuncGet(Instance, IsDf, Type, Role);
    return Part != NULL ? Part->Uuid : 0;
}

// The UUIDs of the parts a transmit channel drives.
typedef struct Parts
{
    int Cdmac, Burst, Txf, SwitchIn, SwitchOut, Dmx, Txp, Phy;
} Parts;

static Parts PartsOf(const Fixture* Fix)
{
    Parts P;

    P.Cdmac = UuidOf(&Fix->Dma, false, DT_BLOCK_TYPE_CDMAC, "");
    P.Burst = UuidOf(&Fix->Dma, false, DT_BLOCK_TYPE_BURSTFIFO, "");
    P.Txf = UuidOf(&Fix->Tx, false, DT_BLOCK_TYPE_SDITXF, "");
    P.SwitchIn = UuidOf(&Fix->Tx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_IN");
    P.SwitchOut = UuidOf(&Fix->Tx, false, DT_BLOCK_TYPE_SWITCH, "SDI_DEMUX_OUT");
    P.Dmx = UuidOf(&Fix->Tx, false, DT_BLOCK_TYPE_SDIDMX12G, "");
    P.Txp = UuidOf(&Fix->Tx, false, DT_BLOCK_TYPE_SDITXP, "");
    P.Phy = UuidOf(&Fix->Tx, true, DT_FUNC_TYPE_SDITXPHY, "");
    return P;
}

// Whether the last command was Cmd of FunctionCode for Uuid on the test port, with an
// input of Size bytes, which are copied to Input when it is not NULL.
static bool LastWas(int FunctionCode, int Uuid, int Cmd, size_t Size, void* Input)
{
    uint8_t In[SIM_MAX_RECORDED_INPUT];
    DtIoctlInputDataHdr Hdr;
    int Code = -1;
    size_t Got = SimDtPcieLastInput(&Code, In, sizeof(In));

    memcpy(&Hdr, In, sizeof(Hdr));
    if (Input != NULL)
        memcpy(Input, In, Size);
    return Got == Size && Code == FunctionCode && Hdr.m_Uuid == Uuid &&
           Hdr.m_PortIndex == PORT && Hdr.m_Cmd == Cmd && Hdr.m_CmdEx == DT_IOCTL_CMD_NOP;
}

// The video standard of the frames the tests send, and its coded geometry.
#define VIDSTD DTAPI_VIDSTD_525I59_94

// A buffer of 8 MB, room for seven coded 525i frames.
#define BUFFER_SIZE (8 * 1024 * 1024)

// Acquires every part and brings the port to what a transmit channel in HOLD is: a
// registered buffer, every block running, the switches bypassing the demultiplexer and
// the PHY in standby. Returns false, having recorded a failure, when that fails.
static bool Hold(Fixture* Fix, Parts* P, OsDmaBuffer* Buf, int* DtFailures)
{
    DtSdiFrameLayout Layout;

    *P = PartsOf(Fix);
    DtSdiFrameLayoutInit(&Layout, VIDSTD, SIM_TX_STREAM_ALIGNMENT);
    if (OsDmaBufferAlloc(BUFFER_SIZE, Buf) != 0 ||
        DtFuncExclAccess(Fix->Drv, &Fix->Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) !=
            DTAPI_OK ||
        DtFuncExclAccess(Fix->Drv, &Fix->Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE) !=
            DTAPI_OK ||
        DtDrvCdmacAllocateBuffer(Fix->Drv, P->Cdmac, PORT, DT_CDMAC_DIR_TX, Buf) !=
            DTAPI_OK ||
        DtDrvSdiTxFSetFmtEventSetting(Fix->Drv, P->Txf, PORT,
                                      (Layout.NumLines + 3) / 4 + 1, 1) != DTAPI_OK ||
        DtDrvCdmacIssueChannelFlush(Fix->Drv, P->Cdmac, PORT) != DTAPI_OK ||
        DtDrvCdmacSetTxWriteOffset(Fix->Drv, P->Cdmac, PORT, 0) != DTAPI_OK ||
        DtDrvCdmacSetOpMode(Fix->Drv, P->Cdmac, PORT, DT_BLOCK_OPMODE_RUN) != DTAPI_OK ||
        DtDrvBurstFifoSetOpMode(Fix->Drv, P->Burst, PORT, DT_BLOCK_OPMODE_RUN) !=
            DTAPI_OK ||
        DtDrvSdiTxFSetOpMode(Fix->Drv, P->Txf, PORT, DT_BLOCK_OPMODE_RUN) != DTAPI_OK ||
        DtDrvSwitchSetPosition(Fix->Drv, P->SwitchIn, PORT, 0, 0) != DTAPI_OK ||
        DtDrvSwitchSetPosition(Fix->Drv, P->SwitchOut, PORT, 0, 0) != DTAPI_OK ||
        DtDrvSwitchSetOpMode(Fix->Drv, P->SwitchIn, PORT, DT_BLOCK_OPMODE_RUN) !=
            DTAPI_OK ||
        DtDrvSdiDmx12GSetOpMode(Fix->Drv, P->Dmx, PORT, DT_BLOCK_OPMODE_IDLE) !=
            DTAPI_OK ||
        DtDrvSwitchSetOpMode(Fix->Drv, P->SwitchOut, PORT, DT_BLOCK_OPMODE_RUN) !=
            DTAPI_OK ||
        DtDrvSdiTxPSetOpMode(Fix->Drv, P->Txp, PORT, DT_BLOCK_OPMODE_RUN) != DTAPI_OK ||
        DtDrvSdiTxPhySetOpMode(Fix->Drv, P->Phy, PORT, DT_FUNC_OPMODE_STANDBY) !=
            DTAPI_OK)
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
    int Have = 0, i, At = 0;

    memset(Out, 0, (size_t)Bytes);
    for (i = 0; i < Count; i++)
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
    size_t i;

    for (i = 0; i < Size; i++)
        Buf->Data[(*Offset + i) % Buf->Size] = Data[i];
    *Offset = (uint32_t)((*Offset + Size) % Buf->Size);
}

// Writes the header of a frame with FrameId and the lines of frame FrameNumber of the
// test pattern at *Offset.
static void PutFrame(OsDmaBuffer* Buf, uint32_t* Offset, uint32_t FrameNumber,
                     int FrameId)
{
    DtSdiFrameLayout L;
    uint32_t Words[5];
    uint8_t Header[32];
    uint16_t Symbols[2000];
    uint8_t Line[4096];
    int n, i;

    DtSdiFrameLayoutInit(&L, VIDSTD, SIM_TX_STREAM_ALIGNMENT);
    Words[0] = 0xFFEFFBFEu;
    Words[1] = 1u << 8 | (uint32_t)DT_DRV_SDIRATE_SD << 9;
    Words[2] = (uint32_t)FrameId | (uint32_t)L.NumLines << 16;
    Words[3] = (uint32_t)(L.LineBytesHanc / L.Alignment) | (uint32_t)L.LineSymsHanc << 16;
    Words[4] = (uint32_t)(L.LineBytesVideo / L.Alignment) | (uint32_t)L.LineSymsVideo
                                                                << 16;
    memset(Header, 0, sizeof(Header));
    for (i = 0; i < 5; i++)
        memcpy(Header + 4 * i, &Words[i], 4);
    Put(Buf, Offset, Header, sizeof(Header));

    for (n = 1; n <= L.NumLines; n++)
    {
        SimChSdiRxLine(VIDSTD, FrameNumber, n, Symbols);
        Pack(Symbols, L.LineSymsHanc, Line, L.LineBytesHanc);
        Pack(Symbols + L.LineSymsHanc, L.LineSymsVideo, Line + L.LineBytesHanc,
             L.LineBytesVideo);
        Put(Buf, Offset, Line, (size_t)L.Stride);
    }
}

// Whether the kept frame at Index holds frame FrameNumber of the test pattern with
// FrameId.
static bool Received(int Index, uint32_t FrameNumber, int FrameId)
{
    SimTxFrame Frame;
    uint16_t Symbols[2000];
    int n, Width;

    if (!SimDtPcieGetTxFrame(PORT, Index, &Frame) || Frame.FrameId != FrameId)
        return false;
    Width = Frame.SymsHanc + Frame.SymsVideo;
    for (n = 1; n <= Frame.NumLines; n++)
    {
        if (SimChSdiRxLine(VIDSTD, FrameNumber, n, Symbols) != Width ||
            memcmp(Symbols, Frame.Symbols + (size_t)(n - 1) * (size_t)Width,
                   (size_t)Width * sizeof(uint16_t)) != 0)
        {
            return false;
        }
    }
    return true;
}

// Waits for an event without a time-out.
static unsigned int Wait(const Fixture* Fix, const Parts* P, DtSdiTxFEvent* Event)
{
    return DtDrvSdiTxFWaitForFmtEvent(Fix->Drv, P->Txf, PORT, 0, Event);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exclusive access +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// One handle holds a part at a time; checking, probing and releasing answer as the
// driver's building blocks do.
DT_TEST(OneHandleHoldsAPart)
{
    Fixture Fix;
    OsDrv* Other;
    int Uuid;

    if (!Open(&Fix, DtFailures))
        return;
    Other = OsDrvOpen(SIM_DEVICE_INDEX);
    Uuid = UuidOf(&Fix.Dma, false, DT_BLOCK_TYPE_CDMAC, "");
    DT_ASSERT(Other != NULL && Uuid != 0);

    DT_ASSERT_EQ(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_CHECK),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_PROBE));
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));

    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    DT_ASSERT_EQ(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_PROBE),
                 DTAPI_E_IN_USE);

    DT_ASSERT_EQ(DtDrvExclAccess(Other, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtDrvExclAccess(Other, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_CHECK),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtDrvExclAccess(Other, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_RELEASE),
                 DTAPI_E_IN_USE);

    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    DT_ASSERT_OK(DtDrvExclAccess(Other, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    // Closing a handle lets go of what it holds.
    OsDrvClose(Other);
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, Uuid, PORT, DT_EXCLUSIVE_ACCESS_CMD_PROBE));

    FINISH(Fix);
}

// Every part of both functions, driver function included, has exclusive access of its
// own; a UUID the card does not have has no I/O stub, and a command the access has not
// is not supported.
DT_TEST(EveryPartHasExclusiveAccess)
{
    Fixture Fix;
    size_t i;

    if (!Open(&Fix, DtFailures))
        return;

    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    for (i = 0; i < DtVecCount(&Fix.Tx.Parts); i++)
    {
        DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Tx, i), PORT,
                                     DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    }
    for (i = 0; i < DtVecCount(&Fix.Dma.Parts); i++)
    {
        DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Dma, i), PORT,
                                     DT_EXCLUSIVE_ACCESS_CMD_CHECK));
    }
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    DT_ASSERT_EQ(
        DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Tx, 0), PORT, DT_EXCLUSIVE_ACCESS_CMD_CHECK),
        DTAPI_E_EXCL_ACCESS_REQD);

    DT_ASSERT_EQ(DtDrvExclAccess(Fix.Drv, DT_UUID_BC_FLAG | 0xFFFF, PORT,
                                 DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_NOT_IMPLEMENTED);
    DT_ASSERT_EQ(DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Tx, 0), PORT, 99),
                 DTAPI_E_NOT_SUPPORTED);

    FINISH(Fix);
}

// Acquiring every part stops at the first part another handle holds, and lets go of the
// parts before it; a part without exclusive access is passed over.
DT_TEST(AcquiringAllRollsBack)
{
    Fixture Fix;
    OsDrv* Other;
    size_t i;

    if (!Open(&Fix, DtFailures))
        return;
    Other = OsDrvOpen(SIM_DEVICE_INDEX);
    DT_ASSERT(Other != NULL && DtVecCount(&Fix.Tx.Parts) == 7);

    DT_ASSERT_OK(DtDrvExclAccess(Other, UuidAt(&Fix.Tx, 3), PORT,
                                 DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtFuncExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE),
                 DTAPI_E_IN_USE);
    for (i = 0; i < DtVecCount(&Fix.Tx.Parts); i++)
    {
        unsigned int Probe = DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Tx, i), PORT,
                                             DT_EXCLUSIVE_ACCESS_CMD_PROBE);

        if (Probe != (i == 3 ? (unsigned int)DTAPI_E_IN_USE : (unsigned int)DTAPI_OK))
            DT_FAIL("part %zu: %s", i, DtapiResult2Str(Probe));
    }

    // Releasing all goes on past the part another handle holds, and reports it.
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Tx, 6), PORT,
                                 DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtFuncExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_RELEASE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, UuidAt(&Fix.Tx, 6), PORT,
                                 DT_EXCLUSIVE_ACCESS_CMD_PROBE));
    OsDrvClose(Other);

    SimDtPcieFailWithStatus(DT_FUNC_CODE_EXCL_ACCESS_CMD, DT_STATUS_NOT_SUPPORTED);
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Requests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Every command goes to its block with the driver's command number and structure.
DT_TEST(RequestsCarryTheirFields)
{
    Fixture Fix;
    Parts P;
    DtCdmacProps CdmacProps;
    DtBurstFifoProps BurstProps;
    DtBurstFifoStatus BurstStatus;
    DtSdiTxFEvent Event;
    uint32_t Offset;
    int Value, MinMax;
    bool Flag;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);

    DT_ASSERT_OK(
        DtDrvExclAccess(Fix.Drv, P.Cdmac, PORT, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT(LastWas(DT_FUNC_CODE_EXCL_ACCESS_CMD, P.Cdmac,
                      DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE, sizeof(DtIoctlExclAccessCmdInput),
                      NULL));

    DT_ASSERT_OK(DtDrvCdmacGetProps(Fix.Drv, P.Cdmac, PORT, &CdmacProps));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_GET_PROPERTIES,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_EQ(CdmacProps.Caps, DT_CDMAC_CAP_RX | DT_CDMAC_CAP_TX);
    DT_ASSERT_EQ(CdmacProps.PrefetchSize, SIM_TX_PREFETCH_PAGES);
    DT_ASSERT_EQ(CdmacProps.PcieDataWidth, SIM_TX_PCIE_DATA_WIDTH);
    DT_ASSERT_EQ(CdmacProps.ReorderBufSize, SIM_TX_REORDER_BUF_SIZE);

    DT_ASSERT_OK(DtDrvCdmacIssueChannelFlush(Fix.Drv, P.Cdmac, PORT));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_ISSUE_CHANNEL_FLUSH,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_OK(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_FREE_BUFFER,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_OK(DtDrvCdmacClearReorderBufMinMax(Fix.Drv, P.Cdmac, PORT));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                      DT_CDMAC_CMD_CLEAR_REORDER_BUF_MIN_MAX, sizeof(DtIoctlInputDataHdr),
                      NULL));
    {
        DtIoctlCDmaCCmdSetOpModeInput In;

        DT_ASSERT_EQ(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_RUN),
                     DTAPI_E_NOT_INITIALIZED);
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                          DT_CDMAC_CMD_SET_OPERATIONAL_MODE, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_OpMode, DT_BLOCK_OPMODE_RUN);
    }
    {
        DtIoctlCDmaCCmdSetTestModeInput In;

        DT_ASSERT_OK(
            DtDrvCdmacSetTestMode(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_TESTMODE_TEST_EXT));
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_SET_TEST_MODE,
                          sizeof(In), &In));
        DT_ASSERT_EQ(In.m_TestMode, DT_CDMAC_TESTMODE_TEST_EXT);
    }
    {
        DtIoctlCDmaCCmdSetTxWrOffsetInput In;

        DT_ASSERT_EQ(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, 0x12340),
                     DTAPI_E_INVALID_ARG);
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                          DT_CDMAC_CMD_SET_TX_WRITE_OFFSET, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_TxWriteOffset, 0x12340);
    }
    DT_ASSERT_OK(DtDrvCdmacGetTxReadOffset(Fix.Drv, P.Cdmac, PORT, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_GET_TX_READ_OFFSET,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_OK(DtDrvCdmacGetReorderBufStatus(Fix.Drv, P.Cdmac, PORT, &Value, &MinMax));
    DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac,
                      DT_CDMAC_CMD_GET_REORDER_BUF_STATUS, sizeof(DtIoctlInputDataHdr),
                      NULL));

    DT_ASSERT_OK(DtDrvBurstFifoGetProps(Fix.Drv, P.Burst, PORT, &BurstProps));
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_GET_PROPERTIES, sizeof(DtIoctlInputDataHdr),
                      NULL));
    DT_ASSERT_EQ(BurstProps.FifoSize, SIM_TX_BURST_FIFO_SIZE);
    DT_ASSERT_EQ(BurstProps.DataWidth, SIM_TX_PCIE_DATA_WIDTH);
    DT_ASSERT_OK(DtDrvBurstFifoGetStatus(Fix.Drv, P.Burst, PORT, &BurstStatus));
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_GET_FIFO_STATUS, sizeof(DtIoctlInputDataHdr),
                      NULL));
    DT_ASSERT_OK(DtDrvBurstFifoGetOvfUflCount(Fix.Drv, P.Burst, PORT, &Offset));
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_GET_OVFL_UFL_COUNT, sizeof(DtIoctlInputDataHdr),
                      NULL));
    {
        DtIoctlBurstFifoCmdClearFifoMaxInput In;

        DT_ASSERT_OK(DtDrvBurstFifoClearMax(Fix.Drv, P.Burst, PORT, false, true));
        DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                          DT_BURSTFIFO_CMD_CLEAR_FIFO_MAX, sizeof(In), &In));
        DT_ASSERT(In.m_ClearMaxFree == 0 && In.m_ClearMaxLoad == 1);
    }
    DT_ASSERT_EQ(DtDrvBurstFifoSetOpMode(Fix.Drv, P.Burst, PORT, DT_BLOCK_OPMODE_STANDBY),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_BURSTFIFO_CMD, P.Burst,
                      DT_BURSTFIFO_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlBurstFifoCmdSetOpModeInput), NULL));

    DT_ASSERT_OK(DtDrvSdiTxFGetStreamAlignment(Fix.Drv, P.Txf, PORT, &Value));
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf, DT_SDITXF_CMD_GET_STREAM_ALIGNMENT,
                      sizeof(DtIoctlInputDataHdr), NULL));
    DT_ASSERT_EQ(Value, SIM_TX_STREAM_ALIGNMENT);
    {
        DtIoctlSdiTxFCmdSetFmtEventSettingInput In;

        DT_ASSERT_EQ(DtDrvSdiTxFSetFmtEventSetting(Fix.Drv, P.Txf, PORT, 283, 7),
                     DTAPI_E_EXCL_ACCESS_REQD);
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf,
                          DT_SDITXF_CMD_SET_FMT_EVENT_SETTING, sizeof(In), &In));
        DT_ASSERT(In.m_NumLinesPerEvent == 283 && In.m_NumSofsBetweenTod == 7);
    }
    {
        DtIoctlSdiTxFCmdWaitForFmtEventInput In;

        DT_ASSERT_EQ(DtDrvSdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, PORT, 40, &Event),
                     DTAPI_E_EXCL_ACCESS_REQD);
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf,
                          DT_SDITXF_CMD_WAIT_FOR_FMT_EVENT, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_Timeout, 40);
    }
    DT_ASSERT_EQ(DtDrvSdiTxFSetOpMode(Fix.Drv, P.Txf, PORT, DT_BLOCK_OPMODE_RUN),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXF_CMD, P.Txf, DT_SDITXF_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiTxFCmdSetOpModeInput), NULL));

    {
        DtIoctlSwitchCmdSetPositionInput In;

        DT_ASSERT_EQ(DtDrvSwitchSetPosition(Fix.Drv, P.SwitchIn, PORT, 0, 1),
                     DTAPI_E_EXCL_ACCESS_REQD);
        DT_ASSERT(LastWas(DT_FUNC_CODE_SWITCH_CMD, P.SwitchIn, DT_SWITCH_CMD_SET_POSITION,
                          sizeof(In), &In));
        DT_ASSERT(In.m_InputIndex == 0 && In.m_OutputIndex == 1);
    }
    DT_ASSERT_EQ(DtDrvSwitchSetOpMode(Fix.Drv, P.SwitchOut, PORT, DT_BLOCK_OPMODE_RUN),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SWITCH_CMD, P.SwitchOut,
                      DT_SWITCH_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSwitchCmdSetOpModeInput), NULL));
    DT_ASSERT_EQ(DtDrvSdiDmx12GSetOpMode(Fix.Drv, P.Dmx, PORT, DT_BLOCK_OPMODE_IDLE),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDIDMX12G_CMD, P.Dmx,
                      DT_SDIDMX12G_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiDmx12GCmdSetOpModeInput), NULL));

    // The encoder's commands need no exclusive access.
    DT_ASSERT_OK(DtDrvSdiTxPSetOpMode(Fix.Drv, P.Txp, PORT, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXP_CMD, P.Txp, DT_SDITXP_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiTxPCmdSetOpModeInput), NULL));
    {
        DtIoctlSdiTxPCmdSetGenModeInput In;
        SimTxState State;

        DT_ASSERT_OK(
            DtDrvSdiTxPSetGenerationMode(Fix.Drv, P.Txp, PORT, true, false, true));
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXP_CMD, P.Txp,
                          DT_SDITXP_CMD_SET_GENERATION_MODE, sizeof(In), &In));
        DT_ASSERT(In.m_ClampEnable == 1 && In.m_AdpChecksumEnable == 0 &&
                  In.m_LineCrcEnable == 1);
        SimDtPcieGetTxState(PORT, &State);
        DT_ASSERT(State.Clamp && !State.AncChecksum && State.LineCrc);
    }

    DT_ASSERT_EQ(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, DT_FUNC_OPMODE_STANDBY),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                      DT_SDITXPHY_CMD_SET_OPERATIONAL_MODE,
                      sizeof(DtIoctlSdiTxPhyCmdSetOpModeInput), NULL));
    DT_ASSERT_OK(DtDrvSdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, PORT, &Flag));
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                      DT_SDITXPHY_CMD_GET_UNDERFLOW_FLAG, sizeof(DtIoctlInputDataHdr),
                      NULL));
    DT_ASSERT_EQ(DtDrvSdiTxPhyClearUnderflowFlag(Fix.Drv, P.Phy, PORT),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                      DT_SDITXPHY_CMD_CLEAR_UNDERFLOW_FLAG, sizeof(DtIoctlInputDataHdr),
                      NULL));
    {
        DtIoctlSdiTxPhyCmdSetStartOfFrameOffsetInput In;

        DT_ASSERT_OK(DtDrvSdiTxPhySetStartOfFrameOffset(Fix.Drv, P.Phy, PORT, 1500));
        DT_ASSERT(LastWas(DT_FUNC_CODE_SDITXPHY_CMD, P.Phy,
                          DT_SDITXPHY_CMD_SET_START_OF_FRAME_OFFSET, sizeof(In), &In));
        DT_ASSERT_EQ(In.m_StartOfFrameOffsetNs, 1500);
    }

    FINISH(Fix);
}

// Arguments the proxies would refuse send nothing.
DT_TEST(InvalidArgumentsSendNothing)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    DtSdiTxFEvent Event;
    int Code;
    uint8_t In[64];

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);
    DT_ASSERT_OK(DtDrvExclAccess(Fix.Drv, P.Cdmac, PORT, DT_EXCLUSIVE_ACCESS_CMD_PROBE));

    DT_ASSERT_EQ(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvCdmacSetTestMode(Fix.Drv, P.Cdmac, PORT, 3), DTAPI_E_INVALID_ARG);
    memset(&Buf, 0, sizeof(Buf));
    DT_ASSERT_EQ(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_INVALID_ARG);
    Buf.Data = (uint8_t*)&Event;
    Buf.Size = 4096;
    DT_ASSERT_EQ(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, 2, &Buf),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, PORT, 0, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvCdmacGetProps(NULL, P.Cdmac, PORT, NULL), DTAPI_E_INVALID_ARG);

    SimDtPcieLastInput(&Code, In, sizeof(In));
    DT_ASSERT_EQ(Code, DT_FUNC_CODE_EXCL_ACCESS_CMD);

    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Buffer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The buffer travels as the output, as on Windows, or as the address in the input, as on
// Linux, and the controller takes it only the way its driver does.
DT_TEST(BufferIsRegisteredBothWays)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    SimTxState State;
    int Round;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT(OsDmaBufferAlloc(BUFFER_SIZE, &Buf) == 0);

    for (Round = 0; Round < 2; Round++)
    {
        bool AsLinux = Round == 1;
        DtIoctlCDmaCCmdAllocateBufferInput In;

        SimDtPcieRegisterTxBufferAsLinux(AsLinux);
        DT_ASSERT_EQ(DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX,
                                                &Buf, AsLinux),
                     DTAPI_E_INVALID_ARG);
        DT_ASSERT_OK(DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX,
                                                &Buf, !AsLinux));
        DT_ASSERT(LastWas(DT_FUNC_CODE_CDMAC_CMD, P.Cdmac, DT_CDMAC_CMD_ALLOCATE_BUFFER,
                          sizeof(In), &In));
        DT_ASSERT_EQ(In.m_Direction, DT_CDMAC_DIR_TX);
        DT_ASSERT_EQ(In.m_BufferSize, BUFFER_SIZE);
        DT_ASSERT_EQ(In.m_BufferAddr, AsLinux ? (uint64_t)(uintptr_t)Buf.Data : 0);
        SimDtPcieGetTxState(PORT, &State);
        DT_ASSERT(State.BufferRegistered && State.BufferSize == BUFFER_SIZE);

        DT_ASSERT_EQ(DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX,
                                                &Buf, !AsLinux),
                     DTAPI_E_IN_USE);
        DT_ASSERT_OK(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT));
        SimDtPcieGetTxState(PORT, &State);
        DT_ASSERT(!State.BufferRegistered);
    }

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// A buffer must start on a page, be a whole number of prefetch units, and be at most
// 256 MB; one registered for receiving takes no transmit write offset.
DT_TEST(BufferRules)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf, Fake;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT(OsDmaBufferAlloc(BUFFER_SIZE, &Buf) == 0);
    SimDtPcieRegisterTxBufferAsLinux(true);

    Fake = Buf;
    Fake.Data = Buf.Data + 16;
    DT_ASSERT_EQ(
        DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Fake, false),
        DTAPI_E_INVALID_ARG);
    Fake = Buf;
    Fake.Size = BUFFER_SIZE - 4096;
    DT_ASSERT_EQ(
        DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Fake, false),
        DTAPI_E_INVALID_ARG);
    Fake.Size = 512u * 1024 * 1024;
    DT_ASSERT_EQ(
        DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Fake, false),
        DTAPI_E_BUF_TOO_LARGE);

    DT_ASSERT_OK(
        DtDrvCdmacAllocateBufferAs(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_RX, &Buf, false));
    DT_ASSERT_EQ(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, 32),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_OK(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT));

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Modes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The controller runs only with a buffer, runs once, and is idle to flush, change its
// test mode or let go of the buffer; going idle returns the read offset to 0.
DT_TEST(ModesOfTheDmaController)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    SimTxState State;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_EQ(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_STANDBY),
                 DTAPI_E_NOT_INITIALIZED);
    DT_ASSERT(OsDmaBufferAlloc(BUFFER_SIZE, &Buf) == 0);
    DT_ASSERT_OK(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf));

    DT_ASSERT_OK(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_EQ(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_RUN),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtDrvCdmacIssueChannelFlush(Fix.Drv, P.Cdmac, PORT),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtDrvCdmacSetTestMode(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_TESTMODE_NORMAL),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT), DTAPI_E_INVALID_MODE);

    DT_ASSERT_OK(DtDrvBurstFifoSetOpMode(Fix.Drv, P.Burst, PORT, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, 4096));
    SimDtPcieGetTxState(PORT, &State);
    DT_ASSERT(State.ReadOffset == 4096 && State.PipelineLoad == 4096);

    DT_ASSERT_OK(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_IDLE));
    SimDtPcieGetTxState(PORT, &State);
    DT_ASSERT(State.ReadOffset == 0 && State.PipelineLoad == 0 &&
              State.CdmacMode == DT_BLOCK_OPMODE_IDLE);
    DT_ASSERT_OK(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT));

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// The blocks refuse what changes them without exclusive access, the encoder excepted.
// On a port that is no output the transmitter's blocks refuse their commands, the
// stream alignment included, while the DMA controller's go on and its buffer stays.
DT_TEST(BlocksCheckAccessAndPort)
{
    Fixture Fix;
    Parts P;
    DtCdmacProps Props;
    DtIoConfig Config;
    OsDmaBuffer Buf;
    int Alignment;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);

    DT_ASSERT_EQ(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_IDLE),
                 DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_OK(DtDrvSdiTxPSetGenerationMode(Fix.Drv, P.Txp, PORT, true, true, true));
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT(OsDmaBufferAlloc(BUFFER_SIZE, &Buf) == 0);
    DT_ASSERT_OK(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf));

    memset(&Config, 0, sizeof(Config));
    Config.Port = PORT + 1;
    Config.Group = DTAPI_IOCONFIG_IODIR;
    Config.Value = DTAPI_IOCONFIG_INPUT;
    Config.SubValue = DTAPI_IOCONFIG_INPUT;
    Config.ParXtra[0] = Config.ParXtra[1] = -1;
    DT_ASSERT_OK(DtDrvSetIoConfig(Fix.Drv, &Config));

    DT_ASSERT_OK(DtDrvCdmacSetOpMode(Fix.Drv, P.Cdmac, PORT, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT_EQ(DtDrvSdiTxPSetOpMode(Fix.Drv, P.Txp, PORT, DT_BLOCK_OPMODE_IDLE),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_OK(DtDrvCdmacGetProps(Fix.Drv, P.Cdmac, PORT, &Props));
    DT_ASSERT_EQ(DtDrvSdiTxFGetStreamAlignment(Fix.Drv, P.Txf, PORT, &Alignment),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_IN_USE);

    Config.Value = Config.SubValue = DTAPI_IOCONFIG_OUTPUT;
    DT_ASSERT_OK(DtDrvSetIoConfig(Fix.Drv, &Config));
    DT_ASSERT_EQ(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT));
    OsDmaBufferFree(&Buf);

    FINISH(Fix);
}

// A wait needs a time-out from -1 to 1000 ms and a running formatter.
DT_TEST(WaitRules)
{
    Fixture Fix;
    Parts P;
    DtSdiTxFEvent Event;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);
    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Tx, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    DT_ASSERT_EQ(DtDrvSdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, PORT, 1001, &Event),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSdiTxFWaitForFmtEvent(Fix.Drv, P.Txf, PORT, -2, &Event),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtDrvSdiTxFSetOpMode(Fix.Drv, P.Txf, PORT, DT_BLOCK_OPMODE_STANDBY),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtDrvSdiTxFSetOpMode(Fix.Drv, P.Txf, PORT, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);

    FINISH(Fix);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Output +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// In standby the card fills its pipeline from the buffer and sends nothing.
DT_TEST(StandbyFillsThePipeline)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    DtSdiTxFEvent Event;
    DtBurstFifoStatus Burst;
    SimTxState State;
    uint32_t Offset = 0, Read = 0;
    bool Underflow = true;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;

    PutFrame(&Buf, &Offset, 0, 0);
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, Offset));
    DT_ASSERT_OK(DtDrvCdmacGetTxReadOffset(Fix.Drv, P.Cdmac, PORT, &Read));
    DT_ASSERT_EQ(Read, SIM_TX_BURST_FIFO_SIZE + 16384);
    DT_ASSERT_OK(DtDrvBurstFifoGetStatus(Fix.Drv, P.Burst, PORT, &Burst));
    DT_ASSERT_EQ(Burst.CurLoad, SIM_TX_BURST_FIFO_SIZE);

    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtDrvSdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, PORT, &Underflow));
    DT_ASSERT(!Underflow);
    SimDtPcieGetTxState(PORT, &State);
    DT_ASSERT(State.FramesSent == 0 &&
              State.PipelineLoad == SIM_TX_BURST_FIFO_SIZE + 16384);

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// In RUN each wait sends a part of a frame, the lines of one event; the sink receives the
// frames whole. The last 16 bytes of a frame whose size is no whole number of 32-byte
// words leave the buffer only with the next data, so that frame completes after more is
// written.
DT_TEST(FramesReachTheSink)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    DtSdiTxFEvent Event;
    SimTxState State;
    uint32_t Offset = 0;
    int f, e;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;

    for (f = 0; f < 3; f++)
        PutFrame(&Buf, &Offset, (uint32_t)(100 + f), f);
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, Offset));
    DT_ASSERT_OK(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, DT_FUNC_OPMODE_RUN));

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
    SimDtPcieGetTxState(PORT, &State);
    DT_ASSERT(State.FramesSent == 2 && State.HeaderErrors == 0);
    DT_ASSERT_EQ((State.WriteOffset + BUFFER_SIZE - State.ReadOffset) % BUFFER_SIZE, 16);

    PutFrame(&Buf, &Offset, 103, 3);
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, Offset));
    DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT(Event.FrameId == 2 && Event.SeqNumber == 3 && Event.Underflow);

    DT_ASSERT_EQ(SimDtPcieTxFrameCount(PORT), 3);
    for (f = 0; f < 3; f++)
    {
        if (!Received(f, (uint32_t)(100 + f), f))
            DT_FAIL("frame %d did not arrive as written", f);
    }

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// An underflow sends nothing and sets the PHY's flag and the burst FIFO's count; the
// formatter reports it with the first event after it, and frames flow again once written,
// without a restart. The PHY's flag stays set until cleared, or until the PHY goes idle.
DT_TEST(UnderflowAndRecovery)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    DtSdiTxFEvent Event;
    uint32_t Offset = 0, Count = 0, CountAfter = 0;
    bool Underflow = false;
    int e;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;
    DT_ASSERT_OK(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, DT_FUNC_OPMODE_RUN));

    // The first event enables the formatter's flag.
    PutFrame(&Buf, &Offset, 0, 0);
    PutFrame(&Buf, &Offset, 1, 1);
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, Offset));
    for (e = 0; e < 4; e++)
        DT_ASSERT_OK(Wait(&Fix, &P, &Event));

    SimDtPcieStarveTx(PORT, 1);
    DT_ASSERT_OK(DtDrvBurstFifoGetOvfUflCount(Fix.Drv, P.Burst, PORT, &Count));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtDrvBurstFifoGetOvfUflCount(Fix.Drv, P.Burst, PORT, &CountAfter));
    DT_ASSERT(CountAfter != Count);
    DT_ASSERT_OK(DtDrvSdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, PORT, &Underflow));
    DT_ASSERT(Underflow);

    DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT(Event.FrameId == 1 && Event.SeqNumber == 0 && Event.Underflow);
    DT_ASSERT_OK(Wait(&Fix, &P, &Event));
    DT_ASSERT(!Event.Underflow);
    DT_ASSERT_OK(DtDrvSdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, PORT, &Underflow));
    DT_ASSERT(Underflow);
    DT_ASSERT_OK(DtDrvSdiTxPhyClearUnderflowFlag(Fix.Drv, P.Phy, PORT));
    DT_ASSERT_OK(DtDrvSdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, PORT, &Underflow));
    DT_ASSERT(!Underflow);

    SimDtPcieStarveTx(PORT, 1);
    DT_ASSERT_EQ(SimDtPcieRunTxEvents(PORT, 5), 0);
    DT_ASSERT_OK(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, DT_FUNC_OPMODE_IDLE));
    DT_ASSERT_OK(DtDrvSdiTxPhyGetUnderflowFlag(Fix.Drv, P.Phy, PORT, &Underflow));
    DT_ASSERT(!Underflow);

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// Nothing goes out while the switches do not bypass the demultiplexer or it runs; a
// position a switch does not have is refused.
DT_TEST(SwitchesMustBypassTheDemux)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    DtSdiTxFEvent Event;
    uint32_t Offset = 0;

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;
    PutFrame(&Buf, &Offset, 0, 0);
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, Offset));
    DT_ASSERT_OK(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, DT_FUNC_OPMODE_RUN));

    DT_ASSERT_EQ(DtDrvSwitchSetPosition(Fix.Drv, P.SwitchIn, PORT, 1, 0),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSwitchSetPosition(Fix.Drv, P.SwitchOut, PORT, 0, 1),
                 DTAPI_E_INVALID_ARG);

    DT_ASSERT_OK(DtDrvSwitchSetPosition(Fix.Drv, P.SwitchIn, PORT, 0, 1));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtDrvSwitchSetPosition(Fix.Drv, P.SwitchIn, PORT, 0, 0));
    DT_ASSERT_OK(DtDrvSdiDmx12GSetOpMode(Fix.Drv, P.Dmx, PORT, DT_BLOCK_OPMODE_RUN));
    DT_ASSERT_EQ(Wait(&Fix, &P, &Event), DTAPI_E_TIMEOUT);
    DT_ASSERT_OK(DtDrvSdiDmx12GSetOpMode(Fix.Drv, P.Dmx, PORT, DT_BLOCK_OPMODE_IDLE));
    DT_ASSERT_OK(Wait(&Fix, &P, &Event));

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// A header that does not check is skipped one alignment word at a time.
DT_TEST(BadHeadersAreSkipped)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    SimTxState State;
    uint32_t Offset = 0;
    uint8_t Garbage[32];

    if (!Open(&Fix, DtFailures) || !Hold(&Fix, &P, &Buf, DtFailures))
        return;
    memset(Garbage, 0xA5, sizeof(Garbage));
    Put(&Buf, &Offset, Garbage, sizeof(Garbage));
    PutFrame(&Buf, &Offset, 7, 7);
    PutFrame(&Buf, &Offset, 8, 8);
    DT_ASSERT_OK(DtDrvCdmacSetTxWriteOffset(Fix.Drv, P.Cdmac, PORT, Offset));
    DT_ASSERT_OK(DtDrvSdiTxPhySetOpMode(Fix.Drv, P.Phy, PORT, DT_FUNC_OPMODE_RUN));

    DT_ASSERT_EQ(SimDtPcieRunTxEvents(PORT, 4), 4);
    SimDtPcieGetTxState(PORT, &State);
    DT_ASSERT(State.FramesSent == 1 && State.HeaderErrors == 2);
    DT_ASSERT(Received(0, 7, 7));

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

// Closing the handle that registered the buffer stops the controller and lets go of the
// buffer; a refused command can be forced.
DT_TEST(ClosingTheHandleStopsTheDma)
{
    Fixture Fix;
    Parts P;
    OsDmaBuffer Buf;
    SimTxState State;
    OsDrv* Other;

    if (!Open(&Fix, DtFailures))
        return;
    P = PartsOf(&Fix);
    Other = OsDrvOpen(SIM_DEVICE_INDEX);
    DT_ASSERT(Other != NULL && OsDmaBufferAlloc(BUFFER_SIZE, &Buf) == 0);
    DT_ASSERT_OK(DtFuncExclAccess(Other, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    DT_ASSERT_OK(DtDrvCdmacAllocateBuffer(Other, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf));
    DT_ASSERT_OK(DtDrvCdmacSetOpMode(Other, P.Cdmac, PORT, DT_BLOCK_OPMODE_RUN));
    OsDrvClose(Other);
    SimDtPcieGetTxState(PORT, &State);
    DT_ASSERT(!State.BufferRegistered && State.CdmacMode == DT_BLOCK_OPMODE_IDLE);

    DT_ASSERT_OK(DtFuncExclAccess(Fix.Drv, &Fix.Dma, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));
    SimDtPcieFailTxCmd(DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER,
                       DT_STATUS_OUT_OF_MEMORY);
    DT_ASSERT_EQ(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf),
                 DTAPI_E_OUT_OF_MEM);
    SimDtPcieFailTxCmd(DT_FUNC_CODE_CDMAC_CMD, DT_CDMAC_CMD_ALLOCATE_BUFFER, 0);
    DT_ASSERT_OK(DtDrvCdmacAllocateBuffer(Fix.Drv, P.Cdmac, PORT, DT_CDMAC_DIR_TX, &Buf));
    DT_ASSERT_OK(DtDrvCdmacFreeBuffer(Fix.Drv, P.Cdmac, PORT));

    OsDmaBufferFree(&Buf);
    FINISH(Fix);
}

DT_TEST_MAIN("SimSdiTx", DT_RUN(OneHandleHoldsAPart), DT_RUN(EveryPartHasExclusiveAccess),
             DT_RUN(AcquiringAllRollsBack), DT_RUN(RequestsCarryTheirFields),
             DT_RUN(InvalidArgumentsSendNothing), DT_RUN(BufferIsRegisteredBothWays),
             DT_RUN(BufferRules), DT_RUN(ModesOfTheDmaController),
             DT_RUN(BlocksCheckAccessAndPort), DT_RUN(WaitRules),
             DT_RUN(StandbyFillsThePipeline), DT_RUN(FramesReachTheSink),
             DT_RUN(UnderflowAndRecovery), DT_RUN(SwitchesMustBypassTheDemux),
             DT_RUN(BadHeadersAreSkipped), DT_RUN(ClosingTheHandleStopsTheDma))

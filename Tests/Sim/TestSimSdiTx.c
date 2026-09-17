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
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

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
#define FINISH(Fix)                                                                      \
    do                                                                                   \
    {                                                                                    \
        DtFuncRelease(&(Fix).Tx);                                                        \
        DtFuncRelease(&(Fix).Dma);                                                       \
        OsDrvClose((Fix).Drv);                                                           \
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);                                         \
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

DT_TEST_MAIN("SimSdiTx", DT_RUN(OneHandleHoldsAPart), DT_RUN(EveryPartHasExclusiveAccess),
             DT_RUN(AcquiringAllRollsBack))

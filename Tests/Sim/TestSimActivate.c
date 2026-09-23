// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSimActivate.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Activating a device at attach, and the VPD it reads, against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtDevActivate.h"          // Function under test.
#include "DtPcie/DtPcieAbi.h"       // Exclusive access commands.
#include "DtPcie/DtPcieCmd.h"       // The VPD layer and exclusive access.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/Sim/SimActivate.h"    // The emulated object and its test controls.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "OAL/Sim/SimDta2110.h"     // The emulated DTA-2110.
#include "OAL/Sim/SimVpd.h"         // The emulated EEPROM.
#include "cdtapi.h"                 // DtDevice.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The index the emulated DTA-2110 is put at; the DTA-2178 keeps index 0.
#define DTA2110_INDEX 1

// Opens the emulated DTA-2110 in its power-on state and notes the live allocations.
static OsDrv* OpenDta2110(int* DtFailures, int* Live)
{
    OsDrv* Drv;

    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(DTA2110_INDEX);
    DtAlloc_ResetCount();
    *Live = DtAlloc_Live();
    Drv = OsDrv_Open(DTA2110_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index %d; is CDTAPI_SIM=1 set?\n",
               DTA2110_INDEX);
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return NULL;
    }
    return Drv;
}

// Closes the device and checks that nothing is left open or allocated.
#define FINISH(Drv, Live)                                                                \
    do                                                                                   \
    {                                                                                    \
        OsDrv_Close(Drv);                                                                \
        DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);                                        \
        DT_ASSERT_EQ(DtAlloc_Live(), Live);                                              \
    } while (0)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= VPD +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The sections lie where the card says they do, and leave room for what is behind them.
DT_TEST(VpdProperties)
{
    int Live = 0;
    OsDrv* Drv = OpenDta2110(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);

    DtVpdProperties Props;
    DT_ASSERT_OK(DtPcieCmd_VpdGetProperties(Drv, &Props));
    DT_ASSERT_EQ(Props.RoOffset, SIM_VPD_RO_OFFSET);
    DT_ASSERT_EQ(Props.RoSize, SIM_VPD_RO_SIZE);
    DT_ASSERT_EQ(Props.RwOffset, SIM_VPD_RW_OFFSET);
    DT_ASSERT_EQ(Props.RwSize, SIM_VPD_RW_SIZE);
    DT_ASSERT_EQ(Props.EepromSize, SIM_VPD_EEPROM_SIZE);
    DT_ASSERT(Props.RoSize + Props.RwSize + SIM_VPD_TAIL_BYTES <= Props.EepromSize);

    DT_ASSERT_EQ(DtPcieCmd_VpdGetProperties(NULL, &Props), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_VpdGetProperties(Drv, NULL), DTAPI_E_INVALID_ARG);
    FINISH(Drv, Live);
}

// A raw read gives the bytes the EEPROM holds, and refuses what it cannot do.
DT_TEST(VpdRawRead)
{
    int Live = 0;
    OsDrv* Drv = OpenDta2110(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);

    uint8_t Buf[SIM_VPD_TAIL_BYTES];
    int NumRead = 0;
    memset(Buf, 0, sizeof(Buf));
    DT_ASSERT_OK(
        DtPcieCmd_VpdRawRead(Drv, SIM_VPD_TAIL_OFFSET, Buf, sizeof(Buf), &NumRead));
    DT_ASSERT_EQ(NumRead, (int)sizeof(Buf));
    DT_ASSERT_EQ(memcmp(Buf, SimVpd_Tail(), sizeof(Buf)), 0);

    DT_ASSERT_EQ(DtPcieCmd_VpdRawRead(Drv, 0, Buf, 0, &NumRead), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_VpdRawRead(Drv, 0, NULL, 4, &NumRead), DTAPI_E_INVALID_ARG);
    DT_ASSERT(DtPcieCmd_VpdRawRead(Drv, SIM_VPD_EEPROM_SIZE, Buf, 4, &NumRead) !=
              DTAPI_OK);
    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A card comes up not activated, and one pass over it makes it ready.
DT_TEST(ActivatesOnce)
{
    int Live = 0;
    OsDrv* Drv = OpenDta2110(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);

    DT_ASSERT(!SimActivate_IsReady());
    DT_ASSERT_OK(DtDevActivate_OnAttach(Drv));
    DT_ASSERT(SimActivate_IsReady());

    // A device that is ready already is left alone, and says so.
    DT_ASSERT_OK(DtDevActivate_OnAttach(Drv));
    DT_ASSERT(SimActivate_IsReady());
    FINISH(Drv, Live);
}

// The object may take a moment; the caller waits for it.
DT_TEST(ActivationWaits)
{
    int Live = 0;
    OsDrv* Drv = OpenDta2110(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);

    SimActivate_SetBusyCount(5);
    DT_ASSERT_OK(DtDevActivate_OnAttach(Drv));
    DT_ASSERT(SimActivate_IsReady());
    FINISH(Drv, Live);
}

// A card whose EEPROM holds nothing for the object cannot be activated, and the failure
// is reported rather than hidden.
DT_TEST(BlankEepromFails)
{
    int Live = 0;
    OsDrv* Drv = OpenDta2110(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);

    SimVpd_SetTailBlank(true);
    DT_ASSERT(DtDevActivate_OnAttach(Drv) != DTAPI_OK);
    DT_ASSERT(!SimActivate_IsReady());
    FINISH(Drv, Live);
}

// A device without the object needs no activating, and does not fail for want of it.
DT_TEST(OtherDeviceNeedsNone)
{
    SimDtPcie_Reset();
    DtAlloc_ResetCount();
    int Live = DtAlloc_Live();
    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    DT_ASSERT(Drv != NULL);

    DT_ASSERT_OK(DtDevActivate_OnAttach(Drv));
    DT_ASSERT(!SimActivate_IsReady());
    FINISH(Drv, Live);
}

// Attaching a device activates it, without the caller asking.
DT_TEST(AttachActivates)
{
    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(DTA2110_INDEX);
    DtAlloc_ResetCount();
    int Live = DtAlloc_Live();

    DtDevice* Device = DtDevice_Alloc();
    DT_ASSERT(Device != NULL);
    DT_ASSERT(!SimActivate_IsReady());
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, (int64_t)SIM_DTA2110_SERIAL));
    DT_ASSERT(SimActivate_IsReady());

    DT_ASSERT_OK(DtDevice_Detach(Device));
    DtDevice_Free(Device);
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

// A card that cannot be activated still attaches: it is there and answers for itself,
// and only carries no data.
DT_TEST(AttachSucceedsWithoutActivation)
{
    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(DTA2110_INDEX);
    SimVpd_SetTailBlank(true);
    DtAlloc_ResetCount();
    int Live = DtAlloc_Live();

    DtDevice* Device = DtDevice_Alloc();
    DT_ASSERT(Device != NULL);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, (int64_t)SIM_DTA2110_SERIAL));
    DT_ASSERT(!SimActivate_IsReady());

    DT_ASSERT_OK(DtDevice_Detach(Device));
    DtDevice_Free(Device);
    DT_ASSERT_EQ(SimDtPcie_OpenHandles(), 0);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

// The object is only answered to whoever holds it, so a second handle cannot activate.
DT_TEST(ActivationNeedsTheObject)
{
    int Live = 0;
    OsDrv* Drv = OpenDta2110(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);

    OsDrv* Other = OsDrv_Open(DTA2110_INDEX);
    DT_ASSERT(Other != NULL);
    int Uuid = 0;
    DT_ASSERT_OK(
        DtPcieCmd_GetPropertyInt(Other, "BC_IPSECG#1_UUID", DT_PROPERTY_DEVICE, &Uuid));
    const DtDrvObject Object = {Uuid, DT_PROPERTY_DEVICE};
    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Other, Object, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE));

    DT_ASSERT(DtDevActivate_OnAttach(Drv) != DTAPI_OK);
    DT_ASSERT(!SimActivate_IsReady());

    DT_ASSERT_OK(DtPcieCmd_ExclAccess(Other, Object, DT_EXCLUSIVE_ACCESS_CMD_RELEASE));
    OsDrv_Close(Other);
    DT_ASSERT_OK(DtDevActivate_OnAttach(Drv));
    DT_ASSERT(SimActivate_IsReady());
    FINISH(Drv, Live);
}
DT_TEST_MAIN("SimActivate", DT_RUN(VpdProperties), DT_RUN(VpdRawRead),
             DT_RUN(ActivatesOnce), DT_RUN(ActivationWaits), DT_RUN(BlankEepromFails),
             DT_RUN(OtherDeviceNeedsNone), DT_RUN(AttachActivates),
             DT_RUN(AttachSucceedsWithoutActivation), DT_RUN(ActivationNeedsTheObject))

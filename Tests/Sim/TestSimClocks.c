// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimClocks.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The genlock, time-of-day and transmit clocks against the emulator: the
// driver commands, and the device functions over them
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, finds the objects as the device layer will, and ends with no handle to the
// emulator and no allocation left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <math.h> // NAN and INFINITY.
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Live allocations.
#include "Device/DtFunc.h"          // Finding the objects.
#include "DtPcie/DtPcieAbi.h"       // The driver's states and video standards.
#include "DtPcie/DtPcieCmd.h"       // Commands under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Device handles.
#include "OAL/OsThread.h"           // Waiting for the counters.
#include "OAL/Sim/SimClocks.h"      // The emulated clocks and their test controls.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "cdtapi.h"                 // DTAPI's constants.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Opens the emulated DTA-2178 in its power-on state and notes the live allocations.
static OsDrv* OpenSim(int* DtFailures, int* Live)
{
    SimDtPcie_Reset();
    *Live = DtAlloc_NumLive();
    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return NULL;
    }
    return Drv;
}

// The object of the device's API function Name that is a driver function when
// IsDriverFunction, of Type and with Role; false when there is none.
static bool FindObject(OsDrv* Drv, const char* Name, bool IsDriverFunction, int Type,
                       const char* Role, DtDrvObject* Object)
{
    DtFuncInstance Instance;

    if (DtFunc_Find(Drv, DT_PROPERTY_DEVICE, Name, "", &Instance) != DTAPI_OK)
        return false;
    const DtFuncObject* Found =
        DtFunc_FindObject(&Instance, IsDriverFunction, Type, Role);
    if (Found != NULL)
        *Object = Found->Object;
    DtFunc_Release(&Instance);
    return Found != NULL;
}

// Closes the device and checks that nothing is left open or allocated.
#define FINISH(Drv, Live)                                                                \
    do                                                                                   \
    {                                                                                    \
        OsDrv_Close(Drv);                                                                \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        DT_ASSERT_EQ(DtAlloc_NumLive(), Live);                                           \
    } while (0)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Objects +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The three API functions belong to the device, with the objects, roles and types a
// DTA-2178 lists, and the emulated driver is new enough for each object.
DT_TEST(ObjectsAreThoseOfTheDevice)
{
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Object;
    DtDriverVersion Version;
    DT_ASSERT_OK(DtPcieCmd_GetDriverVersion(Drv, &Version));

    DT_ASSERT(FindObject(Drv, "AF_GENLOCKCTRL_AF", true, DT_FUNC_TYPE_GENLOCKCTRL, "",
                         &Object));
    DT_ASSERT_EQ(Object.PortIndex, DT_PROPERTY_DEVICE);
    DT_ASSERT_OK(DtFunc_CheckDriverVersion(&Version, true, DT_FUNC_TYPE_GENLOCKCTRL));
    DT_ASSERT(
        FindObject(Drv, "AF_TODCLKCTRL_AF", true, DT_FUNC_TYPE_TODCLKCTRL, "", &Object));
    DT_ASSERT_OK(DtFunc_CheckDriverVersion(&Version, true, DT_FUNC_TYPE_TODCLKCTRL));
    DT_ASSERT(FindObject(Drv, "AF_TXCLKCNTRS", false, DT_BLOCK_TYPE_CLKCNT,
                         "NON_FRAC_CLK", &Object));
    DT_ASSERT(FindObject(Drv, "AF_TXCLKCNTRS", false, DT_BLOCK_TYPE_CLKCNT, "FRAC_CLK",
                         &Object));
    DT_ASSERT_OK(DtFunc_CheckDriverVersion(&Version, false, DT_BLOCK_TYPE_CLKCNT));

    // A port has none of them.
    DtFuncInstance Instance;
    DT_ASSERT_EQ(DtFunc_Find(Drv, 0, "AF_GENLOCKCTRL_AF", "", &Instance),
                 DTAPI_E_NOT_FOUND);
    FINISH(Drv, Live);
}

// A command for another object than the one that takes it is refused.
DT_TEST(CommandsGoToTheirOwnObject)
{
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Genlock;
    DtDrvObject Tod;
    DT_ASSERT(FindObject(Drv, "AF_GENLOCKCTRL_AF", true, DT_FUNC_TYPE_GENLOCKCTRL, "",
                         &Genlock));
    DT_ASSERT(
        FindObject(Drv, "AF_TODCLKCTRL_AF", true, DT_FUNC_TYPE_TODCLKCTRL, "", &Tod));

    DtGenlockState GenlockState;
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetState(Drv, Tod, &GenlockState),
                 DTAPI_E_NOT_SUPPORTED);
    DtTimeOfDayState TodState;
    DT_ASSERT_EQ(DtPcieCmd_TodClkCtrlGetState(Drv, Genlock, &TodState),
                 DTAPI_E_NOT_SUPPORTED);
    uint32_t Count;
    int FrequencyHz;
    DT_ASSERT_EQ(DtPcieCmd_ClkCntGetTickCount(Drv, Genlock, &Count, &FrequencyHz),
                 DTAPI_E_NOT_SUPPORTED);
    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Genlock +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Without a reference the genlock runs free, which DTAPI reports as locked; the 625i50
// reference converts to DTAPI's code, and the top of frame lies within one 40 ms frame.
DT_TEST(GenlockStateAtPowerOn)
{
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Genlock;
    DT_ASSERT(FindObject(Drv, "AF_GENLOCKCTRL_AF", true, DT_FUNC_TYPE_GENLOCKCTRL, "",
                         &Genlock));

    DtGenlockState State;
    DT_ASSERT_OK(DtPcieCmd_GenlockGetState(Drv, Genlock, &State));
    DT_ASSERT_EQ(State.State, DTAPI_GENL_LOCKED);
    DT_ASSERT_EQ(State.RefVidStd, DTAPI_VIDSTD_625I50);
    DT_ASSERT_EQ(State.DetVidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT(State.TofTimeValid);
    DT_ASSERT(State.RefFrameNum > 0);
    DT_ASSERT(State.TimeSinceLastTof >= 0 && State.TimeSinceLastTof < 40000000);
    DT_ASSERT(State.TofTime.Nanoseconds < 1000000000u);
    FINISH(Drv, Live);
}

// Each state of the driver, and one it does not define, as DTAPI converts them; the top
// of frame is valid only with a valid reference.
DT_TEST(GenlockStatesConvert)
{
    static const struct
    {
        int Driver;
        int Dtapi;
        bool Valid;
    } Cases[] = {
        {DT_GENLOCKCTRL_STATE_NO_REF, DTAPI_GENL_NO_REF, false},
        {DT_GENLOCKCTRL_STATE_INVALID_REF, DTAPI_GENL_INVALID, false},
        {DT_GENLOCKCTRL_STATE_LOCKING, DTAPI_GENL_LOCKING, true},
        {DT_GENLOCKCTRL_STATE_LOCKED, DTAPI_GENL_LOCKED, true},
        {DT_GENLOCKCTRL_STATE_FREE_RUN, DTAPI_GENL_LOCKED, true},
        {99, DTAPI_GENL_NO_REF, true},
    };
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Genlock;
    DT_ASSERT(FindObject(Drv, "AF_GENLOCKCTRL_AF", true, DT_FUNC_TYPE_GENLOCKCTRL, "",
                         &Genlock));

    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        SimClocks_SetGenlock(Cases[i].Driver, DT_VIDSTD_1080I50, DT_VIDSTD_1080I59_94);
        DtGenlockState State;
        DT_ASSERT_OK(DtPcieCmd_GenlockGetState(Drv, Genlock, &State));
        DT_ASSERT_EQ(State.State, Cases[i].Dtapi);
        DT_ASSERT_EQ(State.TofTimeValid, Cases[i].Valid);
        DT_ASSERT_EQ(State.RefVidStd, DTAPI_VIDSTD_1080I50);
        DT_ASSERT_EQ(State.DetVidStd, DTAPI_VIDSTD_1080I59_94);
    }
    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit clocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The two clocks a DTA-2178 lists; too little room gives their number and nothing else.
DT_TEST(ClockPropertiesAreListed)
{
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Genlock;
    DT_ASSERT(FindObject(Drv, "AF_GENLOCKCTRL_AF", true, DT_FUNC_TYPE_GENLOCKCTRL, "",
                         &Genlock));

    DtClockProps Props[4];
    int Num = -1;
    DT_ASSERT_OK(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, Props, 4, &Num));
    DT_ASSERT_EQ(Num, 2);
    DT_ASSERT_EQ(Props[0].ClockIndex, 0);
    DT_ASSERT_EQ(Props[0].ClockType, DTAPI_TXCLK_FRACTIONAL);
    DT_ASSERT_EQ(Props[0].FrequencyMicroHz, 148351648351648);
    DT_ASSERT_EQ(Props[0].StepSizePpt, 20);
    DT_ASSERT_EQ(Props[0].RangePpt, 200000000);
    DT_ASSERT_EQ(Props[1].ClockIndex, 1);
    DT_ASSERT_EQ(Props[1].ClockType, DTAPI_TXCLK_NON_FRACTIONAL);
    DT_ASSERT_EQ(Props[1].FrequencyMicroHz, 148500000000000);
    DT_ASSERT_EQ(Props[1].StepSizePpt, 11);

    Num = -1;
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, Props, 1, &Num),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Num, 2);
    Num = -1;
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, NULL, 0, &Num),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Num, 2);

    // A type the driver does not define is a driver failure.
    SimClocks_SetClockType(1, 7);
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, Props, 4, &Num),
                 DTAPI_E_DEV_DRIVER);
    DT_ASSERT_EQ(Num, 0);

    DT_ASSERT_EQ(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, Props, -1, &Num),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, NULL, 1, &Num),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetClockProps(Drv, Genlock, Props, 4, NULL),
                 DTAPI_E_INVALID_ARG);
    FINISH(Drv, Live);
}

// An offset set while the genlock runs free is read back with the frequency it gives;
// the driver refuses one beyond the range, one for a clock it does not have, and any
// while the device is genlocked.
DT_TEST(OffsetsAreSetAndRead)
{
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Genlock;
    DT_ASSERT(FindObject(Drv, "AF_GENLOCKCTRL_AF", true, DT_FUNC_TYPE_GENLOCKCTRL, "",
                         &Genlock));

    int OffsetPpt = -1;
    int64_t FrequencyMicroHz = -1;
    DT_ASSERT_OK(
        DtPcieCmd_GenlockGetFreqOffset(Drv, Genlock, 1, &OffsetPpt, &FrequencyMicroHz));
    DT_ASSERT_EQ(OffsetPpt, 0);
    DT_ASSERT_EQ(FrequencyMicroHz, 148500000000000);

    // 10 ppm on 148.5 MHz is 1485 Hz.
    DT_ASSERT_OK(DtPcieCmd_GenlockSetFreqOffset(Drv, Genlock, 1, 10000000));
    DT_ASSERT_OK(
        DtPcieCmd_GenlockGetFreqOffset(Drv, Genlock, 1, &OffsetPpt, &FrequencyMicroHz));
    DT_ASSERT_EQ(OffsetPpt, 10000000);
    DT_ASSERT_EQ(FrequencyMicroHz, 148501485000000);
    DT_ASSERT_OK(DtPcieCmd_GenlockSetFreqOffset(Drv, Genlock, 0, -200000000));

    DT_ASSERT_EQ(DtPcieCmd_GenlockSetFreqOffset(Drv, Genlock, 0, 200000001),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GenlockSetFreqOffset(Drv, Genlock, 2, 0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(
        DtPcieCmd_GenlockGetFreqOffset(Drv, Genlock, 2, &OffsetPpt, &FrequencyMicroHz),
        DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(OffsetPpt, 0);
    DT_ASSERT_EQ(FrequencyMicroHz, 0);

    SimClocks_SetGenlock(DT_GENLOCKCTRL_STATE_LOCKED, DT_VIDSTD_625I50, DT_VIDSTD_625I50);
    DT_ASSERT_EQ(DtPcieCmd_GenlockSetFreqOffset(Drv, Genlock, 1, 0), DTAPI_E_IN_USE);
    DT_ASSERT_OK(
        DtPcieCmd_GenlockGetFreqOffset(Drv, Genlock, 1, &OffsetPpt, &FrequencyMicroHz));
    DT_ASSERT_EQ(OffsetPpt, 10000000);

    DT_ASSERT_EQ(DtPcieCmd_GenlockGetFreqOffset(Drv, Genlock, 1, NULL, &FrequencyMicroHz),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GenlockGetFreqOffset(Drv, Genlock, 1, &OffsetPpt, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GenlockSetFreqOffset(NULL, Genlock, 1, 0),
                 DTAPI_E_INVALID_ARG);
    FINISH(Drv, Live);
}

// Each counter counts at its own clock's frequency, which it reports in whole Hertz.
DT_TEST(CountersCountTheirClock)
{
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject NonFrac;
    DtDrvObject Frac;
    DT_ASSERT(FindObject(Drv, "AF_TXCLKCNTRS", false, DT_BLOCK_TYPE_CLKCNT,
                         "NON_FRAC_CLK", &NonFrac));
    DT_ASSERT(
        FindObject(Drv, "AF_TXCLKCNTRS", false, DT_BLOCK_TYPE_CLKCNT, "FRAC_CLK", &Frac));

    uint32_t First = 0;
    uint32_t Second = 0;
    int FrequencyHz = 0;
    DT_ASSERT_OK(DtPcieCmd_ClkCntGetTickCount(Drv, NonFrac, &First, &FrequencyHz));
    DT_ASSERT_EQ(FrequencyHz, 148500000);
    OsTime_SleepMs(2);
    DT_ASSERT_OK(DtPcieCmd_ClkCntGetTickCount(Drv, NonFrac, &Second, &FrequencyHz));
    DT_ASSERT(Second - First >= 148500u); // At least 1 ms of ticks, across a wrap too

    DT_ASSERT_OK(DtPcieCmd_ClkCntGetTickCount(Drv, Frac, &First, &FrequencyHz));
    DT_ASSERT_EQ(FrequencyHz, 148351648);

    DT_ASSERT_EQ(DtPcieCmd_ClkCntGetTickCount(Drv, Frac, NULL, &FrequencyHz),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(FrequencyHz, 0);
    DT_ASSERT_EQ(DtPcieCmd_ClkCntGetTickCount(Drv, Frac, &First, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(First, 0u);
    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Without a reference the clock runs free on its internal one; each state and reference
// of the driver converts as DTAPI converts it, and one the driver does not define as free
// run on the internal reference.
DT_TEST(TimeOfDayStatesConvert)
{
    static const struct
    {
        int Driver;
        int Reference;
        int DtapiState;
        int DtapiReference;
    } Cases[] = {
        {DT_TODCLOCKCTRL_STATE_FREE_RUN, DT_TODCLOCKCTRL_REF_INTERNAL,
         DTAPI_TODCLK_FREE_RUN, DTAPI_TODREF_INTERNAL},
        {DT_TODCLOCKCTRL_STATE_LOCKING, DT_TODCLOCKCTRL_REF_STEADYCLOCK,
         DTAPI_TODCLK_LOCKING, DTAPI_TODREF_STEADYCLOCK},
        {DT_TODCLOCKCTRL_STATE_LOCKED, DT_TODCLOCKCTRL_REF_STEADYCLOCK,
         DTAPI_TODCLK_LOCKED, DTAPI_TODREF_STEADYCLOCK},
        {DT_TODCLOCKCTRL_STATE_INVALID_REF, DT_TODCLOCKCTRL_REF_INTERNAL,
         DTAPI_TODCLK_INVALID_REF, DTAPI_TODREF_INTERNAL},
        {99, 99, DTAPI_TODCLK_FREE_RUN, DTAPI_TODREF_INTERNAL},
    };
    int Live = 0;
    OsDrv* Drv = OpenSim(DtFailures, &Live);
    DT_ASSERT(Drv != NULL);
    DtDrvObject Tod;
    DT_ASSERT(
        FindObject(Drv, "AF_TODCLKCTRL_AF", true, DT_FUNC_TYPE_TODCLKCTRL, "", &Tod));

    DtTimeOfDayState State;
    DT_ASSERT_OK(DtPcieCmd_TodClkCtrlGetState(Drv, Tod, &State));
    DT_ASSERT_EQ(State.State, DTAPI_TODCLK_FREE_RUN);
    DT_ASSERT_EQ(State.TodReference, DTAPI_TODREF_INTERNAL);
    DT_ASSERT_EQ(State.RefDeviation, 0);
    DT_ASSERT(State.TodTimestamp.Seconds > 0);
    DT_ASSERT(State.TodTimestamp.Nanoseconds < 1000000000u);

    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        SimClocks_SetTod(Cases[i].Driver, Cases[i].Reference, -3);
        DT_ASSERT_OK(DtPcieCmd_TodClkCtrlGetState(Drv, Tod, &State));
        DT_ASSERT_EQ(State.State, Cases[i].DtapiState);
        DT_ASSERT_EQ(State.TodReference, Cases[i].DtapiReference);
        DT_ASSERT_EQ(State.RefDeviation, -3);
    }

    DT_ASSERT_EQ(DtPcieCmd_TodClkCtrlGetState(Drv, Tod, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_TodClkCtrlGetState(NULL, Tod, &State), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(State.State, 0);
    FINISH(Drv, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Attaches a device object to the emulated DTA-2178 in its power-on state, after the
// overrides a case has set, and notes the live allocations. Returns NULL, having recorded
// a failure, when that is not possible.
static DtDevice* AttachSim(int* DtFailures, int* Live)
{
    *Live = DtAlloc_NumLive();
    DtDevice* Device = DtDevice_Alloc();
    if (Device == NULL || DtDevice_AttachToSerial(Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot attach to the emulated device; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        DtDevice_Free(Device);
        return NULL;
    }
    return Device;
}

// Frees the device and checks that nothing is left open or allocated.
#define FINISH_DEVICE(Device, Live)                                                      \
    do                                                                                   \
    {                                                                                    \
        DtDevice_Freep(&(Device));                                                       \
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);                                    \
        DT_ASSERT_EQ(DtAlloc_NumLive(), Live);                                           \
    } while (0)

// The states come through the device as the commands give them.
DT_TEST(DeviceGivesTheStates)
{
    int Live = 0;
    SimDtPcie_Reset();
    DtDevice* Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);

    DtGenlockState Genlock;
    DT_ASSERT_OK(DtDevice_GetGenlockState(Device, &Genlock));
    DT_ASSERT_EQ(Genlock.State, DTAPI_GENL_LOCKED);
    DT_ASSERT_EQ(Genlock.RefVidStd, DTAPI_VIDSTD_625I50);
    SimClocks_SetGenlock(DT_GENLOCKCTRL_STATE_NO_REF, DT_VIDSTD_625I50,
                         DT_VIDSTD_UNKNOWN);
    DT_ASSERT_OK(DtDevice_GetGenlockState(Device, &Genlock));
    DT_ASSERT_EQ(Genlock.State, DTAPI_GENL_NO_REF);

    DtTimeOfDayState Tod;
    SimClocks_SetTod(DT_TODCLOCKCTRL_STATE_LOCKED, DT_TODCLOCKCTRL_REF_STEADYCLOCK, 2);
    DT_ASSERT_OK(DtDevice_GetTimeOfDayState(Device, &Tod));
    DT_ASSERT_EQ(Tod.State, DTAPI_TODCLK_LOCKED);
    DT_ASSERT_EQ(Tod.TodReference, DTAPI_TODREF_STEADYCLOCK);
    DT_ASSERT_EQ(Tod.RefDeviation, 2);
    FINISH_DEVICE(Device, Live);
}

// The two clocks in Hz and ppm, each with the SIM_SDI_PORT_COUNT ports that can be an
// output; too
// little room gives their number and leaves the array alone.
DT_TEST(DeviceListsTheClocks)
{
    int Live = 0;
    SimDtPcie_Reset();
    DtDevice* Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);

    DtTxClockProperties Clocks[3];
    int Num = -1;
    DT_ASSERT_EQ(DtDevice_GetTxClockProperties(Device, 0, &Num, NULL),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Num, 2);
    memset(Clocks, 0x5A, sizeof(Clocks));
    DT_ASSERT_EQ(DtDevice_GetTxClockProperties(Device, 1, &Num, Clocks),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Num, 2);
    DT_ASSERT_EQ(Clocks[0].TxClockId, 0x5A5A5A5A);

    DT_ASSERT_OK(DtDevice_GetTxClockProperties(Device, 3, &Num, Clocks));
    DT_ASSERT_EQ(Num, 2);
    DT_ASSERT_EQ(Clocks[0].TxClockId, 0);
    DT_ASSERT_EQ(Clocks[0].ClockType, DTAPI_TXCLK_FRACTIONAL);
    DT_ASSERT(Clocks[0].Frequency > 148351648.35 && Clocks[0].Frequency < 148351648.36);
    DT_ASSERT(Clocks[0].RangePpm == 200.0);
    DT_ASSERT(Clocks[0].StepSizePpm > 0.0000199 && Clocks[0].StepSizePpm < 0.0000201);
    DT_ASSERT_EQ(Clocks[1].TxClockId, 1);
    DT_ASSERT_EQ(Clocks[1].ClockType, DTAPI_TXCLK_NON_FRACTIONAL);
    DT_ASSERT(Clocks[1].Frequency == 148500000.0);
    for (int i = 0; i < 2; i++)
    {
        DT_ASSERT_EQ(Clocks[i].NumPorts, SIM_SDI_PORT_COUNT);
        for (int Port = 1; Port <= SIM_SDI_PORT_COUNT; Port++)
            DT_ASSERT_EQ(Clocks[i].Ports[Port - 1], Port);
    }
    FINISH_DEVICE(Device, Live);
}

// A clock is counted by the counter of its type; one the device does not have is not
// found.
DT_TEST(DeviceCountsAClock)
{
    int Live = 0;
    SimDtPcie_Reset();
    DtDevice* Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);

    uint32_t First = 0;
    uint32_t Second = 0;
    DT_ASSERT_OK(DtDevice_GetTxClockCount(Device, 1, &First));
    OsTime_SleepMs(2);
    DT_ASSERT_OK(DtDevice_GetTxClockCount(Device, 1, &Second));
    DT_ASSERT(Second - First >= 148500u);
    DT_ASSERT_OK(DtDevice_GetTxClockCount(Device, 0, &First));
    DT_ASSERT_EQ(DtDevice_GetTxClockCount(Device, 2, &First), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(First, 0u);
    FINISH_DEVICE(Device, Live);
}

// An offset in ppm is rounded to the nearest ppt, halves away from zero, and read back;
// one that is not a number of ppt an int holds, or for a negative clock, is refused here,
// and the driver refuses one beyond the range and any while genlocked.
DT_TEST(DeviceSetsAnOffset)
{
    int Live = 0;
    SimDtPcie_Reset();
    DtDevice* Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);

    double OffsetPpm = 7.0;
    DT_ASSERT_OK(DtDevice_GetTxClockOffset(Device, 1, &OffsetPpm));
    DT_ASSERT(OffsetPpm == 0.0);

    DT_ASSERT_OK(DtDevice_SetTxClockOffset(Device, 1, 10.0));
    DT_ASSERT_OK(DtDevice_GetTxClockOffset(Device, 1, &OffsetPpm));
    DT_ASSERT(OffsetPpm == 10.0);

    // -1.7 ppt is -2, where DTAPI's rounding makes it -1; 2.4 ppt is 2.
    DT_ASSERT_OK(DtDevice_SetTxClockOffset(Device, 0, -0.0000017));
    DT_ASSERT_OK(DtDevice_GetTxClockOffset(Device, 0, &OffsetPpm));
    DT_ASSERT(OffsetPpm == -0.000002);
    DT_ASSERT_OK(DtDevice_SetTxClockOffset(Device, 0, 0.0000024));
    DT_ASSERT_OK(DtDevice_GetTxClockOffset(Device, 0, &OffsetPpm));
    DT_ASSERT(OffsetPpm == 0.000002);

    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, 0, NAN), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, 0, INFINITY), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, 0, 3000.0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, -1, 0.0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_GetTxClockOffset(Device, -1, &OffsetPpm), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, 0, 201.0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_GetTxClockOffset(Device, 2, &OffsetPpm), DTAPI_E_INVALID_ARG);

    SimClocks_SetGenlock(DT_GENLOCKCTRL_STATE_LOCKED, DT_VIDSTD_625I50, DT_VIDSTD_625I50);
    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, 1, 0.0), DTAPI_E_IN_USE);
    DT_ASSERT_OK(DtDevice_GetTxClockOffset(Device, 1, &OffsetPpm));
    DT_ASSERT(OffsetPpm == 10.0);
    FINISH_DEVICE(Device, Live);
}

// A device without an API function, and a driver too old for one, fail only the
// functions that need it.
DT_TEST(DeviceWithoutTheClocks)
{
    int Live = 0;
    SimDtPcie_Reset();
    SimDtPcie_OverrideString("AF_GENLOCKCTRL_AF#1", DT_PROPERTY_DEVICE, false, NULL);
    DtDevice* Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);

    DtGenlockState Genlock;
    DT_ASSERT_EQ(DtDevice_GetGenlockState(Device, &Genlock), DTAPI_E_NOT_SUPPORTED);
    int Num = -1;
    DT_ASSERT_EQ(DtDevice_GetTxClockProperties(Device, 0, &Num, NULL),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(Num, 0);
    uint32_t Count;
    DT_ASSERT_EQ(DtDevice_GetTxClockCount(Device, 0, &Count), DTAPI_E_NOT_SUPPORTED);
    double OffsetPpm;
    DT_ASSERT_EQ(DtDevice_GetTxClockOffset(Device, 0, &OffsetPpm), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtDevice_SetTxClockOffset(Device, 0, 0.0), DTAPI_E_NOT_SUPPORTED);
    DtTimeOfDayState Tod;
    DT_ASSERT_OK(DtDevice_GetTimeOfDayState(Device, &Tod));
    FINISH_DEVICE(Device, Live);

    SimDtPcie_Reset();
    SimDtPcie_OverrideString("AF_TXCLKCNTRS#1", DT_PROPERTY_DEVICE, false, NULL);
    SimDtPcie_OverrideString("AF_TODCLKCTRL_AF#1", DT_PROPERTY_DEVICE, false, NULL);
    Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDayState(Device, &Tod), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtDevice_GetTxClockCount(Device, 0, &Count), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_OK(DtDevice_GetTxClockOffset(Device, 0, &OffsetPpm));
    FINISH_DEVICE(Device, Live);

    // Older than the time-of-day clock control and the counters, not than the genlock.
    SimDtPcie_Reset();
    SimDtPcie_SetDriverVersion(1, 13, 19, 295);
    Device = AttachSim(DtFailures, &Live);
    DT_ASSERT(Device != NULL);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDayState(Device, &Tod), DTAPI_E_DRIVER_INCOMP);
    DT_ASSERT_EQ(DtDevice_GetTxClockCount(Device, 0, &Count), DTAPI_E_DRIVER_INCOMP);
    DT_ASSERT_OK(DtDevice_GetGenlockState(Device, &Genlock));
    FINISH_DEVICE(Device, Live);
}

DT_TEST_MAIN("SimClocks", DT_RUN(ObjectsAreThoseOfTheDevice),
             DT_RUN(CommandsGoToTheirOwnObject), DT_RUN(GenlockStateAtPowerOn),
             DT_RUN(GenlockStatesConvert), DT_RUN(ClockPropertiesAreListed),
             DT_RUN(OffsetsAreSetAndRead), DT_RUN(CountersCountTheirClock),
             DT_RUN(TimeOfDayStatesConvert), DT_RUN(DeviceGivesTheStates),
             DT_RUN(DeviceListsTheClocks), DT_RUN(DeviceCountsAClock),
             DT_RUN(DeviceSetsAnOffset), DT_RUN(DeviceWithoutTheClocks))

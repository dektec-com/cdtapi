// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSimApi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The public device API against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>
#include <time.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // Public API under test.
#include "Core/DtlAlloc.h"          // Allocation failure injection.
#include "DtlDrv.h"                 // Reading back what was configured.
#include "DtlDrvAbi.h"              // Driver statuses and function codes.
#include "DtlTest.h"                // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles for reading back.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Resets the emulator and checks that it is what the process talks to. Returns false,
// having recorded a failure, when it is not.
static bool StartSim(int* DtlFailures)
{
    OsDrv* Drv;

    SimDtPcieReset();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtlFailures)++;
        OsDrvClose(Drv);
        return false;
    }
    OsDrvClose(Drv);
    return true;
}

// An attached device object, or NULL with a failure recorded.
static DtDevice* AttachSim(int* DtlFailures)
{
    DtDevice* Device;

    if (!StartSim(DtlFailures))
        return NULL;

    Device = DtDevice_Alloc();
    if (Device == NULL || DtDevice_AttachToSerial(Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot attach to the emulated device\n");
        (*DtlFailures)++;
        DtDevice_Free(Device);
        return NULL;
    }
    return Device;
}

// The most recent set request, as the emulated driver received it.
typedef struct RawSetIn
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_IoConfigCount;
    DtIoctlIoConfig m_IoCfgPars;
} RawSetIn;

static bool LastSet(RawSetIn* In)
{
    int FunctionCode;

    return SimDtPcieLastInput(&FunctionCode, In, sizeof(*In)) == sizeof(*In) &&
           FunctionCode == DT_FUNC_CODE_IOCONFIG_CMD;
}

// The I/O direction the emulated card holds for a port, numbered from 1.
static int Direction(int Port, int* SubValue)
{
    DtlIoConfig Config;
    OsDrv* Drv = OsDrvOpen(SIM_DEVICE_INDEX);

    Config.Port = Port;
    Config.Group = DTAPI_IOCONFIG_IODIR;
    Config.Value = Config.SubValue = -2;
    DtlDrvGetIoConfig(Drv, &Config);
    OsDrvClose(Drv);
    *SubValue = Config.SubValue;
    return Config.Value;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Scan +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(ScanCountsThePorts)
{
    int Count = -1;
    long Live = DtlAllocLive();

    if (!StartSim(DtlFailures))
        return;

    DTL_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DTL_ASSERT_EQ(DtlAllocLive(), Live);
}

// Every port of the card, as CDTAPI converts DTAPI's hardware functions: capabilities,
// not the current direction, decide IsInput and IsOutput.
DTL_TEST(ScanDescribesEveryPort)
{
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    char Expected[64];
    int Count = -1;
    int i;

    if (!StartSim(DtlFailures))
        return;

    DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);

    for (i = 0; i < SIM_PORT_COUNT; i++)
    {
        const DtHwFuncDesc* Func = &Funcs[i];
        bool Sdi = i < SIM_SDI_PORT_COUNT;

        snprintf(Expected, sizeof(Expected), "%lld:%d", (long long)SIM_SERIAL, i + 1);
        DTL_ASSERT_STR(Func->DeviceName, Expected);
        snprintf(Expected, sizeof(Expected), "DTA-2178 port %d", i + 1);
        DTL_ASSERT_STR(Func->Description, Expected);
        DTL_ASSERT_EQ(Func->SerialNumber, (int64_t)SIM_SERIAL);
        DTL_ASSERT_EQ(Func->Port, i + 1);
        DTL_ASSERT_EQ(Func->IsSdi, 1);
        DTL_ASSERT_EQ(Func->IsAvFifo, 0);
        DTL_ASSERT_EQ(Func->IsInput, Sdi ? 1 : 0);
        DTL_ASSERT_EQ(Func->IsOutput, Sdi ? 1 : 0);
    }
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

// Descriptors beyond the last port are filled as CDTAPI fills them from DTAPI's
// value-initialised ones.
DTL_TEST(ScanFillsTheRestOfTheArray)
{
    DtHwFuncDesc Funcs[SIM_PORT_COUNT + 2];
    DtHwFuncDesc Zero;
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    memset(Funcs, 0x5A, sizeof(Funcs));
    DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT + 2, &Count, Funcs));
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);

    memset(&Zero, 0, sizeof(Zero));
    snprintf(Zero.DeviceName, sizeof(Zero.DeviceName), "0:0");
    snprintf(Zero.Description, sizeof(Zero.Description), "DTA-0 port 0");
    DTL_ASSERT_MEM(&Funcs[SIM_PORT_COUNT], &Zero, sizeof(Zero));
    DTL_ASSERT_MEM(&Funcs[SIM_PORT_COUNT + 1], &Zero, sizeof(Zero));
}

// Too small an array is left as it was.
DTL_TEST(ScanWithTooSmallAnArrayChangesNothing)
{
    DtHwFuncDesc Funcs[3];
    DtHwFuncDesc Before[3];
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    memset(Funcs, 0x5A, sizeof(Funcs));
    memcpy(Before, Funcs, sizeof(Funcs));
    DTL_ASSERT_EQ(DtapiHwFuncScan(3, &Count, Funcs), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);
    DTL_ASSERT_MEM(Funcs, Before, sizeof(Funcs));
}

// One entry too few is too few.
DTL_TEST(ScanNeedsRoomForEveryPort)
{
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    DTL_ASSERT_EQ(DtapiHwFuncScan(SIM_PORT_COUNT - 1, &Count, Funcs),
                  DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);
}

// A device the driver numbers other than 0 is found by the scan and by its serial
// number, up to the last index the driver has.
DTL_TEST(DevicesAreFoundAtAnyIndex)
{
    DtDevice* Device;
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    Device = DtDevice_Alloc();
    SimDtPcieSetIndex(7);
    DTL_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Detach(Device);

    SimDtPcieSetIndex(49);
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Detach(Device);

    SimDtPcieSetIndex(50);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);
    DTL_ASSERT_OK(DtapiHwFuncScan(0, &Count, NULL));
    DTL_ASSERT_EQ(Count, 0);

    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DtDevice_Free(Device);
}

// A device that cannot be attached is left out, whatever the reason.
DTL_TEST(ScanLeavesOutDevicesItCannotAttach)
{
    DtHwFuncDesc Funcs[1];
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    SimDtPcieSetDriverVersion(1, 3, 0);
    DTL_ASSERT_OK(DtapiHwFuncScan(1, &Count, Funcs));
    DTL_ASSERT_EQ(Count, 0);
    DTL_ASSERT_STR(Funcs[0].Description, "DTA-0 port 0");

    SimDtPcieReset();
    SimDtPcieOverrideProperty("PORT_COUNT", -1, false, 0);
    DTL_ASSERT_OK(DtapiHwFuncScan(0, &Count, NULL));
    DTL_ASSERT_EQ(Count, 0);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

DTL_TEST(ScanSurvivesAllocationFailure)
{
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    int Count = -1;
    long Needed;
    long Live = DtlAllocLive();

    if (!StartSim(DtlFailures))
        return;

    // Count what a successful scan allocates, then fail each of those in turn.
    DtlAllocResetCount();
    DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    Needed = DtlAllocCount();
    DTL_ASSERT(Needed > 0);

    for (long Fail = 0; Fail < Needed; Fail++)
    {
        unsigned int Result;

        DtlAllocResetCount();
        DtlAllocFailAfter(Fail);
        Result = DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs);
        DtlAllocResetCount();

        // Either the device could not be opened, and is left out, or the list could
        // not grow, and the scan fails; never a list with ports missing.
        if (Result != DTAPI_OK && Result != DTAPI_E_OUT_OF_MEM)
            DTL_FAIL("allocation %ld failing gave 0x%X", Fail, Result);
        if (Result == DTAPI_OK && Count != 0 && Count != SIM_PORT_COUNT)
            DTL_FAIL("allocation %ld failing gave %d ports", Fail, Count);
        DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
        DTL_ASSERT_EQ(DtlAllocLive(), Live);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(AttachAndDetach)
{
    long Live = DtlAllocLive();
    DtDevice* Device = AttachSim(DtlFailures);

    if (Device == NULL)
        return;

    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 1);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_ATTACHED);
    DTL_ASSERT_OK(DtDevice_Detach(Device));
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DTL_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);

    // A detached object attaches again, and freeing it attached releases everything.
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Free(Device);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DTL_ASSERT_EQ(DtlAllocLive(), Live);
}

DTL_TEST(FreepDetachesAndClears)
{
    DtDevice* Device = AttachSim(DtlFailures);

    if (Device == NULL)
        return;

    DtDevice_Freep(&Device);
    DTL_ASSERT(Device == NULL);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

DTL_TEST(UnknownSerialIsNoSuchDevice)
{
    DtDevice* Device;

    if (!StartSim(DtlFailures))
        return;

    Device = DtDevice_Alloc();
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL + 1),
                  DTAPI_E_NO_SUCH_DEVICE);
    DTL_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DtDevice_Free(Device);
}

// The driver's version is checked before the serial number, so an old driver is
// reported as such even for a serial number no device has.
DTL_TEST(OldDriverIsIncompatible)
{
    DtDevice* Device;

    if (!StartSim(DtlFailures))
        return;

    Device = DtDevice_Alloc();
    SimDtPcieSetDriverVersion(1, 3, 0);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_DRIVER_INCOMP);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL + 1), DTAPI_E_DRIVER_INCOMP);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);

    SimDtPcieSetDriverVersion(1, 3, 1);
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Free(Device);
}

DTL_TEST(FirmwareStatusIsAWarning)
{
    DtDevice* Device;

    if (!StartSim(DtlFailures))
        return;

    Device = DtDevice_Alloc();
    SimDtPcieSetFirmwareStatus(DT_FWSTATUS_OBSOLETE);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK_OBSOLETE_FW);
    DTL_ASSERT_EQ(DtDevice_SetToInput(Device, 1), DTAPI_E_OBSOLETE_FW);
    DtDevice_Detach(Device);

    SimDtPcieSetFirmwareStatus(DT_FWSTATUS_TAINTED);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK_TAINTED_FW);
    DTL_ASSERT_EQ(DtDevice_SetToInput(Device, 1), DTAPI_E_TAINTED_FW);
    DtDevice_Detach(Device);

    // Only those two; an old firmware is not a warning.
    SimDtPcieSetFirmwareStatus(DT_FWSTATUS_OLD);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK);
    DTL_ASSERT_OK(DtDevice_SetToInput(Device, 1));
    DtDevice_Free(Device);
}

// A device whose identity or port count cannot be read is no device.
DTL_TEST(UnreadableDeviceIsNoSuchDevice)
{
    DtDevice* Device;
    long Live;

    if (!StartSim(DtlFailures))
        return;

    Device = DtDevice_Alloc();
    Live = DtlAllocLive();

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO2, DT_STATUS_FAIL);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO, DT_STATUS_FAIL);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcieReset();
    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DRIVER_VERSION, DT_STATUS_FAIL);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcieReset();
    SimDtPcieOverrideProperty("PORT_COUNT", -1, false, 0);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    // Counts no driver reports: negative, or more ports than any device has.
    SimDtPcieReset();
    SimDtPcieOverrideProperty("PORT_COUNT", -1, true, (uint64_t)-1);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcieReset();
    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, true, (uint64_t)-1);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, true, 1025);
    DTL_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, true, 1024);
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Detach(Device);

    DTL_ASSERT_EQ(DtlAllocLive(), Live);
    DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DtDevice_Free(Device);
}

// Only the public ports are ports: MAIN_PORT_COUNT when there is one, else PORT_COUNT.
DTL_TEST(PublicPortsComeFromMainPortCount)
{
    DtDevice* Device;
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, true, 8);
    DTL_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_EQ(Count, 8);

    Device = DtDevice_Alloc();
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DTL_ASSERT_OK(DtDevice_SetToOutput(Device, 8));
    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, 9), DTAPI_E_NO_SUCH_PORT);
    DtDevice_Detach(Device);

    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, false, 0);
    DTL_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_EQ(Count, SIM_PORT_COUNT);

    // A device without public ports attaches, and has no hardware functions.
    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, true, 0);
    DTL_ASSERT_OK(DtapiHwFuncScan(0, &Count, NULL));
    DTL_ASSERT_EQ(Count, 0);
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, 1), DTAPI_E_NO_SUCH_PORT);
    DtDevice_Free(Device);
}

// A capability the driver cannot report counts as absent, as in DTAPI.
DTL_TEST(UnreadableCapabilityIsAbsent)
{
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    int Count = -1;

    if (!StartSim(DtlFailures))
        return;

    SimDtPcieOverrideProperty("CAP_OUTPUT", 1, false, 0);
    DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DTL_ASSERT_EQ(Funcs[0].IsOutput, 1);
    DTL_ASSERT_EQ(Funcs[1].IsOutput, 0);
    DTL_ASSERT_EQ(Funcs[1].IsInput, 1);

    SimDtPcieOverrideProperty("CAP_AVFIFO", 2, true, 1);
    DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DTL_ASSERT_EQ(Funcs[2].IsAvFifo, 1);
    DTL_ASSERT_EQ(Funcs[1].IsAvFifo, 0);
}

// Each of the SDI rates alone makes a port an SDI port.
DTL_TEST(EachSdiRateMakesAnSdiPort)
{
    static const char* const Rates[] = {"CAP_12GSDI", "CAP_6GSDI", "CAP_3GSDI",
                                        "CAP_HDSDI", "CAP_SDI"};
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    int Count = -1;
    size_t i;

    if (!StartSim(DtlFailures))
        return;

    for (i = 0; i < sizeof(Rates) / sizeof(Rates[0]); i++)
        SimDtPcieOverrideProperty(Rates[i], 9, true, 0);
    DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DTL_ASSERT_EQ(Funcs[9].IsSdi, 0);

    for (i = 0; i < sizeof(Rates) / sizeof(Rates[0]); i++)
    {
        SimDtPcieOverrideProperty(Rates[i], 9, true, 1);
        DTL_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
        if (Funcs[9].IsSdi != 1)
            DTL_FAIL("%s alone does not make an SDI port", Rates[i]);
        SimDtPcieOverrideProperty(Rates[i], 9, true, 0);
    }
}

DTL_TEST(AttachSurvivesAllocationFailure)
{
    DtDevice* Device;
    long Needed, Fail;
    long Live;

    if (!StartSim(DtlFailures))
        return;

    Device = DtDevice_Alloc();
    Live = DtlAllocLive();
    DtlAllocResetCount();
    DTL_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    Needed = DtlAllocCount();
    DtDevice_Detach(Device);
    DTL_ASSERT_EQ(DtlAllocLive(), Live);
    DTL_ASSERT(Needed >= 3);

    for (Fail = 0; Fail < Needed; Fail++)
    {
        unsigned int Result;

        DtlAllocResetCount();
        DtlAllocFailAfter(Fail);
        Result = DtDevice_AttachToSerial(Device, SIM_SERIAL);
        DtlAllocResetCount();

        // The handle's own allocations fail as a device that cannot be opened; the
        // port capabilities as memory.
        if (Result != DTAPI_E_NO_SUCH_DEVICE && Result != DTAPI_E_OUT_OF_MEM)
            DTL_FAIL("allocation %ld failing gave 0x%X", Fail, Result);
        DTL_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
        DTL_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
        DTL_ASSERT_EQ(DtlAllocLive(), Live);
    }
    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(DirectionsReachTheCard)
{
    DtDevice* Device = AttachSim(DtlFailures);
    int SubValue;

    if (Device == NULL)
        return;

    DTL_ASSERT_OK(DtDevice_SetToOutput(Device, 1));
    {
        RawSetIn Sent;

        DTL_ASSERT(LastSet(&Sent));
        DTL_ASSERT_EQ(Sent.m_IoCfgPars.m_PortIndex, 0);
        DTL_ASSERT_EQ(Sent.m_IoCfgPars.m_ParXtra[0], -1);
        DTL_ASSERT_EQ(Sent.m_IoCfgPars.m_ParXtra[1], -1);
    }
    DTL_ASSERT_EQ(Direction(1, &SubValue), DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_OUTPUT);

    DTL_ASSERT_OK(DtDevice_SetToInput(Device, 2));
    DTL_ASSERT_EQ(Direction(2, &SubValue), DTAPI_IOCONFIG_INPUT);
    DTL_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_INPUT);

    DTL_ASSERT_OK(DtDevice_SetIoConfig(Device, 3, DTAPI_IOCONFIG_IODIR,
                                       DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF));
    DTL_ASSERT_EQ(Direction(3, &SubValue), DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_DBLBUF);

    DtDevice_Free(Device);
}

// The checks before the driver is asked, in DTAPI's order: the port, then the
// combination.
DTL_TEST(ConfigurationIsCheckedFirst)
{
    DtDevice* Device = AttachSim(DtlFailures);

    if (Device == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_IN_USE);

    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, 0), DTAPI_E_NO_SUCH_PORT);
    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, SIM_PORT_COUNT + 1), DTAPI_E_NO_SUCH_PORT);
    DTL_ASSERT_EQ(DtDevice_SetIoConfig(Device, 0, -1, -1, -1), DTAPI_E_NO_SUCH_PORT);
    DTL_ASSERT_EQ(
        DtDevice_SetIoConfig(Device, 1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, -1),
        DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_SetIoConfig(Device, 1, DTAPI_IOCONFIG_IOSTD,
                                       DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT),
                  DTAPI_E_INVALID_ARG);

    // Valid, so the driver is asked, and refuses.
    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, SIM_PORT_COUNT), DTAPI_E_IN_USE);

    SimDtPcieReset();
    DTL_ASSERT_EQ(DtDevice_SetToOutput(Device, SIM_PORT_COUNT), DTAPI_E_CONFIG);
    DTL_ASSERT_OK(DtDevice_SetToOutput(Device, 1));

    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(TimeOfDayComesFromTheCard)
{
    DtTimeOfDay Tod = {0, 0};
    time_t Before, After;
    DtDevice* Device = AttachSim(DtlFailures);

    if (Device == NULL)
        return;

    Before = time(NULL);
    DTL_ASSERT_OK(DtDevice_GetTimeOfDay(Device, &Tod));
    After = time(NULL);
    DTL_ASSERT((time_t)Tod.Seconds + 1 >= Before);
    DTL_ASSERT((time_t)Tod.Seconds <= After + 1);
    DTL_ASSERT(Tod.Nanoseconds < 1000000000U);

    // Both parts come through: of a few readings, not every one is a whole second.
    {
        int Tries;

        for (Tries = 0; Tod.Nanoseconds == 0 && Tries < 5; Tries++)
            DtDevice_GetTimeOfDay(Device, &Tod);
        DTL_ASSERT(Tod.Nanoseconds != 0);
    }

    // A failure leaves zero, not a stale time.
    SimDtPcieFailWithStatus(DT_FUNC_CODE_TOD_CMD, DT_STATUS_BUSY);
    DTL_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, &Tod), DTAPI_E_BUSY);
    DTL_ASSERT_EQ(Tod.Seconds, 0);
    DTL_ASSERT_EQ(Tod.Nanoseconds, 0);

    DtDevice_Free(Device);
}

DTL_TEST_MAIN("SimApi", DTL_RUN(ScanCountsThePorts), DTL_RUN(ScanDescribesEveryPort),
              DTL_RUN(ScanFillsTheRestOfTheArray),
              DTL_RUN(ScanWithTooSmallAnArrayChangesNothing),
              DTL_RUN(ScanNeedsRoomForEveryPort), DTL_RUN(DevicesAreFoundAtAnyIndex),
              DTL_RUN(ScanLeavesOutDevicesItCannotAttach),
              DTL_RUN(ScanSurvivesAllocationFailure), DTL_RUN(AttachAndDetach),
              DTL_RUN(FreepDetachesAndClears), DTL_RUN(UnknownSerialIsNoSuchDevice),
              DTL_RUN(OldDriverIsIncompatible), DTL_RUN(FirmwareStatusIsAWarning),
              DTL_RUN(UnreadableDeviceIsNoSuchDevice),
              DTL_RUN(PublicPortsComeFromMainPortCount),
              DTL_RUN(UnreadableCapabilityIsAbsent), DTL_RUN(EachSdiRateMakesAnSdiPort),
              DTL_RUN(AttachSurvivesAllocationFailure), DTL_RUN(DirectionsReachTheCard),
              DTL_RUN(ConfigurationIsCheckedFirst), DTL_RUN(TimeOfDayComesFromTheCard))

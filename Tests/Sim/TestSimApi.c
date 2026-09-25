// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSimApi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The public device API against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it left open.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>
#include <time.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Allocation failure injection.
#include "DtPcieAbi.h"              // Driver statuses and function codes.
#include "DtPcieCmd.h"              // Reading back what was configured.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Direct handles for reading back.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "cdtapi.h"                 // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// One I/O configuration through the device, which takes a list.
static DtapiResult SetIoConfig(DtDevice* Device, int Port, int Group, int Value,
                               int SubValue)
{
    DtIoConfig Config = {Port, Group, Value, SubValue, {-1, -1}};
    return DtDevice_SetIoConfig(Device, &Config, 1);
}

// Resets the emulator and checks that it is what the process talks to. Returns false,
// having recorded a failure, when it is not.
static bool StartSim(int* DtFailures)
{
    SimDtPcie_Reset();
    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPI_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return false;
    }
    OsDrv_Close(Drv);
    return true;
}

// An attached device object, or NULL with a failure recorded.
static DtDevice* AttachSim(int* DtFailures)
{
    if (!StartSim(DtFailures))
        return NULL;

    DtDevice* Device = DtDevice_Alloc();
    if (Device == NULL || DtDevice_AttachToSerial(Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot attach to the emulated device\n");
        (*DtFailures)++;
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

    return SimDtPcie_LastInput(&FunctionCode, In, sizeof(*In)) == sizeof(*In) &&
           FunctionCode == DT_FUNC_CODE_IOCONFIG_CMD;
}

// The I/O direction the emulated card holds for a port, numbered from 1.
static int Direction(int Port, int* SubValue)
{
    DtIoConfig Config;
    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);

    Config.Port = Port;
    Config.Group = DTAPI_IOCONFIG_IODIR;
    Config.Value = Config.SubValue = -2;
    DtPcieCmd_GetIoConfig(Drv, &Config);
    OsDrv_Close(Drv);
    *SubValue = Config.SubValue;
    return Config.Value;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Scan +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(ScanCountsThePorts)
{
    int Count = -1;
    int Live = DtAlloc_NumLive();

    if (!StartSim(DtFailures))
        return;

    DT_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// Every port of the card, as the scan describes a hardware function: capabilities,
// not the current direction, decide IsInput and IsOutput.
DT_TEST(ScanDescribesEveryPort)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);

    char Expected[64];
    for (int i = 0; i < SIM_PORT_COUNT; i++)
    {
        const DtHwFuncDesc* Func = &Funcs[i];
        bool Sdi = i < SIM_SDI_PORT_COUNT;

        snprintf(Expected, sizeof(Expected), "%lld:%d", (long long)SIM_SERIAL, i + 1);
        DT_ASSERT_STR(Func->DeviceName, Expected);
        snprintf(Expected, sizeof(Expected), "DTA-2178 port %d", i + 1);
        DT_ASSERT_STR(Func->Description, Expected);
        DT_ASSERT_EQ(Func->SerialNumber, (int64_t)SIM_SERIAL);
        DT_ASSERT_EQ(Func->Port, i + 1);
        DT_ASSERT_EQ(Func->IsSdi, Sdi);
        DT_ASSERT_EQ(Func->IsAvFifo, false);
        DT_ASSERT_EQ(Func->IsInput, Sdi);
        DT_ASSERT_EQ(Func->IsOutput, Sdi);
        DT_ASSERT_EQ(Func->IsAsi, Sdi);
    }
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
}

// Descriptors beyond the last port are filled as a descriptor of no port is: all zeros,
// with the name and description that a device and port of 0 make.
DT_TEST(ScanFillsTheRestOfTheArray)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    DtHwFuncDesc Funcs[SIM_PORT_COUNT + 2];
    memset(Funcs, 0x5A, sizeof(Funcs));
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT + 2, &Count, Funcs));
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);

    DtHwFuncDesc Zero;
    memset(&Zero, 0, sizeof(Zero));
    snprintf(Zero.DeviceName, sizeof(Zero.DeviceName), "0:0");
    snprintf(Zero.Description, sizeof(Zero.Description), "DTA-0 port 0");
    DT_ASSERT_MEM(&Funcs[SIM_PORT_COUNT], &Zero, sizeof(Zero));
    DT_ASSERT_MEM(&Funcs[SIM_PORT_COUNT + 1], &Zero, sizeof(Zero));
}

// Too small an array is left as it was.
DT_TEST(ScanWithTooSmallAnArrayChangesNothing)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    DtHwFuncDesc Funcs[3];
    memset(Funcs, 0x5A, sizeof(Funcs));
    DtHwFuncDesc Before[3];
    memcpy(Before, Funcs, sizeof(Funcs));
    DT_ASSERT_EQ(DtapiHwFuncScan(3, &Count, Funcs), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);
    DT_ASSERT_MEM(Funcs, Before, sizeof(Funcs));
}

// One entry too few is too few.
DT_TEST(ScanNeedsRoomForEveryPort)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    DT_ASSERT_EQ(DtapiHwFuncScan(SIM_PORT_COUNT - 1, &Count, Funcs),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);
}

// A device the driver numbers other than 0 is found by the scan and by its serial
// number, up to the last index the driver has.
DT_TEST(DevicesAreFoundAtAnyIndex)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    DtDevice* Device = DtDevice_Alloc();
    SimDtPcie_SetDta2178Index(7);
    DT_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Detach(Device);

    SimDtPcie_SetDta2178Index(49);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Detach(Device);

    SimDtPcie_SetDta2178Index(50);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);
    DT_ASSERT_OK(DtapiHwFuncScan(0, &Count, NULL));
    DT_ASSERT_EQ(Count, 0);

    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    DtDevice_Free(Device);
}

// A device that cannot be attached is left out, whatever the reason.
DT_TEST(ScanLeavesOutDevicesItCannotAttach)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    SimDtPcie_SetDriverVersion(1, 3, 0, 0);
    DtHwFuncDesc Funcs[1];
    DT_ASSERT_OK(DtapiHwFuncScan(1, &Count, Funcs));
    DT_ASSERT_EQ(Count, 0);
    DT_ASSERT_STR(Funcs[0].Description, "DTA-0 port 0");

    SimDtPcie_Reset();
    SimDtPcie_OverrideProperty("PORT_COUNT", -1, false, 0);
    DT_ASSERT_OK(DtapiHwFuncScan(0, &Count, NULL));
    DT_ASSERT_EQ(Count, 0);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
}

DT_TEST(ScanSurvivesAllocationFailure)
{
    int Count = -1;
    int Live = DtAlloc_NumLive();

    if (!StartSim(DtFailures))
        return;

    // Count what a successful scan allocates, then fail each of those in turn.
    DtAlloc_ResetCount();
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    int Needed = DtAlloc_NumAllocations();
    DT_ASSERT(Needed > 0);

    for (int Fail = 0; Fail < Needed; Fail++)
    {
        DtAlloc_ResetCount();
        DtAlloc_FailAfter(Fail);
        DtapiResult Result = DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs);
        DtAlloc_ResetCount();

        // Either the device could not be opened, and is left out, or the list could
        // not grow, and the scan fails; never a list with ports missing.
        if (Result != DTAPI_OK && Result != DTAPI_E_OUT_OF_MEM)
            DT_FAIL("allocation %d failing gave 0x%X", Fail, Result);
        if (Result == DTAPI_OK && Count != 0 && Count != SIM_PORT_COUNT)
            DT_FAIL("allocation %d failing gave %d ports", Fail, Count);
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
        DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(AttachAndDetach)
{
    int Live = DtAlloc_NumLive();
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 1);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_ATTACHED);
    DT_ASSERT_OK(DtDevice_Detach(Device));
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    DT_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);

    // A detached object attaches again, and freeing it attached releases everything.
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Free(Device);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

DT_TEST(FreepDetachesAndClears)
{
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    DtDevice_Freep(&Device);
    DT_ASSERT(Device == NULL);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
}

DT_TEST(UnknownSerialIsNoSuchDevice)
{
    if (!StartSim(DtFailures))
        return;

    DtDevice* Device = DtDevice_Alloc();
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL + 1), DTAPI_E_NO_SUCH_DEVICE);
    DT_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    DtDevice_Free(Device);
}

// The driver's version is checked before the serial number, so an old driver is
// reported as such even for a serial number no device has.
DT_TEST(OldDriverIsIncompatible)
{
    if (!StartSim(DtFailures))
        return;

    DtDevice* Device = DtDevice_Alloc();
    SimDtPcie_SetDriverVersion(1, 3, 0, 0);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_DRIVER_INCOMP);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL + 1), DTAPI_E_DRIVER_INCOMP);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);

    SimDtPcie_SetDriverVersion(1, 3, 1, 0);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Free(Device);
}

DT_TEST(FirmwareStatusIsAWarning)
{
    if (!StartSim(DtFailures))
        return;

    DtDevice* Device = DtDevice_Alloc();
    SimDtPcie_SetFirmwareStatus(DT_FWSTATUS_OBSOLETE);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK_OBSOLETE_FW);
    DT_ASSERT_EQ(DtDevice_SetToInput(Device, 1), DTAPI_E_OBSOLETE_FW);
    DtDevice_Detach(Device);

    SimDtPcie_SetFirmwareStatus(DT_FWSTATUS_TAINTED);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK_TAINTED_FW);
    DT_ASSERT_EQ(DtDevice_SetToInput(Device, 1), DTAPI_E_TAINTED_FW);
    DtDevice_Detach(Device);

    // Only those two; an old firmware is not a warning.
    SimDtPcie_SetFirmwareStatus(DT_FWSTATUS_OLD);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK);
    DT_ASSERT_OK(DtDevice_SetToInput(Device, 1));
    DtDevice_Free(Device);
}

// A device whose identity or port count cannot be read is not a device.
DT_TEST(UnreadableDeviceIsNoSuchDevice)
{
    if (!StartSim(DtFailures))
        return;

    DtDevice* Device = DtDevice_Alloc();
    int Live = DtAlloc_NumLive();

    SimDtPcie_FailWithStatus(DT_FUNC_CODE_GET_DEV_INFO2, DT_STATUS_FAIL);
    SimDtPcie_FailWithStatus(DT_FUNC_CODE_GET_DEV_INFO, DT_STATUS_FAIL);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcie_Reset();
    SimDtPcie_FailWithStatus(DT_FUNC_CODE_GET_DRIVER_VERSION, DT_STATUS_FAIL);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcie_Reset();
    SimDtPcie_OverrideProperty("PORT_COUNT", -1, false, 0);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    // Counts no driver reports: negative, or more ports than any device has.
    SimDtPcie_Reset();
    SimDtPcie_OverrideProperty("PORT_COUNT", -1, true, (uint64_t)-1);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcie_Reset();
    SimDtPcie_OverrideProperty("MAIN_PORT_COUNT", -1, true, (uint64_t)-1);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcie_OverrideProperty("MAIN_PORT_COUNT", -1, true, 1025);
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_E_NO_SUCH_DEVICE);

    SimDtPcie_OverrideProperty("MAIN_PORT_COUNT", -1, true, 1024);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DtDevice_Detach(Device);

    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
    DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
    DtDevice_Free(Device);
}

// Only the public ports are ports: MAIN_PORT_COUNT when there is one, else PORT_COUNT.
DT_TEST(PublicPortsComeFromMainPortCount)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    SimDtPcie_OverrideProperty("MAIN_PORT_COUNT", -1, true, 8);
    DT_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, 8);

    DtDevice* Device = DtDevice_Alloc();
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DT_ASSERT_OK(DtDevice_SetToOutput(Device, 8));
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, 9), DTAPI_E_NO_SUCH_PORT);
    DtDevice_Detach(Device);

    SimDtPcie_OverrideProperty("MAIN_PORT_COUNT", -1, false, 0);
    DT_ASSERT_EQ(DtapiHwFuncScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, SIM_PORT_COUNT);

    // A device without public ports attaches, and has no hardware functions.
    SimDtPcie_OverrideProperty("MAIN_PORT_COUNT", -1, true, 0);
    DT_ASSERT_OK(DtapiHwFuncScan(0, &Count, NULL));
    DT_ASSERT_EQ(Count, 0);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, 1), DTAPI_E_NO_SUCH_PORT);
    DtDevice_Free(Device);
}

// A capability the driver cannot report counts as absent.
DT_TEST(UnreadableCapabilityIsAbsent)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    SimDtPcie_OverrideProperty("CAP_OUTPUT", 1, false, 0);
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Funcs[0].IsOutput, true);
    DT_ASSERT_EQ(Funcs[1].IsOutput, false);
    DT_ASSERT_EQ(Funcs[1].IsInput, true);

    SimDtPcie_OverrideProperty("CAP_AVFIFO", 2, true, 1);
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Funcs[2].IsAvFifo, true);
    DT_ASSERT_EQ(Funcs[1].IsAvFifo, false);

    SimDtPcie_OverrideProperty("CAP_ASI", 3, false, 0);
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Funcs[3].IsAsi, false);
    DT_ASSERT_EQ(Funcs[4].IsAsi, true);
}

// Each of the SDI rates alone makes an input an SDI port, with CAP_MATRIX2 and not
// without it.
DT_TEST(EachSdiRateMakesAnSdiPort)
{
    static const char* const Rates[] = {"CAP_12GSDI", "CAP_6GSDI", "CAP_3GSDI",
                                        "CAP_HDSDI", "CAP_SDI"};
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    size_t i;
    for (i = 0; i < sizeof(Rates) / sizeof(Rates[0]); i++)
        SimDtPcie_OverrideProperty(Rates[i], 9, true, 0);
    SimDtPcie_OverrideProperty("CAP_MATRIX2", 9, true, 1);
    SimDtPcie_OverrideProperty("CAP_INPUT", 9, true, 1);
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Funcs[9].IsSdi, false);

    for (i = 0; i < sizeof(Rates) / sizeof(Rates[0]); i++)
    {
        SimDtPcie_OverrideProperty(Rates[i], 9, true, 1);
        DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
        if (!Funcs[9].IsSdi)
            DT_FAIL("%s alone does not make an SDI port", Rates[i]);
        SimDtPcie_OverrideProperty(Rates[i], 9, true, 0);
    }

    SimDtPcie_OverrideProperty("CAP_12GSDI", 9, true, 1);
    SimDtPcie_OverrideProperty("CAP_MATRIX2", 9, true, 0);
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Funcs[9].IsSdi, false);
}

// A port that can be neither an input nor an output is neither an ASI nor an SDI port,
// whatever else it has.
DT_TEST(AsiAndSdiNeedADirection)
{
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    SimDtPcie_OverrideProperty("CAP_INPUT", 2, false, 0);
    SimDtPcie_OverrideProperty("CAP_OUTPUT", 2, false, 0);
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Count, Funcs));
    DT_ASSERT_EQ(Funcs[2].IsAsi, false);
    DT_ASSERT_EQ(Funcs[2].IsSdi, false);
    DT_ASSERT_EQ(Funcs[3].IsAsi, true);
    DT_ASSERT_EQ(Funcs[3].IsSdi, true);
}

DT_TEST(AttachSurvivesAllocationFailure)
{
    if (!StartSim(DtFailures))
        return;

    DtDevice* Device = DtDevice_Alloc();
    int Live = DtAlloc_NumLive();
    DtAlloc_ResetCount();
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    int Needed = DtAlloc_NumAllocations();
    DtDevice_Detach(Device);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
    DT_ASSERT(Needed >= 3);

    for (int Fail = 0; Fail < Needed; Fail++)
    {
        DtAlloc_ResetCount();
        DtAlloc_FailAfter(Fail);
        DtapiResult Result = DtDevice_AttachToSerial(Device, SIM_SERIAL);
        DtAlloc_ResetCount();

        // The handle's own allocations fail as a device that cannot be opened; the
        // port capabilities and looking for the clocks as memory.
        if (Result != DTAPI_E_NO_SUCH_DEVICE && Result != DTAPI_E_OUT_OF_MEM)
            DT_FAIL("allocation %d failing gave 0x%X", Fail, Result);
        DT_ASSERT_EQ(SimDtPcie_OpenHandleCount(), 0);
        DT_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
        DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
    }
    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(DirectionsReachTheCard)
{
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    DT_ASSERT_OK(DtDevice_SetToOutput(Device, 1));
    {
        RawSetIn Sent;

        DT_ASSERT(LastSet(&Sent));
        DT_ASSERT_EQ(Sent.m_IoCfgPars.m_PortIndex, 0);
        DT_ASSERT_EQ(Sent.m_IoCfgPars.m_ParXtra[0], -1);
        DT_ASSERT_EQ(Sent.m_IoCfgPars.m_ParXtra[1], -1);
    }
    int SubValue;
    DT_ASSERT_EQ(Direction(1, &SubValue), DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_OUTPUT);

    DT_ASSERT_OK(DtDevice_SetToInput(Device, 2));
    DT_ASSERT_EQ(Direction(2, &SubValue), DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_INPUT);

    DT_ASSERT_OK(SetIoConfig(Device, 3, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                             DTAPI_IOCONFIG_DBLBUF));
    DT_ASSERT_EQ(Direction(3, &SubValue), DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_EQ(SubValue, DTAPI_IOCONFIG_DBLBUF);

    DtDevice_Free(Device);
}

// The checks before the driver is asked, in their order: the port, then the
// combination.
DT_TEST(ConfigurationIsCheckedFirst)
{
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    SimDtPcie_FailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_IN_USE);

    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, 0), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, SIM_PORT_COUNT + 1), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(SetIoConfig(Device, 0, -1, -1, -1), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(SetIoConfig(Device, 1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(SetIoConfig(Device, 1, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_OUTPUT,
                             DTAPI_IOCONFIG_OUTPUT),
                 DTAPI_E_INVALID_ARG);

    // Valid, so the driver is asked, and refuses.
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, SIM_PORT_COUNT), DTAPI_E_IN_USE);

    SimDtPcie_Reset();
    DT_ASSERT_EQ(DtDevice_SetToOutput(Device, SIM_PORT_COUNT), DTAPI_E_CONFIG);
    DT_ASSERT_OK(DtDevice_SetToOutput(Device, 1));

    DtDevice_Free(Device);
}

// A list goes to the driver as one request, the other port of a double-buffered output
// as an index there and as a number here, and reads back as it was set.
DT_TEST(ListsAreSetAndReadInOneRequest)
{
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    DtIoConfig Set[3] = {
        {1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT, {-1, -1}},
        {2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT, {-1, -1}},
        {3, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF, {1, -1}},
    };
    DT_ASSERT_OK(DtDevice_SetIoConfig(Device, Set, 3));
    {
        struct
        {
            DtIoctlInputDataHdr m_CmdHdr;
            Int m_IoConfigCount;
            DtIoctlIoConfig m_IoCfgPars[3];
        } Sent;
        int FunctionCode;
        DT_ASSERT_EQ(SimDtPcie_LastInput(&FunctionCode, &Sent, sizeof(Sent)),
                     sizeof(Sent));
        DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_IOCONFIG_CMD);
        DT_ASSERT_EQ(Sent.m_IoConfigCount, 3);
        DT_ASSERT_EQ(Sent.m_IoCfgPars[2].m_PortIndex, 2);
        DT_ASSERT_EQ(Sent.m_IoCfgPars[2].m_ParXtra[0], 0);
    }

    DtIoConfig Got[3];
    for (int i = 0; i < 3; i++)
    {
        Got[i].Port = i + 1;
        Got[i].Group = DTAPI_IOCONFIG_IODIR;
    }
    DT_ASSERT_OK(DtDevice_GetIoConfig(Device, Got, 3));
    for (int i = 0; i < 3; i++)
    {
        DT_ASSERT_EQ(Got[i].Value, Set[i].Value);
        DT_ASSERT_EQ(Got[i].SubValue, Set[i].SubValue);
        DT_ASSERT_EQ(Got[i].ParXtra[0], Set[i].ParXtra[0]);
    }

    // Nothing to do is no failure, and asks the driver nothing.
    SimDtPcie_FailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_IN_USE);
    DT_ASSERT_OK(DtDevice_SetIoConfig(Device, NULL, 0));
    DT_ASSERT_OK(DtDevice_GetIoConfig(Device, NULL, 0));

    DtDevice_Free(Device);
}

// Every entry is checked before any is sent, so a bad entry anywhere leaves the card as
// it was; so does a list the driver refuses.
DT_TEST(ABadEntryRefusesTheWholeList)
{
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    DT_ASSERT_OK(DtDevice_SetToInput(Device, 1));
    DtIoConfig List[2] = {
        {1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT, {-1, -1}},
        {0, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT, {-1, -1}},
    };
    int SubValue;
    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, List, 2), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(Direction(1, &SubValue), DTAPI_IOCONFIG_INPUT);

    List[1].Port = 2;
    List[1].SubValue = -1;
    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, List, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Direction(1, &SubValue), DTAPI_IOCONFIG_INPUT);

    // Valid for the library, not for the card: the last port has no output.
    List[1].Port = SIM_PORT_COUNT;
    List[1].SubValue = DTAPI_IOCONFIG_OUTPUT;
    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, List, 2), DTAPI_E_CONFIG);
    DT_ASSERT_EQ(Direction(1, &SubValue), DTAPI_IOCONFIG_INPUT);

    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, List, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, NULL, 1), DTAPI_E_INVALID_ARG);

    DtDevice_Free(Device);
}

// DtDevice_GetIoConfig's checks, per entry and in its order: the port, the group, and
// whether the port has a capability of the group. A failure leaves the values -1.
DT_TEST(ReadingIsCheckedFirst)
{
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    DtIoConfig List[2] = {
        {1, DTAPI_IOCONFIG_IODIR, 5, 5, {5, 5}},
        {0, DTAPI_IOCONFIG_IODIR, 5, 5, {5, 5}},
    };
    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, List, 2), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(List[0].Value, -1);
    DT_ASSERT_EQ(List[0].SubValue, -1);
    DT_ASSERT_EQ(List[0].ParXtra[0], -1);
    DT_ASSERT_EQ(List[0].ParXtra[1], -1);

    List[1].Port = 1;
    List[1].Group = DTAPI_IOCONFIG_INPUT; // A value, not a group
    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, List, 2), DTAPI_E_INVALID_ARG);
    List[1].Group = DTAPI_IOCONFIG_TRUE; // What a capability is set to, not a group
    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, List, 2), DTAPI_E_INVALID_ARG);

    // The emulated DTA-2178 has no transport-stream rate selection.
    List[1].Group = DTAPI_IOCONFIG_TSRATESEL;
    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, List, 2), DTAPI_E_NOT_SUPPORTED);

    // A boolean I/O capability is a group of its own.
    List[1].Group = DTAPI_IOCONFIG_DMATESTMODE;
    DT_ASSERT_OK(DtDevice_GetIoConfig(Device, List, 2));

    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, List, -1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, NULL, 1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtDevice_Detach(Device));
    DT_ASSERT_EQ(DtDevice_GetIoConfig(Device, List, 2), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtDevice_SetIoConfig(Device, List, 2), DTAPI_E_NOT_ATTACHED);

    DtDevice_Free(Device);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(TimeOfDayComesFromTheCard)
{
    DtTimeOfDay Tod = {0, 0};
    DtDevice* Device = AttachSim(DtFailures);

    if (Device == NULL)
        return;

    time_t Before = time(NULL);
    DT_ASSERT_OK(DtDevice_GetTimeOfDay(Device, &Tod));
    time_t After = time(NULL);
    DT_ASSERT((time_t)Tod.Seconds + 1 >= Before);
    DT_ASSERT((time_t)Tod.Seconds <= After + 1);
    DT_ASSERT(Tod.Nanoseconds < 1000000000U);

    // Both parts come through: of a few readings, not every one is a whole second.
    {
        for (int Tries = 0; Tod.Nanoseconds == 0 && Tries < 5; Tries++)
            DtDevice_GetTimeOfDay(Device, &Tod);
        DT_ASSERT(Tod.Nanoseconds != 0);
    }

    // A failure leaves zero, not a stale time.
    SimDtPcie_FailWithStatus(DT_FUNC_CODE_TOD_CMD, DT_STATUS_BUSY);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, &Tod), DTAPI_E_BUSY);
    DT_ASSERT_EQ(Tod.Seconds, 0);
    DT_ASSERT_EQ(Tod.Nanoseconds, 0);

    DtDevice_Free(Device);
}

DT_TEST_MAIN("SimApi", DT_RUN(ScanCountsThePorts), DT_RUN(ScanDescribesEveryPort),
             DT_RUN(ScanFillsTheRestOfTheArray),
             DT_RUN(ScanWithTooSmallAnArrayChangesNothing),
             DT_RUN(ScanNeedsRoomForEveryPort), DT_RUN(DevicesAreFoundAtAnyIndex),
             DT_RUN(ScanLeavesOutDevicesItCannotAttach),
             DT_RUN(ScanSurvivesAllocationFailure), DT_RUN(AttachAndDetach),
             DT_RUN(FreepDetachesAndClears), DT_RUN(UnknownSerialIsNoSuchDevice),
             DT_RUN(OldDriverIsIncompatible), DT_RUN(FirmwareStatusIsAWarning),
             DT_RUN(UnreadableDeviceIsNoSuchDevice),
             DT_RUN(PublicPortsComeFromMainPortCount),
             DT_RUN(UnreadableCapabilityIsAbsent), DT_RUN(EachSdiRateMakesAnSdiPort),
             DT_RUN(AsiAndSdiNeedADirection), DT_RUN(AttachSurvivesAllocationFailure),
             DT_RUN(DirectionsReachTheCard), DT_RUN(ConfigurationIsCheckedFirst),
             DT_RUN(ListsAreSetAndReadInOneRequest), DT_RUN(ABadEntryRefusesTheWholeList),
             DT_RUN(ReadingIsCheckedFirst), DT_RUN(TimeOfDayComesFromTheCard))

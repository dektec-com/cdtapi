// #*#*#*#*#*#*#*#*#*#*#*#*# TestSimDeviceScan.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtapiDeviceScan and the device descriptor against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it left open and no allocation left behind.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // Public API under test.
#include "Core/DtAlloc.h"           // Allocation balance.
#include "DtPcieAbi.h"              // Driver statuses, function codes, DT_FWSTATUS_.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // DT_MAX_DEVICES and direct handles.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Resets the emulator and checks that it is what the process talks to. Returns false,
// having recorded a failure, when it is not.
static bool StartSim(int* DtFailures)
{
    OsDrv* Drv;

    SimDtPcieReset();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrvClose(Drv);
        return false;
    }
    OsDrvClose(Drv);
    return true;
}

// Scans into a single descriptor, which must be the emulated card. Returns false, having
// recorded a failure, when the scan does not find exactly that one device.
static bool ScanOne(DtDeviceDesc* Desc, int* DtFailures)
{
    int Count = -1;
    unsigned int Result;

    memset(Desc, 0xA5, sizeof(*Desc));
    Result = DtapiDeviceScan(1, &Count, Desc);
    if (Result != DTAPI_OK || Count != 1)
    {
        printf("    FAIL: scan gave 0x%X with %d devices\n", Result, Count);
        (*DtFailures)++;
        return false;
    }
    return true;
}

// Makes the port at PortIndex have exactly the capabilities for its direction given. Uses
// three of the emulator's eight overrides.
static void SetDirectionCaps(int PortIndex, bool Input, bool Output, bool Ip)
{
    SimDtPcieOverrideProperty("CAP_INPUT", PortIndex, true, Input ? 1 : 0);
    SimDtPcieOverrideProperty("CAP_OUTPUT", PortIndex, true, Output ? 1 : 0);
    SimDtPcieOverrideProperty("CAP_IP", PortIndex, true, Ip ? 1 : 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Arguments +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(RefusesBadArguments)
{
    DtDeviceDesc Desc;
    int Count = 7;

    if (!StartSim(DtFailures))
        return;

    DT_ASSERT_EQ(DtapiDeviceScan(1, NULL, &Desc), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtapiDeviceScan(-1, &Count, &Desc), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtapiDeviceScan(1, &Count, NULL), DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(Count, 7);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

// Asking with no array returns the count and says the array is too small.
DT_TEST(CountsWithoutAnArray)
{
    int Count = -1;
    int Live = DtAllocLive();

    if (!StartSim(DtFailures))
        return;

    DT_ASSERT_EQ(DtapiDeviceScan(0, &Count, NULL), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, 1);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DT_ASSERT_EQ(DtAllocLive(), Live);
}

// A zero-sized array that is not NULL is also allowed, and left alone.
DT_TEST(ZeroEntriesLeavesTheArrayAlone)
{
    DtDeviceDesc Desc;
    DtDeviceDesc Before;
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    memset(&Desc, 0x5A, sizeof(Desc));
    Before = Desc;
    DT_ASSERT_EQ(DtapiDeviceScan(0, &Count, &Desc), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Count, 1);
    DT_ASSERT_MEM(&Desc, &Before, sizeof(Desc));
}

// Entries beyond the devices found are not written.
DT_TEST(LeavesSpareEntriesAlone)
{
    DtDeviceDesc Descs[3];
    DtDeviceDesc Spare;
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    memset(Descs, 0x5A, sizeof(Descs));
    Spare = Descs[1];
    DT_ASSERT_OK(DtapiDeviceScan(3, &Count, Descs));
    DT_ASSERT_EQ(Count, 1);
    DT_ASSERT_EQ(Descs[0].Serial, SIM_SERIAL);
    DT_ASSERT_MEM(&Descs[1], &Spare, sizeof(Spare));
    DT_ASSERT_MEM(&Descs[2], &Spare, sizeof(Spare));
}

// No device, or only one whose driver is too old, is no error: a full scan finds nothing.
DT_TEST(NothingFoundIsNoError)
{
    DtDeviceDesc Desc;
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    SimDtPcieSetIndex(DT_MAX_DEVICES);
    DT_ASSERT_OK(DtapiDeviceScan(1, &Count, &Desc));
    DT_ASSERT_EQ(Count, 0);

    SimDtPcieReset();
    SimDtPcieSetDriverVersion(1, 3, 0, 0);
    Count = -1;
    DT_ASSERT_OK(DtapiDeviceScan(1, &Count, &Desc));
    DT_ASSERT_EQ(Count, 0);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

// The device is found past the indices before it.
DT_TEST(FindsTheDeviceAtALaterIndex)
{
    DtDeviceDesc Desc;

    if (!StartSim(DtFailures))
        return;

    SimDtPcieSetIndex(DT_MAX_DEVICES - 1);
    if (!ScanOne(&Desc, DtFailures))
        return;
    DT_ASSERT_EQ(Desc.Serial, SIM_SERIAL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Descriptor +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Every field of the emulated card. Ports 1 to 8 can be both, and start with the odd ones
// as inputs; ports 9 and 10 can be neither and have no direction.
DT_TEST(DescribesTheCard)
{
    static const unsigned char Zero[sizeof(((DtDeviceDesc*)0)->IpV6)] = {0};
    DtDeviceDesc Desc;
    int Live = DtAllocLive();

    if (!StartSim(DtFailures) || !ScanOne(&Desc, DtFailures))
        return;

    DT_ASSERT_EQ(Desc.Category, DTAPI_CAT_PCI);
    DT_ASSERT_EQ(Desc.Serial, SIM_SERIAL);
    DT_ASSERT_EQ(Desc.PciBusNumber, SIM_BUS_NUMBER);
    DT_ASSERT_EQ(Desc.SlotNumber, SIM_SLOT_NUMBER);
    DT_ASSERT_EQ(Desc.UsbAddress, 0);
    DT_ASSERT_EQ(Desc.TypeNumber, SIM_TYPE_NUMBER);
    DT_ASSERT_EQ(Desc.SubType, 0);
    DT_ASSERT_EQ(Desc.DeviceId, SIM_DEVICE_ID);
    DT_ASSERT_EQ(Desc.VendorId, SIM_VENDOR_ID);
    DT_ASSERT_EQ(Desc.SubsystemId, SIM_SUBSYSTEM_ID);
    DT_ASSERT_EQ(Desc.SubVendorId, SIM_SUBSYSTEM_VENDOR_ID);
    DT_ASSERT_EQ(Desc.NumHwFuncs, SIM_PORT_COUNT);
    DT_ASSERT_EQ(Desc.HardwareRevision, SIM_HARDWARE_REVISION);
    DT_ASSERT_EQ(Desc.FirmwareVersion, SIM_FIRMWARE_VERSION);
    DT_ASSERT_EQ(Desc.FirmwareVariant, SIM_FIRMWARE_VARIANT);
    DT_ASSERT_EQ(Desc.FirmwareStatus, DTAPI_FWSTATUS_UPTODATE);
    DT_ASSERT_EQ(Desc.FwBuildDate.Year, SIM_FW_BUILD_YEAR);
    DT_ASSERT_EQ(Desc.FwBuildDate.Month, SIM_FW_BUILD_MONTH);
    DT_ASSERT_EQ(Desc.FwBuildDate.Day, SIM_FW_BUILD_DAY);
    DT_ASSERT_EQ(Desc.FwBuildDate.Hour, SIM_FW_BUILD_HOUR);
    DT_ASSERT_EQ(Desc.FwBuildDate.Minute, SIM_FW_BUILD_MINUTE);
    DT_ASSERT_EQ(Desc.NumDtInpChan, SIM_SDI_PORT_COUNT / 2);
    DT_ASSERT_EQ(Desc.NumDtOutpChan, SIM_SDI_PORT_COUNT / 2);
    DT_ASSERT_EQ(Desc.NumPorts, SIM_PORT_COUNT);
    DT_ASSERT_MEM(Desc.Ip, Zero, sizeof(Desc.Ip));
    DT_ASSERT_MEM(Desc.IpV6, Zero, sizeof(Desc.IpV6));
    DT_ASSERT_MEM(Desc.MacAddr, Zero, sizeof(Desc.MacAddr));
    DT_ASSERT_EQ(Desc.PcieNumLanes, SIM_PCIE_NUM_LANES);
    DT_ASSERT_EQ(Desc.PcieMaxLanes, SIM_PCIE_MAX_LANES);
    DT_ASSERT_EQ(Desc.PcieLinkSpeed, SIM_PCIE_LINK_SPEED);
    DT_ASSERT_EQ(Desc.PcieMaxSpeed, SIM_PCIE_MAX_SPEED);
    DT_ASSERT_EQ(Desc.PcieMaxPayloadSize, SIM_PCIE_MAX_PAYLOAD_SIZE);
    DT_ASSERT_EQ(Desc.PcieMaxReadRequestSize, SIM_PCIE_MAX_READ_REQUEST_SIZE);
    DT_ASSERT_EQ(Desc.PcieMaxSlotPower, SIM_PCIE_MAX_SLOT_POWER);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
    DT_ASSERT_EQ(DtAllocLive(), Live);
}

// A driver without GET_DEV_INFO2 describes the same device, without the slot power.
DT_TEST(OlderDeviceInfoHasNoSlotPower)
{
    DtDeviceDesc Desc;

    if (!StartSim(DtFailures))
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO2, DT_STATUS_NOT_SUPPORTED);
    if (!ScanOne(&Desc, DtFailures))
        return;
    DT_ASSERT_EQ(Desc.Serial, SIM_SERIAL);
    DT_ASSERT_EQ(Desc.PciBusNumber, SIM_BUS_NUMBER);
    DT_ASSERT_EQ(Desc.PcieMaxReadRequestSize, SIM_PCIE_MAX_READ_REQUEST_SIZE);
    DT_ASSERT_EQ(Desc.FwBuildDate.Minute, SIM_FW_BUILD_MINUTE);
    DT_ASSERT_EQ(Desc.PcieMaxSlotPower, 0);
}

// The driver's firmware status numbers are DTAPI's; a value outside them is undefined.
DT_TEST(ConvertsTheFirmwareStatus)
{
    static const struct
    {
        int Driver;
        DtFirmwareStatus Dtapi;
    } Cases[] = {
        {DT_FWSTATUS_UNDEFINED, DTAPI_FWSTATUS_UNDEFINED},
        {DT_FWSTATUS_UPTODATE, DTAPI_FWSTATUS_UPTODATE},
        {DT_FWSTATUS_BETA, DTAPI_FWSTATUS_BETA},
        {DT_FWSTATUS_OLD, DTAPI_FWSTATUS_OLD},
        {DT_FWSTATUS_NEW, DTAPI_FWSTATUS_NEW},
        {DT_FWSTATUS_TAINTED, DTAPI_FWSTATUS_TAINTED},
        {DT_FWSTATUS_OBSOLETE, DTAPI_FWSTATUS_OBSOLETE},
        {6, DTAPI_FWSTATUS_UNDEFINED},
        {-2, DTAPI_FWSTATUS_UNDEFINED},
    };
    DtDeviceDesc Desc;
    size_t i;

    if (!StartSim(DtFailures))
        return;

    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        SimDtPcieSetFirmwareStatus(Cases[i].Driver);
        if (!ScanOne(&Desc, DtFailures))
            return;
        DT_ASSERT_EQ(Desc.FirmwareStatus, Cases[i].Dtapi);
    }
}

// A port that can be both counts by its current direction.
DT_TEST(CountsByTheCurrentDirection)
{
    DtDevice* Device = NULL;
    DtDeviceDesc Desc;

    if (!StartSim(DtFailures))
        return;

    Device = DtDevice_Alloc();
    DT_ASSERT(Device != NULL);
    DT_ASSERT_OK(DtDevice_AttachToSerial(Device, SIM_SERIAL));
    DT_ASSERT_OK(DtDevice_SetToInput(Device, 2));
    DT_ASSERT_OK(DtDevice_SetToInput(Device, 4));
    DT_ASSERT_OK(DtDevice_SetToOutput(Device, 1));
    DtDevice_Free(Device);

    if (!ScanOne(&Desc, DtFailures))
        return;
    DT_ASSERT_EQ(Desc.NumDtInpChan, SIM_SDI_PORT_COUNT / 2 + 1);
    DT_ASSERT_EQ(Desc.NumDtOutpChan, SIM_SDI_PORT_COUNT / 2 - 1);
}

// A port that is only an input or only an output counts as such whatever its direction,
// and an IP port counts as both.
DT_TEST(CountsByCapabilitiesFirst)
{
    DtDeviceDesc Desc;

    if (!StartSim(DtFailures))
        return;

    // Port 2 is an output, but can only be an input.
    SetDirectionCaps(1, true, false, false);
    // Port 1 is an input, but can only be an output.
    SetDirectionCaps(0, false, true, false);
    // Port 9 has no direction, but is an IP port. Its other capabilities stay as they
    // are, since the emulator holds eight overrides.
    SimDtPcieOverrideProperty("CAP_IP", 8, true, 1);

    if (!ScanOne(&Desc, DtFailures))
        return;
    DT_ASSERT_EQ(Desc.NumDtInpChan, SIM_SDI_PORT_COUNT / 2 + 1);
    DT_ASSERT_EQ(Desc.NumDtOutpChan, SIM_SDI_PORT_COUNT / 2 + 1);
}

// When a direction cannot be read, counting stops there and the scan still succeeds.
DT_TEST(StopsCountingWhenADirectionFails)
{
    DtDeviceDesc Desc;

    if (!StartSim(DtFailures))
        return;

    // Port 1 counts by its capabilities; port 2 is the first whose direction is read.
    SetDirectionCaps(0, true, false, false);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_FAIL);

    if (!ScanOne(&Desc, DtFailures))
        return;
    DT_ASSERT_EQ(Desc.NumDtInpChan, 1);
    DT_ASSERT_EQ(Desc.NumDtOutpChan, 0);
    DT_ASSERT_EQ(Desc.Serial, SIM_SERIAL);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

// A device whose identity cannot be read is left out.
DT_TEST(LeavesOutADeviceThatCannotBeRead)
{
    DtDeviceDesc Desc;
    int Count = -1;

    if (!StartSim(DtFailures))
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO2, DT_STATUS_FAIL);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO, DT_STATUS_FAIL);
    DT_ASSERT_OK(DtapiDeviceScan(1, &Count, &Desc));
    DT_ASSERT_EQ(Count, 0);
    DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Types +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The fields are in DTAPI's order, and the address fields have DTAPI's sizes.
DT_TEST(TypesFollowDtapi)
{
    static const size_t Offsets[] = {
        offsetof(DtDeviceDesc, Category),
        offsetof(DtDeviceDesc, Serial),
        offsetof(DtDeviceDesc, PciBusNumber),
        offsetof(DtDeviceDesc, SlotNumber),
        offsetof(DtDeviceDesc, UsbAddress),
        offsetof(DtDeviceDesc, TypeNumber),
        offsetof(DtDeviceDesc, SubType),
        offsetof(DtDeviceDesc, DeviceId),
        offsetof(DtDeviceDesc, VendorId),
        offsetof(DtDeviceDesc, SubsystemId),
        offsetof(DtDeviceDesc, SubVendorId),
        offsetof(DtDeviceDesc, NumHwFuncs),
        offsetof(DtDeviceDesc, HardwareRevision),
        offsetof(DtDeviceDesc, FirmwareVersion),
        offsetof(DtDeviceDesc, FirmwareVariant),
        offsetof(DtDeviceDesc, FirmwareStatus),
        offsetof(DtDeviceDesc, FwBuildDate),
        offsetof(DtDeviceDesc, NumDtInpChan),
        offsetof(DtDeviceDesc, NumDtOutpChan),
        offsetof(DtDeviceDesc, NumPorts),
        offsetof(DtDeviceDesc, Ip),
        offsetof(DtDeviceDesc, IpV6),
        offsetof(DtDeviceDesc, MacAddr),
        offsetof(DtDeviceDesc, PcieNumLanes),
        offsetof(DtDeviceDesc, PcieMaxLanes),
        offsetof(DtDeviceDesc, PcieLinkSpeed),
        offsetof(DtDeviceDesc, PcieMaxSpeed),
        offsetof(DtDeviceDesc, PcieMaxPayloadSize),
        offsetof(DtDeviceDesc, PcieMaxReadRequestSize),
        offsetof(DtDeviceDesc, PcieMaxSlotPower),
    };
    DtDeviceDesc Desc;
    size_t i;

    for (i = 1; i < sizeof(Offsets) / sizeof(Offsets[0]); i++)
        DT_ASSERT(Offsets[i] > Offsets[i - 1]);

    DT_ASSERT_EQ(sizeof(Desc.Serial), 8);
    DT_ASSERT_EQ(sizeof(Desc.Ip), 4);
    DT_ASSERT_EQ(sizeof(Desc.IpV6), 3 * 16);
    DT_ASSERT_EQ(sizeof(Desc.MacAddr), 6);
    DT_ASSERT_EQ(DTAPI_CAT_ALL, -1);
    DT_ASSERT_EQ(DTAPI_CAT_PCI, 0);
    DT_ASSERT_EQ(DTAPI_CAT_NWAP, 5);
}

DT_TEST_MAIN("SimDeviceScan", DT_RUN(RefusesBadArguments), DT_RUN(CountsWithoutAnArray),
             DT_RUN(ZeroEntriesLeavesTheArrayAlone), DT_RUN(LeavesSpareEntriesAlone),
             DT_RUN(NothingFoundIsNoError), DT_RUN(FindsTheDeviceAtALaterIndex),
             DT_RUN(DescribesTheCard), DT_RUN(OlderDeviceInfoHasNoSlotPower),
             DT_RUN(ConvertsTheFirmwareStatus), DT_RUN(CountsByTheCurrentDirection),
             DT_RUN(CountsByCapabilitiesFirst), DT_RUN(StopsCountingWhenADirectionFails),
             DT_RUN(LeavesOutADeviceThatCannotBeRead), DT_RUN(TypesFollowDtapi))

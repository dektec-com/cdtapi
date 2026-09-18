// #*#*#*#*#*#*#*#*#*#*#*#*# DtListDeviceDescs.c *#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Example: describes every DekTec device, one field per line
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Uses DtapiDeviceScan, which describes devices rather than ports, so this program
// is built against cdtapi.h only. Prints one block per device, every field of its
// descriptor by name, then the number of devices:
//
//     Device 1
//       Category: 0
//       Serial: 9217800001
//       ...
//       PcieMaxSlotPower: 25000
//     1 devices
//
// The lines are those Scripts/Compare/DtapiDeviceScanRef.cpp prints from DTAPI, so that
// the two can be compared line by line. Exits with 0 when devices are found, 2 when there
// are none, and 1 when the scan fails.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>

// Example includes
#include "Common/ExampleCommon.h" // The API and what the examples share.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static const ExampleOption g_Options[] = {
    {"--serial", true, "Describe only the device with this serial number"},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PrintDevice(int Number, const DtDeviceDesc* Desc)
{
    printf("Device %d\n", Number);
    printf("  Category: %d\n", Desc->Category);
    printf("  Serial: %lld\n", (long long)Desc->Serial);
    printf("  PciBusNumber: %d\n", Desc->PciBusNumber);
    printf("  SlotNumber: %d\n", Desc->SlotNumber);
    printf("  UsbAddress: %d\n", Desc->UsbAddress);
    printf("  TypeNumber: %d\n", Desc->TypeNumber);
    printf("  SubType: %d\n", Desc->SubType);
    printf("  DeviceId: 0x%04X\n", (unsigned)Desc->DeviceId);
    printf("  VendorId: 0x%04X\n", (unsigned)Desc->VendorId);
    printf("  SubsystemId: 0x%04X\n", (unsigned)Desc->SubsystemId);
    printf("  SubVendorId: 0x%04X\n", (unsigned)Desc->SubVendorId);
    printf("  NumHwFuncs: %d\n", Desc->NumHwFuncs);
    printf("  HardwareRevision: %d\n", Desc->HardwareRevision);
    printf("  FirmwareVersion: %d\n", Desc->FirmwareVersion);
    printf("  FirmwareVariant: %d\n", Desc->FirmwareVariant);
    printf("  FirmwareStatus: %d\n", (int)Desc->FirmwareStatus);
    printf("  FwBuildDate: %04d-%02d-%02d %02d:%02d\n", Desc->FwBuildDate.Year,
           Desc->FwBuildDate.Month, Desc->FwBuildDate.Day, Desc->FwBuildDate.Hour,
           Desc->FwBuildDate.Minute);
    printf("  NumDtInpChan: %d\n", Desc->NumDtInpChan);
    printf("  NumDtOutpChan: %d\n", Desc->NumDtOutpChan);
    printf("  NumPorts: %d\n", Desc->NumPorts);
    printf("  PcieNumLanes: %d\n", Desc->PcieNumLanes);
    printf("  PcieMaxLanes: %d\n", Desc->PcieMaxLanes);
    printf("  PcieLinkSpeed: %d\n", Desc->PcieLinkSpeed);
    printf("  PcieMaxSpeed: %d\n", Desc->PcieMaxSpeed);
    printf("  PcieMaxPayloadSize: %d\n", Desc->PcieMaxPayloadSize);
    printf("  PcieMaxReadRequestSize: %d\n", Desc->PcieMaxReadRequestSize);
    printf("  PcieMaxSlotPower: %d\n", Desc->PcieMaxSlotPower);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtapiDeviceScan with no room reports how many devices there are, with
// DTAPI_E_BUF_TOO_SMALL; the second call fills a buffer of that size.
//
int main(int Argc, char** Argv)
{
    int64_t Serial = 0;
    int Count = 0;
    int Listed = 0;

    if (!Example_CheckArguments(Argc, Argv, "Describes every DekTec device.", g_Options,
                                (int)(sizeof(g_Options) / sizeof(g_Options[0]))) ||
        !Example_Int64(Argc, Argv, "--serial", &Serial))
    {
        return EXAMPLE_FAILED;
    }

    unsigned int Result = DtapiDeviceScan(0, &Count, NULL);
    if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
        return Example_Failed("DtapiDeviceScan", Result);

    DtDeviceDesc* Descs =
        (DtDeviceDesc*)calloc(Count > 0 ? (size_t)Count : 1, sizeof(DtDeviceDesc));
    if (Descs == NULL)
        return Example_Failed("calloc", DTAPI_E_OUT_OF_MEM);

    Result = DtapiDeviceScan(Count, &Count, Descs);
    if (Result != DTAPI_OK)
    {
        free(Descs);
        return Example_Failed("DtapiDeviceScan", Result);
    }

    for (int i = 0; i < Count; i++)
    {
        if (Serial != 0 && Descs[i].Serial != Serial)
            continue;
        PrintDevice(++Listed, &Descs[i]);
    }
    printf("%d devices\n", Listed);

    free(Descs);
    return Listed > 0 ? EXAMPLE_OK : EXAMPLE_NOTHING;
}

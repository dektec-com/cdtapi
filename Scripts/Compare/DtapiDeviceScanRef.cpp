// #*#*#*#*#*#*#*#*#*#*#*# DtapiDeviceScanRef.cpp *#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DTAPI's own device scan, printed as DtListDeviceDescs prints CDTAPI's
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A comparison tool, not part of the library: it is C++ because DTAPI is, and it builds
// only where DTAPI's object file is, with Scripts/compare_device_scan.sh.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <cstdio>
#include <vector>

// DTAPI includes
#include "DTAPI.h"

using namespace Dtapi;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrintDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The lines and their order must stay those of DtListDeviceDescs.
//
static void PrintDevice(int Number, const DtDeviceDesc& Desc)
{
    printf("Device %d\n", Number);
    printf("  Category: %d\n", Desc.m_Category);
    printf("  Serial: %lld\n", (long long)Desc.m_Serial);
    printf("  PciBusNumber: %d\n", Desc.m_PciBusNumber);
    printf("  SlotNumber: %d\n", Desc.m_SlotNumber);
    printf("  UsbAddress: %d\n", Desc.m_UsbAddress);
    printf("  TypeNumber: %d\n", Desc.m_TypeNumber);
    printf("  SubType: %d\n", Desc.m_SubType);
    printf("  DeviceId: 0x%04X\n", Desc.m_DeviceId);
    printf("  VendorId: 0x%04X\n", Desc.m_VendorId);
    printf("  SubsystemId: 0x%04X\n", Desc.m_SubsystemId);
    printf("  SubVendorId: 0x%04X\n", Desc.m_SubVendorId);
    printf("  NumHwFuncs: %d\n", Desc.m_NumHwFuncs);
    printf("  HardwareRevision: %d\n", Desc.m_HardwareRevision);
    printf("  FirmwareVersion: %d\n", Desc.m_FirmwareVersion);
    printf("  FirmwareVariant: %d\n", Desc.m_FirmwareVariant);
    printf("  FirmwareStatus: %d\n", (int)Desc.m_FirmwareStatus);
    printf("  FwBuildDate: %04d-%02d-%02d %02d:%02d\n", Desc.m_FwBuildDate.m_Year,
           Desc.m_FwBuildDate.m_Month, Desc.m_FwBuildDate.m_Day,
           Desc.m_FwBuildDate.m_Hour, Desc.m_FwBuildDate.m_Minute);
    printf("  NumDtInpChan: %d\n", Desc.m_NumDtInpChan);
    printf("  NumDtOutpChan: %d\n", Desc.m_NumDtOutpChan);
    printf("  NumPorts: %d\n", Desc.m_NumPorts);
    printf("  PcieNumLanes: %d\n", Desc.m_PcieNumLanes);
    printf("  PcieMaxLanes: %d\n", Desc.m_PcieMaxLanes);
    printf("  PcieLinkSpeed: %d\n", Desc.m_PcieLinkSpeed);
    printf("  PcieMaxSpeed: %d\n", Desc.m_PcieMaxSpeed);
    printf("  PcieMaxPayloadSize: %d\n", Desc.m_PcieMaxPayloadSize);
    printf("  PcieMaxReadRequestSize: %d\n", Desc.m_PcieMaxReadRequestSize);
    printf("  PcieMaxSlotPower: %d\n", Desc.m_PcieMaxSlotPower);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- main -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int main()
{
    int Count = 0;
    DTAPI_RESULT Result = DtapiDeviceScan(0, Count, nullptr);

    if (Result != DTAPI_OK && Result != DTAPI_E_BUF_TOO_SMALL)
    {
        printf("DtapiDeviceScan: %s\n", DtapiResult2Str(Result));
        return 1;
    }

    std::vector<DtDeviceDesc> Descs(Count > 0 ? Count : 1);
    Result = DtapiDeviceScan(Count, Count, Descs.data());
    if (Result != DTAPI_OK)
    {
        printf("DtapiDeviceScan: %s\n", DtapiResult2Str(Result));
        return 1;
    }

    // Only the categories CDTAPI scans.
    int Number = 0;
    for (int i = 0; i < Count; i++)
    {
        if (Descs[i].m_Category == DTAPI_CAT_PCI)
            PrintDevice(++Number, Descs[i]);
    }
    printf("%d devices\n", Number);
    return Number > 0 ? 0 : 2;
}

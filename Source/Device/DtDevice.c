// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevice.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Device layer: DtDevice and the hardware function scan - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "Core/DtVec.h"   // The scan's list of hardware functions.
#include "DtAvInput.h"    // Video standard detection.
#include "DtDevice.h"     // Interface being implemented.
#include "DtIoConfig.h"   // I/O configuration validation.
#include "DtPcieAbi.h"    // DT_FWSTATUS_ values.
#include "OAL/OsThread.h" // Sleeping and the clock while waiting for a signal.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The capability properties read per port, and the flag each sets.
static const struct
{
    const char* Name;
    uint32_t Flag;
} g_PortCaps[] = {
    {"CAP_12GSDI", DT_CAP_12GSDI},
    {"CAP_3GSDI", DT_CAP_3GSDI},
    {"CAP_6GSDI", DT_CAP_6GSDI},
    {"CAP_HDSDI", DT_CAP_HDSDI},
    {"CAP_SDI", DT_CAP_SDI},
    {"CAP_AVFIFO", DT_CAP_AVFIFO},
    {"CAP_INPUT", DT_CAP_INPUT},
    {"CAP_OUTPUT", DT_CAP_OUTPUT},
    {"CAP_INTINPUT", DT_CAP_INTINPUT},
    {"CAP_MATRIX2", DT_CAP_MATRIX2},
    {"CAP_SDIRX", DT_CAP_SDIRX},
    {"CAP_HDMI", DT_CAP_HDMI},
    {"CAP_SCALE_12GTO3G", DT_CAP_SCALE_12GTO3G},
    {"CAP_IP", DT_CAP_IP},
    {"CAP_ASI", DT_CAP_ASI},
    {"CAP_MATRIX", DT_CAP_MATRIX},
    {"CAP_TS", DT_CAP_TS},
    {"CAP_HUFFMAN", DT_CAP_HUFFMAN},
    {"CAP_L3MODE", DT_CAP_L3MODE},
    {"CAP_TRPMODE", DT_CAP_TRPMODE},
    {"CAP_TIMESTAMP64", DT_CAP_TIMESTAMP64},
    {"CAP_SDI10BNBO", DT_CAP_SDI10BNBO},
    {"CAP_DMATESTMODE", DT_CAP_DMATESTMODE},
    {"CAP_FAILSAFE", DT_CAP_FAILSAFE},
    {"CAP_SPI", DT_CAP_SPI},
    {"CAP_SPISDI", DT_CAP_SPISDI},
};

#define PORT_CAP_COUNT (sizeof(g_PortCaps) / sizeof(g_PortCaps[0]))

// More public ports than any DekTec device has.
#define DT_MAX_PORTS 1024

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoadPorts -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the port counts and the capabilities of every port into Device, as Device::Init
// and Device::GetCapInfo do: PORT_COUNT is required, MAIN_PORT_COUNT falls back to it for
// an old driver, and a capability that cannot be read counts as absent. The hardware
// functions look at the public ports, detection at all of them, so the capabilities
// cover whichever count is larger. A negative or implausibly large count, which no
// driver reports, is refused rather than allocated.
//
static DtapiResult LoadPorts(DtDevice* Device, OsDrv* Drv)
{
    size_t Port, Cap, Count;
    DtapiResult Result;

    Result = DtPcieCmd_GetPropertyInt(Drv, "PORT_COUNT", DT_PROPERTY_DEVICE,
                                      &Device->NumPorts);
    if (Result != DTAPI_OK)
        return DTAPI_E_NO_SUCH_DEVICE;

    if (DtPcieCmd_GetPropertyInt(Drv, "MAIN_PORT_COUNT", DT_PROPERTY_DEVICE,
                                 &Device->NumPublicPorts) != DTAPI_OK)
    {
        Device->NumPublicPorts = Device->NumPorts;
    }

    if (Device->NumPorts < 0 || Device->NumPublicPorts < 0 ||
        Device->NumPorts > DT_MAX_PORTS || Device->NumPublicPorts > DT_MAX_PORTS)
    {
        return DTAPI_E_NO_SUCH_DEVICE;
    }

    Count = (size_t)(Device->NumPorts > Device->NumPublicPorts ? Device->NumPorts
                                                               : Device->NumPublicPorts);
    if (Count == 0)
        return DTAPI_OK;

    Device->PortCaps = (uint32_t*)DtAlloc_Malloc(Count * sizeof(uint32_t));
    if (Device->PortCaps == NULL)
        return DTAPI_E_OUT_OF_MEM;

    for (Port = 0; Port < Count; Port++)
    {
        Device->PortCaps[Port] = 0;
        for (Cap = 0; Cap < PORT_CAP_COUNT; Cap++)
        {
            bool Has = false;

            if (DtPcieCmd_GetPropertyBool(Drv, g_PortCaps[Cap].Name, (int)Port, &Has) ==
                    DTAPI_OK &&
                Has)
            {
                Device->PortCaps[Port] |= g_PortCaps[Cap].Flag;
            }
        }
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_AttachIndex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The order of the checks is DTAPI's: the driver version before the device's identity,
// so that a driver that is too old is reported as such rather than as a missing device.
//
DtapiResult DtDevice_AttachIndex(DtDevice* Device, int Index, bool MatchSerial,
                                 int64_t Serial)
{
    OsDrv* Drv = OsDrv_Open(Index);

    if (Drv == NULL)
        return DTAPI_E_NO_SUCH_DEVICE;

    memset(Device, 0, sizeof(*Device));

    DtDriverVersion Version;
    DtapiResult Result;
    if (DtPcieCmd_GetDriverVersion(Drv, &Version) != DTAPI_OK)
        Result = DTAPI_E_NO_SUCH_DEVICE;
    else if (!DtPcieCmd_VersionIsSupported(&Version))
        Result = DTAPI_E_DRIVER_INCOMP;
    else if (DtPcieCmd_GetDeviceInfo(Drv, &Device->Info) != DTAPI_OK)
        Result = DTAPI_E_NO_SUCH_DEVICE;
    else if (MatchSerial && Device->Info.Serial != Serial)
        Result = DTAPI_E_NO_SUCH_DEVICE;
    else
        Result = LoadPorts(Device, Drv);

    if (Result != DTAPI_OK)
    {
        DtAlloc_Free(Device->PortCaps);
        memset(Device, 0, sizeof(*Device));
        OsDrv_Close(Drv);
        return Result;
    }

    Device->Drv = Drv;
    Device->Index = Index;
    Device->DriverVersion = Version;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtDevice_Release(DtDevice* Device)
{
    OsDrv_Close(Device->Drv);
    DtAlloc_Free(Device->PortCaps);
    memset(Device, 0, sizeof(*Device));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hardware functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Describe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtapiDtHwFuncDesc2String, for the PCI category. DTAPI's special names for the DTA-107S2
// and DTA-110T depend on capabilities of cards the DtPcie driver does not serve, and are
// left out.
//
DtapiResult DtDevice_Describe(int TypeNumber, int SubType, int Port, char* Buf,
                              size_t Size)
{
    char SubTypeText[16] = "";

    if (Buf == NULL || Size == 0)
        return DTAPI_E_INVALID_BUF;

    Buf[0] = '\0';

    if (SubType > 0)
    {
        if (TypeNumber == 2178 && SubType == 1)
            snprintf(SubTypeText, sizeof(SubTypeText), "DTA-2178-ASI");
        else
            snprintf(SubTypeText, sizeof(SubTypeText), "%c", 'A' + SubType - 1);
    }

    char Text[64];
    int Length =
        snprintf(Text, sizeof(Text), "DTA-%d%s port %d", TypeNumber, SubTypeText, Port);

    // DTAPI refuses a string that does not leave room for its terminator.
    if (Length < 0 || (size_t)Length >= Size)
        return DTAPI_E_BUF_TOO_SMALL;

    memcpy(Buf, Text, (size_t)Length + 1);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_HwFunc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtDevice_HwFunc(const DtDevice* Device, int Port, DtHwFuncDesc* Desc)
{
    uint32_t Caps = Device->PortCaps[Port - 1];

    memset(Desc, 0, sizeof(*Desc));
    snprintf(Desc->DeviceName, sizeof(Desc->DeviceName), "%lld:%d",
             (long long)Device->Info.Serial, Port);
    DtDevice_Describe(Device->Info.TypeNumber, Device->Info.SubType, Port,
                      Desc->Description, sizeof(Desc->Description));
    Desc->SerialNumber = Device->Info.Serial;
    Desc->Port = Port;
    Desc->IsSdi = (Caps & DT_CAP_ANY_SDI) != 0;
    Desc->IsAvFifo = (Caps & DT_CAP_AVFIFO) != 0;
    Desc->IsInput = (Caps & DT_CAP_INPUT) != 0;
    Desc->IsOutput = (Caps & DT_CAP_OUTPUT) != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiHwFuncScan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DTAPI's DtapiHwFuncScan as CDTAPI calls and converts it. Every device is attached in
// turn and each of its public ports becomes a hardware function; a device that cannot
// be attached, including one whose driver is too old, is left out, as DTAPI's full
// device scan leaves it out. The descriptors are collected first and copied only when
// they all fit, because CDTAPI does not touch the caller's array on failure.
//
// On success CDTAPI converts all NumEntries descriptors, and the ones beyond the last
// port are DTAPI's value-initialised, all-zero, descriptors: "0:0", "DTA-0 port 0".
//
DtapiResult DtapiHwFuncScan(int NumEntries, int* NumEntriesResult, DtHwFuncDesc* HwFuncs)
{
    DtapiResult Result = DTAPI_OK;

    if (NumEntriesResult == NULL || NumEntries < 0)
        return DTAPI_E_INVALID_ARG;
    if (HwFuncs == NULL && NumEntries != 0)
        return DTAPI_E_INVALID_BUF;

    *NumEntriesResult = 0;
    DtVec Found;
    DtVec_Init(&Found, sizeof(DtHwFuncDesc));

    for (int Index = 0; Index < DT_MAX_DEVICES && Result == DTAPI_OK; Index++)
    {
        DtDevice Device;

        if (DtDevice_AttachIndex(&Device, Index, false, 0) != DTAPI_OK)
            continue;

        for (int Port = 1; Port <= Device.NumPublicPorts && Result == DTAPI_OK; Port++)
        {
            DtHwFuncDesc Desc;

            DtDevice_HwFunc(&Device, Port, &Desc);
            if (DtVec_Push(&Found, &Desc) != 0)
                Result = DTAPI_E_OUT_OF_MEM;
        }
        DtDevice_Release(&Device);
    }

    size_t Count = DtVec_Count(&Found);
    *NumEntriesResult = (int)Count;

    if (Result == DTAPI_OK && Count > (size_t)NumEntries)
        Result = DTAPI_E_BUF_TOO_SMALL;

    for (size_t i = 0; Result == DTAPI_OK && i < (size_t)NumEntries; i++)
    {
        if (i < Count)
            HwFuncs[i] = DT_VEC_AT(&Found, DtHwFuncDesc, i);
        else
        {
            memset(&HwFuncs[i], 0, sizeof(HwFuncs[i]));
            snprintf(HwFuncs[i].DeviceName, sizeof(HwFuncs[i].DeviceName), "0:0");
            DtDevice_Describe(0, 0, 0, HwFuncs[i].Description,
                              sizeof(HwFuncs[i].Description));
        }
    }

    DtVec_Free(&Found);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device scan +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FirmwareStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver's DT_FWSTATUS_ value as DTAPI's DtFirmwareStatus. The numbers are the same;
// a value the driver should not report is undefined, as in DtProxyCORE::GetDeviceInfo.
//
static DtFirmwareStatus FirmwareStatus(int Status)
{
    switch (Status)
    {
    case DT_FWSTATUS_UPTODATE:
        return DTAPI_FWSTATUS_UPTODATE;
    case DT_FWSTATUS_BETA:
        return DTAPI_FWSTATUS_BETA;
    case DT_FWSTATUS_OLD:
        return DTAPI_FWSTATUS_OLD;
    case DT_FWSTATUS_NEW:
        return DTAPI_FWSTATUS_NEW;
    case DT_FWSTATUS_TAINTED:
        return DTAPI_FWSTATUS_TAINTED;
    case DT_FWSTATUS_OBSOLETE:
        return DTAPI_FWSTATUS_OBSOLETE;
    default:
        return DTAPI_FWSTATUS_UNDEFINED;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_DescribeDevice -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only the public ports count. A port that is only an input or only an output counts as
// such, an IP port as both, and any other port by its I/O direction; DTAPI stops counting
// at the first port whose direction cannot be read.
//
void DtDevice_DescribeDevice(const DtDevice* Device, DtDeviceDesc* Desc)
{
    const DtDeviceInfo* Info = &Device->Info;

    memset(Desc, 0, sizeof(*Desc));
    Desc->Category = DTAPI_CAT_PCI;
    Desc->Serial = Info->Serial;
    Desc->PciBusNumber = Info->BusNumber;
    Desc->SlotNumber = Info->SlotNumber;
    Desc->TypeNumber = Info->TypeNumber;
    Desc->SubType = Info->SubType;
    Desc->DeviceId = Info->DeviceId;
    Desc->VendorId = Info->VendorId;
    Desc->SubsystemId = Info->SubSystemId;
    Desc->SubVendorId = Info->SubVendorId;
    Desc->NumHwFuncs = Device->NumPublicPorts;
    Desc->HardwareRevision = Info->HardwareRevision;
    Desc->FirmwareVersion = Info->FirmwareVersion;
    Desc->FirmwareVariant = Info->FirmwareVariant;
    Desc->FirmwareStatus = FirmwareStatus(Info->FirmwareStatus);
    Desc->FwBuildDate.Year = Info->FwBuildYear;
    Desc->FwBuildDate.Month = Info->FwBuildMonth;
    Desc->FwBuildDate.Day = Info->FwBuildDay;
    Desc->FwBuildDate.Hour = Info->FwBuildHour;
    Desc->FwBuildDate.Minute = Info->FwBuildMinute;
    Desc->NumPorts = Device->NumPublicPorts;
    Desc->PcieNumLanes = Info->PcieNumLanes;
    Desc->PcieMaxLanes = Info->PcieMaxLanes;
    Desc->PcieLinkSpeed = Info->PcieLinkSpeed;
    Desc->PcieMaxSpeed = Info->PcieMaxSpeed;
    Desc->PcieMaxPayloadSize = Info->PcieMaxPayloadSize;
    Desc->PcieMaxReadRequestSize = Info->PcieMaxReadRequestSize;
    Desc->PcieMaxSlotPower = Info->PcieMaxSlotPower;

    for (int Port = 1; Port <= Device->NumPublicPorts; Port++)
    {
        uint32_t Direction = Device->PortCaps[Port - 1] & (DT_CAP_INPUT | DT_CAP_OUTPUT);

        if (Direction == DT_CAP_INPUT)
            Desc->NumDtInpChan++;
        else if (Direction == DT_CAP_OUTPUT)
            Desc->NumDtOutpChan++;
        else if ((Device->PortCaps[Port - 1] & DT_CAP_IP) != 0)
        {
            Desc->NumDtInpChan++;
            Desc->NumDtOutpChan++;
        }
        else
        {
            DtIoConfig Config;
            memset(&Config, 0, sizeof(Config));
            Config.Port = Port;
            Config.Group = DTAPI_IOCONFIG_IODIR;
            if (DtPcieCmd_GetIoConfig(Device->Drv, &Config) != DTAPI_OK)
                break;
            if (Config.Value == DTAPI_IOCONFIG_INPUT)
                Desc->NumDtInpChan++;
            else if (Config.Value == DTAPI_IOCONFIG_OUTPUT)
                Desc->NumDtOutpChan++;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiDeviceScan -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// PcieDevice::DeviceScan: every device that can be attached counts, and its descriptor
// is written while the array has room.
//
DtapiResult DtapiDeviceScan(int NumEntries, int* NumEntriesResult,
                            DtDeviceDesc* DvcDescArr)
{
    if (NumEntriesResult == NULL || NumEntries < 0)
        return DTAPI_E_INVALID_ARG;
    if (DvcDescArr == NULL && NumEntries != 0)
        return DTAPI_E_INVALID_BUF;

    *NumEntriesResult = 0;
    for (int Index = 0; Index < DT_MAX_DEVICES; Index++)
    {
        DtDevice Device;

        if (DtDevice_AttachIndex(&Device, Index, false, 0) != DTAPI_OK)
            continue;

        if (*NumEntriesResult < NumEntries)
            DtDevice_DescribeDevice(&Device, &DvcDescArr[*NumEntriesResult]);
        (*NumEntriesResult)++;
        DtDevice_Release(&Device);
    }

    return *NumEntriesResult > NumEntries ? DTAPI_E_BUF_TOO_SMALL : DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtDevice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtDevice* DtDevice_Alloc(void)
{
    DtDevice* Device = (DtDevice*)DtAlloc_Malloc(sizeof(DtDevice));

    if (Device != NULL)
        memset(Device, 0, sizeof(*Device));
    return Device;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtDevice_Free(DtDevice* Device)
{
    if (Device == NULL)
        return;

    DtDevice_Release(Device);
    DtAlloc_Free(Device);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtDevice_Freep(DtDevice** Device)
{
    if (Device == NULL)
        return;

    DtDevice_Free(*Device);
    *Device = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_AttachToSerial -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtDevice_AttachToSerial(DtDevice* Device, int64_t SerialNumber)
{
    if (Device == NULL)
        return DTAPI_E_INVALID_ARG;
    if (Device->Drv != NULL)
        return DTAPI_E_ATTACHED;

    for (int Index = 0; Index < DT_MAX_DEVICES; Index++)
    {
        DtapiResult Result = DtDevice_AttachIndex(Device, Index, true, SerialNumber);

        if (Result == DTAPI_E_NO_SUCH_DEVICE)
            continue;
        if (Result != DTAPI_OK)
            return Result;

        if (Device->Info.FirmwareStatus == DT_FWSTATUS_OBSOLETE)
            return DTAPI_OK_OBSOLETE_FW;
        if (Device->Info.FirmwareStatus == DT_FWSTATUS_TAINTED)
            return DTAPI_OK_TAINTED_FW;
        return DTAPI_OK;
    }
    return DTAPI_E_NO_SUCH_DEVICE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtDevice_Detach(DtDevice* Device)
{
    if (Device == NULL)
        return DTAPI_E_INVALID_ARG;
    if (Device->Drv == NULL)
        return DTAPI_E_NOT_ATTACHED;

    DtDevice_Release(Device);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The checks of DtDevice::SetIoConfig, in its order. DTAPI's two extra parameters are
// -1, as DtDevice::SetIoConfig defaults them.
//
DtapiResult DtDevice_SetIoConfig(DtDevice* Device, int Port, int Group, int Value,
                                 int SubValue)
{
    if (Device == NULL)
        return DTAPI_E_INVALID_ARG;
    if (Device->Drv == NULL)
        return DTAPI_E_NOT_ATTACHED;

    if (Device->Info.FirmwareStatus == DT_FWSTATUS_OBSOLETE)
        return DTAPI_E_OBSOLETE_FW;
    if (Device->Info.FirmwareStatus == DT_FWSTATUS_TAINTED)
        return DTAPI_E_TAINTED_FW;

    if (Port < 1 || Port > Device->NumPublicPorts)
        return DTAPI_E_NO_SUCH_PORT;

    DtapiResult Result = DtIoConfig_IsValid(Group, Value, SubValue);
    if (Result != DTAPI_OK)
        return Result;

    DtIoConfig Config;
    Config.Port = Port;
    Config.Group = Group;
    Config.Value = Value;
    Config.SubValue = SubValue;
    Config.ParXtra[0] = -1;
    Config.ParXtra[1] = -1;
    return DtPcieCmd_SetIoConfig(Device->Drv, &Config);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetToOutput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtDevice_SetToOutput(DtDevice* Device, int Port)
{
    return DtDevice_SetIoConfig(Device, Port, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                                DTAPI_IOCONFIG_OUTPUT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_SetToInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtDevice_SetToInput(DtDevice* Device, int Port)
{
    return DtDevice_SetIoConfig(Device, Port, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                DTAPI_IOCONFIG_INPUT);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetTimeOfDay -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtDevice_GetTimeOfDay(const DtDevice* Device, DtTimeOfDay* TimeOfDay)
{
    uint32_t Seconds = 0;
    uint32_t Nanoseconds = 0;

    if (Device == NULL || TimeOfDay == NULL)
        return DTAPI_E_INVALID_ARG;

    TimeOfDay->Seconds = 0;
    TimeOfDay->Nanoseconds = 0;

    if (Device->Drv == NULL)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = DtPcieCmd_GetTimeOfDay(Device->Drv, &Seconds, &Nanoseconds);
    if (Result != DTAPI_OK)
        return Result;

    TimeOfDay->Seconds = Seconds;
    TimeOfDay->Nanoseconds = Nanoseconds;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Video standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// How often waiting for a signal detects again, as CDTAPI does.
#define DT_SIGNAL_POLL_MS 5

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_DetectVidStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// CDTAPI attaches a DtAvInputStatus, ignores the result, and detects. The detection then
// fails with DTAPI_E_NOT_ATTACHED, which hides why; the reason attaching failed is
// returned here instead, and *VidStd is left alone on any failure.
//
DtapiResult DtDevice_DetectVidStd(DtDevice* Device, int Port, int* VidStd)
{
    if (Device == NULL || VidStd == NULL)
        return DTAPI_E_INVALID_ARG;

    DtAvInput Input;
    DtapiResult Result = DtAvInput_Attach(&Input, Device, Port);
    if (Result != DTAPI_OK)
        return Result;

    DtDetVidStd Info;
    Result = DtAvInput_DetectVidStd(&Input, &Info);
    if (Result == DTAPI_OK)
        *VidStd = Info.VidStd;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_WaitForSignalTimeout -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Attaches once and detects until a standard is found, as CDTAPI's DtDevice_WaitForSignal
// does, also while detection fails. The time is measured on a monotonic clock, and the
// last pause is cut to what is left, so that the wait ends close to the time limit.
//
DtapiResult DtDevice_WaitForSignalTimeout(DtDevice* Device, int Port, int TimeoutMs,
                                          DtDetVidStd* Result)
{
    if (Result != NULL)
        DtAvInput_SetUnknown(Result);
    if (Device == NULL || Result == NULL)
        return DTAPI_E_INVALID_ARG;

    DtAvInput Input;
    DtapiResult Attached = DtAvInput_Attach(&Input, Device, Port);
    if (Attached != DTAPI_OK)
        return Attached;

    uint64_t Start = OsTime_MonotonicMs();
    for (;;)
    {
        if (DtAvInput_DetectVidStd(&Input, Result) == DTAPI_OK &&
            Result->VidStd != DTAPI_VIDSTD_UNKNOWN)
        {
            return DTAPI_OK;
        }

        uint64_t Elapsed = OsTime_MonotonicMs() - Start;
        if (TimeoutMs >= 0 && Elapsed >= (uint64_t)TimeoutMs)
            break;

        if (TimeoutMs >= 0 && (uint64_t)TimeoutMs - Elapsed < DT_SIGNAL_POLL_MS)
            OsTime_SleepMs((int)((uint64_t)TimeoutMs - Elapsed));
        else
            OsTime_SleepMs(DT_SIGNAL_POLL_MS);
    }

    // The last detection left every field unknown.
    return DTAPI_E_TIMEOUT;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_WaitForSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Without a time limit only a null device or a port that cannot be attached returns, and
// then every field is unknown.
//
DtDetVidStd DtDevice_WaitForSignal(DtDevice* Device, int Port)
{
    DtDetVidStd Info;

    DtDevice_WaitForSignalTimeout(Device, Port, -1, &Info);
    return Info;
}

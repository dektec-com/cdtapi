// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieCmd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"   // Requests whose size depends on their content.
#include "DtIoConfig.h"     // I/O configuration codes to names and back.
#include "DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcieCmd.h"      // Interface being implemented.
#include "DtPcieCmdIssue.h" // Issuing commands.
#include "DtPcieStatus.h"   // Driver status to result.
#include "cdtapi_version.h" // The DTAPI version a request speaks for.

// The DTAPI version a property request says it speaks for. The driver can hide or change
// properties per DTAPI version, so a request carries the major and minor number of this
// library's own version. The driver compares the bug-fix number too, and that is 0 and
// not this library's patch number, which would ask for properties of a later version.
#define DT_DTAPI_MAJOR CDTAPI_VERSION_MAJOR
#define DT_DTAPI_MINOR CDTAPI_VERSION_MINOR
#define DT_DTAPI_BUGFIX 0

// The size of the memory segment each port has for mapping on Linux. The ABI header
// defines it only for Linux builds; the emulator follows the Linux convention on every
// platform, so that convention is tested everywhere.
#ifndef DT_MMAP_PORT_MEM_SEGMENT_SIZE
    #define DT_MMAP_PORT_MEM_SEGMENT_SIZE (256ull * 1024 * 1024)
#endif

// The oldest DtPcie driver this library works with.
#define DT_DRIVER_MIN_MAJOR 1
#define DT_DRIVER_MIN_MINOR 3
#define DT_DRIVER_MIN_MICRO 1

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_InitHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtPcieCmd_InitHeader(DtIoctlInputDataHdr* Hdr, int Cmd, DtPartRef Part)
{
    memset(Hdr, 0, sizeof(*Hdr));
    Hdr->m_Uuid = Part.Uuid;
    Hdr->m_PortIndex = Part.PortIndex;
    Hdr->m_Cmd = Cmd;
    Hdr->m_CmdEx = DT_IOCTL_CMD_NOP;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The header of a device-level command. That addresses no function, so the UUID is zero
// and the port index -1, which the driver reads as "the device itself".
//
static void InitHeader(DtIoctlInputDataHdr* Hdr, int Cmd)
{
    const DtPartRef Device = {0, -1};
    DtPcieCmd_InitHeader(Hdr, Cmd, Device);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_Issue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_Issue(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize,
                            void* Out, size_t OutSize)
{
    size_t Returned = OutSize;
    uint32_t Status;
    int Outcome = OsDrv_IoCtl(Drv, Code, In, InSize, Out, &Returned, &Status);

    if (Outcome != OS_IOCTL_OK)
        return DtPcieStatus_OutcomeToResult(Outcome, Status);

    if (Returned < OutSize)
        return DTAPI_E_DEV_DRIVER;

    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_IssuePlain -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_IssuePlain(OsDrv* Drv, uint32_t Code, int Cmd, DtPartRef Part,
                                 void* Out, size_t OutSize)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlInputDataHdr In;
    DtPcieCmd_InitHeader(&In, Cmd, Part);
    if (Out != NULL)
        memset(Out, 0, OutSize);
    return DtPcieCmd_Issue(Drv, Code, &In, sizeof(In), Out, OutSize);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitPropertyInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills a property request. The filter fields ask for the device behind Drv as it is:
// type number -1 selects the attached device, and a hardware revision and firmware
// version of zero with firmware variant -1 leave the driver to use the device's own.
//
static DtapiResult InitPropertyInput(DtIoctlPropCmdCommonInput* In, int Cmd,
                                     const char* Name, int PortIndex)
{
    memset(In, 0, sizeof(*In));
    InitHeader(&In->m_CmdHdr, Cmd);
    In->m_TypeNumber = -1;
    In->m_SubDvc = -1;
    In->m_SubType = -1;
    In->m_HardwareRevision = 0;
    In->m_FirmwareVersion = 0;
    In->m_FirmwareVariant = -1;
    In->m_PortIndex = PortIndex;
    In->m_DtapiMaj = DT_DTAPI_MAJOR;
    In->m_DtapiMin = DT_DTAPI_MINOR;
    In->m_DtapiBugfix = DT_DTAPI_BUGFIX;

    size_t NameLength = strlen(Name);
    if (NameLength + 1 > sizeof(In->m_Name))
        return DTAPI_E_BUF_TOO_SMALL;
    memcpy(In->m_Name, Name, NameLength + 1);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetPropertyValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a property as the raw 64-bit value the driver stores.
//
// The scope and type of the answer are not checked, so a mismatch in either is passed on
// rather than rejected.
//
static DtapiResult GetPropertyValue(OsDrv* Drv, const char* Name, int PortIndex,
                                    uint64_t* Value)
{
    DtIoctlPropCmdGetValueInput In;

    DtapiResult Result = InitPropertyInput(&In, DT_PROP_CMD_GET_VALUE, Name, PortIndex);
    if (!DT_SUCCEEDED(Result))
        return Result;

    DtIoctlPropCmdGetValueOutput Out;
    memset(&Out, 0, sizeof(Out));
    Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In), &Out,
                             sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Value = Out.m_Value;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsBuddyPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when ParXtra[0] of this configuration names another port, which this library
// numbers from 1 and the driver from 0.
//
static bool IsBuddyPort(const DtIoConfig* Config)
{
    if (Config->Group != DTAPI_IOCONFIG_IODIR)
        return false;

    switch (Config->Value)
    {
    case DTAPI_IOCONFIG_OUTPUT:
    case DTAPI_IOCONFIG_INTOUTPUT:
        return Config->SubValue == DTAPI_IOCONFIG_DBLBUF ||
               Config->SubValue == DTAPI_IOCONFIG_LOOPS2L3 ||
               Config->SubValue == DTAPI_IOCONFIG_LOOPS2TS ||
               Config->SubValue == DTAPI_IOCONFIG_LOOPTHR;
    case DTAPI_IOCONFIG_INPUT:
        return Config->SubValue == DTAPI_IOCONFIG_SHAREDANT;
    case DTAPI_IOCONFIG_MONITOR:
        return true;
    default:
        return false;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConfigToDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Converts a configuration to the driver's form, failing in this order: a code without a
// name, then an ISI out of range.
//
static DtapiResult ConfigToDriver(const DtIoConfig* Config, DtIoctlIoConfig* Drv)
{
    memset(Drv, 0, sizeof(*Drv));
    Drv->m_PortIndex = Config->Port - 1;

    DtapiResult Result =
        DtIoConfig_GetName(Config->Group, Drv->m_Group, sizeof(Drv->m_Group));
    if (Result != DTAPI_OK)
        return Result;
    Result = DtIoConfig_GetName(Config->Value, Drv->m_Value, sizeof(Drv->m_Value));
    if (Result != DTAPI_OK)
        return Result;
    Result =
        DtIoConfig_GetName(Config->SubValue, Drv->m_SubValue, sizeof(Drv->m_SubValue));
    if (Result != DTAPI_OK)
        return Result;

    // A configuration carries two extra parameters; the driver has room for four.
    Drv->m_ParXtra[0] = Config->ParXtra[0];
    Drv->m_ParXtra[1] = Config->ParXtra[1];
    Drv->m_ParXtra[2] = -1;
    Drv->m_ParXtra[3] = -1;

    if (IsBuddyPort(Config))
        Drv->m_ParXtra[0] = Config->ParXtra[0] - 1;

    if (Config->Group == DTAPI_IOCONFIG_IODIR &&
        (Config->Value == DTAPI_IOCONFIG_OUTPUT ||
         Config->Value == DTAPI_IOCONFIG_INTOUTPUT) &&
        Config->SubValue == DTAPI_IOCONFIG_LOOPS2TS &&
        (Drv->m_ParXtra[1] < 0 || Drv->m_ParXtra[1] > 255))
    {
        return DTAPI_E_INVALID_ISI;
    }

    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CodeFromDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Looks up the code for a name field of a driver answer. The field is terminated here
// first, so that a driver filling all of it cannot make the lookup read past its end.
//
static DtapiResult CodeFromDriver(char* Name, size_t Size, int* Code)
{
    Name[Size - 1] = '\0';
    return DtIoConfig_GetCode(Name, Code);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_GetDriverVersion(OsDrv* Drv, DtDriverVersion* Version)
{
    if (Drv == NULL || Version == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlGetDriverVersionInput In;
    InitHeader(&In, DT_IOCTL_CMD_NOP);
    DtIoctlGetDriverVersionOutput Out;
    memset(&Out, 0, sizeof(Out));

    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Version->Major = Out.m_Major;
    Version->Minor = Out.m_Minor;
    Version->Micro = Out.m_Micro;
    Version->Build = Out.m_Build;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_VersionIsSupported -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtPcieCmd_VersionIsSupported(const DtDriverVersion* Version)
{
    if (Version->Major != DT_DRIVER_MIN_MAJOR)
        return Version->Major > DT_DRIVER_MIN_MAJOR;
    if (Version->Minor != DT_DRIVER_MIN_MINOR)
        return Version->Minor > DT_DRIVER_MIN_MINOR;
    return Version->Micro >= DT_DRIVER_MIN_MICRO;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_VersionAtLeast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtPcieCmd_VersionAtLeast(const DtDriverVersion* Version, int Major, int Minor,
                              int Micro, int Build)
{
    if (Version->Major != Major)
        return Version->Major > Major;
    if (Version->Minor != Minor)
        return Version->Minor > Minor;
    if (Version->Micro != Micro)
        return Version->Micro > Micro;
    return Version->Build >= Build;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetDeviceInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_GetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info)
{
    if (Drv == NULL || Info == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlGetDevInfoInput In;
    InitHeader(&In, DT_IOCTL_CMD_NOP);
    DtIoctlGetDevInfoOutput Out;
    memset(&Out, 0, sizeof(Out));

    // GET_DEV_INFO2 first. A driver that predates it refuses the command, and then the
    // original is tried, which carries the same common fields and a PCIe part without the
    // slot power.
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_GET_DEV_INFO2), &In,
                                         sizeof(In), &Out, sizeof(Out));
    bool HasSlotPower = DT_SUCCEEDED(Result);
    if (!DT_SUCCEEDED(Result))
    {
        memset(&Out, 0, sizeof(Out));
        Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_GET_DEV_INFO), &In, sizeof(In),
                                 &Out, sizeof(Out));
        if (!DT_SUCCEEDED(Result))
            return Result;
    }

    memset(Info, 0, sizeof(*Info));
    Info->FwBuildYear = Out.m_FwBuildDate.m_Year;
    Info->FwBuildMonth = Out.m_FwBuildDate.m_Month;
    Info->FwBuildDay = Out.m_FwBuildDate.m_Day;
    Info->FwBuildHour = Out.m_FwBuildDate.m_Hour;
    Info->FwBuildMinute = Out.m_FwBuildDate.m_Minute;
    Info->BusNumber = Out.m_DevSpecific.m_Pcie.m_BusNumber;
    Info->SlotNumber = Out.m_DevSpecific.m_Pcie.m_SlotNumber;
    Info->PcieNumLanes = Out.m_DevSpecific.m_Pcie.m_PcieNumLanes;
    Info->PcieMaxLanes = Out.m_DevSpecific.m_Pcie.m_PcieMaxLanes;
    Info->PcieLinkSpeed = Out.m_DevSpecific.m_Pcie.m_PcieLinkSpeed;
    Info->PcieMaxSpeed = Out.m_DevSpecific.m_Pcie.m_PcieMaxSpeed;
    Info->PcieMaxPayloadSize = Out.m_DevSpecific.m_Pcie.m_PcieMaxPayloadSize;
    Info->PcieMaxReadRequestSize = Out.m_DevSpecific.m_Pcie.m_PcieMaxReadRequestSize;
    Info->PcieMaxSlotPower =
        HasSlotPower ? Out.m_DevSpecific.m_Pcie2.m_PcieMaxSlotPower : 0;
    Info->TypeNumber = Out.m_TypeNumber;
    Info->SubType = Out.m_SubType;
    Info->Serial = (int64_t)Out.m_Serial;
    Info->HardwareRevision = Out.m_HardwareRevision;
    Info->FirmwareVersion = Out.m_FirmwareVersion;
    Info->FirmwareVariant = Out.m_FirmwareVariant;
    Info->FirmwareStatus = Out.m_FirmwareStatus;
    Info->VendorId = Out.m_VendorId;
    Info->DeviceId = Out.m_DeviceId;
    Info->SubVendorId = Out.m_SubVendorId;
    Info->SubSystemId = Out.m_SubSystemId;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetPropertyInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_GetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex,
                                     int* Value)
{
    uint64_t Raw = 0;

    if (Value != NULL)
        *Value = 0;

    if (Drv == NULL || Name == NULL || Value == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = GetPropertyValue(Drv, Name, PortIndex, &Raw);
    if (!DT_SUCCEEDED(Result))
        return Result;

    // Truncated to int; a negative value is stored sign-extended.
    *Value = (int)(int64_t)Raw;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetPropertyBool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_GetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                      bool* Value)
{
    uint64_t Raw = 0;

    if (Value != NULL)
        *Value = false;

    if (Drv == NULL || Name == NULL || Value == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = GetPropertyValue(Drv, Name, PortIndex, &Raw);
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Value = Raw != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetPropertyStr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver fills a fixed field and need not terminate a string that fills it, so the
// length is taken within the field. The scope of the answer is not checked.
//
DtapiResult DtPcieCmd_GetPropertyStr(OsDrv* Drv, const char* Name, int PortIndex,
                                     char* Str, size_t Size)
{
    if (Str != NULL && Size > 0)
        Str[0] = '\0';

    if (Drv == NULL || Name == NULL || Str == NULL || Size == 0)
        return DTAPI_E_INVALID_ARG;

    DtIoctlPropCmdGetStrInput In;
    DtapiResult Result = InitPropertyInput(&In, DT_PROP_CMD_GET_STR, Name, PortIndex);
    if (!DT_SUCCEEDED(Result))
        return Result;

    DtIoctlPropCmdGetStrOutput Out;
    memset(&Out, 0, sizeof(Out));
    Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In), &Out,
                             sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    size_t Length;
    for (Length = 0; Length < sizeof(Out.m_Str) && Out.m_Str[Length] != '\0'; Length++)
        ;
    if (Length + 1 > Size)
        return DTAPI_E_BUF_TOO_SMALL;

    memcpy(Str, Out.m_Str, Length);
    Str[Length] = '\0';
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetIoConfigList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The request and the answer each end in an array of one element per configuration. The
// answers are converted into a copy first, so that a list the driver answers only in part
// leaves every entry as it was.
//
DtapiResult DtPcieCmd_GetIoConfigList(OsDrv* Drv, DtIoConfig* Configs, int Count)
{
    if (Drv == NULL || Configs == NULL || Count < 1)
        return DTAPI_E_INVALID_ARG;

    const size_t InSize = sizeof(DtIoctlIoConfigCmdGetIoConfigInput) +
                          (size_t)Count * sizeof(DtIoctlIoConfigId);
    const size_t OutSize = sizeof(DtIoctlIoConfigCmdGetIoConfigOutput) +
                           (size_t)Count * sizeof(DtIoctlIoConfigValue);
    DtIoctlIoConfigCmdGetIoConfigInput* In =
        (DtIoctlIoConfigCmdGetIoConfigInput*)DtAlloc_Malloc(InSize);
    DtIoctlIoConfigCmdGetIoConfigOutput* Out =
        (DtIoctlIoConfigCmdGetIoConfigOutput*)DtAlloc_Malloc(OutSize);
    DtIoConfig* Got = (DtIoConfig*)DtAlloc_Malloc((size_t)Count * sizeof(DtIoConfig));
    DtapiResult Result = DTAPI_OK;
    if (In == NULL || Out == NULL || Got == NULL)
    {
        Result = DTAPI_E_OUT_OF_MEM;
        goto Cleanup;
    }

    memset(In, 0, InSize);
    memset(Out, 0, OutSize);
    InitHeader(&In->m_CmdHdr, DT_IOCONFIG_CMD_GET_IOCONFIG);
    In->m_IoConfigCount = Count;
    for (int i = 0; i < Count && Result == DTAPI_OK; i++)
    {
        In->m_IoCfgId[i].m_PortIndex = Configs[i].Port - 1;
        Result = DtIoConfig_GetName(Configs[i].Group, In->m_IoCfgId[i].m_Group,
                                    sizeof(In->m_IoCfgId[i].m_Group));
    }
    if (Result != DTAPI_OK)
        goto Cleanup;

    Result =
        DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_IOCONFIG_CMD), In, InSize, Out, OutSize);
    if (!DT_SUCCEEDED(Result))
        goto Cleanup;

    for (int i = 0; i < Count && Result == DTAPI_OK; i++)
    {
        DtIoctlIoConfigValue* Value = &Out->m_IoCfgValue[i];
        Got[i] = Configs[i];
        Result = CodeFromDriver(Value->m_Value, sizeof(Value->m_Value), &Got[i].Value);
        if (Result == DTAPI_OK)
            Result = CodeFromDriver(Value->m_SubValue, sizeof(Value->m_SubValue),
                                    &Got[i].SubValue);
        Got[i].ParXtra[0] = Value->m_ParXtra[0];
        Got[i].ParXtra[1] = Value->m_ParXtra[1];
        if (IsBuddyPort(&Got[i]))
            Got[i].ParXtra[0] = Value->m_ParXtra[0] + 1;
    }
    if (Result == DTAPI_OK)
        memcpy(Configs, Got, (size_t)Count * sizeof(DtIoConfig));

Cleanup:
    DtAlloc_Free(In);
    DtAlloc_Free(Out);
    DtAlloc_Free(Got);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SetIoConfigList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SetIoConfigList(OsDrv* Drv, const DtIoConfig* Configs, int Count)
{
    if (Drv == NULL || Configs == NULL || Count < 1)
        return DTAPI_E_INVALID_ARG;

    const size_t InSize = sizeof(DtIoctlIoConfigCmdSetIoConfigInput) +
                          (size_t)Count * sizeof(DtIoctlIoConfig);
    DtIoctlIoConfigCmdSetIoConfigInput* In =
        (DtIoctlIoConfigCmdSetIoConfigInput*)DtAlloc_Malloc(InSize);
    if (In == NULL)
        return DTAPI_E_OUT_OF_MEM;

    memset(In, 0, InSize);
    InitHeader(&In->m_CmdHdr, DT_IOCONFIG_CMD_SET_IOCONFIG);
    In->m_IoConfigCount = Count;

    DtapiResult Result = DTAPI_OK;
    for (int i = 0; i < Count && Result == DTAPI_OK; i++)
    {
        DtIoctlIoConfig* Pars = &In->m_IoCfgPars[i];
        Result = ConfigToDriver(&Configs[i], Pars);

        // The driver's exclusive-access check is skipped when the configured port is the
        // one the request addresses. A device-level request addresses port index -1, so
        // that is only ever the case for a port number of 0, which the device layer has
        // refused before it gets here.
        Pars->m_SkipExclAccessCheck = (Pars->m_PortIndex == -1) ? 1 : 0;
    }

    if (Result == DTAPI_OK)
        Result =
            DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_IOCONFIG_CMD), In, InSize, NULL, 0);
    DtAlloc_Free(In);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_GetIoConfig(OsDrv* Drv, DtIoConfig* Config)
{
    return DtPcieCmd_GetIoConfigList(Drv, Config, 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_SetIoConfig(OsDrv* Drv, const DtIoConfig* Config)
{
    return DtPcieCmd_SetIoConfigList(Drv, Config, 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_GetTimeOfDay -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_GetTimeOfDay(OsDrv* Drv, uint32_t* Seconds, uint32_t* Nanoseconds)
{
    if (Drv == NULL || Seconds == NULL || Nanoseconds == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlTodCmdGetTimeInput In;
    InitHeader(&In, DT_TOD_CMD_GET_TIME);
    DtIoctlTodCmdGetTimeOutput Out;
    memset(&Out, 0, sizeof(Out));

    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_TOD_CMD), &In, sizeof(In),
                                         &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Seconds = Out.m_Time.m_Seconds;
    *Nanoseconds = Out.m_Time.m_Nanoseconds;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDI receiver +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiRateFromDriver -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An SDI rate as the driver reports it, with a value the driver does not define as
// unknown.
//
static int SdiRateFromDriver(int Rate)
{
    switch (Rate)
    {
    case DT_DRV_SDIRATE_SD:
    case DT_DRV_SDIRATE_HD:
    case DT_DRV_SDIRATE_3G:
    case DT_DRV_SDIRATE_6G:
    case DT_DRV_SDIRATE_12G:
        return Rate;
    default:
        return DT_DRV_SDIRATE_UNKNOWN;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_SdiRxGetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DT_SDIRX_CMD_GET_SDI_STATUS2, with the answer converted: the flags to booleans, the
// frame period in nanoseconds to a rate, and an SDI rate the driver does not define to
// unknown.
//
DtapiResult DtPcieCmd_SdiRxGetStatus(OsDrv* Drv, DtPartRef Part, DtSdiRxStatus* Status)
{
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));

    if (Drv == NULL || Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlSdiRxCmdGetSdiStatusInput In;
    DtPcieCmd_InitHeader(&In, DT_SDIRX_CMD_GET_SDI_STATUS2, Part);
    DtIoctlSdiRxCmdGetSdiStatusOutput2 Out;
    memset(&Out, 0, sizeof(Out));

    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_SDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Status->CarrierDetect = Out.m_CarrierDetect != 0;
    Status->SdiLock = Out.m_SdiLock != 0;
    Status->LineLock = Out.m_LineLock != 0;
    Status->Valid = Out.m_Valid != 0;
    Status->NumSymsHanc = Out.m_NumSymsHanc;
    Status->NumSymsVidVanc = Out.m_NumSymsVidVanc;
    Status->NumLinesF1 = Out.m_NumLinesF1;
    Status->NumLinesF2 = Out.m_NumLinesF2;
    Status->IsLevelB = Out.m_IsLevelB != 0;
    Status->PayloadId = Out.m_PayloadId;
    Status->FrameRate = Out.m_FramePeriod > 0 ? 1e9 / Out.m_FramePeriod : 0.0;
    Status->SdiRate = SdiRateFromDriver(Out.m_SdiRate);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDI receive channel +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxAttach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_ChSdiRxAttach(OsDrv* Drv, DtPartRef Part, bool Exclusive,
                                    const char* FriendlyName)
{
    if (Drv == NULL || FriendlyName == NULL)
        return DTAPI_E_INVALID_ARG;

    size_t Length = strlen(FriendlyName);
    if (Length == 0 || Length > DT_CHAN_FRIENDLY_NAME_MAX_LENGTH)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdAttachInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CHSDIRX_CMD_ATTACH, Part);
    In.m_ReqExclusiveAccess = Exclusive ? 1 : 0;
    memcpy(In.m_FriendlyName, FriendlyName, Length);
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxDetach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_ChSdiRxDetach(OsDrv* Drv, DtPartRef Part)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdDetachInput In;
    DtPcieCmd_InitHeader(&In, DT_CHSDIRX_CMD_DETACH, Part);
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxConfigure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An SDI rate that cannot be converted is refused with DTAPI_E_INVALID_RATE before
// anything is sent.
//
DtapiResult DtPcieCmd_ChSdiRxConfigure(OsDrv* Drv, DtPartRef Part,
                                       const DtChSdiRxConfig* Config)
{
    if (Drv == NULL || Config == NULL || Config->NumPorts < 1 || Config->NumPorts > 4)
        return DTAPI_E_INVALID_ARG;

    switch (Config->SdiRate)
    {
    case DT_DRV_SDIRATE_SD:
    case DT_DRV_SDIRATE_HD:
    case DT_DRV_SDIRATE_3G:
    case DT_DRV_SDIRATE_6G:
    case DT_DRV_SDIRATE_12G:
        break;
    default:
        return DTAPI_E_INVALID_RATE;
    }

    DtIoctlChSdiRxCmdConfigureInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CHSDIRX_CMD_CONFIGURE, Part);
    In.m_NumPhysicalPorts = Config->NumPorts;
    for (int i = 0; i < Config->NumPorts; i++)
        In.m_PhysicalPorts[i] = Config->PortIndices[i];
    In.m_DmaBuf.m_MinSize = Config->DmaMinSize;
    In.m_FmtEvt.m_IntInterval = Config->FmtIntInterval;
    In.m_FmtEvt.m_IntDelay = Config->FmtIntDelay;
    In.m_FmtEvt.m_NumIntsPerFrame = Config->FmtNumIntsPerFrame;
    In.m_FrameProps.m_NumSymsHanc = Config->NumSymsHanc;
    In.m_FrameProps.m_NumSymsVidVanc = Config->NumSymsVidVanc;
    In.m_FrameProps.m_NumLines = Config->NumLines;
    In.m_FrameProps.m_SdiRate = Config->SdiRate;
    In.m_FrameProps.m_AssumeInterlaced = Config->AssumeInterlaced ? 1 : 0;
    In.m_FrameProps.m_Scale12GTo3G = Config->Scale12GTo3G ? 1 : 0;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxGetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ChSdiRxGetOpMode(OsDrv* Drv, DtPartRef Part, int* OpMode)
{
    if (Drv == NULL || OpMode == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdGetOpModeInput In;
    DtPcieCmd_InitHeader(&In, DT_CHSDIRX_CMD_GET_OPERATIONAL_MODE, Part);
    DtIoctlChSdiRxCmdGetOpModeOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *OpMode = Out.m_OpMode;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ChSdiRxSetOpMode(OsDrv* Drv, DtPartRef Part, int OpMode)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdSetOpModeInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CHSDIRX_CMD_SET_OPERATIONAL_MODE, Part);
    In.m_OpMode = OpMode;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxWaitForFmtEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ChSdiRxWaitForFmtEvent(OsDrv* Drv, DtPartRef Part, int TimeoutMs,
                                             DtChSdiRxEvent* Event)
{
    if (Drv == NULL || Event == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdWaitForFmtEventInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT, Part);
    In.m_Timeout = TimeoutMs;
    DtIoctlChSdiRxCmdWaitForFmtEventOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Event->FrameId = Out.m_FrameId;
    Event->SeqNumber = Out.m_SeqNumber;
    Event->InSync = Out.m_InSync != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxGetWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_ChSdiRxGetWriteOffset(OsDrv* Drv, DtPartRef Part, uint32_t* Offset)
{
    if (Drv == NULL || Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdGetWrOffsetInput In;
    DtPcieCmd_InitHeader(&In, DT_CHSDIRX_CMD_GET_WRITE_OFFSET, Part);
    DtIoctlChSdiRxCmdGetWrOffsetOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Offset = Out.m_WriteOffset;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxSetReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ChSdiRxSetReadOffset(OsDrv* Drv, DtPartRef Part, uint32_t Offset)
{
    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdSetRdOffsetInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitHeader(&In.m_CmdHdr, DT_CHSDIRX_CMD_SET_READ_OFFSET, Part);
    In.m_ReadOffset = Offset;
    return DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_ChSdiRxGetProps(OsDrv* Drv, DtPartRef Part, DtChSdiRxProps* Props)
{
    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Drv == NULL || Props == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdGetPropsInput In;
    DtPcieCmd_InitHeader(&In, DT_CHSDIRX_CMD_GET_PROPS, Part);
    DtIoctlChSdiRxCmdGetPropsOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Props->DmaCaps = Out.m_Dma.m_Caps;
    Props->PrefetchSize = Out.m_Dma.m_PrefetchSize;
    Props->PcieDataWidth = Out.m_Dma.m_PcieDataWidth;
    Props->ReorderBufSize = Out.m_Dma.m_ReorderBufSize;
    Props->StreamAlignment = Out.m_StreamAlignment;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxGetSdiStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The answer is converted as DtPcieCmd_SdiRxGetStatus converts it, except that the
// carrier is left out.
//
DtapiResult DtPcieCmd_ChSdiRxGetSdiStatus(OsDrv* Drv, DtPartRef Part,
                                          DtSdiRxStatus* Status)
{
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Drv == NULL || Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtIoctlChSdiRxCmdGetSdiStatusInput In;
    DtPcieCmd_InitHeader(&In, DT_CHSDIRX_CMD_GET_SDI_STATUS, Part);
    DtIoctlChSdiRxCmdGetSdiStatusOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Status->SdiLock = Out.m_SdiLock != 0;
    Status->LineLock = Out.m_LineLock != 0;
    Status->Valid = Out.m_Valid != 0;
    Status->NumSymsHanc = Out.m_NumSymsHanc;
    Status->NumSymsVidVanc = Out.m_NumSymsVidVanc;
    Status->NumLinesF1 = Out.m_NumLinesF1;
    Status->NumLinesF2 = Out.m_NumLinesF2;
    Status->IsLevelB = Out.m_IsLevelB != 0;
    Status->PayloadId = Out.m_PayloadId;
    Status->FrameRate = Out.m_FramePeriod > 0 ? 1e9 / Out.m_FramePeriod : 0.0;
    Status->SdiRate = SdiRateFromDriver(Out.m_SdiRate);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxMapDmaBuf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieCmd_ChSdiRxMapDmaBuf(OsDrv* Drv, DtPartRef Part, uint8_t** Buffer,
                                       int* BufSize, int* MaxLoad, bool* Mapped)
{
    if (Buffer != NULL)
        *Buffer = NULL;
    if (BufSize != NULL)
        *BufSize = 0;
    if (MaxLoad != NULL)
        *MaxLoad = 0;
    if (Mapped != NULL)
        *Mapped = false;
    if (Drv == NULL || Buffer == NULL || BufSize == NULL || MaxLoad == NULL ||
        Mapped == NULL)
    {
        return DTAPI_E_INVALID_ARG;
    }

    DtIoctlChSdiRxCmdMapDmaBufToUserInput In;
    DtPcieCmd_InitHeader(&In, DT_CHSDIRX_CMD_MAP_DMA_BUF_TO_USER, Part);
    DtIoctlChSdiRxCmdMapDmaBufToUserOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result = DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In,
                                         sizeof(In), &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    if (Out.m_BufSize <= 0 || Out.m_MaxLoad <= 0)
        return DTAPI_E_DEV_DRIVER;

    void* Address = (void*)(uintptr_t)Out.m_BufferAddr;
    if (Address == NULL)
    {
        uint64_t Offset =
            (uint64_t)DT_MMAP_PORT_MEM_SEGMENT_SIZE * (uint64_t)Part.PortIndex +
            (uint64_t)DT_MMAP_PORT_MEM_SEGMENT_SIZE;

        Address = OsDrv_MapMemory(Drv, Offset, (size_t)Out.m_BufSize);
        if (Address == NULL)
            return DTAPI_E_OUT_OF_MEM;
        *Mapped = true;
    }

    *Buffer = (uint8_t*)Address;
    *BufSize = Out.m_BufSize;
    *MaxLoad = Out.m_MaxLoad;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_ChSdiRxUnmapDmaBuf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtPcieCmd_ChSdiRxUnmapDmaBuf(OsDrv* Drv, uint8_t* Buffer, int BufSize, bool Mapped)
{
    if (Mapped && Buffer != NULL && BufSize > 0)
        OsDrv_UnmapMemory(Drv, Buffer, (size_t)BufSize);
}

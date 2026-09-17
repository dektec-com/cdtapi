// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtDrv.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Driver ABI layer - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite_Version.h" // The DTAPI version a request speaks for.
#include "DtDrv.h"              // Interface being implemented.
#include "DtDrvAbi.h"           // Vendored driver structures and IOCTL codes.
#include "DtDrvCommand.h"       // Issuing commands.
#include "DtDrvStatus.h"        // Driver status to result.
#include "DtIoConfig.h"         // I/O configuration codes to names and back.

// The DTAPI version a property request says it speaks for. The driver can hide or change
// properties per DTAPI version, so CDtapiLite presents itself as the DTAPI whose
// behaviour it reproduces: the major and minor number of its own version. The driver
// compares the bug-fix number too (DtPropertiesFind), so that is DTAPI's, 0, and not
// CDtapiLite's patch number, which would show properties of a later DTAPI.
#define DT_DTAPI_MAJOR CDTAPILITE_VERSION_MAJOR
#define DT_DTAPI_MINOR CDTAPILITE_VERSION_MINOR
#define DT_DTAPI_BUGFIX 0

// The size of the memory segment each port has for mapping on Linux. The ABI header
// defines it only for Linux builds; the emulator follows the Linux convention on every
// platform, so that convention is tested everywhere.
#ifndef DT_MMAP_PORT_MEM_SEGMENT_SIZE
    #define DT_MMAP_PORT_MEM_SEGMENT_SIZE (256ull * 1024 * 1024)
#endif

// The oldest DtPcie driver DTAPI works with.
#define DT_DRIVER_MIN_MAJOR 1
#define DT_DRIVER_MIN_MINOR 3
#define DT_DRIVER_MIN_MICRO 1

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= I/O configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The driver's I/O configuration commands end in a flexible array, one element per
// configuration. CDtapiLite only ever sends one, so each request is laid out here with a
// single element in place of the array. The assertions hold these to exactly the bytes
// DTAPI sends: the size of the vendored structure plus one element.
//

typedef struct IoConfigGetIn
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_IoConfigCount;
    DtIoctlIoConfigId m_IoCfgId;
} IoConfigGetIn;

typedef struct IoConfigGetOut
{
    Int m_IoConfigCount;
    DtIoctlIoConfigValue m_IoCfgValue;
} IoConfigGetOut;

typedef struct IoConfigSetIn
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_IoConfigCount;
    DtIoctlIoConfig m_IoCfgPars;
} IoConfigSetIn;

_Static_assert(offsetof(IoConfigGetIn, m_IoCfgId) ==
                   offsetof(DtIoctlIoConfigCmdGetIoConfigInput, m_IoCfgId),
               "IoConfigGetIn must match the driver's layout");
_Static_assert(sizeof(IoConfigGetIn) ==
                   sizeof(DtIoctlIoConfigCmdGetIoConfigInput) + sizeof(DtIoctlIoConfigId),
               "IoConfigGetIn must be the driver's size for one configuration");
_Static_assert(offsetof(IoConfigGetOut, m_IoCfgValue) ==
                   offsetof(DtIoctlIoConfigCmdGetIoConfigOutput, m_IoCfgValue),
               "IoConfigGetOut must match the driver's layout");
_Static_assert(sizeof(IoConfigGetOut) == sizeof(DtIoctlIoConfigCmdGetIoConfigOutput) +
                                             sizeof(DtIoctlIoConfigValue),
               "IoConfigGetOut must be the driver's size for one configuration");
_Static_assert(offsetof(IoConfigSetIn, m_IoCfgPars) ==
                   offsetof(DtIoctlIoConfigCmdSetIoConfigInput, m_IoCfgPars),
               "IoConfigSetIn must match the driver's layout");
_Static_assert(sizeof(IoConfigSetIn) ==
                   sizeof(DtIoctlIoConfigCmdSetIoConfigInput) + sizeof(DtIoctlIoConfig),
               "IoConfigSetIn must be the driver's size for one configuration");

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvInitHeaderFor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtDrvInitHeaderFor(DtIoctlInputDataHdr* Hdr, int Cmd, int Uuid, int PortIndex)
{
    memset(Hdr, 0, sizeof(*Hdr));
    Hdr->m_Uuid = Uuid;
    Hdr->m_PortIndex = PortIndex;
    Hdr->m_Cmd = Cmd;
    Hdr->m_CmdEx = DT_IOCTL_CMD_NOP;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The header of a device-level command. That addresses no function, so Uuid is zero and
// PortIndex is -1, which the driver reads as "the device itself".
//
static void InitHeader(DtIoctlInputDataHdr* Hdr, int Cmd)
{
    DtDrvInitHeaderFor(Hdr, Cmd, 0, -1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvIssue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvIssue(OsDrv* Drv, uint32_t Code, const void* In, size_t InSize,
                        void* Out, size_t OutSize)
{
    size_t Returned = OutSize;
    uint32_t Status;
    int Outcome = OsDrvIoCtl(Drv, Code, In, InSize, Out, &Returned, &Status);

    if (Outcome != OS_IOCTL_OK)
        return DtDrvOutcomeToResult(Outcome, Status);

    if (Returned < OutSize)
        return DTAPI_E_DEV_DRIVER;

    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitPropertyInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills a property request. The filter fields ask for the device behind Drv as it is:
// type number -1 selects the attached device, and a hardware revision and firmware
// version of zero with firmware variant -1 leave the driver to use the device's own, as
// DTAPI's Device::PropertyGet* do by default.
//
static unsigned int InitPropertyInput(DtIoctlPropCmdCommonInput* In, int Cmd,
                                      const char* Name, int PortIndex)
{
    size_t NameLength;

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

    NameLength = strlen(Name);
    if (NameLength + 1 > sizeof(In->m_Name))
        return DTAPI_E_BUF_TOO_SMALL;
    memcpy(In->m_Name, Name, NameLength + 1);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetPropertyValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a property as the raw 64-bit value the driver stores.
//
// DTAPI checks the scope and type of the answer only with debug assertions, and so does
// not reject a mismatch in a release build. Neither does this.
//
static unsigned int GetPropertyValue(OsDrv* Drv, const char* Name, int PortIndex,
                                     uint64_t* Value)
{
    DtIoctlPropCmdGetValueInput In;
    DtIoctlPropCmdGetValueOutput Out;
    unsigned int Result;

    Result = InitPropertyInput(&In, DT_PROP_CMD_GET_VALUE, Name, PortIndex);
    if (!DT_SUCCEEDED(Result))
        return Result;

    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Value = Out.m_Value;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsBuddyPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when ParXtra[0] of this configuration names another port, which DTAPI numbers
// from 1 and the driver from 0 (DriverUtils::PrepIoConfigForDriver).
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
// Converts a configuration to the driver's form, in the order DTAPI does and with the
// same failures: a code without a name, then an ISI out of range.
//
static unsigned int ConfigToDriver(const DtIoConfig* Config, DtIoctlIoConfig* Drv)
{
    unsigned int Result;

    memset(Drv, 0, sizeof(*Drv));
    Drv->m_PortIndex = Config->Port - 1;

    Result = DtIoConfigGetName(Config->Group, Drv->m_Group, sizeof(Drv->m_Group));
    if (Result != DTAPI_OK)
        return Result;
    Result = DtIoConfigGetName(Config->Value, Drv->m_Value, sizeof(Drv->m_Value));
    if (Result != DTAPI_OK)
        return Result;
    Result =
        DtIoConfigGetName(Config->SubValue, Drv->m_SubValue, sizeof(Drv->m_SubValue));
    if (Result != DTAPI_OK)
        return Result;

    // DTAPI carries two extra parameters; the driver has room for four.
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
static unsigned int CodeFromDriver(char* Name, size_t Size, int* Code)
{
    Name[Size - 1] = '\0';
    return DtIoConfigGetCode(Name, Code);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvGetDriverVersion(OsDrv* Drv, DtDriverVersion* Version)
{
    DtIoctlGetDriverVersionInput In;
    DtIoctlGetDriverVersionOutput Out;
    unsigned int Result;

    if (Drv == NULL || Version == NULL)
        return DTAPI_E_INVALID_ARG;

    InitHeader(&In, DT_IOCTL_CMD_NOP);
    memset(&Out, 0, sizeof(Out));

    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Version->Major = Out.m_Major;
    Version->Minor = Out.m_Minor;
    Version->Micro = Out.m_Micro;
    Version->Build = Out.m_Build;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvVersionIsSupported -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtDrvVersionIsSupported(const DtDriverVersion* Version)
{
    if (Version->Major != DT_DRIVER_MIN_MAJOR)
        return Version->Major > DT_DRIVER_MIN_MAJOR;
    if (Version->Minor != DT_DRIVER_MIN_MINOR)
        return Version->Minor > DT_DRIVER_MIN_MINOR;
    return Version->Micro >= DT_DRIVER_MIN_MICRO;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvVersionAtLeast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtDrvVersionAtLeast(const DtDriverVersion* Version, int Major, int Minor, int Micro,
                         int Build)
{
    if (Version->Major != Major)
        return Version->Major > Major;
    if (Version->Minor != Minor)
        return Version->Minor > Minor;
    if (Version->Micro != Micro)
        return Version->Micro > Micro;
    return Version->Build >= Build;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetDeviceInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvGetDeviceInfo(OsDrv* Drv, DtDeviceInfo* Info)
{
    DtIoctlGetDevInfoInput In;
    DtIoctlGetDevInfoOutput Out;
    unsigned int Result;
    bool HasSlotPower;

    if (Drv == NULL || Info == NULL)
        return DTAPI_E_INVALID_ARG;

    InitHeader(&In, DT_IOCTL_CMD_NOP);
    memset(&Out, 0, sizeof(Out));

    // GET_DEV_INFO2 first. A driver that predates it refuses the command, and then the
    // original is tried, which carries the same common fields and a PCIe part without the
    // slot power (DtPcieProxyCORE::CopyDeviceTypeSpecificInfo).
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_GET_DEV_INFO2), &In, sizeof(In), &Out,
                        sizeof(Out));
    HasSlotPower = DT_SUCCEEDED(Result);
    if (!DT_SUCCEEDED(Result))
    {
        memset(&Out, 0, sizeof(Out));
        Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_GET_DEV_INFO), &In, sizeof(In), &Out,
                            sizeof(Out));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetPropertyInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvGetPropertyInt(OsDrv* Drv, const char* Name, int PortIndex, int* Value)
{
    uint64_t Raw = 0;
    unsigned int Result;

    if (Value != NULL)
        *Value = 0;

    if (Drv == NULL || Name == NULL || Value == NULL)
        return DTAPI_E_INVALID_ARG;

    Result = GetPropertyValue(Drv, Name, PortIndex, &Raw);
    if (!DT_SUCCEEDED(Result))
        return Result;

    // Truncated to int, as DTAPI casts it; a negative value is stored sign-extended.
    *Value = (int)(int64_t)Raw;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetPropertyBool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvGetPropertyBool(OsDrv* Drv, const char* Name, int PortIndex,
                                  bool* Value)
{
    uint64_t Raw = 0;
    unsigned int Result;

    if (Value != NULL)
        *Value = false;

    if (Drv == NULL || Name == NULL || Value == NULL)
        return DTAPI_E_INVALID_ARG;

    Result = GetPropertyValue(Drv, Name, PortIndex, &Raw);
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Value = Raw != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetPropertyStr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The driver fills a fixed field and need not terminate a string that fills it, so the
// length is taken within the field. DTAPI checks the scope only with a debug assertion,
// and this does not check it.
//
unsigned int DtDrvGetPropertyStr(OsDrv* Drv, const char* Name, int PortIndex, char* Str,
                                 size_t Size)
{
    DtIoctlPropCmdGetStrInput In;
    DtIoctlPropCmdGetStrOutput Out;
    size_t Length;
    unsigned int Result;

    if (Str != NULL && Size > 0)
        Str[0] = '\0';

    if (Drv == NULL || Name == NULL || Str == NULL || Size == 0)
        return DTAPI_E_INVALID_ARG;

    Result = InitPropertyInput(&In, DT_PROP_CMD_GET_STR, Name, PortIndex);
    if (!DT_SUCCEEDED(Result))
        return Result;

    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    for (Length = 0; Length < sizeof(Out.m_Str) && Out.m_Str[Length] != '\0'; Length++)
        ;
    if (Length + 1 > Size)
        return DTAPI_E_BUF_TOO_SMALL;

    memcpy(Str, Out.m_Str, Length);
    Str[Length] = '\0';
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvGetIoConfig(OsDrv* Drv, DtIoConfig* Config)
{
    IoConfigGetIn In;
    IoConfigGetOut Out;
    DtIoctlIoConfigValue* Value;
    unsigned int Result;

    if (Drv == NULL || Config == NULL)
        return DTAPI_E_INVALID_ARG;

    memset(&In, 0, sizeof(In));
    InitHeader(&In.m_CmdHdr, DT_IOCONFIG_CMD_GET_IOCONFIG);
    In.m_IoConfigCount = 1;
    In.m_IoCfgId.m_PortIndex = Config->Port - 1;
    Result = DtIoConfigGetName(Config->Group, In.m_IoCfgId.m_Group,
                               sizeof(In.m_IoCfgId.m_Group));
    if (Result != DTAPI_OK)
        return Result;

    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Value = &Out.m_IoCfgValue;
    Result = CodeFromDriver(Value->m_Value, sizeof(Value->m_Value), &Config->Value);
    if (Result != DTAPI_OK)
        return Result;
    Result =
        CodeFromDriver(Value->m_SubValue, sizeof(Value->m_SubValue), &Config->SubValue);
    if (Result != DTAPI_OK)
        return Result;

    Config->ParXtra[0] = Value->m_ParXtra[0];
    Config->ParXtra[1] = Value->m_ParXtra[1];
    if (IsBuddyPort(Config))
        Config->ParXtra[0] = Value->m_ParXtra[0] + 1;

    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvSetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvSetIoConfig(OsDrv* Drv, const DtIoConfig* Config)
{
    IoConfigSetIn In;
    unsigned int Result;

    if (Drv == NULL || Config == NULL)
        return DTAPI_E_INVALID_ARG;

    memset(&In, 0, sizeof(In));
    InitHeader(&In.m_CmdHdr, DT_IOCONFIG_CMD_SET_IOCONFIG);
    In.m_IoConfigCount = 1;

    Result = ConfigToDriver(Config, &In.m_IoCfgPars);
    if (Result != DTAPI_OK)
        return Result;

    // DTAPI skips the driver's exclusive-access check when the configured port is the
    // one its proxy addresses. A device-level request addresses port index -1, so that
    // is only ever the case for a port number of 0, which the device layer has refused
    // before it gets here.
    In.m_IoCfgPars.m_SkipExclAccessCheck = (In.m_IoCfgPars.m_PortIndex == -1) ? 1 : 0;

    return DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvGetTimeOfDay -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvGetTimeOfDay(OsDrv* Drv, uint32_t* Seconds, uint32_t* Nanoseconds)
{
    DtIoctlTodCmdGetTimeInput In;
    DtIoctlTodCmdGetTimeOutput Out;
    unsigned int Result;

    if (Drv == NULL || Seconds == NULL || Nanoseconds == NULL)
        return DTAPI_E_INVALID_ARG;

    InitHeader(&In, DT_TOD_CMD_GET_TIME);
    memset(&Out, 0, sizeof(Out));

    Result =
        DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_TOD_CMD), &In, sizeof(In), &Out, sizeof(Out));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvSdiRxGetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DT_SDIRX_CMD_GET_SDI_STATUS2, converted as DtProxySDIRX::GetSdiStatus does: the flags
// to booleans, the frame period in nanoseconds to a rate, and an SDI rate the driver does
// not define to unknown.
//
unsigned int DtDrvSdiRxGetStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                 DtSdiRxStatus* Status)
{
    DtIoctlSdiRxCmdGetSdiStatusInput In;
    DtIoctlSdiRxCmdGetSdiStatusOutput2 Out;
    unsigned int Result;

    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));

    if (Drv == NULL || Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtDrvInitHeaderFor(&In, DT_SDIRX_CMD_GET_SDI_STATUS2, Uuid, PortIndex);
    memset(&Out, 0, sizeof(Out));

    Result =
        DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_SDIRX_CMD), &In, sizeof(In), &Out, sizeof(Out));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxAttach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvChSdiRxAttach(OsDrv* Drv, int Uuid, int PortIndex, bool Exclusive,
                                const char* FriendlyName)
{
    DtIoctlChSdiRxCmdAttachInput In;
    size_t Length;

    if (Drv == NULL || FriendlyName == NULL)
        return DTAPI_E_INVALID_ARG;

    Length = strlen(FriendlyName);
    if (Length == 0 || Length > DT_CHAN_FRIENDLY_NAME_MAX_LENGTH)
        return DTAPI_E_INVALID_ARG;

    memset(&In, 0, sizeof(In));
    DtDrvInitHeaderFor(&In.m_CmdHdr, DT_CHSDIRX_CMD_ATTACH, Uuid, PortIndex);
    In.m_ReqExclusiveAccess = Exclusive ? 1 : 0;
    memcpy(In.m_FriendlyName, FriendlyName, Length);
    return DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxDetach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvChSdiRxDetach(OsDrv* Drv, int Uuid, int PortIndex)
{
    DtIoctlChSdiRxCmdDetachInput In;

    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtDrvInitHeaderFor(&In, DT_CHSDIRX_CMD_DETACH, Uuid, PortIndex);
    return DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxConfigure -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtProxyCHSDIRX::Configure refuses an SDI rate it cannot convert with
// DTAPI_E_INVALID_RATE before sending anything; so does this.
//
unsigned int DtDrvChSdiRxConfigure(OsDrv* Drv, int Uuid, int PortIndex,
                                   const DtChSdiRxConfig* Config)
{
    DtIoctlChSdiRxCmdConfigureInput In;
    int i;

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

    memset(&In, 0, sizeof(In));
    DtDrvInitHeaderFor(&In.m_CmdHdr, DT_CHSDIRX_CMD_CONFIGURE, Uuid, PortIndex);
    In.m_NumPhysicalPorts = Config->NumPorts;
    for (i = 0; i < Config->NumPorts; i++)
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
    return DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxGetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvChSdiRxGetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int* OpMode)
{
    DtIoctlChSdiRxCmdGetOpModeInput In;
    DtIoctlChSdiRxCmdGetOpModeOutput Out;
    unsigned int Result;

    if (Drv == NULL || OpMode == NULL)
        return DTAPI_E_INVALID_ARG;

    DtDrvInitHeaderFor(&In, DT_CHSDIRX_CMD_GET_OPERATIONAL_MODE, Uuid, PortIndex);
    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *OpMode = Out.m_OpMode;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxSetOpMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvChSdiRxSetOpMode(OsDrv* Drv, int Uuid, int PortIndex, int OpMode)
{
    DtIoctlChSdiRxCmdSetOpModeInput In;

    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    memset(&In, 0, sizeof(In));
    DtDrvInitHeaderFor(&In.m_CmdHdr, DT_CHSDIRX_CMD_SET_OPERATIONAL_MODE, Uuid,
                       PortIndex);
    In.m_OpMode = OpMode;
    return DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxWaitForFmtEvent -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvChSdiRxWaitForFmtEvent(OsDrv* Drv, int Uuid, int PortIndex,
                                         int TimeoutMs, DtChSdiRxEvent* Event)
{
    DtIoctlChSdiRxCmdWaitForFmtEventInput In;
    DtIoctlChSdiRxCmdWaitForFmtEventOutput Out;
    unsigned int Result;

    if (Drv == NULL || Event == NULL)
        return DTAPI_E_INVALID_ARG;

    memset(&In, 0, sizeof(In));
    DtDrvInitHeaderFor(&In.m_CmdHdr, DT_CHSDIRX_CMD_WAIT_FOR_FMT_EVENT, Uuid, PortIndex);
    In.m_Timeout = TimeoutMs;
    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Event->FrameId = Out.m_FrameId;
    Event->SeqNumber = Out.m_SeqNumber;
    Event->InSync = Out.m_InSync != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxGetWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvChSdiRxGetWriteOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                        uint32_t* Offset)
{
    DtIoctlChSdiRxCmdGetWrOffsetInput In;
    DtIoctlChSdiRxCmdGetWrOffsetOutput Out;
    unsigned int Result;

    if (Drv == NULL || Offset == NULL)
        return DTAPI_E_INVALID_ARG;

    DtDrvInitHeaderFor(&In, DT_CHSDIRX_CMD_GET_WRITE_OFFSET, Uuid, PortIndex);
    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Offset = Out.m_WriteOffset;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxSetReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvChSdiRxSetReadOffset(OsDrv* Drv, int Uuid, int PortIndex,
                                       uint32_t Offset)
{
    DtIoctlChSdiRxCmdSetRdOffsetInput In;

    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    memset(&In, 0, sizeof(In));
    DtDrvInitHeaderFor(&In.m_CmdHdr, DT_CHSDIRX_CMD_SET_READ_OFFSET, Uuid, PortIndex);
    In.m_ReadOffset = Offset;
    return DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), NULL, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtDrvChSdiRxGetProps(OsDrv* Drv, int Uuid, int PortIndex,
                                  DtChSdiRxProps* Props)
{
    DtIoctlChSdiRxCmdGetPropsInput In;
    DtIoctlChSdiRxCmdGetPropsOutput Out;
    unsigned int Result;

    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Drv == NULL || Props == NULL)
        return DTAPI_E_INVALID_ARG;

    DtDrvInitHeaderFor(&In, DT_CHSDIRX_CMD_GET_PROPS, Uuid, PortIndex);
    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Props->DmaCaps = Out.m_Dma.m_Caps;
    Props->PrefetchSize = Out.m_Dma.m_PrefetchSize;
    Props->PcieDataWidth = Out.m_Dma.m_PcieDataWidth;
    Props->ReorderBufSize = Out.m_Dma.m_ReorderBufSize;
    Props->StreamAlignment = Out.m_StreamAlignment;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxGetSdiStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtProxyCHSDIRX::GetSdiStatus converts the answer as the SDIRX proxy does, except that
// it leaves the carrier out; so does this.
//
unsigned int DtDrvChSdiRxGetSdiStatus(OsDrv* Drv, int Uuid, int PortIndex,
                                      DtSdiRxStatus* Status)
{
    DtIoctlChSdiRxCmdGetSdiStatusInput In;
    DtIoctlChSdiRxCmdGetSdiStatusOutput Out;
    unsigned int Result;

    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Drv == NULL || Status == NULL)
        return DTAPI_E_INVALID_ARG;

    DtDrvInitHeaderFor(&In, DT_CHSDIRX_CMD_GET_SDI_STATUS, Uuid, PortIndex);
    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxMapDmaBuf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtDrvChSdiRxMapDmaBuf(OsDrv* Drv, int Uuid, int PortIndex, uint8_t** Buffer,
                                   int* BufSize, int* MaxLoad, bool* Mapped)
{
    DtIoctlChSdiRxCmdMapDmaBufToUserInput In;
    DtIoctlChSdiRxCmdMapDmaBufToUserOutput Out;
    unsigned int Result;
    void* Address;

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

    DtDrvInitHeaderFor(&In, DT_CHSDIRX_CMD_MAP_DMA_BUF_TO_USER, Uuid, PortIndex);
    memset(&Out, 0, sizeof(Out));
    Result = DtDrvIssue(Drv, DT_IOCTL(DT_IOCTL_CHSDIRX_CMD), &In, sizeof(In), &Out,
                        sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    if (Out.m_BufSize <= 0 || Out.m_MaxLoad <= 0)
        return DTAPI_E_DEV_DRIVER;

    Address = (void*)(uintptr_t)Out.m_BufferAddr;
    if (Address == NULL)
    {
        uint64_t Offset = (uint64_t)DT_MMAP_PORT_MEM_SEGMENT_SIZE * (uint64_t)PortIndex +
                          (uint64_t)DT_MMAP_PORT_MEM_SEGMENT_SIZE;

        Address = OsDrvMapMemory(Drv, Offset, (size_t)Out.m_BufSize);
        if (Address == NULL)
            return DTAPI_E_OUT_OF_MEM;
        *Mapped = true;
    }

    *Buffer = (uint8_t*)Address;
    *BufSize = Out.m_BufSize;
    *MaxLoad = Out.m_MaxLoad;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDrvChSdiRxUnmapDmaBuf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtDrvChSdiRxUnmapDmaBuf(OsDrv* Drv, uint8_t* Buffer, int BufSize, bool Mapped)
{
    if (Mapped && Buffer != NULL && BufSize > 0)
        OsDrvUnmapMemory(Drv, Buffer, (size_t)BufSize);
}

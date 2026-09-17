// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated DtPcie device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // DTAPI_IOCONFIG_ codes.
#include "Core/DtAlloc.h"           // Allocation seam.
#include "Core/DtAtomic.h"          // The lock around commands.
#include "DtIoConfig.h"             // I/O configuration names, codes and relation.
#include "DtPcieAbi.h"              // The driver ABI the emulator answers in.
#include "OAL/OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.
#include "OAL/OsBackend.h"          // Backend interface being implemented.
#include "OAL/OsThread.h"           // Pacing format events.
#include "SimChSdiRx.h"             // The receive channels.
#include "SimDtPcie.h"              // What the emulated card reports.
#include "SimDta2178.h"             // What the emulated card is.
#include "SimSdiTx.h"               // The transmit blocks.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimDevice
{
    uint32_t LastError;
    int SleepMs; // How long the command just handled waits after the lock is released
} SimDevice;

// Enough for every I/O configuration code.
#define SIM_IOCONFIG_COUNT (DTAPI_IOCONFIG_TODREF_STEADYCLOCK + 1)

typedef struct SimConfig
{
    int Value;
    int SubValue;
    int64_t ParXtra[DT_MAX_PARXTRA_COUNT];
} SimConfig;

typedef struct SimFault
{
    int FunctionCode; // -1 for an unused slot
    bool Short;       // Answer short instead of refusing
    uint32_t Status;  // The status to refuse with
} SimFault;

// Faults for this many function codes can be active at once.
#define SIM_MAX_FAULTS 4

// Overrides for this many properties can be active at once.
#define SIM_MAX_OVERRIDES 8

// Room for the exclusive access of every part the card has.
#define SIM_MAX_PARTS 256

typedef struct SimOverride
{
    bool Active;
    bool IsString; // Replaces a string property rather than a value
    char Name[PROPERTY_NAME_MAX_SIZE];
    int PortIndex;
    bool Present;
    uint32_t Status; // A status to refuse with, 0 for none
    uint64_t Value;
    char Str[PROPERTY_STR_MAX_SIZE];
} SimOverride;

// The card's state, shared by every handle. See the test controls in SimDtPcie.h.
static struct
{
    bool Initialised;
    int Index;
    int FirmwareStatus;
    DtIoctlGetDriverVersionOutput DriverVersion;
    int OpenHandles;
    SimOverride Overrides[SIM_MAX_OVERRIDES];
    SimConfig Config[SIM_PORT_COUNT][SIM_IOCONFIG_COUNT];
    SimFault Faults[SIM_MAX_FAULTS];
    SimSdiSignal Signals[SIM_SDI_PORT_COUNT];
    int SignalDelays[SIM_SDI_PORT_COUNT]; // Status requests the signal is hidden from
    int LastFunctionCode;
    size_t LastInputSize;
    uint8_t LastInput[SIM_MAX_RECORDED_INPUT];
    void* ExclOwners[SIM_MAX_PARTS]; // Per UUID index less one; NULL when nobody holds it
} g_Sim;

// Serialises commands, so that a thread waiting for a format event and another issuing
// commands do not change the emulator's state at the same time. A counter rather than
// an OsMutex, which would be an allocation that tests counting allocations would see.
static DtAtomicInt g_Lock;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Lock(void)
{
    while (DtAtomicIncrement(&g_Lock) != 1)
    {
        DtAtomicDecrement(&g_Lock);
        OsSleepMs(1);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Unlock(void)
{
    DtAtomicDecrement(&g_Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieLock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieLock(void)
{
    Lock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieUnlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieUnlock(void)
{
    Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EnsureState(void)
{
    if (!g_Sim.Initialised)
        SimDtPcieReset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fault for FunctionCode, or NULL when there is none.
//
static const SimFault* FindFault(int FunctionCode)
{
    int i;

    for (i = 0; i < SIM_MAX_FAULTS; i++)
    {
        if (g_Sim.Faults[i].FunctionCode == FunctionCode)
            return &g_Sim.Faults[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddFault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the fault for FunctionCode, replacing an earlier one for the same code. A fault
// beyond the last free slot is a mistake in the test, and is ignored.
//
static void AddFault(int FunctionCode, bool Short, uint32_t Status)
{
    int i;

    EnsureState();
    for (i = 0; i < SIM_MAX_FAULTS; i++)
    {
        SimFault* Fault = &g_Sim.Faults[i];

        if (Fault->FunctionCode == FunctionCode || Fault->FunctionCode == -1)
        {
            Fault->FunctionCode = FunctionCode;
            Fault->Short = Short;
            Fault->Status = Status;
            return;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindOverride -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The override of a value or string property, or NULL when it is not overridden.
//
static SimOverride* FindOverride(const char* Name, int PortIndex, bool IsString)
{
    int i;

    for (i = 0; i < SIM_MAX_OVERRIDES; i++)
    {
        SimOverride* Override = &g_Sim.Overrides[i];

        if (Override->Active && Override->IsString == IsString &&
            Override->PortIndex == PortIndex && strcmp(Override->Name, Name) == 0)
        {
            return Override;
        }
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddOverride -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The slot for an override: the one that already replaces this property, or a free one.
// NULL when all are taken, which is a mistake in the test and is then ignored.
//
static SimOverride* AddOverride(const char* Name, int PortIndex, bool IsString)
{
    SimOverride* Override;
    int i;

    EnsureState();
    Override = FindOverride(Name, PortIndex, IsString);
    for (i = 0; Override == NULL && i < SIM_MAX_OVERRIDES; i++)
    {
        if (!g_Sim.Overrides[i].Active)
            Override = &g_Sim.Overrides[i];
    }
    if (Override == NULL)
        return NULL;

    memset(Override, 0, sizeof(*Override));
    snprintf(Override->Name, sizeof(Override->Name), "%s", Name);
    Override->PortIndex = PortIndex;
    Override->IsString = IsString;
    Override->Active = true;
    return Override;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Checks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimFail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Refuses a command with a DtStatus, which is how the driver refuses one. The emulator
// stands in for the driver, not for the operating system, so it has no other way to
// fail.
//
static int SimFail(SimDevice* Dev, DtStatus Status, uint32_t* DrvStatus)
{
    Dev->LastError = Status;
    *DrvStatus = Status;
    return OS_IOCTL_DRIVER_STATUS;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckSizes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver refuses a request whose input is shorter than the command's input
// structure, or whose output buffer cannot hold the answer, both with
// DT_STATUS_INVALID_PARAMETER and in that order (DtIoStub_IoctlCheckAndReport). Doing
// the same here is what makes the emulator a test of the wire format rather than a stub
// that accepts anything.
//
// Returns OS_IOCTL_OK when the sizes are acceptable.
//
static int CheckSizes(SimDevice* Dev, size_t InSize, size_t InNeeded, const void* Out,
                      const size_t* OutSize, size_t OutNeeded, uint32_t* DrvStatus)
{
    if (InSize < InNeeded)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    if (OutNeeded == 0)
        return OS_IOCTL_OK;

    if (Out == NULL || OutSize == NULL || *OutSize < OutNeeded)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CodeFromName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a name field of a request, which the emulator terminates itself rather than
// trusting the caller to have done so. Returns false for a name that is no code.
//
static bool CodeFromName(const char* Field, int* Code)
{
    char Name[IOCONFIG_NAME_MAX_SIZE];

    memcpy(Name, Field, sizeof(Name));
    Name[sizeof(Name) - 1] = '\0';
    return DtIoConfigGetCode(Name, Code) == DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSupported -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when the port has the capability named after Code, as CAP_<name>, overridden or
// not. The callers pass valid codes only; a name lookup that failed anyway would leave
// the bare prefix, which is no capability.
//
static bool IsSupported(int PortIndex, int Code)
{
    char CapName[4 + IOCONFIG_NAME_MAX_SIZE];
    uint64_t Value = 0;
    int Type;

    const SimOverride* Override;

    memcpy(CapName, "CAP_", 4);
    DtIoConfigGetName(Code, CapName + 4, IOCONFIG_NAME_MAX_SIZE);
    Override = FindOverride(CapName, PortIndex, false);
    if (Override != NULL)
        return Override->Status == 0 && Override->Present && Override->Value != 0;
    return SimDta2178GetProperty(CapName, PortIndex, &Type, &Value) && Value != 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetDriverVersion(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize,
                            uint32_t* DrvStatus)
{
    DtIoctlGetDriverVersionOutput* Version;
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlGetDriverVersionInput), Out,
                             OutSize, sizeof(DtIoctlGetDriverVersionOutput), DrvStatus);

    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    Version = (DtIoctlGetDriverVersionOutput*)Out;
    *Version = g_Sim.DriverVersion;

    *OutSize = sizeof(DtIoctlGetDriverVersionOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDevInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers GET_DEV_INFO and GET_DEV_INFO2. The two differ only in the PCIe part: the
// original has no slot power, which the driver then leaves zero.
//
static int GetDevInfo(SimDevice* Dev, int FunctionCode, size_t InSize, void* Out,
                      size_t* OutSize, uint32_t* DrvStatus)
{
    DtIoctlGetDevInfoOutput* Info;
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlGetDevInfoInput), Out, OutSize,
                             sizeof(DtIoctlGetDevInfoOutput), DrvStatus);

    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    Info = (DtIoctlGetDevInfoOutput*)Out;
    memset(Info, 0, sizeof(*Info));

    Info->m_TypeNumber = SIM_TYPE_NUMBER;
    Info->m_SubType = 0;
    Info->m_SubDvc = 0;
    Info->m_Serial = SIM_SERIAL;
    Info->m_HardwareRevision = SIM_HARDWARE_REVISION;
    Info->m_FirmwareVersion = SIM_FIRMWARE_VERSION;
    Info->m_FirmwareVariant = SIM_FIRMWARE_VARIANT;
    Info->m_FwPackageVersion = -1;
    Info->m_FirmwareStatus = g_Sim.FirmwareStatus;
    Info->m_VendorId = SIM_VENDOR_ID;
    Info->m_DeviceId = SIM_DEVICE_ID;
    Info->m_SubVendorId = SIM_SUBSYSTEM_VENDOR_ID;
    Info->m_SubSystemId = SIM_SUBSYSTEM_ID;
    Info->m_FwBuildDate.m_Year = SIM_FW_BUILD_YEAR;
    Info->m_FwBuildDate.m_Month = SIM_FW_BUILD_MONTH;
    Info->m_FwBuildDate.m_Day = SIM_FW_BUILD_DAY;
    Info->m_FwBuildDate.m_Hour = SIM_FW_BUILD_HOUR;
    Info->m_FwBuildDate.m_Minute = SIM_FW_BUILD_MINUTE;
    Info->m_DevSpecific.m_Pcie2.m_BusNumber = SIM_BUS_NUMBER;
    Info->m_DevSpecific.m_Pcie2.m_SlotNumber = SIM_SLOT_NUMBER;
    Info->m_DevSpecific.m_Pcie2.m_PcieNumLanes = SIM_PCIE_NUM_LANES;
    Info->m_DevSpecific.m_Pcie2.m_PcieMaxLanes = SIM_PCIE_MAX_LANES;
    Info->m_DevSpecific.m_Pcie2.m_PcieLinkSpeed = SIM_PCIE_LINK_SPEED;
    Info->m_DevSpecific.m_Pcie2.m_PcieMaxSpeed = SIM_PCIE_MAX_SPEED;
    Info->m_DevSpecific.m_Pcie2.m_PcieMaxPayloadSize = SIM_PCIE_MAX_PAYLOAD_SIZE;
    Info->m_DevSpecific.m_Pcie2.m_PcieMaxReadRequestSize = SIM_PCIE_MAX_READ_REQUEST_SIZE;
    if (FunctionCode == DT_FUNC_CODE_GET_DEV_INFO2)
        Info->m_DevSpecific.m_Pcie2.m_PcieMaxSlotPower = SIM_PCIE_MAX_SLOT_POWER;

    *OutSize = sizeof(DtIoctlGetDevInfoOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PropertyCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reading a string: an override, or the card's string property with that name. The
// string is copied into the fixed field, which it may fill without a terminator.
//
static int PropertyGetStr(SimDevice* Dev, const void* In, size_t InSize, void* Out,
                          size_t* OutSize, uint32_t* DrvStatus)
{
    DtIoctlPropCmdGetStrInput Request;
    DtIoctlPropCmdGetStrOutput* Answer;
    const SimOverride* Override;
    const char* Str = NULL;
    size_t Length;
    int Outcome;

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlPropCmdGetStrInput), Out, OutSize,
                         sizeof(DtIoctlPropCmdGetStrOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    memcpy(&Request, In, sizeof(Request));
    Request.m_Name[sizeof(Request.m_Name) - 1] = '\0';

    Answer = (DtIoctlPropCmdGetStrOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));

    Override = FindOverride(Request.m_Name, Request.m_PortIndex, true);
    if (Override != NULL)
    {
        if (Override->Status != 0)
            return SimFail(Dev, (DtStatus)Override->Status, DrvStatus);
        if (!Override->Present)
            return SimFail(Dev, DT_STATUS_NOT_FOUND, DrvStatus);
        memcpy(Answer->m_Str, Override->Str, sizeof(Answer->m_Str));
    }
    else if (SimDta2178GetString(Request.m_Name, Request.m_PortIndex, &Str))
    {
        Length = strlen(Str);
        memcpy(Answer->m_Str, Str,
               Length < sizeof(Answer->m_Str) ? Length : sizeof(Answer->m_Str));
    }
    else
        return SimFail(Dev, DT_STATUS_NOT_FOUND, DrvStatus);

    Answer->m_Scope = (DtPropertyScope)(PROPERTY_SCOPE_DTAPI | PROPERTY_SCOPE_DRIVER);
    *OutSize = sizeof(DtIoctlPropCmdGetStrOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PropertyCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reading a value and a string are modelled; tables are refused as unknown commands.
//
static int PropertyCmd(SimDevice* Dev, int Cmd, const void* In, size_t InSize, void* Out,
                       size_t* OutSize, uint32_t* DrvStatus)
{
    DtIoctlPropCmdGetValueInput Request;
    DtIoctlPropCmdGetValueOutput* Answer;
    const SimOverride* Override;
    uint64_t Value = 0;
    int Type = 0;
    int Outcome;

    if (Cmd == DT_PROP_CMD_GET_STR)
        return PropertyGetStr(Dev, In, InSize, Out, OutSize, DrvStatus);

    if (Cmd != DT_PROP_CMD_GET_VALUE)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlPropCmdGetValueInput), Out, OutSize,
                         sizeof(DtIoctlPropCmdGetValueOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    memcpy(&Request, In, sizeof(Request));
    Request.m_Name[sizeof(Request.m_Name) - 1] = '\0';

    Override = FindOverride(Request.m_Name, Request.m_PortIndex, false);
    if (Override != NULL)
    {
        if (Override->Status != 0)
            return SimFail(Dev, (DtStatus)Override->Status, DrvStatus);
        if (!Override->Present)
            return SimFail(Dev, DT_STATUS_NOT_FOUND, DrvStatus);
        Value = Override->Value;
        Type = strncmp(Request.m_Name, "CAP_", 4) == 0 ? PROPERTY_VALUE_TYPE_BOOL
                                                       : PROPERTY_VALUE_TYPE_INT;
    }
    else if (!SimDta2178GetProperty(Request.m_Name, Request.m_PortIndex, &Type, &Value))
        return SimFail(Dev, DT_STATUS_NOT_FOUND, DrvStatus);

    Answer = (DtIoctlPropCmdGetValueOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_Scope = PROPERTY_SCOPE_DTAPI | PROPERTY_SCOPE_DRIVER;
    Answer->m_Type = Type;
    Answer->m_Value = Value;

    *OutSize = sizeof(DtIoctlPropCmdGetValueOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The request ends in a flexible array whose length it gives itself, so the sizes are
// checked in two steps, as the driver does (DtIoStubCfIoCfg_AppendDynamicSize). A port
// index or group out of range is an invalid parameter, as in DtCfIoCfg_Get.
//
static int GetIoConfig(SimDevice* Dev, const void* In, size_t InSize, void* Out,
                       size_t* OutSize, uint32_t* DrvStatus)
{
    const DtIoctlIoConfigCmdGetIoConfigInput* Request;
    DtIoctlIoConfigCmdGetIoConfigOutput* Answer;
    size_t InNeeded, OutNeeded, Count, i;
    int Outcome;

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlIoConfigCmdGetIoConfigInput), Out,
                         OutSize, sizeof(DtIoctlIoConfigCmdGetIoConfigOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    Request = (const DtIoctlIoConfigCmdGetIoConfigInput*)In;
    if (Request->m_IoConfigCount < 0)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    Count = (size_t)Request->m_IoConfigCount;
    InNeeded =
        sizeof(DtIoctlIoConfigCmdGetIoConfigInput) + Count * sizeof(DtIoctlIoConfigId);
    OutNeeded = sizeof(DtIoctlIoConfigCmdGetIoConfigOutput) +
                Count * sizeof(DtIoctlIoConfigValue);
    Outcome = CheckSizes(Dev, InSize, InNeeded, Out, OutSize, OutNeeded, DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    Answer = (DtIoctlIoConfigCmdGetIoConfigOutput*)Out;
    for (i = 0; i < Count; i++)
    {
        const DtIoctlIoConfigId* Id = &Request->m_IoCfgId[i];
        DtIoctlIoConfigValue* Value = &Answer->m_IoCfgValue[i];
        const SimConfig* Config;
        int Group;
        int j;

        if (!CodeFromName(Id->m_Group, &Group) || Group < 0 || Id->m_PortIndex < 0 ||
            Id->m_PortIndex >= SIM_PORT_COUNT)
        {
            return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);
        }

        Config = &g_Sim.Config[Id->m_PortIndex][Group];
        memset(Value, 0, sizeof(*Value));
        DtIoConfigGetName(Config->Value, Value->m_Value, sizeof(Value->m_Value));
        DtIoConfigGetName(Config->SubValue, Value->m_SubValue, sizeof(Value->m_SubValue));
        for (j = 0; j < DT_MAX_PARXTRA_COUNT; j++)
            Value->m_ParXtra[j] = Config->ParXtra[j];
    }

    Answer->m_IoConfigCount = (Int)Count;
    *OutSize = OutNeeded;
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Every configuration in the request is checked before any is applied, so a request
// that fails changes nothing. A port index or group out of range is an invalid parameter,
// as in DtCfIoCfg_Set; a combination the port cannot take is a configuration error, the
// status of the driver's per-group validation. An unknown name, which the driver only
// asserts on, is treated as an invalid parameter.
//
static int SetIoConfig(SimDevice* Dev, const void* In, size_t InSize, uint32_t* DrvStatus)
{
    const DtIoctlIoConfigCmdSetIoConfigInput* Request;
    size_t Count, i;
    int Outcome;

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlIoConfigCmdSetIoConfigInput), NULL,
                         NULL, 0, DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    Request = (const DtIoctlIoConfigCmdSetIoConfigInput*)In;
    if (Request->m_IoConfigCount < 0)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    Count = (size_t)Request->m_IoConfigCount;
    Outcome = CheckSizes(Dev, InSize,
                         sizeof(DtIoctlIoConfigCmdSetIoConfigInput) +
                             Count * sizeof(DtIoctlIoConfig),
                         NULL, NULL, 0, DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    for (i = 0; i < Count; i++)
    {
        const DtIoctlIoConfig* Pars = &Request->m_IoCfgPars[i];
        int Port = Pars->m_PortIndex;
        int Group, Value, SubValue;
        bool Supported;

        if (!CodeFromName(Pars->m_Group, &Group) ||
            !CodeFromName(Pars->m_Value, &Value) ||
            !CodeFromName(Pars->m_SubValue, &SubValue) || Group < 0 || Port < 0 ||
            Port >= SIM_PORT_COUNT)
        {
            return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);
        }

        // A boolean I/O capability must itself be supported; any other group needs its
        // value and sub-value to be.
        if (DtIoConfigIsValid(Group, Value, SubValue) != DTAPI_OK)
            Supported = false;
        else if (Value == DTAPI_IOCONFIG_TRUE || Value == DTAPI_IOCONFIG_FALSE)
            Supported = IsSupported(Port, Group);
        else
            Supported = IsSupported(Port, Value) &&
                        (SubValue == -1 || IsSupported(Port, SubValue));

        if (!Supported)
            return SimFail(Dev, DT_STATUS_CONFIG_ERROR, DrvStatus);
    }

    for (i = 0; i < Count; i++)
    {
        const DtIoctlIoConfig* Pars = &Request->m_IoCfgPars[i];
        SimConfig* Config;
        int Group = -1;
        int j;

        CodeFromName(Pars->m_Group, &Group);
        Config = &g_Sim.Config[Pars->m_PortIndex][Group];
        CodeFromName(Pars->m_Value, &Config->Value);
        CodeFromName(Pars->m_SubValue, &Config->SubValue);
        for (j = 0; j < DT_MAX_PARXTRA_COUNT; j++)
            Config->ParXtra[j] = Pars->m_ParXtra[j];
    }

    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IoConfigCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int IoConfigCmd(SimDevice* Dev, int Cmd, const void* In, size_t InSize, void* Out,
                       size_t* OutSize, uint32_t* DrvStatus)
{
    switch (Cmd)
    {
    case DT_IOCONFIG_CMD_GET_IOCONFIG:
        return GetIoConfig(Dev, In, InSize, Out, OutSize, DrvStatus);
    case DT_IOCONFIG_CMD_SET_IOCONFIG:
        return SetIoConfig(Dev, In, InSize, DrvStatus);
    default:
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TodCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Only reading the time is modelled. The clock is the host's UTC time, which is what a
// card synchronised to the host would read.
//
static int TodCmd(SimDevice* Dev, int Cmd, size_t InSize, void* Out, size_t* OutSize,
                  uint32_t* DrvStatus)
{
    DtIoctlTodCmdGetTimeOutput* Answer;
    struct timespec Now;
    int Outcome;

    if (Cmd != DT_TOD_CMD_GET_TIME)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlTodCmdGetTimeInput), Out, OutSize,
                         sizeof(DtIoctlTodCmdGetTimeOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    timespec_get(&Now, TIME_UTC);

    Answer = (DtIoctlTodCmdGetTimeOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_Time.m_Seconds = (UInt32)Now.tv_sec;
    Answer->m_Time.m_Nanoseconds = (UInt32)Now.tv_nsec;
    Answer->m_AdjustmentCount = 0;

    *OutSize = sizeof(DtIoctlTodCmdGetTimeOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The SDI receiver of the port at PortIndex. Only reading its status is modelled. Its
// sizes are checked first, then whether the function is enabled, which it is while the
// port is an input (DtDfSdiRx_GetSdiStatus). In ASI mode only the carrier is reported.
//
static int SdiRxCmd(SimDevice* Dev, int PortIndex, int Cmd, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    const SimSdiSignal* Signal = &g_Sim.Signals[PortIndex];
    DtIoctlSdiRxCmdGetSdiStatusOutput2* Answer;
    int Outcome;

    if (Cmd != DT_SDIRX_CMD_GET_SDI_STATUS2)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlSdiRxCmdGetSdiStatusInput), Out,
                         OutSize, sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    if (g_Sim.Config[PortIndex][DTAPI_IOCONFIG_IODIR].Value != DTAPI_IOCONFIG_INPUT)
        return SimFail(Dev, DT_STATUS_NOT_ENABLED, DrvStatus);

    Answer = (DtIoctlSdiRxCmdGetSdiStatusOutput2*)Out;
    memset(Answer, 0, sizeof(*Answer));
    *OutSize = sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2);

    if (g_Sim.SignalDelays[PortIndex] > 0)
    {
        g_Sim.SignalDelays[PortIndex]--;
        Answer->m_SdiRate = DT_DRV_SDIRATE_UNKNOWN;
        return OS_IOCTL_OK;
    }

    Answer->m_CarrierDetect = Signal->CarrierDetect;

    if (g_Sim.Config[PortIndex][DTAPI_IOCONFIG_IOSTD].Value == DTAPI_IOCONFIG_ASI)
        Answer->m_SdiRate = DT_DRV_SDIRATE_UNKNOWN;
    else
    {
        Answer->m_SdiLock = Signal->SdiLock;
        Answer->m_LineLock = Signal->LineLock;
        Answer->m_Valid = Signal->Valid;
        Answer->m_NumSymsHanc = Signal->NumSymsHanc;
        Answer->m_NumSymsVidVanc = Signal->NumSymsVidVanc;
        Answer->m_NumLinesF1 = Signal->NumLinesF1;
        Answer->m_NumLinesF2 = Signal->NumLinesF2;
        Answer->m_IsLevelB = Signal->IsLevelB;
        Answer->m_PayloadId = Signal->PayloadId;
        Answer->m_FramePeriod = Signal->FramePeriod;
        Answer->m_SdiRate = Signal->SdiRate;
    }
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ChSdiRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The receive channel of the port at PortIndex. Its status is the receiver's, which the
// emulator keeps here; everything else is the channel's own.
//
static int ChSdiRxCmd(SimDevice* Dev, int PortIndex, int Cmd, const void* In,
                      size_t InSize, void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    uint32_t Status;

    if (Cmd == DT_CHSDIRX_CMD_GET_SDI_STATUS)
    {
        size_t Size = OutSize != NULL ? *OutSize : 0;
        int Outcome;

        // The channel's status answer has the layout of GET_SDI_STATUS2's.
        _Static_assert(sizeof(DtIoctlChSdiRxCmdGetSdiStatusOutput) ==
                           sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2),
                       "The two status answers must have one layout");
        Outcome = SdiRxCmd(Dev, PortIndex, DT_SDIRX_CMD_GET_SDI_STATUS2, InSize, Out,
                           &Size, DrvStatus);
        if (Outcome == OS_IOCTL_OK)
        {
            ((DtIoctlChSdiRxCmdGetSdiStatusOutput*)Out)->m_CarrierDetect = 0;
            *OutSize = Size;
        }
        return Outcome;
    }

    Status = SimChSdiRxCmd(Dev, PortIndex, Cmd, In, InSize, Out, OutSize, &Dev->SleepMs);
    if (Status != DT_STATUS_OK)
        return SimFail(Dev, Status, DrvStatus);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiTxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A command for a transmit block of the port at PortIndex. The blocks are enabled while
// the port is an output with an SDI I/O standard.
//
static int SdiTxCmd(SimDevice* Dev, int Uuid, int PortIndex, int FunctionCode, int Type,
                    const char* Role, int Cmd, const void* In, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    const SimConfig* Config = g_Sim.Config[PortIndex];
    bool Enabled = Config[DTAPI_IOCONFIG_IODIR].Value == DTAPI_IOCONFIG_OUTPUT &&
                   Config[DTAPI_IOCONFIG_IOSTD].Value != DTAPI_IOCONFIG_ASI;
    uint32_t Access = SimDtPcieCheckAccess(Dev, (Uuid & DT_UUID_INDEX_MASK) - 1);
    uint32_t Status = SimSdiTxCmd(Dev, PortIndex, FunctionCode, Type, Role, Cmd, Access,
                                  Enabled, Config[DTAPI_IOCONFIG_IOSTD].SubValue, In,
                                  InSize, Out, OutSize, &Dev->SleepMs);

    if (Status != DT_STATUS_OK)
        return SimFail(Dev, Status, DrvStatus);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExclAccessCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// EXCL_ACCESS_CMD for the part at PartIndex, with the rules of DtBc_ExclAccess* and
// DtDf_ExclAccess*, the owner being the handle.
//
static int ExclAccessCmd(SimDevice* Dev, int PartIndex, int Cmd, uint32_t* DrvStatus)
{
    void** Owner;

    if (PartIndex < 0 || PartIndex >= SIM_MAX_PARTS)
        return SimFail(Dev, DT_STATUS_NO_IOSTUB, DrvStatus);
    Owner = &g_Sim.ExclOwners[PartIndex];

    switch (Cmd)
    {
    case DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE:
        if (*Owner != NULL)
            return SimFail(Dev, DT_STATUS_IN_USE, DrvStatus);
        *Owner = Dev;
        return OS_IOCTL_OK;
    case DT_EXCLUSIVE_ACCESS_CMD_RELEASE:
        if (*Owner != NULL && *Owner != Dev)
            return SimFail(Dev, DT_STATUS_IN_USE, DrvStatus);
        *Owner = NULL;
        return OS_IOCTL_OK;
    case DT_EXCLUSIVE_ACCESS_CMD_PROBE:
        if (*Owner != NULL)
            return SimFail(Dev, DT_STATUS_IN_USE, DrvStatus);
        return OS_IOCTL_OK;
    case DT_EXCLUSIVE_ACCESS_CMD_CHECK:
    {
        uint32_t Status = SimDtPcieCheckAccess(Dev, PartIndex);

        if (Status != DT_STATUS_OK)
            return SimFail(Dev, Status, DrvStatus);
        return OS_IOCTL_OK;
    }
    default:
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* SimOpen(int Index)
{
    SimDevice* Dev;

    EnsureState();

    if (Index != g_Sim.Index)
        return NULL;

    Dev = (SimDevice*)DtMalloc(sizeof(SimDevice));
    if (Dev == NULL)
        return NULL;

    Dev->LastError = 0;
    Dev->SleepMs = 0;
    g_Sim.OpenHandles++;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void SimClose(void* State)
{
    int i;

    Lock();
    SimChSdiRxCloseHandle(State);
    SimSdiTxCloseHandle(State);
    for (i = 0; i < SIM_MAX_PARTS; i++)
    {
        if (g_Sim.ExclOwners[i] == State)
            g_Sim.ExclOwners[i] = NULL;
    }
    g_Sim.OpenHandles--;
    Unlock();
    DtFree(State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Dispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Dispatches on the function code rather than on the whole IOCTL number, exactly as the
// driver does. On Linux the number also encodes the argument size, so matching on it
// would accept only the one structure size this build happened to be compiled with.
//
// The header's UUID picks the target first, as in DtCore_Ioctl: 0 is the device itself,
// which must be addressed with port index -1; a UUID flagged as a building block or
// driver function is looked up by its index, whatever the port index says; anything
// else, and a UUID the card does not have, has no I/O stub. A target refuses a command it
// does not handle with DT_STATUS_NOT_SUPPORTED, before its sizes are looked at, as the
// driver does.
//
static int Dispatch(SimDevice* Dev, int FunctionCode, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    const DtIoctlInputDataHdr* Hdr = (const DtIoctlInputDataHdr*)In;
    int Cmd = Hdr->m_Cmd;
    const char* Role = NULL;
    int PortIndex, Type;

    if (Hdr->m_Uuid != DT_UUID_CORE)
    {
        if ((Hdr->m_Uuid & (DT_UUID_BC_FLAG | DT_UUID_DF_FLAG)) == 0 ||
            !SimDta2178FindFunction(Hdr->m_Uuid, &PortIndex, &Type, &Role))
        {
            return SimFail(Dev, DT_STATUS_NO_IOSTUB, DrvStatus);
        }

        if (FunctionCode == DT_FUNC_CODE_EXCL_ACCESS_CMD)
        {
            if (InSize < sizeof(DtIoctlExclAccessCmdInput))
                return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);
            return ExclAccessCmd(Dev, (Hdr->m_Uuid & DT_UUID_INDEX_MASK) - 1, Cmd,
                                 DrvStatus);
        }
        if (SimSdiTxTakes(FunctionCode))
            return SdiTxCmd(Dev, Hdr->m_Uuid, PortIndex, FunctionCode, Type, Role, Cmd,
                            In, InSize, Out, OutSize, DrvStatus);
        if ((Hdr->m_Uuid & DT_UUID_DF_FLAG) != 0 && Type == DT_FUNC_TYPE_SDIRX &&
            FunctionCode == DT_FUNC_CODE_SDIRX_CMD)
        {
            return SdiRxCmd(Dev, PortIndex, Cmd, InSize, Out, OutSize, DrvStatus);
        }
        if ((Hdr->m_Uuid & DT_UUID_DF_FLAG) != 0 && Type == DT_FUNC_TYPE_CHSDIRX &&
            FunctionCode == DT_FUNC_CODE_CHSDIRX_CMD)
        {
            return ChSdiRxCmd(Dev, PortIndex, Cmd, In, InSize, Out, OutSize, DrvStatus);
        }
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
    }

    if (Hdr->m_PortIndex != -1)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    switch (FunctionCode)
    {
    case DT_FUNC_CODE_GET_DRIVER_VERSION:
        return GetDriverVersion(Dev, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_GET_DEV_INFO:
    case DT_FUNC_CODE_GET_DEV_INFO2:
        return GetDevInfo(Dev, FunctionCode, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_PROPERTY_CMD:
        return PropertyCmd(Dev, Cmd, In, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_IOCONFIG_CMD:
        return IoConfigCmd(Dev, Cmd, In, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_TOD_CMD:
        return TodCmd(Dev, Cmd, InSize, Out, OutSize, DrvStatus);
    default:
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimIoCtlLocked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver refuses an input too short to hold the common header before it looks at
// anything else (DtCore_Ioctl). OsDrvIoCtl has already refused a request without input.
//
static int SimIoCtlLocked(SimDevice* Dev, int FunctionCode, const void* In, size_t InSize,
                          void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    const SimFault* Fault = FindFault(FunctionCode);
    int Outcome;

    g_Sim.LastFunctionCode = FunctionCode;
    g_Sim.LastInputSize = InSize;
    memcpy(g_Sim.LastInput, In,
           InSize < sizeof(g_Sim.LastInput) ? InSize : sizeof(g_Sim.LastInput));

    if (InSize < sizeof(DtIoctlInputDataHdr))
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    if (Fault != NULL && !Fault->Short)
        return SimFail(Dev, Fault->Status, DrvStatus);

    Outcome = Dispatch(Dev, FunctionCode, In, InSize, Out, OutSize, DrvStatus);

    if (Outcome == OS_IOCTL_OK && Fault != NULL && OutSize != NULL && *OutSize > 0)
        (*OutSize)--;

    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimIoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One command at a time. A wait that paces its events sleeps after the lock is released.
//
static int SimIoCtl(void* State, uint32_t Code, const void* In, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    SimDevice* Dev = (SimDevice*)State;
    int FunctionCode = (int)DT_IOCTL_TO_FUNCTION(Code);
    int Outcome;

    Lock();
    Dev->SleepMs = 0;
    Outcome = SimIoCtlLocked(Dev, FunctionCode, In, InSize, Out, OutSize, DrvStatus);
    Unlock();

    if (Dev->SleepMs > 0)
        OsSleepMs(Dev->SleepMs);
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimMapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void* SimMapMemory(void* State, uint64_t Offset, size_t Size)
{
    void* Address;

    Lock();
    Address = SimChSdiRxMap(State, Offset, Size);
    Unlock();
    return Address;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimUnmapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The ring stays the channel's, so unmapping it releases nothing.
//
static void SimUnmapMemory(void* State, void* Address, size_t Size)
{
    (void)State;
    (void)Address;
    (void)Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t SimLastError(const void* State)
{
    return ((const SimDevice*)State)->LastError;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieReset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieReset(void)
{
    int Port, Group, j;

    for (Port = 0; Port < SIM_PORT_COUNT; Port++)
    {
        for (Group = 0; Group < SIM_IOCONFIG_COUNT; Group++)
        {
            SimConfig* Config = &g_Sim.Config[Port][Group];

            SimDta2178DefaultConfig(Port, Group, &Config->Value, &Config->SubValue);
            for (j = 0; j < DT_MAX_PARXTRA_COUNT; j++)
                Config->ParXtra[j] = -1;
        }
    }

    SimChSdiRxReset();
    SimSdiTxReset();

    for (j = 0; j < SIM_MAX_FAULTS; j++)
        g_Sim.Faults[j].FunctionCode = -1;

    g_Sim.LastFunctionCode = -1;
    g_Sim.LastInputSize = 0;
    memset(g_Sim.ExclOwners, 0, sizeof(g_Sim.ExclOwners));

    for (j = 0; j < SIM_MAX_OVERRIDES; j++)
        g_Sim.Overrides[j].Active = false;

    // Initialised before the signals are cleared, which checks it.
    g_Sim.Initialised = true;
    for (j = 0; j < SIM_SDI_PORT_COUNT; j++)
        SimDtPcieSetSdiSignal(j, NULL);

    g_Sim.Index = SIM_DEVICE_INDEX;
    g_Sim.FirmwareStatus = DT_FWSTATUS_UPTODATE;
    g_Sim.DriverVersion.m_Major = SIM_DRIVER_MAJOR;
    g_Sim.DriverVersion.m_Minor = SIM_DRIVER_MINOR;
    g_Sim.DriverVersion.m_Micro = SIM_DRIVER_MICRO;
    g_Sim.DriverVersion.m_Build = SIM_DRIVER_BUILD;

    g_Sim.Initialised = true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieFailWithStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieFailWithStatus(int FunctionCode, uint32_t Status)
{
    AddFault(FunctionCode, false, Status);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieAnswerShort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieAnswerShort(int FunctionCode)
{
    AddFault(FunctionCode, true, DT_STATUS_OK);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetFirmwareStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcieSetFirmwareStatus(int Status)
{
    EnsureState();
    g_Sim.FirmwareStatus = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieSetDriverVersion(int Major, int Minor, int Micro, int Build)
{
    EnsureState();
    g_Sim.DriverVersion.m_Major = Major;
    g_Sim.DriverVersion.m_Minor = Minor;
    g_Sim.DriverVersion.m_Micro = Micro;
    g_Sim.DriverVersion.m_Build = Build;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieDelaySdiSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieDelaySdiSignal(int PortIndex, int Reads)
{
    EnsureState();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        g_Sim.SignalDelays[PortIndex] = Reads;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieOverrideProperty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieOverrideProperty(const char* Name, int PortIndex, bool Present,
                               uint64_t Value)
{
    SimOverride* Override = AddOverride(Name, PortIndex, false);

    if (Override == NULL)
        return;
    Override->Present = Present;
    Override->Value = Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieOverrideString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The driver's string field holds PROPERTY_STR_MAX_SIZE characters and need not be
// terminated when full, so the override keeps up to that many.
//
void SimDtPcieOverrideString(const char* Name, int PortIndex, bool Present,
                             const char* Value)
{
    SimOverride* Override = AddOverride(Name, PortIndex, true);
    size_t Length;

    if (Override == NULL)
        return;
    Override->Present = Present;
    if (!Present || Value == NULL)
        return;

    Length = strlen(Value);
    if (Length > sizeof(Override->Str))
        Length = sizeof(Override->Str);
    memcpy(Override->Str, Value, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetSdiSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieFailProperty(const char* Name, int PortIndex, bool IsString,
                           uint32_t Status)
{
    SimOverride* Override = AddOverride(Name, PortIndex, IsString);

    if (Override != NULL)
        Override->Status = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetSdiSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieSetSdiSignal(int PortIndex, const SimSdiSignal* Signal)
{
    SimSdiSignal* Port;

    EnsureState();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;

    Port = &g_Sim.Signals[PortIndex];
    g_Sim.SignalDelays[PortIndex] = 0;
    if (Signal != NULL)
    {
        *Port = *Signal;
        return;
    }
    memset(Port, 0, sizeof(*Port));
    Port->SdiRate = DT_DRV_SDIRATE_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieSetIndex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcieSetIndex(int Index)
{
    EnsureState();
    g_Sim.Index = Index;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieOpenHandles -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int SimDtPcieOpenHandles(void)
{
    return g_Sim.OpenHandles;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieLastInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t SimDtPcieLastInput(int* FunctionCode, void* Buf, size_t Size)
{
    size_t Kept;

    EnsureState();
    *FunctionCode = g_Sim.LastFunctionCode;
    Kept = g_Sim.LastInputSize < sizeof(g_Sim.LastInput) ? g_Sim.LastInputSize
                                                         : sizeof(g_Sim.LastInput);
    memcpy(Buf, g_Sim.LastInput, Size < Kept ? Size : Kept);
    return g_Sim.LastInputSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcieCheckAccess -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t SimDtPcieCheckAccess(void* Handle, int PartIndex)
{
    void* Owner;

    if (PartIndex < 0 || PartIndex >= SIM_MAX_PARTS)
        return DT_STATUS_EXCL_ACCESS_REQD;
    Owner = g_Sim.ExclOwners[PartIndex];
    if (Owner == NULL)
        return DT_STATUS_EXCL_ACCESS_REQD;
    return Owner == Handle ? DT_STATUS_OK : DT_STATUS_IN_USE;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Selection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSimBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const OsBackend* OsSimBackend(void)
{
    static const OsBackend Backend = {SimOpen,      SimClose,     SimIoCtl,
                                      SimLastError, SimMapMemory, SimUnmapMemory};
    return &Backend;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSimIsRequested -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool OsSimIsRequested(void)
{
    static int Cached = -1;

    // Any value other than empty or "0" turns the emulator on, so that both
    // CDTAPILITE_SIM=1 and CDTAPILITE_SIM=yes work and CDTAPILITE_SIM=0 does not.
    if (Cached < 0)
    {
        const char* Value = getenv("CDTAPILITE_SIM");
        Cached = (Value != NULL && Value[0] != '\0' && strcmp(Value, "0") != 0) ? 1 : 0;
    }

    return Cached == 1;
}

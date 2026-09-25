// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated DtPcie device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// CDTAPI includes
#include "Core/DtAlloc.h"           // Allocation seam.
#include "Core/DtAtomic.h"          // The lock around commands.
#include "DtIoConfig.h"             // I/O configuration names, codes and relation.
#include "DtPcieAbi.h"              // The driver ABI the emulator answers in.
#include "OAL/OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.
#include "OAL/OsBackend.h"          // Backend interface being implemented.
#include "OAL/OsThread.h"           // Pacing format events.
#include "SimActivate.h"            // The activation object.
#include "SimAsi.h"                 // The ASI blocks.
#include "SimChSdiRx.h"             // The receive channels.
#include "SimClocks.h"              // The genlock, time-of-day and transmit clocks.
#include "SimDtPcie.h"              // What the emulated card reports.
#include "SimDta2110.h"             // What the emulated DTA-2110 is.
#include "SimDta2178.h"             // What the emulated card is.
#include "SimNet.h"                 // The DTA-2110's interface in the network.
#include "SimNw.h"                  // The DTA-2110's network function.
#include "SimSdiTx.h"               // The transmit blocks.
#include "SimVpd.h"                 // The card's own EEPROM.
#include "Video/DtFrameProps.h"     // The signal of a file source's standard.
#include "Video/DtVidStd.h"         // Its level and I/O standard.
#include "cdtapi.h"                 // DTAPI_IOCONFIG_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimDevice
{
    uint32_t LastError;
    int SleepMs;    // How long the command just handled waits after the lock is released
    bool IsDta2110; // The handle is to the DTA-2110 rather than to the DTA-2178
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

// Room for the exclusive access of every object the card has.
#define SIM_MAX_OBJECTS 256

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
    int Dta2110Index; // -1 while there is no DTA-2110
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
    void*
        ExclOwners[SIM_MAX_OBJECTS]; // Per UUID index less one; NULL when nobody holds it
    void* ExclOwners2110[SIM_MAX_OBJECTS]; // The same for the DTA-2110
} g_Sim;

// Serialises commands, so that a thread waiting for a format event and another issuing
// commands do not change the emulator's state at the same time. A counter rather than
// an OsMutex, which would be an allocation that tests counting allocations would see.
static DtAtomicInt g_Lock;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Lock(void)
{
    while (DtAtomic_Increment(&g_Lock) != 1)
    {
        DtAtomic_Decrement(&g_Lock);
        OsTime_SleepMs(1);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Unlock(void)
{
    DtAtomic_Decrement(&g_Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_Lock(void)
{
    Lock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_Unlock(void)
{
    Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EnsureState -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EnsureState(void)
{
    if (!g_Sim.Initialised)
        SimDtPcie_Reset();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindFault -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fault for FunctionCode, or NULL when there is none.
//
static const SimFault* FindFault(int FunctionCode)
{
    for (int i = 0; i < SIM_MAX_FAULTS; i++)
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
    EnsureState();
    for (int i = 0; i < SIM_MAX_FAULTS; i++)
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
    for (int i = 0; i < SIM_MAX_OVERRIDES; i++)
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
    EnsureState();
    SimOverride* Override = FindOverride(Name, PortIndex, IsString);
    for (int i = 0; Override == NULL && i < SIM_MAX_OVERRIDES; i++)
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
// DT_STATUS_INVALID_PARAMETER and in that order. Doing the same here is what makes the
// emulator a test of the wire format rather than a stub that accepts anything.
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
// trusting the caller to have done so. Returns false for a name that is not a code.
//
static bool CodeFromName(const char* Field, int* Code)
{
    char Name[IOCONFIG_NAME_MAX_SIZE];

    memcpy(Name, Field, sizeof(Name));
    Name[sizeof(Name) - 1] = '\0';
    return DtIoConfig_GetCode(Name, Code) == DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSupported -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when the port has the capability named after Code, as CAP_<name>, overridden or
// not. The callers pass valid codes only; a name lookup that failed anyway would leave
// the bare prefix, which is not a capability.
//
static bool IsSupported(int PortIndex, int Code)
{
    uint64_t Value = 0;

    const SimOverride* Override;

    char CapName[4 + IOCONFIG_NAME_MAX_SIZE];
    memcpy(CapName, "CAP_", 4);
    DtIoConfig_GetName(Code, CapName + 4, IOCONFIG_NAME_MAX_SIZE);
    Override = FindOverride(CapName, PortIndex, false);
    if (Override != NULL)
        return Override->Status == 0 && Override->Present && Override->Value != 0;
    int Type;
    return SimDta2178_GetProperty(CapName, PortIndex, &Type, &Value) && Value != 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetDriverVersion(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize,
                            uint32_t* DrvStatus)
{
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlGetDriverVersionInput), Out,
                             OutSize, sizeof(DtIoctlGetDriverVersionOutput), DrvStatus);

    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    DtIoctlGetDriverVersionOutput* Version = (DtIoctlGetDriverVersionOutput*)Out;
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
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlGetDevInfoInput), Out, OutSize,
                             sizeof(DtIoctlGetDevInfoOutput), DrvStatus);

    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    DtIoctlGetDevInfoOutput* Info = (DtIoctlGetDevInfoOutput*)Out;
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

    if (Dev->IsDta2110)
    {
        Info->m_TypeNumber = SIM_DTA2110_TYPE_NUMBER;
        Info->m_Serial = SIM_DTA2110_SERIAL;
        Info->m_HardwareRevision = SIM_DTA2110_HARDWARE_REVISION;
        Info->m_FirmwareVersion = SIM_DTA2110_FIRMWARE_VERSION;
        Info->m_FirmwareVariant = SIM_DTA2110_FIRMWARE_VARIANT;
        Info->m_DeviceId = SIM_DTA2110_DEVICE_ID;
        Info->m_SubVendorId = SIM_DTA2110_SUBSYSTEM_VENDOR_ID;
        Info->m_SubSystemId = SIM_DTA2110_SUBSYSTEM_ID;
        Info->m_FwBuildDate.m_Year = SIM_DTA2110_FW_BUILD_YEAR;
        Info->m_FwBuildDate.m_Month = SIM_DTA2110_FW_BUILD_MONTH;
        Info->m_FwBuildDate.m_Day = SIM_DTA2110_FW_BUILD_DAY;
        Info->m_FwBuildDate.m_Hour = SIM_DTA2110_FW_BUILD_HOUR;
        Info->m_FwBuildDate.m_Minute = SIM_DTA2110_FW_BUILD_MINUTE;
        Info->m_DevSpecific.m_Pcie2.m_BusNumber = SIM_DTA2110_BUS_NUMBER;
        Info->m_DevSpecific.m_Pcie2.m_PcieNumLanes = SIM_DTA2110_PCIE_NUM_LANES;
        Info->m_DevSpecific.m_Pcie2.m_PcieMaxLanes = SIM_DTA2110_PCIE_MAX_LANES;
    }

    *OutSize = sizeof(DtIoctlGetDevInfoOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PropertyGetStr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reading a string: an override, or the card's string property with that name. The
// string is copied into the fixed field, which it may fill without a terminator.
//
static int PropertyGetStr(SimDevice* Dev, const void* In, size_t InSize, void* Out,
                          size_t* OutSize, uint32_t* DrvStatus)
{
    const char* Str = NULL;

    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlPropCmdGetStrInput), Out, OutSize,
                             sizeof(DtIoctlPropCmdGetStrOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    DtIoctlPropCmdGetStrInput Request;
    memcpy(&Request, In, sizeof(Request));
    Request.m_Name[sizeof(Request.m_Name) - 1] = '\0';

    DtIoctlPropCmdGetStrOutput* Answer = (DtIoctlPropCmdGetStrOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));

    const SimOverride* Override = FindOverride(Request.m_Name, Request.m_PortIndex, true);
    if (Override != NULL)
    {
        if (Override->Status != 0)
            return SimFail(Dev, (DtStatus)Override->Status, DrvStatus);
        if (!Override->Present)
            return SimFail(Dev, DT_STATUS_NOT_FOUND, DrvStatus);
        memcpy(Answer->m_Str, Override->Str, sizeof(Answer->m_Str));
    }
    else if (Dev->IsDta2110
                 ? SimDta2110_GetString(Request.m_Name, Request.m_PortIndex, &Str)
                 : SimDta2178_GetString(Request.m_Name, Request.m_PortIndex, &Str))
    {
        size_t Length = strlen(Str);
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
    uint64_t Value = 0;
    int Type = 0;

    if (Cmd == DT_PROP_CMD_GET_STR)
        return PropertyGetStr(Dev, In, InSize, Out, OutSize, DrvStatus);

    if (Cmd != DT_PROP_CMD_GET_VALUE)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlPropCmdGetValueInput), Out,
                             OutSize, sizeof(DtIoctlPropCmdGetValueOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    DtIoctlPropCmdGetValueInput Request;
    memcpy(&Request, In, sizeof(Request));
    Request.m_Name[sizeof(Request.m_Name) - 1] = '\0';

    const SimOverride* Override =
        FindOverride(Request.m_Name, Request.m_PortIndex, false);
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
    else if (Dev->IsDta2110 ? !SimDta2110_GetProperty(Request.m_Name, Request.m_PortIndex,
                                                      &Type, &Value)
                            : !SimDta2178_GetProperty(Request.m_Name, Request.m_PortIndex,
                                                      &Type, &Value))
    {
        return SimFail(Dev, DT_STATUS_NOT_FOUND, DrvStatus);
    }

    DtIoctlPropCmdGetValueOutput* Answer = (DtIoctlPropCmdGetValueOutput*)Out;
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
// checked in two steps, as the driver expects. A port index or group out of range is an
// invalid parameter, as it is in the driver.
//
static int GetIoConfig(SimDevice* Dev, const void* In, size_t InSize, void* Out,
                       size_t* OutSize, uint32_t* DrvStatus)
{
    int Outcome =
        CheckSizes(Dev, InSize, sizeof(DtIoctlIoConfigCmdGetIoConfigInput), Out, OutSize,
                   sizeof(DtIoctlIoConfigCmdGetIoConfigOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    const DtIoctlIoConfigCmdGetIoConfigInput* Request =
        (const DtIoctlIoConfigCmdGetIoConfigInput*)In;
    if (Request->m_IoConfigCount < 0)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    size_t Count = (size_t)Request->m_IoConfigCount;
    size_t InNeeded =
        sizeof(DtIoctlIoConfigCmdGetIoConfigInput) + Count * sizeof(DtIoctlIoConfigId);
    size_t OutNeeded = sizeof(DtIoctlIoConfigCmdGetIoConfigOutput) +
                       Count * sizeof(DtIoctlIoConfigValue);
    Outcome = CheckSizes(Dev, InSize, InNeeded, Out, OutSize, OutNeeded, DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    DtIoctlIoConfigCmdGetIoConfigOutput* Answer =
        (DtIoctlIoConfigCmdGetIoConfigOutput*)Out;
    for (size_t i = 0; i < Count; i++)
    {
        const DtIoctlIoConfigId* Id = &Request->m_IoCfgId[i];
        DtIoctlIoConfigValue* Value = &Answer->m_IoCfgValue[i];
        int Group;

        if (!CodeFromName(Id->m_Group, &Group) || Group < 0 || Id->m_PortIndex < 0 ||
            Id->m_PortIndex >= SIM_PORT_COUNT)
        {
            return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);
        }

        const SimConfig* Config = &g_Sim.Config[Id->m_PortIndex][Group];
        memset(Value, 0, sizeof(*Value));
        DtIoConfig_GetName(Config->Value, Value->m_Value, sizeof(Value->m_Value));
        DtIoConfig_GetName(Config->SubValue, Value->m_SubValue,
                           sizeof(Value->m_SubValue));
        for (int j = 0; j < DT_MAX_PARXTRA_COUNT; j++)
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
// as it is in the driver; a combination the port cannot take is a configuration error,
// the status of the driver's per-group validation. An unknown name, which the driver
// only asserts on, is treated as an invalid parameter.
//
static int SetIoConfig(SimDevice* Dev, const void* In, size_t InSize, uint32_t* DrvStatus)
{
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlIoConfigCmdSetIoConfigInput),
                             NULL, NULL, 0, DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    const DtIoctlIoConfigCmdSetIoConfigInput* Request =
        (const DtIoctlIoConfigCmdSetIoConfigInput*)In;
    if (Request->m_IoConfigCount < 0)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    size_t Count = (size_t)Request->m_IoConfigCount;
    Outcome = CheckSizes(Dev, InSize,
                         sizeof(DtIoctlIoConfigCmdSetIoConfigInput) +
                             Count * sizeof(DtIoctlIoConfig),
                         NULL, NULL, 0, DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    size_t i;
    for (i = 0; i < Count; i++)
    {
        const DtIoctlIoConfig* Pars = &Request->m_IoCfgPars[i];
        int Port = Pars->m_PortIndex;
        int Group;
        int Value;
        int SubValue;

        if (!CodeFromName(Pars->m_Group, &Group) ||
            !CodeFromName(Pars->m_Value, &Value) ||
            !CodeFromName(Pars->m_SubValue, &SubValue) || Group < 0 || Port < 0 ||
            Port >= SIM_PORT_COUNT)
        {
            return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);
        }

        // A boolean I/O capability must itself be supported; any other group needs its
        // value and sub-value to be.
        bool Supported;
        if (DtIoConfig_CheckConfig(Group, Value, SubValue) != DTAPI_OK)
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
        int Group = -1;

        CodeFromName(Pars->m_Group, &Group);
        SimConfig* Config = &g_Sim.Config[Pars->m_PortIndex][Group];
        CodeFromName(Pars->m_Value, &Config->Value);
        CodeFromName(Pars->m_SubValue, &Config->SubValue);
        for (int j = 0; j < DT_MAX_PARXTRA_COUNT; j++)
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
// card synchronised to the host would read, or the time a test set.
//
static int TodCmd(SimDevice* Dev, int Cmd, size_t InSize, void* Out, size_t* OutSize,
                  uint32_t* DrvStatus)
{
    if (Cmd != DT_TOD_CMD_GET_TIME)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlTodCmdGetTimeInput), Out, OutSize,
                             sizeof(DtIoctlTodCmdGetTimeOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    uint64_t NowNs = SimNw_Now();

    DtIoctlTodCmdGetTimeOutput* Answer = (DtIoctlTodCmdGetTimeOutput*)Out;
    memset(Answer, 0, sizeof(*Answer));
    Answer->m_Time.m_Seconds = (UInt32)(NowNs / 1000000000u);
    Answer->m_Time.m_Nanoseconds = (UInt32)(NowNs % 1000000000u);
    Answer->m_AdjustmentCount = 0;

    *OutSize = sizeof(DtIoctlTodCmdGetTimeOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiRxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The SDI receiver of the port at PortIndex. Only reading its status is modelled. Its
// sizes are checked first, then whether the function is enabled, which it is while the
// port is an input. In ASI mode only the carrier is reported.
//
static int SdiRxCmd(SimDevice* Dev, int PortIndex, int Cmd, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    const SimSdiSignal* Signal = &g_Sim.Signals[PortIndex];

    if (Cmd != DT_SDIRX_CMD_GET_SDI_STATUS2)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    int Outcome =
        CheckSizes(Dev, InSize, sizeof(DtIoctlSdiRxCmdGetSdiStatusInput), Out, OutSize,
                   sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    if (g_Sim.Config[PortIndex][DTAPI_IOCONFIG_IODIR].Value != DTAPI_IOCONFIG_INPUT)
        return SimFail(Dev, DT_STATUS_NOT_ENABLED, DrvStatus);

    DtIoctlSdiRxCmdGetSdiStatusOutput2* Answer = (DtIoctlSdiRxCmdGetSdiStatusOutput2*)Out;
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
// emulator keeps here, as the driver answers with its receiver's; everything else is the
// channel's own.
//
static int ChSdiRxCmd(SimDevice* Dev, int PortIndex, int Cmd, const void* In,
                      size_t InSize, void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    if (Cmd == DT_CHSDIRX_CMD_GET_SDI_STATUS)
    {
        size_t Size = OutSize != NULL ? *OutSize : 0;

        // The channel's status answer has the layout of GET_SDI_STATUS2's.
        _Static_assert(sizeof(DtIoctlChSdiRxCmdGetSdiStatusOutput) ==
                           sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2),
                       "The two status answers must have one layout");
        int Outcome = SdiRxCmd(Dev, PortIndex, DT_SDIRX_CMD_GET_SDI_STATUS2, InSize, Out,
                               &Size, DrvStatus);
        if (Outcome == OS_IOCTL_OK)
            *OutSize = Size;
        return Outcome;
    }

    uint32_t Status =
        SimChSdiRx_Cmd(Dev, PortIndex, Cmd, In, InSize, Out, OutSize, &Dev->SleepMs);
    if (Status != DT_STATUS_OK)
        return SimFail(Dev, Status, DrvStatus);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SdiTxCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A command for a transmit block of the port at PortIndex. The blocks are enabled while
// the port is an output with an SDI I/O standard, except for the PHY, which an ASI output
// uses too.
//
static int SdiTxCmd(SimDevice* Dev, int Uuid, int PortIndex, int FunctionCode, int Type,
                    const char* Role, int Cmd, const void* In, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    const SimConfig* Config = g_Sim.Config[PortIndex];
    bool Enabled = Config[DTAPI_IOCONFIG_IODIR].Value == DTAPI_IOCONFIG_OUTPUT &&
                   (Config[DTAPI_IOCONFIG_IOSTD].Value != DTAPI_IOCONFIG_ASI ||
                    FunctionCode == DT_FUNC_CODE_SDITXPHY_CMD);
    uint32_t Access = SimDtPcie_CheckAccess(Dev, (Uuid & DT_UUID_INDEX_MASK) - 1);
    uint32_t Status = SimSdiTx_Cmd(Dev, PortIndex, FunctionCode, Type, Role, Cmd, Access,
                                   Enabled, Config[DTAPI_IOCONFIG_IOSTD].SubValue, In,
                                   InSize, Out, OutSize, &Dev->SleepMs);

    if (Status != DT_STATUS_OK)
        return SimFail(Dev, Status, DrvStatus);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AsiCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A command for ASIRX or ASITXG of the port at PortIndex, enabled while the port is an
// ASI input or an ASI output.
//
static int AsiCmd(SimDevice* Dev, int Uuid, int PortIndex, int FunctionCode, int Type,
                  int Cmd, const void* In, size_t InSize, void* Out, size_t* OutSize,
                  uint32_t* DrvStatus)
{
    const SimConfig* Config = g_Sim.Config[PortIndex];
    int Direction = FunctionCode == DT_FUNC_CODE_ASIRX_CMD ? DTAPI_IOCONFIG_INPUT
                                                           : DTAPI_IOCONFIG_OUTPUT;
    bool Enabled = Config[DTAPI_IOCONFIG_IODIR].Value == Direction &&
                   Config[DTAPI_IOCONFIG_IOSTD].Value == DTAPI_IOCONFIG_ASI;
    uint32_t Access = SimDtPcie_CheckAccess(Dev, (Uuid & DT_UUID_INDEX_MASK) - 1);
    uint32_t Status = SimAsi_Cmd(Dev, PortIndex, FunctionCode, Type, Cmd, Access, Enabled,
                                 In, InSize, Out, OutSize);

    if (Status != DT_STATUS_OK)
        return SimFail(Dev, Status, DrvStatus);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExclAccessCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// EXCL_ACCESS_CMD for the object at ObjectIndex, with the driver's rules for a building
// block or a driver function, the owner being the handle.
//
static int ExclAccessCmd(SimDevice* Dev, int ObjectIndex, int Cmd, uint32_t* DrvStatus)
{
    if (ObjectIndex < 0 || ObjectIndex >= SIM_MAX_OBJECTS)
        return SimFail(Dev, DT_STATUS_NO_IOSTUB, DrvStatus);
    void** Owner = Dev->IsDta2110 ? &g_Sim.ExclOwners2110[ObjectIndex]
                                  : &g_Sim.ExclOwners[ObjectIndex];

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
        uint32_t Status = *Owner == NULL  ? DT_STATUS_EXCL_ACCESS_REQD
                          : *Owner == Dev ? DT_STATUS_OK
                                          : DT_STATUS_IN_USE;

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
    EnsureState();

    if (Index != g_Sim.Index && (g_Sim.Dta2110Index < 0 || Index != g_Sim.Dta2110Index))
        return NULL;

    SimDevice* Dev = (SimDevice*)DtAlloc_Malloc(sizeof(SimDevice));
    if (Dev == NULL)
        return NULL;

    Dev->LastError = 0;
    Dev->SleepMs = 0;
    Dev->IsDta2110 = Index != g_Sim.Index;
    g_Sim.OpenHandles++;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void SimClose(void* State)
{
    Lock();
    SimChSdiRx_CloseHandle(State);
    SimSdiTx_CloseHandle(State);
    SimNw_CloseHandle(State);
    for (int i = 0; i < SIM_MAX_OBJECTS; i++)
    {
        if (g_Sim.ExclOwners[i] == State)
            g_Sim.ExclOwners[i] = NULL;
        if (g_Sim.ExclOwners2110[i] == State)
            g_Sim.ExclOwners2110[i] = NULL;
    }
    g_Sim.OpenHandles--;
    Unlock();
    DtAlloc_Free(State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Dispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Dispatches on the function code rather than on the whole IOCTL number, exactly as the
// driver does. On Linux the number also encodes the argument size, so matching on it
// would accept only the one structure size this build happened to be compiled with.
//
// The header's UUID picks the target first, as in the driver: 0 is the device itself,
// which must be addressed with port index -1; a UUID flagged as a building block or
// driver function is looked up by its flags and index, whatever the port index and the
// bits above the flags say; anything else, and a UUID the card does not have, has no I/O
// stub. A target refuses a command it does not handle with DT_STATUS_NOT_SUPPORTED,
// before its sizes are looked at, as the driver does.
//
// The DTA-2110 has the network function, the activation object and exclusive access on
// its objects; its core answers what the DTA-2178's does, except the I/O configuration,
// which it refuses.
//
static int Dispatch(SimDevice* Dev, int FunctionCode, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    const DtIoctlInputDataHdr* Hdr = (const DtIoctlInputDataHdr*)In;
    int Cmd = Hdr->m_Cmd;
    const char* Role = NULL;

    if (Hdr->m_Uuid != DT_UUID_CORE)
    {
        int PortIndex;
        int Type;
        int Object = Hdr->m_Uuid & (DT_UUID_FLAG_MASK | DT_UUID_INDEX_MASK);
        bool Found = Dev->IsDta2110
                         ? SimDta2110_FindFunction(Object, &PortIndex, &Type, &Role)
                         : SimDta2178_FindFunction(Object, &PortIndex, &Type, &Role);
        if ((Hdr->m_Uuid & (DT_UUID_BC_FLAG | DT_UUID_DF_FLAG)) == 0 || !Found)
            return SimFail(Dev, DT_STATUS_NO_IOSTUB, DrvStatus);

        if (FunctionCode == DT_FUNC_CODE_EXCL_ACCESS_CMD)
        {
            if (InSize < sizeof(DtIoctlExclAccessCmdInput))
                return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);
            return ExclAccessCmd(Dev, (Hdr->m_Uuid & DT_UUID_INDEX_MASK) - 1, Cmd,
                                 DrvStatus);
        }
        if (Dev->IsDta2110)
        {
            if (Type == DT_BLOCK_TYPE_IPSECG && SimActivate_Takes(FunctionCode))
            {
                // The object answers nobody who does not hold it, as a card's does.
                const int ObjectIndex = (Hdr->m_Uuid & DT_UUID_INDEX_MASK) - 1;
                void* Owner = ObjectIndex >= 0 && ObjectIndex < SIM_MAX_OBJECTS
                                  ? g_Sim.ExclOwners2110[ObjectIndex]
                                  : NULL;
                if (Owner == NULL)
                    return SimFail(Dev, DT_STATUS_EXCL_ACCESS_REQD, DrvStatus);
                if (Owner != Dev)
                    return SimFail(Dev, DT_STATUS_IN_USE, DrvStatus);

                uint32_t Status = SimActivate_Cmd(Cmd, In, InSize, Out, OutSize);
                if (Status != DT_STATUS_OK)
                    return SimFail(Dev, Status, DrvStatus);
                return OS_IOCTL_OK;
            }
            if (Type != DT_FUNC_TYPE_NW || !SimNw_Takes(FunctionCode))
                return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
            uint32_t Status =
                SimNw_Cmd(Dev, Hdr->m_Uuid, FunctionCode, Cmd, In, InSize, Out, OutSize);
            if (Status != DT_STATUS_OK)
                return SimFail(Dev, Status, DrvStatus);
            return OS_IOCTL_OK;
        }
        if (SimClocks_Takes(FunctionCode))
        {
            uint32_t Status =
                SimClocks_Cmd(FunctionCode, (Hdr->m_Uuid & DT_UUID_DF_FLAG) != 0, Type,
                              Role, Cmd, In, InSize, Out, OutSize);
            if (Status != DT_STATUS_OK)
                return SimFail(Dev, Status, DrvStatus);
            return OS_IOCTL_OK;
        }
        if (SimAsi_Takes(FunctionCode))
            return AsiCmd(Dev, Hdr->m_Uuid, PortIndex, FunctionCode, Type, Cmd, In,
                          InSize, Out, OutSize, DrvStatus);
        if (SimSdiTx_Takes(FunctionCode))
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
        if (Dev->IsDta2110)
            return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
        return IoConfigCmd(Dev, Cmd, In, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_TOD_CMD:
        return TodCmd(Dev, Cmd, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_VPD_CMD:
    {
        uint32_t Status = SimVpd_Cmd(Cmd, In, InSize, Out, OutSize);
        if (Status != DT_STATUS_OK)
            return SimFail(Dev, Status, DrvStatus);
        return OS_IOCTL_OK;
    }
    default:
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimIoCtlLocked -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver refuses an input too short to hold the common header before it looks at
// anything else. OsDrv_IoCtl has already refused a request without input.
//
static int SimIoCtlLocked(SimDevice* Dev, int FunctionCode, const void* In, size_t InSize,
                          void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    const SimFault* Fault = FindFault(FunctionCode);

    g_Sim.LastFunctionCode = FunctionCode;
    g_Sim.LastInputSize = InSize;
    memcpy(g_Sim.LastInput, In,
           InSize < sizeof(g_Sim.LastInput) ? InSize : sizeof(g_Sim.LastInput));

    if (InSize < sizeof(DtIoctlInputDataHdr))
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    if (Fault != NULL && !Fault->Short)
        return SimFail(Dev, Fault->Status, DrvStatus);

    int Outcome = Dispatch(Dev, FunctionCode, In, InSize, Out, OutSize, DrvStatus);

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

    Lock();
    Dev->SleepMs = 0;
    int Outcome = SimIoCtlLocked(Dev, FunctionCode, In, InSize, Out, OutSize, DrvStatus);
    int SleepMs = Dev->SleepMs; // Taken under the lock: another thread is in here too
    Unlock();

    if (SleepMs > 0)
        OsTime_SleepMs(SleepMs);
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimMapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void* SimMapMemory(void* State, uint64_t Offset, size_t Size)
{
    Lock();
    void* Address = SimChSdiRx_Map(State, Offset, Size);
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Files +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_OpenFile -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
FILE* SimDtPcie_OpenFile(const char* Path, const char* Mode)
{
#ifdef _MSC_VER
    FILE* File = NULL;
    return fopen_s(&File, Path, Mode) == 0 ? File : NULL;
#else
    return fopen(Path, Mode);
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParsePort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The SDI port a value starts with, numbered from 1 and followed by a colon, as a port
// index; *Rest receives what follows the colon. -1 when there is no such port.
//
static int ParsePort(const char* Value, const char** Rest)
{
    int Port = 0;
    const char* At = Value;

    while (*At >= '0' && *At <= '9' && Port <= SIM_SDI_PORT_COUNT)
        Port = Port * 10 + (*At++ - '0');
    if (At == Value || *At != ':' || Port < 1 || Port > SIM_SDI_PORT_COUNT)
        return -1;
    *Rest = At + 1;
    return Port - 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- VidStdNamed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The video standard whose DTAPI_VIDSTD_ name without its prefix is the Length
// characters at Name, in any case; DTAPI_VIDSTD_UNKNOWN for none.
//
static int VidStdNamed(const char* Name, size_t Length)
{
    static const struct
    {
        const char* Name;
        int VidStd;
    } Names[] = {
#define X(Name, FpsNum, FpsDen, Lines, Scan, Hanc, LevelB, IoStd, OneLink)               \
    {#Name, DTAPI_VIDSTD_##Name},
#include "Tables/DtVidStdList.inc"
#undef X
    };

    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        size_t k = 0;
        while (k < Length && Names[i].Name[k] != '\0')
        {
            char A = Name[k], B = Names[i].Name[k];
            if (A >= 'a' && A <= 'z')
                A = (char)(A - 'a' + 'A');
            if (A != B)
                break;
            k++;
        }
        if (k == Length && Names[i].Name[k] == '\0')
            return Names[i].VidStd;
    }
    return DTAPI_VIDSTD_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SignalOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The locked signal of a video standard, without a VPID, as a receiver reports it: of
// 2160p over one link the geometry of one of its four links, as the card reports it.
// False for an unknown standard.
//
static bool SignalOf(int VidStd, SimSdiSignal* Signal)
{
    DtFrameProps Props;
    const DtVidStdEntry* Info = DtVidStd_Find(VidStd);

    if (Info == NULL || !DtFrameProps_Init(&Props, VidStd))
        return false;
    memset(Signal, 0, sizeof(*Signal));
    Signal->CarrierDetect = 1;
    Signal->SdiLock = 1;
    Signal->LineLock = 1;
    Signal->Valid = 1;
    Signal->NumSymsHanc = DtFrameProps_LineNumSymHancInclTiming(&Props);
    Signal->NumSymsVidVanc = Props.LineNumSymActive;
    Signal->NumLinesF1 = Props.Fields[0].EndLine - Props.Fields[0].StartLine + 1;
    if (Props.NumFields == 2)
        Signal->NumLinesF2 = Props.Fields[1].EndLine - Props.Fields[1].StartLine + 1;
    Signal->IsLevelB = Info->IsLevelB ? 1 : 0;
    Signal->FramePeriod = (int)(1e9 * Props.FpsDen / Props.FpsNum + 0.5);
    Signal->SdiRate = Info->IoStd == DTAPI_IOCONFIG_SDI      ? DT_DRV_SDIRATE_SD
                      : Info->IoStd == DTAPI_IOCONFIG_HDSDI  ? DT_DRV_SDIRATE_HD
                      : Info->IoStd == DTAPI_IOCONFIG_6GSDI  ? DT_DRV_SDIRATE_6G
                      : Info->IoStd == DTAPI_IOCONFIG_12GSDI ? DT_DRV_SDIRATE_12G
                                                             : DT_DRV_SDIRATE_3G;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplySdiSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SimDtPcie_SetSdiSource without taking the lock, for the reset, which may hold it.
//
static bool ApplySdiSource(const char* Source)
{
    const char* Rest = NULL;
    int PortIndex = ParsePort(Source, &Rest);
    if (PortIndex < 0)
        return false;
    const char* Colon = strchr(Rest, ':');
    if (Colon == NULL || Colon[1] == '\0')
        return false;

    SimSdiSignal Signal;
    int VidStd = VidStdNamed(Rest, (size_t)(Colon - Rest));
    if (!SignalOf(VidStd, &Signal) ||
        !SimChSdiRx_SetFileSource(PortIndex, VidStd, Colon + 1))
        return false;
    SimDtPcie_SetSdiSignal(PortIndex, &Signal);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplySdiSink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool ApplySdiSink(const char* Sink)
{
    const char* Rest = NULL;
    int PortIndex = ParsePort(Sink, &Rest);
    return PortIndex >= 0 && Rest[0] != '\0' && SimSdiTx_SetFileSink(PortIndex, Rest);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_Reset(void)
{
    int j;

    for (int Port = 0; Port < SIM_PORT_COUNT; Port++)
    {
        for (int Group = 0; Group < SIM_IOCONFIG_COUNT; Group++)
        {
            SimConfig* Config = &g_Sim.Config[Port][Group];

            SimDta2178_DefaultConfig(Port, Group, &Config->Value, &Config->SubValue);
            for (j = 0; j < DT_MAX_PARXTRA_COUNT; j++)
                Config->ParXtra[j] = -1;
        }
    }

    SimChSdiRx_Reset();
    SimSdiTx_Reset();
    SimAsi_Reset();
    SimNw_Reset();
    SimNet_Reset();
    SimVpd_Reset();
    SimActivate_Reset();
    SimClocks_Reset();

    for (j = 0; j < SIM_MAX_FAULTS; j++)
        g_Sim.Faults[j].FunctionCode = -1;

    g_Sim.LastFunctionCode = -1;
    g_Sim.LastInputSize = 0;
    memset(g_Sim.ExclOwners, 0, sizeof(g_Sim.ExclOwners));
    memset(g_Sim.ExclOwners2110, 0, sizeof(g_Sim.ExclOwners2110));

    for (j = 0; j < SIM_MAX_OVERRIDES; j++)
        g_Sim.Overrides[j].Active = false;

    // Initialised before the signals are cleared, which checks it.
    g_Sim.Initialised = true;
    for (j = 0; j < SIM_SDI_PORT_COUNT; j++)
        SimDtPcie_SetSdiSignal(j, NULL);

    g_Sim.Index = SIM_DEVICE_INDEX;

    // A source and a sink through files, as SimDtPcie.h describes.
    const char* Source = getenv("CDTAPI_SIM_SDI_SOURCE");
    if (Source != NULL && Source[0] != '\0')
    {
        if (ApplySdiSource(Source))
            SimDtPcie_SetRxRealTime(true);
        else
            fprintf(stderr, "CDTAPI_SIM_SDI_SOURCE: cannot use \"%s\"\n", Source);
    }
    const char* Sink = getenv("CDTAPI_SIM_SDI_SINK");
    if (Sink != NULL && Sink[0] != '\0' && !ApplySdiSink(Sink))
        fprintf(stderr, "CDTAPI_SIM_SDI_SINK: cannot use \"%s\"\n", Sink);

    // A test adds the DTA-2110 through SimDtPcie_SetDta2110Index. A program that calls
    // no test control, an example, asks for it through the environment, with the device
    // index to put it at; the DTA-2178 has index SIM_DEVICE_INDEX.
    const char* Dta2110 = getenv("CDTAPI_SIM_DTA2110");
    g_Sim.Dta2110Index = Dta2110 != NULL && Dta2110[0] != '\0' ? atoi(Dta2110) : -1;
    if (g_Sim.Dta2110Index >= 0)
    {
        static const uint8_t Mac[6] = SIM_DTA2110_MAC_ADDRESS;

        SimNet_SetDta2110Interface(true, Mac);
    }
    g_Sim.FirmwareStatus = DT_FWSTATUS_UPTODATE;
    g_Sim.DriverVersion.m_Major = SIM_DRIVER_MAJOR;
    g_Sim.DriverVersion.m_Minor = SIM_DRIVER_MINOR;
    g_Sim.DriverVersion.m_Micro = SIM_DRIVER_MICRO;
    g_Sim.DriverVersion.m_Build = SIM_DRIVER_BUILD;

    g_Sim.Initialised = true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_FailWithStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_FailWithStatus(int FunctionCode, uint32_t Status)
{
    AddFault(FunctionCode, false, Status);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_AnswerShort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_AnswerShort(int FunctionCode)
{
    AddFault(FunctionCode, true, DT_STATUS_OK);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetFirmwareStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetFirmwareStatus(int Status)
{
    EnsureState();
    g_Sim.FirmwareStatus = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetDriverVersion(int Major, int Minor, int Micro, int Build)
{
    EnsureState();
    g_Sim.DriverVersion.m_Major = Major;
    g_Sim.DriverVersion.m_Minor = Minor;
    g_Sim.DriverVersion.m_Micro = Micro;
    g_Sim.DriverVersion.m_Build = Build;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_DelaySdiSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_DelaySdiSignal(int PortIndex, int Reads)
{
    EnsureState();
    if (PortIndex >= 0 && PortIndex < SIM_SDI_PORT_COUNT)
        g_Sim.SignalDelays[PortIndex] = Reads;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_OverrideProperty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_OverrideProperty(const char* Name, int PortIndex, bool Present,
                                uint64_t Value)
{
    SimOverride* Override = AddOverride(Name, PortIndex, false);

    if (Override == NULL)
        return;
    Override->Present = Present;
    Override->Value = Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_OverrideString -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver's string field holds PROPERTY_STR_MAX_SIZE characters and need not be
// terminated when full, so the override keeps up to that many.
//
void SimDtPcie_OverrideString(const char* Name, int PortIndex, bool Present,
                              const char* Value)
{
    SimOverride* Override = AddOverride(Name, PortIndex, true);

    if (Override == NULL)
        return;
    Override->Present = Present;
    if (!Present || Value == NULL)
        return;

    size_t Length = strlen(Value);
    if (Length > sizeof(Override->Str))
        Length = sizeof(Override->Str);
    memcpy(Override->Str, Value, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_FailProperty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_FailProperty(const char* Name, int PortIndex, bool IsString,
                            uint32_t Status)
{
    SimOverride* Override = AddOverride(Name, PortIndex, IsString);

    if (Override != NULL)
        Override->Status = Status;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetSdiSignal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetSdiSignal(int PortIndex, const SimSdiSignal* Signal)
{
    EnsureState();
    if (PortIndex < 0 || PortIndex >= SIM_SDI_PORT_COUNT)
        return;

    SimSdiSignal* Port = &g_Sim.Signals[PortIndex];
    g_Sim.SignalDelays[PortIndex] = 0;
    if (Signal != NULL)
    {
        *Port = *Signal;
        return;
    }
    memset(Port, 0, sizeof(*Port));
    Port->SdiRate = DT_DRV_SDIRATE_UNKNOWN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetSdiSource -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimDtPcie_SetSdiSource(const char* Source)
{
    EnsureState();
    Lock();
    bool Done = Source != NULL && ApplySdiSource(Source);
    Unlock();
    return Done;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetSdiSink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimDtPcie_SetSdiSink(const char* Sink)
{
    EnsureState();
    Lock();
    bool Done = Sink != NULL && ApplySdiSink(Sink);
    Unlock();
    return Done;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetIndex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_SetIndex(int Index)
{
    EnsureState();
    g_Sim.Index = Index;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetDta2110Index -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetDta2110Index(int Index)
{
    static const uint8_t Mac[6] = SIM_DTA2110_MAC_ADDRESS;

    EnsureState();
    g_Sim.Dta2110Index = Index;
    SimNet_SetDta2110Interface(Index >= 0, Mac);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_OpenHandles -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int SimDtPcie_OpenHandles(void)
{
    return g_Sim.OpenHandles;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_LastInput -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t SimDtPcie_LastInput(int* FunctionCode, void* Buf, size_t Size)
{
    EnsureState();
    *FunctionCode = g_Sim.LastFunctionCode;
    size_t Kept = g_Sim.LastInputSize < sizeof(g_Sim.LastInput) ? g_Sim.LastInputSize
                                                                : sizeof(g_Sim.LastInput);
    memcpy(Buf, g_Sim.LastInput, Size < Kept ? Size : Kept);
    return g_Sim.LastInputSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_CheckAccess -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint32_t SimDtPcie_CheckAccess(void* Handle, int ObjectIndex)
{
    if (ObjectIndex < 0 || ObjectIndex >= SIM_MAX_OBJECTS)
        return DT_STATUS_EXCL_ACCESS_REQD;
    void* Owner = g_Sim.ExclOwners[ObjectIndex];
    if (Owner == NULL)
        return DT_STATUS_EXCL_ACCESS_REQD;
    return Owner == Handle ? DT_STATUS_OK : DT_STATUS_IN_USE;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Selection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSim_Backend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const OsBackend* OsSim_Backend(void)
{
    static const OsBackend Backend = {SimOpen,      SimClose,     SimIoCtl,
                                      SimLastError, SimMapMemory, SimUnmapMemory};
    return &Backend;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSim_IsRequested -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool OsSim_IsRequested(void)
{
    static int Cached = -1;

    // Any value other than empty or "0" turns the emulator on, so that both
    // CDTAPI_SIM=1 and CDTAPI_SIM=yes work and CDTAPI_SIM=0 does not.
    if (Cached < 0)
    {
        const char* Value = getenv("CDTAPI_SIM");
        Cached = (Value != NULL && Value[0] != '\0' && strcmp(Value, "0") != 0) ? 1 : 0;
    }

    return Cached == 1;
}

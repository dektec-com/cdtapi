// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated DtPcie device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // DTAPI_IOCONFIG_ codes.
#include "Core/DtlAlloc.h"          // Allocation seam.
#include "DtlDrvAbi.h"              // The driver ABI the emulator answers in.
#include "DtlIoConfig.h"            // I/O configuration names, codes and relation.
#include "OAL/OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.
#include "OAL/OsBackend.h"          // Backend interface being implemented.
#include "SimDtPcie.h"              // What the emulated card reports.
#include "SimDta2178.h"             // What the emulated card is.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimDevice
{
    unsigned long LastError;
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

// The card's state, shared by every handle. See the test controls in SimDtPcie.h.
static struct
{
    bool Initialised;
    SimConfig Config[SIM_PORT_COUNT][SIM_IOCONFIG_COUNT];
    SimFault Faults[SIM_MAX_FAULTS];
    int LastFunctionCode;
    size_t LastInputSize;
    uint8_t LastInput[SIM_MAX_RECORDED_INPUT];
} g_Sim;

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
    return DtlIoConfigGetCode(Name, Code) == DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSupported -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when the port has the capability named after Code, as CAP_<name>. The callers
// pass valid codes only; a name lookup that failed anyway would leave the bare prefix,
// which is no capability.
//
static bool IsSupported(int PortIndex, int Code)
{
    char CapName[4 + IOCONFIG_NAME_MAX_SIZE];
    uint64_t Value = 0;
    int Type;

    memcpy(CapName, "CAP_", 4);
    DtlIoConfigGetName(Code, CapName + 4, IOCONFIG_NAME_MAX_SIZE);
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
    Version->m_Major = SIM_DRIVER_MAJOR;
    Version->m_Minor = SIM_DRIVER_MINOR;
    Version->m_Micro = SIM_DRIVER_MICRO;
    Version->m_Build = SIM_DRIVER_BUILD;

    *OutSize = sizeof(DtIoctlGetDriverVersionOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDevInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers GET_DEV_INFO and GET_DEV_INFO2 alike. The two differ only in how the driver
// fills the PCIe-specific tail, which the emulator leaves zeroed.
//
static int GetDevInfo(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize,
                      uint32_t* DrvStatus)
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
    Info->m_FirmwareStatus = DT_FWSTATUS_UPTODATE;
    Info->m_VendorId = SIM_VENDOR_ID;
    Info->m_DeviceId = SIM_DEVICE_ID;
    Info->m_SubVendorId = SIM_VENDOR_ID;
    Info->m_SubSystemId = SIM_DEVICE_ID;

    *OutSize = sizeof(DtIoctlGetDevInfoOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PropertyCmd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only reading a value is modelled; strings and tables are refused as unknown commands.
//
static int PropertyCmd(SimDevice* Dev, int Cmd, const void* In, size_t InSize, void* Out,
                       size_t* OutSize, uint32_t* DrvStatus)
{
    DtIoctlPropCmdGetValueInput Request;
    DtIoctlPropCmdGetValueOutput* Answer;
    uint64_t Value = 0;
    int Type = 0;
    int Outcome;

    if (Cmd != DT_PROP_CMD_GET_VALUE)
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);

    Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlPropCmdGetValueInput), Out, OutSize,
                         sizeof(DtIoctlPropCmdGetValueOutput), DrvStatus);
    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    memcpy(&Request, In, sizeof(Request));
    Request.m_Name[sizeof(Request.m_Name) - 1] = '\0';

    if (!SimDta2178GetProperty(Request.m_Name, Request.m_PortIndex, &Type, &Value))
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
        DtlIoConfigGetName(Config->Value, Value->m_Value, sizeof(Value->m_Value));
        DtlIoConfigGetName(Config->SubValue, Value->m_SubValue,
                           sizeof(Value->m_SubValue));
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
        if (DtlIoConfigIsValid(Group, Value, SubValue) != DTAPI_OK)
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* SimOpen(int Index)
{
    SimDevice* Dev;

    if (Index != SIM_DEVICE_INDEX)
        return NULL;

    EnsureState();

    Dev = (SimDevice*)DtlMalloc(sizeof(SimDevice));
    if (Dev == NULL)
        return NULL;

    Dev->LastError = 0;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void SimClose(void* State)
{
    DtlFree(State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Dispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Dispatches on the function code rather than on the whole IOCTL number, exactly as the
// driver does. On Linux the number also encodes the argument size, so matching on it
// would accept only the one structure size this build happened to be compiled with.
//
// A command the emulator does not model is refused with DT_STATUS_NOT_SUPPORTED before
// its sizes are looked at, as the driver refuses a command it does not know.
//
static int Dispatch(SimDevice* Dev, int FunctionCode, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    int Cmd = ((const DtIoctlInputDataHdr*)In)->m_Cmd;

    switch (FunctionCode)
    {
    case DT_FUNC_CODE_GET_DRIVER_VERSION:
        return GetDriverVersion(Dev, InSize, Out, OutSize, DrvStatus);
    case DT_FUNC_CODE_GET_DEV_INFO:
    case DT_FUNC_CODE_GET_DEV_INFO2:
        return GetDevInfo(Dev, InSize, Out, OutSize, DrvStatus);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimIoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver refuses an input too short to hold the common header before it looks at
// anything else (DtCore_Ioctl). OsDrvIoCtl has already refused a request without input.
//
static int SimIoCtl(void* State, unsigned long Code, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    SimDevice* Dev = (SimDevice*)State;
    int FunctionCode = (int)DT_IOCTL_TO_FUNCTION(Code);
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static unsigned long SimLastError(const void* State)
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

    for (j = 0; j < SIM_MAX_FAULTS; j++)
        g_Sim.Faults[j].FunctionCode = -1;

    g_Sim.LastFunctionCode = -1;
    g_Sim.LastInputSize = 0;

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Selection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSimBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const OsBackend* OsSimBackend(void)
{
    static const OsBackend Backend = {SimOpen, SimClose, SimIoCtl, SimLastError};
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

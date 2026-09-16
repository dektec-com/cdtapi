// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSimCommands.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Property, I/O configuration and time-of-day commands against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state and checks that the handle really is emulated.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>
#include <time.h>

// CDtapiLite includes
#include "Core/DtlAlloc.h"          // Allocation failure injection.
#include "DtlDrv.h"                 // Driver commands under test.
#include "DtlDrvAbi.h"              // Raw structures and statuses.
#include "DtlTest.h"                // Test framework.
#include "OAL/OsAbstractionLayer.h" // Raw IOCTLs.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DTL_TEST_IOCTL(Code) ((unsigned long)(Code))

// Opens the emulated device in its power-on state, or records why not. Returns NULL on
// failure, in which case the calling case must return.
static OsDrv* OpenSim(int* DtlFailures)
{
    OsDrv* Drv;

    SimDtPcieReset();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtlFailures)++;
        OsDrvClose(Drv);
        return NULL;
    }
    return Drv;
}

// A configuration with no extra parameters.
static DtlIoConfig Config(int Port, int Group, int Value, int SubValue)
{
    DtlIoConfig Result;

    Result.Port = Port;
    Result.Group = Group;
    Result.Value = Value;
    Result.SubValue = SubValue;
    Result.ParXtra[0] = -1;
    Result.ParXtra[1] = -1;
    return Result;
}

// One I/O configuration read straight from the emulated driver, bypassing the
// conversions of the driver layer.
typedef struct RawGetIn
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_IoConfigCount;
    DtIoctlIoConfigId m_IoCfgId;
} RawGetIn;

typedef struct RawGetOut
{
    Int m_IoConfigCount;
    DtIoctlIoConfigValue m_IoCfgValue;
} RawGetOut;

static int RawGet(OsDrv* Drv, int PortIndex, const char* Group, RawGetOut* Out)
{
    RawGetIn In;
    size_t OutSize = sizeof(*Out);

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_PortIndex = -1;
    In.m_CmdHdr.m_Cmd = DT_IOCONFIG_CMD_GET_IOCONFIG;
    In.m_CmdHdr.m_CmdEx = DT_IOCTL_CMD_NOP;
    In.m_IoConfigCount = 1;
    In.m_IoCfgId.m_PortIndex = PortIndex;
    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", Group);
    memset(Out, 0, sizeof(*Out));
    return OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In), Out,
                      &OutSize, NULL);
}

// A raw set request, with room for one configuration.
typedef struct RawSetIn
{
    DtIoctlInputDataHdr m_CmdHdr;
    Int m_IoConfigCount;
    DtIoctlIoConfig m_IoCfgPars;
} RawSetIn;

// The configuration of the most recent set request, as the emulated driver received it.
static bool LastSetIoConfig(DtIoctlIoConfig* Pars)
{
    RawSetIn In;
    int FunctionCode;
    size_t Size = SimDtPcieLastInput(&FunctionCode, &In, sizeof(In));

    if (FunctionCode != DT_FUNC_CODE_IOCONFIG_CMD || Size != sizeof(In) ||
        In.m_CmdHdr.m_Cmd != DT_IOCONFIG_CMD_SET_IOCONFIG || In.m_IoConfigCount != 1)
    {
        return false;
    }
    *Pars = In.m_IoCfgPars;
    return true;
}

// Checks the header every device-level command starts with.
static void CheckDeviceHeader(int* DtlFailures, const DtIoctlInputDataHdr* Hdr, int Cmd)
{
    DTL_ASSERT_EQ(Hdr->m_Uuid, 0);
    DTL_ASSERT_EQ(Hdr->m_PortIndex, -1);
    DTL_ASSERT_EQ(Hdr->m_Cmd, Cmd);
    DTL_ASSERT_EQ(Hdr->m_CmdEx, DT_IOCTL_CMD_NOP);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(PortCountsAreReported)
{
    int Value = -1;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_OK(DtlDrvGetPropertyInt(Drv, "PORT_COUNT", DTL_PROPERTY_DEVICE, &Value));
    DTL_ASSERT_EQ(Value, SIM_PORT_COUNT);
    DTL_ASSERT_OK(
        DtlDrvGetPropertyInt(Drv, "MAIN_PORT_COUNT", DTL_PROPERTY_DEVICE, &Value));
    DTL_ASSERT_EQ(Value, SIM_PORT_COUNT);

    OsDrvClose(Drv);
}

// A capability a port lacks is found, with the value false, as the driver answers.
DTL_TEST(CapabilitiesArePerPort)
{
    bool Value = false;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_3GSDI", 0, &Value));
    DTL_ASSERT(Value);
    DTL_ASSERT_OK(
        DtlDrvGetPropertyBool(Drv, "CAP_OUTPUT", SIM_SDI_PORT_COUNT - 1, &Value));
    DTL_ASSERT(Value);
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_12GSDI", 0, &Value));
    DTL_ASSERT(!Value);
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_GENREF", SIM_SDI_PORT_COUNT, &Value));
    DTL_ASSERT(Value);
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_INPUT", SIM_SDI_PORT_COUNT, &Value));
    DTL_ASSERT(!Value);
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_VIRTUAL", SIM_PORT_COUNT - 1, &Value));
    DTL_ASSERT(Value);
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_VIRTUAL", SIM_SDI_PORT_COUNT, &Value));
    DTL_ASSERT(!Value);

    // Beyond the last port, and for the device itself, there are no capabilities.
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_SDI", SIM_PORT_COUNT, &Value));
    DTL_ASSERT(!Value);
    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_SDI", DTL_PROPERTY_DEVICE, &Value));
    DTL_ASSERT(!Value);

    OsDrvClose(Drv);
}

DTL_TEST(UnknownPropertyIsNotFound)
{
    int Value = 7;
    bool Flag = true;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(Drv, "NO_SUCH", DTL_PROPERTY_DEVICE, &Value),
                  DTAPI_E_NOT_FOUND);
    DTL_ASSERT_EQ(Value, 0);
    DTL_ASSERT_EQ(DtlDrvGetPropertyBool(Drv, "PORT_COUNT", 0, &Flag), DTAPI_E_NOT_FOUND);
    DTL_ASSERT(!Flag);

    OsDrvClose(Drv);
}

// The driver's name field holds 50 bytes, terminator included.
DTL_TEST(PropertyNameMustFit)
{
    char Name[64];
    int Value;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(Name, 'A', sizeof(Name));
    Name[PROPERTY_NAME_MAX_SIZE] = '\0';
    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(Drv, Name, DTL_PROPERTY_DEVICE, &Value),
                  DTAPI_E_BUF_TOO_SMALL);

    Name[PROPERTY_NAME_MAX_SIZE - 1] = '\0';
    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(Drv, Name, DTL_PROPERTY_DEVICE, &Value),
                  DTAPI_E_NOT_FOUND);

    OsDrvClose(Drv);
}

DTL_TEST(PropertyNullArgumentsAreRefused)
{
    int Value = 7;
    bool Flag = true;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(NULL, "PORT_COUNT", -1, &Value),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(Value, 0);
    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(Drv, NULL, -1, &Value), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(Drv, "PORT_COUNT", -1, NULL), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetPropertyBool(NULL, "CAP_SDI", 0, &Flag), DTAPI_E_INVALID_ARG);
    DTL_ASSERT(!Flag);
    DTL_ASSERT_EQ(DtlDrvGetPropertyBool(Drv, NULL, 0, &Flag), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetPropertyBool(Drv, "CAP_SDI", 0, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver failures +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A refused command reaches the caller as the result its status stands for, and a short
// answer as a driver failure, through every typed command.
//

DTL_TEST(DriverStatusBecomesTheResult)
{
    DtlDriverVersion Version;
    DtlIoConfig Cfg =
        Config(1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    uint32_t Seconds, Nanoseconds;
    int Value;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_PROPERTY_CMD, DT_STATUS_IN_USE);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_EXCL_ACCESS_REQD);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_TOD_CMD, DT_STATUS_BUSY);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DRIVER_VERSION, DT_STATUS_VERSION_MISMATCH);

    DTL_ASSERT_EQ(DtlDrvGetPropertyInt(Drv, "PORT_COUNT", -1, &Value), DTAPI_E_IN_USE);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_EXCL_ACCESS_REQD);
    DTL_ASSERT_EQ(DtlDrvGetIoConfig(Drv, &Cfg), DTAPI_E_EXCL_ACCESS_REQD);
    DTL_ASSERT_EQ(DtlDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds), DTAPI_E_BUSY);
    DTL_ASSERT_EQ(DtlDrvGetDriverVersion(Drv, &Version), DTAPI_E_DRIVER_INCOMP);

    OsDrvClose(Drv);
}

DTL_TEST(ShortAnswerIsDriverFailure)
{
    DtlIoConfig Cfg = Config(1, DTAPI_IOCONFIG_IODIR, -1, -1);
    uint32_t Seconds, Nanoseconds;
    bool Flag;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    SimDtPcieAnswerShort(DT_FUNC_CODE_PROPERTY_CMD);
    SimDtPcieAnswerShort(DT_FUNC_CODE_IOCONFIG_CMD);
    SimDtPcieAnswerShort(DT_FUNC_CODE_TOD_CMD);

    DTL_ASSERT_EQ(DtlDrvGetPropertyBool(Drv, "CAP_SDI", 0, &Flag), DTAPI_E_DEV_DRIVER);
    DTL_ASSERT_EQ(DtlDrvGetIoConfig(Drv, &Cfg), DTAPI_E_DEV_DRIVER);
    DTL_ASSERT_EQ(DtlDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds), DTAPI_E_DEV_DRIVER);

    OsDrvClose(Drv);
}

// A driver without GET_DEV_INFO2 is asked the original command; only when both fail does
// the failure reach the caller.
DTL_TEST(DeviceInfoFallsBack)
{
    DtlDeviceInfo Info;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO2, DT_STATUS_NOT_SUPPORTED);
    DTL_ASSERT_OK(DtlDrvGetDeviceInfo(Drv, &Info));
    DTL_ASSERT_EQ(Info.Serial, (int64_t)SIM_SERIAL);

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO, DT_STATUS_TIMEOUT);
    DTL_ASSERT_EQ(DtlDrvGetDeviceInfo(Drv, &Info), DTAPI_E_TIMEOUT);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= I/O configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(DefaultDirectionsAlternate)
{
    DtlIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Cfg));
    DTL_ASSERT_EQ(Cfg.Value, DTAPI_IOCONFIG_INPUT);
    DTL_ASSERT_EQ(Cfg.SubValue, DTAPI_IOCONFIG_INPUT);
    DTL_ASSERT_EQ(Cfg.ParXtra[0], -1);
    DTL_ASSERT_EQ(Cfg.ParXtra[1], -1);

    Cfg = Config(2, DTAPI_IOCONFIG_IODIR, 0, 0);
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Cfg));
    DTL_ASSERT_EQ(Cfg.Value, DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_EQ(Cfg.SubValue, DTAPI_IOCONFIG_OUTPUT);

    // A group the port has no setting for reads back as -1, the empty name.
    Cfg = Config(SIM_SDI_PORT_COUNT + 1, DTAPI_IOCONFIG_IODIR, 0, 0);
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Cfg));
    DTL_ASSERT_EQ(Cfg.Value, -1);
    DTL_ASSERT_EQ(Cfg.SubValue, -1);

    OsDrvClose(Drv);
}

DTL_TEST(ConfigurationRoundTrips)
{
    DtlIoConfig Set =
        Config(1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    DtlIoConfig Get = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Set));
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Get));
    DTL_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_EQ(Get.SubValue, DTAPI_IOCONFIG_OUTPUT);

    Set = Config(1, DTAPI_IOCONFIG_GENLOCKED, DTAPI_IOCONFIG_TRUE, -1);
    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Set));
    Get = Config(1, DTAPI_IOCONFIG_GENLOCKED, 0, 0);
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Get));
    DTL_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_TRUE);
    DTL_ASSERT_EQ(Get.SubValue, -1);

    // The state belongs to the card, not to the handle.
    OsDrvClose(Drv);
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    Get = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Get));
    DTL_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_OUTPUT);

    SimDtPcieReset();
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Get));
    DTL_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_INPUT);

    OsDrvClose(Drv);
}

// DTAPI numbers the buddy port of a double-buffered output from 1, the driver from 0.
DTL_TEST(BuddyPortIsConvertedBothWays)
{
    DtlIoConfig Set =
        Config(2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF);
    DtlIoConfig Get = Config(2, DTAPI_IOCONFIG_IODIR, 0, 0);
    RawGetOut Raw;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    Set.ParXtra[0] = 4;
    Set.ParXtra[1] = 77;
    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Set));

    DTL_ASSERT_EQ(RawGet(Drv, 1, "IODIR", &Raw), OS_IOCTL_OK);
    DTL_ASSERT_STR(Raw.m_IoCfgValue.m_Value, "OUTPUT");
    DTL_ASSERT_STR(Raw.m_IoCfgValue.m_SubValue, "DBLBUF");
    DTL_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[0], 3);
    DTL_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[1], 77);
    DTL_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[2], -1);
    DTL_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[3], -1);

    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Get));
    DTL_ASSERT_EQ(Get.SubValue, DTAPI_IOCONFIG_DBLBUF);
    DTL_ASSERT_EQ(Get.ParXtra[0], 4);
    DTL_ASSERT_EQ(Get.ParXtra[1], 77);

    OsDrvClose(Drv);
}

// Only the I/O direction values that name a port have ParXtra[0] converted.
DTL_TEST(OtherParametersAreNotConverted)
{
    DtlIoConfig Set =
        Config(1, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_3GSDI, DTAPI_IOCONFIG_1080P50);
    DtlIoConfig Get = Config(1, DTAPI_IOCONFIG_IOSTD, 0, 0);
    RawGetOut Raw;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    Set.ParXtra[0] = 4;
    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Set));
    DTL_ASSERT_EQ(RawGet(Drv, 0, "IOSTD", &Raw), OS_IOCTL_OK);
    DTL_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[0], 4);
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Get));
    DTL_ASSERT_EQ(Get.ParXtra[0], 4);
    DTL_ASSERT_EQ(Get.SubValue, DTAPI_IOCONFIG_1080P50);

    OsDrvClose(Drv);
}

// Every I/O direction whose ParXtra[0] names a port, as the driver receives it.
DTL_TEST(DirectionsThatNamePortsAreConverted)
{
    static const struct
    {
        int Value;
        int SubValue;
        int64_t Expected;
    } Cases[] = {
        {DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF, 4},
        {DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_LOOPS2L3, 4},
        {DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_LOOPS2TS, 4},
        {DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_LOOPTHR, 4},
        {DTAPI_IOCONFIG_INTOUTPUT, DTAPI_IOCONFIG_DBLBUF, 4},
        {DTAPI_IOCONFIG_INTOUTPUT, DTAPI_IOCONFIG_LOOPTHR, 4},
        {DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_SHAREDANT, 4},
        {DTAPI_IOCONFIG_MONITOR, DTAPI_IOCONFIG_MONITOR, 4},
        {DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT, 5},
        {DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT, 5},
        {DTAPI_IOCONFIG_INTINPUT, DTAPI_IOCONFIG_INTINPUT, 5},
        {DTAPI_IOCONFIG_DISABLED, -1, 5},
    };
    DtIoctlIoConfig Sent;
    size_t i;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        DtlIoConfig Cfg =
            Config(3, DTAPI_IOCONFIG_IODIR, Cases[i].Value, Cases[i].SubValue);

        Cfg.ParXtra[0] = 5;
        Cfg.ParXtra[1] = 6;
        DtlDrvSetIoConfig(Drv, &Cfg);

        DTL_ASSERT(LastSetIoConfig(&Sent));
        if (Sent.m_ParXtra[0] != Cases[i].Expected)
            DTL_FAIL("case %d: ParXtra[0] sent as %lld", (int)i,
                     (long long)Sent.m_ParXtra[0]);
        DTL_ASSERT_EQ(Sent.m_ParXtra[1], 6);
        DTL_ASSERT_EQ(Sent.m_ParXtra[2], -1);
        DTL_ASSERT_EQ(Sent.m_ParXtra[3], -1);
        DTL_ASSERT_EQ(Sent.m_PortIndex, 2);
        DTL_ASSERT_STR(Sent.m_Group, "IODIR");
        DTL_ASSERT_EQ(Sent.m_SkipExclAccessCheck, 0);
    }

    // The same value outside the direction group names no port.
    {
        DtlIoConfig Cfg = Config(3, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_MONITOR,
                                 DTAPI_IOCONFIG_MONITOR);

        Cfg.ParXtra[0] = 5;
        DtlDrvSetIoConfig(Drv, &Cfg);
        DTL_ASSERT(LastSetIoConfig(&Sent));
        DTL_ASSERT_EQ(Sent.m_ParXtra[0], 5);
        DTL_ASSERT_STR(Sent.m_Value, "MONITOR");
        DTL_ASSERT_STR(Sent.m_SubValue, "MONITOR");
    }

    OsDrvClose(Drv);
}

// DTAPI skips the driver's exclusive-access check for the port its proxy addresses,
// which for a device-level request is port index -1.
DTL_TEST(ExclusiveAccessCheckIsSkippedOnlyForTheDevice)
{
    DtlIoConfig Cfg =
        Config(0, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT);
    DtIoctlIoConfig Sent;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DtlDrvSetIoConfig(Drv, &Cfg);
    DTL_ASSERT(LastSetIoConfig(&Sent));
    DTL_ASSERT_EQ(Sent.m_PortIndex, -1);
    DTL_ASSERT_EQ(Sent.m_SkipExclAccessCheck, 1);

    Cfg.Port = 1;
    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Cfg));
    DTL_ASSERT(LastSetIoConfig(&Sent));
    DTL_ASSERT_EQ(Sent.m_SkipExclAccessCheck, 0);

    OsDrvClose(Drv);
}

DTL_TEST(UnsupportedConfigurationIsConfigError)
{
    DtlIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    // The genlock reference port has no direction, and an SDI port is no reference.
    Cfg = Config(SIM_SDI_PORT_COUNT + 1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                 DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);
    Cfg = Config(1, DTAPI_IOCONFIG_GENREF, DTAPI_IOCONFIG_TRUE, -1);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);
    Cfg = Config(1, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);

    OsDrvClose(Drv);
}

DTL_TEST(PortOutOfRangeIsRefused)
{
    DtlIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    Cfg = Config(SIM_PORT_COUNT + 1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                 DTAPI_IOCONFIG_INPUT);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    Cfg.Port = 0;
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// A code without a name never reaches the driver.
DTL_TEST(UnknownCodeIsRefused)
{
    DtlIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_IN_USE);

    Cfg = Config(1, 999, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 999, DTAPI_IOCONFIG_INPUT);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    Cfg = Config(1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, 999);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// The ISI of a DVB-S2 loop-through is checked before the driver is asked, for both
// output directions.
DTL_TEST(LoopThroughIsiIsChecked)
{
    DtlIoConfig Cfg =
        Config(2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_LOOPS2TS);
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    Cfg.ParXtra[0] = 1;
    Cfg.ParXtra[1] = 256;
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ISI);
    Cfg.ParXtra[1] = -1;
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ISI);
    Cfg.Value = DTAPI_IOCONFIG_INTOUTPUT;
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ISI);

    // In range, it reaches the driver, which knows the card cannot loop DVB-S2 through.
    Cfg.Value = DTAPI_IOCONFIG_OUTPUT;
    Cfg.ParXtra[1] = 255;
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);
    Cfg.ParXtra[1] = 0;
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);

    // Another sub-value carries no ISI.
    Cfg.SubValue = DTAPI_IOCONFIG_DBLBUF;
    Cfg.ParXtra[1] = 256;
    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Cfg));

    OsDrvClose(Drv);
}

DTL_TEST(IoConfigNullArgumentsAreRefused)
{
    DtlIoConfig Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(DtlDrvGetIoConfig(NULL, &Cfg), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetIoConfig(Drv, NULL), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(NULL, &Cfg), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvSetIoConfig(Drv, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(TimeOfDayFollowsTheHostClock)
{
    uint32_t Seconds = 0, Nanoseconds = 0;
    time_t Before, After;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    Before = time(NULL);
    DTL_ASSERT_OK(DtlDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds));
    After = time(NULL);

    DTL_ASSERT(Nanoseconds < 1000000000U);
    DTL_ASSERT((time_t)Seconds + 1 >= Before);
    DTL_ASSERT((time_t)Seconds <= After + 1);

    DTL_ASSERT_EQ(DtlDrvGetTimeOfDay(NULL, &Seconds, &Nanoseconds), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetTimeOfDay(Drv, NULL, &Nanoseconds), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetTimeOfDay(Drv, &Seconds, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Wire format +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The new commands, driven below the typed layer with wrong sizes and commands. The
// emulator refuses them the way the driver does.
//

// What the typed commands put on the wire, field by field.
DTL_TEST(PropertyRequestCarriesTheFilter)
{
    DtIoctlPropCmdGetValueInput In;
    char Name[PROPERTY_NAME_MAX_SIZE];
    int FunctionCode;
    bool Value;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_OK(DtlDrvGetPropertyBool(Drv, "CAP_SDI", 3, &Value));
    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &In, sizeof(In)), sizeof(In));
    DTL_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_PROPERTY_CMD);
    CheckDeviceHeader(DtlFailures, &In.m_CmdHdr, DT_PROP_CMD_GET_VALUE);
    DTL_ASSERT_EQ(In.m_TypeNumber, -1);
    DTL_ASSERT_EQ(In.m_SubDvc, -1);
    DTL_ASSERT_EQ(In.m_SubType, -1);
    DTL_ASSERT_EQ(In.m_HardwareRevision, 0);
    DTL_ASSERT_EQ(In.m_FirmwareVersion, 0);
    DTL_ASSERT_EQ(In.m_FirmwareVariant, -1);
    DTL_ASSERT_EQ(In.m_PortIndex, 3);
    DTL_ASSERT_EQ(In.m_DtapiMaj, 6);
    DTL_ASSERT_EQ(In.m_DtapiMin, 13);
    DTL_ASSERT_EQ(In.m_DtapiBugfix, 0);

    // The name is terminated and the rest of its field is zero.
    memset(Name, 0, sizeof(Name));
    memcpy(Name, "CAP_SDI", 7);
    DTL_ASSERT_MEM(In.m_Name, Name, sizeof(Name));

    OsDrvClose(Drv);
}

DTL_TEST(IoConfigRequestsCarryOneConfiguration)
{
    RawGetIn Get;
    DtIoctlIoConfig Sent;
    DtlIoConfig Cfg = Config(2, DTAPI_IOCONFIG_IODIR, 0, 0);
    int FunctionCode;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Cfg));
    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Get, sizeof(Get)), sizeof(Get));
    DTL_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_IOCONFIG_CMD);
    CheckDeviceHeader(DtlFailures, &Get.m_CmdHdr, DT_IOCONFIG_CMD_GET_IOCONFIG);
    DTL_ASSERT_EQ(Get.m_IoConfigCount, 1);
    DTL_ASSERT_EQ(Get.m_IoCfgId.m_PortIndex, 1);
    DTL_ASSERT_STR(Get.m_IoCfgId.m_Group, "IODIR");

    Cfg = Config(2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_OK(DtlDrvSetIoConfig(Drv, &Cfg));
    DTL_ASSERT(LastSetIoConfig(&Sent));
    DTL_ASSERT_EQ(Sent.m_PortIndex, 1);
    DTL_ASSERT_STR(Sent.m_Value, "OUTPUT");
    DTL_ASSERT_STR(Sent.m_SubValue, "OUTPUT");

    {
        RawSetIn Set;

        SimDtPcieLastInput(&FunctionCode, &Set, sizeof(Set));
        CheckDeviceHeader(DtlFailures, &Set.m_CmdHdr, DT_IOCONFIG_CMD_SET_IOCONFIG);
    }

    OsDrvClose(Drv);
}

DTL_TEST(SimpleRequestsAreAHeader)
{
    DtIoctlInputDataHdr Hdr;
    DtlDriverVersion Version;
    DtlDeviceInfo Info;
    uint32_t Seconds, Nanoseconds;
    int FunctionCode;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_OK(DtlDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds));
    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DTL_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_TOD_CMD);
    CheckDeviceHeader(DtlFailures, &Hdr, DT_TOD_CMD_GET_TIME);

    DTL_ASSERT_OK(DtlDrvGetDriverVersion(Drv, &Version));
    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DTL_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_GET_DRIVER_VERSION);
    CheckDeviceHeader(DtlFailures, &Hdr, DT_IOCTL_CMD_NOP);

    DTL_ASSERT_OK(DtlDrvGetDeviceInfo(Drv, &Info));
    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DTL_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_GET_DEV_INFO2);
    CheckDeviceHeader(DtlFailures, &Hdr, DT_IOCTL_CMD_NOP);

    OsDrvClose(Drv);
}

// Nothing is recorded before the first command, and what is recorded is cut off at the
// recording's size while the full size is still reported.
DTL_TEST(RecordingOfInputs)
{
    uint8_t Big[SIM_MAX_RECORDED_INPUT + 8];
    uint8_t Back[8];
    int FunctionCode = 0;
    uint32_t Status;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, Back, sizeof(Back)), 0);
    DTL_ASSERT_EQ(FunctionCode, -1);

    memset(Big, 0x5A, sizeof(Big));
    OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), Big, sizeof(Big), NULL, NULL,
               &Status);
    DTL_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, Back, sizeof(Back)), sizeof(Big));
    DTL_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_DEBUG_CMD);
    DTL_ASSERT_EQ(Back[7], 0x5A);

    OsDrvClose(Drv);
}

DTL_TEST(PropertyRequestSizesAreChecked)
{
    DtIoctlPropCmdGetValueInput In;
    DtIoctlPropCmdGetValueOutput Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_Cmd = DT_PROP_CMD_GET_VALUE;
    snprintf(In.m_Name, sizeof(In.m_Name), "%s", "PORT_COUNT");
    In.m_PortIndex = -1;

    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_PROPERTY_CMD), &In,
                             sizeof(In) - 1, &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OutSize = sizeof(Out) - 1;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    // Unmodelled commands of a modelled IOCTL are unknown commands.
    OutSize = sizeof(Out);
    In.m_CmdHdr.m_Cmd = DT_PROP_CMD_GET_STR;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
}

// The configuration count is part of the size the request must have.
DTL_TEST(IoConfigRequestSizesFollowTheCount)
{
    RawGetIn In;
    RawGetOut Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_Cmd = DT_IOCONFIG_CMD_GET_IOCONFIG;
    In.m_IoConfigCount = 2;
    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", "IODIR");

    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_IoConfigCount = -1;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_IoConfigCount = 0;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_OK);
    DTL_ASSERT_EQ(OutSize, sizeof(DtIoctlIoConfigCmdGetIoConfigOutput));

    In.m_CmdHdr.m_Cmd = 99;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
}

static void RawSetInit(RawSetIn* In, const char* Group, const char* Value,
                       const char* SubValue)
{
    memset(In, 0, sizeof(*In));
    In->m_CmdHdr.m_Cmd = DT_IOCONFIG_CMD_SET_IOCONFIG;
    In->m_IoConfigCount = 1;
    In->m_IoCfgPars.m_PortIndex = 0;
    snprintf(In->m_IoCfgPars.m_Group, sizeof(In->m_IoCfgPars.m_Group), "%s", Group);
    snprintf(In->m_IoCfgPars.m_Value, sizeof(In->m_IoCfgPars.m_Value), "%s", Value);
    snprintf(In->m_IoCfgPars.m_SubValue, sizeof(In->m_IoCfgPars.m_SubValue), "%s",
             SubValue);
}

// Returns the status the emulated driver refused the request with, or DT_STATUS_OK.
static uint32_t RawSet(OsDrv* Drv, const RawSetIn* In, size_t InSize)
{
    uint32_t Status = 0xDEAD;

    if (OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), In, InSize, NULL, NULL,
                   &Status) == OS_IOCTL_OK)
    {
        return DT_STATUS_OK;
    }
    return Status;
}

DTL_TEST(SetRequestsAreChecked)
{
    RawSetIn In;
    DtlIoConfig Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    RawSetInit(&In, "IODIR", "OUTPUT", "OUTPUT");
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_OK);
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(DtIoctlIoConfigCmdSetIoConfigInput) - 1),
                  DT_STATUS_INVALID_PARAMETER);
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In) - 1), DT_STATUS_INVALID_PARAMETER);

    In.m_IoConfigCount = -1;
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);

    // Nothing to set is not a failure.
    In.m_IoConfigCount = 0;
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_OK);

    RawSetInit(&In, "IODIR", "OUTPUT", "INPUT");
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_CONFIG_ERROR);
    RawSetInit(&In, "IODIR", "BOGUS", "OUTPUT");
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    RawSetInit(&In, "IODIR", "OUTPUT", "BOGUS");
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    RawSetInit(&In, "", "OUTPUT", "OUTPUT");
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    RawSetInit(&In, "IODIR", "OUTPUT", "OUTPUT");
    In.m_IoCfgPars.m_PortIndex = SIM_PORT_COUNT;
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    In.m_IoCfgPars.m_PortIndex = -1;
    DTL_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);

    // A refused request leaves the configuration as the first, accepted, request made
    // it; the refused OUTPUT with sub-value INPUT in particular did not get through.
    DTL_ASSERT_OK(DtlDrvGetIoConfig(Drv, &Cfg));
    DTL_ASSERT_EQ(Cfg.Value, DTAPI_IOCONFIG_OUTPUT);
    DTL_ASSERT_EQ(Cfg.SubValue, DTAPI_IOCONFIG_OUTPUT);

    OsDrvClose(Drv);
}

DTL_TEST(GetRequestsAreChecked)
{
    RawGetIn In;
    RawGetOut Out;
    size_t OutSize;
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_Cmd = DT_IOCONFIG_CMD_GET_IOCONFIG;
    In.m_IoConfigCount = 1;
    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", "BOGUS");

    OutSize = sizeof(Out);
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", "IODIR");
    In.m_IoCfgId.m_PortIndex = -1;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_IoCfgId.m_PortIndex = 0;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In,
                             sizeof(DtIoctlIoConfigCmdGetIoConfigInput) - 1, &Out,
                             &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OutSize = sizeof(DtIoctlIoConfigCmdGetIoConfigOutput) - 1;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

// Opening allocates twice: the emulator's handle state, then the abstraction's handle.
// Either failing leaves nothing open.
DTL_TEST(OpenSurvivesAllocationFailure)
{
    OsDrv* Drv;

    SimDtPcieReset();
    DtlAllocResetCount();
    DtlAllocFailAfter(0);
    DTL_ASSERT(OsDrvOpen(SIM_DEVICE_INDEX) == NULL);

    DtlAllocResetCount();
    DtlAllocFailAfter(1);
    DTL_ASSERT(OsDrvOpen(SIM_DEVICE_INDEX) == NULL);

    DtlAllocResetCount();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    DTL_ASSERT(Drv != NULL);
    DTL_ASSERT_EQ(DtlAllocCount(), 2);
    OsDrvClose(Drv);
}

DTL_TEST(TodRequestSizesAreChecked)
{
    DtIoctlTodCmdGetTimeInput In;
    DtIoctlTodCmdGetTimeOutput Out;
    size_t OutSize = sizeof(Out) - 1;
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_Cmd = DT_TOD_CMD_GET_TIME;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_TOD_CMD), &In, sizeof(In), &Out,
                             &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OutSize = sizeof(Out);
    In.m_Cmd = DT_TOD_CMD_SET_TIME;
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_TOD_CMD), &In, sizeof(In), &Out,
                             &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
}

DTL_TEST_MAIN("SimCommands", DTL_RUN(PortCountsAreReported),
              DTL_RUN(CapabilitiesArePerPort), DTL_RUN(UnknownPropertyIsNotFound),
              DTL_RUN(PropertyNameMustFit), DTL_RUN(PropertyNullArgumentsAreRefused),
              DTL_RUN(DriverStatusBecomesTheResult), DTL_RUN(ShortAnswerIsDriverFailure),
              DTL_RUN(DeviceInfoFallsBack), DTL_RUN(DefaultDirectionsAlternate),
              DTL_RUN(ConfigurationRoundTrips), DTL_RUN(BuddyPortIsConvertedBothWays),
              DTL_RUN(OtherParametersAreNotConverted),
              DTL_RUN(DirectionsThatNamePortsAreConverted),
              DTL_RUN(ExclusiveAccessCheckIsSkippedOnlyForTheDevice),
              DTL_RUN(UnsupportedConfigurationIsConfigError),
              DTL_RUN(PortOutOfRangeIsRefused), DTL_RUN(UnknownCodeIsRefused),
              DTL_RUN(LoopThroughIsiIsChecked), DTL_RUN(IoConfigNullArgumentsAreRefused),
              DTL_RUN(TimeOfDayFollowsTheHostClock),
              DTL_RUN(PropertyRequestCarriesTheFilter),
              DTL_RUN(IoConfigRequestsCarryOneConfiguration),
              DTL_RUN(SimpleRequestsAreAHeader), DTL_RUN(RecordingOfInputs),
              DTL_RUN(PropertyRequestSizesAreChecked),
              DTL_RUN(IoConfigRequestSizesFollowTheCount), DTL_RUN(SetRequestsAreChecked),
              DTL_RUN(GetRequestsAreChecked), DTL_RUN(OpenSurvivesAllocationFailure),
              DTL_RUN(TodRequestSizesAreChecked))

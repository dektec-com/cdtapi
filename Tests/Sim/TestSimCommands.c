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
#include "Core/DtAlloc.h"           // Allocation failure injection.
#include "DtDrv.h"                  // Driver commands under test.
#include "DtDrvAbi.h"               // Raw structures and statuses.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Raw IOCTLs.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_TEST_IOCTL(Code) ((unsigned long)(Code))

// Opens the emulated device in its power-on state, or records why not. Returns NULL on
// failure, in which case the calling case must return.
static OsDrv* OpenSim(int* DtFailures)
{
    OsDrv* Drv;

    SimDtPcieReset();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrvClose(Drv);
        return NULL;
    }
    return Drv;
}

// A configuration with no extra parameters.
static DtIoConfig Config(int Port, int Group, int Value, int SubValue)
{
    DtIoConfig Result;

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
    return OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In), Out,
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
static void CheckDeviceHeader(int* DtFailures, const DtIoctlInputDataHdr* Hdr, int Cmd)
{
    DT_ASSERT_EQ(Hdr->m_Uuid, 0);
    DT_ASSERT_EQ(Hdr->m_PortIndex, -1);
    DT_ASSERT_EQ(Hdr->m_Cmd, Cmd);
    DT_ASSERT_EQ(Hdr->m_CmdEx, DT_IOCTL_CMD_NOP);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(PortCountsAreReported)
{
    int Value = -1;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvGetPropertyInt(Drv, "PORT_COUNT", DT_PROPERTY_DEVICE, &Value));
    DT_ASSERT_EQ(Value, SIM_PORT_COUNT);
    DT_ASSERT_OK(DtDrvGetPropertyInt(Drv, "MAIN_PORT_COUNT", DT_PROPERTY_DEVICE, &Value));
    DT_ASSERT_EQ(Value, SIM_PORT_COUNT);

    OsDrvClose(Drv);
}

// A capability a port lacks is found, with the value false, as the driver answers.
DT_TEST(CapabilitiesArePerPort)
{
    bool Value = false;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_3GSDI", 0, &Value));
    DT_ASSERT(Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_OUTPUT", SIM_SDI_PORT_COUNT - 1, &Value));
    DT_ASSERT(Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_12GSDI", 0, &Value));
    DT_ASSERT(!Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_GENREF", SIM_SDI_PORT_COUNT, &Value));
    DT_ASSERT(Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_INPUT", SIM_SDI_PORT_COUNT, &Value));
    DT_ASSERT(!Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_VIRTUAL", SIM_PORT_COUNT - 1, &Value));
    DT_ASSERT(Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_VIRTUAL", SIM_SDI_PORT_COUNT, &Value));
    DT_ASSERT(!Value);

    // Beyond the last port, and for the device itself, there are no capabilities.
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_SDI", SIM_PORT_COUNT, &Value));
    DT_ASSERT(!Value);
    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_SDI", DT_PROPERTY_DEVICE, &Value));
    DT_ASSERT(!Value);

    OsDrvClose(Drv);
}

DT_TEST(UnknownPropertyIsNotFound)
{
    int Value = 7;
    bool Flag = true;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, "NO_SUCH", DT_PROPERTY_DEVICE, &Value),
                 DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(Value, 0);
    DT_ASSERT_EQ(DtDrvGetPropertyBool(Drv, "PORT_COUNT", 0, &Flag), DTAPI_E_NOT_FOUND);
    DT_ASSERT(!Flag);

    OsDrvClose(Drv);
}

// The driver's name field holds 50 bytes, terminator included.
DT_TEST(PropertyNameMustFit)
{
    char Name[64];
    int Value;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(Name, 'A', sizeof(Name));
    Name[PROPERTY_NAME_MAX_SIZE] = '\0';
    DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, Name, DT_PROPERTY_DEVICE, &Value),
                 DTAPI_E_BUF_TOO_SMALL);

    Name[PROPERTY_NAME_MAX_SIZE - 1] = '\0';
    DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, Name, DT_PROPERTY_DEVICE, &Value),
                 DTAPI_E_NOT_FOUND);

    OsDrvClose(Drv);
}

DT_TEST(PropertyNullArgumentsAreRefused)
{
    int Value = 7;
    bool Flag = true;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtDrvGetPropertyInt(NULL, "PORT_COUNT", -1, &Value),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Value, 0);
    DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, NULL, -1, &Value), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, "PORT_COUNT", -1, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetPropertyBool(NULL, "CAP_SDI", 0, &Flag), DTAPI_E_INVALID_ARG);
    DT_ASSERT(!Flag);
    DT_ASSERT_EQ(DtDrvGetPropertyBool(Drv, NULL, 0, &Flag), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetPropertyBool(Drv, "CAP_SDI", 0, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver failures +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A refused command reaches the caller as the result its status stands for, and a short
// answer as a driver failure, through every typed command.
//

DT_TEST(DriverStatusBecomesTheResult)
{
    DtDriverVersion Version;
    DtIoConfig Cfg =
        Config(1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    uint32_t Seconds, Nanoseconds;
    int Value;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_PROPERTY_CMD, DT_STATUS_IN_USE);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_EXCL_ACCESS_REQD);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_TOD_CMD, DT_STATUS_BUSY);
    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DRIVER_VERSION, DT_STATUS_VERSION_MISMATCH);

    DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, "PORT_COUNT", -1, &Value), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_EQ(DtDrvGetIoConfig(Drv, &Cfg), DTAPI_E_EXCL_ACCESS_REQD);
    DT_ASSERT_EQ(DtDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds), DTAPI_E_BUSY);
    DT_ASSERT_EQ(DtDrvGetDriverVersion(Drv, &Version), DTAPI_E_DRIVER_INCOMP);

    OsDrvClose(Drv);
}

DT_TEST(ShortAnswerIsDriverFailure)
{
    DtIoConfig Cfg = Config(1, DTAPI_IOCONFIG_IODIR, -1, -1);
    uint32_t Seconds, Nanoseconds;
    bool Flag;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieAnswerShort(DT_FUNC_CODE_PROPERTY_CMD);
    SimDtPcieAnswerShort(DT_FUNC_CODE_IOCONFIG_CMD);
    SimDtPcieAnswerShort(DT_FUNC_CODE_TOD_CMD);

    DT_ASSERT_EQ(DtDrvGetPropertyBool(Drv, "CAP_SDI", 0, &Flag), DTAPI_E_DEV_DRIVER);
    DT_ASSERT_EQ(DtDrvGetIoConfig(Drv, &Cfg), DTAPI_E_DEV_DRIVER);
    DT_ASSERT_EQ(DtDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds), DTAPI_E_DEV_DRIVER);

    OsDrvClose(Drv);
}

// A driver without GET_DEV_INFO2 is asked the original command; only when both fail does
// the failure reach the caller.
DT_TEST(DeviceInfoFallsBack)
{
    DtDeviceInfo Info;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO2, DT_STATUS_NOT_SUPPORTED);
    DT_ASSERT_OK(DtDrvGetDeviceInfo(Drv, &Info));
    DT_ASSERT_EQ(Info.Serial, (int64_t)SIM_SERIAL);

    SimDtPcieFailWithStatus(DT_FUNC_CODE_GET_DEV_INFO, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDrvGetDeviceInfo(Drv, &Info), DTAPI_E_TIMEOUT);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= I/O configuration +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(DefaultDirectionsAlternate)
{
    DtIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Cfg));
    DT_ASSERT_EQ(Cfg.Value, DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(Cfg.SubValue, DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(Cfg.ParXtra[0], -1);
    DT_ASSERT_EQ(Cfg.ParXtra[1], -1);

    Cfg = Config(2, DTAPI_IOCONFIG_IODIR, 0, 0);
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Cfg));
    DT_ASSERT_EQ(Cfg.Value, DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_EQ(Cfg.SubValue, DTAPI_IOCONFIG_OUTPUT);

    // A group the port has no setting for reads back as -1, the empty name.
    Cfg = Config(SIM_SDI_PORT_COUNT + 1, DTAPI_IOCONFIG_IODIR, 0, 0);
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Cfg));
    DT_ASSERT_EQ(Cfg.Value, -1);
    DT_ASSERT_EQ(Cfg.SubValue, -1);

    OsDrvClose(Drv);
}

DT_TEST(ConfigurationRoundTrips)
{
    DtIoConfig Set =
        Config(1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    DtIoConfig Get = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Set));
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Get));
    DT_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_EQ(Get.SubValue, DTAPI_IOCONFIG_OUTPUT);

    Set = Config(1, DTAPI_IOCONFIG_GENLOCKED, DTAPI_IOCONFIG_TRUE, -1);
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Set));
    Get = Config(1, DTAPI_IOCONFIG_GENLOCKED, 0, 0);
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Get));
    DT_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_TRUE);
    DT_ASSERT_EQ(Get.SubValue, -1);

    // The state belongs to the card, not to the handle.
    OsDrvClose(Drv);
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    Get = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Get));
    DT_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_OUTPUT);

    SimDtPcieReset();
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Get));
    DT_ASSERT_EQ(Get.Value, DTAPI_IOCONFIG_INPUT);

    OsDrvClose(Drv);
}

// DTAPI numbers the buddy port of a double-buffered output from 1, the driver from 0.
DT_TEST(BuddyPortIsConvertedBothWays)
{
    DtIoConfig Set =
        Config(2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_DBLBUF);
    DtIoConfig Get = Config(2, DTAPI_IOCONFIG_IODIR, 0, 0);
    RawGetOut Raw;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Set.ParXtra[0] = 4;
    Set.ParXtra[1] = 77;
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Set));

    DT_ASSERT_EQ(RawGet(Drv, 1, "IODIR", &Raw), OS_IOCTL_OK);
    DT_ASSERT_STR(Raw.m_IoCfgValue.m_Value, "OUTPUT");
    DT_ASSERT_STR(Raw.m_IoCfgValue.m_SubValue, "DBLBUF");
    DT_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[0], 3);
    DT_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[1], 77);
    DT_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[2], -1);
    DT_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[3], -1);

    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Get));
    DT_ASSERT_EQ(Get.SubValue, DTAPI_IOCONFIG_DBLBUF);
    DT_ASSERT_EQ(Get.ParXtra[0], 4);
    DT_ASSERT_EQ(Get.ParXtra[1], 77);

    OsDrvClose(Drv);
}

// Only the I/O direction values that name a port have ParXtra[0] converted.
DT_TEST(OtherParametersAreNotConverted)
{
    DtIoConfig Set =
        Config(1, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_3GSDI, DTAPI_IOCONFIG_1080P50);
    DtIoConfig Get = Config(1, DTAPI_IOCONFIG_IOSTD, 0, 0);
    RawGetOut Raw;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Set.ParXtra[0] = 4;
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Set));
    DT_ASSERT_EQ(RawGet(Drv, 0, "IOSTD", &Raw), OS_IOCTL_OK);
    DT_ASSERT_EQ(Raw.m_IoCfgValue.m_ParXtra[0], 4);
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Get));
    DT_ASSERT_EQ(Get.ParXtra[0], 4);
    DT_ASSERT_EQ(Get.SubValue, DTAPI_IOCONFIG_1080P50);

    OsDrvClose(Drv);
}

// Every I/O direction whose ParXtra[0] names a port, as the driver receives it.
DT_TEST(DirectionsThatNamePortsAreConverted)
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
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        DtIoConfig Cfg =
            Config(3, DTAPI_IOCONFIG_IODIR, Cases[i].Value, Cases[i].SubValue);

        Cfg.ParXtra[0] = 5;
        Cfg.ParXtra[1] = 6;
        DtDrvSetIoConfig(Drv, &Cfg);

        DT_ASSERT(LastSetIoConfig(&Sent));
        if (Sent.m_ParXtra[0] != Cases[i].Expected)
            DT_FAIL("case %d: ParXtra[0] sent as %lld", (int)i,
                    (long long)Sent.m_ParXtra[0]);
        DT_ASSERT_EQ(Sent.m_ParXtra[1], 6);
        DT_ASSERT_EQ(Sent.m_ParXtra[2], -1);
        DT_ASSERT_EQ(Sent.m_ParXtra[3], -1);
        DT_ASSERT_EQ(Sent.m_PortIndex, 2);
        DT_ASSERT_STR(Sent.m_Group, "IODIR");
        DT_ASSERT_EQ(Sent.m_SkipExclAccessCheck, 0);
    }

    // The same value outside the direction group names no port.
    {
        DtIoConfig Cfg = Config(3, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_MONITOR,
                                DTAPI_IOCONFIG_MONITOR);

        Cfg.ParXtra[0] = 5;
        DtDrvSetIoConfig(Drv, &Cfg);
        DT_ASSERT(LastSetIoConfig(&Sent));
        DT_ASSERT_EQ(Sent.m_ParXtra[0], 5);
        DT_ASSERT_STR(Sent.m_Value, "MONITOR");
        DT_ASSERT_STR(Sent.m_SubValue, "MONITOR");
    }

    OsDrvClose(Drv);
}

// DTAPI skips the driver's exclusive-access check for the port its proxy addresses,
// which for a device-level request is port index -1.
DT_TEST(ExclusiveAccessCheckIsSkippedOnlyForTheDevice)
{
    DtIoConfig Cfg =
        Config(0, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT);
    DtIoctlIoConfig Sent;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtDrvSetIoConfig(Drv, &Cfg);
    DT_ASSERT(LastSetIoConfig(&Sent));
    DT_ASSERT_EQ(Sent.m_PortIndex, -1);
    DT_ASSERT_EQ(Sent.m_SkipExclAccessCheck, 1);

    Cfg.Port = 1;
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Cfg));
    DT_ASSERT(LastSetIoConfig(&Sent));
    DT_ASSERT_EQ(Sent.m_SkipExclAccessCheck, 0);

    OsDrvClose(Drv);
}

DT_TEST(UnsupportedConfigurationIsConfigError)
{
    DtIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    // The genlock reference port has no direction, and an SDI port is no reference.
    Cfg = Config(SIM_SDI_PORT_COUNT + 1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                 DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);
    Cfg = Config(1, DTAPI_IOCONFIG_GENREF, DTAPI_IOCONFIG_TRUE, -1);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);
    Cfg = Config(1, DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_12GSDI, DTAPI_IOCONFIG_2160P50);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);

    OsDrvClose(Drv);
}

DT_TEST(PortOutOfRangeIsRefused)
{
    DtIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Cfg = Config(SIM_PORT_COUNT + 1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                 DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    Cfg.Port = 0;
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// A code without a name never reaches the driver.
DT_TEST(UnknownCodeIsRefused)
{
    DtIoConfig Cfg;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_IN_USE);

    Cfg = Config(1, 999, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 999, DTAPI_IOCONFIG_INPUT);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);
    Cfg = Config(1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, 999);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// The ISI of a DVB-S2 loop-through is checked before the driver is asked, for both
// output directions.
DT_TEST(LoopThroughIsiIsChecked)
{
    DtIoConfig Cfg =
        Config(2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_LOOPS2TS);
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Cfg.ParXtra[0] = 1;
    Cfg.ParXtra[1] = 256;
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ISI);
    Cfg.ParXtra[1] = -1;
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ISI);
    Cfg.Value = DTAPI_IOCONFIG_INTOUTPUT;
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_INVALID_ISI);

    // In range, it reaches the driver, which knows the card cannot loop DVB-S2 through.
    Cfg.Value = DTAPI_IOCONFIG_OUTPUT;
    Cfg.ParXtra[1] = 255;
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);
    Cfg.ParXtra[1] = 0;
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, &Cfg), DTAPI_E_CONFIG);

    // Another sub-value carries no ISI.
    Cfg.SubValue = DTAPI_IOCONFIG_DBLBUF;
    Cfg.ParXtra[1] = 256;
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Cfg));

    OsDrvClose(Drv);
}

DT_TEST(IoConfigNullArgumentsAreRefused)
{
    DtIoConfig Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtDrvGetIoConfig(NULL, &Cfg), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetIoConfig(Drv, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSetIoConfig(NULL, &Cfg), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSetIoConfig(Drv, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time of day +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(TimeOfDayFollowsTheHostClock)
{
    uint32_t Seconds = 0, Nanoseconds = 0;
    time_t Before, After;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Before = time(NULL);
    DT_ASSERT_OK(DtDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds));
    After = time(NULL);

    DT_ASSERT(Nanoseconds < 1000000000U);
    DT_ASSERT((time_t)Seconds + 1 >= Before);
    DT_ASSERT((time_t)Seconds <= After + 1);

    DT_ASSERT_EQ(DtDrvGetTimeOfDay(NULL, &Seconds, &Nanoseconds), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetTimeOfDay(Drv, NULL, &Nanoseconds), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetTimeOfDay(Drv, &Seconds, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Wire format +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The new commands, driven below the typed layer with wrong sizes and commands. The
// emulator refuses them the way the driver does.
//

// What the typed commands put on the wire, field by field.
DT_TEST(PropertyRequestCarriesTheFilter)
{
    DtIoctlPropCmdGetValueInput In;
    char Name[PROPERTY_NAME_MAX_SIZE];
    int FunctionCode;
    bool Value;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvGetPropertyBool(Drv, "CAP_SDI", 3, &Value));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &In, sizeof(In)), sizeof(In));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_PROPERTY_CMD);
    CheckDeviceHeader(DtFailures, &In.m_CmdHdr, DT_PROP_CMD_GET_VALUE);
    DT_ASSERT_EQ(In.m_TypeNumber, -1);
    DT_ASSERT_EQ(In.m_SubDvc, -1);
    DT_ASSERT_EQ(In.m_SubType, -1);
    DT_ASSERT_EQ(In.m_HardwareRevision, 0);
    DT_ASSERT_EQ(In.m_FirmwareVersion, 0);
    DT_ASSERT_EQ(In.m_FirmwareVariant, -1);
    DT_ASSERT_EQ(In.m_PortIndex, 3);
    DT_ASSERT_EQ(In.m_DtapiMaj, 6);
    DT_ASSERT_EQ(In.m_DtapiMin, 13);
    DT_ASSERT_EQ(In.m_DtapiBugfix, 0);

    // The name is terminated and the rest of its field is zero.
    memset(Name, 0, sizeof(Name));
    memcpy(Name, "CAP_SDI", 7);
    DT_ASSERT_MEM(In.m_Name, Name, sizeof(Name));

    OsDrvClose(Drv);
}

DT_TEST(IoConfigRequestsCarryOneConfiguration)
{
    RawGetIn Get;
    DtIoctlIoConfig Sent;
    DtIoConfig Cfg = Config(2, DTAPI_IOCONFIG_IODIR, 0, 0);
    int FunctionCode;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Cfg));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Get, sizeof(Get)), sizeof(Get));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_IOCONFIG_CMD);
    CheckDeviceHeader(DtFailures, &Get.m_CmdHdr, DT_IOCONFIG_CMD_GET_IOCONFIG);
    DT_ASSERT_EQ(Get.m_IoConfigCount, 1);
    DT_ASSERT_EQ(Get.m_IoCfgId.m_PortIndex, 1);
    DT_ASSERT_STR(Get.m_IoCfgId.m_Group, "IODIR");

    Cfg = Config(2, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT, DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Cfg));
    DT_ASSERT(LastSetIoConfig(&Sent));
    DT_ASSERT_EQ(Sent.m_PortIndex, 1);
    DT_ASSERT_STR(Sent.m_Value, "OUTPUT");
    DT_ASSERT_STR(Sent.m_SubValue, "OUTPUT");

    {
        RawSetIn Set;

        SimDtPcieLastInput(&FunctionCode, &Set, sizeof(Set));
        CheckDeviceHeader(DtFailures, &Set.m_CmdHdr, DT_IOCONFIG_CMD_SET_IOCONFIG);
    }

    OsDrvClose(Drv);
}

DT_TEST(SimpleRequestsAreAHeader)
{
    DtIoctlInputDataHdr Hdr;
    DtDriverVersion Version;
    DtDeviceInfo Info;
    uint32_t Seconds, Nanoseconds;
    int FunctionCode;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvGetTimeOfDay(Drv, &Seconds, &Nanoseconds));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_TOD_CMD);
    CheckDeviceHeader(DtFailures, &Hdr, DT_TOD_CMD_GET_TIME);

    DT_ASSERT_OK(DtDrvGetDriverVersion(Drv, &Version));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_GET_DRIVER_VERSION);
    CheckDeviceHeader(DtFailures, &Hdr, DT_IOCTL_CMD_NOP);

    DT_ASSERT_OK(DtDrvGetDeviceInfo(Drv, &Info));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_GET_DEV_INFO2);
    CheckDeviceHeader(DtFailures, &Hdr, DT_IOCTL_CMD_NOP);

    OsDrvClose(Drv);
}

// Nothing is recorded before the first command, and what is recorded is cut off at the
// recording's size while the full size is still reported.
DT_TEST(RecordingOfInputs)
{
    uint8_t Big[SIM_MAX_RECORDED_INPUT + 8];
    uint8_t Back[8];
    int FunctionCode = 0;
    uint32_t Status;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, Back, sizeof(Back)), 0);
    DT_ASSERT_EQ(FunctionCode, -1);

    memset(Big, 0x5A, sizeof(Big));
    OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), Big, sizeof(Big), NULL, NULL,
               &Status);
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, Back, sizeof(Back)), sizeof(Big));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_DEBUG_CMD);
    DT_ASSERT_EQ(Back[7], 0x5A);

    OsDrvClose(Drv);
}

DT_TEST(PropertyRequestSizesAreChecked)
{
    DtIoctlPropCmdGetValueInput In;
    DtIoctlPropCmdGetValueOutput Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_Cmd = DT_PROP_CMD_GET_VALUE;
    snprintf(In.m_Name, sizeof(In.m_Name), "%s", "PORT_COUNT");
    In.m_PortIndex = -1;

    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_PROPERTY_CMD), &In,
                            sizeof(In) - 1, &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OutSize = sizeof(Out) - 1;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    // Unmodelled commands of a modelled IOCTL are unknown commands.
    OutSize = sizeof(Out);
    In.m_CmdHdr.m_Cmd = DT_PROP_CMD_GET_STR;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_PROPERTY_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
}

// The configuration count is part of the size the request must have.
DT_TEST(IoConfigRequestSizesFollowTheCount)
{
    RawGetIn In;
    RawGetOut Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_Cmd = DT_IOCONFIG_CMD_GET_IOCONFIG;
    In.m_IoConfigCount = 2;
    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", "IODIR");

    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_IoConfigCount = -1;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_IoConfigCount = 0;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_OK);
    DT_ASSERT_EQ(OutSize, sizeof(DtIoctlIoConfigCmdGetIoConfigOutput));

    In.m_CmdHdr.m_Cmd = 99;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

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

    if (OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), In, InSize, NULL, NULL,
                   &Status) == OS_IOCTL_OK)
    {
        return DT_STATUS_OK;
    }
    return Status;
}

DT_TEST(SetRequestsAreChecked)
{
    RawSetIn In;
    DtIoConfig Cfg = Config(1, DTAPI_IOCONFIG_IODIR, 0, 0);
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    RawSetInit(&In, "IODIR", "OUTPUT", "OUTPUT");
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_OK);
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(DtIoctlIoConfigCmdSetIoConfigInput) - 1),
                 DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In) - 1), DT_STATUS_INVALID_PARAMETER);

    In.m_IoConfigCount = -1;
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);

    // Nothing to set is not a failure.
    In.m_IoConfigCount = 0;
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_OK);

    RawSetInit(&In, "IODIR", "OUTPUT", "INPUT");
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_CONFIG_ERROR);
    RawSetInit(&In, "IODIR", "BOGUS", "OUTPUT");
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    RawSetInit(&In, "IODIR", "OUTPUT", "BOGUS");
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    RawSetInit(&In, "", "OUTPUT", "OUTPUT");
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    RawSetInit(&In, "IODIR", "OUTPUT", "OUTPUT");
    In.m_IoCfgPars.m_PortIndex = SIM_PORT_COUNT;
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);
    In.m_IoCfgPars.m_PortIndex = -1;
    DT_ASSERT_EQ(RawSet(Drv, &In, sizeof(In)), DT_STATUS_INVALID_PARAMETER);

    // A refused request leaves the configuration as the first, accepted, request made
    // it; the refused OUTPUT with sub-value INPUT in particular did not get through.
    DT_ASSERT_OK(DtDrvGetIoConfig(Drv, &Cfg));
    DT_ASSERT_EQ(Cfg.Value, DTAPI_IOCONFIG_OUTPUT);
    DT_ASSERT_EQ(Cfg.SubValue, DTAPI_IOCONFIG_OUTPUT);

    OsDrvClose(Drv);
}

DT_TEST(GetRequestsAreChecked)
{
    RawGetIn In;
    RawGetOut Out;
    size_t OutSize;
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_CmdHdr.m_Cmd = DT_IOCONFIG_CMD_GET_IOCONFIG;
    In.m_IoConfigCount = 1;
    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", "BOGUS");

    OutSize = sizeof(Out);
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    snprintf(In.m_IoCfgId.m_Group, sizeof(In.m_IoCfgId.m_Group), "%s", "IODIR");
    In.m_IoCfgId.m_PortIndex = -1;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_IoCfgId.m_PortIndex = 0;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In,
                            sizeof(DtIoctlIoConfigCmdGetIoConfigInput) - 1, &Out,
                            &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OutSize = sizeof(DtIoctlIoConfigCmdGetIoConfigOutput) - 1;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_IOCONFIG_CMD), &In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

// Opening allocates twice: the emulator's handle state, then the abstraction's handle.
// Either failing leaves nothing open.
DT_TEST(OpenSurvivesAllocationFailure)
{
    OsDrv* Drv;

    SimDtPcieReset();
    DtAllocResetCount();
    DtAllocFailAfter(0);
    DT_ASSERT(OsDrvOpen(SIM_DEVICE_INDEX) == NULL);

    DtAllocResetCount();
    DtAllocFailAfter(1);
    DT_ASSERT(OsDrvOpen(SIM_DEVICE_INDEX) == NULL);

    DtAllocResetCount();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    DT_ASSERT(Drv != NULL);
    DT_ASSERT_EQ(DtAllocCount(), 2);
    OsDrvClose(Drv);
}

DT_TEST(TodRequestSizesAreChecked)
{
    DtIoctlTodCmdGetTimeInput In;
    DtIoctlTodCmdGetTimeOutput Out;
    size_t OutSize = sizeof(Out) - 1;
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_Cmd = DT_TOD_CMD_GET_TIME;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_TOD_CMD), &In, sizeof(In), &Out,
                            &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OutSize = sizeof(Out);
    In.m_Cmd = DT_TOD_CMD_SET_TIME;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_TOD_CMD), &In, sizeof(In), &Out,
                            &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
}

DT_TEST_MAIN("SimCommands", DT_RUN(PortCountsAreReported), DT_RUN(CapabilitiesArePerPort),
             DT_RUN(UnknownPropertyIsNotFound), DT_RUN(PropertyNameMustFit),
             DT_RUN(PropertyNullArgumentsAreRefused),
             DT_RUN(DriverStatusBecomesTheResult), DT_RUN(ShortAnswerIsDriverFailure),
             DT_RUN(DeviceInfoFallsBack), DT_RUN(DefaultDirectionsAlternate),
             DT_RUN(ConfigurationRoundTrips), DT_RUN(BuddyPortIsConvertedBothWays),
             DT_RUN(OtherParametersAreNotConverted),
             DT_RUN(DirectionsThatNamePortsAreConverted),
             DT_RUN(ExclusiveAccessCheckIsSkippedOnlyForTheDevice),
             DT_RUN(UnsupportedConfigurationIsConfigError),
             DT_RUN(PortOutOfRangeIsRefused), DT_RUN(UnknownCodeIsRefused),
             DT_RUN(LoopThroughIsiIsChecked), DT_RUN(IoConfigNullArgumentsAreRefused),
             DT_RUN(TimeOfDayFollowsTheHostClock),
             DT_RUN(PropertyRequestCarriesTheFilter),
             DT_RUN(IoConfigRequestsCarryOneConfiguration),
             DT_RUN(SimpleRequestsAreAHeader), DT_RUN(RecordingOfInputs),
             DT_RUN(PropertyRequestSizesAreChecked),
             DT_RUN(IoConfigRequestSizesFollowTheCount), DT_RUN(SetRequestsAreChecked),
             DT_RUN(GetRequestsAreChecked), DT_RUN(OpenSurvivesAllocationFailure),
             DT_RUN(TodRequestSizesAreChecked))

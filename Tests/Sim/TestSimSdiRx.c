// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimSdiRx.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - String properties, function routing and the SDI receiver in the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state and checks that the handle really is emulated.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "DtDrv.h"                  // Driver commands under test.
#include "DtDrvAbi.h"               // Raw structures and statuses.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Raw IOCTLs.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_TEST_IOCTL(Code) ((unsigned long)(Code))

// The functions of the ASI/SDI receiver of every SDI port, in the order a DTA-2178 lists
// them, with their roles and types.
typedef struct Function
{
    const char* Name;
    const char* Role;
    int Type;
    bool IsDf;
} Function;

static const Function g_Functions[] = {
    {"BC_SWITCH#2", "SDI_MUX_IN", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_ST425LR#1", "", DT_BLOCK_TYPE_ST425LR, false},
    {"BC_SDIMUX12G#1", "", DT_BLOCK_TYPE_SDIMUX12G, false},
    {"BC_SWITCH#3", "SDI_MUX_OUT", DT_BLOCK_TYPE_SWITCH, false},
    {"BC_SDIRXF#1", "", DT_BLOCK_TYPE_SDIRXF, false},
    {"DF_SDIRX#1", "", DT_FUNC_TYPE_SDIRX, true},
    {"DF_ASIRX#1", "", DT_FUNC_TYPE_ASIRX, true},
    {"DF_CHSDIRX#1", "", DT_FUNC_TYPE_CHSDIRX, true},
};

#define FUNCTION_COUNT ((int)(sizeof(g_Functions) / sizeof(g_Functions[0])))

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

// The UUID of a function of the port, as the card reports it; -1 when it cannot be read.
static int UuidOf(OsDrv* Drv, const char* Name, int PortIndex)
{
    char Key[PROPERTY_NAME_MAX_SIZE];
    int Uuid;

    snprintf(Key, sizeof(Key), "%s_UUID", Name);
    return DtDrvGetPropertyInt(Drv, Key, PortIndex, &Uuid) == DTAPI_OK ? Uuid : -1;
}

// A raw SDI receiver request: a header for the function.
static int RawSdiRx(OsDrv* Drv, int Uuid, int PortIndex, int Cmd, size_t InSize,
                    size_t OutSize, uint32_t* Status)
{
    DtIoctlInputDataHdr In;
    DtIoctlSdiRxCmdGetSdiStatusOutput2 Out;
    size_t Returned = OutSize;

    memset(&In, 0, sizeof(In));
    In.m_Uuid = Uuid;
    In.m_PortIndex = PortIndex;
    In.m_Cmd = Cmd;
    In.m_CmdEx = DT_IOCTL_CMD_NOP;
    return OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_SDIRX_CMD), &In, InSize, &Out,
                      &Returned, Status);
}

// A locked 1080i50 signal: 563 and 562 lines, 2640 samples per line of which 1920 active,
// and a VPID for 1080-line HD at 25 frames, interlaced, 16:9.
static SimSdiSignal Signal1080i50(void)
{
    SimSdiSignal Signal;

    memset(&Signal, 0, sizeof(Signal));
    Signal.CarrierDetect = 1;
    Signal.SdiLock = 1;
    Signal.LineLock = 1;
    Signal.Valid = 1;
    Signal.NumSymsHanc = 2 * (2640 - 1920);
    Signal.NumSymsVidVanc = 2 * 1920;
    Signal.NumLinesF1 = 563;
    Signal.NumLinesF2 = 562;
    Signal.IsLevelB = 0;
    Signal.PayloadId = 0x00800585;
    Signal.FramePeriod = 40000000;
    Signal.SdiRate = DT_DRV_SDIRATE_HD;
    return Signal;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= String properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(StringPropertiesAreRead)
{
    char Str[DT_PROPERTY_STR_SIZE];
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    snprintf(Str, sizeof(Str), "x");
    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", 0, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, "");
    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1.6", 3, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, "DF_SDIRX#1");
    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "BC_SWITCH#3", 7, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, "SDI_MUX_OUT");

    OsDrvClose(Drv);
}

// What the typed command puts on the wire: the filter of a value request, with the string
// command.
DT_TEST(StringRequestCarriesTheFilter)
{
    DtIoctlPropCmdGetStrInput In;
    char Name[PROPERTY_NAME_MAX_SIZE];
    char Str[DT_PROPERTY_STR_SIZE];
    int FunctionCode;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", 2, Str, sizeof(Str)));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &In, sizeof(In)), sizeof(In));
    DT_ASSERT_EQ(sizeof(In), 104);
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_PROPERTY_CMD);
    DT_ASSERT_EQ(In.m_CmdHdr.m_Uuid, 0);
    DT_ASSERT_EQ(In.m_CmdHdr.m_PortIndex, -1);
    DT_ASSERT_EQ(In.m_CmdHdr.m_Cmd, DT_PROP_CMD_GET_STR);
    DT_ASSERT_EQ(In.m_CmdHdr.m_CmdEx, DT_IOCTL_CMD_NOP);
    DT_ASSERT_EQ(In.m_TypeNumber, -1);
    DT_ASSERT_EQ(In.m_SubDvc, -1);
    DT_ASSERT_EQ(In.m_SubType, -1);
    DT_ASSERT_EQ(In.m_HardwareRevision, 0);
    DT_ASSERT_EQ(In.m_FirmwareVersion, 0);
    DT_ASSERT_EQ(In.m_FirmwareVariant, -1);
    DT_ASSERT_EQ(In.m_PortIndex, 2);
    DT_ASSERT_EQ(In.m_DtapiMaj, 6);
    DT_ASSERT_EQ(In.m_DtapiMin, 13);
    DT_ASSERT_EQ(In.m_DtapiBugfix, 0);

    memset(Name, 0, sizeof(Name));
    memcpy(Name, "AF_ASISDIRX#1", 13);
    DT_ASSERT_MEM(In.m_Name, Name, sizeof(Name));

    OsDrvClose(Drv);
}

// A string the port or the card does not have is not found, and leaves the buffer empty.
DT_TEST(MissingStringIsNotFound)
{
    char Str[DT_PROPERTY_STR_SIZE];
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    snprintf(Str, sizeof(Str), "x");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#2", 0, Str, sizeof(Str)),
                 DTAPI_E_NOT_FOUND);
    DT_ASSERT_STR(Str, "");
    DT_ASSERT_EQ(
        DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", SIM_SDI_PORT_COUNT, Str, sizeof(Str)),
        DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(
        DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", DT_PROPERTY_DEVICE, Str, sizeof(Str)),
        DTAPI_E_NOT_FOUND);

    // A value property is no string, and a string no value.
    DT_ASSERT_EQ(
        DtDrvGetPropertyStr(Drv, "PORT_COUNT", DT_PROPERTY_DEVICE, Str, sizeof(Str)),
        DTAPI_E_NOT_FOUND);
    {
        int Value;

        DT_ASSERT_EQ(DtDrvGetPropertyInt(Drv, "DF_SDIRX#1", 0, &Value),
                     DTAPI_E_NOT_FOUND);
    }

    OsDrvClose(Drv);
}

// The driver may fill its whole field without a terminator; such a string fits a buffer
// of DT_PROPERTY_STR_SIZE, and one byte less is too small.
DT_TEST(StringMustFitTheBuffer)
{
    char Filled[PROPERTY_STR_MAX_SIZE + 1];
    char Str[DT_PROPERTY_STR_SIZE];
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(Filled, 'L', PROPERTY_STR_MAX_SIZE);
    Filled[PROPERTY_STR_MAX_SIZE] = '\0';
    SimDtPcieOverrideString("LONG", 0, true, Filled);

    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "LONG", 0, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, Filled);

    snprintf(Str, sizeof(Str), "x");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "LONG", 0, Str, sizeof(Str) - 1),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_STR(Str, "");

    // The empty string needs the terminator only, and a one-byte buffer is cleared too.
    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", 0, Str, 1));
    DT_ASSERT_STR(Str, "");
    Str[0] = 'x';
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "NONE", 0, Str, 1), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(Str[0], '\0');

    OsDrvClose(Drv);
}

DT_TEST(StringNameMustFit)
{
    char Name[PROPERTY_NAME_MAX_SIZE + 1];
    char Str[DT_PROPERTY_STR_SIZE];
    int FunctionCode;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(Name, 'N', PROPERTY_NAME_MAX_SIZE);
    Name[PROPERTY_NAME_MAX_SIZE] = '\0';
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, Name, 0, Str, sizeof(Str)),
                 DTAPI_E_BUF_TOO_SMALL);

    // Refused before anything reaches the driver.
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, Str, 0), 0);

    OsDrvClose(Drv);
}

DT_TEST(StringNullArgumentsAreRefused)
{
    char Str[DT_PROPERTY_STR_SIZE];
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    snprintf(Str, sizeof(Str), "x");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(NULL, "AF_ASISDIRX#1", 0, Str, sizeof(Str)),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_STR(Str, "");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, NULL, 0, Str, sizeof(Str)),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", 0, NULL, sizeof(Str)),
                 DTAPI_E_INVALID_ARG);
    snprintf(Str, sizeof(Str), "x");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", 0, Str, 0),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_STR(Str, "x");

    OsDrvClose(Drv);
}

DT_TEST(StringFailuresBecomeResults)
{
    char Str[DT_PROPERTY_STR_SIZE];
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_PROPERTY_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", 0, Str, sizeof(Str)),
                 DTAPI_E_TIMEOUT);

    SimDtPcieReset();
    SimDtPcieAnswerShort(DT_FUNC_CODE_PROPERTY_CMD);
    snprintf(Str, sizeof(Str), "x");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1.1", 0, Str, sizeof(Str)),
                 DTAPI_E_DEV_DRIVER);
    DT_ASSERT_STR(Str, "");

    OsDrvClose(Drv);
}

// A string override replaces the string only, and is found or hidden per port.
DT_TEST(StringOverridesAreSeparate)
{
    char Str[DT_PROPERTY_STR_SIZE];
    int Value = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieOverrideString("DF_SDIRX#1", 1, true, "ROLE");
    SimDtPcieOverrideString("AF_ASISDIRX#1.6", 1, false, NULL);

    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "DF_SDIRX#1", 1, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, "ROLE");
    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "DF_SDIRX#1", 0, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, "");
    DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1.6", 1, Str, sizeof(Str)),
                 DTAPI_E_NOT_FOUND);
    DT_ASSERT_OK(DtDrvGetPropertyInt(Drv, "DF_SDIRX#1_TYPE", 1, &Value));
    DT_ASSERT_EQ(Value, DT_FUNC_TYPE_SDIRX);

    // A value override with the same name as a string leaves the string alone, and the
    // other way round.
    SimDtPcieOverrideProperty("AF_ASISDIRX#1.1", 2, true, 5);
    DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1.1", 2, Str, sizeof(Str)));
    DT_ASSERT_STR(Str, "BC_SWITCH#2");
    SimDtPcieOverrideString("DF_SDIRX#1_UUID", 2, false, NULL);
    DT_ASSERT(UuidOf(Drv, "DF_SDIRX#1", 2) > 0);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every SDI port has the ASI/SDI receiver function with the same eight parts, each with
// its role, type and a UUID flagged as a building block or a driver function. No two
// parts share a UUID, also across ports. The genlock ports have none.
DT_TEST(EverySdiPortHasTheReceiverFunction)
{
    int Uuids[SIM_SDI_PORT_COUNT * FUNCTION_COUNT];
    int Count = 0;
    int Port, i, j;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    for (Port = 0; Port < SIM_SDI_PORT_COUNT; Port++)
    {
        char Str[DT_PROPERTY_STR_SIZE];
        char Key[PROPERTY_NAME_MAX_SIZE];

        DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, "AF_ASISDIRX#1", Port, Str, sizeof(Str)));
        DT_ASSERT_STR(Str, "");

        for (i = 0; i < FUNCTION_COUNT; i++)
        {
            const Function* F = &g_Functions[i];
            int Type = -1;
            int Uuid;

            snprintf(Key, sizeof(Key), "AF_ASISDIRX#1.%d", i + 1);
            DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, Key, Port, Str, sizeof(Str)));
            DT_ASSERT_STR(Str, F->Name);
            DT_ASSERT_OK(DtDrvGetPropertyStr(Drv, F->Name, Port, Str, sizeof(Str)));
            DT_ASSERT_STR(Str, F->Role);

            snprintf(Key, sizeof(Key), "%s_TYPE", F->Name);
            DT_ASSERT_OK(DtDrvGetPropertyInt(Drv, Key, Port, &Type));
            DT_ASSERT_EQ(Type, F->Type);

            Uuid = UuidOf(Drv, F->Name, Port);
            DT_ASSERT_EQ(Uuid & DT_UUID_FLAG_MASK,
                         F->IsDf ? DT_UUID_DF_FLAG : DT_UUID_BC_FLAG);
            DT_ASSERT((Uuid & DT_UUID_INDEX_MASK) != 0);
            for (j = 0; j < Count; j++)
                DT_ASSERT(Uuids[j] != Uuid);
            Uuids[Count++] = Uuid;
        }

        snprintf(Key, sizeof(Key), "AF_ASISDIRX#1.%d", FUNCTION_COUNT + 1);
        DT_ASSERT_EQ(DtDrvGetPropertyStr(Drv, Key, Port, Str, sizeof(Str)),
                     DTAPI_E_NOT_FOUND);
    }

    for (Port = SIM_SDI_PORT_COUNT; Port < SIM_PORT_COUNT; Port++)
        DT_ASSERT_EQ(UuidOf(Drv, "DF_SDIRX#1", Port), -1);

    OsDrvClose(Drv);
}

// Only the plain decimal numbers of the parts name a part.
DT_TEST(PartNumbersAreDecimal)
{
    static const char* const NoPart[] = {
        "AF_ASISDIRX#1.0", "AF_ASISDIRX#1.01", "AF_ASISDIRX#1.1x",
        "AF_ASISDIRX#1.",  "AF_ASISDIRX#1.-1", "AF_ASISDIRX#1.99999999999",
        "AF_ASISDIRX#10",  "AF_ASISDIRX#1x",   "DF_SDIRX#1_UUIDX",
        "DF_SDIRX#",
    };
    char Str[DT_PROPERTY_STR_SIZE];
    size_t i;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    for (i = 0; i < sizeof(NoPart) / sizeof(NoPart[0]); i++)
    {
        if (DtDrvGetPropertyStr(Drv, NoPart[i], 0, Str, sizeof(Str)) != DTAPI_E_NOT_FOUND)
            DT_FAIL("\"%s\" was found", NoPart[i]);
    }

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Routing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The device itself is UUID 0 and only with port index -1.
DT_TEST(DeviceCommandsNeedTheDevicePortIndex)
{
    DtIoctlGetDriverVersionInput In;
    DtIoctlGetDriverVersionOutput Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    In.m_PortIndex = 0;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                            sizeof(In), &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    In.m_PortIndex = -1;
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                            sizeof(In), &Out, &OutSize, &Status),
                 OS_IOCTL_OK);

    OsDrvClose(Drv);
}

// A UUID the card does not have, or one that flags neither a building block nor a driver
// function, has no I/O stub. Building blocks and driver functions are numbered apart, so
// a function's index with the other flag is not checked: a card may have that one.
DT_TEST(UnknownUuidHasNoIoStub)
{
    uint32_t Status = 0;
    int Uuid;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Uuid = UuidOf(Drv, "DF_SDIRX#1", 0);
    DT_ASSERT(Uuid > 0);

    DT_ASSERT_EQ(RawSdiRx(Drv, DT_UUID_DF_FLAG | 0xFFFF, 0, DT_SDIRX_CMD_GET_SDI_STATUS2,
                          sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NO_IOSTUB);

    // The index of a driver function, without its flag or with another.
    DT_ASSERT_EQ(RawSdiRx(Drv, Uuid & DT_UUID_INDEX_MASK, 0, DT_SDIRX_CMD_GET_SDI_STATUS2,
                          sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NO_IOSTUB);
    DT_ASSERT_EQ(RawSdiRx(Drv, (Uuid & DT_UUID_INDEX_MASK) | DT_UUID_AF_FLAG, 0,
                          DT_SDIRX_CMD_GET_SDI_STATUS2, sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NO_IOSTUB);

    {
        DtSdiRxStatus S;

        DT_ASSERT_EQ(DtDrvSdiRxGetStatus(Drv, 12345, 0, &S), DTAPI_E_NOT_IMPLEMENTED);
    }

    OsDrvClose(Drv);
}

// The SDI receiver command is for the SDI receiver: the device, another driver function
// and a building block refuse it as unknown.
DT_TEST(OnlyTheReceiverTakesItsCommand)
{
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(RawSdiRx(Drv, 0, -1, DT_SDIRX_CMD_GET_SDI_STATUS2,
                          sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    DT_ASSERT_EQ(RawSdiRx(Drv, UuidOf(Drv, "DF_ASIRX#1", 0), 0,
                          DT_SDIRX_CMD_GET_SDI_STATUS2, sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    DT_ASSERT_EQ(RawSdiRx(Drv, UuidOf(Drv, "BC_SDIRXF#1", 0), 0,
                          DT_SDIRX_CMD_GET_SDI_STATUS2, sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    // And the receiver refuses another IOCTL, even one whose command number is that of
    // the status and whose output would hold it.
    {
        DtIoctlInputDataHdr In;
        DtIoctlSdiRxCmdGetSdiStatusOutput2 Out;
        size_t OutSize = sizeof(Out);

        memset(&In, 0, sizeof(In));
        In.m_Uuid = UuidOf(Drv, "DF_SDIRX#1", 0);
        In.m_PortIndex = 0;
        In.m_Cmd = DT_SDIRX_CMD_GET_SDI_STATUS2;
        DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                                sizeof(In), &Out, &OutSize, &Status),
                     OS_IOCTL_DRIVER_STATUS);
        DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);
    }

    OsDrvClose(Drv);
}

// The UUID alone picks the function; the port index in the header does not.
DT_TEST(UuidAlonePicksTheFunction)
{
    SimSdiSignal Signal = Signal1080i50();
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieSetSdiSignal(2, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 2), 4, &S));
    DT_ASSERT(S.CarrierDetect);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 4), 2, &S));
    DT_ASSERT(!S.CarrierDetect);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SDI receiver +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The request is a header for the function and port.
DT_TEST(StatusRequestIsAHeaderForTheFunction)
{
    DtIoctlInputDataHdr Hdr;
    DtSdiRxStatus S;
    int FunctionCode;
    int Uuid;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Uuid = UuidOf(Drv, "DF_SDIRX#1", 6);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, Uuid, 6, &S));
    DT_ASSERT_EQ(SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr)), sizeof(Hdr));
    DT_ASSERT_EQ(FunctionCode, DT_FUNC_CODE_SDIRX_CMD);
    DT_ASSERT_EQ(Hdr.m_Uuid, Uuid);
    DT_ASSERT_EQ(Hdr.m_PortIndex, 6);
    DT_ASSERT_EQ(Hdr.m_Cmd, DT_SDIRX_CMD_GET_SDI_STATUS2);
    DT_ASSERT_EQ(Hdr.m_CmdEx, DT_IOCTL_CMD_NOP);

    OsDrvClose(Drv);
}

// An input without a signal reports nothing, with the rate unknown.
DT_TEST(InputWithoutSignalReportsNothing)
{
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&S, 0x5A, sizeof(S));
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(!S.CarrierDetect);
    DT_ASSERT(!S.SdiLock);
    DT_ASSERT(!S.LineLock);
    DT_ASSERT(!S.Valid);
    DT_ASSERT_EQ(S.NumSymsHanc, 0);
    DT_ASSERT_EQ(S.NumSymsVidVanc, 0);
    DT_ASSERT_EQ(S.NumLinesF1, 0);
    DT_ASSERT_EQ(S.NumLinesF2, 0);
    DT_ASSERT(!S.IsLevelB);
    DT_ASSERT_EQ(S.PayloadId, 0);
    DT_ASSERT(S.FrameRate == 0.0);
    DT_ASSERT_EQ(S.SdiRate, -1);

    OsDrvClose(Drv);
}

// Every field of the signal comes through, the frame period as a rate.
DT_TEST(StatusFollowsTheSignal)
{
    SimSdiSignal Signal = Signal1080i50();
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieSetSdiSignal(0, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(S.CarrierDetect);
    DT_ASSERT(S.SdiLock);
    DT_ASSERT(S.LineLock);
    DT_ASSERT(S.Valid);
    DT_ASSERT_EQ(S.NumSymsHanc, 1440);
    DT_ASSERT_EQ(S.NumSymsVidVanc, 3840);
    DT_ASSERT_EQ(S.NumLinesF1, 563);
    DT_ASSERT_EQ(S.NumLinesF2, 562);
    DT_ASSERT(!S.IsLevelB);
    DT_ASSERT_EQ(S.PayloadId, 0x00800585);
    DT_ASSERT(S.FrameRate == 25.0);
    DT_ASSERT_EQ(S.SdiRate, 1);

    // Each flag on its own, so that no field is read from another.
    memset(&Signal, 0, sizeof(Signal));
    Signal.SdiRate = DT_DRV_SDIRATE_3G;
    Signal.LineLock = 1;
    Signal.IsLevelB = 1;
    Signal.NumLinesF2 = 7;
    SimDtPcieSetSdiSignal(0, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(!S.CarrierDetect);
    DT_ASSERT(!S.SdiLock);
    DT_ASSERT(S.LineLock);
    DT_ASSERT(!S.Valid);
    DT_ASSERT(S.IsLevelB);
    DT_ASSERT_EQ(S.NumLinesF1, 0);
    DT_ASSERT_EQ(S.NumLinesF2, 7);
    DT_ASSERT_EQ(S.SdiRate, 2);

    memset(&Signal, 0, sizeof(Signal));
    Signal.Valid = 1;
    Signal.CarrierDetect = 1;
    SimDtPcieSetSdiSignal(0, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(S.CarrierDetect);
    DT_ASSERT(!S.SdiLock);
    DT_ASSERT(!S.LineLock);
    DT_ASSERT(S.Valid);
    DT_ASSERT(!S.IsLevelB);

    OsDrvClose(Drv);
}

// A flag is true for any non-zero value the driver writes.
DT_TEST(AnyNonZeroFlagIsTrue)
{
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);
    SimSdiSignal Signal;

    if (Drv == NULL)
        return;

    memset(&Signal, 0, sizeof(Signal));
    Signal.CarrierDetect = 2;
    Signal.SdiLock = -1;
    Signal.LineLock = 0x100;
    Signal.Valid = 3;
    Signal.IsLevelB = 4;
    SimDtPcieSetSdiSignal(0, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(S.CarrierDetect);
    DT_ASSERT(S.SdiLock);
    DT_ASSERT(S.LineLock);
    DT_ASSERT(S.Valid);
    DT_ASSERT(S.IsLevelB);

    OsDrvClose(Drv);
}

// The frame period in nanoseconds becomes frames per second; none or a negative one gives
// no rate.
DT_TEST(FramePeriodBecomesARate)
{
    static const struct
    {
        int Period;
        double Rate;
    } Cases[] = {{40000000, 25.0},
                 {16683333, 1e9 / 16683333},
                 {1, 1e9},
                 {0, 0.0},
                 {-40000000, 0.0}};
    SimSdiSignal Signal;
    DtSdiRxStatus S;
    size_t i;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&Signal, 0, sizeof(Signal));
    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        Signal.FramePeriod = Cases[i].Period;
        SimDtPcieSetSdiSignal(0, &Signal);
        DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
        if (S.FrameRate != Cases[i].Rate)
            DT_FAIL("period %d: rate %f", Cases[i].Period, S.FrameRate);
    }

    OsDrvClose(Drv);
}

// The driver's rates come through as they are, and anything else is unknown.
DT_TEST(RateOutsideTheDriverValuesIsUnknown)
{
    SimSdiSignal Signal;
    DtSdiRxStatus S;
    int Rate;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&Signal, 0, sizeof(Signal));
    for (Rate = -3; Rate <= 7; Rate++)
    {
        int Expected =
            Rate >= DT_DRV_SDIRATE_SD && Rate <= DT_DRV_SDIRATE_12G ? Rate : -1;

        Signal.SdiRate = Rate;
        SimDtPcieSetSdiSignal(0, &Signal);
        DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
        if (S.SdiRate != Expected)
            DT_FAIL("driver rate %d gave %d", Rate, S.SdiRate);
    }

    OsDrvClose(Drv);
}

// The receiver of a port that is not an input is not enabled, as on a card, whose driver
// refuses the status with DT_STATUS_NOT_ENABLED.
DT_TEST(OutputPortReceiverIsNotEnabled)
{
    SimSdiSignal Signal = Signal1080i50();
    DtIoConfig Cfg;
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieSetSdiSignal(1, &Signal);
    memset(&S, 0x5A, sizeof(S));
    DT_ASSERT_EQ(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 1), 1, &S),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT(!S.CarrierDetect);
    DT_ASSERT_EQ(S.NumLinesF1, 0);

    // Once the port is an input, the signal is seen.
    Cfg.Port = 2;
    Cfg.Group = DTAPI_IOCONFIG_IODIR;
    Cfg.Value = DTAPI_IOCONFIG_INPUT;
    Cfg.SubValue = DTAPI_IOCONFIG_INPUT;
    Cfg.ParXtra[0] = -1;
    Cfg.ParXtra[1] = -1;
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Cfg));
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 1), 1, &S));
    DT_ASSERT_EQ(S.NumLinesF1, 563);

    OsDrvClose(Drv);
}

// In ASI mode only the carrier is reported.
DT_TEST(AsiReportsTheCarrierOnly)
{
    SimSdiSignal Signal = Signal1080i50();
    DtIoConfig Cfg;
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Cfg.Port = 1;
    Cfg.Group = DTAPI_IOCONFIG_IOSTD;
    Cfg.Value = DTAPI_IOCONFIG_ASI;
    Cfg.SubValue = -1;
    Cfg.ParXtra[0] = -1;
    Cfg.ParXtra[1] = -1;
    DT_ASSERT_OK(DtDrvSetIoConfig(Drv, &Cfg));

    SimDtPcieSetSdiSignal(0, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(S.CarrierDetect);
    DT_ASSERT(!S.SdiLock);
    DT_ASSERT(!S.LineLock);
    DT_ASSERT(!S.Valid);
    DT_ASSERT_EQ(S.NumLinesF1, 0);
    DT_ASSERT_EQ(S.NumSymsHanc, 0);
    DT_ASSERT_EQ(S.PayloadId, 0);
    DT_ASSERT(S.FrameRate == 0.0);
    DT_ASSERT_EQ(S.SdiRate, -1);

    OsDrvClose(Drv);
}

// The sizes are checked before whether the receiver is enabled, and a command the
// emulator does not model is refused before its sizes are looked at.
DT_TEST(StatusRequestSizesAreChecked)
{
    uint32_t Status = 0;
    int Uuid;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Uuid = UuidOf(Drv, "DF_SDIRX#1", 1);
    DT_ASSERT_EQ(RawSdiRx(Drv, Uuid, 1, DT_SDIRX_CMD_GET_SDI_STATUS2,
                          sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2) - 1, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    DT_ASSERT_EQ(RawSdiRx(Drv, Uuid, 1, DT_SDIRX_CMD_GET_SDI_STATUS2,
                          sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_ENABLED);

    DT_ASSERT_EQ(RawSdiRx(Drv, Uuid, 1, DT_SDIRX_CMD_GET_OPERATIONAL_MODE,
                          sizeof(DtIoctlInputDataHdr), 0, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    // An input with room for the answer.
    DT_ASSERT_EQ(RawSdiRx(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0,
                          DT_SDIRX_CMD_GET_SDI_STATUS2, sizeof(DtIoctlInputDataHdr),
                          sizeof(DtIoctlSdiRxCmdGetSdiStatusOutput2), &Status),
                 OS_IOCTL_OK);

    OsDrvClose(Drv);
}

DT_TEST(StatusFailuresBecomeResults)
{
    SimSdiSignal Signal = Signal1080i50();
    DtSdiRxStatus S;
    int Uuid;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    Uuid = UuidOf(Drv, "DF_SDIRX#1", 0);
    SimDtPcieSetSdiSignal(0, &Signal);

    SimDtPcieFailWithStatus(DT_FUNC_CODE_SDIRX_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDrvSdiRxGetStatus(Drv, Uuid, 0, &S), DTAPI_E_TIMEOUT);

    SimDtPcieReset();
    SimDtPcieSetSdiSignal(0, &Signal);
    SimDtPcieAnswerShort(DT_FUNC_CODE_SDIRX_CMD);
    DT_ASSERT_EQ(DtDrvSdiRxGetStatus(Drv, Uuid, 0, &S), DTAPI_E_DEV_DRIVER);
    DT_ASSERT(!S.CarrierDetect);

    DT_ASSERT_EQ(DtDrvSdiRxGetStatus(NULL, Uuid, 0, &S), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvSdiRxGetStatus(Drv, Uuid, 0, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// A reset takes the signals away; a port outside the SDI ports takes none.
DT_TEST(SignalsAreResetAndPerSdiPort)
{
    SimSdiSignal Signal = Signal1080i50();
    DtSdiRxStatus S;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    SimDtPcieSetSdiSignal(0, &Signal);
    SimDtPcieReset();
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(!S.CarrierDetect);
    DT_ASSERT_EQ(S.SdiRate, -1);

    // Nothing lands on a port of the card.
    SimDtPcieSetSdiSignal(-1, &Signal);
    SimDtPcieSetSdiSignal(SIM_SDI_PORT_COUNT, &Signal);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(!S.CarrierDetect);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 6), 6, &S));
    DT_ASSERT(!S.CarrierDetect);

    SimDtPcieSetSdiSignal(0, &Signal);
    SimDtPcieSetSdiSignal(0, NULL);
    DT_ASSERT_OK(DtDrvSdiRxGetStatus(Drv, UuidOf(Drv, "DF_SDIRX#1", 0), 0, &S));
    DT_ASSERT(!S.CarrierDetect);
    DT_ASSERT_EQ(S.SdiRate, -1);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A version is compared number by number, the build number included.
DT_TEST(VersionsCompareNumberByNumber)
{
    static const struct
    {
        DtDriverVersion Version;
        bool Supported;
    } Cases[] = {
        {{1, 4, 0, 111}, true},   {{1, 4, 0, 110}, false},   {{1, 4, 1, 0}, true},
        {{1, 5, 0, 0}, true},     {{2, 0, 0, 0}, true},      {{3, 6, 4, 398}, true},
        {{1, 3, 99, 999}, false}, {{0, 99, 99, 999}, false}, {{1, 4, 0, 0}, false},
    };
    size_t i;

    for (i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        const DtDriverVersion* V = &Cases[i].Version;

        if (DtDrvVersionAtLeast(V, 1, 4, 0, 111) != Cases[i].Supported)
            DT_FAIL("%d.%d.%d.%d", V->Major, V->Minor, V->Micro, V->Build);
    }
}

DT_TEST_MAIN("SimSdiRx", DT_RUN(StringPropertiesAreRead),
             DT_RUN(StringRequestCarriesTheFilter), DT_RUN(MissingStringIsNotFound),
             DT_RUN(StringMustFitTheBuffer), DT_RUN(StringNameMustFit),
             DT_RUN(StringNullArgumentsAreRefused), DT_RUN(StringFailuresBecomeResults),
             DT_RUN(StringOverridesAreSeparate),
             DT_RUN(EverySdiPortHasTheReceiverFunction), DT_RUN(PartNumbersAreDecimal),
             DT_RUN(DeviceCommandsNeedTheDevicePortIndex), DT_RUN(UnknownUuidHasNoIoStub),
             DT_RUN(OnlyTheReceiverTakesItsCommand), DT_RUN(UuidAlonePicksTheFunction),
             DT_RUN(StatusRequestIsAHeaderForTheFunction),
             DT_RUN(InputWithoutSignalReportsNothing), DT_RUN(StatusFollowsTheSignal),
             DT_RUN(AnyNonZeroFlagIsTrue), DT_RUN(FramePeriodBecomesARate),
             DT_RUN(RateOutsideTheDriverValuesIsUnknown),
             DT_RUN(OutputPortReceiverIsNotEnabled), DT_RUN(AsiReportsTheCarrierOnly),
             DT_RUN(StatusRequestSizesAreChecked), DT_RUN(StatusFailuresBecomeResults),
             DT_RUN(SignalsAreResetAndPerSdiPort), DT_RUN(VersionsCompareNumberByNumber))

// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimDetect.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Detecting and waiting for a video standard against the emulator
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case starts from the emulator's power-on
// state, and ends with no handle to it and no allocation left open. The signals fed to
// the emulator are computed from the line timing in Unit/SdiFormats.inc, not from the
// library's tables.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"             // Public API under test.
#include "Core/DtAlloc.h"           // Live allocations.
#include "DtDrvAbi.h"               // Driver statuses, function codes and types.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // Checking that the device is emulated.
#include "OAL/OsThread.h"           // Timing the waits.
#include "OAL/Sim/SimDtPcie.h"      // The emulated card and its test controls.
#include "Unit/SdiFormat.h"         // Line timing of every standard.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The ports of the emulated card: odd ports start as inputs and even ones as outputs;
// port 9 is the genlock input, which is no input for signals.
#define PORT_INPUT 1
#define PORT_OUTPUT 2
#define PORT_GENLOCK 9

// Resets the emulator, checks that it is what the process talks to, and notes the live
// allocations. Returns false, having recorded a failure, when it is not the emulator.
static bool StartSim(int* DtFailures, int* Live)
{
    OsDrv* Drv;

    SimDtPcieReset();
    Drv = OsDrvOpen(SIM_DEVICE_INDEX);
    if (Drv == NULL || !OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: no emulated device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        OsDrvClose(Drv);
        return false;
    }
    OsDrvClose(Drv);
    *Live = DtAllocLive();
    return true;
}

// Attaches a device object to the emulated card, whose properties a case may have
// overridden first. Returns NULL, having recorded a failure, when it cannot.
static DtDevice* Attach(int* DtFailures)
{
    DtDevice* Device = DtDevice_Alloc();

    if (Device == NULL || DtDevice_AttachToSerial(Device, SIM_SERIAL) != DTAPI_OK)
    {
        printf("    FAIL: cannot attach to the emulated device\n");
        (*DtFailures)++;
        DtDevice_Free(Device);
        return NULL;
    }
    return Device;
}

// Frees the device object and checks that nothing is left open or allocated.
#define FINISH(Device, Live)                                                             \
    do                                                                                   \
    {                                                                                    \
        DtDevice_Free(Device);                                                           \
        DT_ASSERT_EQ(SimDtPcieOpenHandles(), 0);                                         \
        DT_ASSERT_EQ(DtAllocLive(), Live);                                               \
    } while (0)

// The signal an SDI receiver reports for a format: locked, with the line timing's
// counters, the format's level and SDI rate, the frame period in nanoseconds, and the
// format's VPID or none.
static SimSdiSignal SignalOf(const SdiFormat* Format, bool WithVpid)
{
    SimSdiSignal Signal;

    memset(&Signal, 0, sizeof(Signal));
    Signal.CarrierDetect = 1;
    Signal.SdiLock = 1;
    Signal.LineLock = 1;
    Signal.Valid = 1;
    Signal.NumSymsHanc = SdiFormatHancSymbols(Format);
    Signal.NumSymsVidVanc = SdiFormatVancSymbols(Format);
    Signal.NumLinesF1 = Format->LinesF1;
    Signal.NumLinesF2 = SdiFormatLinesF2(Format);
    Signal.IsLevelB = SdiFormatIsLevelB(Format) ? 1 : 0;
    Signal.PayloadId = WithVpid ? SdiFormatVpid(Format) : 0;
    Signal.FramePeriod = (int)(1e9 * Format->FpsDen / Format->FpsNum + 0.5);
    Signal.SdiRate = Format->SdiRate;
    return Signal;
}

// The format of a standard.
static const SdiFormat* FormatOf(int VidStd)
{
    int i;

    for (i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        if (g_SdiFormats[i].VidStd == VidStd)
            return &g_SdiFormats[i];
    }
    return NULL;
}

// The link standard SMPTE 352 names for a 2160p payload identifier: six and twelve
// gigabit on one link, and four links of either level.
static int LinkOfPayload(int Payload)
{
    switch (Payload)
    {
    case 0xC0:
        return 2;
    case 0xCE:
        return 3;
    case 0x97:
    case 0x98:
        return 0;
    default:
        return -1;
    }
}

// The 1080p standard of the format one link of a 2160p format carries, found from the
// line timing: 1125 progressive lines, not 2160p, the same rate and level.
static int OneLinkOf(const SdiFormat* Uhd)
{
    int i;

    for (i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];

        if (F->Lines == 1125 && F->Scan == SDI_SCAN_P &&
            (F->Payload == 0x85 || F->Payload == 0x89 || F->Payload == 0x8A) &&
            F->FpsNum == Uhd->FpsNum && F->FpsDen == Uhd->FpsDen &&
            SdiFormatIsLevelB(F) == SdiFormatIsLevelB(Uhd))
        {
            return F->VidStd;
        }
    }
    return DTAPI_VIDSTD_UNKNOWN;
}

// Checks that every field of Info is unknown.
static void CheckUnknown(int* DtFailures, const DtDetVidStd* Info)
{
    DT_ASSERT_EQ(Info->VidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(Info->LinkStd, -1);
    DT_ASSERT_EQ(Info->LinkNr, -1);
    DT_ASSERT_EQ(Info->Vpid, 0);
    DT_ASSERT_EQ(Info->Vpid2, 0);
    DT_ASSERT_EQ(Info->AspectRatio, DT_AR_UNKNOWN);
    DT_ASSERT_EQ(Info->OriginalVidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(Info->OriginalLinkStd, -1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Attach +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(NullArgumentsAreRefused)
{
    DtDetVidStd Info;
    int VidStd = 12345;
    int Live;
    DtDevice* Device;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    DT_ASSERT_EQ(DtDevice_DetectVidStd(NULL, PORT_INPUT, &VidStd), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(VidStd, 12345);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, NULL), DTAPI_E_INVALID_ARG);

    memset(&Info, 0x5A, sizeof(Info));
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(NULL, PORT_INPUT, 0, &Info),
                 DTAPI_E_INVALID_ARG);
    CheckUnknown(DtFailures, &Info);
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 0, NULL),
                 DTAPI_E_INVALID_ARG);

    Info = DtDevice_WaitForSignal(NULL, PORT_INPUT);
    CheckUnknown(DtFailures, &Info);

    FINISH(Device, Live);
}

// A device object that is not attached is no device to detect on, rather than a device
// that is not attached, as DtAvInputStatus::AttachToPort has it.
DT_TEST(DetachedDeviceIsNoDevice)
{
    DtDetVidStd Info;
    int VidStd = 12345;
    int Live;
    DtDevice* Device;

    if (!StartSim(DtFailures, &Live))
        return;

    Device = DtDevice_Alloc();
    DT_ASSERT(Device != NULL);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd), DTAPI_E_DEVICE);
    DT_ASSERT_EQ(VidStd, 12345);
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, -1, &Info),
                 DTAPI_E_DEVICE);
    CheckUnknown(DtFailures, &Info);
    Info = DtDevice_WaitForSignal(Device, PORT_INPUT);
    CheckUnknown(DtFailures, &Info);

    FINISH(Device, Live);
}

// The firmware status is checked before the port number.
DT_TEST(FirmwareStatusComesFirst)
{
    int VidStd = 12345;
    int Live;
    DtDevice* Device;

    if (!StartSim(DtFailures, &Live))
        return;

    SimDtPcieSetFirmwareStatus(DT_FWSTATUS_OBSOLETE);
    Device = DtDevice_Alloc();
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK_OBSOLETE_FW);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, 99, &VidStd), DTAPI_E_OBSOLETE_FW);
    DtDevice_Free(Device);

    SimDtPcieSetFirmwareStatus(DT_FWSTATUS_TAINTED);
    Device = DtDevice_Alloc();
    DT_ASSERT_EQ(DtDevice_AttachToSerial(Device, SIM_SERIAL), DTAPI_OK_TAINTED_FW);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, 99, &VidStd), DTAPI_E_TAINTED_FW);
    DT_ASSERT_EQ(VidStd, 12345);

    FINISH(Device, Live);
}

// A port is looked for among all ports of the card, not only the public ones: with
// fewer public ports, the ports beyond them are refused for their capabilities.
DT_TEST(PortsAreAllPortsOfTheCard)
{
    int VidStd = 12345;
    int Live;
    DtDevice* Device;

    if (!StartSim(DtFailures, &Live))
        return;

    // Port 9 made an input the Matrix API can use: past the capabilities, it has no
    // receiver function.
    SimDtPcieOverrideProperty("MAIN_PORT_COUNT", -1, true, 8);
    SimDtPcieOverrideProperty("CAP_INPUT", PORT_GENLOCK - 1, true, 1);
    SimDtPcieOverrideProperty("CAP_MATRIX2", PORT_GENLOCK - 1, true, 1);
    if ((Device = Attach(DtFailures)) == NULL)
        return;

    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, 0, &VidStd), DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, SIM_PORT_COUNT + 1, &VidStd),
                 DTAPI_E_NO_SUCH_PORT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_GENLOCK, &VidStd), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, SIM_PORT_COUNT, &VidStd),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(VidStd, 12345);

    FINISH(Device, Live);
}

// Refuses a port by its capabilities before any of its functions is looked up.
static void CheckRefusedByCaps(int* DtFailures, const char* Cap1, bool Has1,
                               const char* Cap2, bool Has2, unsigned int Expected)
{
    DtIoctlInputDataHdr Hdr;
    int FunctionCode;
    int VidStd = 12345;
    int Live;
    DtDevice* Device;

    if (!StartSim(DtFailures, &Live))
        return;

    SimDtPcieOverrideProperty(Cap1, PORT_INPUT - 1, true, Has1 ? 1 : 0);
    if (Cap2 != NULL)
        SimDtPcieOverrideProperty(Cap2, PORT_INPUT - 1, true, Has2 ? 1 : 0);
    if ((Device = Attach(DtFailures)) == NULL)
        return;

    if (DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd) != Expected)
        DT_FAIL("%s=%d %s=%d: not refused", Cap1, Has1, Cap2 ? Cap2 : "", Has2);

    // The last request is still one of attaching's capability reads.
    SimDtPcieLastInput(&FunctionCode, &Hdr, sizeof(Hdr));
    if (Expected != DTAPI_OK &&
        (FunctionCode != DT_FUNC_CODE_PROPERTY_CMD || Hdr.m_Cmd != DT_PROP_CMD_GET_VALUE))
    {
        DT_FAIL("%s=%d: a function was looked up", Cap1, Has1);
    }

    FINISH(Device, Live);
}

// An input or internal input, with the Matrix API; SDI receiver or HDMI alone do not
// do, because a DtPcie card goes through the Matrix API.
DT_TEST(PortNeedsAnInputWithTheMatrixApi)
{
    CheckRefusedByCaps(DtFailures, "CAP_INPUT", false, NULL, false,
                       DTAPI_E_NOT_SUPPORTED);
    CheckRefusedByCaps(DtFailures, "CAP_INPUT", false, "CAP_INTINPUT", true, DTAPI_OK);
    CheckRefusedByCaps(DtFailures, "CAP_MATRIX2", false, NULL, false,
                       DTAPI_E_NOT_SUPPORTED);
    CheckRefusedByCaps(DtFailures, "CAP_MATRIX2", false, "CAP_SDIRX", true,
                       DTAPI_E_NOT_SUPPORTED);
    CheckRefusedByCaps(DtFailures, "CAP_MATRIX2", false, "CAP_HDMI", true,
                       DTAPI_E_NOT_SUPPORTED);
    CheckRefusedByCaps(DtFailures, "CAP_SDIRX", true, "CAP_HDMI", true, DTAPI_OK);
}

// An internal input is no input: the scan does not describe it as one.
DT_TEST(InternalInputIsNoInputToTheScan)
{
    DtHwFuncDesc Funcs[SIM_PORT_COUNT];
    int Found = 0;
    int Live;

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideProperty("CAP_INPUT", PORT_INPUT - 1, true, 0);
    SimDtPcieOverrideProperty("CAP_INTINPUT", PORT_INPUT - 1, true, 1);

    DT_ASSERT_OK(DtapiHwFuncScan(SIM_PORT_COUNT, &Found, Funcs));
    DT_ASSERT_EQ(Found, SIM_PORT_COUNT);
    DT_ASSERT(!Funcs[PORT_INPUT - 1].IsInput);
    DT_ASSERT(Funcs[PORT_INPUT - 1].IsOutput);
    DT_ASSERT_EQ(DtAllocLive(), Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Discovery +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Detects on the input port after the case's overrides, and returns the result, with
// *VidStd the standard found.
static unsigned int DetectWith(int* DtFailures, DtDevice** Device, int* VidStd)
{
    const SdiFormat* Format = FormatOf(DTAPI_VIDSTD_1080I59_94);
    SimSdiSignal Signal = SignalOf(Format, true);

    *VidStd = 12345;
    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    *Device = Attach(DtFailures);
    if (*Device == NULL)
        return DTAPI_E_INTERNAL;
    return DtDevice_DetectVidStd(*Device, PORT_INPUT, VidStd);
}

// The first instance with the empty role is used; one with another role is passed over.
DT_TEST(FirstInstanceWithTheEmptyRole)
{
    DtDevice* Device;
    int VidStd;
    int Live;

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideString("AF_ASISDIRX#1", 0, true, "OTHER");
    SimDtPcieOverrideString("AF_ASISDIRX#1.6", 0, false, NULL);
    SimDtPcieOverrideString("AF_ASISDIRX#2", 0, true, "");
    SimDtPcieOverrideString("AF_ASISDIRX#2.1", 0, true, "DF_SDIRX#1");
    DT_ASSERT_EQ(DetectWith(DtFailures, &Device, &VidStd), DTAPI_OK);
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_1080I59_94);
    FINISH(Device, Live);

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideString("AF_ASISDIRX#1", 0, true, "OTHER");
    DT_ASSERT_EQ(DetectWith(DtFailures, &Device, &VidStd), DTAPI_E_NOT_FOUND);
    DT_ASSERT_EQ(VidStd, 12345);
    FINISH(Device, Live);

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideString("AF_ASISDIRX#1", 0, false, NULL);
    DT_ASSERT_EQ(DetectWith(DtFailures, &Device, &VidStd), DTAPI_E_NOT_FOUND);
    FINISH(Device, Live);
}

// A failure to read the function other than not finding it is returned.
DT_TEST(ReadFailureIsReturned)
{
    DtDevice* Device;
    int VidStd = 12345;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    SimDtPcieFailWithStatus(DT_FUNC_CODE_PROPERTY_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd), DTAPI_E_TIMEOUT);
    DT_ASSERT_EQ(VidStd, 12345);
    FINISH(Device, Live);

    // Reading an instance's role, or a part's name, fails the search.
    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;
    SimDtPcieFailProperty("AF_ASISDIRX#1", 0, true, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd), DTAPI_E_TIMEOUT);
    FINISH(Device, Live);

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;
    SimDtPcieFailProperty("AF_ASISDIRX#1.7", 0, true, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd), DTAPI_E_TIMEOUT);
    FINISH(Device, Live);

    // Reading a part's role or type only skips the part.
    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;
    SimDtPcieFailProperty("BC_SWITCH#2", 0, true, DT_STATUS_TIMEOUT);
    SimDtPcieFailProperty("DF_ASIRX#1_TYPE", 0, false, DT_STATUS_TIMEOUT);
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));
    FINISH(Device, Live);
}

// Parts end at the first one not found; without the receiver among them, it is not
// found at all.
DT_TEST(PartsEndAtTheFirstMissingOne)
{
    DtDevice* Device;
    int VidStd;
    int Live;

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideString("AF_ASISDIRX#1.5", 0, false, NULL);
    DT_ASSERT_EQ(DetectWith(DtFailures, &Device, &VidStd), DTAPI_E_NOT_FOUND);
    FINISH(Device, Live);

    // A part that cannot be read is skipped, not fatal.
    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideString("BC_SWITCH#2", 0, false, NULL);
    SimDtPcieOverrideProperty("BC_ST425LR#1_TYPE", 0, false, 0);
    SimDtPcieOverrideProperty("BC_SDIMUX12G#1_UUID", 0, false, 0);
    DT_ASSERT_EQ(DetectWith(DtFailures, &Device, &VidStd), DTAPI_OK);
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_1080I59_94);
    FINISH(Device, Live);
}

// The receiver is a driver function named DF_, of type SDIRX, with the empty role, and
// all three of its role, type and UUID can be read.
DT_TEST(ReceiverIsAnSdiRxDriverFunction)
{
    static const struct
    {
        const char* Name;
        bool IsString;
        bool Present;
        const char* Str;
        uint64_t Value;
    } Breaks[] = {
        {"DF_SDIRX#1", true, true, "ROLE", 0},
        {"DF_SDIRX#1", true, false, NULL, 0},
        {"DF_SDIRX#1_TYPE", false, true, NULL, DT_FUNC_TYPE_ASIRX},
        {"DF_SDIRX#1_TYPE", false, false, NULL, 0},
        {"DF_SDIRX#1_UUID", false, false, NULL, 0},
    };
    DtDevice* Device;
    int VidStd;
    int Live;
    size_t i;

    for (i = 0; i < sizeof(Breaks) / sizeof(Breaks[0]); i++)
    {
        if (!StartSim(DtFailures, &Live))
            return;
        if (Breaks[i].IsString)
            SimDtPcieOverrideString(Breaks[i].Name, 0, Breaks[i].Present, Breaks[i].Str);
        else
            SimDtPcieOverrideProperty(Breaks[i].Name, 0, Breaks[i].Present,
                                      Breaks[i].Value);
        if (DetectWith(DtFailures, &Device, &VidStd) != DTAPI_E_NOT_FOUND)
            DT_FAIL("%s changed: receiver still found", Breaks[i].Name);
        FINISH(Device, Live);
    }

    // The same receiver under a name without DF_.
    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideString("AF_ASISDIRX#1.6", 0, true, "XF_SDIRX#1");
    SimDtPcieOverrideString("XF_SDIRX#1", 0, true, "");
    SimDtPcieOverrideProperty("XF_SDIRX#1_TYPE", 0, true, DT_FUNC_TYPE_SDIRX);
    SimDtPcieOverrideProperty("XF_SDIRX#1_UUID", 0, true, DT_UUID_DF_FLAG | 6);
    DT_ASSERT_EQ(DetectWith(DtFailures, &Device, &VidStd), DTAPI_E_NOT_FOUND);
    FINISH(Device, Live);
}

// Of two SDI receivers the last is used, because DTAPI's proxy collection keeps the last
// proxy it adds for a type and role. The extra receiver is that of port 3, which has no
// signal: listed after the input port's own, it is used; listed before, it is not.
static unsigned int DetectWithSecondReceiver(int* DtFailures, const char* Position,
                                             DtDevice** Device, int* VidStd)
{
    SimDtPcieOverrideString(Position, 0, true, "DF_SDIRX#9");
    SimDtPcieOverrideString("DF_SDIRX#9", 0, true, "");
    SimDtPcieOverrideProperty("DF_SDIRX#9_TYPE", 0, true, DT_FUNC_TYPE_SDIRX);
    SimDtPcieOverrideProperty("DF_SDIRX#9_UUID", 0, true, DT_UUID_DF_FLAG | 22);
    return DetectWith(DtFailures, Device, VidStd);
}

DT_TEST(LastReceiverIsUsed)
{
    DtDevice* Device;
    int VidStd;
    int Live;

    if (!StartSim(DtFailures, &Live))
        return;
    DT_ASSERT_EQ(
        DetectWithSecondReceiver(DtFailures, "AF_ASISDIRX#1.7", &Device, &VidStd),
        DTAPI_OK);
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_UNKNOWN);
    FINISH(Device, Live);

    if (!StartSim(DtFailures, &Live))
        return;
    DT_ASSERT_EQ(
        DetectWithSecondReceiver(DtFailures, "AF_ASISDIRX#1.5", &Device, &VidStd),
        DTAPI_OK);
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_1080I59_94);
    FINISH(Device, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Detect +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Feeds the input port Format's signal and waits for it, without a limit on time.
static DtDetVidStd WaitFor(int* DtFailures, DtDevice* Device, const SdiFormat* Format,
                           bool WithVpid)
{
    SimSdiSignal Signal = SignalOf(Format, WithVpid);
    DtDetVidStd Info;
    unsigned int Result;

    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    Result = DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 1000, &Info);
    if (Result != DTAPI_OK && Result != DTAPI_E_TIMEOUT)
        printf("    %s: result 0x%X\n", Format->Name, Result), (*DtFailures)++;
    return Info;
}

// With its VPID every standard is detected, with its link standard, the VPID, a link
// number from 1, and the aspect ratio the VPID gives: 16:9 in HD, 4:3 in SD. Nothing is
// scaled on a port without the capability.
DT_TEST(EveryStandardWithItsVpid)
{
    DtDevice* Device;
    int Live;
    int i;

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideProperty("CAP_SCALE_12GTO3G", PORT_INPUT - 1, true, 0);
    if ((Device = Attach(DtFailures)) == NULL)
        return;

    for (i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtDetVidStd Info = WaitFor(DtFailures, Device, Format, true);
        int Link = LinkOfPayload(Format->Payload);

        SDI_ASSERT_EQ(Format, Info.VidStd, Format->VidStd);
        SDI_ASSERT_EQ(Format, Info.LinkStd, Link);
        SDI_ASSERT_EQ(Format, Info.OriginalVidStd, Format->VidStd);
        SDI_ASSERT_EQ(Format, Info.OriginalLinkStd, Link);
        SDI_ASSERT_EQ(Format, Info.Vpid, SdiFormatVpid(Format));
        SDI_ASSERT_EQ(Format, Info.Vpid2, 0);
        SDI_ASSERT_EQ(Format, Info.LinkNr, 1);
        SDI_ASSERT_EQ(Format, Info.AspectRatio,
                      Format->Lines > 625 ? DT_AR_16_9 : DT_AR_4_3);
    }

    FINISH(Device, Live);
}

// Without a VPID the counters decide: no VPID, link number or aspect ratio, and 2160p on
// the one link of its SDI rate.
DT_TEST(EveryStandardWithoutVpid)
{
    DtDevice* Device;
    int Live;
    int i;

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideProperty("CAP_SCALE_12GTO3G", PORT_INPUT - 1, true, 0);
    if ((Device = Attach(DtFailures)) == NULL)
        return;

    for (i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtDetVidStd Info = WaitFor(DtFailures, Device, Format, false);
        int Link = -1;

        if (Format->SdiRate == DT_SDIRATE_6G)
            Link = 2;
        else if (Format->SdiRate == DT_SDIRATE_12G)
            Link = 3;

        SDI_ASSERT_EQ(Format, Info.VidStd, Format->NoVpid);
        SDI_ASSERT_EQ(Format, Info.LinkStd, Link);
        SDI_ASSERT_EQ(Format, Info.OriginalVidStd, Format->NoVpid);
        SDI_ASSERT_EQ(Format, Info.OriginalLinkStd, Link);
        SDI_ASSERT_EQ(Format, Info.Vpid, 0);
        SDI_ASSERT_EQ(Format, Info.LinkNr, -1);
        SDI_ASSERT_EQ(Format, Info.AspectRatio, DT_AR_UNKNOWN);
    }

    FINISH(Device, Live);
}

// A port that scales 12G down to 3G, as the card's ports do by default, reports 4K on
// one link, 6G or 12G, as the 1080p standard that link carries; what arrived stays in
// the original fields. Four links are not scaled.
DT_TEST(ScaledPortReportsOneLink)
{
    DtDevice* Device;
    int Live;
    int i;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    for (i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtDetVidStd Info = WaitFor(DtFailures, Device, Format, true);
        int Link = LinkOfPayload(Format->Payload);
        bool Scaled = Link == 2 || Link == 3;

        SDI_ASSERT_EQ(Format, Info.VidStd, Scaled ? OneLinkOf(Format) : Format->VidStd);
        SDI_ASSERT_EQ(Format, Info.LinkStd, Scaled ? -1 : Link);
        SDI_ASSERT_EQ(Format, Info.OriginalVidStd, Format->VidStd);
        SDI_ASSERT_EQ(Format, Info.OriginalLinkStd, Link);
    }

    FINISH(Device, Live);
}

// The link number is the VPID's plus one, and the aspect ratio its byte 3 bit 7.
DT_TEST(LinkNumberAndAspectRatioFromTheVpid)
{
    const SdiFormat* Format = FormatOf(DTAPI_VIDSTD_2160P50);
    SimSdiSignal Signal;
    DtDetVidStd Info;
    DtDevice* Device;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    // The second of four level-A links at 50 frames, 4:3.
    Signal = SignalOf(Format, false);
    Signal.SdiRate = DT_DRV_SDIRATE_3G;
    Signal.PayloadId = 0x4000C997;
    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    DT_ASSERT_OK(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 0, &Info));
    DT_ASSERT_EQ(Info.VidStd, DTAPI_VIDSTD_2160P50);
    DT_ASSERT_EQ(Info.LinkStd, 0);
    DT_ASSERT_EQ(Info.LinkNr, 2);
    DT_ASSERT_EQ(Info.AspectRatio, DT_AR_4_3);
    DT_ASSERT_EQ(Info.Vpid, 0x4000C997);

    FINISH(Device, Live);
}

// A signal that is not valid or not locked, and counters of no standard, give DTAPI_OK
// with every field unknown.
DT_TEST(NoStandardIsUnknown)
{
    const SdiFormat* Format = FormatOf(DTAPI_VIDSTD_1080P50);
    SimSdiSignal Signal;
    DtDetVidStd Info;
    DtDevice* Device;
    int VidStd = 12345;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_UNKNOWN);

    Signal = SignalOf(Format, true);
    Signal.SdiLock = 0;
    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 0, &Info),
                 DTAPI_E_TIMEOUT);
    CheckUnknown(DtFailures, &Info);

    Signal = SignalOf(Format, true);
    Signal.Valid = 0;
    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    VidStd = 12345;
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_UNKNOWN);

    Signal = SignalOf(Format, false);
    Signal.NumLinesF1 = 1124;
    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    VidStd = 12345;
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_UNKNOWN);

    FINISH(Device, Live);
}

// The receiver of a port configured as an output is not enabled, and says so.
DT_TEST(OutputPortIsInTheWrongMode)
{
    SimSdiSignal Signal = SignalOf(FormatOf(DTAPI_VIDSTD_720P50), true);
    DtDevice* Device;
    int VidStd = 12345;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    SimDtPcieSetSdiSignal(PORT_OUTPUT - 1, &Signal);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_OUTPUT, &VidStd),
                 DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(VidStd, 12345);

    DT_ASSERT_OK(DtDevice_SetToInput(Device, PORT_OUTPUT));
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_OUTPUT, &VidStd));
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_720P50);

    FINISH(Device, Live);
}

// A driver older than the SDI receiver needs is found out when detecting, after the
// down-scaling configuration is read, as DTAPI does. The build number counts.
DT_TEST(OldDriverIsFoundWhenDetecting)
{
    DtDevice* Device;
    int VidStd = 12345;
    int Live;

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieSetDriverVersion(1, 4, 0, 110);
    if ((Device = Attach(DtFailures)) == NULL)
        return;
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd),
                 DTAPI_E_DRIVER_INCOMP);
    DT_ASSERT_EQ(VidStd, 12345);

    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd), DTAPI_E_TIMEOUT);
    DtDevice_Free(Device);

    SimDtPcieReset();
    SimDtPcieSetDriverVersion(1, 4, 0, 111);
    if ((Device = Attach(DtFailures)) == NULL)
        return;
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));

    FINISH(Device, Live);
}

// The down-scaling configuration is read only for a port that can scale, and a failure
// to read it is returned.
DT_TEST(DownScalingIsReadOnlyWhereItExists)
{
    DtDevice* Device;
    int VidStd = 12345;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;
    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd), DTAPI_E_TIMEOUT);
    FINISH(Device, Live);

    if (!StartSim(DtFailures, &Live))
        return;
    SimDtPcieOverrideProperty("CAP_SCALE_12GTO3G", PORT_INPUT - 1, true, 0);
    if ((Device = Attach(DtFailures)) == NULL)
        return;
    SimDtPcieFailWithStatus(DT_FUNC_CODE_IOCONFIG_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));
    FINISH(Device, Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Wait +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Waiting detects again until the signal appears, also through failures.
DT_TEST(WaitRetriesUntilTheSignalAppears)
{
    SimSdiSignal Signal = SignalOf(FormatOf(DTAPI_VIDSTD_1080P60), true);
    uint64_t Start;
    DtDetVidStd Info;
    DtDevice* Device;
    int VidStd = 12345;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    SimDtPcieSetSdiSignal(PORT_INPUT - 1, &Signal);
    // Three misses take three pauses of 5 ms, which a scheduler may stretch but not by
    // seconds.
    SimDtPcieDelaySdiSignal(PORT_INPUT - 1, 3);
    Start = OsMonotonicMs();
    DT_ASSERT_OK(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 5000, &Info));
    DT_ASSERT(OsMonotonicMs() - Start < 1000);
    DT_ASSERT_EQ(Info.VidStd, DTAPI_VIDSTD_1080P60);

    SimDtPcieDelaySdiSignal(PORT_INPUT - 1, 3);
    Info = DtDevice_WaitForSignal(Device, PORT_INPUT);
    DT_ASSERT_EQ(Info.VidStd, DTAPI_VIDSTD_1080P60);
    DT_ASSERT_EQ(Info.LinkNr, 1);

    // One try hides the signal once, and leaves it for the next.
    SimDtPcieDelaySdiSignal(PORT_INPUT - 1, 1);
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 0, &Info),
                 DTAPI_E_TIMEOUT);
    CheckUnknown(DtFailures, &Info);
    DT_ASSERT_OK(DtDevice_DetectVidStd(Device, PORT_INPUT, &VidStd));
    DT_ASSERT_EQ(VidStd, DTAPI_VIDSTD_1080P60);

    // Failures are retried until the time is up.
    SimDtPcieFailWithStatus(DT_FUNC_CODE_SDIRX_CMD, DT_STATUS_TIMEOUT);
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 30, &Info),
                 DTAPI_E_TIMEOUT);
    CheckUnknown(DtFailures, &Info);

    FINISH(Device, Live);
}

// The wait ends near its time limit.
DT_TEST(WaitEndsAtItsTimeLimit)
{
    DtDetVidStd Info;
    DtDevice* Device;
    uint64_t Start, Elapsed;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    Start = OsMonotonicMs();
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_INPUT, 80, &Info),
                 DTAPI_E_TIMEOUT);
    Elapsed = OsMonotonicMs() - Start;
    if (Elapsed < 80 - 17 || Elapsed > 80 + 150)
        DT_FAIL("an 80 ms wait took %llu ms", (unsigned long long)Elapsed);
    CheckUnknown(DtFailures, &Info);

    Start = OsMonotonicMs();
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_OUTPUT, 40, &Info),
                 DTAPI_E_TIMEOUT);
    Elapsed = OsMonotonicMs() - Start;
    if (Elapsed < 40 - 17 || Elapsed > 40 + 150)
        DT_FAIL("a 40 ms wait on an output took %llu ms", (unsigned long long)Elapsed);

    FINISH(Device, Live);
}

// A port that cannot be attached ends the wait at once, also without a time limit.
DT_TEST(WaitReturnsAtOnceForAPortItCannotAttach)
{
    DtDetVidStd Info;
    DtDevice* Device;
    int Live;

    if (!StartSim(DtFailures, &Live) || (Device = Attach(DtFailures)) == NULL)
        return;

    memset(&Info, 0x5A, sizeof(Info));
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, PORT_GENLOCK, -1, &Info),
                 DTAPI_E_NOT_SUPPORTED);
    CheckUnknown(DtFailures, &Info);
    DT_ASSERT_EQ(DtDevice_WaitForSignalTimeout(Device, 0, -1, &Info),
                 DTAPI_E_NO_SUCH_PORT);

    Info = DtDevice_WaitForSignal(Device, PORT_GENLOCK);
    CheckUnknown(DtFailures, &Info);

    FINISH(Device, Live);
}

DT_TEST_MAIN("SimDetect", DT_RUN(NullArgumentsAreRefused),
             DT_RUN(DetachedDeviceIsNoDevice), DT_RUN(FirmwareStatusComesFirst),
             DT_RUN(PortsAreAllPortsOfTheCard), DT_RUN(PortNeedsAnInputWithTheMatrixApi),
             DT_RUN(InternalInputIsNoInputToTheScan),
             DT_RUN(FirstInstanceWithTheEmptyRole), DT_RUN(ReadFailureIsReturned),
             DT_RUN(PartsEndAtTheFirstMissingOne),
             DT_RUN(ReceiverIsAnSdiRxDriverFunction), DT_RUN(LastReceiverIsUsed),
             DT_RUN(EveryStandardWithItsVpid), DT_RUN(EveryStandardWithoutVpid),
             DT_RUN(ScaledPortReportsOneLink),
             DT_RUN(LinkNumberAndAspectRatioFromTheVpid), DT_RUN(NoStandardIsUnknown),
             DT_RUN(OutputPortIsInTheWrongMode), DT_RUN(OldDriverIsFoundWhenDetecting),
             DT_RUN(DownScalingIsReadOnlyWhereItExists),
             DT_RUN(WaitRetriesUntilTheSignalAppears), DT_RUN(WaitEndsAtItsTimeLimit),
             DT_RUN(WaitReturnsAtOnceForAPortItCannotAttach))

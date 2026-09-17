// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimDevice.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The OS abstraction and driver layers against the emulated device
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPILITE_SIM=1. Every case first checks that the handle really
// is emulated, so that the suite fails instead of quietly talking to a real card when it
// is started by hand without the variable.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtPcieAbi.h"              // Raw structures for the wire-format cases.
#include "DtPcieCmd.h"              // Driver commands under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // OS abstraction under test.
#include "OAL/Sim/SimDtPcie.h"      // What the emulated card reports.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_TEST_IOCTL(Code) ((uint32_t)(Code))

// Opens the emulated device, or records why not. Returns NULL on failure, in which case
// the calling case must return.
static OsDrv* OpenSim(int* DtFailures)
{
    OsDrv* Drv = OsDrv_Open(SIM_DEVICE_INDEX);

    if (Drv == NULL)
    {
        printf("    FAIL: no device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        return NULL;
    }

    if (!OsDrv_IsEmulated(Drv))
    {
        printf("    FAIL: device 0 is real hardware; refusing to run against it\n");
        (*DtFailures)++;
        OsDrv_Close(Drv);
        return NULL;
    }

    return Drv;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(EmulatedDeviceOpens)
{
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT(OsDrv_IsEmulated(Drv));
    DT_ASSERT_EQ(OsDrv_LastError(Drv), 0);
    OsDrv_Close(Drv);
}

// The emulator replaces the hardware rather than adding to it: one device, at index 0.
DT_TEST(OnlyIndexZeroExists)
{
    DT_ASSERT(OsDrv_Open(1) == NULL);
    DT_ASSERT(OsDrv_Open(DT_MAX_DEVICES - 1) == NULL);
}

DT_TEST(OutOfRangeIndexIsRefused)
{
    DT_ASSERT(OsDrv_Open(-1) == NULL);
    DT_ASSERT(OsDrv_Open(DT_MAX_DEVICES) == NULL);
}

DT_TEST(NullHandleIsAccepted)
{
    uint32_t Status = 0xDEAD;

    OsDrv_Close(NULL);
    DT_ASSERT(!OsDrv_IsEmulated(NULL));
    DT_ASSERT_EQ(OsDrv_LastError(NULL), 0);
    uint8_t In[16];
    DT_ASSERT_EQ(OsDrv_IoCtl(NULL, 0, In, sizeof(In), NULL, NULL, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
    DT_ASSERT_EQ(OsDrv_IoCtl(NULL, 0, In, sizeof(In), NULL, NULL, NULL),
                 OS_IOCTL_COMMUNICATION);
}

// A request without input never reaches the driver, so there is no driver status.
DT_TEST(RequestWithoutInputIsRefused)
{
    uint32_t Status = 0xDEAD;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    uint8_t In[16];
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), NULL,
                             sizeof(In), NULL, NULL, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), In, 0, NULL,
                             NULL, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);

    OsDrv_Close(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(DriverVersionComesThrough)
{
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtDriverVersion Version;
    DT_ASSERT_EQ(DtPcieCmd_GetDriverVersion(Drv, &Version), DTAPI_OK);
    DT_ASSERT_EQ(Version.Major, SIM_DRIVER_MAJOR);
    DT_ASSERT_EQ(Version.Minor, SIM_DRIVER_MINOR);
    DT_ASSERT_EQ(Version.Micro, SIM_DRIVER_MICRO);
    DT_ASSERT_EQ(Version.Build, SIM_DRIVER_BUILD);

    OsDrv_Close(Drv);
}

DT_TEST(DeviceInfoComesThrough)
{
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtDeviceInfo Info;
    DT_ASSERT_EQ(DtPcieCmd_GetDeviceInfo(Drv, &Info), DTAPI_OK);
    DT_ASSERT_EQ(Info.TypeNumber, SIM_TYPE_NUMBER);
    DT_ASSERT_EQ(Info.Serial, (int64_t)SIM_SERIAL);
    DT_ASSERT_EQ(Info.HardwareRevision, SIM_HARDWARE_REVISION);
    DT_ASSERT_EQ(Info.FirmwareVersion, SIM_FIRMWARE_VERSION);
    DT_ASSERT_EQ(Info.FirmwareVariant, SIM_FIRMWARE_VARIANT);
    DT_ASSERT_EQ(Info.FirmwareStatus, DT_FWSTATUS_UPTODATE);
    DT_ASSERT_EQ(Info.VendorId, SIM_VENDOR_ID);
    DT_ASSERT_EQ(Info.DeviceId, SIM_DEVICE_ID);

    OsDrv_Close(Drv);
}

DT_TEST(CommandsRejectNullArguments)
{
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtDriverVersion Version;
    DT_ASSERT_EQ(DtPcieCmd_GetDriverVersion(NULL, &Version), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GetDriverVersion(Drv, NULL), DTAPI_E_INVALID_ARG);
    DtDeviceInfo Info;
    DT_ASSERT_EQ(DtPcieCmd_GetDeviceInfo(NULL, &Info), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtPcieCmd_GetDeviceInfo(Drv, NULL), DTAPI_E_INVALID_ARG);

    OsDrv_Close(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Wire format +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// These drive the emulator below the typed commands, with deliberately wrong sizes. The
// emulator refuses them the way the driver does, which is what makes it a test of the
// wire format rather than a stub that accepts anything.
//

DT_TEST(OutputBufferTooSmallIsRefused)
{
    uint8_t Out[sizeof(DtIoctlGetDriverVersionOutput) - 1];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtIoctlGetDriverVersionInput In;
    memset(&In, 0, sizeof(In));
    In.m_PortIndex = -1;
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                             sizeof(In), Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(OsDrv_LastError(Drv), DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(OutSize, sizeof(Out));

    OsDrv_Close(Drv);
}

DT_TEST(InputShorterThanHeaderIsRefused)
{
    DtIoctlGetDevInfoOutput Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    uint8_t In[sizeof(DtIoctlInputDataHdr) - 1];
    memset(In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DEV_INFO2), In, sizeof(In),
                             &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(OsDrv_LastError(Drv), DT_STATUS_INVALID_PARAMETER);

    OsDrv_Close(Drv);
}

// The header is checked before the command is looked at, so a short input is an invalid
// parameter even for a command the emulator does not model.
DT_TEST(ShortInputIsRefusedBeforeTheCommand)
{
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    uint8_t In[sizeof(DtIoctlInputDataHdr) - 1];
    memset(In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), In, sizeof(In), NULL,
                             NULL, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrv_Close(Drv);
}

DT_TEST(DeviceInfoOutputTooSmallIsRefused)
{
    uint8_t Out[sizeof(DtIoctlGetDevInfoOutput) - 1];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtIoctlGetDevInfoInput In;
    memset(&In, 0, sizeof(In));
    In.m_PortIndex = -1;
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DEV_INFO2), &In, sizeof(In),
                             Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrv_Close(Drv);
}

// A command the emulator does not model is refused, not answered with zeroes. Silently
// succeeding would let a missing emulation look like a working feature. The command is
// refused as unknown before its sizes are looked at, so even an empty output buffer
// yields DT_STATUS_NOT_SUPPORTED rather than DT_STATUS_INVALID_PARAMETER.
DT_TEST(UnmodelledCommandIsRefused)
{
    uint8_t Out[64];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DtIoctlInputDataHdr In;
    memset(&In, 0, sizeof(In));
    In.m_PortIndex = -1;
    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), &In, sizeof(In), Out,
                             &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);
    DT_ASSERT_EQ(OsDrv_LastError(Drv), DT_STATUS_NOT_SUPPORTED);

    DT_ASSERT_EQ(OsDrv_IoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), &In, sizeof(In),
                             NULL, NULL, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrv_Close(Drv);
}

DT_TEST_MAIN("SimDevice", DT_RUN(EmulatedDeviceOpens), DT_RUN(OnlyIndexZeroExists),
             DT_RUN(OutOfRangeIndexIsRefused), DT_RUN(NullHandleIsAccepted),
             DT_RUN(RequestWithoutInputIsRefused), DT_RUN(DriverVersionComesThrough),
             DT_RUN(DeviceInfoComesThrough), DT_RUN(CommandsRejectNullArguments),
             DT_RUN(OutputBufferTooSmallIsRefused),
             DT_RUN(InputShorterThanHeaderIsRefused),
             DT_RUN(DeviceInfoOutputTooSmallIsRefused),
             DT_RUN(ShortInputIsRefusedBeforeTheCommand),
             DT_RUN(UnmodelledCommandIsRefused))

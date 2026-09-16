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
#include "DtDrv.h"                  // Driver commands under test.
#include "DtDrvAbi.h"               // Raw structures for the wire-format cases.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // OS abstraction under test.
#include "OAL/Sim/SimDtPcie.h"      // What the emulated card reports.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DT_TEST_IOCTL(Code) ((unsigned long)(Code))

// Opens the emulated device, or records why not. Returns NULL on failure, in which case
// the calling case must return.
static OsDrv* OpenSim(int* DtFailures)
{
    OsDrv* Drv = OsDrvOpen(SIM_DEVICE_INDEX);

    if (Drv == NULL)
    {
        printf("    FAIL: no device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtFailures)++;
        return NULL;
    }

    if (!OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: device 0 is real hardware; refusing to run against it\n");
        (*DtFailures)++;
        OsDrvClose(Drv);
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

    DT_ASSERT(OsDrvIsEmulated(Drv));
    DT_ASSERT_EQ(OsDrvLastError(Drv), 0);
    OsDrvClose(Drv);
}

// The emulator replaces the hardware rather than adding to it: one device, at index 0.
DT_TEST(OnlyIndexZeroExists)
{
    DT_ASSERT(OsDrvOpen(1) == NULL);
    DT_ASSERT(OsDrvOpen(DT_MAX_DEVICES - 1) == NULL);
}

DT_TEST(OutOfRangeIndexIsRefused)
{
    DT_ASSERT(OsDrvOpen(-1) == NULL);
    DT_ASSERT(OsDrvOpen(DT_MAX_DEVICES) == NULL);
}

DT_TEST(NullHandleIsAccepted)
{
    uint8_t In[16];
    uint32_t Status = 0xDEAD;

    OsDrvClose(NULL);
    DT_ASSERT(!OsDrvIsEmulated(NULL));
    DT_ASSERT_EQ(OsDrvLastError(NULL), 0);
    DT_ASSERT_EQ(OsDrvIoCtl(NULL, 0, In, sizeof(In), NULL, NULL, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
    DT_ASSERT_EQ(OsDrvIoCtl(NULL, 0, In, sizeof(In), NULL, NULL, NULL),
                 OS_IOCTL_COMMUNICATION);
}

// A request without input never reaches the driver, so there is no driver status.
DT_TEST(RequestWithoutInputIsRefused)
{
    uint8_t In[16];
    uint32_t Status = 0xDEAD;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), NULL,
                            sizeof(In), NULL, NULL, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), In, 0, NULL,
                            NULL, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(DriverVersionComesThrough)
{
    DtDriverVersion Version;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtDrvGetDriverVersion(Drv, &Version), DTAPI_OK);
    DT_ASSERT_EQ(Version.Major, SIM_DRIVER_MAJOR);
    DT_ASSERT_EQ(Version.Minor, SIM_DRIVER_MINOR);
    DT_ASSERT_EQ(Version.Micro, SIM_DRIVER_MICRO);
    DT_ASSERT_EQ(Version.Build, SIM_DRIVER_BUILD);

    OsDrvClose(Drv);
}

DT_TEST(DeviceInfoComesThrough)
{
    DtDeviceInfo Info;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtDrvGetDeviceInfo(Drv, &Info), DTAPI_OK);
    DT_ASSERT_EQ(Info.TypeNumber, SIM_TYPE_NUMBER);
    DT_ASSERT_EQ(Info.Serial, (int64_t)SIM_SERIAL);
    DT_ASSERT_EQ(Info.HardwareRevision, SIM_HARDWARE_REVISION);
    DT_ASSERT_EQ(Info.FirmwareVersion, SIM_FIRMWARE_VERSION);
    DT_ASSERT_EQ(Info.FirmwareVariant, SIM_FIRMWARE_VARIANT);
    DT_ASSERT_EQ(Info.FirmwareStatus, DT_FWSTATUS_UPTODATE);
    DT_ASSERT_EQ(Info.VendorId, SIM_VENDOR_ID);
    DT_ASSERT_EQ(Info.DeviceId, SIM_DEVICE_ID);

    OsDrvClose(Drv);
}

DT_TEST(CommandsRejectNullArguments)
{
    DtDriverVersion Version;
    DtDeviceInfo Info;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    DT_ASSERT_EQ(DtDrvGetDriverVersion(NULL, &Version), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetDriverVersion(Drv, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetDeviceInfo(NULL, &Info), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDrvGetDeviceInfo(Drv, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Wire format +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// These drive the emulator below the typed commands, with deliberately wrong sizes. The
// emulator refuses them the way the driver does, which is what makes it a test of the
// wire format rather than a stub that accepts anything.
//

DT_TEST(OutputBufferTooSmallIsRefused)
{
    DtIoctlGetDriverVersionInput In;
    uint8_t Out[sizeof(DtIoctlGetDriverVersionOutput) - 1];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                            sizeof(In), Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(OsDrvLastError(Drv), DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(OutSize, sizeof(Out));

    OsDrvClose(Drv);
}

DT_TEST(InputShorterThanHeaderIsRefused)
{
    uint8_t In[sizeof(DtIoctlInputDataHdr) - 1];
    DtIoctlGetDevInfoOutput Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DEV_INFO2), In, sizeof(In),
                            &Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);
    DT_ASSERT_EQ(OsDrvLastError(Drv), DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

// The header is checked before the command is looked at, so a short input is an invalid
// parameter even for a command the emulator does not model.
DT_TEST(ShortInputIsRefusedBeforeTheCommand)
{
    uint8_t In[sizeof(DtIoctlInputDataHdr) - 1];
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), In, sizeof(In), NULL,
                            NULL, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

DT_TEST(DeviceInfoOutputTooSmallIsRefused)
{
    DtIoctlGetDevInfoInput In;
    uint8_t Out[sizeof(DtIoctlGetDevInfoOutput) - 1];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_GET_DEV_INFO2), &In, sizeof(In),
                            Out, &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

// A command the emulator does not model is refused, not answered with zeroes. Silently
// succeeding would let a missing emulation look like a working feature. The command is
// refused as unknown before its sizes are looked at, so even an empty output buffer
// yields DT_STATUS_NOT_SUPPORTED rather than DT_STATUS_INVALID_PARAMETER.
DT_TEST(UnmodelledCommandIsRefused)
{
    DtIoctlInputDataHdr In;
    uint8_t Out[64];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), &In, sizeof(In), Out,
                            &OutSize, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);
    DT_ASSERT_EQ(OsDrvLastError(Drv), DT_STATUS_NOT_SUPPORTED);

    DT_ASSERT_EQ(OsDrvIoCtl(Drv, DT_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), &In, sizeof(In), NULL,
                            NULL, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
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

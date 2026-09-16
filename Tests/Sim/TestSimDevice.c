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
#include "DtlDrv.h"                 // Driver commands under test.
#include "DtlDrvAbi.h"              // Raw structures for the wire-format cases.
#include "DtlTest.h"                // Test framework.
#include "OAL/OsAbstractionLayer.h" // OS abstraction under test.
#include "OAL/Sim/SimDtPcie.h"      // What the emulated card reports.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define DTL_TEST_IOCTL(Code) ((unsigned long)(Code))

// Opens the emulated device, or records why not. Returns NULL on failure, in which case
// the calling case must return.
static OsDrv* OpenSim(int* DtlFailures)
{
    OsDrv* Drv = OsDrvOpen(SIM_DEVICE_INDEX);

    if (Drv == NULL)
    {
        printf("    FAIL: no device at index 0; is CDTAPILITE_SIM=1 set?\n");
        (*DtlFailures)++;
        return NULL;
    }

    if (!OsDrvIsEmulated(Drv))
    {
        printf("    FAIL: device 0 is real hardware; refusing to run against it\n");
        (*DtlFailures)++;
        OsDrvClose(Drv);
        return NULL;
    }

    return Drv;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(EmulatedDeviceOpens)
{
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT(OsDrvIsEmulated(Drv));
    DTL_ASSERT_EQ(OsDrvLastError(Drv), 0);
    OsDrvClose(Drv);
}

// The emulator replaces the hardware rather than adding to it: one device, at index 0.
DTL_TEST(OnlyIndexZeroExists)
{
    DTL_ASSERT(OsDrvOpen(1) == NULL);
    DTL_ASSERT(OsDrvOpen(DTL_MAX_DEVICES - 1) == NULL);
}

DTL_TEST(OutOfRangeIndexIsRefused)
{
    DTL_ASSERT(OsDrvOpen(-1) == NULL);
    DTL_ASSERT(OsDrvOpen(DTL_MAX_DEVICES) == NULL);
}

DTL_TEST(NullHandleIsAccepted)
{
    uint8_t In[16];
    uint32_t Status = 0xDEAD;

    OsDrvClose(NULL);
    DTL_ASSERT(!OsDrvIsEmulated(NULL));
    DTL_ASSERT_EQ(OsDrvLastError(NULL), 0);
    DTL_ASSERT_EQ(OsDrvIoCtl(NULL, 0, In, sizeof(In), NULL, NULL, &Status),
                  OS_IOCTL_COMMUNICATION);
    DTL_ASSERT_EQ(Status, 0);
    DTL_ASSERT_EQ(OsDrvIoCtl(NULL, 0, In, sizeof(In), NULL, NULL, NULL),
                  OS_IOCTL_COMMUNICATION);
}

// A request without input never reaches the driver, so there is no driver status.
DTL_TEST(RequestWithoutInputIsRefused)
{
    uint8_t In[16];
    uint32_t Status = 0xDEAD;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), NULL,
                             sizeof(In), NULL, NULL, &Status),
                  OS_IOCTL_COMMUNICATION);
    DTL_ASSERT_EQ(Status, 0);
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), In, 0,
                             NULL, NULL, &Status),
                  OS_IOCTL_COMMUNICATION);
    DTL_ASSERT_EQ(Status, 0);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(DriverVersionComesThrough)
{
    DtlDriverVersion Version;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(DtlDrvGetDriverVersion(Drv, &Version), DTAPI_OK);
    DTL_ASSERT_EQ(Version.Major, SIM_DRIVER_MAJOR);
    DTL_ASSERT_EQ(Version.Minor, SIM_DRIVER_MINOR);
    DTL_ASSERT_EQ(Version.Micro, SIM_DRIVER_MICRO);
    DTL_ASSERT_EQ(Version.Build, SIM_DRIVER_BUILD);

    OsDrvClose(Drv);
}

DTL_TEST(DeviceInfoComesThrough)
{
    DtlDeviceInfo Info;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(DtlDrvGetDeviceInfo(Drv, &Info), DTAPI_OK);
    DTL_ASSERT_EQ(Info.TypeNumber, SIM_TYPE_NUMBER);
    DTL_ASSERT_EQ(Info.Serial, (int64_t)SIM_SERIAL);
    DTL_ASSERT_EQ(Info.HardwareRevision, SIM_HARDWARE_REVISION);
    DTL_ASSERT_EQ(Info.FirmwareVersion, SIM_FIRMWARE_VERSION);
    DTL_ASSERT_EQ(Info.FirmwareVariant, SIM_FIRMWARE_VARIANT);
    DTL_ASSERT_EQ(Info.FirmwareStatus, DT_FWSTATUS_UPTODATE);
    DTL_ASSERT_EQ(Info.VendorId, SIM_VENDOR_ID);
    DTL_ASSERT_EQ(Info.DeviceId, SIM_DEVICE_ID);

    OsDrvClose(Drv);
}

DTL_TEST(CommandsRejectNullArguments)
{
    DtlDriverVersion Version;
    DtlDeviceInfo Info;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    DTL_ASSERT_EQ(DtlDrvGetDriverVersion(NULL, &Version), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetDriverVersion(Drv, NULL), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetDeviceInfo(NULL, &Info), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlDrvGetDeviceInfo(Drv, NULL), DTAPI_E_INVALID_ARG);

    OsDrvClose(Drv);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Wire format +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// These drive the emulator below the typed commands, with deliberately wrong sizes. The
// emulator refuses them the way the driver does, which is what makes it a test of the
// wire format rather than a stub that accepts anything.
//

DTL_TEST(OutputBufferTooSmallIsRefused)
{
    DtIoctlGetDriverVersionInput In;
    uint8_t Out[sizeof(DtIoctlGetDriverVersionOutput) - 1];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In,
                             sizeof(In), Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);
    DTL_ASSERT_EQ(OsDrvLastError(Drv), DT_STATUS_INVALID_PARAMETER);
    DTL_ASSERT_EQ(OutSize, sizeof(Out));

    OsDrvClose(Drv);
}

DTL_TEST(InputShorterThanHeaderIsRefused)
{
    uint8_t In[sizeof(DtIoctlInputDataHdr) - 1];
    DtIoctlGetDevInfoOutput Out;
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(In, 0, sizeof(In));
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_GET_DEV_INFO2), In, sizeof(In),
                             &Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);
    DTL_ASSERT_EQ(OsDrvLastError(Drv), DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

// The header is checked before the command is looked at, so a short input is an invalid
// parameter even for a command the emulator does not model.
DTL_TEST(ShortInputIsRefusedBeforeTheCommand)
{
    uint8_t In[sizeof(DtIoctlInputDataHdr) - 1];
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(In, 0, sizeof(In));
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), In, sizeof(In),
                             NULL, NULL, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

DTL_TEST(DeviceInfoOutputTooSmallIsRefused)
{
    DtIoctlGetDevInfoInput In;
    uint8_t Out[sizeof(DtIoctlGetDevInfoOutput) - 1];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_GET_DEV_INFO2), &In, sizeof(In),
                             Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_INVALID_PARAMETER);

    OsDrvClose(Drv);
}

// A command the emulator does not model is refused, not answered with zeroes. Silently
// succeeding would let a missing emulation look like a working feature. The command is
// refused as unknown before its sizes are looked at, so even an empty output buffer
// yields DT_STATUS_NOT_SUPPORTED rather than DT_STATUS_INVALID_PARAMETER.
DTL_TEST(UnmodelledCommandIsRefused)
{
    DtIoctlInputDataHdr In;
    uint8_t Out[64];
    size_t OutSize = sizeof(Out);
    uint32_t Status = 0;
    OsDrv* Drv = OpenSim(DtlFailures);

    if (Drv == NULL)
        return;

    memset(&In, 0, sizeof(In));
    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), &In, sizeof(In),
                             Out, &OutSize, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);
    DTL_ASSERT_EQ(OsDrvLastError(Drv), DT_STATUS_NOT_SUPPORTED);

    DTL_ASSERT_EQ(OsDrvIoCtl(Drv, DTL_TEST_IOCTL(DT_IOCTL_DEBUG_CMD), &In, sizeof(In),
                             NULL, NULL, &Status),
                  OS_IOCTL_DRIVER_STATUS);
    DTL_ASSERT_EQ(Status, DT_STATUS_NOT_SUPPORTED);

    OsDrvClose(Drv);
}

DTL_TEST_MAIN("SimDevice", DTL_RUN(EmulatedDeviceOpens), DTL_RUN(OnlyIndexZeroExists),
              DTL_RUN(OutOfRangeIndexIsRefused), DTL_RUN(NullHandleIsAccepted),
              DTL_RUN(RequestWithoutInputIsRefused), DTL_RUN(DriverVersionComesThrough),
              DTL_RUN(DeviceInfoComesThrough), DTL_RUN(CommandsRejectNullArguments),
              DTL_RUN(OutputBufferTooSmallIsRefused),
              DTL_RUN(InputShorterThanHeaderIsRefused),
              DTL_RUN(DeviceInfoOutputTooSmallIsRefused),
              DTL_RUN(ShortInputIsRefusedBeforeTheCommand),
              DTL_RUN(UnmodelledCommandIsRefused))

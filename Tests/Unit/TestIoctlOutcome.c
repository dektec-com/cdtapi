// #*#*#*#*#*#*#*#*#*#*#*#*# TestIoctlOutcome.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for classifying a failed IOCTL on Windows and on Linux
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Runs on every platform, with each platform's driver statuses written out as numbers:
// the vendored DT_STATUS_ names take the encoding of the platform the test is built on,
// and both encodings are needed here.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtTest.h"             // Test framework.
#include "OAL/OsIoctlOutcome.h" // Interface under test.

// DT_STATUS_IN_USE as the Windows driver reports it: error severity, customer bit, 10.
#define WIN_DT_STATUS_IN_USE 0xE000000Au

// DT_STATUS_IN_USE as the Linux driver encodes it, before negating it.
#define LIN_DT_STATUS_IN_USE 0x0001000A

// Plain Windows errors, from winerror.h.
#define WIN_ERROR_INVALID_FUNCTION 1UL
#define WIN_ERROR_ACCESS_DENIED 5UL
#define WIN_ERROR_GEN_FAILURE 31UL

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Windows +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(WindowsCustomerBitIsDriverStatus)
{
    uint32_t Status = 0xDEAD;

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyWindows(WIN_DT_STATUS_IN_USE, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, WIN_DT_STATUS_IN_USE);

    // Bit 29 alone is enough; the severity bits are not what decides.
    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyWindows(0x20000001u, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, 0x20000001u);
}

DT_TEST(WindowsNoSystemResourcesIsNoResources)
{
    uint32_t Status = 0xDEAD;

    DT_ASSERT_EQ(
        OsIoctlOutcome_ClassifyWindows(OS_WIN_ERROR_NO_SYSTEM_RESOURCES, &Status),
        OS_IOCTL_NO_RESOURCES);
    DT_ASSERT_EQ(Status, 0);
}

DT_TEST(WindowsOtherErrorsAreCommunication)
{
    uint32_t Status = 0xDEAD;

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyWindows(WIN_ERROR_INVALID_FUNCTION, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyWindows(WIN_ERROR_ACCESS_DENIED, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyWindows(WIN_ERROR_GEN_FAILURE, &Status),
                 OS_IOCTL_COMMUNICATION);

    // Every bit but the customer bit set: still an error of Windows itself.
    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyWindows(0xDFFFFFFFu, &Status),
                 OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Linux +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(LinuxZeroIsOk)
{
    uint32_t Status = 0xDEAD;

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyLinux(0, &Status), OS_IOCTL_OK);
    DT_ASSERT_EQ(Status, 0);
}

// The C library returns -1 and sets errno for a failure of the call itself.
DT_TEST(LinuxMinusOneIsCommunication)
{
    uint32_t Status = 0xDEAD;

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyLinux(-1, &Status), OS_IOCTL_COMMUNICATION);
    DT_ASSERT_EQ(Status, 0);
}

DT_TEST(LinuxNegatedStatusIsDriverStatus)
{
    uint32_t Status = 0xDEAD;

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyLinux(-LIN_DT_STATUS_IN_USE, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, LIN_DT_STATUS_IN_USE);
}

// The extremes of the return value: the negation must not overflow, and a positive
// value, which no driver returns, is still passed on rather than taken for success.
DT_TEST(LinuxExtremesDoNotOverflow)
{
    uint32_t Status = 0;

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyLinux(-2147483647 - 1, &Status),
                 OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, 0x80000000u);

    DT_ASSERT_EQ(OsIoctlOutcome_ClassifyLinux(1, &Status), OS_IOCTL_DRIVER_STATUS);
    DT_ASSERT_EQ(Status, 0xFFFFFFFFu);
}

DT_TEST_MAIN("IoctlOutcome", DT_RUN(WindowsCustomerBitIsDriverStatus),
             DT_RUN(WindowsNoSystemResourcesIsNoResources),
             DT_RUN(WindowsOtherErrorsAreCommunication), DT_RUN(LinuxZeroIsOk),
             DT_RUN(LinuxMinusOneIsCommunication),
             DT_RUN(LinuxNegatedStatusIsDriverStatus), DT_RUN(LinuxExtremesDoNotOverflow))

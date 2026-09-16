// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestDevice.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the device layer that need no device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"    // Public API under test.
#include "Core/DtlAlloc.h" // Allocation failure injection.
#include "DtlDevice.h"     // Device layer under test.
#include "DtlTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Description +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The emulated card has no sub-type, so the sub-type cases of DTAPI's description are
// checked here, against the strings DtapiDtHwFuncDesc2String builds.
//

static const char* Describe(int TypeNumber, int SubType, int Port)
{
    static char Buf[MAX_DEVICE_DESC_SIZE];

    if (DtlDeviceDescribe(TypeNumber, SubType, Port, Buf, sizeof(Buf)) != DTAPI_OK)
        return "(failed)";
    return Buf;
}

DTL_TEST(DescriptionHasTypeAndPort)
{
    DTL_ASSERT_STR(Describe(2178, 0, 1), "DTA-2178 port 1");
    DTL_ASSERT_STR(Describe(2110, 0, 10), "DTA-2110 port 10");
    DTL_ASSERT_STR(Describe(0, 0, 0), "DTA-0 port 0");
}

DTL_TEST(SubTypeIsALetter)
{
    DTL_ASSERT_STR(Describe(2172, 1, 3), "DTA-2172A port 3");
    DTL_ASSERT_STR(Describe(2178, 2, 1), "DTA-2178B port 1");
    DTL_ASSERT_STR(Describe(2174, 26, 2), "DTA-2174Z port 2");
    DTL_ASSERT_STR(Describe(2174, -1, 2), "DTA-2174 port 2");
}

// DTAPI's special case, repeated type name included.
DTL_TEST(Dta2178AsiRepeatsTheName)
{
    DTL_ASSERT_STR(Describe(2178, 1, 1), "DTA-2178DTA-2178-ASI port 1");
    DTL_ASSERT_STR(Describe(2179, 1, 1), "DTA-2179A port 1");
}

// DTAPI refuses a buffer that cannot also hold the terminator, and leaves it empty.
DTL_TEST(DescriptionMustFit)
{
    char Buf[16];

    DTL_ASSERT_EQ(DtlDeviceDescribe(2178, 0, 1, Buf, 15), DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_STR(Buf, "");
    DTL_ASSERT_OK(DtlDeviceDescribe(2178, 0, 1, Buf, 16));
    DTL_ASSERT_STR(Buf, "DTA-2178 port 1");
    DTL_ASSERT_EQ(DtlDeviceDescribe(2178, 0, 1, NULL, 16), DTAPI_E_INVALID_BUF);
    DTL_ASSERT_EQ(DtlDeviceDescribe(2178, 0, 1, Buf, 0), DTAPI_E_INVALID_BUF);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static bool Supported(int Major, int Minor, int Micro, int Build)
{
    DtlDriverVersion Version;

    Version.Major = Major;
    Version.Minor = Minor;
    Version.Micro = Micro;
    Version.Build = Build;
    return DtlDrvVersionIsSupported(&Version);
}

// 1.3.1 is the oldest DtPcie driver DTAPI accepts; each part decides only when the parts
// before it are equal.
DTL_TEST(DriverVersionFrom131)
{
    DTL_ASSERT(Supported(1, 3, 1, 0));
    DTL_ASSERT(Supported(1, 3, 2, 0));
    DTL_ASSERT(Supported(1, 4, 0, 0));
    DTL_ASSERT(Supported(2, 0, 0, 0));
    DTL_ASSERT(Supported(1, 99, 0, 0));
    DTL_ASSERT(!Supported(1, 3, 0, 99));
    DTL_ASSERT(!Supported(1, 2, 9, 0));
    DTL_ASSERT(!Supported(0, 99, 99, 0));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Without a device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Everything a detached or missing device object answers before any device is involved.
//

DTL_TEST(NullDeviceIsRefused)
{
    DtTimeOfDay Tod = {7, 7};
    DtDevice* Null = NULL;

    DTL_ASSERT_EQ(DtDevice_AttachToSerial(NULL, 1), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_Detach(NULL), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_SetIoConfig(NULL, 1, DTAPI_IOCONFIG_IODIR,
                                       DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_SetToInput(NULL, 1), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_SetToOutput(NULL, 1), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtDevice_GetTimeOfDay(NULL, &Tod), DTAPI_E_INVALID_ARG);

    DtDevice_Free(NULL);
    DtDevice_Freep(NULL);
    DtDevice_Freep(&Null);
    DTL_ASSERT(Null == NULL);
}

DTL_TEST(DetachedDeviceIsNotAttached)
{
    DtTimeOfDay Tod = {7, 7};
    DtDevice* Device = DtDevice_Alloc();

    DTL_ASSERT(Device != NULL);
    DTL_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
    DTL_ASSERT_EQ(DtDevice_SetToInput(Device, 1), DTAPI_E_NOT_ATTACHED);
    DTL_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, &Tod), DTAPI_E_NOT_ATTACHED);
    DTL_ASSERT_EQ(Tod.Seconds, 0);
    DTL_ASSERT_EQ(Tod.Nanoseconds, 0);
    DTL_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, NULL), DTAPI_E_INVALID_ARG);

    DtDevice_Freep(&Device);
    DTL_ASSERT(Device == NULL);
}

DTL_TEST(AllocSurvivesAllocationFailure)
{
    DtlAllocResetCount();
    DtlAllocFailAfter(0);
    DTL_ASSERT(DtDevice_Alloc() == NULL);
    DtlAllocResetCount();
}

// The argument checks of the scan come before any device is looked at.
DTL_TEST(ScanArgumentsAreChecked)
{
    DtHwFuncDesc Funcs[2];
    int Count = 5;

    DTL_ASSERT_EQ(DtapiHwFuncScan(2, NULL, Funcs), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtapiHwFuncScan(-1, &Count, Funcs), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtapiHwFuncScan(2, &Count, NULL), DTAPI_E_INVALID_BUF);
    DTL_ASSERT_EQ(DtapiHwFuncScan(1, &Count, NULL), DTAPI_E_INVALID_BUF);
    DTL_ASSERT_EQ(Count, 5);
}

DTL_TEST_MAIN("Device", DTL_RUN(DescriptionHasTypeAndPort), DTL_RUN(SubTypeIsALetter),
              DTL_RUN(Dta2178AsiRepeatsTheName), DTL_RUN(DescriptionMustFit),
              DTL_RUN(DriverVersionFrom131), DTL_RUN(NullDeviceIsRefused),
              DTL_RUN(DetachedDeviceIsNotAttached),
              DTL_RUN(AllocSurvivesAllocationFailure), DTL_RUN(ScanArgumentsAreChecked))

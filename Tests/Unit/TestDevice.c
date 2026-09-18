// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestDevice.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for the device layer that need no device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation failure injection.
#include "DtDevice.h"     // Device layer under test.
#include "DtTest.h"       // Test framework.
#include "cdtapi.h"       // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Description +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The emulated card has no sub-type, so the sub-type cases of DTAPI's description are
// checked here, against the strings DtapiDtHwFuncDesc2String builds.
//

static const char* Describe(int TypeNumber, int SubType, int Port)
{
    static char Buf[MAX_DEVICE_DESC_SIZE];

    if (DtDevice_Describe(TypeNumber, SubType, Port, Buf, sizeof(Buf)) != DTAPI_OK)
        return "(failed)";
    return Buf;
}

DT_TEST(DescriptionHasTypeAndPort)
{
    DT_ASSERT_STR(Describe(2178, 0, 1), "DTA-2178 port 1");
    DT_ASSERT_STR(Describe(2110, 0, 10), "DTA-2110 port 10");
    DT_ASSERT_STR(Describe(0, 0, 0), "DTA-0 port 0");
}

DT_TEST(SubTypeIsALetter)
{
    DT_ASSERT_STR(Describe(2172, 1, 3), "DTA-2172A port 3");
    DT_ASSERT_STR(Describe(2178, 2, 1), "DTA-2178B port 1");
    DT_ASSERT_STR(Describe(2174, 26, 2), "DTA-2174Z port 2");
    DT_ASSERT_STR(Describe(2174, -1, 2), "DTA-2174 port 2");
}

// DTAPI's special case, repeated type name included.
DT_TEST(Dta2178AsiRepeatsTheName)
{
    DT_ASSERT_STR(Describe(2178, 1, 1), "DTA-2178DTA-2178-ASI port 1");
    DT_ASSERT_STR(Describe(2179, 1, 1), "DTA-2179A port 1");
}

// DTAPI refuses a buffer that cannot also hold the terminator, and leaves it empty.
DT_TEST(DescriptionMustFit)
{
    char Buf[16];

    DT_ASSERT_EQ(DtDevice_Describe(2178, 0, 1, Buf, 15), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_STR(Buf, "");
    DT_ASSERT_OK(DtDevice_Describe(2178, 0, 1, Buf, 16));
    DT_ASSERT_STR(Buf, "DTA-2178 port 1");
    DT_ASSERT_EQ(DtDevice_Describe(2178, 0, 1, NULL, 16), DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtDevice_Describe(2178, 0, 1, Buf, 0), DTAPI_E_INVALID_BUF);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Driver version +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static bool Supported(int Major, int Minor, int Micro, int Build)
{
    DtDriverVersion Version;

    Version.Major = Major;
    Version.Minor = Minor;
    Version.Micro = Micro;
    Version.Build = Build;
    return DtPcieCmd_VersionIsSupported(&Version);
}

// 1.3.1 is the oldest DtPcie driver DTAPI accepts; each part decides only when the parts
// before it are equal.
DT_TEST(DriverVersionFrom131)
{
    DT_ASSERT(Supported(1, 3, 1, 0));
    DT_ASSERT(Supported(1, 3, 2, 0));
    DT_ASSERT(Supported(1, 4, 0, 0));
    DT_ASSERT(Supported(2, 0, 0, 0));
    DT_ASSERT(Supported(1, 99, 0, 0));
    DT_ASSERT(!Supported(1, 3, 0, 99));
    DT_ASSERT(!Supported(1, 2, 9, 0));
    DT_ASSERT(!Supported(0, 99, 99, 0));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Without a device +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Everything a detached or missing device object answers before any device is involved.
//

DT_TEST(NullDeviceIsRefused)
{
    DtTimeOfDay Tod = {7, 7};
    DtDevice* Null = NULL;

    DT_ASSERT_EQ(DtDevice_AttachToSerial(NULL, 1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_Detach(NULL), DTAPI_E_INVALID_ARG);
    DtIoConfig Config = {
        1, DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, DTAPI_IOCONFIG_INPUT, {-1, -1}};
    DT_ASSERT_EQ(DtDevice_SetIoConfig(NULL, &Config, 1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_GetIoConfig(NULL, &Config, 1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetToInput(NULL, 1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_SetToOutput(NULL, 1), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDay(NULL, &Tod), DTAPI_E_INVALID_ARG);

    DtDevice_Free(NULL);
    DtDevice_Freep(NULL);
    DtDevice_Freep(&Null);
    DT_ASSERT(Null == NULL);
}

DT_TEST(DetachedDeviceIsNotAttached)
{
    DtTimeOfDay Tod = {7, 7};
    DtDevice* Device = DtDevice_Alloc();

    DT_ASSERT(Device != NULL);
    DT_ASSERT_EQ(DtDevice_Detach(Device), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtDevice_SetToInput(Device, 1), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, &Tod), DTAPI_E_NOT_ATTACHED);
    DT_ASSERT_EQ(Tod.Seconds, 0);
    DT_ASSERT_EQ(Tod.Nanoseconds, 0);
    DT_ASSERT_EQ(DtDevice_GetTimeOfDay(Device, NULL), DTAPI_E_INVALID_ARG);

    DtDevice_Freep(&Device);
    DT_ASSERT(Device == NULL);
}

DT_TEST(AllocSurvivesAllocationFailure)
{
    DtAlloc_ResetCount();
    DtAlloc_FailAfter(0);
    DT_ASSERT(DtDevice_Alloc() == NULL);
    DtAlloc_ResetCount();
}

// The argument checks of the scan come before any device is looked at.
DT_TEST(ScanArgumentsAreChecked)
{
    DtHwFuncDesc Funcs[2];
    int Count = 5;

    DT_ASSERT_EQ(DtapiHwFuncScan(2, NULL, Funcs), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtapiHwFuncScan(-1, &Count, Funcs), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtapiHwFuncScan(2, &Count, NULL), DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(DtapiHwFuncScan(1, &Count, NULL), DTAPI_E_INVALID_BUF);
    DT_ASSERT_EQ(Count, 5);
}

DT_TEST_MAIN("Device", DT_RUN(DescriptionHasTypeAndPort), DT_RUN(SubTypeIsALetter),
             DT_RUN(Dta2178AsiRepeatsTheName), DT_RUN(DescriptionMustFit),
             DT_RUN(DriverVersionFrom131), DT_RUN(NullDeviceIsRefused),
             DT_RUN(DetachedDeviceIsNotAttached), DT_RUN(AllocSurvivesAllocationFailure),
             DT_RUN(ScanArgumentsAreChecked))

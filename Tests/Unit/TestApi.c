// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestApi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the version entry point and the build wiring
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h" // Test framework.
#include "cdtapi.h" // Public API.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(VersionStringIsPresent)
{
    const char* Version = DtapiGetVersion();
    DT_ASSERT(Version != NULL);
    DT_ASSERT(Version[0] != '\0');
}

// The string macro and the numeric macros are generated from the same CMake project
// version. If the template ever drifts, they stop agreeing.
DT_TEST(VersionMacrosAgreeWithString)
{
    char Expected[32];
    snprintf(Expected, sizeof(Expected), "%d.%d.%d", CDTAPI_VERSION_MAJOR,
             CDTAPI_VERSION_MINOR, CDTAPI_VERSION_PATCH);
    DT_ASSERT_STR(DtapiGetVersion(), Expected);
}

DT_TEST_MAIN("Api", DT_RUN(VersionStringIsPresent), DT_RUN(VersionMacrosAgreeWithString))

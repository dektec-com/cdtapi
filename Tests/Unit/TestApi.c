// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestApi.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the version entry point and the build wiring
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h" // Public API.
#include "DtlTest.h"    // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(VersionStringIsPresent)
{
    const char* Version = DtapiLiteGetVersion();
    DTL_ASSERT(Version != NULL);
    DTL_ASSERT(Version[0] != '\0');
}

// The string macro and the numeric macros are generated from the same CMake project
// version. If the template ever drifts, they stop agreeing.
DTL_TEST(VersionMacrosAgreeWithString)
{
    char Expected[32];
    snprintf(Expected, sizeof(Expected), "%d.%d.%d", CDTAPILITE_VERSION_MAJOR,
             CDTAPILITE_VERSION_MINOR, CDTAPILITE_VERSION_PATCH);
    DTL_ASSERT_STR(DtapiLiteGetVersion(), Expected);
}

DTL_TEST_MAIN("Api", DTL_RUN(VersionStringIsPresent),
              DTL_RUN(VersionMacrosAgreeWithString))

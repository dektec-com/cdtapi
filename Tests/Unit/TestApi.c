// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestApi.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the version entry point and the build wiring
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"        // Test framework.
#include "cdtapi.h"        // Public API.
#include "cdtapi_avfifo.h" // The FIFOs' configurations, which the bridge takes.

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

// The library says it has the NMOS bridge exactly when it was built with it. The test's
// own build is told which, CDTAPI_TEST_WITH_NMOS, from the same option.
DT_TEST(HasNmosAsBuilt)
{
    DT_ASSERT(DtapiHasNmos() == (CDTAPI_TEST_WITH_NMOS != 0));
}

#if !CDTAPI_TEST_WITH_NMOS

// The bridge's functions as a build without it exports them: the header that declares
// them is not installed, so they are declared here as the stubs define them.
typedef struct DtNmosFlow DtNmosFlow;
CDTAPI_API DtapiResult DtNmosAvFifo_FlowFromTxFifo(AvFifo_TxFifo* Fifo, DtNmosFlow* Flow);
CDTAPI_API DtapiResult DtNmosAvFifo_RxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_RxFrameFormat Format,
                                                     St2110_RxConfigVideo* Video,
                                                     St2110_RxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);
CDTAPI_API DtapiResult DtNmosAvFifo_TxConfigFromFlow(const DtNmosFlow* Flow,
                                                     St2110_TxConfigVideo* Video,
                                                     St2110_TxConfigAudio* Audio,
                                                     AvFifo_IpPars* IpPars);

#endif

// Without the bridge its functions are there and fail, saying why.
DT_TEST(NmosStubsFail)
{
#if CDTAPI_TEST_WITH_NMOS
    (void)DtFailures;
#else
    AvFifo_IpPars IpPars;
    DT_ASSERT_EQ(DtNmosAvFifo_FlowFromTxFifo(NULL, NULL), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "DtNmosAvFifo_FlowFromTxFifo") != NULL);
    DT_ASSERT_EQ(DtNmosAvFifo_RxConfigFromFlow(NULL, St2110_RxFrameFormat_Raw, NULL, NULL,
                                               &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT(strstr(GetLastException(), "without NMOS") != NULL);
    DT_ASSERT_EQ(DtNmosAvFifo_TxConfigFromFlow(NULL, NULL, NULL, &IpPars),
                 DTAPI_E_NOT_SUPPORTED);
#endif
}

DT_TEST_MAIN("Api", DT_RUN(VersionStringIsPresent), DT_RUN(VersionMacrosAgreeWithString),
             DT_RUN(HasNmosAsBuilt), DT_RUN(NmosStubsFail))

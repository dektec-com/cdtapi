// #*#*#*#*#*#*#*#*#*#*#*#*# TestDtPcieVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the translation of driver video standards to DTAPI ones
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The standards are written out again here, rather than taken from the list the
// implementation is built from, so that a standard missing from that list fails.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtPcieAbi.h"    // The DT_VIDSTD_ codes.
#include "DtPcieVidStd.h" // Interface under test.
#include "DtTest.h"       // Test framework.
#include "cdtapi.h"       // The DTAPI_VIDSTD_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Video standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct VidStdCase
{
    const char* Name;
    int DrvVidStd;
    int VidStd;
} VidStdCase;

#define CASE(Name) {#Name, DT_VIDSTD_##Name, DTAPI_VIDSTD_##Name}

static const VidStdCase KnownCases[] = {
    CASE(525I59_94),  CASE(625I50),      CASE(720P23_98),    CASE(720P24),
    CASE(720P25),     CASE(720P29_97),   CASE(720P30),       CASE(720P50),
    CASE(720P59_94),  CASE(720P60),      CASE(1080P23_98),   CASE(1080P24),
    CASE(1080P25),    CASE(1080P29_97),  CASE(1080P30),      CASE(1080PSF23_98),
    CASE(1080PSF24),  CASE(1080PSF25),   CASE(1080PSF29_97), CASE(1080PSF30),
    CASE(1080I50),    CASE(1080I59_94),  CASE(1080I60),      CASE(1080P50),
    CASE(1080P50B),   CASE(1080P59_94),  CASE(1080P59_94B),  CASE(1080P60),
    CASE(1080P60B),   CASE(2160P23_98),  CASE(2160P24),      CASE(2160P25),
    CASE(2160P29_97), CASE(2160P30),     CASE(2160P50),      CASE(2160P50B),
    CASE(2160P59_94), CASE(2160P59_94B), CASE(2160P60),      CASE(2160P60B),
};

#undef CASE

DT_TEST(EveryKnownStandardMaps)
{
    size_t i;

    for (i = 0; i < sizeof(KnownCases) / sizeof(KnownCases[0]); i++)
    {
        const VidStdCase* Case = &KnownCases[i];
        int VidStd = DtPcieVidStd_FromDriver(Case->DrvVidStd);

        if (VidStd != Case->VidStd)
            DT_FAIL("DT_VIDSTD_%s: expected %d, got %d", Case->Name, Case->VidStd,
                    VidStd);
    }
}

// The two numbering schemes differ, which is why the translation exists at all.
DT_TEST(CodesAreTheDriversNotDtapis)
{
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(0x014C), 67);
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(67), DTAPI_VIDSTD_UNKNOWN);
}

DT_TEST(UnknownAndTransportStreamAreUnknown)
{
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(DT_VIDSTD_UNKNOWN), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(DT_VIDSTD_TS), DTAPI_VIDSTD_UNKNOWN);
}

// The driver names three standards that CDTAPI has no DTAPI_VIDSTD_ code for.
DT_TEST(StandardsCdtapiDoesNotKnowAreUnknown)
{
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(DT_VIDSTD_480P59_94), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(DT_VIDSTD_525P59_94), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(DT_VIDSTD_625P50), DTAPI_VIDSTD_UNKNOWN);
}

DT_TEST(ValueThatIsNoCodeIsUnknown)
{
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(0x7FFF), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DtPcieVidStd_FromDriver(-2), DTAPI_VIDSTD_UNKNOWN);
}

DT_TEST_MAIN("DtPcieVidStd", DT_RUN(EveryKnownStandardMaps),
             DT_RUN(CodesAreTheDriversNotDtapis),
             DT_RUN(UnknownAndTransportStreamAreUnknown),
             DT_RUN(StandardsCdtapiDoesNotKnowAreUnknown),
             DT_RUN(ValueThatIsNoCodeIsUnknown))

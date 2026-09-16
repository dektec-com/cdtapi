// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the video standard to I/O standard mapping
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h"     // Function under test and constants.
#include "DtIoConfig.h"     // Names of I/O configuration codes.
#include "DtTest.h"         // Test framework.
#include "Video/DtVidStd.h" // Link standards and 4K classification.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Expected +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Every video standard CDTAPI.h defines, with the I/O standard DTAPI maps it to and the
// link standard that selects that mapping. Each entry names the standard once, and the
// macro and its string are expanded from that one token.
//

typedef struct Expected
{
    const char* Name;
    int VidStd;
    int IoStd;
    int LinkStd;
} Expected;

#define V(Name, IoStd, Link) {#Name, DTAPI_VIDSTD_##Name, DTAPI_IOCONFIG_##IoStd, Link}

static const Expected g_Expected[] = {
    V(525I59_94, SDI, DT_VIDLNK_NONE),
    V(625I50, SDI, DT_VIDLNK_NONE),
    V(720P23_98, HDSDI, DT_VIDLNK_NONE),
    V(720P24, HDSDI, DT_VIDLNK_NONE),
    V(720P25, HDSDI, DT_VIDLNK_NONE),
    V(720P29_97, HDSDI, DT_VIDLNK_NONE),
    V(720P30, HDSDI, DT_VIDLNK_NONE),
    V(720P50, HDSDI, DT_VIDLNK_NONE),
    V(720P59_94, HDSDI, DT_VIDLNK_NONE),
    V(720P60, HDSDI, DT_VIDLNK_NONE),
    V(1080P23_98, HDSDI, DT_VIDLNK_NONE),
    V(1080P24, HDSDI, DT_VIDLNK_NONE),
    V(1080P25, HDSDI, DT_VIDLNK_NONE),
    V(1080P29_97, HDSDI, DT_VIDLNK_NONE),
    V(1080P30, HDSDI, DT_VIDLNK_NONE),
    V(1080PSF23_98, HDSDI, DT_VIDLNK_NONE),
    V(1080PSF24, HDSDI, DT_VIDLNK_NONE),
    V(1080PSF25, HDSDI, DT_VIDLNK_NONE),
    V(1080PSF29_97, HDSDI, DT_VIDLNK_NONE),
    V(1080PSF30, HDSDI, DT_VIDLNK_NONE),
    V(1080I50, HDSDI, DT_VIDLNK_NONE),
    V(1080I59_94, HDSDI, DT_VIDLNK_NONE),
    V(1080I60, HDSDI, DT_VIDLNK_NONE),
    V(1080P50, 3GSDI, DT_VIDLNK_NONE),
    V(1080P50B, 3GSDI, DT_VIDLNK_NONE),
    V(1080P59_94, 3GSDI, DT_VIDLNK_NONE),
    V(1080P59_94B, 3GSDI, DT_VIDLNK_NONE),
    V(1080P60, 3GSDI, DT_VIDLNK_NONE),
    V(1080P60B, 3GSDI, DT_VIDLNK_NONE),
    V(2160P23_98, 6GSDI, DT_VIDLNK_4K_SMPTE2081),
    V(2160P24, 6GSDI, DT_VIDLNK_4K_SMPTE2081),
    V(2160P25, 6GSDI, DT_VIDLNK_4K_SMPTE2081),
    V(2160P29_97, 6GSDI, DT_VIDLNK_4K_SMPTE2081),
    V(2160P30, 6GSDI, DT_VIDLNK_4K_SMPTE2081),
    V(2160P50, 12GSDI, DT_VIDLNK_4K_SMPTE2082),
    V(2160P50B, 12GSDI, DT_VIDLNK_4K_SMPTE2082),
    V(2160P59_94, 12GSDI, DT_VIDLNK_4K_SMPTE2082),
    V(2160P59_94B, 12GSDI, DT_VIDLNK_4K_SMPTE2082),
    V(2160P60, 12GSDI, DT_VIDLNK_4K_SMPTE2082),
    V(2160P60B, 12GSDI, DT_VIDLNK_4K_SMPTE2082),
};

#undef V

#define EXPECTED_COUNT ((int)(sizeof(g_Expected) / sizeof(g_Expected[0])))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Mapping +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every video standard maps to its I/O standard, and the sub-value is the I/O
// configuration code whose name is the video standard's own name.
DT_TEST(EveryStandardMapsAsDtapiDoes)
{
    char Name[64];
    int i;

    for (i = 0; i < EXPECTED_COUNT; i++)
    {
        const Expected* E = &g_Expected[i];
        int Value = 0;
        int SubValue = 0;
        unsigned int Result = DtapiVidStd2IoStd(E->VidStd, E->LinkStd, &Value, &SubValue);

        if (Result != DTAPI_OK)
            DT_FAIL("%s: result 0x%X", E->Name, Result);
        if (Value != E->IoStd)
            DT_FAIL("%s: value %d, expected %d", E->Name, Value, E->IoStd);
        if (SubValue != E->VidStd)
            DT_FAIL("%s: sub-value %d, expected %d", E->Name, SubValue, E->VidStd);

        DT_ASSERT_OK(DtIoConfigGetName(SubValue, Name, sizeof(Name)));
        if (strcmp(Name, E->Name) != 0)
            DT_FAIL("%s: sub-value %d is named \"%s\"", E->Name, SubValue, Name);
    }
}

// Exactly the eleven 2160p standards count as 4K. Checked over a range well beyond every
// defined code, so that a stray case label for some other number would also show.
DT_TEST(OnlyThe2160pStandardsAre4k)
{
    int Count = 0;
    int VidStd;

    for (VidStd = -10; VidStd < 1000; VidStd++)
    {
        if (DtVidStdIs4k(VidStd))
            Count++;
    }

    DT_ASSERT_EQ(Count, 11);
    DT_ASSERT(DtVidStdIs4k(DTAPI_VIDSTD_2160P60B));
    DT_ASSERT(!DtVidStdIs4k(DTAPI_VIDSTD_1080P60B));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Link standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A standard that is not 4K takes no link standard, and says so before anything else.
DT_TEST(NonUhdStandardRefusesALinkStandard)
{
    int Link;
    int i;

    for (i = 0; i < EXPECTED_COUNT; i++)
    {
        if (DtVidStdIs4k(g_Expected[i].VidStd))
            continue;

        for (Link = 0; Link <= 3; Link++)
        {
            int Value = 7;
            int SubValue = 7;

            if (DtapiVidStd2IoStd(g_Expected[i].VidStd, Link, &Value, &SubValue) !=
                DTAPI_E_INVALID_LINKSTD)
            {
                DT_FAIL("%s with link %d was not refused", g_Expected[i].Name, Link);
            }
            DT_ASSERT_EQ(Value, -1);
            DT_ASSERT_EQ(SubValue, -1);
        }
    }
}

DT_TEST(UhdStandardNeedsAValidLinkStandard)
{
    int Value;
    int SubValue;

    DT_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P50, -1, &Value, &SubValue),
                 DTAPI_E_INVALID_LINKSTD);
    DT_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P50, 4, &Value, &SubValue),
                 DTAPI_E_INVALID_LINKSTD);
    DT_ASSERT_EQ(Value, -1);
}

// Four-link 4K needs the per-link standard, which comes with milestone M4.
DT_TEST(FourLinkUhdIsNotYetImplemented)
{
    int Value = 7;
    int SubValue = 7;

    DT_ASSERT_EQ(
        DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P50, DT_VIDLNK_4K_SMPTE425, &Value, &SubValue),
        DTAPI_E_NOT_IMPLEMENTED);
    DT_ASSERT_EQ(Value, -1);
    DT_ASSERT_EQ(SubValue, -1);

    DT_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P30, DT_VIDLNK_4K_SMPTE425B, &Value,
                                   &SubValue),
                 DTAPI_E_NOT_IMPLEMENTED);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Refusals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(UnknownVideoStandardIsRefused)
{
    int Value = 7;
    int SubValue = 7;

    DT_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_UNKNOWN, -1, &Value, &SubValue),
                 DTAPI_E_INVALID_VIDSTD);
    DT_ASSERT_EQ(Value, -1);
    DT_ASSERT_EQ(SubValue, -1);

    DT_ASSERT_EQ(DtapiVidStd2IoStd(12345, -1, &Value, &SubValue), DTAPI_E_INVALID_VIDSTD);
}

// With both the link standard and the video standard wrong, DTAPI reports the link
// standard, because it checks that first.
DT_TEST(LinkStandardIsCheckedFirst)
{
    int Value;
    int SubValue;

    DT_ASSERT_EQ(DtapiVidStd2IoStd(12345, 0, &Value, &SubValue), DTAPI_E_INVALID_LINKSTD);
}

// A null output pointer is refused before anything is written through the other one.
DT_TEST(NullOutputIsRefusedWithoutWriting)
{
    int Kept = 7;

    DT_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, NULL, &Kept),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Kept, 7);
    DT_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, &Kept, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Kept, 7);
}

DT_TEST_MAIN("VidStd", DT_RUN(EveryStandardMapsAsDtapiDoes),
             DT_RUN(OnlyThe2160pStandardsAre4k),
             DT_RUN(NonUhdStandardRefusesALinkStandard),
             DT_RUN(UhdStandardNeedsAValidLinkStandard),
             DT_RUN(FourLinkUhdIsNotYetImplemented),
             DT_RUN(UnknownVideoStandardIsRefused), DT_RUN(LinkStandardIsCheckedFirst),
             DT_RUN(NullOutputIsRefusedWithoutWriting))

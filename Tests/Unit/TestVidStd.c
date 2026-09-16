// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the video standard to I/O standard mapping
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h"      // Function under test and constants.
#include "DtlIoConfig.h"     // Names of I/O configuration codes.
#include "DtlTest.h"         // Test framework.
#include "Video/DtlVidStd.h" // Link standards and 4K classification.

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
    V(525I59_94, SDI, DTL_VIDLNK_NONE),
    V(625I50, SDI, DTL_VIDLNK_NONE),
    V(720P23_98, HDSDI, DTL_VIDLNK_NONE),
    V(720P24, HDSDI, DTL_VIDLNK_NONE),
    V(720P25, HDSDI, DTL_VIDLNK_NONE),
    V(720P29_97, HDSDI, DTL_VIDLNK_NONE),
    V(720P30, HDSDI, DTL_VIDLNK_NONE),
    V(720P50, HDSDI, DTL_VIDLNK_NONE),
    V(720P59_94, HDSDI, DTL_VIDLNK_NONE),
    V(720P60, HDSDI, DTL_VIDLNK_NONE),
    V(1080P23_98, HDSDI, DTL_VIDLNK_NONE),
    V(1080P24, HDSDI, DTL_VIDLNK_NONE),
    V(1080P25, HDSDI, DTL_VIDLNK_NONE),
    V(1080P29_97, HDSDI, DTL_VIDLNK_NONE),
    V(1080P30, HDSDI, DTL_VIDLNK_NONE),
    V(1080PSF23_98, HDSDI, DTL_VIDLNK_NONE),
    V(1080PSF24, HDSDI, DTL_VIDLNK_NONE),
    V(1080PSF25, HDSDI, DTL_VIDLNK_NONE),
    V(1080PSF29_97, HDSDI, DTL_VIDLNK_NONE),
    V(1080PSF30, HDSDI, DTL_VIDLNK_NONE),
    V(1080I50, HDSDI, DTL_VIDLNK_NONE),
    V(1080I59_94, HDSDI, DTL_VIDLNK_NONE),
    V(1080I60, HDSDI, DTL_VIDLNK_NONE),
    V(1080P50, 3GSDI, DTL_VIDLNK_NONE),
    V(1080P50B, 3GSDI, DTL_VIDLNK_NONE),
    V(1080P59_94, 3GSDI, DTL_VIDLNK_NONE),
    V(1080P59_94B, 3GSDI, DTL_VIDLNK_NONE),
    V(1080P60, 3GSDI, DTL_VIDLNK_NONE),
    V(1080P60B, 3GSDI, DTL_VIDLNK_NONE),
    V(2160P23_98, 6GSDI, DTL_VIDLNK_4K_SMPTE2081),
    V(2160P24, 6GSDI, DTL_VIDLNK_4K_SMPTE2081),
    V(2160P25, 6GSDI, DTL_VIDLNK_4K_SMPTE2081),
    V(2160P29_97, 6GSDI, DTL_VIDLNK_4K_SMPTE2081),
    V(2160P30, 6GSDI, DTL_VIDLNK_4K_SMPTE2081),
    V(2160P50, 12GSDI, DTL_VIDLNK_4K_SMPTE2082),
    V(2160P50B, 12GSDI, DTL_VIDLNK_4K_SMPTE2082),
    V(2160P59_94, 12GSDI, DTL_VIDLNK_4K_SMPTE2082),
    V(2160P59_94B, 12GSDI, DTL_VIDLNK_4K_SMPTE2082),
    V(2160P60, 12GSDI, DTL_VIDLNK_4K_SMPTE2082),
    V(2160P60B, 12GSDI, DTL_VIDLNK_4K_SMPTE2082),
};

#undef V

#define EXPECTED_COUNT ((int)(sizeof(g_Expected) / sizeof(g_Expected[0])))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Mapping +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Every video standard maps to its I/O standard, and the sub-value is the I/O
// configuration code whose name is the video standard's own name.
DTL_TEST(EveryStandardMapsAsDtapiDoes)
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
            DTL_FAIL("%s: result 0x%X", E->Name, Result);
        if (Value != E->IoStd)
            DTL_FAIL("%s: value %d, expected %d", E->Name, Value, E->IoStd);
        if (SubValue != E->VidStd)
            DTL_FAIL("%s: sub-value %d, expected %d", E->Name, SubValue, E->VidStd);

        DTL_ASSERT_OK(DtlIoConfigGetName(SubValue, Name, sizeof(Name)));
        if (strcmp(Name, E->Name) != 0)
            DTL_FAIL("%s: sub-value %d is named \"%s\"", E->Name, SubValue, Name);
    }
}

// Exactly the eleven 2160p standards count as 4K. Checked over a range well beyond every
// defined code, so that a stray case label for some other number would also show.
DTL_TEST(OnlyThe2160pStandardsAre4k)
{
    int Count = 0;
    int VidStd;

    for (VidStd = -10; VidStd < 1000; VidStd++)
    {
        if (DtlVidStdIs4k(VidStd))
            Count++;
    }

    DTL_ASSERT_EQ(Count, 11);
    DTL_ASSERT(DtlVidStdIs4k(DTAPI_VIDSTD_2160P60B));
    DTL_ASSERT(!DtlVidStdIs4k(DTAPI_VIDSTD_1080P60B));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Link standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A standard that is not 4K takes no link standard, and says so before anything else.
DTL_TEST(NonUhdStandardRefusesALinkStandard)
{
    int Link;
    int i;

    for (i = 0; i < EXPECTED_COUNT; i++)
    {
        if (DtlVidStdIs4k(g_Expected[i].VidStd))
            continue;

        for (Link = 0; Link <= 3; Link++)
        {
            int Value = 7;
            int SubValue = 7;

            if (DtapiVidStd2IoStd(g_Expected[i].VidStd, Link, &Value, &SubValue) !=
                DTAPI_E_INVALID_LINKSTD)
            {
                DTL_FAIL("%s with link %d was not refused", g_Expected[i].Name, Link);
            }
            DTL_ASSERT_EQ(Value, -1);
            DTL_ASSERT_EQ(SubValue, -1);
        }
    }
}

DTL_TEST(UhdStandardNeedsAValidLinkStandard)
{
    int Value;
    int SubValue;

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P50, -1, &Value, &SubValue),
                  DTAPI_E_INVALID_LINKSTD);
    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P50, 4, &Value, &SubValue),
                  DTAPI_E_INVALID_LINKSTD);
    DTL_ASSERT_EQ(Value, -1);
}

// Four-link 4K needs the per-link standard, which comes with milestone M4.
DTL_TEST(FourLinkUhdIsNotYetImplemented)
{
    int Value = 7;
    int SubValue = 7;

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P50, DTL_VIDLNK_4K_SMPTE425, &Value,
                                    &SubValue),
                  DTAPI_E_NOT_IMPLEMENTED);
    DTL_ASSERT_EQ(Value, -1);
    DTL_ASSERT_EQ(SubValue, -1);

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_2160P30, DTL_VIDLNK_4K_SMPTE425B, &Value,
                                    &SubValue),
                  DTAPI_E_NOT_IMPLEMENTED);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Refusals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(UnknownVideoStandardIsRefused)
{
    int Value = 7;
    int SubValue = 7;

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_UNKNOWN, -1, &Value, &SubValue),
                  DTAPI_E_INVALID_VIDSTD);
    DTL_ASSERT_EQ(Value, -1);
    DTL_ASSERT_EQ(SubValue, -1);

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(12345, -1, &Value, &SubValue),
                  DTAPI_E_INVALID_VIDSTD);
}

// With both the link standard and the video standard wrong, DTAPI reports the link
// standard, because it checks that first.
DTL_TEST(LinkStandardIsCheckedFirst)
{
    int Value;
    int SubValue;

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(12345, 0, &Value, &SubValue),
                  DTAPI_E_INVALID_LINKSTD);
}

// A null output pointer is refused before anything is written through the other one.
DTL_TEST(NullOutputIsRefusedWithoutWriting)
{
    int Kept = 7;

    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, NULL, &Kept),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(Kept, 7);
    DTL_ASSERT_EQ(DtapiVidStd2IoStd(DTAPI_VIDSTD_1080I50, -1, &Kept, NULL),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(Kept, 7);
}

DTL_TEST_MAIN("VidStd", DTL_RUN(EveryStandardMapsAsDtapiDoes),
              DTL_RUN(OnlyThe2160pStandardsAre4k),
              DTL_RUN(NonUhdStandardRefusesALinkStandard),
              DTL_RUN(UhdStandardNeedsAValidLinkStandard),
              DTL_RUN(FourLinkUhdIsNotYetImplemented),
              DTL_RUN(UnknownVideoStandardIsRefused), DTL_RUN(LinkStandardIsCheckedFirst),
              DTL_RUN(NullOutputIsRefusedWithoutWriting))

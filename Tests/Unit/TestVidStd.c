// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestVidStd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for video standard properties and the I/O standard mapping
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h"     // Function under test and constants.
#include "DtIoConfig.h"     // Names of I/O configuration codes.
#include "DtTest.h"         // Test framework.
#include "SdiFormat.h"      // Line timing of every standard.
#include "Video/DtVidStd.h" // Functions under test.

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
        DtapiResult Result = DtapiVidStd2IoStd(E->VidStd, E->LinkStd, &Value, &SubValue);

        if (Result != DTAPI_OK)
            DT_FAIL("%s: result 0x%X", E->Name, Result);
        if (Value != E->IoStd)
            DT_FAIL("%s: value %d, expected %d", E->Name, Value, E->IoStd);
        if (SubValue != E->VidStd)
            DT_FAIL("%s: sub-value %d, expected %d", E->Name, SubValue, E->VidStd);

        DT_ASSERT_OK(DtIoConfig_GetName(SubValue, Name, sizeof(Name)));
        if (strcmp(Name, E->Name) != 0)
            DT_FAIL("%s: sub-value %d is named \"%s\"", E->Name, SubValue, Name);
    }
}

// Exactly the eleven 2160p standards count as 4K. Checked over a range well beyond every
// defined code, so that a stray case label for some other number would also show.
DT_TEST(OnlyThe2160pStandardsAre4k)
{
    int Count = 0;

    for (int VidStd = -10; VidStd < 1000; VidStd++)
    {
        if (DtVidStd_Is4k(VidStd))
            Count++;
    }

    DT_ASSERT_EQ(Count, 11);
    DT_ASSERT(DtVidStd_Is4k(DTAPI_VIDSTD_2160P60B));
    DT_ASSERT(!DtVidStd_Is4k(DTAPI_VIDSTD_1080P60B));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Link standard +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A standard that is not 4K takes no link standard, and says so before anything else.
DT_TEST(NonUhdStandardRefusesALinkStandard)
{
    for (int i = 0; i < EXPECTED_COUNT; i++)
    {
        if (DtVidStd_Is4k(g_Expected[i].VidStd))
            continue;

        int Link;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OneLinkOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The format of one link of a 2160p format, found from the line timing: the 1080-line
// progressive format that is not 2160p, with the same rate and level.
//
static const SdiFormat* OneLinkOf(const SdiFormat* Uhd)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];

        if (!DtVidStd_Is4k(Format->VidStd) && Format->Lines == 1125 &&
            Format->Scan == SDI_SCAN_P && Format->FpsNum == Uhd->FpsNum &&
            Format->FpsDen == Uhd->FpsDen &&
            SdiFormat_IsLevelB(Format) == SdiFormat_IsLevelB(Uhd))
        {
            return Format;
        }
    }
    return NULL;
}

// A 2160p standard that is not on the single link of its rate, 6G up to 30 frames and 12G
// from 50, is configured as the standard of one link: the 1080p standard of its rate, on
// HD-SDI or 3G-SDI. That includes a 50 Hz standard on 6G and a 30 Hz one on 12G.
DT_TEST(FourLinkUhdTakesTheStandardOfOneLink)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];

        if (!DtVidStd_Is4k(Format->VidStd))
            continue;
        const SdiFormat* Link1 = OneLinkOf(Format);
        if (Link1 == NULL)
            DT_FAIL("%s: no format of one link", Format->Name);
        bool High = SdiFormat_Fps(Format) >= 50.0;

        int Link;
        for (Link = DT_VIDLNK_4K_SMPTE425; Link <= DT_VIDLNK_4K_SMPTE2082; Link++)
        {
            int Value = 7;
            int SubValue = 7;

            if (Link == (High ? DT_VIDLNK_4K_SMPTE2082 : DT_VIDLNK_4K_SMPTE2081))
                continue;

            SDI_ASSERT_EQ(Format,
                          DtapiVidStd2IoStd(Format->VidStd, Link, &Value, &SubValue),
                          DTAPI_OK);
            SDI_ASSERT_EQ(Format, Value,
                          High ? DTAPI_IOCONFIG_3GSDI : DTAPI_IOCONFIG_HDSDI);
            SDI_ASSERT_EQ(Format, SubValue, Link1->VidStd);
        }
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standard properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A standard that is not 2160p is its own link; a 2160p standard needs a link standard
// and has the frame of one link.
DT_TEST(PropertiesHoldTheFrameOfOneLink)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtVidStdProps Props;

        if (!DtVidStd_Is4k(Format->VidStd))
        {
            SDI_ASSERT_EQ(Format, DtVidStdProps_Init(&Props, Format->VidStd, -1), true);
            SDI_ASSERT_EQ(Format, Props.VidStd, Format->VidStd);
            SDI_ASSERT_EQ(Format, Props.LinkStd, DT_VIDLNK_NONE);
            SDI_ASSERT_EQ(Format, Props.Frame.VidStd, Format->VidStd);
            continue;
        }

        SDI_ASSERT_EQ(Format, DtVidStdProps_Init(&Props, Format->VidStd, -1), false);
        SDI_ASSERT_EQ(Format, Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
        SDI_ASSERT_EQ(Format, Props.Frame.VidStd, DTAPI_VIDSTD_UNKNOWN);

        int Link;
        for (Link = DT_VIDLNK_4K_SMPTE425; Link <= DT_VIDLNK_4K_SMPTE2082; Link++)
        {
            SDI_ASSERT_EQ(Format, DtVidStdProps_Init(&Props, Format->VidStd, Link), true);
            SDI_ASSERT_EQ(Format, Props.VidStd, Format->VidStd);
            SDI_ASSERT_EQ(Format, Props.LinkStd, Link);
            SDI_ASSERT_EQ(Format, Props.Frame.VidStd, OneLinkOf(Format)->VidStd);
            SDI_ASSERT_EQ(Format, DtFrameProps_NumLines(&Props.Frame), Format->Lines);
        }
    }
}

// An unknown video or link standard gives invalid properties. A link standard on a
// standard that is not 2160p is accepted, as DTAPI does.
DT_TEST(PropertiesRefuseUnknownStandards)
{
    DtVidStdProps Props;

    DT_ASSERT(!DtVidStdProps_Init(&Props, DTAPI_VIDSTD_UNKNOWN, -1));
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT(!DtVidStdProps_Init(&Props, 12345, -1));
    DT_ASSERT(!DtVidStdProps_Init(&Props, DTAPI_VIDSTD_2160P50, 4));
    DT_ASSERT_EQ(Props.LinkStd, DT_VIDLNK_NONE);
    DT_ASSERT(!DtVidStdProps_Init(&Props, DTAPI_VIDSTD_1080P50, -2));
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);

    DT_ASSERT(DtVidStdProps_Init(&Props, DTAPI_VIDSTD_1080P50, DT_VIDLNK_4K_SMPTE425));
    DT_ASSERT_EQ(Props.LinkStd, DT_VIDLNK_4K_SMPTE425);
}

DT_TEST(PhysicalLinks)
{
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(DT_VIDLNK_NONE), 1);
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(DT_VIDLNK_4K_SMPTE425), 4);
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(DT_VIDLNK_4K_SMPTE425B), 4);
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(DT_VIDLNK_4K_SMPTE2081), 1);
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(DT_VIDLNK_4K_SMPTE2082), 1);
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(4), 0);
    DT_ASSERT_EQ(DtVidStd_NumPhysicalLinks(-2), 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinkOfPayload -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The link standard of a 2160p payload: 6G, 12G, or four links of either level.
//
static int LinkOfPayload(int Payload)
{
    switch (Payload)
    {
    case 0xC0:
        return DT_VIDLNK_4K_SMPTE2081;
    case 0xCE:
        return DT_VIDLNK_4K_SMPTE2082;
    case 0x97:
    case 0x98:
        return DT_VIDLNK_4K_SMPTE425;
    default:
        return DT_VIDLNK_NONE;
    }
}

// The VPID of every standard gives that standard, and for 2160p how it is carried.
DT_TEST(VpidGivesEveryStandard)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtVidStdProps Props;

        DtVidStdProps_FromSmpte352(&Props, SdiFormat_Vpid(Format));
        SDI_ASSERT_EQ(Format, Props.VidStd, Format->VidStd);
        SDI_ASSERT_EQ(Format, Props.LinkStd, LinkOfPayload(Format->Payload));
    }
}

// Four level-A links carry every 2160p rate of level A.
DT_TEST(VpidOfFourLevelALinks)
{
    DtVidStdProps Props;

    DtVidStdProps_FromSmpte352(&Props, 0x0080C297);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_2160P23_98);
    DT_ASSERT_EQ(Props.LinkStd, DT_VIDLNK_4K_SMPTE425);
    DtVidStdProps_FromSmpte352(&Props, 0x4080C797);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_2160P30);
    DtVidStdProps_FromSmpte352(&Props, 0xC080CB97);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_2160P60);
    DT_ASSERT_EQ(Props.LinkStd, DT_VIDLNK_4K_SMPTE425);
}

// A VPID outside what its payload allows gives no standard, and then no link standard.
// SD looks at the rate only, so its scan bits do not matter.
DT_TEST(VpidOutsideItsPayloadGivesNothing)
{
    static const uint32_t NoStandard[] = {
        0x00000584, // 720 lines, interlaced
        0x00008584, // 720 lines, interlaced picture in a progressive transport
        0x0000C884, // 720 lines at 48 frames
        0x00000985, // 1080 lines on HD, interlaced at 50 frames
        0x0000C985, // 1080 lines on HD, progressive at 50 frames
        0x00000385, // 1080 lines on HD, interlaced at 24 frames
        0x00008585, // 1080 lines on HD, interlaced picture in a progressive transport
        0x00000989, // 3G level A, interlaced
        0x0000C589, // 3G level A at 25 frames
        0x0000408A, // 3G level B, no rate
        0x00000381, // SD at 24 frames
        0x0000C9C0, // 6G at 50 frames
        0x0000C798, // Four level-B links at 30 frames
        0x0000C7CE, // 12G at 30 frames
        0x00000BCE, // 12G, interlaced
        0x0000C586, // An unknown payload
        0x00000000,
    };
    DtVidStdProps Props;

    for (size_t i = 0; i < sizeof(NoStandard) / sizeof(NoStandard[0]); i++)
    {
        DtVidStdProps_FromSmpte352(&Props, NoStandard[i]);
        if (Props.VidStd != DTAPI_VIDSTD_UNKNOWN || Props.LinkStd != DT_VIDLNK_NONE)
            DT_FAIL("VPID 0x%08X gave %d, link %d", (unsigned)NoStandard[i], Props.VidStd,
                    Props.LinkStd);
    }

    DtVidStdProps_FromSmpte352(&Props, 0x0000C581);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_625I50);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DeduceFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void DeduceFormat(DtVidStdProps* Props, const SdiFormat* Format, double Fps,
                         uint32_t Vpid)
{
    DtVidStdProps_Deduce(Props, Format->LinesF1, SdiFormat_LinesF2(Format),
                         SdiFormat_HancSymbols(Format), SdiFormat_VancSymbols(Format),
                         Fps, SdiFormat_IsLevelB(Format), Vpid, Format->SdiRate);
}

// With its VPID every standard is found exactly, with how it is carried.
DT_TEST(DeduceWithVpidFindsEveryStandard)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtVidStdProps Props;

        DeduceFormat(&Props, Format, SdiFormat_Fps(Format), SdiFormat_Vpid(Format));
        SDI_ASSERT_EQ(Format, Props.VidStd, Format->VidStd);
        SDI_ASSERT_EQ(Format, Props.LinkStd, LinkOfPayload(Format->Payload));
    }
}

// Without a VPID the counters decide, and a 2160p standard is on the one link of its
// SDI rate.
DT_TEST(DeduceWithoutVpidTakesTheRateForTheLink)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        int Link = DT_VIDLNK_NONE;

        if (Format->SdiRate == DT_SDIRATE_6G)
            Link = DT_VIDLNK_4K_SMPTE2081;
        else if (Format->SdiRate == DT_SDIRATE_12G)
            Link = DT_VIDLNK_4K_SMPTE2082;

        DtVidStdProps Props;
        DeduceFormat(&Props, Format, SdiFormat_Fps(Format), 0);
        SDI_ASSERT_EQ(Format, Props.VidStd, Format->NoVpid);
        SDI_ASSERT_EQ(Format, Props.LinkStd, Link);
    }
}

// A VPID whose standard has the counters' geometry decides, even without a frame rate;
// one of another geometry is left to the search.
DT_TEST(DeduceTrustsAVpidThatFitsTheCounters)
{
    const SdiFormat* Sd = &g_SdiFormats[1];
    const SdiFormat* P25 = &g_SdiFormats[12];
    const SdiFormat* I50 = &g_SdiFormats[20];

    DT_ASSERT_EQ(Sd->VidStd, DTAPI_VIDSTD_625I50);
    DT_ASSERT_EQ(P25->VidStd, DTAPI_VIDSTD_1080P25);
    DT_ASSERT_EQ(I50->VidStd, DTAPI_VIDSTD_1080I50);

    DtVidStdProps Props;
    DeduceFormat(&Props, P25, 0.0, SdiFormat_Vpid(P25));
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_1080P25);

    DeduceFormat(&Props, I50, 25.0, SdiFormat_Vpid(Sd));
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_1080I50);
}

// 1080p counters at 50 frames with a 3G level-B payload that does not decode match no
// 1080p standard, level A for its payload and level B for the level flag, but they match
// 2160p50. At 3G that has no link standard, so nothing is found. At 6G the rate gives the
// link standard of 30 frames and less, which is accepted.
DT_TEST(Deduce2160pNeedsARateWithALink)
{
    DtFrameProps Frame;

    DtFrameProps_Deduce(&Frame, 1125, 0, 1440, 3840, 50.0, false, 0x0000C08A,
                        DT_SDIRATE_3G);
    DT_ASSERT_EQ(Frame.VidStd, DTAPI_VIDSTD_2160P50);
    DtVidStdProps Props;
    DtVidStdProps_Deduce(&Props, 1125, 0, 1440, 3840, 50.0, false, 0x0000C08A,
                         DT_SDIRATE_3G);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(Props.LinkStd, DT_VIDLNK_NONE);

    DtVidStdProps_Deduce(&Props, 1125, 0, 1440, 3840, 50.0, false, 0, DT_SDIRATE_6G);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_2160P50);
    DT_ASSERT_EQ(Props.LinkStd, DT_VIDLNK_4K_SMPTE2081);
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

DT_TEST_MAIN(
    "VidStd", DT_RUN(EveryStandardMapsAsDtapiDoes), DT_RUN(OnlyThe2160pStandardsAre4k),
    DT_RUN(NonUhdStandardRefusesALinkStandard),
    DT_RUN(UhdStandardNeedsAValidLinkStandard),
    DT_RUN(FourLinkUhdTakesTheStandardOfOneLink), DT_RUN(UnknownVideoStandardIsRefused),
    DT_RUN(LinkStandardIsCheckedFirst), DT_RUN(NullOutputIsRefusedWithoutWriting),
    DT_RUN(PropertiesHoldTheFrameOfOneLink), DT_RUN(PropertiesRefuseUnknownStandards),
    DT_RUN(PhysicalLinks), DT_RUN(VpidGivesEveryStandard), DT_RUN(VpidOfFourLevelALinks),
    DT_RUN(VpidOutsideItsPayloadGivesNothing), DT_RUN(DeduceWithVpidFindsEveryStandard),
    DT_RUN(DeduceWithoutVpidTakesTheRateForTheLink),
    DT_RUN(DeduceTrustsAVpidThatFitsTheCounters), DT_RUN(Deduce2160pNeedsARateWithALink))

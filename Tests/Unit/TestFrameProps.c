// #*#*#*#*#*#*#*#*#*#*#*#*#* TestFrameProps.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for SDI frame geometry and deducing a frame from its counters
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"             // Test framework.
#include "SdiFormat.h"          // Line timing of every standard.
#include "Video/DtFrameProps.h" // Functions under test.
#include "Video/DtVidStd.h"     // 4K classification.
#include "cdtapi.h"             // DTAPI_VIDSTD_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Geometry +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Every standard has the lines, fields, rate and line widths of its SMPTE line timing. A
// 2160p standard has those of the 1080p frame one link carries.
DT_TEST(InitGivesTheLineTimingOfEveryStandard)
{
    DT_ASSERT_EQ(SDI_FORMAT_COUNT, 40);

    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        DtFrameProps Props;
        int NumActive = 0;

        if (!DtFrameProps_Init(&Props, Format->VidStd))
            DT_FAIL("%s: not initialised", Format->Name);

        SDI_ASSERT_EQ(Format, Props.VidStd, Format->VidStd);
        SDI_ASSERT_EQ(Format, Props.FpsNum, Format->FpsNum);
        SDI_ASSERT_EQ(Format, Props.FpsDen, Format->FpsDen);
        SDI_ASSERT_EQ(Format, Props.NumFields, Format->Scan == SDI_SCAN_P ? 1 : 2);
        SDI_ASSERT_EQ(Format, DtFrameProps_NumLines(&Props), Format->Lines);
        SDI_ASSERT_EQ(Format, Props.Fields[0].EndLine - Props.Fields[0].StartLine + 1,
                      Format->LinesF1);
        SDI_ASSERT_EQ(Format, DtFrameProps_LineNumSymHancInclTiming(&Props),
                      SdiFormat_HancSymbols(Format));
        SDI_ASSERT_EQ(Format, Props.LineNumSymActive, SdiFormat_ActiveSymbols(Format));

        // The fields are numbered from line 1 without a gap, and hold the active lines.
        const DtFieldProps* Last = &Props.Fields[Props.NumFields - 1];
        SDI_ASSERT_EQ(Format, Props.Fields[0].StartLine, 1);
        SDI_ASSERT_EQ(Format, Last->EndLine, Format->Lines);
        if (Props.NumFields == 2)
            SDI_ASSERT_EQ(Format, Props.Fields[1].StartLine, Props.Fields[0].EndLine + 1);
        for (int f = 0; f < Props.NumFields; f++)
        {
            const DtFieldProps* Field = &Props.Fields[f];

            DT_ASSERT(Field->StartLine < Field->ActiveStartLine);
            DT_ASSERT(Field->ActiveEndLine < Field->EndLine);
            DT_ASSERT(Field->SwitchingLine >= Field->StartLine);
            DT_ASSERT(Field->SwitchingLine < Field->ActiveStartLine);
            NumActive += Field->ActiveEndLine - Field->ActiveStartLine + 1;
        }
        SDI_ASSERT_EQ(Format, NumActive, Format->ActiveLines);
    }
}

// EAV and SAV are 4 symbols each in SD, and 8 and 4 samples in HD, counted twice.
DT_TEST(TimingReferenceSignalsHaveTheirSize)
{
    DtFrameProps Props;

    DT_ASSERT(DtFrameProps_Init(&Props, DTAPI_VIDSTD_625I50));
    DT_ASSERT_EQ(Props.LineNumSymEav, 4);
    DT_ASSERT_EQ(Props.LineNumSymSav, 4);

    DT_ASSERT(DtFrameProps_Init(&Props, DTAPI_VIDSTD_720P50));
    DT_ASSERT_EQ(Props.LineNumSymEav, 16);
    DT_ASSERT_EQ(Props.LineNumSymSav, 8);

    DT_ASSERT(DtFrameProps_Init(&Props, DTAPI_VIDSTD_2160P60B));
    DT_ASSERT_EQ(Props.LineNumSymEav, 16);
    DT_ASSERT_EQ(Props.LineNumSymSav, 8);
}

// Anything that is not a standard has no properties and no rate.
DT_TEST(InitRefusesWhatIsNoStandard)
{
    static const int NoStandards[] = {DTAPI_VIDSTD_UNKNOWN, 0, 12345, -5};
    DtFrameProps Props;
    int Num;
    int Den;

    for (size_t i = 0; i < sizeof(NoStandards) / sizeof(NoStandards[0]); i++)
    {
        Props.VidStd = DTAPI_VIDSTD_1080I50;
        DT_ASSERT(!DtFrameProps_Init(&Props, NoStandards[i]));
        DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
        DT_ASSERT_EQ(DtFrameProps_NumLines(&Props), 0);

        DtVidStd_FrameRate(NoStandards[i], &Num, &Den);
        DT_ASSERT_EQ(Num, 0);
        DT_ASSERT_EQ(Den, 1);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// SD is up to 625 lines; 3G is 1080 lines at 50 frames and up, except 2160p; level B is
// what its payload says; PsF counts as interlaced.
DT_TEST(ClassificationFollowsTheLineTiming)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        bool Is3g = Format->Lines == 1125 && SdiFormat_Fps(Format) >= 50.0 &&
                    !DtVidStd_Is4k(Format->VidStd);
        DtFrameProps Props;

        DtFrameProps_Init(&Props, Format->VidStd);
        SDI_ASSERT_EQ(Format, DtFrameProps_IsSd(&Props), Format->Lines <= 625);
        SDI_ASSERT_EQ(Format, DtFrameProps_IsHd(&Props), Format->Lines > 625);
        SDI_ASSERT_EQ(Format, DtFrameProps_Is3g(&Props), Is3g);
        SDI_ASSERT_EQ(Format, DtFrameProps_Is3gLevelB(&Props),
                      Is3g && SdiFormat_IsLevelB(Format));
        SDI_ASSERT_EQ(Format, DtFrameProps_IsInterlaced(&Props),
                      Format->Scan != SDI_SCAN_P);
        SDI_ASSERT_EQ(Format, DtFrameProps_IsPsF(&Props), Format->Scan == SDI_SCAN_S);
    }
}

DT_TEST(InvalidPropertiesHaveNoClass)
{
    DtFrameProps Props;

    DtFrameProps_Init(&Props, DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT(!DtFrameProps_IsSd(&Props));
    DT_ASSERT(!DtFrameProps_IsHd(&Props));
    DT_ASSERT(!DtFrameProps_Is3g(&Props));
    DT_ASSERT(!DtFrameProps_Is3gLevelB(&Props));
    DT_ASSERT(!DtFrameProps_IsInterlaced(&Props));
    DT_ASSERT(!DtFrameProps_IsPsF(&Props));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Deduction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DeduceFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Deduces a frame from what a receiver reports for Format, with the given frame rate and
// VPID.
//
static int DeduceFormat(const SdiFormat* Format, double Fps, uint32_t Vpid)
{
    DtFrameProps Props;

    DtFrameProps_Deduce(&Props, Format->LinesF1, SdiFormat_LinesF2(Format),
                        SdiFormat_HancSymbols(Format), SdiFormat_ActiveSymbols(Format),
                        Fps, SdiFormat_IsLevelB(Format), Vpid, Format->SdiRate);
    return Props.VidStd;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FormatNamed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const SdiFormat* FormatNamed(int VidStd)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        if (g_SdiFormats[i].VidStd == VidStd)
            return &g_SdiFormats[i];
    }
    return NULL;
}

// From the counters alone every standard is found, except what the counters cannot
// show: PsF at 25 frames and up is taken as interlaced, and four level-B links as one 3G
// level-B link.
DT_TEST(CountersAloneGiveTheStandard)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];

        SDI_ASSERT_EQ(Format, DeduceFormat(Format, SdiFormat_Fps(Format), 0),
                      Format->NoVpid);
    }
}

// The frame properties found are those of the standard found.
DT_TEST(DeducedPropertiesAreComplete)
{
    DtFrameProps Deduced;
    const SdiFormat* Format = FormatNamed(DTAPI_VIDSTD_1080I59_94);

    DtFrameProps_Deduce(&Deduced, Format->LinesF1, SdiFormat_LinesF2(Format),
                        SdiFormat_HancSymbols(Format), SdiFormat_ActiveSymbols(Format),
                        SdiFormat_Fps(Format), false, 0, DT_SDIRATE_HD);
    DtFrameProps Expected;
    DtFrameProps_Init(&Expected, DTAPI_VIDSTD_1080I59_94);
    DT_ASSERT_MEM(&Deduced, &Expected, sizeof(Expected));
}

// A VPID with the transport bits of each scan picks it among standards with the same
// counters; the 2160p payloads at 6G and 12G give 2160p.
DT_TEST(VpidSeparatesStandardsWithTheSameCounters)
{
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* Format = &g_SdiFormats[i];
        int Expected = Format->VidStd;

        // A link of four level-B links carries payload 0x98, which this search's level-B
        // test, looking for the single-link 0x8A, does not take: the level-A 2160p
        // standard of the same rate is found instead.
        if (Format->VidStd == DTAPI_VIDSTD_2160P50B)
            Expected = DTAPI_VIDSTD_2160P50;
        else if (Format->VidStd == DTAPI_VIDSTD_2160P59_94B)
            Expected = DTAPI_VIDSTD_2160P59_94;
        else if (Format->VidStd == DTAPI_VIDSTD_2160P60B)
            Expected = DTAPI_VIDSTD_2160P60;

        SDI_ASSERT_EQ(Format,
                      DeduceFormat(Format, SdiFormat_Fps(Format), SdiFormat_Vpid(Format)),
                      Expected);
    }
}

// The frame rate may be 500 ppm off, not more.
DT_TEST(FrameRateMayDeviate500Ppm)
{
    const SdiFormat* Format = FormatNamed(DTAPI_VIDSTD_1080I50);

    DT_ASSERT_EQ(DeduceFormat(Format, 25.0 * (1 + 490e-6), 0), DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0 * (1 - 490e-6), 0), DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0 * (1 + 510e-6), 0), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0 * (1 - 510e-6), 0), DTAPI_VIDSTD_UNKNOWN);
}

// Counters of no standard give none.
DT_TEST(UnknownCountersGiveNoStandard)
{
    DtFrameProps Props;

    DtFrameProps_Deduce(&Props, 1125, 0, 560, 3840, 25.0, false, 0, DT_SDIRATE_HD);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DtFrameProps_Deduce(&Props, 1124, 0, 560, 3840, 30.0, false, 0, DT_SDIRATE_HD);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DtFrameProps_Deduce(&Props, 1125, 0, 560, 3842, 30.0, false, 0, DT_SDIRATE_HD);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
    DtFrameProps_Deduce(&Props, 0, 0, 0, 0, 0.0, false, 0, DT_SDIRATE_UNKNOWN);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_UNKNOWN);
}

// At 6G and 12G only a 2160p standard is looked for, of level A whatever the level flag.
DT_TEST(RateAbove3gGives2160p)
{
    DtFrameProps Props;

    DtFrameProps_Deduce(&Props, 1125, 0, 1440, 3840, 50.0, true, 0, DT_SDIRATE_12G);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_2160P50);
    DtFrameProps_Deduce(&Props, 1125, 0, 1440, 3840, 50.0, false, 0, DT_SDIRATE_6G);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_2160P50);
    DtFrameProps_Deduce(&Props, 1125, 0, 560, 3840, 30.0, false, 0, DT_SDIRATE_3G);
    DT_ASSERT_EQ(Props.VidStd, DTAPI_VIDSTD_1080P30);
}

// When the search finds nothing, a VPID whose standard has the counters' geometry still
// gives that standard; for 2160p the frame of one link.
DT_TEST(VpidDecidesWhenTheRateIsMissing)
{
    const SdiFormat* Interlaced = FormatNamed(DTAPI_VIDSTD_1080I50);
    const SdiFormat* Uhd = FormatNamed(DTAPI_VIDSTD_2160P30);

    DT_ASSERT_EQ(DeduceFormat(Interlaced, 0.0, SdiFormat_Vpid(Interlaced)),
                 DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(DeduceFormat(Uhd, 0.0, SdiFormat_Vpid(Uhd)), DTAPI_VIDSTD_1080P30);

    // A VPID of another geometry does not.
    DT_ASSERT_EQ(
        DeduceFormat(Interlaced, 0.0, SdiFormat_Vpid(FormatNamed(DTAPI_VIDSTD_720P50))),
        DTAPI_VIDSTD_UNKNOWN);
}

// With a VPID the search never gives PsF: its PsF test wants scan bits 10, and its
// interlaced test, which PsF's two fields also meet, wants 00. PsF is found only when the
// VPID decodes. A VPID that does not decode, here for want of a rate, gives interlaced
// with scan bits 00 and nothing with any others.
DT_TEST(SearchNeverGivesPsfWithAVpid)
{
    const SdiFormat* Format = FormatNamed(DTAPI_VIDSTD_1080PSF25);

    DT_ASSERT_EQ(DeduceFormat(Format, 25.0, 0x00004085), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0, 0x00008085), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0, 0x0000C085), DTAPI_VIDSTD_UNKNOWN);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0, 0x00000085), DTAPI_VIDSTD_1080I50);
    DT_ASSERT_EQ(DeduceFormat(Format, 25.0, SdiFormat_Vpid(Format)),
                 DTAPI_VIDSTD_1080PSF25);
}

// Without a VPID, only PsF 23.98 and 24, which have no interlaced form, stay PsF.
DT_TEST(WithoutVpidPsfIsInterlaced)
{
    DT_ASSERT_EQ(DeduceFormat(FormatNamed(DTAPI_VIDSTD_1080PSF24), 24.0, 0),
                 DTAPI_VIDSTD_1080PSF24);
    DT_ASSERT_EQ(DeduceFormat(FormatNamed(DTAPI_VIDSTD_1080PSF30), 30.0, 0),
                 DTAPI_VIDSTD_1080I60);
}

DT_TEST_MAIN("FrameProps", DT_RUN(InitGivesTheLineTimingOfEveryStandard),
             DT_RUN(TimingReferenceSignalsHaveTheirSize),
             DT_RUN(InitRefusesWhatIsNoStandard),
             DT_RUN(ClassificationFollowsTheLineTiming),
             DT_RUN(InvalidPropertiesHaveNoClass), DT_RUN(CountersAloneGiveTheStandard),
             DT_RUN(DeducedPropertiesAreComplete),
             DT_RUN(VpidSeparatesStandardsWithTheSameCounters),
             DT_RUN(FrameRateMayDeviate500Ppm), DT_RUN(UnknownCountersGiveNoStandard),
             DT_RUN(RateAbove3gGives2160p), DT_RUN(VpidDecidesWhenTheRateIsMissing),
             DT_RUN(SearchNeverGivesPsfWithAVpid), DT_RUN(WithoutVpidPsfIsInterlaced))

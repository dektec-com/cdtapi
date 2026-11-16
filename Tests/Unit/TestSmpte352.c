// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSmpte352.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the fields of a SMPTE ST 352 payload identifier
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"           // Test framework.
#include "Video/DtSmpte352.h" // Functions under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Payload fields +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Byte 1 is the payload identifier, whatever the other bytes hold.
DT_TEST(PayloadIdIsTheFirstByte)
{
    DT_ASSERT_EQ(DtSmpte352_PayloadId(0x00000085), 0x85);
    DT_ASSERT_EQ(DtSmpte352_PayloadId(0xFFFFFFCE), 0xCE);
    DT_ASSERT_EQ(DtSmpte352_PayloadId(0xFFFFFF00), 0);
}

// The picture rate codes of SMPTE ST 352 table 2 that the library knows; the others,
// reserved or not supported, give 0/0. Only byte 2 bits 3..0 count.
DT_TEST(PictureRateCodes)
{
    static const struct
    {
        uint32_t Code;
        int Num;
        int Den;
    } Rates[] = {
        {0x0, 0, 0},        {0x1, 0, 0},  {0x2, 24000, 1001}, {0x3, 24, 1},
        {0x4, 48000, 1001}, {0x5, 25, 1}, {0x6, 30000, 1001}, {0x7, 30, 1},
        {0x8, 48, 1},       {0x9, 50, 1}, {0xA, 60000, 1001}, {0xB, 60, 1},
        {0xC, 0, 0},        {0xD, 0, 0},  {0xE, 0, 0},        {0xF, 0, 0},
    };

    for (size_t i = 0; i < sizeof(Rates) / sizeof(Rates[0]); i++)
    {
        int Num = -1;
        int Den = -1;

        DtSmpte352_PictureRate(0xFFFFF0FF | (Rates[i].Code << 8), &Num, &Den);
        if (Num != Rates[i].Num || Den != Rates[i].Den)
            DT_FAIL("code 0x%X: %d/%d", (unsigned)Rates[i].Code, Num, Den);
    }
}

// Byte 2 bit 7 is the transport and bit 6 the picture; a set bit means progressive.
DT_TEST(ScanBits)
{
    DT_ASSERT(DtSmpte352_IsInterlacedTransport(0x00000085));
    DT_ASSERT(DtSmpte352_IsInterlacedStructure(0x00000085));

    DT_ASSERT(DtSmpte352_IsInterlacedTransport(0x00004085));
    DT_ASSERT(!DtSmpte352_IsInterlacedStructure(0x00004085));

    DT_ASSERT(!DtSmpte352_IsInterlacedTransport(0x0000C085));
    DT_ASSERT(!DtSmpte352_IsInterlacedStructure(0x0000C085));

    DT_ASSERT(!DtSmpte352_IsInterlacedTransport(0xFFFFBFFF));
    DT_ASSERT(DtSmpte352_IsInterlacedStructure(0xFFFFBFFF));
}

// The aspect ratio is on the bit its payload's standard puts it: byte 3 bit 7 in SD (ST
// 352 Annex B.1), where 0 is 4:3; bit 5 in 1080 lines on HD (ST 292-1 section 9.5.2) and
// on 3G level B (ST 372 section 7.1); bit 7 in 3G level A (ST 425-1 section 4.1.6.2.2)
// and 2160p (ST 2081-10 and 2082-10 section 4.3.3). Outside SD a 0 is unknown. 720p has
// no such bit (ST 292-1 section 9.4) and is 16:9. In 85 and 8A bit 7 is the colorimetry,
// not the aspect ratio. An unknown payload is unknown.
DT_TEST(AspectRatioPerPayload)
{
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00800081), DT_AR_16_9);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00000081), DT_AR_4_3);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00000084), DT_AR_16_9);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00200085), DT_AR_16_9);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00800085), DT_AR_UNKNOWN);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x0020008A), DT_AR_16_9);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x0080008A), DT_AR_UNKNOWN);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00800089), DT_AR_16_9);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00200089), DT_AR_UNKNOWN);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x008000C0), DT_AR_16_9);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x000000CE), DT_AR_UNKNOWN);
    DT_ASSERT_EQ(DtSmpte352_AspectRatio(0x00800042), DT_AR_UNKNOWN);
}

// Byte 4 numbers the link: bits 7..6 on four level-A links, bits 7..5 on 6G and 12G; on
// four level-B links bits 7..5 number the channel, two to a link. Single-link payloads,
// 3G level B with its channel bit included, are link 0.
DT_TEST(LinkNumber)
{
    uint32_t Link;

    for (Link = 0; Link < 4; Link++)
        DT_ASSERT_EQ(DtSmpte352_LinkNumber(0x1F00CA97 | (Link << 30)), Link);
    for (Link = 0; Link < 8; Link++)
    {
        DT_ASSERT_EQ(DtSmpte352_LinkNumber(0x1F00CA98 | (Link << 29)), Link / 2);
        DT_ASSERT_EQ(DtSmpte352_LinkNumber(0x1F00C7C0 | (Link << 29)), Link);
        DT_ASSERT_EQ(DtSmpte352_LinkNumber(0x1F00CACE | (Link << 29)), Link);
    }

    DT_ASSERT_EQ(DtSmpte352_LinkNumber(0xC000CA8A), 0);
    DT_ASSERT_EQ(DtSmpte352_LinkNumber(0xE000C589), 0);
    DT_ASSERT_EQ(DtSmpte352_LinkNumber(0xE0000585), 0);
    DT_ASSERT_EQ(DtSmpte352_LinkNumber(0xFFFFFF00), 0);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Signalling +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns a whole signalling of the five values.
static DtSdiVideoSignalling MakeSig(DtSdiTransfer Transfer, DtSdiColorimetry Colorimetry,
                                    DtSdiAspect Aspect, DtSdiLumaChroma LumaChroma,
                                    DtSdiRange Range)
{
    DtSdiVideoSignalling Signalling;
    memset(&Signalling, 0, sizeof(Signalling));
    Signalling.Size = (int)sizeof(Signalling);
    Signalling.Transfer = Transfer;
    Signalling.Colorimetry = Colorimetry;
    Signalling.Aspect = Aspect;
    Signalling.LumaChroma = LumaChroma;
    Signalling.Range = Range;
    return Signalling;
}

// The payload IDs of the standards' defaults, worked out by hand from the standards:
// 625i50 81 05 00 01 (4:3); 525i59.94 81 06 00 01; 720p50 84 49 00 01, no fields;
// 1080i50 85 05 20 01, 16:9 in byte 3 bit 5; 1080p50 89 C9 80 01, 16:9 in bit 7;
// 1080p50 level B 8A 49 20 01; 2160p25 on 6G C0 C5 A0 01 and 2160p50 on 12G CE C9 A0 01,
// BT.2020 (2h in bits 5..4) and 16:9.
DT_TEST(DefaultPayloadIds)
{
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_625I50), 0x01000581u);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_525I59_94), 0x01000681u);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_720P50), 0x01004984u);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_1080I50), 0x01200585u);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_1080P50), 0x0180C989u);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_1080P50B), 0x0120498Au);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_2160P25), 0x01A0C5C0u);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_2160P50), 0x01A0C9CEu);
}

// Every field on its bits, by hand: HLG (1h in byte 2 bits 5..4), BT.2020, 16:9, ICtCp
// (byte 4 bit 4) and full range, which is 3h in ST 292-1 section 9.5.3 and 0h in the
// others. 1080i50: 85 15 A0 13, colorimetry 2h in byte 3 bits 7,4 and 16:9 in bit 5.
// 1080p50: 89 D9 A0 10. PQ with unknown colorimetry and aspect ratio, Y'CbCr and narrow:
// 1080i50 85 25 90 01, 2160p50 CE E9 30 01. 16:9 in SD: 81 05 80 01.
DT_TEST(SignallingBits)
{
    const DtSdiVideoSignalling Hlg =
        MakeSig(DT_SDI_TRANSFER_HLG, DT_SDI_COLORIMETRY_BT2020, DT_SDI_ASPECT_16X9,
                DT_SDI_LUMACHROMA_ICTCP, DT_SDI_RANGE_FULL);
    DT_ASSERT_EQ(DtSmpte352_MakeSignalling(DTAPI_VIDSTD_1080I50, &Hlg), 0x13A01585u);
    DT_ASSERT_EQ(DtSmpte352_MakeSignalling(DTAPI_VIDSTD_1080P50, &Hlg), 0x10A0D989u);

    const DtSdiVideoSignalling Pq =
        MakeSig(DT_SDI_TRANSFER_PQ, DT_SDI_COLORIMETRY_UNKNOWN, DT_SDI_ASPECT_UNKNOWN,
                DT_SDI_LUMACHROMA_YCBCR, DT_SDI_RANGE_NARROW);
    DT_ASSERT_EQ(DtSmpte352_MakeSignalling(DTAPI_VIDSTD_1080I50, &Pq), 0x01902585u);
    DT_ASSERT_EQ(DtSmpte352_MakeSignalling(DTAPI_VIDSTD_2160P50, &Pq), 0x0130E9CEu);

    const DtSdiVideoSignalling Wide =
        MakeSig(DT_SDI_TRANSFER_SDR, DT_SDI_COLORIMETRY_BT601, DT_SDI_ASPECT_16X9,
                DT_SDI_LUMACHROMA_YCBCR, DT_SDI_RANGE_NARROW);
    DT_ASSERT_EQ(DtSmpte352_MakeSignalling(DTAPI_VIDSTD_625I50, &Wide), 0x01800581u);
}

// Checks that the signalling Want, made on standard VidStd, reads back as it was.
static void CheckRoundTrip(int* DtFailures, int VidStd, const DtSdiVideoSignalling* Want)
{
    DtSdiVideoSignalling Read;
    const uint32_t Vpid = DtSmpte352_MakeSignalling(VidStd, Want);
    DT_ASSERT(DtSmpte352_ReadSignalling(Vpid, &Read));
    if (memcmp(&Read, Want, sizeof(Read)) != 0)
        DT_FAIL("std %d %d/%d/%d/%d/%d: VPID %08X", VidStd, (int)Want->Transfer,
                (int)Want->Colorimetry, (int)Want->Aspect, (int)Want->LumaChroma,
                (int)Want->Range, (unsigned)Vpid);
}

// What is made is read back, for every value a standard can carry, on every payload the
// builder makes: 81, 84, 85 (interlaced and progressive), 89, 8A, C0 and CE. Signalling
// the standard cannot carry is one ResolveSignalling refuses.
DT_TEST(SignallingRoundTrip)
{
    static const int Stds[] = {DTAPI_VIDSTD_625I50,   DTAPI_VIDSTD_720P50,
                               DTAPI_VIDSTD_1080I50,  DTAPI_VIDSTD_1080P50,
                               DTAPI_VIDSTD_1080P50B, DTAPI_VIDSTD_2160P25,
                               DTAPI_VIDSTD_2160P50};
    DtSdiVideoSignalling None;
    DT_ASSERT_OK(DtSmpte352_TakeSignalling(NULL, &None));
    int Checked = 0;
    for (size_t s = 0; s < sizeof(Stds) / sizeof(Stds[0]); s++)
    {
        for (int Code = 0; Code < 4 * 5 * 3 * 2 * 2; Code++)
        {
            const DtSdiVideoSignalling Want = MakeSig(
                (DtSdiTransfer)(1 + Code % 4), (DtSdiColorimetry)(1 + Code / 4 % 5),
                (DtSdiAspect)(1 + Code / 20 % 3), (DtSdiLumaChroma)(1 + Code / 60 % 2),
                (DtSdiRange)(1 + Code / 120));
            DtSdiVideoSignalling Resolved;
            if (Want.Colorimetry == DT_SDI_COLORIMETRY_COLOR_VANC ||
                DtSmpte352_ResolveSignalling(Stds[s], &Want, &None, &Resolved) !=
                    DTAPI_OK)
                continue;
            CheckRoundTrip(DtFailures, Stds[s], &Want);
            Checked++;
        }
    }
    // SD 2 aspect ratios, 720p 1 signalling, five standards of 4 x 3 x 2 x 2 x 2.
    DT_ASSERT_EQ(Checked, 2 + 1 + 5 * 96);
}

// Code 1h of the colorimetry is the Color VANC packet in 2160p (ST 2081-10, 2082-10 and
// 425-5) and reserved in 1080 lines, where it reads as unknown. The range: in 85 code 3h
// is full and 0h, 1h narrow (ST 292-1 section 9.5.3); elsewhere 0h and 3h are full and
// 1h, 2h narrow (ST 425-1 section 4.1.6.2.3). 97 reads as C0, 98 as 8A.
DT_TEST(ReadSignallingCodes)
{
    DtSdiVideoSignalling Signalling;
    DT_ASSERT(DtSmpte352_ReadSignalling(0x0110C5C0, &Signalling));
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_COLOR_VANC);
    DT_ASSERT(DtSmpte352_ReadSignalling(0x0110C989, &Signalling));
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_UNKNOWN);
    DT_ASSERT(DtSmpte352_ReadSignalling(0x01100585, &Signalling));
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_UNKNOWN);
    DT_ASSERT(DtSmpte352_ReadSignalling(0x0110CA97, &Signalling));
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_COLOR_VANC);
    DT_ASSERT(DtSmpte352_ReadSignalling(0x0180CA98, &Signalling));
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_BT2020);

    static const struct
    {
        uint32_t Vpid;
        DtSdiRange Range;
    } Ranges[] = {
        {0x03000585, DT_SDI_RANGE_FULL},   {0x00000585, DT_SDI_RANGE_NARROW},
        {0x01000585, DT_SDI_RANGE_NARROW}, {0x0000C989, DT_SDI_RANGE_FULL},
        {0x0300C989, DT_SDI_RANGE_FULL},   {0x0100C989, DT_SDI_RANGE_NARROW},
        {0x0200C989, DT_SDI_RANGE_NARROW}, {0x0000C9CE, DT_SDI_RANGE_FULL},
    };
    for (size_t i = 0; i < sizeof(Ranges) / sizeof(Ranges[0]); i++)
    {
        DT_ASSERT(DtSmpte352_ReadSignalling(Ranges[i].Vpid, &Signalling));
        DT_ASSERT_EQ(Signalling.Range, Ranges[i].Range);
    }

    // SD and 720p carry no transfer or colorimetry: whatever the bits, SDR and the
    // implied colorimetry.
    DT_ASSERT(DtSmpte352_ReadSignalling(0xFF7FFF81, &Signalling));
    DT_ASSERT_EQ(Signalling.Transfer, DT_SDI_TRANSFER_SDR);
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_BT601);
    DT_ASSERT_EQ(Signalling.LumaChroma, DT_SDI_LUMACHROMA_YCBCR);
    DT_ASSERT(DtSmpte352_ReadSignalling(0xFFFFFF84, &Signalling));
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_BT709);
    DT_ASSERT_EQ(Signalling.Range, DT_SDI_RANGE_NARROW);

    DT_ASSERT(!DtSmpte352_ReadSignalling(0x01000042, &Signalling));
    DT_ASSERT_EQ(Signalling.Transfer, DT_SDI_TRANSFER_UNSPECIFIED);
    DT_ASSERT_EQ(Signalling.Colorimetry, DT_SDI_COLORIMETRY_UNKNOWN);
}

// A field of the frame's own signalling wins, and must fit the standard; a field of the
// builder's fits or gives way to the standard's default; the rest is the default.
DT_TEST(ResolveSignalling)
{
    DtSdiVideoSignalling None;
    DT_ASSERT_OK(DtSmpte352_TakeSignalling(NULL, &None));
    DtSdiVideoSignalling Out;

    // Nothing set: the defaults, BT.2020 in 2160p and BT.601 and 4:3 in SD.
    DT_ASSERT_OK(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_2160P50, &None, &None, &Out));
    DT_ASSERT_EQ(Out.Colorimetry, DT_SDI_COLORIMETRY_BT2020);
    DT_ASSERT_EQ(Out.Aspect, DT_SDI_ASPECT_16X9);
    DT_ASSERT_OK(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_625I50, &None, &None, &Out));
    DT_ASSERT_EQ(Out.Colorimetry, DT_SDI_COLORIMETRY_BT601);
    DT_ASSERT_EQ(Out.Aspect, DT_SDI_ASPECT_4X3);

    // The builder's HLG and BT.2020: taken in HD, given way in SD and 720p.
    DtSdiVideoSignalling Hdr = None;
    Hdr.Transfer = DT_SDI_TRANSFER_HLG;
    Hdr.Colorimetry = DT_SDI_COLORIMETRY_BT2020;
    DT_ASSERT_OK(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_1080I50, &None, &Hdr, &Out));
    DT_ASSERT_EQ(Out.Transfer, DT_SDI_TRANSFER_HLG);
    DT_ASSERT_EQ(Out.Colorimetry, DT_SDI_COLORIMETRY_BT2020);
    DT_ASSERT_OK(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_625I50, &None, &Hdr, &Out));
    DT_ASSERT_EQ(Out.Transfer, DT_SDI_TRANSFER_SDR);
    DT_ASSERT_EQ(Out.Colorimetry, DT_SDI_COLORIMETRY_BT601);
    DT_ASSERT_OK(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_720P50, &None, &Hdr, &Out));
    DT_ASSERT_EQ(Out.Transfer, DT_SDI_TRANSFER_SDR);

    // The same set on the frame: refused where it does not fit.
    DT_ASSERT_EQ(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_625I50, &Hdr, &None, &Out),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_720P50, &Hdr, &None, &Out),
                 DTAPI_E_NOT_SUPPORTED);

    // The frame's field wins over the builder's.
    DtSdiVideoSignalling Pq = None;
    Pq.Transfer = DT_SDI_TRANSFER_PQ;
    DT_ASSERT_OK(DtSmpte352_ResolveSignalling(DTAPI_VIDSTD_1080I50, &Pq, &Hdr, &Out));
    DT_ASSERT_EQ(Out.Transfer, DT_SDI_TRANSFER_PQ);
    DT_ASSERT_EQ(Out.Colorimetry, DT_SDI_COLORIMETRY_BT2020);

    // Values a standard does not carry: BT.601 and 4:3 above SD, unknown aspect ratio
    // in SD and 720p, ICtCp in SD, full range in 720p. The implied value fits.
    static const struct
    {
        int VidStd;
        int Field; // 0 colorimetry, 1 aspect ratio, 2 Y'CbCr or ICtCp, 3 range
        int Value;
        DtapiResult Result;
    } Cases[] = {
        {DTAPI_VIDSTD_1080I50, 0, DT_SDI_COLORIMETRY_BT601, DTAPI_E_NOT_SUPPORTED},
        {DTAPI_VIDSTD_625I50, 0, DT_SDI_COLORIMETRY_BT601, DTAPI_OK},
        {DTAPI_VIDSTD_720P50, 0, DT_SDI_COLORIMETRY_BT709, DTAPI_OK},
        {DTAPI_VIDSTD_1080I50, 1, DT_SDI_ASPECT_4X3, DTAPI_E_NOT_SUPPORTED},
        {DTAPI_VIDSTD_625I50, 1, DT_SDI_ASPECT_UNKNOWN, DTAPI_E_NOT_SUPPORTED},
        {DTAPI_VIDSTD_625I50, 1, DT_SDI_ASPECT_16X9, DTAPI_OK},
        {DTAPI_VIDSTD_720P50, 1, DT_SDI_ASPECT_UNKNOWN, DTAPI_E_NOT_SUPPORTED},
        {DTAPI_VIDSTD_2160P50, 1, DT_SDI_ASPECT_UNKNOWN, DTAPI_OK},
        {DTAPI_VIDSTD_625I50, 2, DT_SDI_LUMACHROMA_ICTCP, DTAPI_E_NOT_SUPPORTED},
        {DTAPI_VIDSTD_1080P50B, 2, DT_SDI_LUMACHROMA_ICTCP, DTAPI_OK},
        {DTAPI_VIDSTD_720P50, 3, DT_SDI_RANGE_FULL, DTAPI_E_NOT_SUPPORTED},
        {DTAPI_VIDSTD_2160P25, 3, DT_SDI_RANGE_FULL, DTAPI_OK},
    };
    for (size_t i = 0; i < sizeof(Cases) / sizeof(Cases[0]); i++)
    {
        DtSdiVideoSignalling Frame = None;
        if (Cases[i].Field == 0)
            Frame.Colorimetry = (DtSdiColorimetry)Cases[i].Value;
        else if (Cases[i].Field == 1)
            Frame.Aspect = (DtSdiAspect)Cases[i].Value;
        else if (Cases[i].Field == 2)
            Frame.LumaChroma = (DtSdiLumaChroma)Cases[i].Value;
        else
            Frame.Range = (DtSdiRange)Cases[i].Value;
        const DtapiResult Result =
            DtSmpte352_ResolveSignalling(Cases[i].VidStd, &Frame, &None, &Out);
        if (Result != Cases[i].Result)
            DT_FAIL("case %d: %s", (int)i, DtapiResult2Str(Result));
    }
}

// A program's signalling comes in for the fields its Size covers; Size 0 and NULL are
// every field default. A Size that does not end at a field, a value outside its enum and
// Color VANC are refused, and leave the signalling as it was. Signalling goes out only as
// far as the receiver's own Size.
DT_TEST(TakeAndGiveSignalling)
{
    DtSdiVideoSignalling Whole;
    DtSdiVideoSignalling In;
    memset(&In, 0, sizeof(In));
    DT_ASSERT_OK(DtSmpte352_TakeSignalling(NULL, &Whole));
    DT_ASSERT_EQ(Whole.Size, (int)sizeof(DtSdiVideoSignalling));
    DT_ASSERT_EQ(Whole.Transfer, DT_SDI_TRANSFER_DEFAULT);

    In.Size = (int)(offsetof(DtSdiVideoSignalling, Transfer) + sizeof(DtSdiTransfer));
    In.Transfer = DT_SDI_TRANSFER_PQ;
    In.Colorimetry = DT_SDI_COLORIMETRY_BT2020; // Beyond Size, so not taken
    DT_ASSERT_OK(DtSmpte352_TakeSignalling(&In, &Whole));
    DT_ASSERT_EQ(Whole.Transfer, DT_SDI_TRANSFER_PQ);
    DT_ASSERT_EQ(Whole.Colorimetry, DT_SDI_COLORIMETRY_DEFAULT);

    In.Size = 0;
    DT_ASSERT_OK(DtSmpte352_TakeSignalling(&In, &Whole));
    DT_ASSERT_EQ(Whole.Transfer, DT_SDI_TRANSFER_DEFAULT);

    In.Size = 3;
    DT_ASSERT_EQ(DtSmpte352_TakeSignalling(&In, &Whole), DTAPI_E_INVALID_ARG);
    In.Size = (int)sizeof(DtSdiVideoSignalling) + 4;
    DT_ASSERT_EQ(DtSmpte352_TakeSignalling(&In, &Whole), DTAPI_E_INVALID_ARG);
    In.Size = (int)sizeof(DtSdiVideoSignalling);
    In.Colorimetry = DT_SDI_COLORIMETRY_DEFAULT;
    In.Range = (DtSdiRange)7;
    DT_ASSERT_EQ(DtSmpte352_TakeSignalling(&In, &Whole), DTAPI_E_INVALID_ARG);
    In.Range = DT_SDI_RANGE_DEFAULT;
    In.Colorimetry = DT_SDI_COLORIMETRY_COLOR_VANC;
    DT_ASSERT_EQ(DtSmpte352_TakeSignalling(&In, &Whole), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(Whole.Transfer, DT_SDI_TRANSFER_DEFAULT);

    DT_ASSERT(!DtSmpte352_IsSignallingSize(0));
    DT_ASSERT(DtSmpte352_IsSignallingSize((int)sizeof(DtSdiVideoSignalling)));

    const DtSdiVideoSignalling All =
        MakeSig(DT_SDI_TRANSFER_HLG, DT_SDI_COLORIMETRY_BT2020, DT_SDI_ASPECT_16X9,
                DT_SDI_LUMACHROMA_ICTCP, DT_SDI_RANGE_FULL);
    DtSdiVideoSignalling Out;
    memset(&Out, 0, sizeof(Out));
    Out.Size =
        (int)(offsetof(DtSdiVideoSignalling, Colorimetry) + sizeof(DtSdiColorimetry));
    DtSmpte352_GiveSignalling(&All, &Out);
    DT_ASSERT_EQ(Out.Transfer, DT_SDI_TRANSFER_HLG);
    DT_ASSERT_EQ(Out.Colorimetry, DT_SDI_COLORIMETRY_BT2020);
    DT_ASSERT_EQ(Out.Aspect, DT_SDI_ASPECT_DEFAULT);
}

DT_TEST_MAIN("Smpte352", DT_RUN(PayloadIdIsTheFirstByte), DT_RUN(PictureRateCodes),
             DT_RUN(ScanBits), DT_RUN(AspectRatioPerPayload), DT_RUN(LinkNumber),
             DT_RUN(DefaultPayloadIds), DT_RUN(SignallingBits),
             DT_RUN(SignallingRoundTrip), DT_RUN(ReadSignallingCodes),
             DT_RUN(ResolveSignalling), DT_RUN(TakeAndGiveSignalling))

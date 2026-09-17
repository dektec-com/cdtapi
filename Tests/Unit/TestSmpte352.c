// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSmpte352.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for decoding the fields of a SMPTE ST 352 payload identifier
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtTest.h"           // Test framework.
#include "Video/DtSmpte352.h" // Functions under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Payload fields +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Byte 1 is the payload identifier, whatever the other bytes hold.
DT_TEST(PayloadIdIsTheFirstByte)
{
    DT_ASSERT_EQ(DtSmpte352PayloadId(0x00000085), 0x85);
    DT_ASSERT_EQ(DtSmpte352PayloadId(0xFFFFFFCE), 0xCE);
    DT_ASSERT_EQ(DtSmpte352PayloadId(0xFFFFFF00), 0);
}

// The picture rate codes of SMPTE ST 352 table 2 that DTAPI knows; the others, reserved
// or not supported, give 0/0. Only byte 2 bits 3..0 count.
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

        DtSmpte352PictureRate(0xFFFFF0FF | (Rates[i].Code << 8), &Num, &Den);
        if (Num != Rates[i].Num || Den != Rates[i].Den)
            DT_FAIL("code 0x%X: %d/%d", (unsigned)Rates[i].Code, Num, Den);
    }
}

// Byte 2 bit 7 is the transport and bit 6 the picture; a set bit means progressive.
DT_TEST(ScanBits)
{
    DT_ASSERT(DtSmpte352IsInterlacedTransport(0x00000085));
    DT_ASSERT(DtSmpte352IsInterlacedStructure(0x00000085));

    DT_ASSERT(DtSmpte352IsInterlacedTransport(0x00004085));
    DT_ASSERT(!DtSmpte352IsInterlacedStructure(0x00004085));

    DT_ASSERT(!DtSmpte352IsInterlacedTransport(0x0000C085));
    DT_ASSERT(!DtSmpte352IsInterlacedStructure(0x0000C085));

    DT_ASSERT(!DtSmpte352IsInterlacedTransport(0xFFFFBFFF));
    DT_ASSERT(DtSmpte352IsInterlacedStructure(0xFFFFBFFF));
}

// Byte 3 bit 7 is the aspect ratio, 1 for 16:9.
DT_TEST(AspectRatioBit)
{
    DT_ASSERT(DtSmpte352Is16x9(0x00800000));
    DT_ASSERT(!DtSmpte352Is16x9(0xFF7FFFFF));
}

// Byte 4 numbers the link: bits 7..6 on four level-A links, bits 7..5 on 6G and 12G; on
// four level-B links bits 7..5 number the channel, two to a link. Single-link payloads,
// 3G level B with its channel bit included, are link 0.
DT_TEST(LinkNumber)
{
    uint32_t Link;

    for (Link = 0; Link < 4; Link++)
        DT_ASSERT_EQ(DtSmpte352LinkNumber(0x1F00CA97 | (Link << 30)), Link);
    for (Link = 0; Link < 8; Link++)
    {
        DT_ASSERT_EQ(DtSmpte352LinkNumber(0x1F00CA98 | (Link << 29)), Link / 2);
        DT_ASSERT_EQ(DtSmpte352LinkNumber(0x1F00C7C0 | (Link << 29)), Link);
        DT_ASSERT_EQ(DtSmpte352LinkNumber(0x1F00CACE | (Link << 29)), Link);
    }

    DT_ASSERT_EQ(DtSmpte352LinkNumber(0xC000CA8A), 0);
    DT_ASSERT_EQ(DtSmpte352LinkNumber(0xE000C589), 0);
    DT_ASSERT_EQ(DtSmpte352LinkNumber(0xE0000585), 0);
    DT_ASSERT_EQ(DtSmpte352LinkNumber(0xFFFFFF00), 0);
}

DT_TEST_MAIN("Smpte352", DT_RUN(PayloadIdIsTheFirstByte), DT_RUN(PictureRateCodes),
             DT_RUN(ScanBits), DT_RUN(AspectRatioBit), DT_RUN(LinkNumber))

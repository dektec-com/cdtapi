// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestEthIp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for the header in front of the frames in a pipe's buffer
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtPcie/DtEthIp.h" // Functions under test.
#include "DtTest.h"         // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A version 1 header for a UDP datagram of 1,200 bytes over IPv4 without VLAN: a frame
// of 1,242 bytes in 158 words, the IP addresses at byte 44 and the UDP header at byte
// 52; here with substream 2, a valid time stamp and a time of day of 12345678h seconds
// and 999,999,999 nanoseconds. The bytes were worked out by hand from the bit positions
// the header has on the wire.
static const uint8_t g_V1[DT_ETHIP_HEADER_SIZE] = {
    0xEE, 0xEE, 0x9E, 0xDA, 0x5C, 0x0D, 0x13, 0x02, // Header word
    0xFF, 0xC9, 0x9A, 0x3B, 0x78, 0x56, 0x34, 0x12, // Time of day
    0x00, 0x00,                                     // Alignment
};

static DtEthIpFields V1Header(void)
{
    DtEthIpFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.NumWords = 158;
    Header.FrameSize = 1242;
    Header.IpAddressOffset = 44;
    Header.PortOffset = 52;
    Header.Protocol = DT_ETHIP_PROTO_UDP;
    Header.PacketType = DT_ETHIP_TYPE_IPV4;
    Header.SubStream = 2;
    Header.TimestampValid = true;
    Header.Seconds = 0x12345678;
    Header.Nanoseconds = 999999999;
    return Header;
}

// A version 2 header of a frame of 8,000 bytes aligned to 16 bytes, in 1,004 words with
// 14 bytes of padding, every flag but the valid time stamp set, fingerprint 2Ah.
static const uint8_t g_V2[8] = {0xEF, 0xEF, 0xEC, 0x73, 0x58, 0x0D, 0xE4, 0xA9};

static DtEthIpFields V2Header(void)
{
    DtEthIpFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.Jumbo = true;
    Header.NumWords = DtEthIp_NumWords(8000, 16);
    Header.FrameSize = 8000;
    Header.IpAddressOffset = 44;
    Header.PortOffset = 52;
    Header.PacketType = DT_ETHIP_TYPE_IPV6;
    Header.IpV4ChecksumError = true;
    Header.UdpChecksumError = true;
    Header.TcpChecksumError = true;
    Header.TimestampRequest = true;
    Header.Fingerprint = 0x2A;
    return Header;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A packet is the header and the frame rounded up to words, and to the alignment.
DT_TEST(WordsOfAPacket)
{
    DT_ASSERT_EQ(DtEthIp_NumWords(1242, 8), 158);
    DT_ASSERT_EQ(DtEthIp_NumWords(1238, 8), 157);
    DT_ASSERT_EQ(DtEthIp_NumWords(1239, 8), 158);
    DT_ASSERT_EQ(DtEthIp_NumWords(8000, 16), 1004);
    DT_ASSERT_EQ(DtEthIp_NumWords(7998, 16), 1002);
    DT_ASSERT_EQ(DtEthIp_NumWords(0, 0), 3);
}

DT_TEST(HeaderSizes)
{
    DT_ASSERT_EQ(DtEthIp_HeaderSize(DT_ETHIP_TYPE_TIMESTAMP), 16);
    DT_ASSERT_EQ(DtEthIp_HeaderSize(DT_ETHIP_TYPE_IPV4), 18);
    DT_ASSERT_EQ(DtEthIp_HeaderSize(DT_ETHIP_TYPE_IPV6), 18);
    DT_ASSERT_EQ(DtEthIp_HeaderSize(DT_ETHIP_TYPE_OTHER), 18);
}

DT_TEST(WritesVersion1)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    DtEthIpFields Header = V1Header();

    memset(Bytes, 0x55, sizeof(Bytes));
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT(memcmp(Bytes, g_V1, sizeof(g_V1)) == 0);
}

DT_TEST(ReadsVersion1)
{
    DtEthIpFields Expected = V1Header();
    DtEthIpFields Header;

    DT_ASSERT(DtEthIp_Read(g_V1, &Header));
    DT_ASSERT(memcmp(&Header, &Expected, sizeof(Header)) == 0);
}

DT_TEST(WritesAndReadsVersion2)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    DtEthIpFields Expected = V2Header();
    DtEthIpFields Header;

    DtEthIp_Write(&Expected, Bytes);
    DT_ASSERT(memcmp(Bytes, g_V2, sizeof(g_V2)) == 0);
    DT_ASSERT(DtEthIp_Read(Bytes, &Header));
    DT_ASSERT(memcmp(&Header, &Expected, sizeof(Header)) == 0);
}

// Values wider than their field are cut to it, and every field reads back where the
// others are all ones.
DT_TEST(FieldsAreCutAndKeptApart)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    DtEthIpFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.NumWords = 255;
    Header.FrameSize = 2047;
    Header.IpAddressOffset = 31 * 4;
    Header.PortOffset = 255 * 4;
    Header.Protocol = 3;
    Header.PacketType = 7;
    Header.SubStream = 7;
    Header.Fingerprint = 0xFF;
    Header.Seconds = 0xFFFFFFFF;
    Header.Nanoseconds = 0xFFFFFFFF;
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT_EQ(Bytes[6] & 0x01, 0x01);
    DT_ASSERT_EQ(Bytes[7], 0xFC);
    DT_ASSERT_EQ(Bytes[11], 0x3F);
    DT_ASSERT_EQ(Bytes[16], 0);
    DT_ASSERT_EQ(Bytes[17], 0);

    DtEthIp_Read(Bytes, &Header);
    DT_ASSERT_EQ(Header.NumWords, 255);
    DT_ASSERT_EQ(Header.FrameSize, 2047);
    DT_ASSERT_EQ(Header.IpAddressOffset, 124);
    DT_ASSERT_EQ(Header.PortOffset, 1020);
    DT_ASSERT_EQ(Header.Protocol, 1);
    DT_ASSERT_EQ(Header.PacketType, 3);
    DT_ASSERT_EQ(Header.SubStream, 3);
    DT_ASSERT_EQ(Header.Fingerprint, 0x3F);
    DT_ASSERT(!Header.TimestampValid && !Header.TimestampRequest);
    DT_ASSERT_EQ(Header.Nanoseconds, 0x3FFFFFFFu);
    DT_ASSERT_EQ(Header.Seconds, 0xFFFFFFFFu);
}

// A time stamp packet's frame starts at byte 16, so version 2 counts two bytes more.
DT_TEST(TimestampPacketHasNoAlignment)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    DtEthIpFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.Jumbo = true;
    Header.NumWords = 2;
    Header.FrameSize = 0;
    Header.PacketType = DT_ETHIP_TYPE_TIMESTAMP;
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT(DtEthIp_Read(Bytes, &Header));
    DT_ASSERT_EQ(Header.FrameSize, 0);
}

DT_TEST(RefusesUnknownSyncWord)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    DtEthIpFields Header;

    memcpy(Bytes, g_V1, sizeof(Bytes));
    Bytes[0] = 0xEF;
    DT_ASSERT(!DtEthIp_Read(Bytes, &Header));
    DT_ASSERT_EQ(Header.PortOffset, 52);
}

// Version 1's frame must fit its words; version 2's padding must leave room.
DT_TEST(RefusesAFrameLargerThanItsWords)
{
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];
    DtEthIpFields Header = V1Header();

    Header.FrameSize = 158 * 8 - 18 + 1;
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT(!DtEthIp_Read(Bytes, &Header));

    Header = V2Header();
    Header.NumWords = 3;
    Header.FrameSize = 0;
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT(DtEthIp_Read(Bytes, &Header));
    Bytes[3] = (uint8_t)(Bytes[3] | 0xF8);
    DT_ASSERT(!DtEthIp_Read(Bytes, &Header));
}

DT_TEST_MAIN("EthIp", DT_RUN(WordsOfAPacket), DT_RUN(HeaderSizes), DT_RUN(WritesVersion1),
             DT_RUN(ReadsVersion1), DT_RUN(WritesAndReadsVersion2),
             DT_RUN(FieldsAreCutAndKeptApart), DT_RUN(TimestampPacketHasNoAlignment),
             DT_RUN(RefusesUnknownSyncWord), DT_RUN(RefusesAFrameLargerThanItsWords))

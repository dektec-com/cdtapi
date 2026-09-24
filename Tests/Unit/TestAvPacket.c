// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestAvPacket.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the headers of transmitted and received packets
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The expected bytes are written out from RFC 791, 8200, 768, 3550 and 4175 and IEEE
// 802.1Q, field by field.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvPacket.h" // Functions under test.
#include "DtPcie/DtEthIp.h"    // The packet header.
#include "DtTest.h"            // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static const uint8_t SrcMac[6] = {0x00, 0x14, 0xF4, 0x08, 0x00, 0x01};
static const uint8_t DstMac[6] = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03};

// A time of day: 1,800,000,000 s and 123,456,789 ns.
#define TOD UINT64_C(1800000000123456789)

// An IPv4 stream from 192.168.1.10:5000 to 239.1.2.3:5004, without VLAN.
static DtAvNet NetV4(void)
{
    DtAvNet Net;
    memset(&Net, 0, sizeof(Net));
    Net.Alignment = 8;
    memcpy(Net.SrcMac, SrcMac, 6);
    memcpy(Net.DstMac, DstMac, 6);
    static const uint8_t Src[4] = {192, 168, 1, 10};
    static const uint8_t Dst[4] = {239, 1, 2, 3};
    memcpy(Net.SrcIp, Src, 4);
    memcpy(Net.DstIp, Dst, 4);
    Net.DiffServ = 0x88;
    Net.TimeToLive = 32;
    Net.SrcPort = 5000;
    Net.DstPort = 5004;
    Net.IpIdentification = 0x1234;
    return Net;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(Ipv4PacketBytes)
{
    static const uint8_t Expected[42] = {
        // Ethernet
        0x01, 0x00, 0x5E, 0x01, 0x02, 0x03, 0x00, 0x14, 0xF4, 0x08, 0x00, 0x01, 0x08,
        0x00,
        // IPv4: version and length, DSCP, total length 128, identification, don't
        // fragment, TTL, UDP, checksum, addresses
        0x45, 0x88, 0x00, 0x80, 0x12, 0x34, 0x40, 0x00, 32, 17, 0x00, 0x00, 192, 168, 1,
        10, 239, 1, 2, 3,
        // UDP: ports, length 108, checksum
        0x13, 0x88, 0x13, 0x8C, 0x00, 0x6C, 0x00, 0x00};
    DtAvNet Net = NetV4();
    uint8_t Packet[256];

    DT_ASSERT_EQ(DtAvNet_HeaderSize(&Net), 18 + 42);
    DT_ASSERT_EQ(DtAvNet_PacketSize(&Net, 100), 160);
    memset(Packet, 0xAA, sizeof(Packet));
    DT_ASSERT_EQ(DtAvNet_Finish(&Net, Packet, 100, 0, TOD), 160);
    DT_ASSERT_MEM(Packet + 18, Expected, 42);
    DT_ASSERT_EQ(Net.IpIdentification, 0x1235);
    for (int i = 18 + 42 + 100; i < 160; i++)
        DT_ASSERT_EQ(Packet[i], 0);
    DT_ASSERT_EQ(Packet[160], 0xAA);

    DtEthIpFields Header;
    DT_ASSERT(DtEthIp_Read(Packet, &Header));
    DT_ASSERT(!Header.HeaderV2);
    DT_ASSERT_EQ(Header.NumWords, 20);
    DT_ASSERT_EQ(Header.FrameSize, 142);
    DT_ASSERT_EQ(Header.IpAddressOffset, 18 + 14 + 12);
    DT_ASSERT_EQ(Header.PortOffset, 18 + 14 + 20);
    DT_ASSERT_EQ(Header.IsUdp, DT_ETHIP_PROTO_UDP);
    DT_ASSERT_EQ(Header.PacketType, DT_ETHIP_TYPE_IPV4);
    DT_ASSERT(Header.TimestampValid);
    DT_ASSERT_EQ(Header.Seconds, 1800000000u);
    DT_ASSERT_EQ(Header.Nanoseconds, 123456789u);

    // A destination port offset for a second substream.
    DtAvNet_Finish(&Net, Packet, 100, 2, TOD);
    DT_ASSERT_EQ(DtAvPacket_Get16(Packet + 18 + 34 + 2), 5006);
}

DT_TEST(Ipv6VlanJumboPacketBytes)
{
    static const uint8_t Expected[66] = {
        // Ethernet with the tag of priority 5 and VLAN 100
        0x33, 0x33, 0x00, 0x00, 0x00, 0x42, 0x00, 0x14, 0xF4, 0x08, 0x00, 0x01, 0x81,
        0x00, 0xA0, 0x64, 0x86, 0xDD,
        // IPv6: version 6, traffic class 0x88, flow 0, payload length 1008, UDP, hops
        0x68, 0x80, 0x00, 0x00, 0x03, 0xF0, 17, 64,
        // Source 2001:db8::10
        0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10,
        // Destination ff0e::42
        0xFF, 0x0E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42,
        // UDP: ports, length 1008, checksum
        0x13, 0x88, 0x13, 0x8C, 0x03, 0xF0, 0x00, 0x00};
    static const uint8_t Src[16] = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0,
                                    0,    0,    0,    0,    0, 0, 0, 0x10};
    static const uint8_t Dst[16] = {0xFF, 0x0E, 0, 0, 0, 0, 0, 0,
                                    0,    0,    0, 0, 0, 0, 0, 0x42};
    static const uint8_t Mac[6] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x42};
    DtAvNet Net = NetV4();
    uint8_t Packet[1200];

    Net.HeaderV2 = true;
    Net.Alignment = 16;
    Net.IpV6 = true;
    Net.VlanId = 100;
    Net.VlanPriority = 5;
    Net.TimeToLive = 64;
    memcpy(Net.SrcIp, Src, 16);
    memcpy(Net.DstIp, Dst, 16);
    memcpy(Net.DstMac, Mac, 6);

    DT_ASSERT_EQ(DtAvNet_HeaderSize(&Net), 18 + 18 + 40 + 8);
    int Size = DtAvNet_Finish(&Net, Packet, 1000, 0, TOD);
    DT_ASSERT_EQ(Size, (18 + 18 + 40 + 8 + 1000 + 15) / 16 * 16);
    DT_ASSERT_EQ(Size, DtAvNet_PacketSize(&Net, 1000));
    DT_ASSERT_MEM(Packet + 18, Expected, 66);
    DT_ASSERT_EQ(DtAvPacket_Get16(Packet + 18 + 58 + 4), 1008);
    DT_ASSERT_EQ(Net.IpIdentification, 0x1234);

    DtEthIpFields Header;
    DT_ASSERT(DtEthIp_Read(Packet, &Header));
    DT_ASSERT(Header.HeaderV2);
    DT_ASSERT_EQ(Header.NumWords * 8, Size);
    DT_ASSERT_EQ(Header.FrameSize, 18 + 40 + 8 + 1000);
    DT_ASSERT_EQ(Header.IpAddressOffset, 18 + 18 + 8);
    DT_ASSERT_EQ(Header.PortOffset, 18 + 18 + 40);
    DT_ASSERT_EQ(Header.PacketType, DT_ETHIP_TYPE_IPV6);
}

DT_TEST(ParsingReceivedPackets)
{
    DtAvNet Net = NetV4();
    uint8_t Packet[256];
    DtAvRxPacket Rx;

    int Size = DtAvNet_Finish(&Net, Packet, 100, 0, TOD);
    DT_ASSERT(DtAvRxPacket_Parse(Packet, Size, &Rx));
    DT_ASSERT_EQ(Rx.TodNs, TOD);
    DT_ASSERT(Rx.Udp == Packet + 60);
    DT_ASSERT_EQ(Rx.UdpSize, 100);
    DT_ASSERT_EQ(Rx.SubStream, 0);

    // A frame that ends in the Ethernet checksum, on substream 2.
    DtEthIpFields Header;
    DtEthIp_Read(Packet, &Header);
    Header.FrameSize += 4;
    Header.NumWords++;
    Header.SubStream = 2;
    DtEthIp_Write(&Header, Packet);
    Size += 8;
    DT_ASSERT(DtAvRxPacket_Parse(Packet, Size, &Rx));
    DT_ASSERT_EQ(Rx.UdpSize, 100);
    DT_ASSERT_EQ(Rx.SubStream, 2);

    // Shorter than its header says, a UDP length beyond the frame, a UDP length below
    // its header, another protocol, and a bad sync word.
    DT_ASSERT(!DtAvRxPacket_Parse(Packet, 150, &Rx));
    DtAvPacket_Put16(200, Packet + 56);
    DT_ASSERT(!DtAvRxPacket_Parse(Packet, Size, &Rx));
    DtAvPacket_Put16(7, Packet + 56);
    DT_ASSERT(!DtAvRxPacket_Parse(Packet, Size, &Rx));
    DtAvPacket_Put16(108, Packet + 56);
    DT_ASSERT(DtAvRxPacket_Parse(Packet, Size, &Rx));
    Header.IsUdp = 0;
    DtEthIp_Write(&Header, Packet);
    DT_ASSERT(!DtAvRxPacket_Parse(Packet, Size, &Rx));
    Packet[0] = 0;
    DT_ASSERT(!DtAvRxPacket_Parse(Packet, Size, &Rx));
    DT_ASSERT(Rx.Udp == NULL);
}

DT_TEST(RtpHeaderBytes)
{
    static const uint8_t Expected[12] = {0x80, 0xE0, 0xAB, 0xCD, 0x12, 0x34,
                                         0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    DtAvRtp Rtp = {true, 96, 0xABCD, 0x12345678, 0x9ABCDEF0};
    uint8_t Bytes[12];
    DtAvRtp Back;

    DtAvRtp_Write(&Rtp, Bytes);
    DT_ASSERT_MEM(Bytes, Expected, 12);
    DtAvRtp_Read(Bytes, &Back);
    DT_ASSERT(Back.Marker);
    DT_ASSERT_EQ(Back.PayloadType, 96);
    DT_ASSERT_EQ(Back.SequenceNumber, 0xABCD);
    DT_ASSERT_EQ(Back.Timestamp, 0x12345678u);
    DT_ASSERT_EQ(Back.Ssrc, 0x9ABCDEF0u);
    Rtp.Marker = false;
    Rtp.PayloadType = 97;
    DtAvRtp_Write(&Rtp, Bytes);
    DT_ASSERT_EQ(Bytes[1], 97);
}

DT_TEST(SampleRowDataHeaderBytes)
{
    static const uint8_t Expected[6] = {0x04, 0xB0, 0x82, 0x1B, 0x80, 0x64};
    DtAvSrd Srd = {1200, true, 539, true, 100};
    uint8_t Bytes[6];
    DtAvSrd Back;

    DtAvSrd_Write(&Srd, Bytes);
    DT_ASSERT_MEM(Bytes, Expected, 6);
    DtAvSrd_Read(Bytes, &Back);
    DT_ASSERT_EQ(Back.Length, 1200);
    DT_ASSERT(Back.Field && Back.Continuation);
    DT_ASSERT_EQ(Back.Row, 539);
    DT_ASSERT_EQ(Back.Offset, 100);

    DtAvSrd Top = {5, false, 0x7FFF, false, 0x7FFF};
    DtAvSrd_Write(&Top, Bytes);
    DtAvSrd_Read(Bytes, &Back);
    DT_ASSERT(!Back.Field && !Back.Continuation);
    DT_ASSERT_EQ(Back.Row, 0x7FFF);
    DT_ASSERT_EQ(Back.Offset, 0x7FFF);
}

DT_TEST_MAIN("AvPacket", DT_RUN(Ipv4PacketBytes), DT_RUN(Ipv6VlanJumboPacketBytes),
             DT_RUN(ParsingReceivedPackets), DT_RUN(RtpHeaderBytes),
             DT_RUN(SampleRowDataHeaderBytes))

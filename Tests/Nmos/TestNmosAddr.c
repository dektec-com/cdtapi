// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestNmosAddr.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the addresses of the NMOS bridge, read and written
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The cases follow RFC 4291 §2.2 for what an IPv6 address may be written as, and RFC
// 5952 §4 for the one form it is written in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"           // Test framework.
#include "Nmos/DtNmosAddr.h"  // Functions under test.
#include "cdtapi_constants.h" // Result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Reads Text and writes it again: Written is what comes out, or "" when the reading
// fails.
static const char* RoundTrip(const char* Text)
{
    static char Written[DT_NMOS_ADDR_SIZE];
    uint8_t Ip[16];
    bool IpV6 = false;
    Written[0] = '\0';
    if (DtNmosAddr_Parse(Text, Ip, &IpV6) == DTAPI_OK)
        DtNmosAddr_Format(IpV6, Ip, Written, sizeof(Written));
    return Written;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IPv4 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(Ipv4Read)
{
    static const uint8_t Want[16] = {239, 1, 2, 3};
    uint8_t Ip[16];
    bool IpV6 = true;
    DT_ASSERT_OK(DtNmosAddr_Parse("239.1.2.3", Ip, &IpV6));
    DT_ASSERT(!IpV6);
    DT_ASSERT_MEM(Ip, Want, 16);
    DT_ASSERT_STR(RoundTrip("0.0.0.0"), "0.0.0.0");
    DT_ASSERT_STR(RoundTrip("255.255.255.255"), "255.255.255.255");
}

// Not addresses: too few or too many parts, a part too large, a leading zero that could
// be read as octal, an empty part, and other characters.
DT_TEST(Ipv4Refused)
{
    static const char* const Wrong[] = {
        "1.2.3",  "1.2.3.4.5", "256.1.1.1", "1.2.3.04",   "1..2.3",
        "1.2.3.", ".1.2.3",    "1.2.3.4 ",  "1.2.3.4/24", "0001.2.3.4"};
    for (size_t i = 0; i < sizeof(Wrong) / sizeof(Wrong[0]); i++)
    {
        uint8_t Ip[16];
        bool IpV6 = false;
        if (DtNmosAddr_Parse(Wrong[i], Ip, &IpV6) != DTAPI_E_INVALID_ARG)
            DT_FAIL("%s was not refused as no address", Wrong[i]);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= IPv6 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(Ipv6Read)
{
    static const uint8_t Want[16] = {0xFF, 0x3E, 0, 0, 0, 0, 0,    0,
                                     0,    0,    0, 0, 0, 0, 0x12, 0x34};
    uint8_t Ip[16];
    bool IpV6 = false;
    DT_ASSERT_OK(DtNmosAddr_Parse("ff3e::1234", Ip, &IpV6));
    DT_ASSERT(IpV6);
    DT_ASSERT_MEM(Ip, Want, 16);
    DT_ASSERT_OK(DtNmosAddr_Parse("FF3E:0:0:0:0:0:0:1234", Ip, &IpV6));
    DT_ASSERT_MEM(Ip, Want, 16);
}

// RFC 5952 §4: no leading zeros, lower case, "::" for the longest run of two or more
// zero groups and the first of two as long, and an IPv4-mapped address dotted.
DT_TEST(Ipv6Written)
{
    DT_ASSERT_STR(RoundTrip("2001:0db8:0000:0000:0000:0000:0002:0001"), "2001:db8::2:1");
    DT_ASSERT_STR(RoundTrip("2001:DB8::1"), "2001:db8::1");
    DT_ASSERT_STR(RoundTrip("2001:db8:0:1:1:1:1:1"), "2001:db8:0:1:1:1:1:1");
    DT_ASSERT_STR(RoundTrip("2001:db8:0:0:1:0:0:1"), "2001:db8::1:0:0:1");
    DT_ASSERT_STR(RoundTrip("2001:0:0:1:0:0:0:1"), "2001:0:0:1::1");
    DT_ASSERT_STR(RoundTrip("::"), "::");
    DT_ASSERT_STR(RoundTrip("::1"), "::1");
    DT_ASSERT_STR(RoundTrip("1::"), "1::");
    DT_ASSERT_STR(RoundTrip("fe80::1:2:3:4"), "fe80::1:2:3:4");
    DT_ASSERT_STR(RoundTrip("::ffff:192.0.2.1"), "::ffff:192.0.2.1");
    DT_ASSERT_STR(RoundTrip("64:ff9b::192.0.2.33"), "64:ff9b::c000:221");
}

// Not addresses: a second "::", too many groups or too few without one, a group of five
// digits, a colon at either end alone, an IPv4 address not at the end, and a zone.
DT_TEST(Ipv6Refused)
{
    static const char* const Wrong[] = {"1::2::3",
                                        "1:2:3:4:5:6:7:8:9",
                                        "1:2:3:4:5:6:7",
                                        "12345::1",
                                        ":1:2:3:4:5:6:7",
                                        "1:2:3:4:5:6:7:",
                                        "1:::2",
                                        "::1.2.3.4:5",
                                        "1:2:3:4:5:6:7:1.2.3.4",
                                        "fe80::1%eth0",
                                        "g::1"};
    for (size_t i = 0; i < sizeof(Wrong) / sizeof(Wrong[0]); i++)
    {
        uint8_t Ip[16];
        bool IpV6 = false;
        if (DtNmosAddr_Parse(Wrong[i], Ip, &IpV6) != DTAPI_E_INVALID_ARG)
            DT_FAIL("%s was not refused as no address", Wrong[i]);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Names +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A domain name is not looked up, and an empty text is no address.
DT_TEST(NamesRefused)
{
    uint8_t Ip[16];
    bool IpV6 = false;
    DT_ASSERT_EQ(DtNmosAddr_Parse("cam1.studio.local", Ip, &IpV6), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtNmosAddr_Parse("localhost", Ip, &IpV6), DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtNmosAddr_Parse("", Ip, &IpV6), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtNmosAddr_Parse(NULL, Ip, &IpV6), DTAPI_E_INVALID_ARG);
}

// A buffer too small is said so, and the longest address fits DT_NMOS_ADDR_SIZE.
DT_TEST(SmallBuffer)
{
    uint8_t Ip[16];
    bool IpV6 = false;
    char Text[DT_NMOS_ADDR_SIZE];
    DT_ASSERT_OK(DtNmosAddr_Parse("1:2:3:4:5:6:7:8", Ip, &IpV6));
    DT_ASSERT_EQ(DtNmosAddr_Format(IpV6, Ip, Text, 15), DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_OK(DtNmosAddr_Format(IpV6, Ip, Text, 16));
    DT_ASSERT_STR(Text, "1:2:3:4:5:6:7:8");
    DT_ASSERT_OK(DtNmosAddr_Parse("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", Ip, &IpV6));
    DT_ASSERT_OK(DtNmosAddr_Format(IpV6, Ip, Text, sizeof(Text)));
}

DT_TEST_MAIN("NmosAddr", DT_RUN(Ipv4Read), DT_RUN(Ipv4Refused), DT_RUN(Ipv6Read),
             DT_RUN(Ipv6Written), DT_RUN(Ipv6Refused), DT_RUN(NamesRefused),
             DT_RUN(SmallBuffer))

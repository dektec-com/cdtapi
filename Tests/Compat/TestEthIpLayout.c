// #*#*#*#*#*#*#*#*#*#*#*#*#* TestEthIpLayout.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The packet header's bytes against the bit fields of the SDK's EthPrtcls.h
//
// SPDX-License-Identifier: BSD-3-Clause
//
// DtEthIp.c writes the header with shifts in the layout the compiler gives EthPrtcls.h's
// bit fields. This fills the SDK's structure through those bit fields, compiled by the
// compiler at hand, and compares its bytes with what DtEthIp_Write makes of the same
// values. EthPrtcls.h lives outside this repository; CMake registers this test only
// where it finds the header.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// The base types EthPrtcls.h spells, as the SDK's StandardTypes.h defines them.
typedef uint8_t UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef uint64_t UInt64;
typedef uint32_t UInt;

// CDTAPI includes
#include "DtPcie/DtEthIp.h" // Functions under test.
#include "DtTest.h"         // Test framework.
#include "EthPrtcls.h"      // The SDK's header.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Fills a header with values that differ per field, as version 2 when IsVersion2.
static DtEthIpHeaderFields Values(bool IsVersion2)
{
    DtEthIpHeaderFields Header;

    memset(&Header, 0, sizeof(Header));
    Header.IsVersion2 = IsVersion2;
    Header.NumWords = IsVersion2 ? 1004 : 158;
    Header.FrameSize = IsVersion2 ? 8000 : 1242;
    Header.IpAddressOffset = 44;
    Header.PortOffset = 52;
    Header.IsUdp = 1;
    Header.PacketType = 2;
    Header.SubStream = 3;
    Header.IpV4ChecksumError = true;
    Header.UdpChecksumError = false;
    Header.TcpChecksumError = true;
    Header.TimestampRequest = false;
    Header.TimestampValid = true;
    Header.Fingerprint = 0x2A;
    Header.Seconds = 0x89ABCDEF;
    Header.Nanoseconds = 987654321;
    return Header;
}

// The same values through the SDK's bit fields.
static void FillSdk(const DtEthIpHeaderFields* Values, DtEthIp* EthIp)
{
    memset(EthIp, 0, sizeof(*EthIp));
    if (Values->IsVersion2)
    {
        EthIp->m_Hdr.m_SyncWord = DT_ETHIP_SYNCWORD_V2;
        EthIp->m_Hdr.V2.m_SizeInQWords = (UInt64)Values->NumWords;
        EthIp->m_Hdr.V2.m_PaddingInBytes =
            (UInt64)(Values->NumWords * 8 - 18 - Values->FrameSize);
    }
    else
    {
        EthIp->m_Hdr.m_SyncWord = DT_ETHIP_SYNCWORD_V1;
        EthIp->m_Hdr.V1.m_SizeInQWords = (UInt64)Values->NumWords;
        EthIp->m_Hdr.V1.m_SizeInBytes = (UInt64)Values->FrameSize;
    }
    EthIp->m_Hdr.m_IpAddressOffset = (UInt64)Values->IpAddressOffset / 4;
    EthIp->m_Hdr.m_PortOffset = (UInt64)Values->PortOffset / 4;
    EthIp->m_Hdr.m_Protocol = (UInt64)Values->IsUdp;
    EthIp->m_Hdr.m_PacketType = (UInt64)Values->PacketType;
    EthIp->m_Hdr.m_SubStream = (UInt64)Values->SubStream;
    EthIp->m_Hdr.m_IpV4HdrChecksumError = Values->IpV4ChecksumError ? 1 : 0;
    EthIp->m_Hdr.m_UdpChecksumError = Values->UdpChecksumError ? 1 : 0;
    EthIp->m_Hdr.m_TcpChecksumError = Values->TcpChecksumError ? 1 : 0;
    EthIp->m_Hdr.m_TimestampRequest = Values->TimestampRequest ? 1 : 0;
    EthIp->m_Hdr.m_TimestampValid = Values->TimestampValid ? 1 : 0;
    EthIp->m_Hdr.m_Fingerprint = (UInt64)Values->Fingerprint;
    EthIp->m_Tod.m_TodSeconds = Values->Seconds;
    EthIp->m_Tod.m_TodNanoseconds = Values->Nanoseconds;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(SizesAgree)
{
    DT_ASSERT_EQ(sizeof(DtEthIp), DT_ETHIP_HEADER_SIZE);
    DT_ASSERT_EQ(sizeof(DtEthIpHeader) + sizeof(DtEthIpTod), 16);
}

DT_TEST(Version1BytesAgree)
{
    DtEthIpHeaderFields Header = Values(false);
    DtEthIp Sdk;
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];

    FillSdk(&Header, &Sdk);
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT_MEM(Bytes, &Sdk, DT_ETHIP_HEADER_SIZE);
}

DT_TEST(Version2BytesAgree)
{
    DtEthIpHeaderFields Header = Values(true);
    DtEthIp Sdk;
    uint8_t Bytes[DT_ETHIP_HEADER_SIZE];

    FillSdk(&Header, &Sdk);
    DtEthIp_Write(&Header, Bytes);
    DT_ASSERT_MEM(Bytes, &Sdk, DT_ETHIP_HEADER_SIZE);
}

DT_TEST_MAIN("EthIpLayout", DT_RUN(SizesAgree), DT_RUN(Version1BytesAgree),
             DT_RUN(Version2BytesAgree))

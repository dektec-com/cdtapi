// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestNetAddr.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the kinds of IP address, masks and multicast MAC addresses
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"    // Test framework.
#include "Net/DtNet.h" // Functions under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(Ipv4Kinds)
{
    static const uint8_t Zero[16] = {0};
    static const uint8_t Group[16] = {239, 1, 2, 3};
    static const uint8_t Ssm[16] = {232, 0, 0, 1};
    static const uint8_t Low[16] = {224, 0, 0, 0};
    static const uint8_t High[16] = {240, 0, 0, 0};
    static const uint8_t LinkLocal[16] = {169, 254, 1, 1};
    static const uint8_t Unicast[16] = {192, 168, 1, 10, 1, 1, 1, 1};

    DT_ASSERT(DtNet_IsEmpty(false, Zero));
    DT_ASSERT(DtNet_IsEmpty(false, Unicast) == false);
    DT_ASSERT(DtNet_IsMulticast(false, Group) && DtNet_IsMulticast(false, Low));
    DT_ASSERT(!DtNet_IsMulticast(false, High) && !DtNet_IsMulticast(false, Unicast));
    DT_ASSERT(DtNet_IsSourceSpecific(false, Ssm) &&
              !DtNet_IsSourceSpecific(false, Group));
    DT_ASSERT(DtNet_IsLinkLocal(false, LinkLocal) && !DtNet_IsLinkLocal(false, Unicast));
}

DT_TEST(Ipv6Kinds)
{
    static const uint8_t Zero[16] = {0};
    static const uint8_t NotEmpty[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t Group[16] = {0xFF, 0x0E, 0, 0, 0, 0, 0, 0,
                                      0,    0,    0, 0, 1, 2, 3, 4};
    static const uint8_t Ssm[16] = {0xFF, 0x3E};
    static const uint8_t LinkLocal[16] = {0xFE, 0x80};
    static const uint8_t LinkLocalEnd[16] = {0xFE, 0xBF};
    static const uint8_t SiteLocal[16] = {0xFD, 0x12};
    static const uint8_t Global[16] = {0x20, 0x01, 0x0D, 0xB8};
    static const uint8_t NotGlobal[16] = {0x40, 0x01};

    DT_ASSERT(DtNet_IsEmpty(true, Zero) && !DtNet_IsEmpty(true, NotEmpty));
    DT_ASSERT(DtNet_IsMulticast(true, Group) && !DtNet_IsMulticast(true, Global));
    DT_ASSERT(DtNet_IsSourceSpecific(true, Ssm) && !DtNet_IsSourceSpecific(true, Group));
    DT_ASSERT(DtNet_IsLinkLocal(true, LinkLocal) &&
              DtNet_IsLinkLocal(true, LinkLocalEnd));
    DT_ASSERT(!DtNet_IsLinkLocal(true, SiteLocal));
    DT_ASSERT(DtNet_IsSiteLocalV6(SiteLocal) && !DtNet_IsSiteLocalV6(LinkLocal));
    DT_ASSERT(DtNet_IsGlobalV6(Global) && !DtNet_IsGlobalV6(NotGlobal));
}

DT_TEST(MasksAndSubnets)
{
    uint8_t Mask[16];
    static const uint8_t Mask24[16] = {255, 255, 255, 0};
    static const uint8_t Mask20[16] = {255, 255, 240, 0};
    static const uint8_t Mask64[16] = {255, 255, 255, 255, 255, 255, 255, 255};
    static const uint8_t A[16] = {192, 168, 1, 10};
    static const uint8_t B[16] = {192, 168, 1, 200};
    static const uint8_t C[16] = {192, 168, 2, 10};

    DtNet_PrefixMask(false, 24, Mask);
    DT_ASSERT_MEM(Mask, Mask24, 16);
    DtNet_PrefixMask(false, 20, Mask);
    DT_ASSERT_MEM(Mask, Mask20, 16);
    DtNet_PrefixMask(false, 64, Mask);
    DT_ASSERT_EQ(Mask[4], 0);
    DtNet_PrefixMask(true, 64, Mask);
    DT_ASSERT_MEM(Mask, Mask64, 16);

    DT_ASSERT(DtNet_SameSubnet(false, A, B, Mask24));
    DT_ASSERT(!DtNet_SameSubnet(false, A, C, Mask24));
    DT_ASSERT(DtNet_SameSubnet(false, A, C, Mask20));
}

DT_TEST(MulticastMacAddresses)
{
    uint8_t Mac[6];
    static const uint8_t V4[16] = {239, 129, 2, 3};
    static const uint8_t V4Mac[6] = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03};
    static const uint8_t V6[16] = {0xFF, 0x0E, 0, 0, 0,    0,    0,    0,
                                   0,    0,    0, 0, 0xAB, 0xCD, 0x12, 0x34};
    static const uint8_t V6Mac[6] = {0x33, 0x33, 0xAB, 0xCD, 0x12, 0x34};

    DtNet_MulticastMac(false, V4, Mac);
    DT_ASSERT_MEM(Mac, V4Mac, 6);
    DtNet_MulticastMac(true, V6, Mac);
    DT_ASSERT_MEM(Mac, V6Mac, 6);
}

DT_TEST_MAIN("NetAddr", DT_RUN(Ipv4Kinds), DT_RUN(Ipv6Kinds), DT_RUN(MasksAndSubnets),
             DT_RUN(MulticastMacAddresses))

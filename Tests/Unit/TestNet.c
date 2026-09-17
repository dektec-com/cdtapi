// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The host's network through OsNet.h: interfaces, addresses and sockets
//
// SPDX-License-Identifier: BSD-3-Clause
//
// These cases look at whatever network the machine has. A machine without an interface
// with an IPv4 address passes them with a note, as a build machine without a network
// should.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Live allocations.
#include "DtTest.h"       // Test framework.
#include "OAL/OsNet.h"    // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define MAX_ITFS 64
#define MAX_ADDRS 16

// An interface with a MAC address, no VLAN, and a preferred IPv4 address, which the
// cases below can use: false when the machine has none.
static bool FindUsable(OsNetItf* Itf, OsNetAddr* Addr)
{
    static const uint8_t NoMac[6] = {0};
    OsNetItf Itfs[MAX_ITFS];
    int Num = 0;

    if (OsNet_ListInterfaces(Itfs, MAX_ITFS, &Num) != OS_NET_OK)
        return false;
    for (int i = 0; i < Num; i++)
    {
        OsNetAddr Addrs[MAX_ADDRS];
        int NumAddrs = 0;
        if (memcmp(Itfs[i].Mac, NoMac, 6) == 0 || Itfs[i].VlanId != 0 ||
            !Itfs[i].LinkUp ||
            OsNet_GetAddresses(Itfs[i].Index, false, Addrs, MAX_ADDRS, &NumAddrs) !=
                OS_NET_OK)
        {
            continue;
        }
        for (int k = 0; k < NumAddrs; k++)
        {
            if (Addrs[k].State == OS_NET_ADDR_PREFERRED && Addrs[k].Ip[0] != 127)
            {
                *Itf = Itfs[i];
                *Addr = Addrs[k];
                return true;
            }
        }
    }
    return false;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(ListsInterfaces)
{
    OsNetItf Itfs[MAX_ITFS];
    int Num = -1;
    int Live = DtAlloc_Live();

    DT_ASSERT_OK(OsNet_ListInterfaces(Itfs, MAX_ITFS, &Num));
    DT_ASSERT(Num >= 0);
    for (int i = 0; i < Num; i++)
    {
        DT_ASSERT(Itfs[i].Index != 0);
        DT_ASSERT(memchr(Itfs[i].Name, '\0', sizeof(Itfs[i].Name)) != NULL);
        DT_ASSERT(!Itfs[i].LinkUp || Itfs[i].AdminUp);
        printf("    %u %s %02x:%02x:%02x:%02x:%02x:%02x vlan %d%s%s\n", Itfs[i].Index,
               Itfs[i].Name, Itfs[i].Mac[0], Itfs[i].Mac[1], Itfs[i].Mac[2],
               Itfs[i].Mac[3], Itfs[i].Mac[4], Itfs[i].Mac[5], Itfs[i].VlanId,
               Itfs[i].AdminUp ? " enabled" : "", Itfs[i].LinkUp ? " connected" : "");
    }
    int Counted = -1;
    int Outcome = OsNet_ListInterfaces(NULL, 0, &Counted);
    DT_ASSERT(Outcome == (Num > 0 ? OS_NET_TOO_SMALL : OS_NET_OK));
    DT_ASSERT_EQ(Counted, Num);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

DT_TEST(FindsAnInterfaceByItsMac)
{
    OsNetItf Usable;
    OsNetAddr Addr;
    OsNetItf Found;
    uint8_t Gateway[16];

    if (!FindUsable(&Usable, &Addr))
    {
        printf("    no connected interface with an IPv4 address; skipped\n");
        return;
    }
    DT_ASSERT_OK(OsNet_FindInterface(Usable.Mac, 0, &Found));
    DT_ASSERT_EQ(Found.Index, Usable.Index);
    DT_ASSERT(Addr.PrefixLength > 0 && Addr.PrefixLength <= 32);
    int Outcome = OsNet_GetGateway(Usable.Index, false, Gateway);
    DT_ASSERT(Outcome == OS_NET_OK || Outcome == OS_NET_NOT_FOUND);
    printf("    %s: %u.%u.%u.%u/%d, gateway %u.%u.%u.%u\n", Usable.Name, Addr.Ip[0],
           Addr.Ip[1], Addr.Ip[2], Addr.Ip[3], Addr.PrefixLength, Gateway[0], Gateway[1],
           Gateway[2], Gateway[3]);

    static const uint8_t Unknown[6] = {0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x01};
    DT_ASSERT_EQ(OsNet_FindInterface(Unknown, 0, &Found), OS_NET_NOT_FOUND);
    DT_ASSERT_EQ(OsNet_FindInterface(Usable.Mac, 4094, &Found), OS_NET_NOT_FOUND);
}

DT_TEST(BindsJoinsAndLeaves)
{
    static const uint8_t Group[16] = {239, 255, 77, 11};
    OsNetItf Usable;
    OsNetAddr Addr;
    OsNetSocket* Socket = NULL;
    int Live = DtAlloc_Live();

    if (!FindUsable(&Usable, &Addr))
    {
        printf("    no connected interface with an IPv4 address; skipped\n");
        return;
    }
    DT_ASSERT_OK(OsNetSocket_Bind(false, Addr.Ip, 0, Usable.Index, &Socket));
    DT_ASSERT(OsNetSocket_Port(Socket) != 0);
    OsNetSocket_Close(Socket);

    static const uint8_t Any[16] = {0};
    Socket = NULL;
    DT_ASSERT_OK(OsNetSocket_Bind(false, Any, 0, 0, &Socket));
    int Outcome = OsNetSocket_Join(Socket, Usable.Index, Group, NULL);
    if (Outcome == OS_NET_OK)
    {
        DT_ASSERT_OK(OsNetSocket_Leave(Socket, Usable.Index, Group, NULL));
    }
    else
    {
        printf("    joining 239.255.77.11 on %s failed; not checked\n", Usable.Name);
    }
    OsNetSocket_Close(Socket);
    DT_ASSERT_EQ(DtAlloc_Live(), Live);
}

DT_TEST(RoutesAndNeighbours)
{
    OsNetItf Usable;
    OsNetAddr Addr;
    uint8_t Gateway[16];
    uint8_t Mac[6];

    if (!FindUsable(&Usable, &Addr))
    {
        printf("    no connected interface with an IPv4 address; skipped\n");
        return;
    }

    // Another address in the own subnet is reached directly.
    if (Addr.PrefixLength <= 24)
    {
        uint8_t Neighbour[16];
        memcpy(Neighbour, Addr.Ip, 16);
        Neighbour[3] = Addr.Ip[3] == 100 ? 101 : 100;
        DT_ASSERT_OK(
            OsNet_GetBestRoute(Usable.Index, false, Addr.Ip, Neighbour, Gateway));
        static const uint8_t Empty[4] = {0};
        DT_ASSERT_MEM(Gateway, Empty, 4);
    }

    // TEST-NET-1 has no hosts; nothing on the link answers for it, unless a gateway
    // happens to. Either outcome must be clean.
    static const uint8_t Nobody[16] = {192, 0, 2, 213};
    int Outcome = OsNet_ResolveNeighbour(Usable.Index, false, Addr.Ip, Nobody, Mac);
    DT_ASSERT(Outcome == OS_NET_OK || Outcome == OS_NET_NOT_FOUND);

    // A default gateway routes TEST-NET-1 and answers for its own address.
    uint8_t Default[16];
    if (OsNet_GetGateway(Usable.Index, false, Default) != OS_NET_OK)
    {
        printf("    no default gateway; the gateway's MAC address not checked\n");
        return;
    }
    DT_ASSERT_OK(OsNet_GetBestRoute(Usable.Index, false, Addr.Ip, Nobody, Gateway));
    DT_ASSERT(Gateway[0] != 0);
    DT_ASSERT_OK(OsNet_ResolveNeighbour(Usable.Index, false, Addr.Ip, Default, Mac));
    static const uint8_t NoMac[6] = {0};
    DT_ASSERT(memcmp(Mac, NoMac, 6) != 0);
    printf("    route to 192.0.2.213 via %u.%u.%u.%u; gateway at "
           "%02x:%02x:%02x:%02x:%02x:%02x\n",
           Gateway[0], Gateway[1], Gateway[2], Gateway[3], Mac[0], Mac[1], Mac[2], Mac[3],
           Mac[4], Mac[5]);
}

DT_TEST(IpV6Addresses)
{
    OsNetItf Usable;
    OsNetAddr Addr;
    OsNetAddr Addrs[MAX_ADDRS];
    int Num = 0;

    if (!FindUsable(&Usable, &Addr))
    {
        printf("    no connected interface with an IPv4 address; skipped\n");
        return;
    }
    int Outcome = OsNet_GetAddresses(Usable.Index, true, Addrs, MAX_ADDRS, &Num);
    DT_ASSERT(Outcome == OS_NET_OK || Outcome == OS_NET_TOO_SMALL);
    for (int i = 0; i < Num && i < MAX_ADDRS; i++)
    {
        const uint8_t* Ip = Addrs[i].Ip;
        DT_ASSERT(Addrs[i].IpV6);
        DT_ASSERT(Addrs[i].PrefixLength > 0 && Addrs[i].PrefixLength <= 128);
        DT_ASSERT(Addrs[i].State >= OS_NET_ADDR_PREFERRED &&
                  Addrs[i].State <= OS_NET_ADDR_TENTATIVE);
        printf("    ");
        for (int k = 0; k < 16; k += 2)
            printf("%02x%02x%s", Ip[k], Ip[k + 1], k < 14 ? ":" : "");
        printf("/%d state %d\n", Addrs[i].PrefixLength, Addrs[i].State);
    }
}

DT_TEST_MAIN("Net", DT_RUN(ListsInterfaces), DT_RUN(FindsAnInterfaceByItsMac),
             DT_RUN(BindsJoinsAndLeaves), DT_RUN(RoutesAndNeighbours),
             DT_RUN(IpV6Addresses))

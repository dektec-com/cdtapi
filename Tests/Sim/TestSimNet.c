// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestSimNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The operating system's network and an IP port's use of it, emulated
//
// SPDX-License-Identifier: BSD-3-Clause
//
// CTest runs this with CDTAPI_SIM=1. Every case starts from the emulator's power-on
// state with a DTA-2110 and its interface added, and ends with no socket open and no
// allocation left.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Live allocations.
#include "DtTest.h"             // Test framework.
#include "Net/DtNet.h"          // An IP port's address, neighbours and groups.
#include "OAL/OsNet.h"          // The network under test.
#include "OAL/Sim/SimDtPcie.h"  // The emulator's reset.
#include "OAL/Sim/SimDta2110.h" // The DTA-2110's MAC address.
#include "OAL/Sim/SimNet.h"     // The emulated network and its controls.
#include "cdtapi.h"             // Results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The DTA-2110's driver index and interface.
#define INDEX 1
#define ITF SIM_NET_DTA2110_INDEX

static const uint8_t Mac2110[6] = SIM_DTA2110_MAC_ADDRESS;
static const uint8_t GatewayMac[6] = SIM_NET_GATEWAY_MAC;
static const uint8_t IpV4[16] = SIM_NET_DTA2110_IPV4;
static const uint8_t GatewayV4[16] = SIM_NET_DTA2110_GATEWAY_IPV4;
static const uint8_t LinkLocal[16] = SIM_NET_DTA2110_LINK_LOCAL;
static const uint8_t Global[16] = SIM_NET_DTA2110_GLOBAL;
static const uint8_t GatewayV6[16] = SIM_NET_DTA2110_GATEWAY_IPV6;
static const uint8_t Any[16] = {0};

// Adds the DTA-2110 with its interface, and gives the live allocations.
static int Start(void)
{
    SimDtPcie_Reset();
    SimDtPcie_SetDta2110Index(INDEX);
    return DtAlloc_NumLive();
}

// Checks that no socket is open and nothing is allocated, and resets the emulator.
#define FINISH(Live)                                                                     \
    do                                                                                   \
    {                                                                                    \
        DT_ASSERT_EQ(SimDtPcie_OpenNetSocketCount(), 0);                                 \
        DT_ASSERT_EQ(DtAlloc_NumLive(), (Live));                                         \
        SimDtPcie_Reset();                                                               \
    } while (0)

// An address of a family and state with a prefix.
static OsNetAddr Addr(bool IpV6, const uint8_t* Ip, int PrefixLength, int State)
{
    OsNetAddr New;

    memset(&New, 0, sizeof(New));
    New.IpV6 = IpV6;
    memcpy(New.Ip, Ip, IpV6 ? 16 : 4);
    New.PrefixLength = PrefixLength;
    New.State = State;
    return New;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interfaces +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(Dta2110BringsItsInterface)
{
    OsNetItf Itfs[4];
    int Num = -1;

    SimDtPcie_Reset();
    DT_ASSERT_OK(OsNet_ListInterfaces(Itfs, 4, &Num));
    DT_ASSERT_EQ(Num, 0);

    int Live = Start();
    DT_ASSERT_OK(OsNet_ListInterfaces(Itfs, 4, &Num));
    DT_ASSERT_EQ(Num, 1);
    DT_ASSERT_EQ(Itfs[0].Index, ITF);
    DT_ASSERT_MEM(Itfs[0].Mac, Mac2110, 6);
    DT_ASSERT_EQ(Itfs[0].VlanId, 0);
    DT_ASSERT(Itfs[0].AdminUp && Itfs[0].LinkUp);
    DT_ASSERT_STR(Itfs[0].Name, SIM_NET_DTA2110_NAME);
    DT_ASSERT_EQ(OsNet_ListInterfaces(NULL, 0, &Num), OS_NET_TOO_SMALL);
    DT_ASSERT_EQ(Num, 1);

    SimDtPcie_SetNetInterfaceUp(ITF, true, false);
    DT_ASSERT_OK(OsNet_ListInterfaces(Itfs, 4, &Num));
    DT_ASSERT(Itfs[0].AdminUp && !Itfs[0].LinkUp);

    SimDtPcie_SetDta2110Index(-1);
    DT_ASSERT_OK(OsNet_ListInterfaces(Itfs, 4, &Num));
    DT_ASSERT_EQ(Num, 0);
    FINISH(Live);
}

DT_TEST(FindsTheInterfaceAndItsVlans)
{
    static const uint8_t Other[6] = {0x00, 0x14, 0xF4, 0x08, 0x00, 0x02};
    OsNetItf Itf;
    int Live = Start();

    DT_ASSERT(SimDtPcie_AddNetInterface(2, Other, 0, 0, "other"));
    DT_ASSERT(SimDtPcie_AddNetInterface(3, Other, 100, 2, "other.100"));
    DT_ASSERT(SimDtPcie_AddNetInterface(7, Mac2110, 200, ITF, "dta2110.200"));
    DT_ASSERT(!SimDtPcie_AddNetInterface(7, Mac2110, 300, ITF, "taken"));

    DT_ASSERT_OK(OsNet_FindInterface(Mac2110, 0, &Itf));
    DT_ASSERT_EQ(Itf.Index, ITF);
    DT_ASSERT_OK(OsNet_FindInterface(Mac2110, 200, &Itf));
    DT_ASSERT_EQ(Itf.Index, 7);
    DT_ASSERT_EQ(Itf.ParentIndex, ITF);
    DT_ASSERT_EQ(OsNet_FindInterface(Mac2110, 100, &Itf), OS_NET_NOT_FOUND);
    DT_ASSERT_EQ(Itf.Index, 0);
    DT_ASSERT_OK(OsNet_FindInterface(Other, 100, &Itf));
    DT_ASSERT_EQ(Itf.Index, 3);

    SimDtPcie_RemoveNetInterface(ITF);
    DT_ASSERT_EQ(OsNet_FindInterface(Mac2110, 0, &Itf), OS_NET_NOT_FOUND);
    DT_ASSERT_EQ(OsNet_FindInterface(Mac2110, 200, &Itf), OS_NET_NOT_FOUND);
    FINISH(Live);
}

DT_TEST(AddressesAndGateways)
{
    OsNetAddr Addrs[4];
    uint8_t Gateway[16];
    int Num = -1;
    int Live = Start();

    DT_ASSERT_OK(OsNet_GetAddresses(ITF, false, Addrs, 4, &Num));
    DT_ASSERT_EQ(Num, 1);
    DT_ASSERT(!Addrs[0].IpV6);
    DT_ASSERT_MEM(Addrs[0].Ip, IpV4, 16);
    DT_ASSERT_EQ(Addrs[0].PrefixLength, 24);
    DT_ASSERT_EQ(Addrs[0].State, OS_NET_ADDR_PREFERRED);
    DT_ASSERT_OK(OsNet_GetAddresses(ITF, true, Addrs, 4, &Num));
    DT_ASSERT_EQ(Num, 2);
    DT_ASSERT_MEM(Addrs[0].Ip, LinkLocal, 16);
    DT_ASSERT_MEM(Addrs[1].Ip, Global, 16);
    DT_ASSERT_EQ(OsNet_GetAddresses(ITF, true, Addrs, 1, &Num), OS_NET_TOO_SMALL);
    DT_ASSERT_EQ(Num, 2);
    DT_ASSERT_EQ(OsNet_GetAddresses(99, true, Addrs, 4, &Num), OS_NET_NOT_FOUND);

    DT_ASSERT_OK(OsNet_GetGateway(ITF, false, Gateway));
    DT_ASSERT_MEM(Gateway, GatewayV4, 16);
    DT_ASSERT_OK(OsNet_GetGateway(ITF, true, Gateway));
    DT_ASSERT_MEM(Gateway, GatewayV6, 16);
    SimDtPcie_SetNetGateway(ITF, false, NULL);
    DT_ASSERT_EQ(OsNet_GetGateway(ITF, false, Gateway), OS_NET_NOT_FOUND);
    DT_ASSERT_MEM(Gateway, Any, 16);

    SimDtPcie_ClearNetAddresses(ITF, true);
    DT_ASSERT_OK(OsNet_GetAddresses(ITF, true, Addrs, 4, &Num));
    DT_ASSERT_EQ(Num, 0);
    DT_ASSERT_OK(OsNet_GetAddresses(ITF, false, Addrs, 4, &Num));
    DT_ASSERT_EQ(Num, 1);
    FINISH(Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Routes and neighbours +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(BestRoutes)
{
    static const uint8_t InSubnet[16] = {192, 168, 1, 77};
    static const uint8_t LinkLocalV4[16] = {169, 254, 3, 4};
    static const uint8_t Far[16] = {10, 1, 2, 3};
    static const uint8_t Net10[16] = {10, 0, 0, 0};
    static const uint8_t Net10_1[16] = {10, 1, 0, 0};
    static const uint8_t Router1[16] = {192, 168, 1, 2};
    static const uint8_t Router2[16] = {192, 168, 1, 3};
    uint8_t Gateway[16];
    int Live = Start();

    DT_ASSERT_OK(OsNet_GetBestRoute(ITF, false, IpV4, InSubnet, Gateway));
    DT_ASSERT_MEM(Gateway, Any, 16);
    DT_ASSERT_OK(OsNet_GetBestRoute(ITF, false, IpV4, LinkLocalV4, Gateway));
    DT_ASSERT_MEM(Gateway, Any, 16);
    DT_ASSERT_OK(OsNet_GetBestRoute(ITF, false, IpV4, Far, Gateway));
    DT_ASSERT_MEM(Gateway, GatewayV4, 16);

    DT_ASSERT(SimDtPcie_AddNetRoute(ITF, false, Net10, 8, Router1));
    DT_ASSERT(SimDtPcie_AddNetRoute(ITF, false, Net10_1, 16, Router2));
    DT_ASSERT_OK(OsNet_GetBestRoute(ITF, false, IpV4, Far, Gateway));
    DT_ASSERT_MEM(Gateway, Router2, 16);

    SimDtPcie_SetNetGateway(ITF, true, NULL);
    static const uint8_t FarV6[16] = {0x20, 0x01, 0x0D, 0xB9, 0, 0, 0, 0,
                                      0,    0,    0,    0,    0, 0, 0, 1};
    DT_ASSERT_EQ(OsNet_GetBestRoute(ITF, true, Global, FarV6, Gateway), OS_NET_NOT_FOUND);
    DT_ASSERT_EQ(OsNet_GetBestRoute(99, false, IpV4, Far, Gateway), OS_NET_NOT_FOUND);
    FINISH(Live);
}

DT_TEST(Neighbours)
{
    static const uint8_t Host[16] = {192, 168, 1, 20};
    static const uint8_t HostMac[6] = {0x02, 0, 0, 0, 0, 0x20};
    uint8_t Mac[6];
    int Live = Start();

    DT_ASSERT_OK(OsNet_ResolveNeighbour(ITF, false, IpV4, GatewayV4, Mac));
    DT_ASSERT_MEM(Mac, GatewayMac, 6);
    DT_ASSERT_OK(OsNet_ResolveNeighbour(ITF, true, LinkLocal, GatewayV6, Mac));
    DT_ASSERT_MEM(Mac, GatewayMac, 6);
    DT_ASSERT_EQ(OsNet_ResolveNeighbour(ITF, false, IpV4, Host, Mac), OS_NET_NOT_FOUND);
    DT_ASSERT(SimDtPcie_AddNetNeighbour(ITF, false, Host, HostMac));
    DT_ASSERT_OK(OsNet_ResolveNeighbour(ITF, false, IpV4, Host, Mac));
    DT_ASSERT_MEM(Mac, HostMac, 6);
    DT_ASSERT_EQ(OsNet_ResolveNeighbour(4, false, IpV4, Host, Mac), OS_NET_NOT_FOUND);
    FINISH(Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sockets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(BindingRules)
{
    static const uint8_t Foreign[16] = {192, 168, 1, 11};
    OsNetSocket* A = NULL;
    OsNetSocket* B = NULL;
    OsNetSocket* C = NULL;
    int Live = Start();

    DT_ASSERT_OK(OsNetSocket_Bind(false, Any, 0, 0, &A));
    DT_ASSERT_EQ(OsNetSocket_Port(A), 49152);
    DT_ASSERT_OK(OsNetSocket_Bind(false, IpV4, 5004, ITF, &B));
    DT_ASSERT_EQ(OsNetSocket_Port(B), 5004);
    DT_ASSERT_EQ(SimDtPcie_OpenNetSocketCount(), 2);
    DT_ASSERT_EQ(OsNetSocket_Bind(false, Foreign, 0, 0, &C), OS_NET_BIND);
    DT_ASSERT(C == NULL);
    DT_ASSERT_EQ(OsNetSocket_Bind(true, LinkLocal, 0, 0, &C), OS_NET_BIND);
    DT_ASSERT_OK(OsNetSocket_Bind(true, LinkLocal, 0, ITF, &C));
    DT_ASSERT_EQ(OsNetSocket_Port(C), 49153);
    OsNetSocket_Close(C);
    C = NULL;
    DT_ASSERT_OK(OsNetSocket_Bind(true, Global, 0, 0, &C));
    OsNetSocket_Close(C);
    C = NULL;

    SimDtPcie_FailNetBind(true);
    DT_ASSERT_EQ(OsNetSocket_Bind(false, Any, 0, 0, &C), OS_NET_BIND);
    SimDtPcie_FailNetBind(false);

    OsNetSocket_Close(A);
    OsNetSocket_Close(B);
    OsNetSocket_Close(NULL);
    DT_ASSERT_EQ(OsNetSocket_Port(NULL), 0);
    FINISH(Live);
}

DT_TEST(JoiningAndLeaving)
{
    static const uint8_t Group[16] = {239, 1, 2, 3};
    static const uint8_t Ssm[16] = {232, 1, 2, 3};
    static const uint8_t Source[16] = {192, 168, 1, 50};
    OsNetSocket* A = NULL;
    OsNetSocket* B = NULL;
    SimNetMembership Joined;
    int Live = Start();

    DT_ASSERT_OK(OsNetSocket_Bind(false, Any, 5004, 0, &A));
    DT_ASSERT_OK(OsNetSocket_Bind(false, Any, 5006, 0, &B));
    DT_ASSERT_OK(OsNetSocket_Join(A, ITF, Group, NULL));
    DT_ASSERT_EQ(OsNetSocket_Join(A, ITF, Group, NULL), OS_NET_JOIN);
    DT_ASSERT_OK(OsNetSocket_Join(A, ITF, Ssm, Source));
    DT_ASSERT_OK(OsNetSocket_Join(B, ITF, Group, NULL));
    DT_ASSERT_EQ(OsNetSocket_Join(A, ITF, Source, NULL), OS_NET_JOIN);
    DT_ASSERT_EQ(OsNetSocket_Join(A, 99, Group, Source), OS_NET_JOIN);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 3);

    DT_ASSERT(SimDtPcie_GetNetMembership(1, &Joined));
    DT_ASSERT_EQ(Joined.IfIndex, ITF);
    DT_ASSERT(!Joined.IpV6);
    DT_ASSERT_MEM(Joined.Group, Ssm, 16);
    DT_ASSERT(Joined.HasSource);
    DT_ASSERT_MEM(Joined.Source, Source, 16);
    DT_ASSERT_EQ(Joined.Port, 5004);
    DT_ASSERT(!SimDtPcie_GetNetMembership(3, &Joined));

    DT_ASSERT_EQ(OsNetSocket_Leave(A, ITF, Ssm, NULL), OS_NET_JOIN);
    DT_ASSERT_OK(OsNetSocket_Leave(A, ITF, Ssm, Source));
    DT_ASSERT_EQ(OsNetSocket_Leave(A, ITF, Ssm, Source), OS_NET_JOIN);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 2);

    SimDtPcie_FailNetJoin(true);
    DT_ASSERT_EQ(OsNetSocket_Join(A, ITF, Ssm, Source), OS_NET_JOIN);
    SimDtPcie_FailNetJoin(false);

    OsNetSocket_Close(A);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 1);
    DT_ASSERT(SimDtPcie_GetNetMembership(0, &Joined));
    DT_ASSERT_EQ(Joined.Port, 5006);
    OsNetSocket_Close(B);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 0);

    static const uint8_t GroupV6[16] = {0xFF, 0x0E, 0, 0, 0, 0, 0, 0,
                                        0,    0,    0, 0, 0, 0, 0, 0x42};
    DT_ASSERT_OK(OsNetSocket_Bind(true, Any, 0, 0, &A));
    DT_ASSERT_EQ(OsNetSocket_Join(A, ITF, Group, NULL), OS_NET_JOIN);
    DT_ASSERT_OK(OsNetSocket_Join(A, ITF, GroupV6, NULL));
    OsNetSocket_Close(A);
    FINISH(Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Own address +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(OwnAddressOfEachKind)
{
    static const uint8_t Mask24[16] = {255, 255, 255, 0};
    static const uint8_t Mask64[16] = {255, 255, 255, 255, 255, 255, 255, 255};
    DtNetOwnAddress Own;
    int Live = Start();

    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_IPV4, NULL, &Own));
    DT_ASSERT(!Own.IpV6);
    DT_ASSERT_EQ(Own.Itf.Index, ITF);
    DT_ASSERT_MEM(Own.Ip, IpV4, 16);
    DT_ASSERT_MEM(Own.Mask, Mask24, 16);
    DT_ASSERT_MEM(Own.Gateway, GatewayV4, 16);

    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_LINK_LOCAL, NULL, &Own));
    DT_ASSERT(Own.IpV6);
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);
    DT_ASSERT_MEM(Own.Mask, Mask64, 16);
    DT_ASSERT_MEM(Own.Gateway, GatewayV6, 16);
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_GLOBAL, NULL, &Own));
    DT_ASSERT_MEM(Own.Ip, Global, 16);
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_SITE_LOCAL, NULL, &Own),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_OTHER_V6, NULL, &Own),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_OTHER_V6, Global, &Own));
    DT_ASSERT_MEM(Own.Ip, Global, 16);

    SimDtPcie_SetNetGateway(ITF, false, NULL);
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_IPV4, NULL, &Own));
    DT_ASSERT_MEM(Own.Gateway, Any, 16);
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 0, 7, NULL, &Own), DTAPI_E_INVALID_ARG);
    FINISH(Live);
}

DT_TEST(PreferredBeforeDeprecatedNeverTentative)
{
    static const uint8_t Tentative[16] = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 1,
                                          0,    0,    0,    0,    0, 0, 0, 1};
    static const uint8_t Deprecated[16] = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 2,
                                           0,    0,    0,    0,    0, 0, 0, 1};
    DtNetOwnAddress Own;
    int Live = Start();

    SimDtPcie_ClearNetAddresses(ITF, true);
    OsNetAddr New = Addr(true, Tentative, 64, OS_NET_ADDR_TENTATIVE);
    DT_ASSERT(SimDtPcie_AddNetAddress(ITF, &New));
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_GLOBAL, NULL, &Own),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    New = Addr(true, Deprecated, 64, OS_NET_ADDR_DEPRECATED);
    DT_ASSERT(SimDtPcie_AddNetAddress(ITF, &New));
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_GLOBAL, NULL, &Own));
    DT_ASSERT_MEM(Own.Ip, Deprecated, 16);
    New = Addr(true, Global, 64, OS_NET_ADDR_PREFERRED);
    DT_ASSERT(SimDtPcie_AddNetAddress(ITF, &New));
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_GLOBAL, NULL, &Own));
    DT_ASSERT_MEM(Own.Ip, Global, 16);
    FINISH(Live);
}

DT_TEST(MissingInterfacesAndVlans)
{
    static const uint8_t Unknown[6] = {0x00, 0x14, 0xF4, 0x08, 0x00, 0x09};
    static const uint8_t VlanIp[16] = {10, 0, 100, 10};
    DtNetOwnAddress Own;
    int Live = Start();

    DT_ASSERT_EQ(DtNet_GetOwnAddress(Unknown, 0, DT_NET_ADDR_IPV4, NULL, &Own),
                 DTAPI_E_NW_DRIVER);
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Unknown, 100, DT_NET_ADDR_IPV4, NULL, &Own),
                 DTAPI_E_NW_DRIVER);
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 100, DT_NET_ADDR_IPV4, NULL, &Own),
                 DTAPI_E_VLAN_NOT_FOUND);
    DT_ASSERT_EQ(Own.Itf.Index, 0);

    DT_ASSERT(SimDtPcie_AddNetInterface(8, Mac2110, 100, ITF, "dta2110.100"));
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 100, DT_NET_ADDR_IPV4, NULL, &Own),
                 DTAPI_E_VLAN_NOT_FOUND);
    OsNetAddr New = Addr(false, VlanIp, 24, OS_NET_ADDR_PREFERRED);
    DT_ASSERT(SimDtPcie_AddNetAddress(8, &New));
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 100, DT_NET_ADDR_IPV4, NULL, &Own));
    DT_ASSERT_EQ(Own.Itf.Index, 8);
    DT_ASSERT_EQ(Own.Itf.VlanId, 100);
    DT_ASSERT_MEM(Own.Ip, VlanIp, 16);
    DT_ASSERT_MEM(Own.Gateway, Any, 16);

    SimDtPcie_ClearNetAddresses(ITF, false);
    DT_ASSERT_EQ(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_IPV4, NULL, &Own),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    FINISH(Live);
}

DT_TEST(InputAddressChoice)
{
    static const uint8_t GroupV6[16] = {0xFF, 0x0E, 0, 0, 0, 0, 0, 0,
                                        0,    0,    0, 0, 0, 0, 0, 0x42};
    static const uint8_t StreamGlobal[16] = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0,
                                             0,    0,    0,    0,    0, 0, 0, 0x99};
    static const uint8_t StreamSiteLocal[16] = {0xFD, 0, 0, 0, 0, 0, 0, 0,
                                                0,    0, 0, 0, 0, 0, 0, 0x99};
    static const uint8_t Group[16] = {239, 1, 2, 3};
    DtNetOwnAddress Own;
    int Live = Start();

    DT_ASSERT_OK(DtNet_ChooseInputAddress(Mac2110, 0, false, Group, &Own));
    DT_ASSERT_MEM(Own.Ip, IpV4, 16);
    DT_ASSERT_OK(DtNet_ChooseInputAddress(Mac2110, 0, true, GroupV6, &Own));
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);
    DT_ASSERT_OK(DtNet_ChooseInputAddress(Mac2110, 0, true, Any, &Own));
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);
    DT_ASSERT_OK(DtNet_ChooseInputAddress(Mac2110, 0, true, StreamGlobal, &Own));
    DT_ASSERT_MEM(Own.Ip, StreamGlobal, 16);
    DT_ASSERT_EQ(DtNet_ChooseInputAddress(Mac2110, 0, true, StreamSiteLocal, &Own),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);

    SimDtPcie_ClearNetAddresses(ITF, true);
    OsNetAddr New = Addr(true, Global, 64, OS_NET_ADDR_PREFERRED);
    DT_ASSERT(SimDtPcie_AddNetAddress(ITF, &New));
    DT_ASSERT_OK(DtNet_ChooseInputAddress(Mac2110, 0, true, GroupV6, &Own));
    DT_ASSERT_MEM(Own.Ip, Global, 16);
    FINISH(Live);
}

DT_TEST(OutputAddressChoice)
{
    static const uint8_t SiteLocal[16] = {0xFD, 0, 0, 0, 0, 0, 0, 0,
                                          0,    0, 0, 0, 0, 0, 0, 0x10};
    static const uint8_t ScopeLink[16] = {0xFF, 0x02, 0, 0, 0, 0, 0, 0,
                                          0,    0,    0, 0, 0, 0, 0, 0x42};
    static const uint8_t ScopeGlobal[16] = {0xFF, 0x0E, 0, 0, 0, 0, 0, 0,
                                            0,    0,    0, 0, 0, 0, 0, 0x42};
    static const uint8_t ScopeOther[16] = {0xFF, 0x06, 0, 0, 0, 0, 0, 0,
                                           0,    0,    0, 0, 0, 0, 0, 0x42};
    static const uint8_t DstGlobal[16] = {0x20, 0x01, 0x0D, 0xB9, 0, 0, 0, 0,
                                          0,    0,    0,    0,    0, 0, 0, 1};
    static const uint8_t DstLinkLocal[16] = {0xFE, 0x80, 0, 0, 0, 0, 0, 0,
                                             0,    0,    0, 0, 0, 0, 0, 0x77};
    static const uint8_t DstSiteLocal[16] = {0xFD, 0, 0, 0, 0, 0, 0, 0,
                                             0,    0, 0, 0, 0, 0, 0, 0x77};
    static const uint8_t Group[16] = {239, 1, 2, 3};
    DtNetOwnAddress Own;
    int Live = Start();

    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, false, Group, &Own));
    DT_ASSERT_MEM(Own.Ip, IpV4, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, ScopeGlobal, &Own));
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, ScopeOther, &Own));
    DT_ASSERT_MEM(Own.Ip, Global, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, DstGlobal, &Own));
    DT_ASSERT_MEM(Own.Ip, Global, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, DstSiteLocal, &Own));
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);

    OsNetAddr New = Addr(true, SiteLocal, 64, OS_NET_ADDR_PREFERRED);
    DT_ASSERT(SimDtPcie_AddNetAddress(ITF, &New));
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, ScopeGlobal, &Own));
    DT_ASSERT_MEM(Own.Ip, SiteLocal, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, ScopeLink, &Own));
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, DstLinkLocal, &Own));
    DT_ASSERT_MEM(Own.Ip, LinkLocal, 16);
    DT_ASSERT_OK(DtNet_ChooseOutputAddress(Mac2110, 0, true, DstSiteLocal, &Own));
    DT_ASSERT_MEM(Own.Ip, SiteLocal, 16);

    SimDtPcie_ClearNetAddresses(ITF, true);
    DT_ASSERT_EQ(DtNet_ChooseOutputAddress(Mac2110, 0, true, DstGlobal, &Own),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    FINISH(Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Destination MAC +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(DestinationMacIpV4)
{
    static const uint8_t Group[16] = {239, 129, 2, 3};
    static const uint8_t GroupMac[6] = {0x01, 0x00, 0x5E, 0x01, 0x02, 0x03};
    static const uint8_t Broadcast[16] = {192, 168, 1, 255};
    static const uint8_t LocalBroadcast[16] = {169, 254, 255, 255};
    static const uint8_t AllOnes[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t Host[16] = {192, 168, 1, 20};
    static const uint8_t HostMac[6] = {0x02, 0, 0, 0, 0, 0x20};
    static const uint8_t Far[16] = {10, 1, 2, 3};
    static const uint8_t Net10[16] = {10, 0, 0, 0};
    static const uint8_t Router[16] = {192, 168, 1, 2};
    static const uint8_t RouterMac[6] = {0x02, 0, 0, 0, 0, 0x02};
    DtNetOwnAddress Own;
    uint8_t Mac[6];
    int Live = Start();

    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_IPV4, NULL, &Own));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Group, NULL, Mac));
    DT_ASSERT_MEM(Mac, GroupMac, 6);
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Broadcast, NULL, Mac));
    DT_ASSERT_MEM(Mac, AllOnes, 6);
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, LocalBroadcast, NULL, Mac));
    DT_ASSERT_MEM(Mac, AllOnes, 6);

    DT_ASSERT_EQ(DtNet_ResolveDstMac(&Own, Host, NULL, Mac), DTAPI_E_DST_MAC_ADDR);
    DT_ASSERT(SimDtPcie_AddNetNeighbour(ITF, false, Host, HostMac));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Host, NULL, Mac));
    DT_ASSERT_MEM(Mac, HostMac, 6);

    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Far, NULL, Mac));
    DT_ASSERT_MEM(Mac, GatewayMac, 6);
    DT_ASSERT(SimDtPcie_AddNetRoute(ITF, false, Net10, 8, Router));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Far, NULL, Mac));
    DT_ASSERT_MEM(Mac, GatewayMac, 6);
    DT_ASSERT(SimDtPcie_AddNetNeighbour(ITF, false, Router, RouterMac));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Far, NULL, Mac));
    DT_ASSERT_MEM(Mac, RouterMac, 6);
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Far, GatewayV4, Mac));
    DT_ASSERT_MEM(Mac, GatewayMac, 6);
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Host, Router, Mac));
    DT_ASSERT_MEM(Mac, HostMac, 6);

    // Another address of the interface puts a neighbour outside Own's subnet on the link.
    static const uint8_t Second[16] = {10, 0, 0, 1};
    static const uint8_t OnLink[16] = {10, 0, 0, 9};
    static const uint8_t OnLinkMac[6] = {0x02, 0, 0, 0, 0, 0x09};
    OsNetAddr New = Addr(false, Second, 8, OS_NET_ADDR_PREFERRED);
    DT_ASSERT(SimDtPcie_AddNetAddress(ITF, &New));
    DT_ASSERT(SimDtPcie_AddNetNeighbour(ITF, false, OnLink, OnLinkMac));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, OnLink, NULL, Mac));
    DT_ASSERT_MEM(Mac, OnLinkMac, 6);

    static const uint8_t Nowhere[16] = {172, 16, 0, 1};
    SimDtPcie_SetNetGateway(ITF, false, NULL);
    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_IPV4, NULL, &Own));
    DT_ASSERT_EQ(DtNet_ResolveDstMac(&Own, Nowhere, NULL, Mac), DTAPI_E_DST_MAC_ADDR);
    DT_ASSERT_MEM(Mac, Any, 6);
    DT_ASSERT_EQ(DtNet_ResolveDstMac(&Own, Nowhere, Any, Mac), DTAPI_E_DST_MAC_ADDR);
    FINISH(Live);
}

DT_TEST(DestinationMacIpV6)
{
    static const uint8_t Group[16] = {0xFF, 0x0E, 0, 0, 0,    0,    0,    0,
                                      0,    0,    0, 0, 0xAB, 0xCD, 0x12, 0x34};
    static const uint8_t GroupMac[6] = {0x33, 0x33, 0xAB, 0xCD, 0x12, 0x34};
    static const uint8_t Peer[16] = {0xFE, 0x80, 0, 0, 0, 0, 0, 0,
                                     0,    0,    0, 0, 0, 0, 0, 0x33};
    static const uint8_t PeerMac[6] = {0x02, 0, 0, 0, 0, 0x33};
    static const uint8_t Far[16] = {0x20, 0x01, 0x0D, 0xB9, 0, 0, 0, 0,
                                    0,    0,    0,    0,    0, 0, 0, 1};
    DtNetOwnAddress Own;
    uint8_t Mac[6];
    int Live = Start();

    DT_ASSERT_OK(DtNet_GetOwnAddress(Mac2110, 0, DT_NET_ADDR_GLOBAL, NULL, &Own));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Group, NULL, Mac));
    DT_ASSERT_MEM(Mac, GroupMac, 6);
    DT_ASSERT_EQ(DtNet_ResolveDstMac(&Own, Peer, NULL, Mac), DTAPI_E_DST_MAC_ADDR);
    DT_ASSERT(SimDtPcie_AddNetNeighbour(ITF, true, Peer, PeerMac));
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Peer, NULL, Mac));
    DT_ASSERT_MEM(Mac, PeerMac, 6);
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Far, NULL, Mac));
    DT_ASSERT_MEM(Mac, GatewayMac, 6);
    DT_ASSERT_OK(DtNet_ResolveDstMac(&Own, Far, Peer, Mac));
    DT_ASSERT_MEM(Mac, PeerMac, 6);
    FINISH(Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Operation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(OperationalChecks)
{
    static const uint8_t Unknown[6] = {0x00, 0x14, 0xF4, 0x08, 0x00, 0x09};
    int Live = Start();

    DT_ASSERT_OK(DtNet_CheckOperational(Mac2110, 0, true, true));
    DT_ASSERT_OK(DtNet_CheckOperational(Mac2110, 0, false, false));
    DT_ASSERT_EQ(DtNet_CheckOperational(Unknown, 0, true, false), DTAPI_E_NW_DRIVER);
    DT_ASSERT_EQ(DtNet_CheckOperational(Mac2110, 10, true, false),
                 DTAPI_E_VLAN_NOT_FOUND);

    SimDtPcie_FailNetBind(true);
    DT_ASSERT_EQ(DtNet_CheckOperational(Mac2110, 0, false, true), DTAPI_E_BIND);
    SimDtPcie_FailNetBind(false);

    SimDtPcie_SetNetInterfaceUp(ITF, false, false);
    DT_ASSERT_EQ(DtNet_CheckOperational(Mac2110, 0, true, false), DTAPI_E_DISABLED);
    SimDtPcie_SetNetInterfaceUp(ITF, true, true);

    SimDtPcie_ClearNetAddresses(ITF, false);
    DT_ASSERT_EQ(DtNet_CheckOperational(Mac2110, 0, true, true),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    DT_ASSERT_OK(DtNet_CheckOperational(Mac2110, 0, false, true));
    SimDtPcie_ClearNetAddresses(ITF, true);
    DT_ASSERT_EQ(DtNet_CheckOperational(Mac2110, 0, false, true),
                 DTAPI_E_NO_ADAPTER_IP_ADDR);
    FINISH(Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Groups +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(GroupsWithSources)
{
    static const uint8_t Group[16] = {232, 1, 2, 3};
    static const uint8_t Sources[4 * 16] = {192, 168, 1,          50,  [16] = 192, 168,
                                            1,   51,  [32] = 192, 168, 1,          50};
    OsNetSocket* Socket = NULL;
    SimNetMembership Joined;
    int Live = Start();

    DT_ASSERT_OK(OsNetSocket_Bind(false, Any, 5004, 0, &Socket));
    DT_ASSERT_OK(DtNet_Join(Socket, ITF, false, Group, NULL, 0));
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 1);
    DT_ASSERT(SimDtPcie_GetNetMembership(0, &Joined));
    DT_ASSERT(!Joined.HasSource);
    DtNet_Leave(Socket, ITF, false, Group, NULL, 0);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 0);

    // Two distinct sources, the third a repeat of the first, the fourth any source.
    DT_ASSERT_OK(DtNet_Join(Socket, ITF, false, Group, Sources, 4));
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 3);
    DT_ASSERT(SimDtPcie_GetNetMembership(0, &Joined));
    DT_ASSERT(Joined.HasSource);
    DT_ASSERT_MEM(Joined.Source, Sources, 16);
    DT_ASSERT(SimDtPcie_GetNetMembership(1, &Joined));
    DT_ASSERT_MEM(Joined.Source, Sources + 16, 16);
    DT_ASSERT(SimDtPcie_GetNetMembership(2, &Joined));
    DT_ASSERT(!Joined.HasSource);
    DtNet_Leave(Socket, ITF, false, Group, Sources, 4);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 0);

    // A failure stops the joins and keeps the ones before it.
    DT_ASSERT_OK(OsNetSocket_Join(Socket, ITF, Group, Sources + 16));
    DT_ASSERT_EQ(DtNet_Join(Socket, ITF, false, Group, Sources, 4),
                 DTAPI_E_MULTICASTJOIN);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 2);
    DtNet_Leave(Socket, ITF, false, Group, Sources, 4);
    DT_ASSERT_EQ(SimDtPcie_NetMembershipCount(), 0);

    SimDtPcie_FailNetJoin(true);
    DT_ASSERT_EQ(DtNet_Join(Socket, ITF, false, Group, NULL, 0), DTAPI_E_MULTICASTJOIN);
    SimDtPcie_FailNetJoin(false);
    DT_ASSERT_EQ(DtNet_Join(NULL, ITF, false, Group, NULL, 0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtNet_Join(Socket, ITF, false, Group, NULL, 2), DTAPI_E_INVALID_ARG);
    OsNetSocket_Close(Socket);
    FINISH(Live);
}

DT_TEST_MAIN("SimNet", DT_RUN(Dta2110BringsItsInterface),
             DT_RUN(FindsTheInterfaceAndItsVlans), DT_RUN(AddressesAndGateways),
             DT_RUN(BestRoutes), DT_RUN(Neighbours), DT_RUN(BindingRules),
             DT_RUN(JoiningAndLeaving), DT_RUN(OwnAddressOfEachKind),
             DT_RUN(PreferredBeforeDeprecatedNeverTentative),
             DT_RUN(MissingInterfacesAndVlans), DT_RUN(InputAddressChoice),
             DT_RUN(OutputAddressChoice), DT_RUN(DestinationMacIpV4),
             DT_RUN(DestinationMacIpV6), DT_RUN(OperationalChecks),
             DT_RUN(GroupsWithSources))

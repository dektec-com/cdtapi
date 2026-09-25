// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* WinNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The host's network on Windows: IP Helper and Winsock
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Windows includes; Winsock before anything that includes windows.h.
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include <ws2tcpip.h>

#include <iphlpapi.h>

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "OAL/OsNet.h"    // Backend interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The interfaces come from GetAdaptersAddresses, which lists adapters with their unicast
// addresses; an adapter is identified by its IPv4 interface index, or by its IPv6 one
// where IPv4 is not bound. VLANs are not the operating system's on Windows, so every
// interface has VLAN ID 0.
//

// The flags of GetAdaptersAddresses: nothing that is not read. The prefix length comes
// with each unicast address, the gateway from the routing table.
#define WIN_NET_ADAPTER_FLAGS                                                            \
    (GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |        \
     GAA_FLAG_SKIP_FRIENDLY_NAME)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListAdapters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The adapters of Family, in an allocation the caller frees; NULL after a failure, with
// *Outcome saying why.
//
static IP_ADAPTER_ADDRESSES* ListAdapters(ULONG Family, int* Outcome)
{
    ULONG Size = 32 * 1024;

    for (int Try = 0; Try < 4; Try++)
    {
        IP_ADAPTER_ADDRESSES* List = (IP_ADAPTER_ADDRESSES*)DtAlloc_Malloc(Size);
        if (List == NULL)
        {
            *Outcome = OS_NET_NO_MEMORY;
            return NULL;
        }
        ULONG Error =
            GetAdaptersAddresses(Family, WIN_NET_ADAPTER_FLAGS, NULL, List, &Size);
        if (Error == NO_ERROR)
        {
            *Outcome = OS_NET_OK;
            return List;
        }
        DtAlloc_Free(List);
        if (Error != ERROR_BUFFER_OVERFLOW)
            break;
    }
    *Outcome = OS_NET_ERROR;
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AdapterIfIndex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t AdapterIfIndex(const IP_ADAPTER_ADDRESSES* Adapter)
{
    return Adapter->IfIndex != 0 ? Adapter->IfIndex : Adapter->Ipv6IfIndex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToSockAddr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void ToSockAddr(bool IpV6, const uint8_t* Ip, SOCKADDR_INET* Addr)
{
    memset(Addr, 0, sizeof(*Addr));
    if (IpV6)
    {
        Addr->si_family = AF_INET6;
        memcpy(&Addr->Ipv6.sin6_addr, Ip, 16);
    }
    else
    {
        Addr->si_family = AF_INET;
        memcpy(&Addr->Ipv4.sin_addr, Ip, 4);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromSockAddr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void FromSockAddr(const SOCKADDR_INET* Addr, uint8_t* Ip)
{
    memset(Ip, 0, 16);
    if (Addr->si_family == AF_INET6)
        memcpy(Ip, &Addr->Ipv6.sin6_addr, 16);
    else
        memcpy(Ip, &Addr->Ipv4.sin_addr, 4);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interfaces +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListInterfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Every adapter with a MAC address of 6 bytes. Its administrative state comes from
// GetIfEntry2.
//
static int ListInterfaces(OsNetItf* Itfs, int MaxItfs, int* NumItfs)
{
    int Outcome;
    IP_ADAPTER_ADDRESSES* List = ListAdapters(AF_UNSPEC, &Outcome);
    if (List == NULL)
        return Outcome;

    int Count = 0;
    for (IP_ADAPTER_ADDRESSES* Adapter = List; Adapter != NULL; Adapter = Adapter->Next)
    {
        if (Adapter->PhysicalAddressLength != 6)
            continue;
        if (Count < MaxItfs)
        {
            OsNetItf* Itf = &Itfs[Count];
            MIB_IF_ROW2 Row;

            memset(Itf, 0, sizeof(*Itf));
            Itf->Index = AdapterIfIndex(Adapter);
            memcpy(Itf->Mac, Adapter->PhysicalAddress, 6);
            memset(&Row, 0, sizeof(Row));
            Row.InterfaceIndex = Itf->Index;
            Itf->AdminUp = GetIfEntry2(&Row) == NO_ERROR &&
                           Row.AdminStatus == NET_IF_ADMIN_STATUS_UP;
            Itf->LinkUp = Adapter->OperStatus == IfOperStatusUp;
            snprintf(Itf->Name, sizeof(Itf->Name), "%s", Adapter->AdapterName);
        }
        Count++;
    }
    DtAlloc_Free(List);
    *NumItfs = Count;
    return Count > MaxItfs ? OS_NET_TOO_SMALL : OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetAddresses -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The unicast addresses in their duplicate address detection state; invalid and
// duplicate ones are left out.
//
static int GetAddresses(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                        int* NumAddrs)
{
    int Outcome;
    IP_ADAPTER_ADDRESSES* List = ListAdapters(IpV6 ? AF_INET6 : AF_INET, &Outcome);
    if (List == NULL)
        return Outcome;

    int Count = 0;
    Outcome = OS_NET_NOT_FOUND;
    for (IP_ADAPTER_ADDRESSES* Adapter = List; Adapter != NULL; Adapter = Adapter->Next)
    {
        if (Adapter->PhysicalAddressLength != 6 || AdapterIfIndex(Adapter) != IfIndex)
            continue;
        Outcome = OS_NET_OK;
        for (IP_ADAPTER_UNICAST_ADDRESS* Unicast = Adapter->FirstUnicastAddress;
             Unicast != NULL; Unicast = Unicast->Next)
        {
            const SOCKADDR* Sock = Unicast->Address.lpSockaddr;
            int State = Unicast->DadState == IpDadStatePreferred ? OS_NET_ADDR_PREFERRED
                        : Unicast->DadState == IpDadStateDeprecated
                            ? OS_NET_ADDR_DEPRECATED
                        : Unicast->DadState == IpDadStateTentative ? OS_NET_ADDR_TENTATIVE
                                                                   : -1;
            if (Sock == NULL || Sock->sa_family != (IpV6 ? AF_INET6 : AF_INET) ||
                State < 0)
                continue;
            if (Count < MaxAddrs)
            {
                OsNetAddr* Addr = &Addrs[Count];

                memset(Addr, 0, sizeof(*Addr));
                Addr->IpV6 = IpV6;
                if (IpV6)
                    memcpy(Addr->Ip, &((const SOCKADDR_IN6*)Sock)->sin6_addr, 16);
                else
                    memcpy(Addr->Ip, &((const SOCKADDR_IN*)Sock)->sin_addr, 4);
                Addr->PrefixLength = Unicast->OnLinkPrefixLength;
                Addr->State = State;
            }
            Count++;
        }
        break;
    }
    DtAlloc_Free(List);
    *NumAddrs = Count;
    if (Outcome == OS_NET_OK && Count > MaxAddrs)
        return OS_NET_TOO_SMALL;
    return Outcome;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Routes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetGateway -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The default route through the interface with the lowest metric.
//
static int GetGateway(uint32_t IfIndex, bool IpV6, uint8_t* Gateway)
{
    MIB_IPFORWARD_TABLE2* Table = NULL;

    if (GetIpForwardTable2(IpV6 ? AF_INET6 : AF_INET, &Table) != NO_ERROR)
        return OS_NET_ERROR;

    const MIB_IPFORWARD_ROW2* Best = NULL;
    for (ULONG i = 0; i < Table->NumEntries; i++)
    {
        const MIB_IPFORWARD_ROW2* Row = &Table->Table[i];
        uint8_t NextHop[16];

        FromSockAddr(&Row->NextHop, NextHop);
        bool HasNextHop = memcmp(NextHop, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) != 0;
        if (Row->InterfaceIndex == IfIndex && Row->DestinationPrefix.PrefixLength == 0 &&
            HasNextHop && (Best == NULL || Row->Metric < Best->Metric))
        {
            Best = Row;
        }
    }
    if (Best != NULL)
        FromSockAddr(&Best->NextHop, Gateway);
    FreeMibTable(Table);
    return Best != NULL ? OS_NET_OK : OS_NET_NOT_FOUND;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindBestRoute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int FindBestRoute(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                         const uint8_t* Dst, uint8_t* Gateway)
{
    SOCKADDR_INET Source;
    SOCKADDR_INET Destination;
    SOCKADDR_INET BestSource;
    MIB_IPFORWARD_ROW2 Row;

    ToSockAddr(IpV6, Src, &Source);
    ToSockAddr(IpV6, Dst, &Destination);
    if (GetBestRoute2(NULL, IfIndex, &Source, &Destination, 0, &Row, &BestSource) !=
        NO_ERROR)
    {
        return OS_NET_NOT_FOUND;
    }
    FromSockAddr(&Row.NextHop, Gateway);
    return OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResolveNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// ResolveIpNetEntry2 asks the network itself when the neighbour is not known, and waits
// for the answer.
//
static int ResolveNeighbour(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                            const uint8_t* Dst, uint8_t* Mac)
{
    MIB_IPNET_ROW2 Row;
    SOCKADDR_INET Source;

    memset(&Row, 0, sizeof(Row));
    Row.InterfaceIndex = IfIndex;
    ToSockAddr(IpV6, Dst, &Row.Address);
    ToSockAddr(IpV6, Src, &Source);
    if (ResolveIpNetEntry2(&Row, &Source) != NO_ERROR || Row.PhysicalAddressLength != 6 ||
        Row.State == NlnsUnreachable || Row.State == NlnsIncomplete)
    {
        return OS_NET_NOT_FOUND;
    }
    memcpy(Mac, Row.PhysicalAddress, 6);
    return OS_NET_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sockets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Every socket starts Winsock and cleans it up again; Winsock counts.
//

typedef struct WinSocket
{
    SOCKET Socket;
} WinSocket;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Bind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                void** Socket, uint16_t* BoundPort)
{
    WSADATA Data;

    *Socket = NULL;
    if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
        return OS_NET_ERROR;
    WinSocket* New = (WinSocket*)DtAlloc_Malloc(sizeof(WinSocket));
    if (New == NULL)
    {
        WSACleanup();
        return OS_NET_NO_MEMORY;
    }
    New->Socket = socket(IpV6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (New->Socket == INVALID_SOCKET)
    {
        DtAlloc_Free(New);
        WSACleanup();
        return OS_NET_ERROR;
    }

    SOCKADDR_STORAGE Addr;
    int AddrSize;
    memset(&Addr, 0, sizeof(Addr));
    if (IpV6)
    {
        SOCKADDR_IN6* Addr6 = (SOCKADDR_IN6*)&Addr;
        bool Scoped = Ip[0] == 0xFE && (Ip[1] & 0xC0) == 0x80;

        Addr6->sin6_family = AF_INET6;
        Addr6->sin6_port = htons(Port);
        memcpy(&Addr6->sin6_addr, Ip, 16);
        Addr6->sin6_scope_id = Scoped ? IfIndex : 0;
        AddrSize = sizeof(SOCKADDR_IN6);
    }
    else
    {
        SOCKADDR_IN* Addr4 = (SOCKADDR_IN*)&Addr;

        Addr4->sin_family = AF_INET;
        Addr4->sin_port = htons(Port);
        memcpy(&Addr4->sin_addr, Ip, 4);
        AddrSize = sizeof(SOCKADDR_IN);
    }

    const int On = 1;
    int AddrLen = AddrSize;
    if (setsockopt(New->Socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&On, sizeof(On)) !=
            0 ||
        bind(New->Socket, (SOCKADDR*)&Addr, AddrSize) != 0 ||
        getsockname(New->Socket, (SOCKADDR*)&Addr, &AddrLen) != 0)
    {
        closesocket(New->Socket);
        DtAlloc_Free(New);
        WSACleanup();
        return OS_NET_BIND;
    }
    *BoundPort =
        ntohs(IpV6 ? ((SOCKADDR_IN6*)&Addr)->sin6_port : ((SOCKADDR_IN*)&Addr)->sin_port);
    *Socket = New;
    return OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- JoinOrLeave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The protocol-independent multicast options, by interface index, for both families.
//
static int JoinOrLeave(void* State, bool Join, bool IpV6, uint32_t IfIndex,
                       const uint8_t* Group, const uint8_t* Source)
{
    const WinSocket* Socket = (const WinSocket*)State;
    int Level = IpV6 ? IPPROTO_IPV6 : IPPROTO_IP;
    int Result;

    if (Source == NULL)
    {
        GROUP_REQ Request;
        memset(&Request, 0, sizeof(Request));
        Request.gr_interface = IfIndex;
        SOCKADDR_INET GroupAddr;
        ToSockAddr(IpV6, Group, &GroupAddr);
        memcpy(&Request.gr_group, &GroupAddr, sizeof(GroupAddr));
        Result =
            setsockopt(Socket->Socket, Level, Join ? MCAST_JOIN_GROUP : MCAST_LEAVE_GROUP,
                       (const char*)&Request, sizeof(Request));
    }
    else
    {
        GROUP_SOURCE_REQ Request;
        memset(&Request, 0, sizeof(Request));
        Request.gsr_interface = IfIndex;
        SOCKADDR_INET Addr;
        ToSockAddr(IpV6, Group, &Addr);
        memcpy(&Request.gsr_group, &Addr, sizeof(Addr));
        ToSockAddr(IpV6, Source, &Addr);
        memcpy(&Request.gsr_source, &Addr, sizeof(Addr));
        Result = setsockopt(Socket->Socket, Level,
                            Join ? MCAST_JOIN_SOURCE_GROUP : MCAST_LEAVE_SOURCE_GROUP,
                            (const char*)&Request, sizeof(Request));
    }
    return Result == 0 ? OS_NET_OK : OS_NET_JOIN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Close(void* State)
{
    WinSocket* Socket = (WinSocket*)State;

    closesocket(Socket->Socket);
    DtAlloc_Free(Socket);
    WSACleanup();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_NetBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const OsNetBackend* OsPlatform_NetBackend(void)
{
    static const OsNetBackend Backend = {
        ListInterfaces,   GetAddresses, GetGateway,  FindBestRoute,
        ResolveNeighbour, Bind,         JoinOrLeave, Close};
    return &Backend;
}

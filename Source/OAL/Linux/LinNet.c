// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* LinNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The host's network on Linux: rtnetlink and sockets
//
// SPDX-License-Identifier: BSD-3-Clause

// Exposes the multicast requests and the netlink definitions of the C library.
#define _GNU_SOURCE

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// C library includes, before the kernel's, whose interface flags they would redefine
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>

// Linux includes
#include <linux/if_link.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "OAL/OsNet.h"    // Backend interface being implemented.
#include "OAL/OsThread.h" // Waiting for a neighbour.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Netlink +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Interfaces, addresses, routes and neighbours all come from rtnetlink: a request, and
// the kernel's messages until it says it is done.
//

// The bytes of one receive from the kernel.
#define LIN_NET_BUFFER 32768

// How often and how long to wait for a neighbour to answer, as DTAPI does.
#define LIN_NET_RESOLVE_TRIES 10
#define LIN_NET_RESOLVE_WAIT_V4_MS 200
#define LIN_NET_RESOLVE_WAIT_V6_MS 250

// A UDP socket, and the functions that open and close one.
typedef struct LinSocket
{
    int Handle;
} LinSocket;

static int Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                void** Socket, uint16_t* BoundPort);
static void Close(void* State);

// Handles one message; returns false to stop.
typedef bool (*LinNetHandler)(const struct nlmsghdr* Msg, void* Context);

// The neighbour states whose link-layer address is known, as the kernel's NUD_VALID,
// which it does not export.
#define LIN_NET_NUD_VALID                                                                \
    (NUD_PERMANENT | NUD_NOARP | NUD_REACHABLE | NUD_PROBE | NUD_STALE | NUD_DELAY)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MsgOk -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// NLMSG_OK and NLMSG_NEXT, and RTA_OK and RTA_NEXT below, with the signed byte count
// they mean but whose macros mix with unsigned lengths.
//
static bool MsgOk(const struct nlmsghdr* Msg, int Left)
{
    return Left >= (int)sizeof(struct nlmsghdr) &&
           Msg->nlmsg_len >= sizeof(struct nlmsghdr) && Msg->nlmsg_len <= (unsigned)Left;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextMsg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const struct nlmsghdr* NextMsg(const struct nlmsghdr* Msg, int* Left)
{
    int Step = (int)NLMSG_ALIGN(Msg->nlmsg_len);
    *Left -= Step;
    return (const struct nlmsghdr*)((const char*)Msg + Step);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttrOk -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool AttrOk(const struct rtattr* Attr, int Left)
{
    return Left >= (int)sizeof(struct rtattr) &&
           (size_t)Attr->rta_len >= sizeof(struct rtattr) && Attr->rta_len <= Left;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextAttr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static const struct rtattr* NextAttr(const struct rtattr* Attr, int* Left)
{
    int Step = (int)RTA_ALIGN(Attr->rta_len);
    *Left -= Step;
    return (const struct rtattr*)((const char*)Attr + Step);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transact -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sends Request, of the length its header gives, and hands every answer to Handler
// until the kernel is done. OS_NET_NOT_FOUND when the kernel answers with an error, such
// as for a route that does not exist.
//
static int Transact(struct nlmsghdr* Request, LinNetHandler Handler, void* Context)
{
    int Socket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (Socket < 0)
        return OS_NET_ERROR;
    const struct timeval Timeout = {2, 0};
    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, &Timeout, sizeof(Timeout));

    Request->nlmsg_seq = 1;
    if (send(Socket, Request, Request->nlmsg_len, 0) < 0)
    {
        close(Socket);
        return OS_NET_ERROR;
    }

    char* Buffer = (char*)DtAlloc_Malloc(LIN_NET_BUFFER);
    if (Buffer == NULL)
    {
        close(Socket);
        return OS_NET_NO_MEMORY;
    }
    bool Dump = (Request->nlmsg_flags & NLM_F_DUMP) == NLM_F_DUMP;
    int Outcome = OS_NET_OK;
    bool Done = false;
    while (!Done)
    {
        ssize_t Length = recv(Socket, Buffer, LIN_NET_BUFFER, 0);
        if (Length <= 0)
        {
            Outcome = OS_NET_ERROR;
            break;
        }
        int Left = (int)Length;
        for (const struct nlmsghdr* Msg = (const struct nlmsghdr*)Buffer;
             !Done && MsgOk(Msg, Left); Msg = NextMsg(Msg, &Left))
        {
            if (Msg->nlmsg_type == NLMSG_DONE)
                Done = true;
            else if (Msg->nlmsg_type == NLMSG_ERROR)
            {
                const struct nlmsgerr* Error = (const struct nlmsgerr*)NLMSG_DATA(Msg);
                Outcome = Error->error == 0 ? OS_NET_OK : OS_NET_NOT_FOUND;
                Done = true;
            }
            else
                Done = !Handler(Msg, Context) || !Dump;
        }
    }
    DtAlloc_Free(Buffer);
    close(Socket);
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddAttr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Appends an attribute to the request of Capacity bytes at Request, whose message header
// comes first. An attribute that does not fit is left out. The request is passed whole,
// so that the compiler sees the room behind its header.
//
static void AddAttr(uint8_t* Request, size_t Capacity, int Type, const void* Data,
                    int Length)
{
    struct nlmsghdr* Msg = (struct nlmsghdr*)(void*)Request;
    size_t Offset = NLMSG_ALIGN(Msg->nlmsg_len);
    size_t AttrLength = RTA_LENGTH((unsigned)Length);
    if (Offset + RTA_ALIGN(AttrLength) > Capacity)
        return;

    struct rtattr Attr;
    Attr.rta_type = (unsigned short)Type;
    Attr.rta_len = (unsigned short)AttrLength;
    memcpy(Request + Offset, &Attr, sizeof(Attr));
    memcpy(Request + Offset + RTA_LENGTH(0), Data, (size_t)Length);
    Msg->nlmsg_len = (uint32_t)(Offset + RTA_ALIGN(AttrLength));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interfaces +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct LinItfList
{
    OsNetItf* Itfs;
    int Max;
    int Count;
} LinItfList;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OnLink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An Ethernet link: its MAC address, name and parent, and, for a VLAN, its ID from the
// link information.
//
static bool OnLink(const struct nlmsghdr* Msg, void* Context)
{
    LinItfList* List = (LinItfList*)Context;
    const struct ifinfomsg* Info = (const struct ifinfomsg*)NLMSG_DATA(Msg);
    OsNetItf Itf;
    bool HasMac = false;

    if (Msg->nlmsg_type != RTM_NEWLINK || Info->ifi_type != ARPHRD_ETHER)
        return true;
    memset(&Itf, 0, sizeof(Itf));
    Itf.Index = (uint32_t)Info->ifi_index;
    Itf.AdminUp = (Info->ifi_flags & IFF_UP) != 0;
    Itf.LinkUp = Itf.AdminUp && (Info->ifi_flags & IFF_RUNNING) != 0;

    int Left = (int)IFLA_PAYLOAD(Msg);
    for (const struct rtattr* Attr = IFLA_RTA(Info); AttrOk(Attr, Left);
         Attr = NextAttr(Attr, &Left))
    {
        if (Attr->rta_type == IFLA_ADDRESS && RTA_PAYLOAD(Attr) == 6)
        {
            memcpy(Itf.Mac, RTA_DATA(Attr), 6);
            HasMac = true;
        }
        else if (Attr->rta_type == IFLA_IFNAME)
        {
            size_t Size = RTA_PAYLOAD(Attr) < sizeof(Itf.Name) ? RTA_PAYLOAD(Attr)
                                                               : sizeof(Itf.Name) - 1;
            memcpy(Itf.Name, RTA_DATA(Attr), Size);
            Itf.Name[sizeof(Itf.Name) - 1] = '\0';
        }
        else if (Attr->rta_type == IFLA_LINK && RTA_PAYLOAD(Attr) >= sizeof(uint32_t))
            memcpy(&Itf.ParentIndex, RTA_DATA(Attr), sizeof(uint32_t));
        else if (Attr->rta_type == IFLA_LINKINFO)
        {
            bool IsVlan = false;
            int InfoLeft = (int)RTA_PAYLOAD(Attr);
            for (const struct rtattr* Sub = (const struct rtattr*)RTA_DATA(Attr);
                 AttrOk(Sub, InfoLeft); Sub = NextAttr(Sub, &InfoLeft))
            {
                if (Sub->rta_type == IFLA_INFO_KIND)
                    IsVlan = strncmp((const char*)RTA_DATA(Sub), "vlan", 5) == 0;
                else if (Sub->rta_type == IFLA_INFO_DATA && IsVlan)
                {
                    int DataLeft = (int)RTA_PAYLOAD(Sub);
                    for (const struct rtattr* Vlan = (const struct rtattr*)RTA_DATA(Sub);
                         AttrOk(Vlan, DataLeft); Vlan = NextAttr(Vlan, &DataLeft))
                    {
                        uint16_t Id = 0;
                        if (Vlan->rta_type != IFLA_VLAN_ID ||
                            RTA_PAYLOAD(Vlan) < sizeof(Id))
                            continue;
                        memcpy(&Id, RTA_DATA(Vlan), sizeof(Id));
                        Itf.VlanId = Id;
                    }
                }
            }
        }
    }
    if (Itf.VlanId == 0)
        Itf.ParentIndex = 0;
    if (HasMac)
    {
        if (List->Count < List->Max)
            List->Itfs[List->Count] = Itf;
        List->Count++;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListInterfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int ListInterfaces(OsNetItf* Itfs, int MaxItfs, int* NumItfs)
{
    struct
    {
        struct nlmsghdr Hdr;
        struct ifinfomsg Info;
    } Request;
    LinItfList List = {Itfs, MaxItfs, 0};

    memset(&Request, 0, sizeof(Request));
    Request.Hdr.nlmsg_len = NLMSG_LENGTH(sizeof(Request.Info));
    Request.Hdr.nlmsg_type = RTM_GETLINK;
    Request.Hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    Request.Info.ifi_family = AF_UNSPEC;
    int Outcome = Transact(&Request.Hdr, OnLink, &List);
    *NumItfs = List.Count;
    if (Outcome != OS_NET_OK)
        return Outcome;
    return List.Count > MaxItfs ? OS_NET_TOO_SMALL : OS_NET_OK;
}

typedef struct LinAddrList
{
    uint32_t IfIndex;
    bool IpV6;
    OsNetAddr* Addrs;
    int Max;
    int Count;
} LinAddrList;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OnAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An address of the interface. The flags come from IFA_FLAGS when the kernel gives it;
// an IPv4 address is the local one, which differs from the peer's on a point-to-point
// link.
//
static bool OnAddress(const struct nlmsghdr* Msg, void* Context)
{
    LinAddrList* List = (LinAddrList*)Context;
    const struct ifaddrmsg* Info = (const struct ifaddrmsg*)NLMSG_DATA(Msg);
    uint32_t Flags = Info->ifa_flags;
    const uint8_t* Address = NULL;
    const uint8_t* Local = NULL;
    size_t Size = List->IpV6 ? 16 : 4;

    if (Msg->nlmsg_type != RTM_NEWADDR || Info->ifa_index != List->IfIndex ||
        Info->ifa_family != (List->IpV6 ? AF_INET6 : AF_INET))
    {
        return true;
    }
    int Left = (int)IFA_PAYLOAD(Msg);
    for (const struct rtattr* Attr = IFA_RTA(Info); AttrOk(Attr, Left);
         Attr = NextAttr(Attr, &Left))
    {
        if (Attr->rta_type == IFA_ADDRESS && RTA_PAYLOAD(Attr) >= Size)
            Address = (const uint8_t*)RTA_DATA(Attr);
        else if (Attr->rta_type == IFA_LOCAL && RTA_PAYLOAD(Attr) >= Size)
            Local = (const uint8_t*)RTA_DATA(Attr);
        else if (Attr->rta_type == IFA_FLAGS && RTA_PAYLOAD(Attr) >= sizeof(Flags))
            memcpy(&Flags, RTA_DATA(Attr), sizeof(Flags));
    }
    if (!List->IpV6 && Local != NULL)
        Address = Local;
    if (Address == NULL || (Flags & IFA_F_DADFAILED) != 0)
        return true;

    if (List->Count < List->Max)
    {
        OsNetAddr* Addr = &List->Addrs[List->Count];

        memset(Addr, 0, sizeof(*Addr));
        Addr->IpV6 = List->IpV6;
        memcpy(Addr->Ip, Address, Size);
        Addr->PrefixLength = Info->ifa_prefixlen;
        Addr->State = (Flags & IFA_F_TENTATIVE) != 0 && (Flags & IFA_F_OPTIMISTIC) == 0
                          ? OS_NET_ADDR_TENTATIVE
                      : (Flags & IFA_F_DEPRECATED) != 0 ? OS_NET_ADDR_DEPRECATED
                                                        : OS_NET_ADDR_PREFERRED;
    }
    List->Count++;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetAddresses -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetAddresses(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                        int* NumAddrs)
{
    struct
    {
        struct nlmsghdr Hdr;
        struct ifaddrmsg Info;
    } Request;
    LinAddrList List = {IfIndex, IpV6, Addrs, MaxAddrs, 0};

    if (if_indextoname(IfIndex, (char[IF_NAMESIZE]){0}) == NULL)
        return OS_NET_NOT_FOUND;
    memset(&Request, 0, sizeof(Request));
    Request.Hdr.nlmsg_len = NLMSG_LENGTH(sizeof(Request.Info));
    Request.Hdr.nlmsg_type = RTM_GETADDR;
    Request.Hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    Request.Info.ifa_family = IpV6 ? AF_INET6 : AF_INET;
    int Outcome = Transact(&Request.Hdr, OnAddress, &List);
    *NumAddrs = List.Count;
    if (Outcome != OS_NET_OK)
        return Outcome;
    return List.Count > MaxAddrs ? OS_NET_TOO_SMALL : OS_NET_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Routes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct LinRoute
{
    uint32_t IfIndex; // 0 for any
    bool IpV6;
    bool DefaultOnly;
    bool Found;
    uint32_t Priority;
    uint8_t Gateway[16];
} LinRoute;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OnRoute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A route of the main table through the interface: the default route with a gateway and
// the lowest priority, or the one route the kernel answered a lookup with.
//
static bool OnRoute(const struct nlmsghdr* Msg, void* Context)
{
    LinRoute* Route = (LinRoute*)Context;
    const struct rtmsg* Info = (const struct rtmsg*)NLMSG_DATA(Msg);
    uint32_t Table = Info->rtm_table;
    uint32_t OutIndex = 0;
    uint32_t Priority = 0;
    const uint8_t* Gateway = NULL;
    size_t Size = Route->IpV6 ? 16 : 4;

    if (Msg->nlmsg_type != RTM_NEWROUTE)
        return true;
    int Left = (int)RTM_PAYLOAD(Msg);
    for (const struct rtattr* Attr = RTM_RTA(Info); AttrOk(Attr, Left);
         Attr = NextAttr(Attr, &Left))
    {
        if (Attr->rta_type == RTA_TABLE && RTA_PAYLOAD(Attr) >= sizeof(Table))
            memcpy(&Table, RTA_DATA(Attr), sizeof(Table));
        else if (Attr->rta_type == RTA_OIF && RTA_PAYLOAD(Attr) >= sizeof(OutIndex))
            memcpy(&OutIndex, RTA_DATA(Attr), sizeof(OutIndex));
        else if (Attr->rta_type == RTA_PRIORITY && RTA_PAYLOAD(Attr) >= sizeof(Priority))
            memcpy(&Priority, RTA_DATA(Attr), sizeof(Priority));
        else if (Attr->rta_type == RTA_GATEWAY && RTA_PAYLOAD(Attr) >= Size)
            Gateway = (const uint8_t*)RTA_DATA(Attr);
    }

    if (Route->DefaultOnly)
    {
        if (Table != RT_TABLE_MAIN || Info->rtm_dst_len != 0 || Gateway == NULL ||
            OutIndex != Route->IfIndex || (Route->Found && Priority >= Route->Priority))
        {
            return true;
        }
    }
    Route->Found = true;
    Route->Priority = Priority;
    memset(Route->Gateway, 0, 16);
    if (Gateway != NULL)
        memcpy(Route->Gateway, Gateway, Size);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetGateway -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetGateway(uint32_t IfIndex, bool IpV6, uint8_t* Gateway)
{
    struct
    {
        struct nlmsghdr Hdr;
        struct rtmsg Info;
    } Request;
    LinRoute Route;

    memset(&Route, 0, sizeof(Route));
    Route.IfIndex = IfIndex;
    Route.IpV6 = IpV6;
    Route.DefaultOnly = true;
    memset(&Request, 0, sizeof(Request));
    Request.Hdr.nlmsg_len = NLMSG_LENGTH(sizeof(Request.Info));
    Request.Hdr.nlmsg_type = RTM_GETROUTE;
    Request.Hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    Request.Info.rtm_family = IpV6 ? AF_INET6 : AF_INET;
    int Outcome = Transact(&Request.Hdr, OnRoute, &Route);
    if (Outcome != OS_NET_OK)
        return Outcome;
    if (!Route.Found)
        return OS_NET_NOT_FOUND;
    memcpy(Gateway, Route.Gateway, 16);
    return OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetBestRoute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Asks the kernel which route it takes from Src to Dst out of the interface, as "ip route
// get" does.
//
static int GetBestRoute(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                        const uint8_t* Dst, uint8_t* Gateway)
{
    struct
    {
        struct nlmsghdr Hdr;
        struct rtmsg Info;
        char Attrs[128];
    } Request;
    LinRoute Route;
    int Size = IpV6 ? 16 : 4;

    memset(&Route, 0, sizeof(Route));
    Route.IpV6 = IpV6;
    memset(&Request, 0, sizeof(Request));
    Request.Hdr.nlmsg_len = NLMSG_LENGTH(sizeof(Request.Info));
    Request.Hdr.nlmsg_type = RTM_GETROUTE;
    Request.Hdr.nlmsg_flags = NLM_F_REQUEST;
    Request.Info.rtm_family = IpV6 ? AF_INET6 : AF_INET;
    Request.Info.rtm_dst_len = (unsigned char)(Size * 8);
    Request.Info.rtm_src_len = (unsigned char)(Size * 8);
    AddAttr((uint8_t*)&Request, sizeof(Request), RTA_DST, Dst, Size);
    AddAttr((uint8_t*)&Request, sizeof(Request), RTA_SRC, Src, Size);
    AddAttr((uint8_t*)&Request, sizeof(Request), RTA_OIF, &IfIndex, sizeof(IfIndex));
    int Outcome = Transact(&Request.Hdr, OnRoute, &Route);
    if (Outcome != OS_NET_OK)
        return Outcome;
    if (!Route.Found)
        return OS_NET_NOT_FOUND;
    memcpy(Gateway, Route.Gateway, 16);
    return OS_NET_OK;
}

typedef struct LinNeighbour
{
    uint32_t IfIndex;
    bool IpV6;
    const uint8_t* Dst;
    bool Found;
    uint8_t Mac[6];
} LinNeighbour;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OnNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The neighbour when its link-layer address counts: for IPv4 any complete entry, as
// SIOCGARP gives, for IPv6 a reachable one or one being confirmed, as DTAPI takes.
//
static bool OnNeighbour(const struct nlmsghdr* Msg, void* Context)
{
    LinNeighbour* Neighbour = (LinNeighbour*)Context;
    const struct ndmsg* Info = (const struct ndmsg*)NLMSG_DATA(Msg);
    const uint8_t* Address = NULL;
    const uint8_t* Mac = NULL;
    size_t Size = Neighbour->IpV6 ? 16 : 4;
    unsigned States = Neighbour->IpV6 ? (NUD_REACHABLE | NUD_DELAY) : LIN_NET_NUD_VALID;

    if (Msg->nlmsg_type != RTM_NEWNEIGH ||
        (uint32_t)Info->ndm_ifindex != Neighbour->IfIndex ||
        (Info->ndm_state & States) == 0)
    {
        return true;
    }
    int Left = (int)(Msg->nlmsg_len - NLMSG_LENGTH(sizeof(*Info)));
    for (const struct rtattr* Attr =
             (const struct rtattr*)((const char*)Info + NLMSG_ALIGN(sizeof(*Info)));
         AttrOk(Attr, Left); Attr = NextAttr(Attr, &Left))
    {
        if (Attr->rta_type == NDA_DST && RTA_PAYLOAD(Attr) == Size)
            Address = (const uint8_t*)RTA_DATA(Attr);
        else if (Attr->rta_type == NDA_LLADDR && RTA_PAYLOAD(Attr) == 6)
            Mac = (const uint8_t*)RTA_DATA(Attr);
    }
    if (Address == NULL || Mac == NULL || memcmp(Address, Neighbour->Dst, Size) != 0)
        return true;
    memcpy(Neighbour->Mac, Mac, 6);
    Neighbour->Found = true;
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Prod -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sends a datagram to port 4 of Dst from Src, which makes the kernel ask the network for
// its link-layer address.
//
static void Prod(uint32_t IfIndex, bool IpV6, const uint8_t* Src, const uint8_t* Dst)
{
    static const char Data[10] = "DektecArp";
    void* State = NULL;
    uint16_t Port = 0;

    if (Bind(IpV6, Src, 0, IfIndex, &State, &Port) != OS_NET_OK)
        return;
    int Handle = ((LinSocket*)State)->Handle;
    if (IpV6)
    {
        struct sockaddr_in6 Addr;
        memset(&Addr, 0, sizeof(Addr));
        Addr.sin6_family = AF_INET6;
        Addr.sin6_port = htons(4);
        memcpy(&Addr.sin6_addr, Dst, 16);
        Addr.sin6_scope_id = Dst[0] == 0xFE && (Dst[1] & 0xC0) == 0x80 ? IfIndex : 0;
        sendto(Handle, Data, sizeof(Data), 0, (struct sockaddr*)&Addr, sizeof(Addr));
    }
    else
    {
        struct sockaddr_in Addr;
        memset(&Addr, 0, sizeof(Addr));
        Addr.sin_family = AF_INET;
        Addr.sin_port = htons(4);
        memcpy(&Addr.sin_addr, Dst, 4);
        sendto(Handle, Data, sizeof(Data), 0, (struct sockaddr*)&Addr, sizeof(Addr));
    }
    Close(State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResolveNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int ResolveNeighbour(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                            const uint8_t* Dst, uint8_t* Mac)
{
    struct
    {
        struct nlmsghdr Hdr;
        struct ndmsg Info;
    } Request;
    LinNeighbour Neighbour;

    memset(&Neighbour, 0, sizeof(Neighbour));
    Neighbour.IfIndex = IfIndex;
    Neighbour.IpV6 = IpV6;
    Neighbour.Dst = Dst;
    for (int Try = 0; Try <= LIN_NET_RESOLVE_TRIES; Try++)
    {
        memset(&Request, 0, sizeof(Request));
        Request.Hdr.nlmsg_len = NLMSG_LENGTH(sizeof(Request.Info));
        Request.Hdr.nlmsg_type = RTM_GETNEIGH;
        Request.Hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        Request.Info.ndm_family = IpV6 ? AF_INET6 : AF_INET;
        Request.Info.ndm_ifindex = (int)IfIndex;
        int Outcome = Transact(&Request.Hdr, OnNeighbour, &Neighbour);
        if (Outcome != OS_NET_OK && !Neighbour.Found)
            return Outcome;
        if (Neighbour.Found || Try == LIN_NET_RESOLVE_TRIES)
            break;
        Prod(IfIndex, IpV6, Src, Dst);
        OsTime_SleepMs(IpV6 ? LIN_NET_RESOLVE_WAIT_V6_MS : LIN_NET_RESOLVE_WAIT_V4_MS);
    }
    if (!Neighbour.Found)
        return OS_NET_NOT_FOUND;
    memcpy(Mac, Neighbour.Mac, 6);
    return OS_NET_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sockets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Bind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                void** Socket, uint16_t* BoundPort)
{
    struct sockaddr_storage Addr;
    socklen_t AddrSize;

    *Socket = NULL;
    memset(&Addr, 0, sizeof(Addr));
    if (IpV6)
    {
        struct sockaddr_in6* Addr6 = (struct sockaddr_in6*)&Addr;
        bool Scoped = Ip[0] == 0xFE && (Ip[1] & 0xC0) == 0x80;

        Addr6->sin6_family = AF_INET6;
        Addr6->sin6_port = htons(Port);
        memcpy(&Addr6->sin6_addr, Ip, 16);
        Addr6->sin6_scope_id = Scoped ? IfIndex : 0;
        AddrSize = sizeof(struct sockaddr_in6);
    }
    else
    {
        struct sockaddr_in* Addr4 = (struct sockaddr_in*)&Addr;

        Addr4->sin_family = AF_INET;
        Addr4->sin_port = htons(Port);
        memcpy(&Addr4->sin_addr, Ip, 4);
        AddrSize = sizeof(struct sockaddr_in);
    }

    LinSocket* New = (LinSocket*)DtAlloc_Malloc(sizeof(LinSocket));
    if (New == NULL)
        return OS_NET_NO_MEMORY;
    New->Handle =
        socket(IpV6 ? AF_INET6 : AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (New->Handle < 0)
    {
        DtAlloc_Free(New);
        return OS_NET_ERROR;
    }
    const int On = 1;
    socklen_t Bound = sizeof(Addr);
    if (setsockopt(New->Handle, SOL_SOCKET, SO_REUSEADDR, &On, sizeof(On)) != 0 ||
        bind(New->Handle, (struct sockaddr*)&Addr, AddrSize) != 0 ||
        getsockname(New->Handle, (struct sockaddr*)&Addr, &Bound) != 0)
    {
        close(New->Handle);
        DtAlloc_Free(New);
        return OS_NET_BIND;
    }
    *BoundPort = ntohs(IpV6 ? ((struct sockaddr_in6*)&Addr)->sin6_port
                            : ((struct sockaddr_in*)&Addr)->sin_port);
    *Socket = New;
    return OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToStorage -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ToStorage(bool IpV6, const uint8_t* Ip, struct sockaddr_storage* Storage)
{
    memset(Storage, 0, sizeof(*Storage));
    if (IpV6)
    {
        struct sockaddr_in6* Addr = (struct sockaddr_in6*)Storage;
        Addr->sin6_family = AF_INET6;
        memcpy(&Addr->sin6_addr, Ip, 16);
    }
    else
    {
        struct sockaddr_in* Addr = (struct sockaddr_in*)Storage;
        Addr->sin_family = AF_INET;
        memcpy(&Addr->sin_addr, Ip, 4);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Membership -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The protocol-independent multicast options, by interface index, for both families.
//
static int Membership(void* State, bool Join, bool IpV6, uint32_t IfIndex,
                      const uint8_t* Group, const uint8_t* Source)
{
    const LinSocket* Socket = (const LinSocket*)State;
    int Level = IpV6 ? IPPROTO_IPV6 : IPPROTO_IP;
    int Result;

    if (Source == NULL)
    {
        struct group_req Request;
        memset(&Request, 0, sizeof(Request));
        Request.gr_interface = IfIndex;
        ToStorage(IpV6, Group, &Request.gr_group);
        Result =
            setsockopt(Socket->Handle, Level, Join ? MCAST_JOIN_GROUP : MCAST_LEAVE_GROUP,
                       &Request, sizeof(Request));
    }
    else
    {
        struct group_source_req Request;
        memset(&Request, 0, sizeof(Request));
        Request.gsr_interface = IfIndex;
        ToStorage(IpV6, Group, &Request.gsr_group);
        ToStorage(IpV6, Source, &Request.gsr_source);
        Result = setsockopt(Socket->Handle, Level,
                            Join ? MCAST_JOIN_SOURCE_GROUP : MCAST_LEAVE_SOURCE_GROUP,
                            &Request, sizeof(Request));
    }
    return Result == 0 ? OS_NET_OK : OS_NET_JOIN;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Close(void* State)
{
    LinSocket* Socket = (LinSocket*)State;

    close(Socket->Handle);
    DtAlloc_Free(Socket);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_NetBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const OsNetBackend* OsPlatform_NetBackend(void)
{
    static const OsNetBackend Backend = {
        ListInterfaces,   GetAddresses, GetGateway, GetBestRoute,
        ResolveNeighbour, Bind,         Membership, Close};
    return &Backend;
}

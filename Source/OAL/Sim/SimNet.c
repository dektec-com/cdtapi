// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SimNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated network of the operating system, and its test controls
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "SimDtPcie.h"    // The lock.
#include "SimNet.h"       // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimRoute
{
    bool IpV6;
    uint8_t Dst[16];
    int PrefixLength;
    uint8_t Gateway[16];
} SimRoute;

typedef struct SimItfEntry
{
    bool Used;
    OsNetItf Itf;
    OsNetAddr Addrs[SIM_NET_MAX_ADDRESSES];
    int NumAddrs;
    bool HasGateway[2]; // IPv4, IPv6
    uint8_t Gateway[2][16];
    SimRoute Routes[SIM_NET_MAX_ROUTES];
    int NumRoutes;
} SimItfEntry;

typedef struct SimNeighbour
{
    uint32_t IfIndex;
    bool IpV6;
    uint8_t Ip[16];
    uint8_t Mac[6];
} SimNeighbour;

typedef struct SimSocket
{
    bool IpV6;
    uint16_t Port;
} SimSocket;

typedef struct SimMembership
{
    SimSocket* Socket;
    SimNetMembership Membership;
} SimMembership;

static struct
{
    SimItfEntry Itfs[SIM_NET_MAX_INTERFACES];
    SimNeighbour Neighbours[SIM_NET_MAX_NEIGHBOURS];
    int NumNeighbours;
    SimMembership Joined[SIM_NET_MAX_MEMBERSHIPS];
    int NumJoined;
    int NumOpenSockets;
    uint16_t NextPort;
    bool FailBind;
    bool FailJoin;
} g_Net = {.NextPort = 49152};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindItf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static SimItfEntry* FindItf(uint32_t Index)
{
    for (int i = 0; i < SIM_NET_MAX_INTERFACES; i++)
    {
        if (g_Net.Itfs[i].Used && g_Net.Itfs[i].Itf.Index == Index)
            return &g_Net.Itfs[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PrefixMatches -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether the first PrefixLength bits of A and B are the same.
//
static bool PrefixMatches(const uint8_t* A, const uint8_t* B, int PrefixLength)
{
    for (int Bit = 0; Bit < PrefixLength; Bit++)
    {
        int Mask = 0x80 >> (Bit % 8);
        if ((A[Bit / 8] & Mask) != (B[Bit / 8] & Mask))
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsEmpty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsEmpty(const uint8_t* Ip)
{
    for (int i = 0; i < 16; i++)
    {
        if (Ip[i] != 0)
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsMulticast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsMulticast(bool IpV6, const uint8_t* Ip)
{
    return IpV6 ? Ip[0] == 0xFF : (Ip[0] & 0xF0) == 0xE0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Copy16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An address of the family into 16 bytes, the rest of an IPv4 address zero.
//
static void Copy16(bool IpV6, const uint8_t* From, uint8_t* To)
{
    memset(To, 0, 16);
    memcpy(To, From, IpV6 ? 16 : 4);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ListInterfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int ListInterfaces(OsNetItf* Itfs, int MaxItfs, int* NumItfs)
{
    int Count = 0;

    SimDtPcie_Lock();
    for (int i = 0; i < SIM_NET_MAX_INTERFACES; i++)
    {
        if (!g_Net.Itfs[i].Used)
            continue;
        if (Count < MaxItfs)
            Itfs[Count] = g_Net.Itfs[i].Itf;
        Count++;
    }
    SimDtPcie_Unlock();
    *NumItfs = Count;
    return Count > MaxItfs ? OS_NET_TOO_SMALL : OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetAddresses -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetAddresses(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                        int* NumAddrs)
{
    int Count = 0;

    SimDtPcie_Lock();
    const SimItfEntry* Itf = FindItf(IfIndex);
    for (int i = 0; Itf != NULL && i < Itf->NumAddrs; i++)
    {
        if (Itf->Addrs[i].IpV6 != IpV6)
            continue;
        if (Count < MaxAddrs)
            Addrs[Count] = Itf->Addrs[i];
        Count++;
    }
    SimDtPcie_Unlock();
    *NumAddrs = Count;
    if (Itf == NULL)
        return OS_NET_NOT_FOUND;
    return Count > MaxAddrs ? OS_NET_TOO_SMALL : OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetGateway -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetGateway(uint32_t IfIndex, bool IpV6, uint8_t* Gateway)
{
    int Outcome = OS_NET_NOT_FOUND;

    SimDtPcie_Lock();
    const SimItfEntry* Itf = FindItf(IfIndex);
    if (Itf != NULL && Itf->HasGateway[IpV6 ? 1 : 0])
    {
        memcpy(Gateway, Itf->Gateway[IpV6 ? 1 : 0], 16);
        Outcome = OS_NET_OK;
    }
    SimDtPcie_Unlock();
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetBestRoute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetBestRoute(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                        const uint8_t* Dst, uint8_t* Gateway)
{
    (void)Src;
    SimDtPcie_Lock();
    const SimItfEntry* Itf = FindItf(IfIndex);
    if (Itf == NULL)
    {
        SimDtPcie_Unlock();
        return OS_NET_NOT_FOUND;
    }

    bool Direct =
        IpV6 ? Dst[0] == 0xFE && (Dst[1] & 0xC0) == 0x80 : Dst[0] == 169 && Dst[1] == 254;
    for (int i = 0; i < Itf->NumAddrs && !Direct; i++)
    {
        Direct = Itf->Addrs[i].IpV6 == IpV6 &&
                 PrefixMatches(Itf->Addrs[i].Ip, Dst, Itf->Addrs[i].PrefixLength);
    }
    const SimRoute* Best = NULL;
    for (int i = 0; i < Itf->NumRoutes && !Direct; i++)
    {
        const SimRoute* Route = &Itf->Routes[i];
        if (Route->IpV6 == IpV6 && PrefixMatches(Route->Dst, Dst, Route->PrefixLength) &&
            (Best == NULL || Route->PrefixLength > Best->PrefixLength))
        {
            Best = Route;
        }
    }

    int Outcome = OS_NET_OK;
    if (Direct)
        memset(Gateway, 0, 16);
    else if (Best != NULL)
        memcpy(Gateway, Best->Gateway, 16);
    else if (Itf->HasGateway[IpV6 ? 1 : 0])
        memcpy(Gateway, Itf->Gateway[IpV6 ? 1 : 0], 16);
    else
        Outcome = OS_NET_NOT_FOUND;
    SimDtPcie_Unlock();
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ResolveNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int ResolveNeighbour(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                            const uint8_t* Dst, uint8_t* Mac)
{
    int Outcome = OS_NET_NOT_FOUND;

    (void)Src;
    SimDtPcie_Lock();
    for (int i = 0; i < g_Net.NumNeighbours && Outcome != OS_NET_OK; i++)
    {
        const SimNeighbour* Neighbour = &g_Net.Neighbours[i];
        if (Neighbour->IfIndex == IfIndex && Neighbour->IpV6 == IpV6 &&
            memcmp(Neighbour->Ip, Dst, IpV6 ? 16 : 4) == 0)
        {
            memcpy(Mac, Neighbour->Mac, 6);
            Outcome = OS_NET_OK;
        }
    }
    SimDtPcie_Unlock();
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Bind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                void** Socket, uint16_t* BoundPort)
{
    uint8_t Address[16];

    Copy16(IpV6, Ip, Address);
    SimDtPcie_Lock();
    bool Found = IsEmpty(Address);
    for (int i = 0; i < SIM_NET_MAX_INTERFACES && !Found; i++)
    {
        const SimItfEntry* Itf = &g_Net.Itfs[i];
        bool LinkLocal = IpV6 && Address[0] == 0xFE && (Address[1] & 0xC0) == 0x80;
        for (int k = 0; Itf->Used && k < Itf->NumAddrs && !Found; k++)
        {
            Found = Itf->Addrs[k].IpV6 == IpV6 &&
                    memcmp(Itf->Addrs[k].Ip, Address, 16) == 0 &&
                    (!LinkLocal || Itf->Itf.Index == IfIndex);
        }
    }
    bool Allowed = Found && !g_Net.FailBind;
    SimSocket* New = NULL;
    if (Allowed)
        New = (SimSocket*)DtAlloc_Malloc(sizeof(SimSocket));
    if (New != NULL)
    {
        New->IpV6 = IpV6;
        New->Port = Port != 0 ? Port : g_Net.NextPort++;
        *BoundPort = New->Port;
        g_Net.NumOpenSockets++;
    }
    SimDtPcie_Unlock();
    *Socket = New;
    if (New == NULL)
        return Allowed ? OS_NET_NO_MEMORY : OS_NET_BIND;
    return OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindJoined -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int FindJoined(const SimSocket* Socket, const SimNetMembership* Membership)
{
    for (int i = 0; i < g_Net.NumJoined; i++)
    {
        const SimNetMembership* Joined = &g_Net.Joined[i].Membership;
        if (g_Net.Joined[i].Socket == Socket && Joined->IfIndex == Membership->IfIndex &&
            memcmp(Joined->Group, Membership->Group, 16) == 0 &&
            Joined->HasSource == Membership->HasSource &&
            memcmp(Joined->Source, Membership->Source, 16) == 0)
        {
            return i;
        }
    }
    return -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RemoveJoined -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void RemoveJoined(int Index)
{
    memmove(&g_Net.Joined[Index], &g_Net.Joined[Index + 1],
            (size_t)(g_Net.NumJoined - Index - 1) * sizeof(g_Net.Joined[0]));
    g_Net.NumJoined--;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- JoinOrLeave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int JoinOrLeave(void* State, bool Join, bool IpV6, uint32_t IfIndex,
                       const uint8_t* Group, const uint8_t* Source)
{
    SimSocket* Socket = (SimSocket*)State;
    SimNetMembership Membership;

    memset(&Membership, 0, sizeof(Membership));
    Membership.IfIndex = IfIndex;
    Membership.IpV6 = IpV6;
    Copy16(IpV6, Group, Membership.Group);
    Membership.HasSource = Source != NULL;
    if (Source != NULL)
        Copy16(IpV6, Source, Membership.Source);
    Membership.Port = Socket->Port;

    SimDtPcie_Lock();
    int Outcome = OS_NET_OK;
    int Index = FindJoined(Socket, &Membership);
    if (g_Net.FailJoin || FindItf(IfIndex) == NULL || !IsMulticast(IpV6, Group) ||
        (Join && Index >= 0) || (!Join && Index < 0) ||
        (Join && g_Net.NumJoined == SIM_NET_MAX_MEMBERSHIPS))
    {
        Outcome = OS_NET_JOIN;
    }
    else if (Join)
    {
        g_Net.Joined[g_Net.NumJoined].Socket = Socket;
        g_Net.Joined[g_Net.NumJoined].Membership = Membership;
        g_Net.NumJoined++;
    }
    else
        RemoveJoined(Index);
    SimDtPcie_Unlock();
    return Outcome;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Close(void* State)
{
    SimDtPcie_Lock();
    for (int i = g_Net.NumJoined - 1; i >= 0; i--)
    {
        if (g_Net.Joined[i].Socket == (SimSocket*)State)
            RemoveJoined(i);
    }
    g_Net.NumOpenSockets--;
    SimDtPcie_Unlock();
    DtAlloc_Free(State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSim_NetBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const OsNetBackend* OsSim_NetBackend(void)
{
    static const OsNetBackend Backend = {
        ListInterfaces,   GetAddresses, GetGateway,  GetBestRoute,
        ResolveNeighbour, Bind,         JoinOrLeave, Close};
    return &Backend;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Emulator parts +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddItf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool AddItf(uint32_t Index, const uint8_t* Mac, int VlanId, uint32_t ParentIndex,
                   const char* Name)
{
    if (Index == 0 || FindItf(Index) != NULL || Mac == NULL)
        return false;
    for (int i = 0; i < SIM_NET_MAX_INTERFACES; i++)
    {
        SimItfEntry* Itf = &g_Net.Itfs[i];
        if (Itf->Used)
            continue;
        memset(Itf, 0, sizeof(*Itf));
        Itf->Used = true;
        Itf->Itf.Index = Index;
        memcpy(Itf->Itf.Mac, Mac, 6);
        Itf->Itf.VlanId = VlanId;
        Itf->Itf.ParentIndex = ParentIndex;
        Itf->Itf.AdminUp = true;
        Itf->Itf.LinkUp = true;
        snprintf(Itf->Itf.Name, sizeof(Itf->Itf.Name), "%s", Name != NULL ? Name : "");
        return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddAddr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool AddAddr(uint32_t Index, bool IpV6, const uint8_t* Ip, int PrefixLength)
{
    SimItfEntry* Itf = FindItf(Index);
    if (Itf == NULL || Itf->NumAddrs == SIM_NET_MAX_ADDRESSES)
        return false;
    OsNetAddr* Addr = &Itf->Addrs[Itf->NumAddrs++];
    memset(Addr, 0, sizeof(*Addr));
    Addr->IpV6 = IpV6;
    Copy16(IpV6, Ip, Addr->Ip);
    Addr->PrefixLength = PrefixLength;
    Addr->State = OS_NET_ADDR_PREFERRED;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool AddNeighbour(uint32_t Index, bool IpV6, const uint8_t* Ip, const uint8_t* Mac)
{
    if (g_Net.NumNeighbours == SIM_NET_MAX_NEIGHBOURS || Ip == NULL || Mac == NULL)
        return false;
    SimNeighbour* Neighbour = &g_Net.Neighbours[g_Net.NumNeighbours++];
    Neighbour->IfIndex = Index;
    Neighbour->IpV6 = IpV6;
    Copy16(IpV6, Ip, Neighbour->Ip);
    memcpy(Neighbour->Mac, Mac, 6);
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RemoveItf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void RemoveItf(uint32_t Index)
{
    SimItfEntry* Itf = FindItf(Index);
    if (Itf != NULL)
        memset(Itf, 0, sizeof(*Itf));
    for (int i = g_Net.NumNeighbours - 1; i >= 0; i--)
    {
        if (g_Net.Neighbours[i].IfIndex != Index)
            continue;
        memmove(&g_Net.Neighbours[i], &g_Net.Neighbours[i + 1],
                (size_t)(g_Net.NumNeighbours - i - 1) * sizeof(g_Net.Neighbours[0]));
        g_Net.NumNeighbours--;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNet_Reset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sockets still open stay valid, so that closing them later is harmless; they only
// lose their groups.
//
void SimNet_Reset(void)
{
    int OpenSockets = g_Net.NumOpenSockets;

    memset(&g_Net, 0, sizeof(g_Net));
    g_Net.NumOpenSockets = OpenSockets;
    g_Net.NextPort = 49152;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimNet_SetDta2110Interface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimNet_SetDta2110Interface(bool Present, const uint8_t* Mac)
{
    static const uint8_t IpV4[4] = SIM_NET_DTA2110_IPV4;
    static const uint8_t GatewayV4[4] = SIM_NET_DTA2110_GATEWAY_IPV4;
    static const uint8_t LinkLocal[16] = SIM_NET_DTA2110_LINK_LOCAL;
    static const uint8_t Global[16] = SIM_NET_DTA2110_GLOBAL;
    static const uint8_t GatewayV6[16] = SIM_NET_DTA2110_GATEWAY_IPV6;
    static const uint8_t GatewayMac[6] = SIM_NET_GATEWAY_MAC;

    RemoveItf(SIM_NET_DTA2110_INDEX);
    if (!Present || !AddItf(SIM_NET_DTA2110_INDEX, Mac, 0, 0, SIM_NET_DTA2110_NAME))
        return;
    AddAddr(SIM_NET_DTA2110_INDEX, false, IpV4, SIM_NET_DTA2110_IPV4_PREFIX);
    AddAddr(SIM_NET_DTA2110_INDEX, true, LinkLocal, SIM_NET_DTA2110_IPV6_PREFIX);
    AddAddr(SIM_NET_DTA2110_INDEX, true, Global, SIM_NET_DTA2110_IPV6_PREFIX);

    SimItfEntry* Itf = FindItf(SIM_NET_DTA2110_INDEX);
    Itf->HasGateway[0] = true;
    Copy16(false, GatewayV4, Itf->Gateway[0]);
    Itf->HasGateway[1] = true;
    Copy16(true, GatewayV6, Itf->Gateway[1]);
    AddNeighbour(SIM_NET_DTA2110_INDEX, false, GatewayV4, GatewayMac);
    AddNeighbour(SIM_NET_DTA2110_INDEX, true, GatewayV6, GatewayMac);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_AddNetInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcie_AddNetInterface(uint32_t Index, const uint8_t* Mac, int VlanId,
                               uint32_t ParentIndex, const char* Name)
{
    SimDtPcie_Lock();
    bool Added = AddItf(Index, Mac, VlanId, ParentIndex, Name);
    SimDtPcie_Unlock();
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_RemoveNetInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimDtPcie_RemoveNetInterface(uint32_t Index)
{
    SimDtPcie_Lock();
    RemoveItf(Index);
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetNetInterfaceUp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetNetInterfaceUp(uint32_t Index, bool AdminUp, bool LinkUp)
{
    SimDtPcie_Lock();
    SimItfEntry* Itf = FindItf(Index);
    if (Itf != NULL)
    {
        Itf->Itf.AdminUp = AdminUp;
        Itf->Itf.LinkUp = AdminUp && LinkUp;
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_AddNetAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcie_AddNetAddress(uint32_t Index, const OsNetAddr* Addr)
{
    if (Addr == NULL)
        return false;
    SimDtPcie_Lock();
    bool Added = AddAddr(Index, Addr->IpV6, Addr->Ip, Addr->PrefixLength);
    SimItfEntry* Itf = FindItf(Index);
    if (Added)
        Itf->Addrs[Itf->NumAddrs - 1].State = Addr->State;
    SimDtPcie_Unlock();
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_ClearNetAddresses -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_ClearNetAddresses(uint32_t Index, bool IpV6)
{
    SimDtPcie_Lock();
    SimItfEntry* Itf = FindItf(Index);
    int Kept = 0;
    for (int i = 0; Itf != NULL && i < Itf->NumAddrs; i++)
    {
        if (Itf->Addrs[i].IpV6 != IpV6)
            Itf->Addrs[Kept++] = Itf->Addrs[i];
    }
    if (Itf != NULL)
        Itf->NumAddrs = Kept;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_SetNetGateway -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_SetNetGateway(uint32_t Index, bool IpV6, const uint8_t* Gateway)
{
    SimDtPcie_Lock();
    SimItfEntry* Itf = FindItf(Index);
    if (Itf != NULL)
    {
        Itf->HasGateway[IpV6 ? 1 : 0] = Gateway != NULL;
        if (Gateway != NULL)
            Copy16(IpV6, Gateway, Itf->Gateway[IpV6 ? 1 : 0]);
    }
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_AddNetRoute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcie_AddNetRoute(uint32_t Index, bool IpV6, const uint8_t* Dst,
                           int PrefixLength, const uint8_t* Gateway)
{
    bool Added = false;

    SimDtPcie_Lock();
    SimItfEntry* Itf = FindItf(Index);
    if (Itf != NULL && Itf->NumRoutes < SIM_NET_MAX_ROUTES && Dst != NULL &&
        Gateway != NULL)
    {
        SimRoute* Route = &Itf->Routes[Itf->NumRoutes++];
        Route->IpV6 = IpV6;
        Copy16(IpV6, Dst, Route->Dst);
        Route->PrefixLength = PrefixLength;
        Copy16(IpV6, Gateway, Route->Gateway);
        Added = true;
    }
    SimDtPcie_Unlock();
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_AddNetNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool SimDtPcie_AddNetNeighbour(uint32_t Index, bool IpV6, const uint8_t* Ip,
                               const uint8_t* Mac)
{
    SimDtPcie_Lock();
    bool Added = AddNeighbour(Index, IpV6, Ip, Mac);
    SimDtPcie_Unlock();
    return Added;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_FailNetBind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_FailNetBind(bool Fail)
{
    SimDtPcie_Lock();
    g_Net.FailBind = Fail;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_FailNetJoin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimDtPcie_FailNetJoin(bool Fail)
{
    SimDtPcie_Lock();
    g_Net.FailJoin = Fail;
    SimDtPcie_Unlock();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_NetMembershipCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int SimDtPcie_NetMembershipCount(void)
{
    SimDtPcie_Lock();
    int Count = g_Net.NumJoined;
    SimDtPcie_Unlock();
    return Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_GetNetMembership -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimDtPcie_GetNetMembership(int Index, SimNetMembership* Membership)
{
    memset(Membership, 0, sizeof(*Membership));
    SimDtPcie_Lock();
    bool Found = Index >= 0 && Index < g_Net.NumJoined;
    if (Found)
        *Membership = g_Net.Joined[Index].Membership;
    SimDtPcie_Unlock();
    return Found;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- SimDtPcie_OpenNetSocketCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int SimDtPcie_OpenNetSocketCount(void)
{
    SimDtPcie_Lock();
    int Count = g_Net.NumOpenSockets;
    SimDtPcie_Unlock();
    return Count;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - An IP port in the operating system's network: its address and neighbours
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtNet.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The addresses an interface is looked at for.
#define DT_NET_MAX_ADDRS 32

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Length -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static size_t Length(bool IpV6)
{
    return IpV6 ? 16 : 4;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Addresses +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_IsEmpty -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtNet_IsEmpty(bool IpV6, const uint8_t* Ip)
{
    for (size_t i = 0; i < Length(IpV6); i++)
    {
        if (Ip[i] != 0)
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_IsMulticast -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtNet_IsMulticast(bool IpV6, const uint8_t* Ip)
{
    return IpV6 ? Ip[0] == 0xFF : Ip[0] >= 224 && Ip[0] <= 239;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_IsSourceSpecific -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtNet_IsSourceSpecific(bool IpV6, const uint8_t* Ip)
{
    return IpV6 ? Ip[0] == 0xFF && (Ip[1] & 0xF0) == 0x30 : Ip[0] == 232;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_IsLinkLocal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtNet_IsLinkLocal(bool IpV6, const uint8_t* Ip)
{
    return IpV6 ? Ip[0] == 0xFE && (Ip[1] & 0xC0) == 0x80 : Ip[0] == 169 && Ip[1] == 254;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_IsSiteLocalV6 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtNet_IsSiteLocalV6(const uint8_t* Ip)
{
    return (Ip[0] & 0xFE) == 0xFC;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_IsGlobalV6 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtNet_IsGlobalV6(const uint8_t* Ip)
{
    return (Ip[0] & 0xE0) == 0x20;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_PrefixMask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNet_PrefixMask(bool IpV6, int PrefixLength, uint8_t* Mask)
{
    memset(Mask, 0, 16);
    for (int Bit = 0; Bit < PrefixLength && Bit < (int)Length(IpV6) * 8; Bit++)
        Mask[Bit / 8] = (uint8_t)(Mask[Bit / 8] | 0x80 >> (Bit % 8));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_SameSubnet -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtNet_SameSubnet(bool IpV6, const uint8_t* A, const uint8_t* B, const uint8_t* Mask)
{
    for (size_t i = 0; i < Length(IpV6); i++)
    {
        if ((A[i] & Mask[i]) != (B[i] & Mask[i]))
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_MulticastMac -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtNet_MulticastMac(bool IpV6, const uint8_t* Group, uint8_t* Mac)
{
    if (IpV6)
    {
        Mac[0] = 0x33;
        Mac[1] = 0x33;
        memcpy(Mac + 2, Group + 12, 4);
        return;
    }
    Mac[0] = 0x01;
    Mac[1] = 0x00;
    Mac[2] = 0x5E;
    Mac[3] = (uint8_t)(Group[1] & 0x7F);
    Mac[4] = Group[2];
    Mac[5] = Group[3];
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Own address +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsKind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether an address of the interface is of Kind. An other address is in Hint's subnet,
// or of no other kind when Hint is empty.
//
static bool IsKind(const OsNetAddr* Addr, int Kind, const uint8_t* Hint)
{
    const uint8_t* Ip = Addr->Ip;

    switch (Kind)
    {
    case DT_NET_ADDR_IPV4:
        return !Addr->IpV6;
    case DT_NET_ADDR_LINK_LOCAL:
        return DtNet_IsLinkLocal(true, Ip);
    case DT_NET_ADDR_SITE_LOCAL:
        return DtNet_IsSiteLocalV6(Ip);
    case DT_NET_ADDR_GLOBAL:
        return DtNet_IsGlobalV6(Ip);
    default:
    {
        static const uint8_t Loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                             0, 0, 0, 0, 0, 0, 0, 1};
        uint8_t Mask[16];

        if (Hint != NULL && !DtNet_IsEmpty(true, Hint))
        {
            DtNet_PrefixMask(true, Addr->PrefixLength, Mask);
            return DtNet_SameSubnet(true, Ip, Hint, Mask);
        }
        return !DtNet_IsGlobalV6(Ip) && !DtNet_IsLinkLocal(true, Ip) &&
               !DtNet_IsSiteLocalV6(Ip) && !DtNet_IsMulticast(true, Ip) &&
               memcmp(Ip, Loopback, 16) != 0;
    }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_GetOwnAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtNet_GetOwnAddress(const uint8_t* Mac, int VlanId, int Kind,
                                const uint8_t* Hint, DtNetOwn* Own)
{
    if (Own != NULL)
        memset(Own, 0, sizeof(*Own));
    if (Mac == NULL || Own == NULL || Kind < DT_NET_ADDR_IPV4 || Kind > DT_NET_ADDR_OTHER)
        return DTAPI_E_INVALID_ARG;

    int Outcome = OsNet_FindInterface(Mac, VlanId, &Own->Itf);
    if (Outcome == OS_NET_NOT_FOUND && VlanId != 0 &&
        OsNet_FindInterface(Mac, 0, &Own->Itf) == OS_NET_OK)
    {
        memset(&Own->Itf, 0, sizeof(Own->Itf));
        return DTAPI_E_VLAN_NOT_FOUND;
    }
    if (Outcome == OS_NET_NO_MEMORY)
        return DTAPI_E_OUT_OF_MEM;
    if (Outcome != OS_NET_OK)
        return DTAPI_E_NW_DRIVER;

    bool IpV6 = Kind != DT_NET_ADDR_IPV4;
    OsNetAddr Addrs[DT_NET_MAX_ADDRS];
    int Count = 0;
    Outcome = OsNet_GetAddresses(Own->Itf.Index, IpV6, Addrs, DT_NET_MAX_ADDRS, &Count);
    if (Outcome == OS_NET_TOO_SMALL)
        Count = DT_NET_MAX_ADDRS;
    else if (Outcome != OS_NET_OK)
        Count = 0;

    const OsNetAddr* Found = NULL;
    for (int State = OS_NET_ADDR_PREFERRED;
         State <= OS_NET_ADDR_DEPRECATED && Found == NULL; State++)
    {
        for (int i = 0; i < Count && Found == NULL; i++)
        {
            if (Addrs[i].State == State && IsKind(&Addrs[i], Kind, Hint))
                Found = &Addrs[i];
        }
    }
    if (Found == NULL)
        return VlanId != 0 ? DTAPI_E_VLAN_NOT_FOUND : DTAPI_E_NO_ADAPTER_IP_ADDR;

    Own->IpV6 = IpV6;
    memcpy(Own->Ip, Found->Ip, 16);
    DtNet_PrefixMask(IpV6, Found->PrefixLength, Own->Mask);
    OsNet_GetGateway(Own->Itf.Index, IpV6, Own->Gateway);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TryKinds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Tries the kinds in order and gives the first own address found, or the last failure.
//
static DtapiResult TryKinds(const uint8_t* Mac, int VlanId, const int* Kinds,
                            int NumKinds, const uint8_t* Hint, DtNetOwn* Own)
{
    DtapiResult Result = DTAPI_E_NO_ADAPTER_IP_ADDR;

    for (int i = 0; i < NumKinds; i++)
    {
        Result = DtNet_GetOwnAddress(Mac, VlanId, Kinds[i], Hint, Own);
        if (Result == DTAPI_OK)
            break;
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_ChooseInputAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNet_ChooseInputAddress(const uint8_t* Mac, int VlanId, bool IpV6,
                                     const uint8_t* Stream, DtNetOwn* Own)
{
    if (Own != NULL)
        memset(Own, 0, sizeof(*Own));
    if (Stream == NULL || Own == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!IpV6)
        return DtNet_GetOwnAddress(Mac, VlanId, DT_NET_ADDR_IPV4, NULL, Own);

    if (DtNet_IsMulticast(true, Stream) || DtNet_IsEmpty(true, Stream))
    {
        static const int Kinds[] = {DT_NET_ADDR_LINK_LOCAL, DT_NET_ADDR_SITE_LOCAL,
                                    DT_NET_ADDR_GLOBAL, DT_NET_ADDR_OTHER};
        return TryKinds(Mac, VlanId, Kinds, 4, NULL, Own);
    }

    int Kind = DtNet_IsGlobalV6(Stream)          ? DT_NET_ADDR_GLOBAL
               : DtNet_IsLinkLocal(true, Stream) ? DT_NET_ADDR_LINK_LOCAL
               : DtNet_IsSiteLocalV6(Stream)     ? DT_NET_ADDR_SITE_LOCAL
                                                 : DT_NET_ADDR_OTHER;
    DtapiResult Result = DtNet_GetOwnAddress(Mac, VlanId, Kind, Stream, Own);
    if (Result == DTAPI_OK)
        memcpy(Own->Ip, Stream, 16);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_ChooseOutputAddress -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A multicast group's scope picks the kind: interface- and link-local scopes link-local,
// the realm-, admin-, site- and organisation-local scopes and the global scope
// site-local, and any other scope global.
//
DtapiResult DtNet_ChooseOutputAddress(const uint8_t* Mac, int VlanId, bool IpV6,
                                      const uint8_t* Dst, DtNetOwn* Own)
{
    static const int LinkLocal[] = {DT_NET_ADDR_LINK_LOCAL, DT_NET_ADDR_SITE_LOCAL,
                                    DT_NET_ADDR_GLOBAL, DT_NET_ADDR_OTHER};
    static const int SiteLocal[] = {DT_NET_ADDR_SITE_LOCAL, DT_NET_ADDR_LINK_LOCAL,
                                    DT_NET_ADDR_GLOBAL, DT_NET_ADDR_OTHER};
    static const int Global[] = {DT_NET_ADDR_GLOBAL, DT_NET_ADDR_SITE_LOCAL,
                                 DT_NET_ADDR_LINK_LOCAL, DT_NET_ADDR_OTHER};
    static const int Other[] = {DT_NET_ADDR_OTHER, DT_NET_ADDR_GLOBAL,
                                DT_NET_ADDR_SITE_LOCAL, DT_NET_ADDR_LINK_LOCAL};

    if (Own != NULL)
        memset(Own, 0, sizeof(*Own));
    if (Dst == NULL || Own == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!IpV6)
        return DtNet_GetOwnAddress(Mac, VlanId, DT_NET_ADDR_IPV4, NULL, Own);

    if (DtNet_IsMulticast(true, Dst))
    {
        switch (Dst[1] & 0xF)
        {
        case 0x1:
        case 0x2:
            return TryKinds(Mac, VlanId, LinkLocal, 4, NULL, Own);
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x8:
        case 0xE:
            return TryKinds(Mac, VlanId, SiteLocal, 4, NULL, Own);
        default:
            return TryKinds(Mac, VlanId, Global, 4, NULL, Own);
        }
    }
    if (DtNet_IsLinkLocal(true, Dst))
        return TryKinds(Mac, VlanId, LinkLocal, 4, NULL, Own);
    if (DtNet_IsSiteLocalV6(Dst))
        return TryKinds(Mac, VlanId, SiteLocal, 4, NULL, Own);
    if (DtNet_IsGlobalV6(Dst))
        return TryKinds(Mac, VlanId, Global, 4, NULL, Own);
    return TryKinds(Mac, VlanId, Other, 4, Dst, Own);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Neighbours +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_ResolveDstMac -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An IPv4 broadcast is the subnet's broadcast address, when Dst is in the subnet, or
// 169.254.255.255. A best route without a gateway reaches Dst on the link, which happens
// outside Own's subnet when the interface has another address whose subnet holds Dst, or
// when Own's prefix is longer than the link's, as Windows reports for temporary IPv6
// addresses.
//
DtapiResult DtNet_ResolveDstMac(const DtNetOwn* Own, const uint8_t* Dst,
                                const uint8_t* Gateway, uint8_t* Mac)
{
    if (Mac != NULL)
        memset(Mac, 0, 6);
    if (Own == NULL || Dst == NULL || Mac == NULL)
        return DTAPI_E_INVALID_ARG;

    bool IpV6 = Own->IpV6;
    if (DtNet_IsMulticast(IpV6, Dst))
    {
        DtNet_MulticastMac(IpV6, Dst, Mac);
        return DTAPI_OK;
    }

    bool SameSubnet =
        DtNet_SameSubnet(IpV6, Own->Ip, Dst, Own->Mask) || DtNet_IsLinkLocal(IpV6, Dst);
    if (!IpV6)
    {
        bool NetBroadcast = true;
        for (int i = 0; i < 4; i++)
            NetBroadcast = NetBroadcast && (Dst[i] | Own->Mask[i]) == 0xFF;
        bool LocalBroadcast =
            Dst[0] == 169 && Dst[1] == 254 && Dst[2] == 255 && Dst[3] == 255;
        if ((SameSubnet && NetBroadcast) || LocalBroadcast)
        {
            memset(Mac, 0xFF, 6);
            return DTAPI_OK;
        }
    }

    bool Forced = Gateway != NULL && !DtNet_IsEmpty(IpV6, Gateway);
    if (!SameSubnet && !Forced)
    {
        uint8_t RouteGateway[16];
        if (OsNet_GetBestRoute(Own->Itf.Index, IpV6, Own->Ip, Dst, RouteGateway) ==
            OS_NET_OK)
        {
            const uint8_t* Hop = DtNet_IsEmpty(IpV6, RouteGateway) ? Dst : RouteGateway;
            if (OsNet_ResolveNeighbour(Own->Itf.Index, IpV6, Own->Ip, Hop, Mac) ==
                OS_NET_OK)
            {
                return DTAPI_OK;
            }
        }
    }

    const uint8_t* Next = SameSubnet ? Dst : Forced ? Gateway : Own->Gateway;
    if (DtNet_IsEmpty(IpV6, Next) ||
        OsNet_ResolveNeighbour(Own->Itf.Index, IpV6, Own->Ip, Next, Mac) != OS_NET_OK)
    {
        memset(Mac, 0, 6);
        return DTAPI_E_DST_MAC_ADDR;
    }
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Operation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFamily -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult CheckFamily(const uint8_t* Mac, int VlanId, bool IpV6)
{
    static const int IpV4Kinds[] = {DT_NET_ADDR_IPV4};
    static const int IpV6Kinds[] = {DT_NET_ADDR_LINK_LOCAL, DT_NET_ADDR_SITE_LOCAL,
                                    DT_NET_ADDR_GLOBAL, DT_NET_ADDR_OTHER};
    DtNetOwn Own;

    DtapiResult Result = IpV6 ? TryKinds(Mac, VlanId, IpV6Kinds, 4, NULL, &Own)
                              : TryKinds(Mac, VlanId, IpV4Kinds, 1, NULL, &Own);
    if (Result != DTAPI_OK)
        return Result;
    if (DtNet_IsEmpty(IpV6, Own.Ip))
        return DTAPI_E_NO_ADAPTER_IP_ADDR;

    OsNetSocket* Socket = NULL;
    if (OsNetSocket_Bind(IpV6, Own.Ip, 0, Own.Itf.Index, &Socket) != OS_NET_OK)
        return DTAPI_E_BIND;
    OsNetSocket_Close(Socket);
    return Own.Itf.AdminUp ? DTAPI_OK : DTAPI_E_DISABLED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_CheckOperational -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNet_CheckOperational(const uint8_t* Mac, int VlanId, bool IpV4, bool IpV6)
{
    if (Mac == NULL)
        return DTAPI_E_INVALID_ARG;
    DtapiResult Result = DTAPI_OK;
    if (IpV4)
        Result = CheckFamily(Mac, VlanId, false);
    if (Result == DTAPI_OK && IpV6)
        Result = CheckFamily(Mac, VlanId, true);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Groups +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsRepeated -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether source Index is the same as one before it.
//
static bool IsRepeated(bool IpV6, const uint8_t* Sources, int Index)
{
    for (int k = 0; k < Index; k++)
    {
        if (memcmp(Sources + 16 * k, Sources + 16 * Index, Length(IpV6)) == 0)
            return true;
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Membership -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Joins or leaves every distinct source; stops at a failed join and returns false.
//
static bool Membership(bool Join, OsNetSocket* Socket, uint32_t IfIndex, bool IpV6,
                       const uint8_t* Group, const uint8_t* Sources, int NumSources)
{
    bool Succeeded = true;

    for (int i = 0; i == 0 || i < NumSources; i++)
    {
        const uint8_t* Source = NULL;
        if (NumSources > 0)
        {
            if (IsRepeated(IpV6, Sources, i))
                continue;
            if (!DtNet_IsEmpty(IpV6, Sources + 16 * i))
                Source = Sources + 16 * i;
        }
        int Outcome = Join ? OsNetSocket_Join(Socket, IfIndex, Group, Source)
                           : OsNetSocket_Leave(Socket, IfIndex, Group, Source);
        if (Outcome != OS_NET_OK)
        {
            Succeeded = false;
            if (Join)
                break;
        }
    }
    return Succeeded;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_Join -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNet_Join(OsNetSocket* Socket, uint32_t IfIndex, bool IpV6,
                       const uint8_t* Group, const uint8_t* Sources, int NumSources)
{
    if (Socket == NULL || Group == NULL || NumSources < 0 ||
        (NumSources > 0 && Sources == NULL))
    {
        return DTAPI_E_INVALID_ARG;
    }
    if (!Membership(true, Socket, IfIndex, IpV6, Group, Sources, NumSources))
        return DTAPI_E_MULTICASTJOIN;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNet_Leave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtNet_Leave(OsNetSocket* Socket, uint32_t IfIndex, bool IpV6, const uint8_t* Group,
                 const uint8_t* Sources, int NumSources)
{
    if (Socket == NULL || Group == NULL || NumSources < 0 ||
        (NumSources > 0 && Sources == NULL))
    {
        return;
    }
    Membership(false, Socket, IfIndex, IpV6, Group, Sources, NumSources);
}

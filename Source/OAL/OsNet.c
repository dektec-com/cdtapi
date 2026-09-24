// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# OsNet.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The operating system's network: chooses the host's or the emulated one
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "OsBackend.h"    // Whether the emulator is asked for.
#include "OsNet.h"        // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct OsNetSocket
{
    const OsNetBackend* Backend;
    void* State;
    bool IpV6;
    uint16_t Port;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Backend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static const OsNetBackend* Backend(void)
{
    return OsSim_IsRequested() ? OsSim_NetBackend() : OsPlatform_NetBackend();
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interfaces +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNet_ListInterfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNet_ListInterfaces(OsNetItf* Itfs, int MaxItfs, int* NumItfs)
{
    const OsNetBackend* Net = Backend();

    if (NumItfs != NULL)
        *NumItfs = 0;
    if (NumItfs == NULL || MaxItfs < 0 || (Itfs == NULL && MaxItfs > 0))
        return OS_NET_ERROR;
    if (Net == NULL)
        return OS_NET_ERROR;
    return Net->ListInterfaces(Itfs, MaxItfs, NumItfs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNet_FindInterface -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Lists the interfaces, growing the list until it holds them all, and looks for the one
// with the MAC address: the interface that is not a VLAN, then the VLAN on it.
//
int OsNet_FindInterface(const uint8_t* Mac, int VlanId, OsNetItf* Itf)
{
    if (Itf != NULL)
        memset(Itf, 0, sizeof(*Itf));
    if (Mac == NULL || Itf == NULL || VlanId < 0)
        return OS_NET_ERROR;

    int Max = 16;
    int Count = 0;
    OsNetItf* Itfs = NULL;
    int Outcome = OS_NET_TOO_SMALL;
    while (Outcome == OS_NET_TOO_SMALL)
    {
        Max *= 2;
        DtAlloc_Free(Itfs);
        Itfs = (OsNetItf*)DtAlloc_Malloc((size_t)Max * sizeof(OsNetItf));
        if (Itfs == NULL)
            return OS_NET_NO_MEMORY;
        Outcome = OsNet_ListInterfaces(Itfs, Max, &Count);
    }
    if (Outcome != OS_NET_OK)
    {
        DtAlloc_Free(Itfs);
        return Outcome;
    }

    const OsNetItf* Main = NULL;
    for (int i = 0; i < Count && Main == NULL; i++)
    {
        if (Itfs[i].VlanId == 0 && memcmp(Itfs[i].Mac, Mac, 6) == 0)
            Main = &Itfs[i];
    }
    const OsNetItf* Found = VlanId == 0 ? Main : NULL;
    for (int i = 0; i < Count && Main != NULL && Found == NULL; i++)
    {
        if (Itfs[i].VlanId == VlanId && Itfs[i].ParentIndex == Main->Index)
            Found = &Itfs[i];
    }
    if (Found != NULL)
        *Itf = *Found;
    DtAlloc_Free(Itfs);
    return Found != NULL ? OS_NET_OK : OS_NET_NOT_FOUND;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNet_GetAddresses -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNet_GetAddresses(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                       int* NumAddrs)
{
    const OsNetBackend* Net = Backend();

    if (NumAddrs != NULL)
        *NumAddrs = 0;
    if (NumAddrs == NULL || MaxAddrs < 0 || (Addrs == NULL && MaxAddrs > 0) ||
        Net == NULL)
        return OS_NET_ERROR;
    return Net->GetAddresses(IfIndex, IpV6, Addrs, MaxAddrs, NumAddrs);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Routes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNet_GetGateway -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNet_GetGateway(uint32_t IfIndex, bool IpV6, uint8_t* Gateway)
{
    const OsNetBackend* Net = Backend();

    if (Gateway != NULL)
        memset(Gateway, 0, 16);
    if (Gateway == NULL || Net == NULL)
        return OS_NET_ERROR;
    return Net->GetGateway(IfIndex, IpV6, Gateway);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNet_GetBestRoute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNet_GetBestRoute(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                       const uint8_t* Dst, uint8_t* Gateway)
{
    const OsNetBackend* Net = Backend();

    if (Gateway != NULL)
        memset(Gateway, 0, 16);
    if (Src == NULL || Dst == NULL || Gateway == NULL || Net == NULL)
        return OS_NET_ERROR;
    return Net->GetBestRoute(IfIndex, IpV6, Src, Dst, Gateway);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNet_ResolveNeighbour -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNet_ResolveNeighbour(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                           const uint8_t* Dst, uint8_t* Mac)
{
    const OsNetBackend* Net = Backend();

    if (Mac != NULL)
        memset(Mac, 0, 6);
    if (Src == NULL || Dst == NULL || Mac == NULL || Net == NULL)
        return OS_NET_ERROR;
    return Net->ResolveNeighbour(IfIndex, IpV6, Src, Dst, Mac);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sockets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNetSocket_Bind -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNetSocket_Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                     OsNetSocket** Socket)
{
    const OsNetBackend* Net = Backend();

    if (Socket != NULL)
        *Socket = NULL;
    if (Ip == NULL || Socket == NULL || Net == NULL)
        return OS_NET_ERROR;

    OsNetSocket* New = (OsNetSocket*)DtAlloc_Malloc(sizeof(OsNetSocket));
    if (New == NULL)
        return OS_NET_NO_MEMORY;
    New->Backend = Net;
    New->IpV6 = IpV6;
    New->Port = 0;
    int Outcome = Net->Bind(IpV6, Ip, Port, IfIndex, &New->State, &New->Port);
    if (Outcome != OS_NET_OK)
    {
        DtAlloc_Free(New);
        return Outcome;
    }
    *Socket = New;
    return OS_NET_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNetSocket_Port -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint16_t OsNetSocket_Port(const OsNetSocket* Socket)
{
    return Socket != NULL ? Socket->Port : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNetSocket_Join -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsNetSocket_Join(OsNetSocket* Socket, uint32_t IfIndex, const uint8_t* Group,
                     const uint8_t* Source)
{
    if (Socket == NULL || Group == NULL)
        return OS_NET_ERROR;
    return Socket->Backend->Membership(Socket->State, true, Socket->IpV6, IfIndex, Group,
                                       Source);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNetSocket_Leave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int OsNetSocket_Leave(OsNetSocket* Socket, uint32_t IfIndex, const uint8_t* Group,
                      const uint8_t* Source)
{
    if (Socket == NULL || Group == NULL)
        return OS_NET_ERROR;
    return Socket->Backend->Membership(Socket->State, false, Socket->IpV6, IfIndex, Group,
                                       Source);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsNetSocket_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsNetSocket_Close(OsNetSocket* Socket)
{
    if (Socket == NULL)
        return;
    Socket->Backend->Close(Socket->State);
    DtAlloc_Free(Socket);
}

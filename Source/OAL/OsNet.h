// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# OsNet.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The operating system's network: interfaces, addresses, routes, neighbours
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Network +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What the operating system knows of its network interfaces, and UDP sockets that can
// join multicast groups. The functions enumerate and act; they choose nothing.
//
// An address is 16 bytes in network byte order; an IPv4 address fills the first 4 and
// leaves the rest zero. A MAC address is 6 bytes.
//
// When CDTAPI_SIM asks for the emulator, the emulated network answers, whose
// interfaces and neighbours the tests set; otherwise the host's.
//

// Outcomes.
#define OS_NET_OK 0
#define OS_NET_NOT_FOUND -1 // No such interface, address, route or neighbour
#define OS_NET_TOO_SMALL -2 // The output has room for fewer than there are
#define OS_NET_BIND -3      // The socket cannot be bound to the address and port
#define OS_NET_JOIN -4      // The group cannot be joined or left
#define OS_NET_NO_MEMORY -5
#define OS_NET_ERROR -6 // Any other failure of the operating system

// The bytes of an interface name, terminator included.
#define OS_NET_NAME_SIZE 64

typedef struct OsNetItf
{
    uint32_t Index; // The operating system's interface index
    uint8_t Mac[6];
    int VlanId;           // 0 for an interface that is not a VLAN
    uint32_t ParentIndex; // The interface a VLAN interface is on; 0 for none
    bool AdminUp;         // Enabled
    bool LinkUp;          // Enabled and connected
    char Name[OS_NET_NAME_SIZE];
} OsNetItf;

// Address states. An address whose duplicate address detection failed is left out.
#define OS_NET_ADDR_PREFERRED 0
#define OS_NET_ADDR_DEPRECATED 1 // Still valid, not for new connections
#define OS_NET_ADDR_TENTATIVE 2  // Duplicate address detection has not finished

typedef struct OsNetAddr
{
    bool IpV6;
    uint8_t Ip[16];
    int PrefixLength; // Bits of the subnet
    int State;        // An OS_NET_ADDR_ value
} OsNetAddr;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Interfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Lists the Ethernet interfaces into Itfs, which holds MaxItfs, and stores in *NumItfs
// how many there are. Returns OS_NET_TOO_SMALL, having filled all MaxItfs, when there
// are more.
int OsNet_ListInterfaces(OsNetItf* Itfs, int MaxItfs, int* NumItfs);

// Finds the interface with MAC address Mac that is not a VLAN when VlanId is 0, or the
// VLAN interface with that ID on it otherwise.
int OsNet_FindInterface(const uint8_t* Mac, int VlanId, OsNetItf* Itf);

// Lists the IPv4 or IPv6 addresses of the interface with index IfIndex, in the order the
// operating system gives them, filling Addrs and *NumAddrs as OsNet_ListInterfaces fills
// its output. OS_NET_NOT_FOUND when there is no such interface.
int OsNet_GetAddresses(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                       int* NumAddrs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Routes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Reads the gateway of the default route through the interface; OS_NET_NOT_FOUND when
// there is none.
int OsNet_GetGateway(uint32_t IfIndex, bool IpV6, uint8_t* Gateway);

// Reads the gateway the operating system would send a packet from Src to Dst through,
// on the interface; all zero when Dst is reached directly. OS_NET_NOT_FOUND when there is
// no route.
int OsNet_GetBestRoute(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                       const uint8_t* Dst, uint8_t* Gateway);

// Reads the MAC address of the neighbour Dst on the interface, asking the network for
// it when the operating system does not know it yet, from the interface's address Src.
// That can take seconds: on Linux about 2 for IPv4 and 2.5 for IPv6, on Windows as long
// as the operating system's own resolution takes. OS_NET_NOT_FOUND when no answer comes.
int OsNet_ResolveNeighbour(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                           const uint8_t* Dst, uint8_t* Mac);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sockets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct OsNetSocket OsNetSocket;

// Opens a UDP socket bound to Ip and Port, 0 for a free port, with the address reusable.
// IfIndex is the scope of a link-local IPv6 address, fe80::/10, and is ignored
// otherwise. *Socket is NULL after a failure.
int OsNetSocket_Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                     OsNetSocket** Socket);

// The port the socket is bound to.
uint16_t OsNetSocket_Port(const OsNetSocket* Socket);

// Joins or leaves the multicast group Group on the interface, from any source when
// Source is NULL, or from Source only.
int OsNetSocket_Join(OsNetSocket* Socket, uint32_t IfIndex, const uint8_t* Group,
                     const uint8_t* Source);
int OsNetSocket_Leave(OsNetSocket* Socket, uint32_t IfIndex, const uint8_t* Group,
                      const uint8_t* Source);

// Closes the socket, which leaves the groups it joined. Passing NULL does nothing.
void OsNetSocket_Close(OsNetSocket* Socket);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backends +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Internal to the abstraction layer: the host's network and the emulated one implement
// the same functions, and OsNet.c chooses. A socket of either starts with the backend
// that made it.
//

typedef struct OsNetBackend
{
    int (*ListInterfaces)(OsNetItf* Itfs, int MaxItfs, int* NumItfs);
    int (*GetAddresses)(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                        int* NumAddrs);
    int (*GetGateway)(uint32_t IfIndex, bool IpV6, uint8_t* Gateway);
    int (*GetBestRoute)(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                        const uint8_t* Dst, uint8_t* Gateway);
    int (*ResolveNeighbour)(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                            const uint8_t* Dst, uint8_t* Mac);
    int (*Bind)(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                void** Socket, uint16_t* BoundPort);
    int (*Membership)(void* Socket, bool Join, bool IpV6, uint32_t IfIndex,
                      const uint8_t* Group, const uint8_t* Source);
    void (*Close)(void* Socket);
} OsNetBackend;

// The host's network.
const OsNetBackend* OsPlatform_NetBackend(void);

// The emulated network.
const OsNetBackend* OsSim_NetBackend(void);

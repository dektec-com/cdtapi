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
// Asks the operating system about its network interfaces, their addresses, routes and
// neighbours, and opens UDP sockets that can join multicast groups. These functions only
// list and act; choosing an address is left to DtNet.h.
//
// An address is 16 bytes in network byte order; an IPv4 address fills the first 4 and
// leaves the rest zero. A MAC address is 6 bytes.
//
// When CDTAPI_SIM asks for the emulated device, an emulated network answers instead of
// the host's. The tests set up its interfaces and neighbours.
//
// Every function returns one of the OS_NET_ outcomes below, unless it says otherwise.
//

// The outcomes.
#define OS_NET_OK 0
#define OS_NET_NOT_FOUND -1 // No such interface, address, route or neighbour
#define OS_NET_TOO_SMALL -2 // The output has room for fewer items than there are
#define OS_NET_BIND -3      // The socket cannot be bound to the address and port
#define OS_NET_JOIN -4      // The group cannot be joined or left
#define OS_NET_NO_MEMORY -5 // Not enough memory
#define OS_NET_ERROR -6     // An invalid argument, or any other failure

// The size of an interface name in bytes, with the terminating zero.
#define OS_NET_NAME_SIZE 64

typedef struct OsNetItf
{
    uint32_t Index;              // The operating system's index of the interface
    uint8_t Mac[6];              // The MAC address
    int VlanId;                  // The VLAN ID; 0 for an interface that is not a VLAN
    uint32_t ParentIndex;        // The interface a VLAN interface is on; 0 for none
    bool AdminUp;                // Whether the interface is enabled
    bool LinkUp;                 // Whether it is enabled and connected
    char Name[OS_NET_NAME_SIZE]; // The operating system's name of the interface
} OsNetItf;

// The states of an address. An address that failed duplicate address detection is not
// listed.
#define OS_NET_ADDR_PREFERRED 0
#define OS_NET_ADDR_DEPRECATED 1 // Still valid, but not for new connections
#define OS_NET_ADDR_TENTATIVE 2  // Duplicate address detection has not finished

typedef struct OsNetAddr
{
    bool IpV6;        // Whether Ip is an IPv6 address
    uint8_t Ip[16];   // The address
    int PrefixLength; // The length of the subnet prefix, in bits
    int State;        // An OS_NET_ADDR_ value
} OsNetAddr;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Interfaces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Lists the Ethernet interfaces into Itfs, which has room for MaxItfs, and sets
// *NumItfs to the number listed. When there are more, fills all MaxItfs and returns
// OS_NET_TOO_SMALL.
int OsNet_ListInterfaces(OsNetItf* Itfs, int MaxItfs, int* NumItfs);

// Finds the interface with MAC address Mac. For a VlanId of 0, the interface that is not
// a VLAN; otherwise the VLAN interface with that ID on it.
int OsNet_FindInterface(const uint8_t* Mac, int VlanId, OsNetItf* Itf);

// Lists the IPv4 or IPv6 addresses of the interface with index IfIndex, in the order the
// operating system gives them. Fills Addrs and *NumAddrs as OsNet_ListInterfaces fills
// its output. Returns OS_NET_NOT_FOUND when there is no such interface.
int OsNet_GetAddresses(uint32_t IfIndex, bool IpV6, OsNetAddr* Addrs, int MaxAddrs,
                       int* NumAddrs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Routes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Writes the gateway of the interface's default route into Gateway. Returns
// OS_NET_NOT_FOUND when the interface has no default route.
int OsNet_GetGateway(uint32_t IfIndex, bool IpV6, uint8_t* Gateway);

// Writes the gateway that the operating system would send a packet from Src to Dst
// through, on the interface, into Gateway. Gateway is all zero when Dst is reached
// directly. Returns OS_NET_NOT_FOUND when there is no route.
int OsNet_GetBestRoute(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                       const uint8_t* Dst, uint8_t* Gateway);

// Writes the MAC address of the neighbour Dst on the interface into Mac. When the
// operating system does not know it yet, asks the network for it from the interface's
// address Src. That can take seconds: on Linux about 2 for IPv4 and 2.5 for IPv6, on
// Windows as long as the operating system takes. Returns OS_NET_NOT_FOUND when no answer
// comes.
int OsNet_ResolveNeighbour(uint32_t IfIndex, bool IpV6, const uint8_t* Src,
                           const uint8_t* Dst, uint8_t* Mac);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sockets -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct OsNetSocket OsNetSocket;

// Opens a UDP socket bound to address Ip and Port, with the address reusable. Port 0
// picks a free port. IfIndex is the interface of a link-local IPv6 address, fe80::/10,
// and is ignored for other addresses. *Socket is NULL after a failure.
int OsNetSocket_Bind(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                     OsNetSocket** Socket);

// Returns the port the socket is bound to, or 0 for a NULL Socket.
uint16_t OsNetSocket_Port(const OsNetSocket* Socket);

// Join or leave the multicast group Group on the interface with index IfIndex. With a
// NULL Source the socket receives from any source; otherwise from Source only.
int OsNetSocket_Join(OsNetSocket* Socket, uint32_t IfIndex, const uint8_t* Group,
                     const uint8_t* Source);
int OsNetSocket_Leave(OsNetSocket* Socket, uint32_t IfIndex, const uint8_t* Group,
                      const uint8_t* Source);

// Closes the socket, which also leaves the groups it joined. A NULL Socket does nothing.
void OsNetSocket_Close(OsNetSocket* Socket);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backends +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The host's network and the emulated one each implement the functions above as a
// backend, and OsNet.c chooses one. A socket remembers the backend that made it. Only
// the abstraction layer sees the backends.
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

    // Opens a socket as OsNetSocket_Bind does. Sets *Socket to the backend's state of
    // the socket, and *BoundPort to the port it is bound to.
    int (*Bind)(bool IpV6, const uint8_t* Ip, uint16_t Port, uint32_t IfIndex,
                void** Socket, uint16_t* BoundPort);

    // Joins the group when Join is true, and leaves it otherwise.
    int (*JoinOrLeave)(void* Socket, bool Join, bool IpV6, uint32_t IfIndex,
                       const uint8_t* Group, const uint8_t* Source);
    void (*Close)(void* Socket);
} OsNetBackend;

// Returns the backend of the host's network.
const OsNetBackend* OsPlatform_NetBackend(void);

// Returns the backend of the emulated network.
const OsNetBackend* OsSim_NetBackend(void);

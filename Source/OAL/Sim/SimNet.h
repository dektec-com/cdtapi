// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SimNet.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated network of the operating system, and its test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "OAL/OsNet.h" // Interfaces and addresses.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Network +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The network that OsNet.h shows when the emulator is used. Tests fill its tables: the
// interfaces, with their addresses, gateways and routes, and the neighbours. Its sockets
// remember the groups they join. It answers at once, as an operating system that knows
// every neighbour would:
//
//   interfaces  are listed in the order of the table: the order they were added in,
//               until one is removed. A new interface then takes the first free place.
//   routes      A destination is reached directly when it lies in the subnet of one of
//               the interface's addresses of its family, or is link-local (IPv4 or IPv6).
//               Otherwise, the route with the longest matching prefix gives the gateway,
//               and then the default gateway. Without either, there is no route.
//   neighbours  are known, or not found.
//   binding     is to the any address, or to an address of an interface. An IPv6
//               link-local address needs that interface's index. Port 0 gets the next
//               port from 49152 up. A port may be bound twice.
//   groups      Joining needs a multicast group of the socket's family and an existing
//               interface. Joining a group twice, or leaving one that was not joined,
//               with the same source, fails, as on Linux. Closing a socket leaves all its
//               groups.
//
// The DTA-2110 comes with an interface, as a card whose network driver is installed (see
// the SIM_NET_DTA2110_ values below). SimNet_Reset() removes it with every other
// interface. The emulator's reset adds it again when CDTAPI_SIM_DTA2110 places a
// DTA-2110.
//

// The DTA-2110's interface: its index and name, its addresses and gateways, and the MAC
// address of the neighbours that are its gateways.
#define SIM_NET_DTA2110_INDEX 5
#define SIM_NET_DTA2110_NAME "dta2110"
#define SIM_NET_DTA2110_IPV4 {192, 168, 1, 10}
#define SIM_NET_DTA2110_IPV4_PREFIX 24
#define SIM_NET_DTA2110_GATEWAY_IPV4 {192, 168, 1, 1}
#define SIM_NET_DTA2110_LINK_LOCAL                                                       \
    {0xFE, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x14, 0xF4, 0xFF, 0xFE, 0x08, 0x00, 0x01}
#define SIM_NET_DTA2110_GLOBAL                                                           \
    {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10}
#define SIM_NET_DTA2110_IPV6_PREFIX 64
#define SIM_NET_DTA2110_GATEWAY_IPV6                                                     \
    {0xFE, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01}
#define SIM_NET_GATEWAY_MAC {0x00, 0x00, 0x5E, 0x00, 0x01, 0x01}

// The sizes of the tables.
#define SIM_NET_MAX_INTERFACES 16
#define SIM_NET_MAX_ADDRESSES 8
#define SIM_NET_MAX_ROUTES 8
#define SIM_NET_MAX_NEIGHBOURS 32
#define SIM_NET_MAX_MEMBERSHIPS 64

// Restores the power-on state of every interface, neighbour, group and control. Does
// not take the emulator's lock. Open sockets stay open, and are still counted.
void SimNet_Reset(void);

// Adds the DTA-2110's interface, with MAC address Mac, when Present is true, and removes
// it otherwise. Does not take the emulator's lock.
void SimNet_SetDta2110Interface(bool Present, const uint8_t* Mac);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Adds an interface with index Index. It starts enabled and connected, without
// addresses. A VLAN interface has a VlanId, and the ParentIndex of the interface it is
// on. Returns false when Index is 0 or in use, Mac is NULL, or the table is full.
bool SimDtPcie_AddNetInterface(uint32_t Index, const uint8_t* Mac, int VlanId,
                               uint32_t ParentIndex, const char* Name);

// Removes an interface, with its routes and neighbours.
void SimDtPcie_RemoveNetInterface(uint32_t Index);

// Enables or disables an interface (AdminUp), and connects or disconnects it (LinkUp).
// LinkUp counts only while the interface is enabled.
void SimDtPcie_SetNetInterfaceUp(uint32_t Index, bool AdminUp, bool LinkUp);

// Adds an address to an interface. Returns false when Addr is NULL, there is no such
// interface, or there is no room.
bool SimDtPcie_AddNetAddress(uint32_t Index, const OsNetAddr* Addr);

// Removes the IPv6 addresses of an interface when IpV6 is true, and its IPv4 addresses
// otherwise.
void SimDtPcie_ClearNetAddresses(uint32_t Index, bool IpV6);

// Sets the default gateway of an interface for one family. NULL removes it.
void SimDtPcie_SetNetGateway(uint32_t Index, bool IpV6, const uint8_t* Gateway);

// Adds a route to the subnet Dst, of PrefixLength bits, through Gateway. Returns false
// when there is no such interface, Dst or Gateway is NULL, or there is no room.
bool SimDtPcie_AddNetRoute(uint32_t Index, bool IpV6, const uint8_t* Dst,
                           int PrefixLength, const uint8_t* Gateway);

// Adds Ip as a known neighbour with MAC address Mac on an interface, which need not
// exist. Returns false when Ip or Mac is NULL, or there is no room.
bool SimDtPcie_AddNetNeighbour(uint32_t Index, bool IpV6, const uint8_t* Ip,
                               const uint8_t* Mac);

// Make binding, or joining and leaving groups, fail from now on when Fail is true.
void SimDtPcie_FailNetBind(bool Fail);
void SimDtPcie_FailNetJoin(bool Fail);

// A group a socket joined.
typedef struct SimNetMembership
{
    uint32_t IfIndex;   // The interface it was joined on
    bool IpV6;          // The group is an IPv6 group
    uint8_t Group[16];  // The group's address
    bool HasSource;     // The join names a source
    uint8_t Source[16]; // The source's address, when HasSource
    uint16_t Port;      // The port the socket is bound to
} SimNetMembership;

// SimDtPcie_NetMembershipCount() returns the number of groups the open sockets joined.
// SimDtPcie_GetNetMembership() returns the one at Index, in the order they were joined,
// and false when there is none.
int SimDtPcie_NetMembershipCount(void);
bool SimDtPcie_GetNetMembership(int Index, SimNetMembership* Membership);

// Returns the number of open sockets.
int SimDtPcie_OpenNetSocketCount(void);

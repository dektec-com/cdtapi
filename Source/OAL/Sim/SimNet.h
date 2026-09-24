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
// The network OsNet.h gives while the emulator is asked for: tables of interfaces with
// their addresses, gateways and routes, and of neighbours, which the tests set, and
// sockets that remember the groups they join. It answers at once, as an operating system
// does that knows every neighbour:
//
//   interfaces  as added, in that order
//   routes      a destination in the subnet of one of the interface's addresses of its
//               family, or IPv6 link-local or IPv4 link-local, is reached directly;
//               otherwise the route with the longest matching prefix gives the gateway,
//               then the default gateway; without either there is no route
//   neighbours  known or not found
//   binding     to the any address, or to an address of an interface, IPv6 link-local
//               ones only with that interface's index; a port of 0 gets a free port from
//               49152 up, and a port may be bound twice
//   groups      joining needs a multicast group of the socket's family and an existing
//               interface; joining a group twice, or leaving one that was not joined,
//               with the same source, fails, as on Linux; closing leaves every group
//
// The DTA-2110 comes with an interface, as a card whose network driver is installed: see
// the SIM_NET_DTA2110_ values below. A reset takes it and every other interface away.
//

// The DTA-2110's interface: its index and name, its addresses and gateways, and the
// neighbours the gateways are.
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

// Room in the tables.
#define SIM_NET_MAX_INTERFACES 16
#define SIM_NET_MAX_ADDRESSES 8
#define SIM_NET_MAX_ROUTES 8
#define SIM_NET_MAX_NEIGHBOURS 32
#define SIM_NET_MAX_MEMBERSHIPS 64

// Takes every interface, neighbour, group and control back to the power-on state,
// without taking the emulator's lock. Open sockets stay open and counted.
void SimNet_Reset(void);

// Adds the DTA-2110's interface with the MAC address Mac, or takes it away, without
// taking the emulator's lock.
void SimNet_SetDta2110Interface(bool Present, const uint8_t* Mac);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Adds an interface with index Index, which must not be in use, and returns true; false
// when the table is full. A VLAN interface has a VlanId and the ParentIndex of the
// interface it is on. It starts enabled and connected, without addresses.
bool SimDtPcie_AddNetInterface(uint32_t Index, const uint8_t* Mac, int VlanId,
                               uint32_t ParentIndex, const char* Name);

// Takes an interface away with its routes and neighbours.
void SimDtPcie_RemoveNetInterface(uint32_t Index);

// Makes an interface enabled or not, and connected or not.
void SimDtPcie_SetNetInterfaceUp(uint32_t Index, bool AdminUp, bool LinkUp);

// Adds an address to an interface; false when there is no room.
bool SimDtPcie_AddNetAddress(uint32_t Index, const OsNetAddr* Addr);

// Takes the IPv4 or IPv6 addresses of an interface away.
void SimDtPcie_ClearNetAddresses(uint32_t Index, bool IpV6);

// Sets the default gateway of an interface for a family, or takes it away with NULL.
void SimDtPcie_SetNetGateway(uint32_t Index, bool IpV6, const uint8_t* Gateway);

// Adds a route to the subnet Dst of PrefixLength bits through Gateway; false when there
// is no room.
bool SimDtPcie_AddNetRoute(uint32_t Index, bool IpV6, const uint8_t* Dst,
                           int PrefixLength, const uint8_t* Gateway);

// Makes Ip a known neighbour with MAC address Mac on an interface; false when there is no
// room.
bool SimDtPcie_AddNetNeighbour(uint32_t Index, bool IpV6, const uint8_t* Ip,
                               const uint8_t* Mac);

// Makes binding, or joining and leaving, fail from now on when true.
void SimDtPcie_FailNetBind(bool Fail);
void SimDtPcie_FailNetJoin(bool Fail);

// A group a socket joined.
typedef struct SimNetMembership
{
    uint32_t IfIndex;
    bool IpV6;
    uint8_t Group[16];
    bool HasSource;
    uint8_t Source[16];
    uint16_t Port; // The port the socket is bound to
} SimNetMembership;

// The number of groups the open sockets joined, and the one at Index, in the order they
// were joined; false when there is none.
int SimDtPcie_NetMembershipCount(void);
bool SimDtPcie_GetNetMembership(int Index, SimNetMembership* Membership);

// The number of open sockets.
int SimDtPcie_OpenNetSockets(void);

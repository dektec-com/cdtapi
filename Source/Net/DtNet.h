// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtNet.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - An IP port in the operating system's network: its address and neighbours
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDtapiLite includes
#include "CDtapiLite.h" // DtapiResult.
#include "OAL/OsNet.h"  // Interfaces, addresses and sockets.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Addresses +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An address is 16 bytes in network byte order, an IPv4 address in the first 4, as in
// OsNet.h. The kinds are DTAPI's (NwUtility).
//

// All zero.
bool DtNet_IsEmpty(bool IpV6, const uint8_t* Ip);

// 224.0.0.0/4, or ff00::/8.
bool DtNet_IsMulticast(bool IpV6, const uint8_t* Ip);

// 232.0.0.0/8, or ff30::/12: source-specific multicast.
bool DtNet_IsSourceSpecific(bool IpV6, const uint8_t* Ip);

// 169.254.0.0/16, or fe80::/10.
bool DtNet_IsLinkLocal(bool IpV6, const uint8_t* Ip);

// fc00::/7, the unique local addresses that replaced the site-local ones.
bool DtNet_IsSiteLocalV6(const uint8_t* Ip);

// 2000::/3.
bool DtNet_IsGlobalV6(const uint8_t* Ip);

// The subnet mask of a prefix of PrefixLength bits, into the 16 bytes at Mask.
void DtNet_PrefixMask(bool IpV6, int PrefixLength, uint8_t* Mask);

// Whether A and B are in the same subnet under Mask.
bool DtNet_SameSubnet(bool IpV6, const uint8_t* A, const uint8_t* B, const uint8_t* Mask);

// The MAC address of a multicast group: 01:00:5e and the low 23 bits of an IPv4 group,
// or 33:33 and the low 32 bits of an IPv6 group.
void DtNet_MulticastMac(bool IpV6, const uint8_t* Group, uint8_t* Mac);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Own address +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The port's interface is the operating system's interface with the port's MAC address,
// or the VLAN interface on it. Of its addresses of a kind the first preferred one counts,
// then the first deprecated one; one whose duplicate address detection has not finished
// never does.
//

// The kinds of own address.
#define DT_NET_ADDR_IPV4 0
#define DT_NET_ADDR_LINK_LOCAL 1 // IPv6
#define DT_NET_ADDR_SITE_LOCAL 2 // IPv6
#define DT_NET_ADDR_GLOBAL 3     // IPv6
#define DT_NET_ADDR_OTHER 4      // IPv6, in the subnet of a given address

typedef struct DtNetOwn
{
    OsNetItf Itf;
    bool IpV6;
    uint8_t Ip[16];
    uint8_t Mask[16];
    uint8_t Gateway[16]; // All zero when the interface has no default route
} DtNetOwn;

// Finds the port's own address of Kind, a DT_NET_ADDR_ value, with its mask, its
// interface's default gateway and the interface; for DT_NET_ADDR_OTHER an address in the
// subnet of Hint, or, when Hint is all zero, one of no other kind. The failures are
// DTAPI_E_NW_DRIVER when the operating system has no interface with the MAC address,
// DTAPI_E_VLAN_NOT_FOUND when it has no VLAN interface with the ID on it or, as DTAPI has
// it, when the VLAN interface has no such address, and DTAPI_E_NO_ADAPTER_IP_ADDR when
// the interface has none.
DtapiResult DtNet_GetOwnAddress(const uint8_t* Mac, int VlanId, int Kind,
                                const uint8_t* Hint, DtNetOwn* Own);

// The own address to receive a stream to Stream from, as DTAPI's PrepareInputChannel
// chooses it: IPv4's address; for IPv6 multicast or the any address link-local, then
// site-local, then global, then any other; for IPv6 unicast the interface's address of
// Stream's kind, which must exist, taking Stream itself as the own address.
DtapiResult DtNet_ChooseInputAddress(const uint8_t* Mac, int VlanId, bool IpV6,
                                     const uint8_t* Stream, DtNetOwn* Own);

// The own address to send to Dst from, as DTAPI's PrepareOutputChannel chooses it: IPv4's
// address; for IPv6 the kind of Dst first, a multicast group's kind being its scope, and
// the other kinds after it in DTAPI's order.
DtapiResult DtNet_ChooseOutputAddress(const uint8_t* Mac, int VlanId, bool IpV6,
                                      const uint8_t* Dst, DtNetOwn* Own);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Neighbours +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The MAC address to send to Dst from Own, as DTAPI's TxFifo finds it: a multicast
// group's own, the broadcast address for an IPv4 broadcast, the neighbour Dst in Own's
// subnet or link-local, and otherwise the gateway: Gateway when it is not NULL or all
// zero, else the one of the operating system's best route, or Dst itself when that route
// has none, else Own's default gateway.
// DTAPI_E_DST_MAC_ADDR when there is no gateway or the neighbour does not answer.
DtapiResult DtNet_ResolveDstMac(const DtNetOwn* Own, const uint8_t* Dst,
                                const uint8_t* Gateway, uint8_t* Mac);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Operation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Whether the port can be used for IPv4 and for IPv6, as DTAPI's
// DtDevice::IsNetworkCardOperational checks it once the link is up: each asked for needs
// an own address, a socket bound to it, and an enabled interface. Otherwise
// DTAPI_E_NO_ADAPTER_IP_ADDR, DTAPI_E_BIND or DTAPI_E_DISABLED, or a failure of
// DtNet_GetOwnAddress.
DtapiResult DtNet_CheckOperational(const uint8_t* Mac, int VlanId, bool IpV4, bool IpV6);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Groups +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Joins Group on the socket, as DTAPI's AddMultiCastMemberShip does: from any source
// without sources or for an all-zero one, and from each distinct source otherwise.
// Sources holds NumSources addresses of 16 bytes each.
// DTAPI_E_MULTICASTJOIN when a join fails; the joins before it stay.
DtapiResult DtNet_Join(OsNetSocket* Socket, uint32_t IfIndex, bool IpV6,
                       const uint8_t* Group, const uint8_t* Sources, int NumSources);

// Leaves what DtNet_Join joined with the same arguments, going on past failures.
void DtNet_Leave(OsNetSocket* Socket, uint32_t IfIndex, bool IpV6, const uint8_t* Group,
                 const uint8_t* Sources, int NumSources);

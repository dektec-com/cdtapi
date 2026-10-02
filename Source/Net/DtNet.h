// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtNet.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - An IP port in the operating system's network: its address and neighbours
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "OAL/OsNet.h" // Interfaces, addresses and sockets.
#include "cdtapi.h"    // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Addresses +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Tests and computations on IP addresses. An address is 16 bytes in network byte order;
// an IPv4 address fills the first 4, as in OsNet.h.
//

// Returns whether Ip is all zero.
bool DtNet_IsEmpty(bool IpV6, const uint8_t* Ip);

// Returns whether Ip is a multicast address: 224.0.0.0/4, or ff00::/8.
bool DtNet_IsMulticast(bool IpV6, const uint8_t* Ip);

// Returns whether Ip is a source-specific multicast address: 232.0.0.0/8, or ff30::/12.
bool DtNet_IsSourceSpecific(bool IpV6, const uint8_t* Ip);

// Returns whether Ip is a link-local address: 169.254.0.0/16, or fe80::/10.
bool DtNet_IsLinkLocal(bool IpV6, const uint8_t* Ip);

// Returns whether the IPv6 address Ip is a unique local address, fc00::/7. These
// replaced the site-local addresses, and the library calls them site-local.
bool DtNet_IsSiteLocalV6(const uint8_t* Ip);

// Returns whether the IPv6 address Ip is a global address, 2000::/3.
bool DtNet_IsGlobalV6(const uint8_t* Ip);

// Writes the subnet mask of a prefix of PrefixLength bits into the 16 bytes at Mask.
void DtNet_PrefixMask(bool IpV6, int PrefixLength, uint8_t* Mask);

// Returns whether addresses A and B are in the same subnet, given its Mask.
bool DtNet_SameSubnet(bool IpV6, const uint8_t* A, const uint8_t* B, const uint8_t* Mask);

// Writes the MAC address that packets to the multicast group Group are sent to into the
// 6 bytes at Mac. For IPv4 that is 01:00:5e with the low 23 bits of the group; for IPv6,
// 33:33 with the low 32 bits.
void DtNet_MulticastMac(bool IpV6, const uint8_t* Group, uint8_t* Mac);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Own address +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A DekTec IP port is a network interface of the operating system. The library finds it
// by the port's MAC address, or, for a VLAN, as the VLAN interface on that interface.
// The port's own address is one of the interface's addresses.
//
// An interface can have several addresses: one IPv4 address, and IPv6 addresses of
// several kinds. The functions below pick the address of a kind. A preferred address
// comes before a deprecated one, and an address that is still being checked for
// duplicates (tentative) is never picked.
//

// The kinds of own address, for DtNet_GetOwnAddress.
#define DT_NET_ADDR_IPV4 0
#define DT_NET_ADDR_LINK_LOCAL 1 // IPv6
#define DT_NET_ADDR_SITE_LOCAL 2 // IPv6
#define DT_NET_ADDR_GLOBAL 3     // IPv6
#define DT_NET_ADDR_OTHER_V6 4   // IPv6, in the subnet of a given address

typedef struct DtNetOwnAddress
{
    OsNetItf Itf;        // The interface of the port
    bool IpV6;           // Whether Ip is an IPv6 address
    uint8_t Ip[16];      // The address
    uint8_t Mask[16];    // The subnet mask of the address
    uint8_t Gateway[16]; // The interface's default gateway; all zero when it has none
} DtNetOwnAddress;

// Finds the port's own address of a kind, with its subnet mask, the interface's default
// gateway and the interface. Mac is the port's MAC address; VlanId is 0 for the port
// itself, or the ID of a VLAN on it. Kind is a DT_NET_ADDR_ value.
//
// For DT_NET_ADDR_OTHER_V6, SubnetOf picks the address: one in the same subnet as
// SubnetOf. When SubnetOf is NULL or all zero, any IPv6 unicast address of no other kind
// that is not loopback.
//
// Returns DTAPI_OK, or, checked in this order:
//   DTAPI_E_INVALID_ARG         Mac or Own is NULL, or Kind is unknown
//   DTAPI_E_VLAN_NOT_FOUND      the port's interface has no VLAN interface with VlanId
//   DTAPI_E_OUT_OF_MEM          not enough memory to list the interfaces
//   DTAPI_E_NW_DRIVER           no interface has the MAC address, or listing failed
//   DTAPI_E_VLAN_NOT_FOUND      the VLAN interface has no address of Kind
//   DTAPI_E_NO_ADAPTER_IP_ADDR  the interface has no address of Kind
// On an error, *Own is all zero.
DtapiResult DtNet_GetOwnAddress(const uint8_t* Mac, int VlanId, int Kind,
                                const uint8_t* SubnetOf, DtNetOwnAddress* Own);

// Chooses the own address to receive a stream on, given the stream's StreamAddress.
//   - IPv4: the port's IPv4 address.
//   - IPv6 multicast, or StreamAddress all zero: the first address found of link-local,
//     site-local, global and other, in that order.
//   - IPv6 unicast: StreamAddress itself. The interface must have an address of the same
//     kind, and Own gets that address's mask, gateway and interface.
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG when StreamAddress or Own is NULL, or the errors
// of DtNet_GetOwnAddress.
DtapiResult DtNet_ChooseInputAddress(const uint8_t* Mac, int VlanId, bool IpV6,
                                     const uint8_t* StreamAddress, DtNetOwnAddress* Own);

// Chooses the own address to send to Dst from.
//   - IPv4: the port's IPv4 address.
//   - IPv6: an address of the same kind as Dst first, then the other kinds in a fixed
//     order. For a multicast group, its scope decides the kind.
// Returns DTAPI_OK, DTAPI_E_INVALID_ARG when Dst or Own is NULL, or the errors of
// DtNet_GetOwnAddress.
DtapiResult DtNet_ChooseOutputAddress(const uint8_t* Mac, int VlanId, bool IpV6,
                                      const uint8_t* Dst, DtNetOwnAddress* Own);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Neighbours +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Finds the MAC address to send packets for Dst to, from the own address Own, and
// writes it into the 6 bytes at Mac:
//   - for a multicast group, the group's MAC address
//   - for an IPv4 broadcast, ff:ff:ff:ff:ff:ff
//   - for Dst in Own's subnet, or link-local, the MAC address of Dst itself
//   - otherwise the MAC address of a gateway: Gateway when it is not NULL or all zero;
//     else the gateway of the operating system's best route, or Dst itself when that
//     route has none; else Own's default gateway.
// Finding a neighbour's MAC address can take seconds; see OsNet_ResolveNeighbour.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG    Own, Dst or Mac is NULL
//   DTAPI_E_DST_MAC_ADDR   there is no gateway, or the neighbour does not answer
DtapiResult DtNet_ResolveDstMac(const DtNetOwnAddress* Own, const uint8_t* Dst,
                                const uint8_t* Gateway, uint8_t* Mac);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Operation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Checks that the port can be used for IPv4, for IPv6, or both, as IpV4 and IpV6 ask.
// It is checked once the link is up. Each IP version needs an own address, a socket that
// can be bound to it, and an enabled interface.
//
// Returns DTAPI_OK, or, checked in this order:
//   DTAPI_E_INVALID_ARG         Mac is NULL
//   the errors of DtNet_GetOwnAddress
//   DTAPI_E_NO_ADAPTER_IP_ADDR  the own address is all zero
//   DTAPI_E_BIND                no socket can be bound to the own address
//   DTAPI_E_DISABLED            the interface is disabled
DtapiResult DtNet_CheckOperational(const uint8_t* Mac, int VlanId, bool IpV4, bool IpV6);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Groups +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Joins the multicast group Group on Socket, on the interface with index ItfIndex.
// Sources holds NumSources source addresses of 16 bytes each. With no sources, or for a
// source that is all zero, the socket receives from any source; otherwise it joins once
// for each different source.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG     Socket or Group is NULL, NumSources is negative, or Sources
//                           is NULL while NumSources is not 0
//   DTAPI_E_MULTICASTJOIN   a join failed; the joins before it stay
DtapiResult DtNet_Join(OsNetSocket* Socket, uint32_t ItfIndex, bool IpV6,
                       const uint8_t* Group, const uint8_t* Sources, int NumSources);

// Leaves what DtNet_Join joined with the same arguments. It goes on when one leave
// fails.
void DtNet_Leave(OsNetSocket* Socket, uint32_t ItfIndex, bool IpV6, const uint8_t* Group,
                 const uint8_t* Sources, int NumSources);

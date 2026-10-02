// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosAddr.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - IP addresses as an SDP writes them, read into bytes and written from them
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Addresses +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The NMOS bridge converts the addresses in an SDP to bytes and back. An address is 16
// bytes in network byte order; an IPv4 address takes the first 4, as in OsNet.h.
//
// Only literal addresses are read, never host names: looking up a name could block the
// bridge. An IPv6 address is written in the short form of RFC 5952.
//

// The size of the longest address text, an IPv6 address that ends in an IPv4 address,
// including its terminating null.
#define DT_NMOS_ADDR_SIZE 46

// Writes address Ip as text into Text, of Size bytes, null-terminated. Returns DTAPI_OK,
// or DTAPI_E_BUF_TOO_SMALL when it does not fit; DT_NMOS_ADDR_SIZE bytes always do.
DtapiResult DtNmosAddr_Format(bool IpV6, const uint8_t Ip[16], char* Text, size_t Size);

// Reads an address from Text into Ip, setting all 16 bytes, and sets *IpV6 to whether it
// is an IPv6 address. Accepted are:
//
//   - An IPv4 address: four decimal numbers of 0 to 255, without leading zeros, which
//     some readers take as octal.
//   - An IPv6 address: up to eight groups of hexadecimal digits, with at most one "::"
//     for a run of zero groups, and optionally an IPv4 address as the last two groups.
//     A zone, such as "%eth0", is not accepted.
//
// Returns DTAPI_OK, DTAPI_E_NOT_SUPPORTED when Text is a host name, or
// DTAPI_E_INVALID_ARG when it is not an address in any other way.
DtapiResult DtNmosAddr_Parse(const char* Text, uint8_t Ip[16], bool* IpV6);

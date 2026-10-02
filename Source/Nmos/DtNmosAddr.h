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
// An address is 16 bytes in network byte order, an IPv4 address in the first 4, as in
// OsNet.h. Only literal addresses are read: the bridge looks no name up, which would
// block. Written, an IPv6 address takes RFC 5952's short form.
//

// The longest address written, an IPv6 address with an IPv4 address in it, and its null.
#define DT_NMOS_ADDR_SIZE 46

// Writes the address Ip into Text, of Size bytes, with its null. DTAPI_E_BUF_TOO_SMALL
// when it does not fit, which DT_NMOS_ADDR_SIZE always does.
DtapiResult DtNmosAddr_Format(bool IpV6, const uint8_t Ip[16], char* Text, size_t Size);

// Reads Text into Ip, all 16 bytes of which it sets, and *IpV6. An IPv4 address is four
// decimal numbers of 0 to 255 without leading zeros, which could be read as octal; an
// IPv6 address is eight groups of hexadecimal digits, with one "::" for a run of zero
// groups and the last two groups as an IPv4 address if wanted, and without a zone.
// DTAPI_E_NOT_SUPPORTED for a domain name, DTAPI_E_INVALID_ARG for anything else that
// is not an address.
DtapiResult DtNmosAddr_Parse(const char* Text, uint8_t Ip[16], bool* IpV6);

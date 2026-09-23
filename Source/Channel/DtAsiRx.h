// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAsiRx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The ASI side of an input channel
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtRxBackend.h" // The side's functions.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtAsiRx +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The receiving side of a port whose I/O standard is ASI, with no thread and no software
// FIFO of its own (0011). The side holds AF_ASISDIRX and AF_DMA of the port exclusively,
// drives ASIRX, CDMAC and BURSTFIFO, and gives CDMAC a receive buffer of
// DT_ASIRX_RING_SIZE, into which the card writes transparent packets (DtTsTrp.h).
//
// A scan walks what the card has written since the last: the packets that convert are
// counted in the load, the bytes Read would deliver in the receive mode; a packet that
// would take the load over DT_ASIRX_FIFO_SIZE is dropped, with DTAPI_RX_FIFO_OVF; and
// bytes that are no packet are searched for the stream. What a scan drops or searches
// past is noted, so that taking the bytes later converts the same packets again without
// looking at the flags. Taking converts straight from the buffer into the caller's,
// keeping at most one packet's output for the next take.
//

// The FIFO size the load never exceeds, and the buffer that stands in for the FIFO.
#define DT_ASIRX_FIFO_SIZE (8 * 1024 * 1024)
#define DT_ASIRX_RING_SIZE (16 * 1024 * 1024)

// Attaches the side to the port: takes the API functions exclusively, puts ASIRX, the
// burst FIFO and CDMAC in IDLE, flushes CDMAC, registers the receive buffer, sets ASIRX
// to automatic polarity, synchronisation and packets, clears the flags, and sets
// DTAPI_RXMODE_ST188. *Rx is the side after a success, NULL otherwise.
DtapiResult DtAsiRx_Attach(const DtRxPort* Port, DtRx** Rx);

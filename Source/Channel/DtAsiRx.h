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
// The side of an input channel that receives ASI (see DtRxBackend.h). It has no thread
// and no FIFO of its own: the card writes transparent packets (see DtTsTrp.h) into a
// receive buffer of DT_ASIRX_RING_SIZE, and a read takes them from there. The side holds
// the port's AF_ASISDIRX and AF_DMA exclusively, and drives their ASIRX, CDMAC and
// BURSTFIFO.
//
// A scan walks what the card has written since the last scan:
// - each packet that decodes adds to the load: the bytes a read would deliver in the
//   receive mode;
// - a packet that would take the load over DT_ASIRX_FIFO_SIZE is dropped, and sets
//   DTAPI_RX_FIFO_OVF;
// - bytes that are not a packet are searched for the next packet.
// The scan notes what it dropped or skipped, so that delivering decodes the same packets
// again without looking at the flags. Delivering decodes from the receive buffer straight
// into the caller's buffer, and keeps at most one packet's output for the next read.
//

// The largest load: the size of the FIFO that a read sees.
#define DT_ASIRX_FIFO_SIZE (8 * 1024 * 1024)
// The size of the receive buffer the card writes into.
#define DT_ASIRX_RING_SIZE (16 * 1024 * 1024)

// Creates the ASI side for the port, and returns it in *Rx (NULL after a failure). The
// side starts idle, in DTAPI_RXMODE_ST188, with the receiver set to detect polarity,
// synchronisation and packet size by itself, and with cleared flags.
//
// Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM, or the failure of finding the port's blocks,
// taking exclusive access or setting them up.
DtapiResult DtAsiRx_Attach(const DtRxAttachedPort* Port, DtRx** Rx);

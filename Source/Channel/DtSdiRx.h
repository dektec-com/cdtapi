// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiRx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The SDI side of an input channel
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtRxBackend.h" // The side's functions.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtSdiRx +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The receiving side of a port whose I/O standard is SDI, which delivers raw SDI frames
// straight from the card's CHSDIRX ring: it follows the write offset the driver reports,
// checks each frame's header, converts the frame's coded lines into the caller's buffer
// and sets the read offset past it. Plan 0007 holds where it departs from DTAPI.
//

// Attaches the side to the port, whose I/O configuration IoStd is an SDI standard: finds
// the receiver and the receive channel, reads the down-scaling, sets
// DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B, applies the I/O standard again and sets
// the receive channel up for it. *Rx is the side after a success, NULL otherwise.
DtapiResult DtSdiRx_Attach(const DtRxPort* Port, const DtIoConfig* IoStd, DtRx** Rx);

// Converts the lines of a 4K frame over Threads threads of the library's own, 1 for the
// reading thread alone, which is the default. The threads live until the side is
// released or the count is set again. No other standard divides: the lines of a packed
// frame share a byte at each boundary.
//
// The channel must not be reading while this is called. DTAPI_E_INVALID_ARG below 1,
// DTAPI_E_OUT_OF_MEM when the threads or their buffers cannot be had, and then the side
// converts in the reading thread again.
DtapiResult DtSdiRx_SetConversionThreads(DtRx* Rx, int Threads);

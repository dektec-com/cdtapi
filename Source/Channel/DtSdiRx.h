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

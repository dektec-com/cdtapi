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
// The side of an input channel that receives raw SDI frames (see DtRxBackend.h). It
// reads the frames straight from the ring of the card's CHSDIRX receive channel. For each
// frame, it checks the frame's header, decodes the frame's coded lines into the caller's
// buffer, and moves the ring's read offset past the frame. Plan 0007 lists where it
// differs from DTAPI.
//

// Creates the SDI side for the port, and returns it in *Rx (NULL after a failure). IoStd
// is the port's I/O configuration, with an SDI standard. Attaching sets the I/O standard
// on the port again, and sets the receive channel up for it. The side starts idle, in
// DTAPI_RXMODE_SDI_FULL | DTAPI_RXMODE_SDI_10B.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_FOUND      the port has no SDI receiver or receive channel
//   DTAPI_E_DRIVER_INCOMP  the driver is too old for them
//   DTAPI_E_OUT_OF_MEM     not enough memory
// and the errors of DtFunc_Find() and of setting the port up.
DtapiResult DtSdiRx_Attach(const DtRxAttachedPort* Port, const DtIoConfig* IoStd,
                           DtRx** Rx);

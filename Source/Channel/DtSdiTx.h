// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiTx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The SDI side of an output channel
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTxBackend.h" // The side's functions.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtSdiTx +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The side of an output channel that sends raw SDI frames (see DtTxBackend.h). It has no
// FIFO of its own: a write encodes the raw frame straight into the DMA buffer. The side
// keeps the stream aligned on frames, encodes each line into its place after the frame's
// header, and moves the write offset on once the frame is complete.
//
// While the channel sends, a thread keeps the signal going: whenever less than one frame
// is left to send, it writes a black frame. Plan 0008 lists where the side differs from
// DTAPI.
//

// Creates the SDI side for the port, and returns it in *Tx (NULL after a failure). IoStd
// is the port's I/O configuration, with an SDI standard. Attaching sets the I/O standard
// on the port again, takes the transmitter and the DMA exclusively, sets every block
// idle, turns the encoder's corrections on, and sets the channel up for the standard.
// The side starts idle, in DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B.
//
// Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM, or the failure of finding the port's blocks,
// taking exclusive access or setting them up.
DtapiResult DtSdiTx_Attach(const DtTxAttachedPort* Port, const DtIoConfig* IoStd,
                           DtTx** Tx);

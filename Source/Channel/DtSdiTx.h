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
// The transmitting side of a port whose I/O standard is SDI, with no software FIFO of its
// own. The side drives the port's transmit blocks and converts the raw frames a write
// takes straight into the DMA buffer: it aligns the stream on frames, codes each line
// into its place behind a header, and moves the write offset on when a frame is complete.
// A thread keeps the signal while sending by writing a black frame whenever less than a
// frame is left. Plan 0008 holds where it departs from DTAPI.
//

// Attaches the side to the port, whose I/O configuration IoStd is an SDI standard: sets
// DTAPI_TXMODE_SDI_FULL | DTAPI_TXMODE_SDI_10B and clears the flags, applies the I/O
// standard again, takes the transmitter and the DMA exclusively, sets every block idle
// and the encoder's corrections on, and sets the channel up for the standard. *Tx is the
// side after a success, NULL otherwise.
DtapiResult DtSdiTx_Attach(const DtTxPort* Port, const DtIoConfig* IoStd, DtTx** Tx);

// Codes the lines of a 4K frame over Threads threads of the library's own, 1 for the
// writing thread alone, which is the default. The threads live until the side is
// released or the count is set again. No other standard divides: the lines of a packed
// frame share a byte at each boundary.
//
// What one call to write brings is what can be divided, so a caller that hands over a
// whole frame gets the whole of it and one that hands over a line at a time gets none.
//
// The channel must not be writing while this is called. DTAPI_E_INVALID_ARG below 1,
// DTAPI_E_OUT_OF_MEM when the threads or their buffers cannot be had, and then the side
// codes in the writing thread again.
DtapiResult DtSdiTx_SetConversionThreads(DtTx* Tx, int Threads);

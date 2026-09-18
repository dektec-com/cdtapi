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
// What DTAPI's SdiTxImpl_Bb2 does, whose software FIFO a Matrix row empties into
// MxChannelMemlessTx, which drives the port's transmit blocks. This side drives the
// blocks itself and converts the raw frames a write takes straight into the DMA buffer:
// it aligns the stream on frames as SdiTxImpl_Bb2 does, codes each line into its place
// behind a header, and moves the write offset on when a frame is complete. A thread
// keeps the signal while sending by writing a black frame whenever less than a frame is
// left. Plan 0008 holds where it departs from DTAPI.
//

// SdiTxImpl_Bb2::SetIoConfig and MxChannelMemlessTx::Attach on the port, whose I/O
// configuration IoStd is an SDI standard: sets DTAPI_TXMODE_SDI_FULL |
// DTAPI_TXMODE_SDI_10B and clears the flags, applies the I/O standard again, takes the
// transmitter and the DMA exclusively, sets every block idle and the encoder's
// corrections on, and sets the channel up for the standard. *Tx is the side after a
// success, NULL otherwise.
DtapiResult DtSdiTx_Attach(const DtTxPort* Port, const DtIoConfig* IoStd, DtTx** Tx);

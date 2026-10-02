// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAsiTx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The ASI side of an output channel
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTxBackend.h" // The side's functions.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtAsiTx +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The side of an output channel that sends ASI (see DtTxBackend.h). The side holds the
// port's AF_ASISDITX and AF_DMA exclusively, and drives ASITXG, the port's SDITXPHY or
// ASITXSER, CDMAC and BURSTFIFO.
//
// The software makes the ASI symbols. A write goes into a FIFO of DT_ASITX_FIFO_SIZE, and
// DtAsiEnc encodes the FIFO into 8b/10b symbols in a DMA buffer of DT_ASITX_BUF_SIZE. The
// card sends those at the ASI line rate, whatever the transport-stream rate.
// - While the channel holds, each write encodes what it wrote.
// - While it sends, a thread encodes every 10 ms, or sooner when a write leaves more
//   than 100 packets or 5 ms of data in the FIFO. When the FIFO runs dry, the thread
//   pads the last data word with K28.5. With stuffing on, it fills the buffer up to
//   50 ms of symbols with null packets.
//
// The double-buffered and monitor outputs that name the port as their master in
// ParXtra[0] are the side's slaves. The side holds each exclusively, through
// AF_ASISDITX, AF_SDIPHYONLYTX or AF_ASISDIMON depending on its direction, sets it to
// ASI, and starts and stops its PHY with the master's.
//

// The size of the FIFO a write goes into.
#define DT_ASITX_FIFO_SIZE (8 * 1024 * 1024)
// The size of the DMA buffer the symbols are encoded into.
#define DT_ASITX_BUF_SIZE (8 * 1024 * 1024)

// Creates the ASI side for the port, and returns it in *Tx (NULL after a failure). The
// port and its slaves then send K28.5. The side starts idle, in
// DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST without stuffing, at 10 Mbit/s, with normal
// polarity and cleared flags.
//
// Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM, or the failure of finding the port's blocks
// or slaves, taking exclusive access or setting them up.
DtapiResult DtAsiTx_Attach(const DtTxAttachedPort* Port, DtTx** Tx);

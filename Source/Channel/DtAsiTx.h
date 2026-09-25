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
// The transmitting side of a port whose I/O standard is ASI (plan 0011). The side holds
// AF_ASISDITX and AF_DMA of the port exclusively and drives ASITXG, the port's SDITXPHY
// or ASITXSER, CDMAC and BURSTFIFO. The software makes the symbol stream: a write goes
// into a FIFO of DT_ASITX_FIFO_SIZE, and DtAsiEnc encodes it into 8b/10b symbols in a
// DMA buffer of DT_ASITX_BUF_SIZE, which the card sends at the ASI line rate whatever the
// transport-stream rate. While holding a write encodes what it wrote; while sending a
// thread encodes every 10 ms, or sooner when a write leaves more than 100 packets or
// 5 ms of data in the FIFO, pads the last data word with K28.5 when the FIFO runs dry,
// and with stuffing tops the buffer up to 50 ms of symbols with null packets.
//
// The double-buffered and monitor outputs that name the port as their master in
// ParXtra[0] are the side's slaves: held exclusively through AF_ASISDITX, AF_SDIPHYONLYTX
// or AF_ASISDIMON by their direction, set to ASI, and their PHYs moved with the master's.
//

// The software FIFO a write goes into, and the DMA buffer the symbols are encoded into.
#define DT_ASITX_FIFO_SIZE (8 * 1024 * 1024)
#define DT_ASITX_BUF_SIZE (8 * 1024 * 1024)

// Attaches the side to the port: takes the API functions and the slaves, registers the
// buffer, sets the slaves to ASI, starts sending K28.5, and sets the defaults,
// DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST without stuffing, 10 Mbit/s and normal polarity.
// *Tx is the side after a success, NULL otherwise.
DtapiResult DtAsiTx_Attach(const DtTxAttachedPort* Port, DtTx** Tx);

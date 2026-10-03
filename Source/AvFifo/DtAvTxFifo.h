// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvTxFifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the library itself reads of a transmit FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtAvPort.h"      // What a FIFO carries.
#include "cdtapi_avfifo.h" // The FIFO and its configurations.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Description +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The NMOS bridge describes a sender's stream from its transmit FIFO. These functions
// give it what it needs. Each takes the FIFO's lock, works whether the FIFO is started
// or not, and takes Where, the name of the calling public function, for the text of a
// failure.
//

// What is needed to describe the stream of a transmit FIFO.
typedef struct DtAvTxFifoDescription
{
    DtAvKind Kind;              // Whether the FIFO carries audio or video
    St2110_TxConfigAudio Audio; // The configuration, for an audio FIFO
    St2110_TxConfigVideo Video; // The configuration, for a video FIFO
    AvFifo_IpPars IpPars;       // Where the stream is sent
    uint8_t SourceIp[16];       // The port's IP address the stream is sent from, of
                                // IpPars's IP version, as Start chooses it
    uint8_t Mac[6];             // The port's MAC address
    const DtDevice* Device;     // The FIFO's own handle to the device, while attached
    int Port;                   // The port, counting from 1
} DtAvTxFifoDescription;

// Fills in the description of a transmit FIFO. The source address and MAC address are
// read from the driver and the operating system, the way Start finds them. Returns:
//
//   DTAPI_OK                The description is filled in
//   DTAPI_E_NOT_ATTACHED    The FIFO is not attached
//   DTAPI_E_CONFIG          The FIFO is not configured yet
//   DTAPI_E_NO_IPPARS       The FIFO has no IP parameters yet
//
// and the errors of checking the network, such as DTAPI_E_NO_LINK.
DtapiResult DtAvTxFifo_Describe(AvFifo_TxFifo* Fifo, DtAvTxFifoDescription* Description,
                                const char* Where);

// Gets the IP parameters of a transmit FIFO, without checking the network. Returns
// DTAPI_OK, DTAPI_E_NOT_ATTACHED when the FIFO is not attached, or DTAPI_E_NO_IPPARS when
// it has no IP parameters yet.
DtapiResult DtAvTxFifo_GetIpPars(AvFifo_TxFifo* Fifo, AvFifo_IpPars* IpPars,
                                 const char* Where);

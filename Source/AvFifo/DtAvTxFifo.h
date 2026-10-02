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

// A transmit FIFO as a description of its stream needs it: what it is configured with,
// and the addresses Start sends from.
typedef struct DtAvTxFifoDescription
{
    DtAvKind Kind;
    St2110_TxConfigAudio Audio; // Of an audio FIFO
    St2110_TxConfigVideo Video; // Of a video FIFO
    AvFifo_IpPars IpPars;
    uint8_t SourceIp[16]; // Of IpPars's IP version, as Start chooses it
    uint8_t Mac[6];       // Of the port
} DtAvTxFifoDescription;

// Describes Fifo, started or not, under its lock: its configuration, its IP parameters,
// and the port's address and MAC address Start sends from, which it asks the driver and
// the operating system for. DTAPI_E_NOT_ATTACHED, DTAPI_E_CONFIG before Configure,
// DTAPI_E_NO_IPPARS before SetIpPars, and what checking the network gives, with the
// failure text naming Where.
DtapiResult DtAvTxFifo_Describe(AvFifo_TxFifo* Fifo, DtAvTxFifoDescription* Description,
                                const char* Where);

// Gives the IP parameters of Fifo, started or not, under its lock, without checking the
// network. DTAPI_E_NOT_ATTACHED, and DTAPI_E_NO_IPPARS before SetIpPars, with the
// failure text naming Where.
DtapiResult DtAvTxFifo_GetIpPars(AvFifo_TxFifo* Fifo, AvFifo_IpPars* IpPars,
                                 const char* Where);

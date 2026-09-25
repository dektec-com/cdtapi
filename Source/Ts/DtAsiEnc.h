// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAsiEnc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A transport stream coded as the ASI symbols a DtPcie card sends
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult and the transmit modes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Symbols +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A DtPcie card sends what its DMA buffer holds as 10-bit 8b/10b symbols, one in each
// 16-bit word, at the ASI line rate of 27 M symbols a second; the library makes them.
// Every byte of the transport stream becomes its 8b/10b code for the running disparity,
// and K28.5 comma symbols fill the line so that the stream has the rate asked for.
//
// The rate is kept as an accumulator over an interval of 188 x 8 seconds, so that the
// bytes a rate in whole bits a second sends in it are a whole number. Before each packet
// go two K28.5, one when the rate leaves no room for two, none when it leaves none. In
// burst mode a packet's bytes go together and the fill between packets; in normal mode
// the fill is spread between the bytes. With DTAPI_TXMODE_TXONTIME every packet is
// preceded by a 32-bit little-endian time in 54 MHz ticks, and goes out when that time
// comes, counted from the first packet's.
//
// A byte where a packet should start that is not 0x47 sets the synchronisation error,
// and bytes are skipped up to the next 0x47; DTAPI_TXMODE_RAW has no packets and checks
// nothing.
//

// The symbols of the transmission character K28.5, for a negative and a positive
// running disparity.
#define DT_ASI_K28_5_RDNEG 0x17C
#define DT_ASI_K28_5_RDPOS 0x283

// The ASI line rate in symbols a second.
#define DT_ASI_SYMBOL_RATE 27000000

// The code of Byte for running disparity Rd, 0 for negative and 1 for positive; *NextRd
// receives the running disparity after it.
uint16_t DtAsiEnc_EncodeByte(uint8_t Byte, int Rd, int* NextRd);

typedef struct DtAsiEnc
{
    // What the mode sets.
    int InSize;      // Bytes of a packet written: 188 or 204
    int InUsed;      // Of which sent: 188 or 204
    int OutSize;     // Bytes of a packet sent, zeros after InUsed
    bool IsRaw;      // DTAPI_TXMODE_RAW
    bool IsBurst;    // DTAPI_TXMODE_BURST
    bool IsTxOnTime; // DTAPI_TXMODE_TXONTIME
    int64_t Rate;    // Bits a second, of 188-byte packets

    // What the rate sets.
    int NumK28BeforePacket;
    // The stream's bytes in an interval, a symbol each, and the symbols the interval has,
    // less the K28.5 before packets.
    int64_t SymbolsNeededPerInterval;
    int64_t SymbolsAvailablePerInterval;

    // The state of the stream.
    int Rd;
    int64_t RateAccumulator;
    int ByteIndex;   // Of the packet being sent
    int NumK28Sent;  // Before the packet being sent
    int BytesToSkip; // Bytes still to drop after the packet, for MIN16
    bool SyncErr, SyncErrLatched;

    // DTAPI_TXMODE_TXONTIME.
    int OnTimeState;
    uint8_t TimeBytes[4];
    uint32_t NowTicks;        // In 54 MHz ticks
    uint32_t PacketTimeTicks; // When the packet being sent goes out
    bool IsFirstPacket;
} DtAsiEnc;

// A stream with the default settings: DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST at 10 Mbit/s.
void DtAsiEnc_Init(DtAsiEnc* Enc);

// Sets the transmit mode, a DTAPI_TXMODE_ of the transport-stream group with
// DTAPI_TXMODE_BURST or DTAPI_TXMODE_TXONTIME. Returns DTAPI_E_NOT_IMPLEMENTED for
// DTAPI_TXMODE_RAWASI and DTAPI_E_INVALID_ARG for another mode, and then changes
// nothing. A rate the new packet size does not fit is not refused here but by Start.
DtapiResult DtAsiEnc_SetTxMode(DtAsiEnc* Enc, int TxMode);

// Sets the rate in bits a second, counted in 188-byte packets whatever the packet size.
// Returns DTAPI_E_INVALID_RATE for a rate of 0 or less, or one whose packets need more
// symbols than the line has, and then changes nothing.
DtapiResult DtAsiEnc_SetRate(DtAsiEnc* Enc, int64_t Rate);

// Starts a stream: positive running disparity, an empty accumulator, no packet begun,
// the synchronisation error cleared. Returns DTAPI_E_INVALID_RATE, and starts nothing,
// when the rate does not fit the packet size, except with DTAPI_TXMODE_TXONTIME, whose
// packets go out at their times whatever the rate.
DtapiResult DtAsiEnc_Start(DtAsiEnc* Enc);

// Encodes from In, InSize bytes, into Out, room for OutSymbols symbols, until either is
// used up. *BytesTaken receives the bytes taken and *SymbolsWritten the symbols written.
// A packet or a time stamp that is cut off continues in the next call.
void DtAsiEnc_Encode(DtAsiEnc* Enc, const uint8_t* In, size_t InSize, uint16_t* Out,
                     size_t OutSymbols, size_t* BytesTaken, size_t* SymbolsWritten);

// Writes Symbols K28.5 symbols into Out, keeping the running disparity, to fill the
// card's last data word.
void DtAsiEnc_Pad(DtAsiEnc* Enc, uint16_t* Out, size_t Symbols);

// The bytes, in packets of OutSize as sent, that Symbols symbols carry at the current
// rate, rounded down.
int64_t DtAsiEnc_BytesInSymbols(const DtAsiEnc* Enc, int64_t Symbols);

// DTAPI_TX_SYNC_ERR in *Flags and *Latched when set; ClearFlags clears it when Flags has
// it.
void DtAsiEnc_GetFlags(const DtAsiEnc* Enc, int* Flags, int* Latched);
void DtAsiEnc_ClearFlags(DtAsiEnc* Enc, int Flags);

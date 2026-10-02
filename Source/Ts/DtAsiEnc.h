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
// A DtPcie card sends its DMA buffer as 10-bit 8b/10b symbols, one in each 16-bit word,
// at the ASI line rate of 27 million symbols a second. The library makes those symbols
// with the functions below:
// - Each byte of the transport stream becomes its 8b/10b code for the running disparity.
// - K28.5 comma symbols fill the line, so that the stream has the rate asked for.
//
// The rate is kept with an accumulator over an interval of 188 x 8 seconds, so that a
// rate in whole bits a second sends a whole number of bytes per interval. Two K28.5 go
// before each packet; one when the rate leaves room for only one, none when it leaves
// none. In burst mode, a packet's bytes go out together, with the fill between packets;
// in normal mode, the fill is spread between the bytes.
//
// With DTAPI_TXMODE_TXONTIME, each packet is preceded by a 32-bit little-endian time in
// 54 MHz ticks. The packet goes out at that time, counted from the first packet's time.
//
// When a byte where a packet should start is not 0x47, the synchronisation error is set,
// and bytes are skipped up to the next 0x47. DTAPI_TXMODE_RAW has no packets and checks
// nothing.
//

// The symbol K28.5, for a negative and for a positive running disparity.
#define DT_ASI_K28_5_RDNEG 0x17C
#define DT_ASI_K28_5_RDPOS 0x283

// The ASI line rate, in symbols a second.
#define DT_ASI_SYMBOL_RATE 27000000

// Returns the 8b/10b code of Byte for running disparity Rd (0 negative, 1 positive).
// *NextRd receives the running disparity after it.
uint16_t DtAsiEnc_EncodeByte(uint8_t Byte, int Rd, int* NextRd);

// The state of one encoded stream.
typedef struct DtAsiEnc
{
    // Set by the transmit mode.
    int InSize;      // Bytes per packet written: 188 or 204
    int InUsed;      // ... of which sent: 188 or 204
    int OutSize;     // Bytes per packet sent; zeros follow the InUsed bytes
    bool IsRaw;      // DTAPI_TXMODE_RAW
    bool IsBurst;    // DTAPI_TXMODE_BURST
    bool IsTxOnTime; // DTAPI_TXMODE_TXONTIME
    int64_t Rate;    // The rate in bits a second, counted in 188-byte packets

    // Set by the rate.
    int NumK28BeforePacket; // The K28.5 symbols before each packet: 0, 1 or 2
    // The stream's bytes per interval (one symbol each), and the symbols the interval
    // has for them: all, less the K28.5 before packets.
    int64_t SymbolsNeededPerInterval;
    int64_t SymbolsAvailablePerInterval;

    // The state of the stream.
    int Rd;                       // The running disparity: 0 negative, 1 positive
    int64_t RateAccumulator;      // Keeps the rate over the interval
    int ByteIndex;                // The next byte of the packet being sent
    int NumK28Sent;               // K28.5 sent before the packet being sent
    int BytesToSkip;              // Bytes still to drop after the packet, for MIN16
    bool SyncErr, SyncErrLatched; // The synchronisation error, now and latched

    // For DTAPI_TXMODE_TXONTIME.
    int OnTimeState;          // Which part of a timed packet comes next
    uint8_t TimeBytes[4];     // The time bytes read so far
    uint32_t NowTicks;        // The stream's time, in 54 MHz ticks
    uint32_t PacketTimeTicks; // When the packet being sent goes out
    bool IsFirstPacket;       // True until the first packet's time is read
} DtAsiEnc;

// Sets *Enc to the defaults: DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST at 10 Mbit/s.
void DtAsiEnc_Init(DtAsiEnc* Enc);

// Sets the transmit mode: a transport-stream DTAPI_TXMODE_ value, optionally with
// DTAPI_TXMODE_BURST or DTAPI_TXMODE_TXONTIME. A rate that the new packet size cannot
// carry is not refused here, but by DtAsiEnc_Start.
//
// Returns DTAPI_OK, or, changing nothing:
//   DTAPI_E_NOT_IMPLEMENTED  DTAPI_TXMODE_RAWASI
//   DTAPI_E_INVALID_ARG      any other mode
DtapiResult DtAsiEnc_SetTxMode(DtAsiEnc* Enc, int TxMode);

// Sets the rate in bits a second, counted in 188-byte packets whatever the packet size.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_RATE, changing nothing, when Rate is 0 or less,
// or its packets need more symbols than the line has.
DtapiResult DtAsiEnc_SetRate(DtAsiEnc* Enc, int64_t Rate);

// Starts the stream: positive running disparity, an empty accumulator, no packet begun,
// and the synchronisation error cleared.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_RATE, starting nothing, when the packet size
// cannot carry the rate. With DTAPI_TXMODE_TXONTIME the rate is not checked, as packets
// go out at their own times.
DtapiResult DtAsiEnc_Start(DtAsiEnc* Enc);

// Encodes the InSize bytes at In into symbols at Out, which has room for OutSymbols,
// until either is used up. Returns the bytes taken in *BytesTaken and the symbols
// written in *SymbolsWritten. A packet or time stamp that is cut off continues in the
// next call.
void DtAsiEnc_Encode(DtAsiEnc* Enc, const uint8_t* In, size_t InSize, uint16_t* Out,
                     size_t OutSymbols, size_t* BytesTaken, size_t* SymbolsWritten);

// Writes Symbols K28.5 symbols to Out, keeping the running disparity. Used to fill up
// the card's last data word.
void DtAsiEnc_Pad(DtAsiEnc* Enc, uint16_t* Out, size_t Symbols);

// Returns how many bytes Symbols symbols carry at the current rate, rounded down,
// counted in packets of OutSize as sent.
int64_t DtAsiEnc_BytesInSymbols(const DtAsiEnc* Enc, int64_t Symbols);

// DtAsiEnc_GetFlags sets DTAPI_TX_SYNC_ERR in *Flags and *Latched when the
// synchronisation error is set. DtAsiEnc_ClearFlags clears it when Flags has
// DTAPI_TX_SYNC_ERR.
void DtAsiEnc_GetFlags(const DtAsiEnc* Enc, int* Flags, int* Latched);
void DtAsiEnc_ClearFlags(DtAsiEnc* Enc, int Flags);

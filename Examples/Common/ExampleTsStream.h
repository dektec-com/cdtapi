// #*#*#*#*#*#*#*#*#*#*#*#*#* ExampleTsStream.h *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Test transport streams: numbered packets, and one MPEG-2 video service
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Numbered packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Packets of 188 or 204 bytes that say which they are, for a receiver to check one by
// one: on PID 0x0100, with the continuity counter counting, the packet's number as eight
// bytes from offset 4, most significant first, and from offset 12 to the end of the
// packet, the 16 bytes of a 204-byte packet included, each byte the number plus its
// offset.
//

// Writes packet Number of Size bytes, 188 or 204, into Packet.
void ExampleTs_Numbered(uint64_t Number, int Size, uint8_t* Packet);

// True when Packet, of Size bytes, is a numbered packet; *Number is then its number.
bool ExampleTs_IsNumbered(const uint8_t* Packet, int Size, uint64_t* Number);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The service +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A transport stream of 188-byte packets at a constant rate, with one service, "CDTAPI
// ASI test": a PAT, a PMT and an SDT, and MPEG-2 video of 720 x 576 progressive at 25
// frames a second. Every frame shows colour bars, "CDTAPI", the time code of the frame
// and a block that bounces from side to side, so that a player shows at a glance whether
// frames were lost. Each 16 x 16 macroblock is one flat colour, which makes a frame an
// intra picture of DC coefficients only and lets it be coded without a DCT.
//
// Every frame is a closed group of pictures with a sequence header, so that a player can
// start anywhere. The video PID carries the PCR, in the first packet of each frame; the
// rest of a frame period is filled with null packets. The same bit rate and packet number
// always give the same packet.
//

// The lowest rate the stream fits in, in bits a second, and the highest, about what ASI
// carries.
#define EXAMPLE_TS_MIN_RATE 3000000
#define EXAMPLE_TS_MAX_RATE 210000000

typedef struct ExampleTsStream
{
    int64_t Rate;      // Bits a second of 188-byte packets
    uint64_t Packet;   // Number of the next packet, from 0
    int64_t Frame;     // Number of the frame being sent
    uint64_t FrameEnd; // First packet of the next frame
    uint8_t* Es;       // The frame's PES packet
    int EsCapacity;
    int EsSize;
    int EsSent;
    int PsiSent;   // Tables sent in this frame: PAT, PMT, SDT
    uint8_t Cc[4]; // Continuity counters: PAT, PMT, SDT, video
} ExampleTsStream;

// Prepares a stream at Rate bits a second. False for a rate outside the limits above, or
// when memory runs out.
bool ExampleTsStream_Init(ExampleTsStream* Stream, int64_t Rate);

// Frees what Init allocated.
void ExampleTsStream_Close(ExampleTsStream* Stream);

// Writes the next 188-byte packet into Packet.
void ExampleTsStream_Next(ExampleTsStream* Stream, uint8_t* Packet);

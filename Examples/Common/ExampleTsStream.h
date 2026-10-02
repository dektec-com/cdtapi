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
// Transport stream packets that carry their own number, so that a receiver can check
// that none is lost or changed. A numbered packet of 188 or 204 bytes is on PID 0x0100,
// with a continuity counter that counts. From offset 4 it holds its number in eight
// bytes, most significant first. Every byte from offset 12 to the end of the packet
// (also the 16 extra bytes of a 204-byte packet) is the number plus its offset.
//

// Writes packet Number of Size bytes, 188 or 204, into Packet.
void ExampleTs_Numbered(uint64_t Number, int Size, uint8_t* Packet);

// Returns whether Packet, of Size bytes, is an intact numbered packet, and if so sets
// *Number to its number.
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

// The lowest and highest rate of the stream, in bits per second. The highest is about
// what ASI carries.
#define EXAMPLE_TS_MIN_RATE 3000000
#define EXAMPLE_TS_MAX_RATE 210000000

// The state of the test stream. ExampleTsStream_Init() sets it up; the program only
// reads Rate and Packet.
typedef struct ExampleTsStream
{
    int64_t Rate;      // Bits per second of 188-byte packets
    uint64_t Packet;   // Number of the next packet, from 0
    int64_t Frame;     // Number of the frame being sent
    uint64_t FrameEnd; // Number of the first packet of the next frame
    uint8_t* Es;       // The PES packet of the frame: its coded picture
    int EsCapacity;    // Bytes Es has room for
    int EsSize;        // Bytes of Es in use
    int EsSent;        // Bytes of Es sent so far
    int PsiSent;       // Tables sent in this frame: PAT, PMT, SDT, in that order
    uint8_t Cc[4];     // Continuity counters: PAT, PMT, SDT, video
} ExampleTsStream;

// Sets up Stream to run at Rate bits per second. Returns false for a rate outside
// EXAMPLE_TS_MIN_RATE and EXAMPLE_TS_MAX_RATE, or when there is not enough memory.
bool ExampleTsStream_Init(ExampleTsStream* Stream, int64_t Rate);

// Frees the memory ExampleTsStream_Init() took.
void ExampleTsStream_Close(ExampleTsStream* Stream);

// Writes the next 188-byte packet into Packet.
void ExampleTsStream_Next(ExampleTsStream* Stream, uint8_t* Packet);

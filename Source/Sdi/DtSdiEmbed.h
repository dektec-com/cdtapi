// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiEmbed.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Embedding audio in SDI frames: the builder's audio
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtSdiGeometry.h" // The frame's lines and streams.
#include "cdtapi_sdi.h"    // The audio.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Embedding audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The builder embeds audio as DTAPI's matrix does, so that a receiver sees the same
// packets from either. SMPTE ST 299-1 (HD and up) puts a data packet of one sample of a
// group of four channels in the C stream, the groups taking turns, and a control packet
// per group in the Y stream, two lines after each switching line; in 2160p they go on
// link 1. SMPTE ST 272 (SD) puts up to four samples of a group in a packet, without
// control packets. No line after a switching line carries audio.
//
// A sample goes on the first line after the one its moment falls in, as a clock running
// at the line's sample rate and stepping 48 kHz on tells, no more than a frame's share
// per line; the clock and the rounding are the matrix's, so that the clock phase words
// are its too. A frame of a 1001 rate takes its number of samples from the cadence.
//
// The work goes in steps, so that a refusal changes nothing: DtSdiEmbed_Begin checks
// the program's audio and plans the frame; once every check has passed,
// DtSdiEmbed_Start takes up the state that runs on from frame to frame,
// DtSdiEmbed_Put writes each line's packets, and DtSdiEmbed_End moves the cadence on.
//

// The longest audio cadence: five frames at 29.97 and 59.94 Hz.
#define DT_SDIEMBED_MAX_CADENCE 5

// The most lines of a frame, and the most samples of a channel a frame takes.
#define DT_SDIEMBED_MAX_LINES 1125
#define DT_SDIEMBED_MAX_SAMPLES 2048

// What a channel carries in the frame being built.
typedef enum DtSdiEmbedSource
{
    DT_SDIEMBED_NONE, // Nothing: its group carries no audio
    DT_SDIEMBED_PCM,  // The program's PCM samples
    DT_SDIEMBED_AES3, // The program's AES3 subframes
    DT_SDIEMBED_MUTE, // Silence marked not valid, for a channel its group lacks
} DtSdiEmbedSource;

// The audio state of a builder.
typedef struct DtSdiEmbed
{
    // Of the standard of the frame being built.
    int VidStd;          // The DTAPI_VIDSTD_ code
    bool Sd;             // SMPTE ST 272 rather than ST 299-1
    int NumLines;        // Lines of a frame, of one link in 2160p
    int TicksPerLine;    // The clock's ticks per line
    double Increment;    // The clock's ticks per audio sample
    double PhaseAtStart; // The clock at the cadence's first sample
    int CadenceLength;   // Frames in the cadence, 1 at most rates
    int SamplesInFrame[DT_SDIEMBED_MAX_CADENCE]; // Per place in the cadence
    int MaxPerLine[DT_SDIEMBED_MAX_CADENCE];     // Per place: the most a line takes
    DtFrameProps Props;                          // The fields and their switching lines
    uint8_t Status[2][24]; // Channel status: of PCM, of a mute channel

    // What runs on from frame to frame, of the standard of StateVidStd.
    int StateVidStd;                          // 0 before the first frame with audio
    int NextFrameNumber;                      // The cadence's next place, from 1
    uint8_t Dbn[4];                           // Per group: the next data block number
    int StatusBit[DT_SDI_AUDIO_MAX_CHANNELS]; // Per channel: its place in the AES3 block

    // Of the frame being built.
    int FrameNumber;    // Its place in the cadence, from 1
    int NumSamples;     // Samples per channel it takes
    bool HasAudio;      // Any group carries audio
    bool Group[4];      // The group carries audio
    unsigned Active[4]; // Per group, a bit per channel the program sends
    DtSdiEmbedSource Source[DT_SDI_AUDIO_MAX_CHANNELS];
    const uint8_t* Samples[DT_SDI_AUDIO_MAX_CHANNELS]; // The program's samples
    size_t Stride[DT_SDI_AUDIO_MAX_CHANNELS];          // In bytes
    uint8_t Count[DT_SDIEMBED_MAX_LINES];    // Per line: samples per group it carries
    uint16_t Clock[DT_SDIEMBED_MAX_SAMPLES]; // Per sample: its clock phase, bit 13 MPF
    int Next;                                // The next sample to write
} DtSdiEmbed;

// Prepares Embed for the frames of Geo, before DtSdiEmbed_Begin: the clock and the
// cadence of the standard. The state that runs on from frame to frame starts afresh when
// the first frame of another standard is built.
void DtSdiEmbed_Init(DtSdiEmbed* Embed, const DtSdiGeometry* Geo);

// Returns in *NumSamples the samples per channel the next frame takes at place
// FrameNumber in the cadence, or at the cadence's own next place for 0. Embed must have
// had DtSdiEmbed_Init.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_ARG for a FrameNumber that is no place.
DtapiResult DtSdiEmbed_NumSamples(const DtSdiEmbed* Embed, int FrameNumber,
                                  int* NumSamples);

// Checks the program's audio for the next frame and plans where its samples go. Audio
// may be NULL, for a frame without audio. Sets Audio->NumSamplesUsed to the samples per
// channel the frame takes, or 0 when the program sends no channel.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_FORMAT  a pair's format is not one of the enum
//   DTAPI_E_INVALID_ARG     a negative stride, or a FrameNumber that is no place
//   DTAPI_E_BUF_TOO_SMALL   a channel offers fewer samples than the frame takes
DtapiResult DtSdiEmbed_Begin(DtSdiEmbed* Embed, DtSdiAudio* Audio);

// Returns the words the audio packets take in the horizontal blanking of stream Stream
// of line LineIndex.
int DtSdiEmbed_HancWords(const DtSdiEmbed* Embed, const DtSdiGeometry* Geo, int LineIndex,
                         int Stream);

// Writes the audio packets of stream Stream of line LineIndex into Words from Pos on,
// with their checksums worked out when Checksum is true. Lines go in order. Returns the
// index of the word after them.
int DtSdiEmbed_Put(DtSdiEmbed* Embed, const DtSdiGeometry* Geo, int LineIndex, int Stream,
                   uint16_t* Words, int Pos, bool Checksum);

// Starts writing the frame planned: the data block numbers and the places in the AES3
// blocks start afresh when the frame's standard is not that of the frame before.
void DtSdiEmbed_Start(DtSdiEmbed* Embed);

// Ends a frame that was built: the cadence moves on to its next place.
void DtSdiEmbed_End(DtSdiEmbed* Embed);

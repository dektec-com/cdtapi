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
#include "DtSdiAudio.h"    // The channels carried.
#include "DtSdiGeometry.h" // The frame's lines and streams.
#include "cdtapi_sdi.h"    // The audio.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Embedding audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The builder embeds audio in the same packets as the DekTec matrix API, so a receiver
// sees the same packets from either.
//
// In HD and up, SMPTE ST 299-1 applies. Each data packet carries one sample of a group
// of four channels and goes in the C stream. The groups take turns. Each group also has
// a control packet in the Y stream, two lines after each switching line. In 2160p these
// packets go on link 1. In SD, SMPTE ST 272 applies. A data packet carries up to four
// samples of a group, and there are no control packets. In both, the line after a
// switching line carries no audio.
//
// A clock decides which line each sample goes on. The clock ticks at the line's sample
// rate and steps on by one 48 kHz sample at a time. A sample goes on the first line
// after the line its moment falls in. A line carries no more than its share of the
// frame's samples. The clock and its rounding are chosen so that the clock phase words
// come out the same as well. At a 1001 frame rate, the number of samples in a frame
// comes from the cadence.
//
// In 3G level B the audio goes on link A of the interface, whose clock runs at
// 74.25 MHz (SMPTE ST 372 6.4). So the plan is made for a frame of the interface, the
// 1080i standard of the same rate, with its lines, switching lines and cadence. A
// picture carries the samples on the interface lines of its field, which are the lines
// of link A in the picture. The cadence then counts pictures: two places for each place
// of the interface's cadence, field 1 on the odd places and field 2 on the even ones.
//
// The work goes in steps, so that a refused frame changes nothing:
// 1. DtSdiEmbed_Begin checks the program's audio and plans the frame.
// 2. Once every check of the frame has passed, DtSdiEmbed_Start takes up the state that
//    carries on from frame to frame.
// 3. DtSdiEmbed_Put writes the packets of each line.
// 4. DtSdiEmbed_End moves the state and the cadence on.
//
// DtSdiEmbed_Put writes through a cursor. DtSdiEmbed_CursorAt sets a cursor for any
// line from the plan, so bands of lines can be built at the same time.
//

// The longest audio cadence: five frames at 29.97 and 59.94 Hz.
#define DT_SDIEMBED_MAX_CADENCE 5

// The most lines a frame has, and the most samples per channel a frame carries.
#define DT_SDIEMBED_MAX_LINES 1125
#define DT_SDIEMBED_MAX_SAMPLES 2048

// What a channel carries in the frame being built.
typedef enum DtSdiEmbedSource
{
    DT_SDIEMBED_NONE, // Nothing, because its group carries no audio
    DT_SDIEMBED_PCM,  // The program's PCM samples
    DT_SDIEMBED_AES3, // The program's AES3 subframes
    DT_SDIEMBED_MUTE, // Silence marked not valid, for a channel the program does not send
                      // in a group that carries audio
} DtSdiEmbedSource;

// The audio state of a builder.
typedef struct DtSdiEmbed
{
    // Set by DtSdiEmbed_Init for the standard of the frame being built. In 3G level B
    // the clock, the lines and the frames are those of the interface.
    int VidStd;          // The standard, as a DTAPI_VIDSTD_ code
    bool Sd;             // True for SD, which uses SMPTE ST 272 rather than ST 299-1
    bool LevelB;         // True for 3G level B
    int NumLines;        // The lines of a frame; in 2160p, those of one link
    int TicksPerLine;    // The clock ticks in one line
    double Increment;    // The clock ticks from one audio sample to the next
    double PhaseAtStart; // The clock's value at the cadence's first sample
    int FrameCadence;    // The number of frames in the cadence; 1 at most rates
    int CadenceLength;   // The places in the cadence: FrameCadence, or twice that in 3G
                         // level B, whose places are pictures
    // Per frame of the cadence: the samples per channel of that frame.
    int SamplesInFrame[DT_SDIEMBED_MAX_CADENCE];
    // Per frame of the cadence: the most samples one line carries.
    int MaxPerLine[DT_SDIEMBED_MAX_CADENCE];
    // 3G level B: per place in the cadence, the samples per channel of that picture.
    int SamplesInPicture[2 * DT_SDIEMBED_MAX_CADENCE];
    DtFrameProps Props;    // The fields and their switching lines
    uint8_t Status[2][24]; // Channel status blocks: [0] for PCM, [1] for mute

    // The state that carries on from frame to frame. It belongs to standard StateVidStd
    // and holds the values at the start of the frame being built.
    int StateVidStd;     // This state's standard; 0 before the first frame is built
    int NextFrameNumber; // The cadence's next place, counted from 1
    uint8_t Dbn[4];      // Per group: the next data block number
    int StatusBit[DT_SDIAUDIO_CHANNELS]; // Per channel: its place in its AES3 block

    // Set by DtSdiEmbed_Begin for the frame being built.
    int FrameNumber;    // The frame's place in the cadence, counted from 1
    int PlanFrame;      // The frame of the cadence the plan is for, counted from 1: the
                        // place, or in 3G level B the interface frame of the picture
    int FirstLine;      // The first line of the plan the frame takes, counted from 1
    int EndLine;        // The line after the last one of the plan the frame takes
    int SampleBase;     // The index in the plan of the frame's first sample
    int Field;          // 3G level B: the picture's field, 1 or 2; else 0
    int NumSamples;     // The samples per channel the frame carries
    bool HasAudio;      // True when any group carries audio
    bool Group[4];      // Per group: true when the group carries audio
    unsigned Active[4]; // Per group: a bit for each channel the program sends
    DtSdiEmbedSource Source[DT_SDIAUDIO_CHANNELS]; // Per channel: what it carries
    const uint8_t* Samples[DT_SDIAUDIO_CHANNELS];  // Per channel: its samples
    size_t Stride[DT_SDIAUDIO_CHANNELS];           // Per channel: sample step, bytes
    uint8_t Count[DT_SDIEMBED_MAX_LINES];    // Per line: the samples each group carries
    uint16_t Clock[DT_SDIEMBED_MAX_SAMPLES]; // Per sample: clock word, MPF in bit 13

    // Per line: the samples a group carries on the lines before it.
    int SamplesBefore[DT_SDIEMBED_MAX_LINES + 1];
    // Per line, used in SD: the packets of up to four samples a group carries on the
    // lines before it.
    int PacketsBefore[DT_SDIEMBED_MAX_LINES + 1];
} DtSdiEmbed;

// The position reached in writing a frame's audio, at the start of a line.
typedef struct DtSdiEmbedCursor
{
    int Next;                            // The index of the next sample
    uint8_t Dbn[4];                      // Per group: the next data block number
    int StatusBit[DT_SDIAUDIO_CHANNELS]; // Per channel: its place in its AES3 block
} DtSdiEmbedCursor;

// Prepares Embed for the frames of Geo's standard. Works out the clock and the cadence
// of the standard. Call it before DtSdiEmbed_Begin. The state that carries on from frame
// to frame starts afresh when the first frame of another standard is built.
void DtSdiEmbed_Init(DtSdiEmbed* Embed, const DtSdiGeometry* Geo);

// Gets the number of samples per channel that a frame takes at place FrameNumber in the
// cadence, in *NumSamples. FrameNumber 0 stands for the cadence's next place. Embed must
// have had DtSdiEmbed_Init.
//
// Returns DTAPI_OK, or DTAPI_E_INVALID_ARG for a FrameNumber that is not a place in the
// cadence.
DtapiResult DtSdiEmbed_NumSamples(const DtSdiEmbed* Embed, int FrameNumber,
                                  int* NumSamples);

// Checks the program's audio for the next frame and plans where its samples go. Audio
// may be NULL for a frame without audio. Sets Audio->NumSamplesUsed to the samples per
// channel the frame takes, or to 0 when the program sends no channel.
//
// In 3G level B, Field is the picture's field, 1 or 2, and decides the place in the
// cadence: a place of the other field gives way to the next place, of this field. For
// any other standard Field is 0.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_FORMAT  a pair's format is not one of the enum
//   DTAPI_E_INVALID_ARG     a negative stride, or a FrameNumber that is not a place in
//                           the cadence
//   DTAPI_E_BUF_TOO_SMALL   a channel offers fewer samples than the frame takes
DtapiResult DtSdiEmbed_Begin(DtSdiEmbed* Embed, DtSdiAudio* Audio, int Field);

// Sets *Cursor to the position that writing the planned frame has reached at the start
// of line LineIndex. A LineIndex equal to the frame's number of lines gives the end of
// the frame.
void DtSdiEmbed_CursorAt(const DtSdiEmbed* Embed, int LineIndex,
                         DtSdiEmbedCursor* Cursor);

// Returns the number of words the audio packets take in the horizontal blanking of
// stream Stream on line LineIndex.
int DtSdiEmbed_HancWords(const DtSdiEmbed* Embed, const DtSdiGeometry* Geo, int LineIndex,
                         int Stream);

// Writes the audio packets of stream Stream on line LineIndex into Words, starting at
// index Pos. Takes the samples from *Cursor on, and moves *Cursor past them. Works out
// the packets' checksums when Checksum is true. Call it for the streams of a line in
// order, and for the lines that one cursor writes in order. Returns the index of the word
// after the last packet.
int DtSdiEmbed_Put(const DtSdiEmbed* Embed, DtSdiEmbedCursor* Cursor, int LineIndex,
                   int Stream, uint16_t* Words, int Pos, bool Checksum);

// Starts writing the planned frame. When the frame's standard differs from that of the
// frame before, the data block numbers and the places in the AES3 blocks start afresh.
void DtSdiEmbed_Start(DtSdiEmbed* Embed);

// Ends a frame that was built. Moves the data block numbers and the places in the AES3
// blocks on to where the frame ends, and moves the cadence on to its next place.
void DtSdiEmbed_End(DtSdiEmbed* Embed);

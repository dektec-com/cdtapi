// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleAvFifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the SMPTE ST 2110 examples share: the header, the stream and frames
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Example includes
#include "Common/ExampleCommon.h"  // The API and what every example shares.
#include "Common/ExamplePattern.h" // The test pattern and the test tone.

// The AV FIFO.
#include "cdtapi_avfifo.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The stream +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The help text of the --pipe option.
#define EXAMPLE_PIPE_HELP "auto, hw, sw or prefer; auto without it"

// The stream an example sends or receives, as the command line describes it.
typedef struct ExampleAvConfig
{
    uint8_t Ip[4];       // Destination or group address
    int UdpPort;         // Destination UDP port
    int Width;           // Video: pixels per row
    int Height;          // Video: rows per frame
    int Rate;            // Video: frames per second, a whole number
    bool EightBit;       // Video: 8-bit rather than 10-bit samples
    int Channels;        // Audio: channels of L24; 0 for video
    int SampleRate;      // Audio: samples per second
    int SamplesPerFrame; // Audio: samples per channel in one frame
    HwOrSwPipe Pipe;     // Which pipe the FIFO uses
} ExampleAvConfig;

// Fills *Config from the command line options --ip, --udp, --width, --height, --rate,
// --8bit, --audio and --pipe, with defaults for those not given. The width is rounded
// down to an even number, a whole number of pixel groups. Returns false, after printing
// what is wrong, for a value that is not valid or out of range.
bool ExampleAv_Config(int Argc, char** Argv, ExampleAvConfig* Config);

// Returns whether a port has an AV FIFO, for Example_FindPort().
bool ExampleAv_IsIpPort(const DtHwFuncDesc* Port);

// Fills *Pars with the IP parameters of the stream: its destination and port, and
// IPv4, RTP over UDP, a time to live of 32 and DiffServ 0x88.
void ExampleAv_IpPars(const ExampleAvConfig* Config, AvFifo_IpPars* Pars);

// Prints a line that describes the stream: "<serial>:<port>  <pipe>  <address>:<port>
// <format>". For a receiver, pass the name of its frame format in RxFormat; the format
// then reads "video as <RxFormat>", as the receiver learns the rest from the stream. For
// a sender, pass NULL; the format then gives the size, rate and bits of Config.
void ExampleAv_PrintStream(const DtHwFuncDesc* Port, const char* Pipe,
                           const ExampleAvConfig* Config, const char* RxFormat);

// Prints the failed call as Example_Failed() does, then the text of GetLastException(),
// and returns EXAMPLE_FAILED.
int ExampleAv_Failed(const char* What, unsigned int Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Return the bytes of one row of video, and of one frame of video or audio.
int ExampleAv_RowBytes(const ExampleAvConfig* Config);
int ExampleAv_FrameBytes(const ExampleAvConfig* Config);

// Returns the time between two frames, in nanoseconds.
int64_t ExampleAv_PeriodNs(const ExampleAvConfig* Config);

// Convert a time of day to nanoseconds since the epoch, and back.
int64_t ExampleAv_ToNs(const DtTimeOfDay* ToD);
DtTimeOfDay ExampleAv_FromNs(int64_t Ns);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The examples' test pattern as SMPTE ST 2110-20 video, or their test tone as ST 2110-30
// audio, in the format of a stream.
typedef struct ExampleAvSource
{
    const ExampleAvConfig* Config; // The stream
    ExamplePattern Pattern;        // Video: the image of the frame being made
    uint8_t* Background;           // Video: the first image of the pattern, packed
    ExampleTone Tone;              // Audio: where the tone is
    int32_t* Samples;              // Audio: one frame's samples of the tone
} ExampleAvSource;

// Sets up *Src for the stream of Config, which must stay valid as long as *Src is used.
// Returns false, after printing why, when the pattern cannot be drawn in the stream's
// image size, which must be at least 320 by 240 pixels, or when there is not enough
// memory; *Src then holds nothing to free.
bool ExampleAv_SourceInit(ExampleAvSource* Src, const ExampleAvConfig* Config);

// Frees what ExampleAv_SourceInit allocated. A zeroed *Src is freed too.
void ExampleAv_SourceFree(ExampleAvSource* Src);

// Writes frame Number of the stream to Data, of ExampleAv_FrameBytes() bytes. Video: the
// packed first image, with the parts that changed packed again over it: the moving bar,
// and the box with the frame number. Audio: the next samples of the tone, the same on
// every channel, as 24-bit samples, most significant byte first.
void ExampleAv_SourceFrame(ExampleAvSource* Src, int64_t Number, uint8_t* Data);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Attach a FIFO to port Port (from 1) of Device, with the pipe Config asks for.
unsigned int ExampleAv_AttachTx(AvFifo_TxFifo* Fifo, const DtDevice* Device, int Port,
                                const ExampleAvConfig* Config);
unsigned int ExampleAv_AttachRx(AvFifo_RxFifo* Fifo, const DtDevice* Device, int Port,
                                const ExampleAvConfig* Config);

// Return "hardware pipe" or "software pipe" for the pipe the FIFO uses, or "pipe" when
// it does not say.
const char* ExampleAv_TxPipeKind(const AvFifo_TxFifo* Fifo);
const char* ExampleAv_RxPipeKind(const AvFifo_RxFifo* Fifo);

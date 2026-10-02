// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleAvFifo.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the SMPTE ST 2110 examples share: the header, the stream and frames
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Example includes
#include "Common/ExampleCommon.h" // The API and what every example shares.

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

// Writes pixel group Index (two pixels with the same luma) into a row of video, in the
// sample size of Config.
void ExampleAv_WritePgroup(const ExampleAvConfig* Config, uint8_t* Row, int Index,
                           int Blue, int Luma, int Red);

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

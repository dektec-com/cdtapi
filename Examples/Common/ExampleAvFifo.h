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

// The help of --pipe.
#define EXAMPLE_PIPE_HELP "auto, hw, sw or prefer; auto without it"

// What the examples send or receive, from the command line.
typedef struct ExampleAvConfig
{
    uint8_t Ip[4];       // Destination or group address
    int UdpPort;         // Destination UDP port
    int Width;           // Video: pixels a row
    int Height;          // Video: rows a frame
    int Rate;            // Video: whole frames a second
    bool EightBit;       // Video: 8-bit rather than 10-bit samples
    int Channels;        // Audio when above 0: channels of L24
    int SampleRate;      // Audio: samples a second
    int SamplesPerFrame; // Audio: samples of every channel in one frame
    HwOrSwPipe Pipe;     // Which pipe to use
} ExampleAvConfig;

// Fills *Config from the command line. Prints what is wrong and returns false for a
// value that is no address, number or pipe.
bool ExampleAv_Config(int Argc, char** Argv, ExampleAvConfig* Config);

// True for a port with an AV FIFO.
bool ExampleAv_IsIpPort(const DtHwFuncDesc* Port);

// The IP parameters of the stream: the destination, the payload type and the rest as
// DTAPI's defaults have them.
void ExampleAv_IpPars(const ExampleAvConfig* Config, AvFifo_IpPars* Pars);

// Prints "<serial>:<port>  <pipe>  <address>:<port>  <format>".
void ExampleAv_PrintStream(const DtHwFuncDesc* Port, const char* Pipe,
                           const ExampleAvConfig* Config);

// As Example_Failed, and then the failure's text from GetLastException.
int ExampleAv_Failed(const char* What, unsigned int Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The bytes of one row of video, of a whole frame of video, or of one frame of audio.
int ExampleAv_RowBytes(const ExampleAvConfig* Config);
int ExampleAv_FrameBytes(const ExampleAvConfig* Config);

// The nanoseconds between two frames.
int64_t ExampleAv_PeriodNs(const ExampleAvConfig* Config);

// A time of day as nanoseconds since the epoch, and the other way about.
int64_t ExampleAv_ToNs(const DtTimeOfDay* ToD);
DtTimeOfDay ExampleAv_FromNs(int64_t Ns);

// Writes one pixel group, two pixels of the same luma, into a row of video.
void ExampleAv_WritePgroup(const ExampleAvConfig* Config, uint8_t* Row, int Index,
                           int Blue, int Luma, int Red);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Attaches the FIFO to the port, counted from 0, with the pipe the configuration asks
// for.
unsigned int ExampleAv_AttachTx(AvFifo_TxFifo* Fifo, const DtDevice* Device, int Port,
                                const ExampleAvConfig* Config);
unsigned int ExampleAv_AttachRx(AvFifo_RxFifo* Fifo, const DtDevice* Device, int Port,
                                const ExampleAvConfig* Config);

// "hardware pipe" or "software pipe"; "pipe" where the FIFO does not tell.
const char* ExampleAv_TxPipeKind(const AvFifo_TxFifo* Fifo);
const char* ExampleAv_RxPipeKind(const AvFifo_RxFifo* Fifo);

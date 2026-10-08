// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiAudio.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The embedded audio of SDI frames: what the library uses besides the API
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

// CDTAPI includes
#include "DtSdiAnc.h"   // The packets audio comes in.
#include "cdtapi_sdi.h" // The audio.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Taking audio apart +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The parser passes each audio packet it finds to DtSdiAudio_TakeHd() or
// DtSdiAudio_TakeSd(), in the frame's order. These write the samples into the program's
// buffers as they come. So the parser reads a frame's audio in the same pass as its
// ancillary data.
//

// Checks that Audio can take the audio of a frame of video standard VidStd. The format
// of each pair must be known. Each channel the program wants must have room for the
// most samples that a frame of the standard holds.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_FORMAT  a pair's format is not one of the enum
//   DTAPI_E_INVALID_ARG     a channel's stride is negative
//   DTAPI_E_BUF_TOO_SMALL   a channel's MaxSamples is below DtSdiAudio_MaxSamples()
DtapiResult DtSdiAudio_Check(const DtSdiAudio* Audio, int VidStd);

// Returns the number of frames in the audio cadence of video standard VidStd. This is 5
// at 29.97 and 59.94 Hz. It is 1 at a rate where every frame holds the same number of
// samples.
int DtSdiAudio_CadenceLength(int VidStd);

// Clears the fields of Audio that the parser sets, ready for a new frame.
void DtSdiAudio_Begin(DtSdiAudio* Audio);

// Works out the six BCH words of an HD data packet, as SMPTE ST 299-1 defines them.
// Words[0] is the packet's first ADF word. The code covers the lower bytes of the flag,
// the IDs, the data count and the first 18 user data words. Returns the six words as
// one byte each, the first word in the lowest bits.
uint64_t DtSdiAudio_HdBch(const uint16_t* Words);

// Takes the content of an audio packet of SMPTE ST 299-1 (HD and up). From a data
// packet it takes the four samples. From a control packet it takes the frame number,
// unless an earlier packet already gave one other than 0. When Check is true, it checks
// a data packet's BCH code and checksum. The packet's first ADF word must be
// Found->Words - 6.
void DtSdiAudio_TakeHd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check);

// Takes the content of an audio packet of SMPTE ST 272 (SD). From a data packet it
// takes the subframes. From a control packet it takes the frame number, unless an
// earlier packet already gave one other than 0. When Check is true, it checks the
// checksum.
void DtSdiAudio_TakeSd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check);

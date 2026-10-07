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
// The parser gives each audio packet it finds to one of these, in the frame's order. The
// samples go into the program's buffers as they come, so a frame's audio is read in one
// walk with its ancillary data.
//

// Checks that Audio can take the audio of a frame of VidStd: the format of each pair is
// known, and each channel the program wants has room for the most samples a frame of the
// standard holds.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_FORMAT  a pair's format is not one of the enum
//   DTAPI_E_INVALID_ARG     a channel's stride is negative
//   DTAPI_E_BUF_TOO_SMALL   a channel's MaxSamples is below DtSdiAudio_MaxSamples()
DtapiResult DtSdiAudio_Check(const DtSdiAudio* Audio, int VidStd);

// Clears what the parser sets in Audio, for a new frame.
void DtSdiAudio_Begin(DtSdiAudio* Audio);

// Takes an audio packet of SMPTE ST 299-1 (HD and up): a data packet's four samples, or
// a control packet's frame number. Checks the data packet's BCH code and checksum when
// Check is true; Found->Words - 6 is its first ADF word.
void DtSdiAudio_TakeHd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check);

// Takes an audio packet of SMPTE ST 272 (SD): a data packet's subframes, or a control
// packet's frame number. Checks the checksum when Check is true.
void DtSdiAudio_TakeSd(DtSdiAudio* Audio, const DtSdiAncFound* Found, bool Check);

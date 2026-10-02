// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvTime.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Times of day on the media clock, and RTP timestamps
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtTimeOfDay.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Arithmetic +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A time of day here is a number of nanoseconds since the epoch. Converting it to and
// from other clocks needs a product of two 64-bit numbers divided by a third, which
// overflows 64 bits. MSVC has no 128-bit integer, so DtAvTime_MulAddDiv computes it by
// hand.
//

#define DT_AV_NS_PER_SEC UINT64_C(1000000000)
#define DT_AV_PS_PER_NS 1000

// The RTP clock rate of video, in Hz.
#define DT_AV_VIDEO_RTP_RATE 90000

// Returns (A * B + Add) / Div, computed in 128 bits, truncated to its low 64 bits. Div
// must not be 0.
uint64_t DtAvTime_MulAddDiv(uint64_t A, uint64_t B, uint64_t Add, uint64_t Div);

// Convert between a DtTimeOfDay and nanoseconds. FromNs keeps only the low 32 bits of the
// seconds.
uint64_t DtAvTime_ToNs(const DtTimeOfDay* ToD);
DtTimeOfDay DtAvTime_FromNs(uint64_t Ns);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// These convert between a time of day and the media clock of a stream. A rate that is 0
// or negative returns the time unchanged, or an RTP timestamp of 0.
//

// Returns the start of the period nearest to Ns, on a grid of Numerator / Denominator
// periods per second (a frame rate, for example), rounded to a nanosecond.
uint64_t DtAvTime_ToGrid(uint64_t Ns, int Numerator, int Denominator);

// Returns the RTP timestamp of Ns on a clock of RtpRate Hz. At 90 kHz an eighth of a tick
// is added before truncating, so that a frame time on a fractional frame rate's grid
// gets the right timestamp despite its rounding; at other rates the timestamp is rounded.
uint32_t DtAvTime_Tod2Rtp(int RtpRate, uint64_t Ns);

// Returns the time of day of RTP timestamp RtpTime on a clock of RtpRate Hz. A timestamp
// wraps around every 2^32 ticks, so the time chosen is the one that lies within half a
// wrap of ApproxNs.
uint64_t DtAvTime_Rtp2Tod(int RtpRate, uint32_t RtpTime, uint64_t ApproxNs);

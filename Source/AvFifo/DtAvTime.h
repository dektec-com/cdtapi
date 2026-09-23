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
// A time of day here is a count of nanoseconds since the epoch. The conversions compute
// in 128 bits: MSVC has no 128-bit integer, so the one step that needs it, a product of
// two 64-bit numbers divided by a third, is done by hand.
//

#define DT_AV_NS_PER_SEC UINT64_C(1000000000)

// The RTP clock rate of video.
#define DT_AV_VIDEO_RTP_RATE 90000

// (A * B + Add) / Div, the product and sum taken in 128 bits; the low 64 bits of the
// quotient. Div must not be 0.
uint64_t DtAvTime_MulAddDiv(uint64_t A, uint64_t B, uint64_t Add, uint64_t Div);

// A DtTimeOfDay as nanoseconds, and nanoseconds as a DtTimeOfDay, whose seconds keep
// their low 32 bits.
uint64_t DtAvTime_ToNs(const DtTimeOfDay* ToD);
DtTimeOfDay DtAvTime_FromNs(uint64_t Ns);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A rate that is not positive leaves a time unchanged and gives an RTP timestamp of 0.
//

// The time on the grid of Numerator / Denominator periods per second nearest to Ns,
// rounded to a nanosecond.
uint64_t DtAvTime_Align(uint64_t Ns, int Numerator, int Denominator);

// The RTP timestamp of Ns on a clock of RtpRate Hz: truncated after adding an eighth of a
// tick at 90 kHz and half a tick at any other rate.
uint32_t DtAvTime_Tod2Rtp(int RtpRate, uint64_t Ns);

// The time of RtpTime on a clock of RtpRate Hz in the 32-bit wrap of the timestamp that
// lies within half a wrap of ApproxNs.
uint64_t DtAvTime_Rtp2Tod(int RtpRate, uint32_t RtpTime, uint64_t ApproxNs);

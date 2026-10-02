// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* LinTime.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Deadline arithmetic for timed waits on Linux
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Deadline +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Computes the deadline for a timed wait. pthread_cond_timedwait takes the time at which
// to stop waiting, in seconds and nanoseconds, so a timeout is added to the current
// time. The nanoseconds must stay below one billion. Otherwise the call fails with
// EINVAL, and the wait returns at once instead of waiting.
//
// The carry from nanoseconds into seconds is easy to get wrong, so it is here, without
// Linux headers, where the tests check it on every platform.
//

// The number of nanoseconds in a second.
#define LIN_NSEC_PER_SEC 1000000000L

// Adds Ms milliseconds to the time Sec seconds plus Nsec nanoseconds, and writes the
// result to *OutSec and *OutNsec, with 0 <= *OutNsec < LIN_NSEC_PER_SEC. Nsec must
// already be in that range. A negative Ms counts as zero.
void LinTime_AddMs(int64_t Sec, long Nsec, int Ms, int64_t* OutSec, long* OutNsec);

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
// pthread_cond_timedwait takes an absolute deadline as seconds and nanoseconds, so a
// relative timeout has to be added to the current time. The nanosecond field must stay
// below one billion; a value at or above it makes the call fail with EINVAL, and the
// wait then returns immediately instead of waiting.
//
// That carry is the whole difficulty, and it is kept here, free of Linux headers, so
// that it is tested on every platform.
//

#define LIN_NSEC_PER_SEC 1000000000L

// Adds Ms milliseconds to the time (Sec, Nsec) and writes the normalised result, with
// 0 <= *OutNsec < LIN_NSEC_PER_SEC. Nsec is expected to be normalised already. A
// negative Ms is treated as zero.
void LinTime_AddMs(int64_t Sec, long Nsec, int Ms, int64_t* OutSec, long* OutNsec);

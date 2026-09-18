// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* LinTime.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Deadline arithmetic for timed waits on Linux - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "LinTime.h" // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinTime_AddMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void LinTime_AddMs(int64_t Sec, long Nsec, int Ms, int64_t* OutSec, long* OutNsec)
{
    if (Ms < 0)
        Ms = 0;

    // Whole seconds are split off before converting the rest, so that the nanosecond sum
    // stays small: at most 999,999,999 plus 999,000,000, which fits comfortably in 64
    // bits and in a 32-bit long after the carry.
    Sec += Ms / 1000;
    int64_t TotalNsec = (int64_t)Nsec + (int64_t)(Ms % 1000) * 1000000;

    if (TotalNsec >= LIN_NSEC_PER_SEC)
    {
        Sec += 1;
        TotalNsec -= LIN_NSEC_PER_SEC;
    }

    *OutSec = Sec;
    *OutNsec = (long)TotalNsec;
}

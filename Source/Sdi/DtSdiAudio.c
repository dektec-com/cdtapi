// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiAudio.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The embedded audio of SDI frames: how many samples a frame holds
//
// SPDX-License-Identifier: BSD-3-Clause
//
// For now a stub that checks its arguments and returns DTAPI_E_NOT_SUPPORTED; plan 0032
// fills it in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi_sdi.h" // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiAudio_MaxSamples -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiAudio_MaxSamples(int VidStd, int* NumSamples)
{
    (void)VidStd;
    if (NumSamples == NULL)
        return DTAPI_E_INVALID_ARG;
    *NumSamples = 0;
    return DTAPI_E_NOT_SUPPORTED;
}

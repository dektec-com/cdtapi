// #*#*#*#*#*#*#*#*#*#*#*#*#*# OsNoPlatform.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Stands in for the driver backend in a build made without one
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "OsBackend.h" // Backend interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A CDTAPILITE_SIM_ONLY build has no real driver to talk to. Answering NULL makes every
// attempt to open real hardware find nothing, which is what such a build should do,
// while the emulated device stays fully available.
//
const OsBackend* OsPlatformBackend(void)
{
    return NULL;
}

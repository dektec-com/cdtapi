// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# WinMemory.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Page size and fork protection on Windows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Windows includes
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// CDtapiLite includes
#include "OAL/OsDmaBuffer.h" // Platform part being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformPageSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t OsPlatformPageSize(void)
{
    SYSTEM_INFO Info;

    GetSystemInfo(&Info);
    return (size_t)Info.dwPageSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformDontFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Windows has no fork(), so there is nothing to protect against.
//
int OsPlatformDontFork(uint8_t* Data, size_t Size)
{
    (void)Data;
    (void)Size;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformDoFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsPlatformDoFork(uint8_t* Data, size_t Size)
{
    (void)Data;
    (void)Size;
}

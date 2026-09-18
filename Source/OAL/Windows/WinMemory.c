// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# WinMemory.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Page size and fork protection on Windows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Windows includes
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// CDTAPI includes
#include "OAL/OsDmaBuffer.h" // Platform part being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_PageSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t OsPlatform_PageSize(void)
{
    SYSTEM_INFO Info;

    GetSystemInfo(&Info);
    return (size_t)Info.dwPageSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_DontFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Windows has no fork(), so there is nothing to protect against.
//
int OsPlatform_DontFork(uint8_t* Data, size_t Size)
{
    (void)Data;
    (void)Size;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_DoFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsPlatform_DoFork(uint8_t* Data, size_t Size)
{
    (void)Data;
    (void)Size;
}

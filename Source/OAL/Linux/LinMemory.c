// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinMemory.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Page size and fork protection on Linux
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes the POSIX and Linux calls the backend makes.
#define _GNU_SOURCE

// Standard includes
#include <sys/mman.h>
#include <unistd.h>

// CDTAPI includes
#include "OAL/OsDmaBuffer.h" // Platform part being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_PageSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t OsPlatform_PageSize(void)
{
    long Page = sysconf(_SC_PAGESIZE);

    return Page > 0 ? (size_t)Page : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_DontFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// DtProxy.cpp does the same after allocating each DMA buffer. The range is page-aligned
// and covers whole pages, which madvise requires.
//
int OsPlatform_DontFork(uint8_t* Data, size_t Size)
{
    return madvise(Data, Size, MADV_DONTFORK) == 0 ? 0 : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_DoFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsPlatform_DoFork(uint8_t* Data, size_t Size)
{
    madvise(Data, Size, MADV_DOFORK);
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinMemory.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Page size and fork protection on Linux
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Not yet built or run: no Linux machine is available to the project.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <sys/mman.h>
#include <unistd.h>

// CDtapiLite includes
#include "OAL/OsDmaBuffer.h" // Platform part being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformPageSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t OsPlatformPageSize(void)
{
    long Page = sysconf(_SC_PAGESIZE);

    return Page > 0 ? (size_t)Page : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformDontFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// DtProxy.cpp does the same after allocating each DMA buffer. The range is page-aligned
// and covers whole pages, which madvise requires.
//
int OsPlatformDontFork(uint8_t* Data, size_t Size)
{
    return madvise(Data, Size, MADV_DONTFORK) == 0 ? 0 : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformDoFork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsPlatformDoFork(uint8_t* Data, size_t Size)
{
    madvise(Data, Size, MADV_DOFORK);
}

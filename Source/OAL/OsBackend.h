// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# OsBackend.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Interface every OS abstraction backend implements
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_OS_BACKEND_H
#define CDTAPILITE_OS_BACKEND_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backends +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Internal to the OS abstraction layer. OsDispatch.c picks one of these per handle;
// nothing above the layer sees them.
//
// A backend owns an opaque state pointer of its own. Open returns NULL when there is no
// device at that index, which is the normal answer during a scan and not an error.
//
// IoCtl follows the OsDrvIoCtl contract, except that DrvStatus is never NULL.
//

typedef struct OsBackend
{
    void* (*Open)(int Index);
    void (*Close)(void* State);
    int (*IoCtl)(void* State, unsigned long Code, const void* In, size_t InSize,
                 void* Out, size_t* OutSize, uint32_t* DrvStatus);
    unsigned long (*LastError)(const void* State);

    // NULL for a backend that maps no memory; see OsDrvMapMemory.
    void* (*MapMemory)(void* State, uint64_t Offset, size_t Size);
    void (*UnmapMemory)(void* State, void* Address, size_t Size);
} OsBackend;

// The emulated device. Always present, so that a build can be tested anywhere.
const OsBackend* OsSimBackend(void);

// True when CDTAPILITE_SIM asks for the emulator. Read once and remembered, so that
// changing the variable half way through a run cannot leave some handles emulated and
// others real.
bool OsSimIsRequested(void);

// The real driver on this platform, or NULL in a build made without it.
const OsBackend* OsPlatformBackend(void);

#endif // CDTAPILITE_OS_BACKEND_H

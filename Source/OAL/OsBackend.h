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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backends +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Internal to the OS abstraction layer. OsDispatch.c picks one of these per handle;
// nothing above the layer sees them.
//
// A backend owns an opaque state pointer of its own. Open returns NULL when there is no
// device at that index, which is the normal answer during a scan and not an error.
//

typedef struct OsBackend
{
    void* (*Open)(int Index);
    void (*Close)(void* State);
    int (*IoCtl)(void* State, unsigned long Code, const void* In, size_t InSize,
                 void* Out, size_t* OutSize);
    unsigned long (*LastError)(const void* State);
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

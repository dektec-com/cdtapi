// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# OsBackend.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Interface every OS abstraction backend implements
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

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
// device at that index, which is not an error.
//
// IoCtl follows the OsDrv_IoCtl contract, except that DrvStatus is never NULL.
//

typedef struct OsBackend
{
    void* (*Open)(int Index);
    void (*Close)(void* State);
    int (*IoCtl)(void* State, uint32_t Code, const void* In, size_t InSize, void* Out,
                 size_t* OutSize, uint32_t* DrvStatus);
    uint32_t (*LastError)(const void* State);

    // NULL for a backend that maps no memory; see OsDrv_MapMemory.
    void* (*MapMemory)(void* State, uint64_t Offset, size_t Size);
    void (*UnmapMemory)(void* State, void* Address, size_t Size);
} OsBackend;

// The emulated device. Always present, so that a build can be tested anywhere.
const OsBackend* OsSim_Backend(void);

// True when CDTAPI_SIM asks for the emulator. Read once and remembered, so that
// changing the variable half way through a run cannot leave some handles emulated and
// others real.
bool OsSim_IsRequested(void);

// The real driver on this platform, or NULL in a build made without it.
const OsBackend* OsPlatform_Backend(void);

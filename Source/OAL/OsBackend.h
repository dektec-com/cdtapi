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
// A backend is one way to reach a device: the real driver of the platform, or the
// emulated device. OsDispatch.c picks a backend for each handle and calls its functions
// for the OsDrv_ functions. Only the abstraction layer sees the backends.
//
// Each backend keeps a state of its own per open device, which Open returns.
//

typedef struct OsBackend
{
    // Opens the device at Index. Returns its state, or NULL when there is no device at
    // Index, which is not an error, or it cannot be opened.
    void* (*Open)(int Index);

    // Closes the device that State belongs to. State is not used after this.
    void (*Close)(void* State);

    // Sends a command, as OsDrv_Ioctl does, except that DrvStatus is never NULL.
    int (*Ioctl)(void* State, uint32_t Code, const void* In, size_t InSize, void* Out,
                 size_t* OutSize, uint32_t* DrvStatus);

    // Returns the error of the last failed call, as OsDrv_LastError does.
    uint32_t (*LastError)(const void* State);

    // Map and unmap memory, as OsDrv_MapMemory and OsDrv_UnmapMemory do. NULL for a
    // backend that does not map memory this way.
    void* (*MapMemory)(void* State, uint64_t Offset, size_t Size);
    void (*UnmapMemory)(void* State, void* Address, size_t Size);
} OsBackend;

// Returns the backend of the emulated device. It is in every build, so that every build
// can be tested.
const OsBackend* OsSim_Backend(void);

// Returns whether the environment variable CDTAPI_SIM asks for the emulated device. It is
// read once and remembered, so that changing it during a run cannot leave some handles
// emulated and others real.
bool OsSim_IsRequested(void);

// Returns the backend of the platform's real driver, or NULL in a build without it.
const OsBackend* OsPlatform_Backend(void);

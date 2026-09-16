// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlAlloc.c *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Allocation seam and the shared container growth policy - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>

// CDtapiLite includes
#include "DtlAlloc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Fault injection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Deliberately not thread-safe and deliberately global. Fault injection is driven from a
// single-threaded test; making it per-thread or atomic would suggest it is meant for
// production use.
//

static long g_AllocCount = 0;
static long g_FailAfter = -1;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlAllocFailAfter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtlAllocFailAfter(long Count)
{
    g_FailAfter = Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlAllocCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
long DtlAllocCount(void)
{
    return g_AllocCount;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlAllocResetCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtlAllocResetCount(void)
{
    g_AllocCount = 0;
    g_FailAfter = -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ShouldFail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Counts this allocation and reports whether it is the one that was armed to fail.
//
static int ShouldFail(void)
{
    g_AllocCount++;

    if (g_FailAfter < 0)
        return 0;

    if (g_FailAfter == 0)
    {
        // One-shot: disarm so that the caller's recovery path can itself allocate.
        g_FailAfter = -1;
        return 1;
    }

    g_FailAfter--;
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlMalloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void* DtlMalloc(size_t Size)
{
    if (ShouldFail())
        return NULL;

    return malloc(Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRealloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void* DtlRealloc(void* Ptr, size_t Size)
{
    if (ShouldFail())
        return NULL;

    return realloc(Ptr, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtlFree(void* Ptr)
{
    free(Ptr);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlGrowCapacity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlGrowCapacity(size_t Current, size_t Needed, size_t ElemSize, size_t MinCapacity,
                    size_t* Out)
{
    size_t Capacity;

    if (Out == NULL || ElemSize == 0)
        return -1;

    if (Needed <= Current)
    {
        *Out = Current;
        return 0;
    }

    Capacity = Current != 0 ? Current : MinCapacity;
    while (Capacity < Needed)
    {
        // Doubling again would wrap the capacity back to a small number, so stop at
        // exactly what is needed instead.
        if (Capacity > (size_t)-1 / 2)
        {
            Capacity = Needed;
            break;
        }
        Capacity *= 2;
    }

    // The byte count is what is actually passed to the allocator, so that is what has to
    // fit, not the element count.
    if (Capacity > (size_t)-1 / ElemSize)
        return -1;

    *Out = Capacity;
    return 0;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAlloc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Allocation seam and the shared container growth policy - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>

// CDTAPI includes
#include "DtAlloc.h"  // Interface being implemented.
#include "DtAtomic.h" // The counters.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Fault injection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The counters are atomic, because allocations can happen on several threads at once.
// Arming a failure is meant for a test that allocates from one thread: with allocations
// on other threads at the same time, a different allocation than the intended one may
// fail, or none.
//

static DtAtomicInt g_AllocCount = 0;
static DtAtomicInt g_FailAfter = -1;
static DtAtomicInt g_NumLive = 0;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_FailAfter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAlloc_FailAfter(int Count)
{
    DtAtomic_Store(&g_FailAfter, Count < 0 ? -1 : Count);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_NumAllocations -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAlloc_NumAllocations(void)
{
    return DtAtomic_Load(&g_AllocCount);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_NumLive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtAlloc_NumLive(void)
{
    return DtAtomic_Load(&g_NumLive);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_ResetCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAlloc_ResetCount(void)
{
    DtAtomic_Store(&g_AllocCount, 0);
    DtAtomic_Store(&g_FailAfter, -1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ShouldFail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Counts this allocation and reports whether it is the one that was armed to fail. The
// armed count goes down by one per allocation; the allocation that takes it from 0 to -1
// fails, which also disarms it, so that the caller's recovery path can itself allocate.
//
static int ShouldFail(void)
{
    DtAtomic_Increment(&g_AllocCount);

    if (DtAtomic_Load(&g_FailAfter) < 0)
        return 0;

    return DtAtomic_Decrement(&g_FailAfter) == -1 ? 1 : 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_Malloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void* DtAlloc_Malloc(size_t Size)
{
    if (ShouldFail())
        return NULL;

    void* Block = malloc(Size);
    if (Block != NULL)
        DtAtomic_Increment(&g_NumLive);
    return Block;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_Realloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only reallocating NULL creates a block; growing an existing one does not add to the
// live count, and a failure leaves the original block, and the count, as they were.
//
void* DtAlloc_Realloc(void* Ptr, size_t Size)
{
    if (ShouldFail())
        return NULL;

    void* Block = realloc(Ptr, Size);
    if (Block != NULL && Ptr == NULL)
        DtAtomic_Increment(&g_NumLive);
    return Block;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAlloc_Free(void* Ptr)
{
    if (Ptr != NULL)
        DtAtomic_Decrement(&g_NumLive);
    free(Ptr);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAlloc_GrowCapacity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAlloc_GrowCapacity(size_t Current, size_t Needed, size_t ElemSize,
                         size_t MinCapacity, size_t* Out)
{
    if (Out == NULL || ElemSize == 0)
        return -1;

    if (Needed <= Current)
    {
        *Out = Current;
        return 0;
    }

    size_t Capacity = Current != 0 ? Current : MinCapacity;
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

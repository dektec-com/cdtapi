// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAlloc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Allocation seam and the shared container growth policy - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>

// CDtapiLite includes
#include "DtAlloc.h"  // Interface being implemented.
#include "DtAtomic.h" // The counters.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Fault injection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The counters are atomic, because allocations can happen on several threads at once.
// Arming a failure is meant for a test
// that allocates from one thread: with allocations on other threads at the same time, a
// different allocation than the intended one may fail, or none.
//

static DtAtomicInt g_AllocCount = 0;
static DtAtomicInt g_FailAfter = -1;
static DtAtomicInt g_Live = 0;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocFailAfter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAllocFailAfter(int Count)
{
    DtAtomicStore(&g_FailAfter, Count < 0 ? -1 : Count);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAllocCount(void)
{
    return DtAtomicLoad(&g_AllocCount);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocLive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtAllocLive(void)
{
    return DtAtomicLoad(&g_Live);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocResetCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAllocResetCount(void)
{
    DtAtomicStore(&g_AllocCount, 0);
    DtAtomicStore(&g_FailAfter, -1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ShouldFail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Counts this allocation and reports whether it is the one that was armed to fail. The
// armed count goes down by one per allocation; the allocation that takes it from 0 to -1
// fails, which also disarms it, so that the caller's recovery path can itself allocate.
//
static int ShouldFail(void)
{
    DtAtomicIncrement(&g_AllocCount);

    if (DtAtomicLoad(&g_FailAfter) < 0)
        return 0;

    return DtAtomicDecrement(&g_FailAfter) == -1 ? 1 : 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtMalloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void* DtMalloc(size_t Size)
{
    if (ShouldFail())
        return NULL;

    void* Block = malloc(Size);
    if (Block != NULL)
        DtAtomicIncrement(&g_Live);
    return Block;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRealloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only reallocating NULL creates a block; growing an existing one does not add to the
// live count, and a failure leaves the original block, and the count, as they were.
//
void* DtRealloc(void* Ptr, size_t Size)
{
    if (ShouldFail())
        return NULL;

    void* Block = realloc(Ptr, Size);
    if (Block != NULL && Ptr == NULL)
        DtAtomicIncrement(&g_Live);
    return Block;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtFree(void* Ptr)
{
    if (Ptr != NULL)
        DtAtomicDecrement(&g_Live);
    free(Ptr);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtGrowCapacity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtGrowCapacity(size_t Current, size_t Needed, size_t ElemSize, size_t MinCapacity,
                   size_t* Out)
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

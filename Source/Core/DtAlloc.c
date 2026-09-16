// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAlloc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Allocation seam and the shared container growth policy - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>

// CDtapiLite includes
#include "DtAlloc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Fault injection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Deliberately not thread-safe and deliberately global. Fault injection is driven from a
// single-threaded test; making it per-thread or atomic would suggest it is meant for
// production use.
//

static long g_AllocCount = 0;
static long g_FailAfter = -1;
static long g_Live = 0;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocFailAfter -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAllocFailAfter(long Count)
{
    g_FailAfter = Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
long DtAllocCount(void)
{
    return g_AllocCount;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocLive -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
long DtAllocLive(void)
{
    return g_Live;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAllocResetCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAllocResetCount(void)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtMalloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void* DtMalloc(size_t Size)
{
    void* Block;

    if (ShouldFail())
        return NULL;

    Block = malloc(Size);
    if (Block != NULL)
        g_Live++;
    return Block;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRealloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Only reallocating NULL creates a block; growing an existing one does not add to the
// live count, and a failure leaves the original block, and the count, as they were.
//
void* DtRealloc(void* Ptr, size_t Size)
{
    void* Block;

    if (ShouldFail())
        return NULL;

    Block = realloc(Ptr, Size);
    if (Block != NULL && Ptr == NULL)
        g_Live++;
    return Block;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtFree(void* Ptr)
{
    if (Ptr != NULL)
        g_Live--;
    free(Ptr);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtGrowCapacity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtGrowCapacity(size_t Current, size_t Needed, size_t ElemSize, size_t MinCapacity,
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

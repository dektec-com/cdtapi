// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAlloc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Allocation seam and the shared container growth policy
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_ALLOC_H
#define CDTAPILITE_DT_ALLOC_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Everything in the library allocates through these three calls rather than through
// malloc directly, so that a test can make an allocation fail on demand.
//
// That is not a luxury. Every container has a path that runs only when an allocation
// fails, and a path that never runs is a path that does not work. Making them reachable
// is the only way to know that a failed allocation leaves the object usable and unchanged
// rather than half-modified.
//
// The counter is always compiled in. It costs one predictable branch on a path that was
// about to call malloc anyway, and having the production build take a different route
// than the tested one would defeat the point.
//

void* DtMalloc(size_t Size);
void* DtRealloc(void* Ptr, size_t Size);
void DtFree(void* Ptr);

// Makes the next allocation after Count more succeed-then-fail: 0 fails the very next
// one, 1 lets one through and fails the one after it. A negative value disarms the
// injection, which is the default.
void DtAllocFailAfter(long Count);

// How many allocations have been made since the last DtAllocResetCount. Lets a test
// assert that the path it meant to exercise really did allocate.
long DtAllocCount(void);

// Sets the count back to zero and disarms any pending injection.
void DtAllocResetCount(void);

// How many blocks allocated through the seam have not been freed. A test compares it
// before and after an operation to find a leak, which matters on platforms where no leak
// sanitizer runs. DtAllocResetCount does not change it.
long DtAllocLive(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Doubling from a minimum, shared by every growable container so that they cannot drift
// apart. Current is the capacity now, Needed the capacity required.
//
// Returns 0 with *Out set, or -1 when Needed elements of ElemSize bytes each cannot be
// represented in a size_t. Refusing is the point: a capacity that wraps produces a small
// allocation followed by writes beyond its end.
int DtGrowCapacity(size_t Current, size_t Needed, size_t ElemSize, size_t MinCapacity,
                   size_t* Out);

#endif // CDTAPILITE_DT_ALLOC_H

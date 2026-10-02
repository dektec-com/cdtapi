// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAlloc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Allocation seam and the shared container growth policy
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Allocation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The library allocates all its memory through DtAlloc_Malloc, DtAlloc_Realloc and
// DtAlloc_Free, never through malloc directly. A test can then make an allocation fail
// on purpose, and check that the code that handles the failure works: that it leaves
// the object usable and unchanged.
//
// A test takes these steps:
//   1. DtAlloc_ResetCount, to start counting from zero.
//   2. DtAlloc_FailAfter(N), so that allocation N + 1 from now fails.
//   3. The operation under test, which must fail cleanly.
//   4. DtAlloc_NumLive, compared with its value before step 3, to find a leak.
//
// The counting is in every build, the released one too, so that the code a test runs is
// the code a program runs.
//

// Allocate, reallocate and free memory, as malloc, realloc and free do. They also count
// the allocations and the blocks in use, and fail an allocation when a test asks for it.
void* DtAlloc_Malloc(size_t Size);
void* DtAlloc_Realloc(void* Ptr, size_t Size);
void DtAlloc_Free(void* Ptr);

// Makes an allocation fail: Count allocations succeed, and the one after them fails. 0
// fails the next one. A negative Count switches the failure off, which is the default.
// Only one allocation fails; the ones after it succeed again.
void DtAlloc_FailAfter(int Count);

// Returns how many allocations were made since the last DtAlloc_ResetCount, failed ones
// included. A test uses it to check that the code it meant to test did allocate.
int DtAlloc_NumAllocations(void);

// Sets the count of allocations back to zero and switches off a failure that
// DtAlloc_FailAfter asked for.
void DtAlloc_ResetCount(void);

// Returns how many blocks are allocated and not yet freed. A test compares it before and
// after an operation to find a leak, also on platforms without a leak sanitiser.
// DtAlloc_ResetCount does not change it.
int DtAlloc_NumLive(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Growth policy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Computes the new capacity of a container that must hold Needed elements of ElemSize
// bytes and now has room for Current. The capacity doubles, starting at MinCapacity,
// until it is large enough; every growable container uses this, so that they all grow
// alike. When Needed is not more than Current, *Out is Current.
//
// MinCapacity must not be 0, because doubling from 0 stays 0.
//
// Returns 0 with *Out set, or -1 when:
//   - Out is NULL, or ElemSize is 0
//   - the new capacity in bytes does not fit in a size_t. Allocating would wrap to a
//     small block, and the writes after it would run past its end.
int DtAlloc_GrowCapacity(size_t Current, size_t Needed, size_t ElemSize,
                         size_t MinCapacity, size_t* Out);

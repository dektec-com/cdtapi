// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVec.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Growable array of fixed-size elements
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtVec +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An array that grows as elements are added, for when the number of elements is known
// only at run time. All elements have the same size, which the vector stores, so one
// implementation serves every element type. DT_VEC_AT reads or writes an element as its
// own type.
//
// The struct is in the header so that a vector can live on the stack or inside another
// struct. Use the functions rather than its fields.
//
// The functions that can allocate return 0, or -1 when there is not enough memory; the
// vector is then unchanged.
//

typedef struct DtVec
{
    uint8_t* Data;   // The elements; NULL until the first allocation
    size_t Count;    // The number of elements
    size_t Capacity; // The number of elements Data has room for
    size_t ElemSize; // The size of one element, in bytes
} DtVec;

// Prepares an empty vector of elements of ElemSize bytes, which must not be zero.
// Nothing is allocated until the first DtVec_Push, DtVec_Reserve or DtVec_Resize.
void DtVec_Init(DtVec* Vec, size_t ElemSize);

// Frees the elements and leaves the vector empty. Calling it twice does no harm.
void DtVec_Free(DtVec* Vec);

// Makes room for at least Capacity elements. The number of elements stays the same.
int DtVec_Reserve(DtVec* Vec, size_t Capacity);

// Adds a copy of the element at Elem to the end.
int DtVec_Push(DtVec* Vec, const void* Elem);

// Sets the number of elements to Count. New elements are zero; elements beyond Count
// are dropped. The capacity never shrinks.
int DtVec_Resize(DtVec* Vec, size_t Count);

// Removes all elements, but keeps the memory, so that the vector can be filled again
// without allocating. A rescan does this.
void DtVec_Clear(DtVec* Vec);

// Returns a pointer to element Index, or NULL when Index is out of range. The pointer
// becomes invalid when DtVec_Push, DtVec_Reserve or DtVec_Resize reallocates.
void* DtVec_At(const DtVec* Vec, size_t Index);

// Returns the number of elements, or zero for a NULL Vec.
size_t DtVec_Count(const DtVec* Vec);

// Element Index of Vec, as a Type. It can be read and assigned to. An Index out of range
// dereferences NULL, so that it crashes rather than corrupt memory.
#define DT_VEC_AT(Vec, Type, Index) (*(Type*)DtVec_At((Vec), (Index)))

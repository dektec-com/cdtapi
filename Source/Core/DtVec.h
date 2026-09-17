// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtVec.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Growable array of fixed-size elements
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtVec +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A growable array of elements of one size, for a number of elements that is only known
// at run time.
//
// The element size is carried in the structure rather than generated per type, so there
// is one implementation instead of one per instantiation. Typed access goes through
// DT_VEC_AT, which keeps the cast in one place.
//
// The structure is visible so that a vector can live on the stack or inside another
// structure. Its fields are not part of any interface: go through the functions.
//
// Every call that can allocate returns 0 on success and -1 when out of memory, and
// leaves the vector unchanged on failure.
//

typedef struct DtVec
{
    uint8_t* Data;
    size_t Count;
    size_t Capacity;
    size_t ElemSize;
} DtVec;

// Prepares an empty vector. No allocation happens until the first push or reserve.
// ElemSize must not be zero.
void DtVec_Init(DtVec* Vec, size_t ElemSize);

// Releases the storage and returns the vector to the empty state, so that it can be
// initialised again or simply discarded. Safe to call twice.
void DtVec_Free(DtVec* Vec);

// Makes room for at least Capacity elements without changing the count.
int DtVec_Reserve(DtVec* Vec, size_t Capacity);

// Appends a copy of ElemSize bytes read from Elem.
int DtVec_Push(DtVec* Vec, const void* Elem);

// Sets the count. Elements added by growing are zero-filled; elements removed by
// shrinking are simply dropped. The capacity is never reduced.
int DtVec_Resize(DtVec* Vec, size_t Count);

// Drops every element but keeps the storage, so that the vector can be refilled without
// allocating again. That is what a rescan does.
void DtVec_Clear(DtVec* Vec);

// A pointer to element Index, or NULL when Index is out of range. The pointer is
// invalidated by anything that can reallocate, which is push, reserve and resize.
void* DtVec_At(const DtVec* Vec, size_t Index);

// The number of elements. Zero when Vec is NULL.
size_t DtVec_Count(const DtVec* Vec);

// Typed access. Reads as an lvalue, so it can be assigned to as well as read. Out of
// range is a null dereference rather than silent corruption.
#define DT_VEC_AT(Vec, Type, Index) (*(Type*)DtVec_At((Vec), (Index)))

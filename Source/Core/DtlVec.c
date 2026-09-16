// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlVec.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Growable array of fixed-size elements - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "DtlAlloc.h" // Allocation seam and growth policy.
#include "DtlVec.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Small enough that a vector holding a handful of ports does not waste a page, large
// enough that the common cases never reallocate.
#define DTL_VEC_MIN_CAPACITY 8

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Grow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Grows to at least Needed elements, doubling so that repeated pushes stay amortised
// constant. Returns 0 on success, -1 when out of memory, and leaves Vec untouched on
// failure.
//
static int Grow(DtlVec* Vec, size_t Needed)
{
    size_t NewCapacity;
    uint8_t* NewData;

    if (DtlGrowCapacity(Vec->Capacity, Needed, Vec->ElemSize, DTL_VEC_MIN_CAPACITY,
                        &NewCapacity) != 0)
    {
        return -1;
    }

    if (NewCapacity == Vec->Capacity)
        return 0;

    NewData = (uint8_t*)DtlRealloc(Vec->Data, NewCapacity * Vec->ElemSize);
    if (NewData == NULL)
        return -1;

    Vec->Data = NewData;
    Vec->Capacity = NewCapacity;
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlVecInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtlVecInit(DtlVec* Vec, size_t ElemSize)
{
    if (Vec == NULL)
        return;

    Vec->Data = NULL;
    Vec->Count = 0;
    Vec->Capacity = 0;
    Vec->ElemSize = ElemSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlVecFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtlVecFree(DtlVec* Vec)
{
    if (Vec == NULL)
        return;

    DtlFree(Vec->Data);
    Vec->Data = NULL;
    Vec->Count = 0;
    Vec->Capacity = 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Modifiers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlVecReserve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlVecReserve(DtlVec* Vec, size_t Capacity)
{
    if (Vec == NULL || Vec->ElemSize == 0)
        return -1;

    return Grow(Vec, Capacity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlVecPush -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtlVecPush(DtlVec* Vec, const void* Elem)
{
    if (Vec == NULL || Elem == NULL || Vec->ElemSize == 0)
        return -1;

    if (Grow(Vec, Vec->Count + 1) != 0)
        return -1;

    memcpy(Vec->Data + Vec->Count * Vec->ElemSize, Elem, Vec->ElemSize);
    Vec->Count++;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- DtlVecResize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlVecResize(DtlVec* Vec, size_t Count)
{
    if (Vec == NULL || Vec->ElemSize == 0)
        return -1;

    if (Count > Vec->Count)
    {
        if (Grow(Vec, Count) != 0)
            return -1;

        memset(Vec->Data + Vec->Count * Vec->ElemSize, 0,
               (Count - Vec->Count) * Vec->ElemSize);
    }

    Vec->Count = Count;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlVecClear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtlVecClear(DtlVec* Vec)
{
    if (Vec == NULL)
        return;

    Vec->Count = 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Accessors +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- DtlVecAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void* DtlVecAt(const DtlVec* Vec, size_t Index)
{
    if (Vec == NULL || Index >= Vec->Count)
        return NULL;

    return Vec->Data + Index * Vec->ElemSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- DtlVecCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtlVecCount(const DtlVec* Vec)
{
    return Vec != NULL ? Vec->Count : 0;
}

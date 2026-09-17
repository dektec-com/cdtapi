// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The frames of the A/V FIFO, their memory pool and their FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtAvFrame.h"    // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFramePool_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAvFramePool_Init(DtAvFramePool* Pool)
{
    memset(Pool, 0, sizeof(*Pool));
    Pool->Mutex = OsMutex_Create();
    return Pool->Mutex != NULL ? DTAPI_OK : DTAPI_E_OUT_OF_MEM;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFramePool_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvFramePool_Destroy(DtAvFramePool* Pool)
{
    DtAvFrame* Frame = Pool->All;
    while (Frame != NULL)
    {
        DtAvFrame* Next = Frame->NextInPool;
        DtAlloc_Free(Frame->Blob);
        DtAlloc_Free(Frame);
        Frame = Next;
    }
    if (Pool->Mutex != NULL)
        OsMutex_Destroy(Pool->Mutex);
    memset(Pool, 0, sizeof(*Pool));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Reserve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives a frame's data room for Size bytes on the alignment boundary; false when there
// is no memory, leaving the frame as it was.
//
static bool Reserve(DtAvFrame* Frame, size_t Size)
{
    if (Frame->Blob != NULL && Frame->Capacity >= Size)
        return true;
    uint8_t* Blob = (uint8_t*)DtAlloc_Malloc(Size + DT_AV_FRAME_ALIGNMENT);
    if (Blob == NULL)
        return false;
    DtAlloc_Free(Frame->Blob);
    size_t Skip = (DT_AV_FRAME_ALIGNMENT - (uintptr_t)Blob % DT_AV_FRAME_ALIGNMENT) %
                  DT_AV_FRAME_ALIGNMENT;
    Frame->Blob = Blob;
    Frame->Frame.Data = Blob + Skip;
    Frame->Capacity = Size + DT_AV_FRAME_ALIGNMENT - Skip;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFramePool_Get -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtAvFrame* DtAvFramePool_Get(DtAvFramePool* Pool, size_t Size)
{
    OsMutex_Lock(Pool->Mutex);
    DtAvFrame* Frame = Pool->Free;
    if (Frame != NULL)
    {
        Pool->Free = Frame->NextFree;
        Pool->NumFree--;
        Frame->IsFree = false;
        Frame->NextFree = NULL;
    }
    OsMutex_Unlock(Pool->Mutex);

    bool IsNew = Frame == NULL;
    if (IsNew)
    {
        Frame = (DtAvFrame*)DtAlloc_Malloc(sizeof(DtAvFrame));
        if (Frame == NULL)
            return NULL;
        memset(Frame, 0, sizeof(*Frame));
    }
    if (!Reserve(Frame, Size))
    {
        if (IsNew)
            DtAlloc_Free(Frame);
        else
            DtAvFramePool_Return(Pool, &Frame->Frame);
        return NULL;
    }

    if (IsNew)
    {
        OsMutex_Lock(Pool->Mutex);
        Frame->NextInPool = Pool->All;
        Pool->All = Frame;
        Pool->NumFrames++;
        OsMutex_Unlock(Pool->Mutex);
    }

    uint8_t* Data = Frame->Frame.Data;
    memset(&Frame->Frame, 0, sizeof(Frame->Frame));
    Frame->Frame.Data = Data;
    Frame->Frame.Size = Size;
    Frame->Frame.NumRows = -1;
    return Frame;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFramePool_Return -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtAvFramePool_Return(DtAvFramePool* Pool, AvFifo_Frame* Frame)
{
    if (Frame == NULL)
        return false;
    OsMutex_Lock(Pool->Mutex);
    DtAvFrame* Found = Pool->All;
    while (Found != NULL && &Found->Frame != Frame)
        Found = Found->NextInPool;
    bool Returned = Found != NULL && !Found->IsFree;
    if (Returned)
    {
        Found->IsFree = true;
        Found->NextFree = Pool->Free;
        Pool->Free = Found;
        Pool->NumFree++;
    }
    OsMutex_Unlock(Pool->Mutex);
    return Returned;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFramePool_NumFrames -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtAvFramePool_NumFrames(const DtAvFramePool* Pool)
{
    OsMutex_Lock(Pool->Mutex);
    int Count = Pool->NumFrames;
    OsMutex_Unlock(Pool->Mutex);
    return Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFramePool_NumFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtAvFramePool_NumFree(const DtAvFramePool* Pool)
{
    OsMutex_Lock(Pool->Mutex);
    int Count = Pool->NumFree;
    OsMutex_Unlock(Pool->Mutex);
    return Count;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= FIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAvFrameFifo_Init(DtAvFrameFifo* Fifo)
{
    memset(Fifo, 0, sizeof(*Fifo));
    Fifo->Mutex = OsMutex_Create();
    Fifo->MaxSize = DT_AV_FIFO_DEFAULT_MAX_SIZE;
    Fifo->Items =
        (DtAvFrame**)DtAlloc_Malloc(DT_AV_FIFO_DEFAULT_MAX_SIZE * sizeof(DtAvFrame*));
    Fifo->Capacity = DT_AV_FIFO_DEFAULT_MAX_SIZE;
    if (Fifo->Mutex == NULL || Fifo->Items == NULL)
    {
        DtAvFrameFifo_Destroy(Fifo);
        return DTAPI_E_OUT_OF_MEM;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvFrameFifo_Destroy(DtAvFrameFifo* Fifo)
{
    if (Fifo->Mutex != NULL)
        OsMutex_Destroy(Fifo->Mutex);
    DtAlloc_Free(Fifo->Items);
    memset(Fifo, 0, sizeof(*Fifo));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_Push -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtAvFrameFifo_Push(DtAvFrameFifo* Fifo, DtAvFrame* Frame)
{
    OsMutex_Lock(Fifo->Mutex);
    bool Pushed = Fifo->Count < Fifo->MaxSize && Fifo->Count < Fifo->Capacity;
    if (Pushed)
    {
        Fifo->Items[(Fifo->Head + Fifo->Count) % Fifo->Capacity] = Frame;
        Fifo->Count++;
    }
    else
        Fifo->Overflow = true;
    OsMutex_Unlock(Fifo->Mutex);
    return Pushed;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_Pop -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtAvFrame* DtAvFrameFifo_Pop(DtAvFrameFifo* Fifo)
{
    DtAvFrame* Frame = NULL;

    OsMutex_Lock(Fifo->Mutex);
    if (Fifo->Count > 0)
    {
        Frame = Fifo->Items[Fifo->Head];
        Fifo->Head = (Fifo->Head + 1) % Fifo->Capacity;
        Fifo->Count--;
    }
    OsMutex_Unlock(Fifo->Mutex);
    return Frame;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_Load -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvFrameFifo_Load(const DtAvFrameFifo* Fifo)
{
    OsMutex_Lock(Fifo->Mutex);
    int Count = Fifo->Count;
    OsMutex_Unlock(Fifo->Mutex);
    return Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_GetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtAvFrameFifo_GetMaxSize(const DtAvFrameFifo* Fifo)
{
    OsMutex_Lock(Fifo->Mutex);
    int MaxSize = Fifo->MaxSize;
    OsMutex_Unlock(Fifo->Mutex);
    return MaxSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_SetMaxSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A larger maximum gets a larger ring, into which the frames move in order.
//
DtapiResult DtAvFrameFifo_SetMaxSize(DtAvFrameFifo* Fifo, int MaxSize)
{
    if (MaxSize < 1)
        return DTAPI_E_INVALID_ARG;
    DtapiResult Result = DTAPI_OK;
    OsMutex_Lock(Fifo->Mutex);
    if (MaxSize > Fifo->Capacity)
    {
        DtAvFrame** Items =
            (DtAvFrame**)DtAlloc_Malloc((size_t)MaxSize * sizeof(DtAvFrame*));
        if (Items == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
        else
        {
            for (int i = 0; i < Fifo->Count; i++)
                Items[i] = Fifo->Items[(Fifo->Head + i) % Fifo->Capacity];
            DtAlloc_Free(Fifo->Items);
            Fifo->Items = Items;
            Fifo->Capacity = MaxSize;
            Fifo->Head = 0;
        }
    }
    if (Result == DTAPI_OK)
        Fifo->MaxSize = MaxSize;
    OsMutex_Unlock(Fifo->Mutex);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_TakeOverflow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtAvFrameFifo_TakeOverflow(DtAvFrameFifo* Fifo)
{
    OsMutex_Lock(Fifo->Mutex);
    bool Overflow = Fifo->Overflow;
    Fifo->Overflow = false;
    OsMutex_Unlock(Fifo->Mutex);
    return Overflow;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvFrameFifo_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAvFrameFifo_Clear(DtAvFrameFifo* Fifo, DtAvFramePool* Pool)
{
    for (DtAvFrame* Frame = DtAvFrameFifo_Pop(Fifo); Frame != NULL;
         Frame = DtAvFrameFifo_Pop(Fifo))
    {
        DtAvFramePool_Return(Pool, &Frame->Frame);
    }
    OsMutex_Lock(Fifo->Mutex);
    Fifo->Overflow = false;
    OsMutex_Unlock(Fifo->Mutex);
}

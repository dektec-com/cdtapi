// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvFrame.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The frames of the A/V FIFO, their memory pool and their FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>

// CDTAPI includes
#include "OAL/OsThread.h"  // Mutexes.
#include "cdtapi_avfifo.h" // AvFifo_Frame.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A frame is the application's AvFifo_Frame itself, followed by what the pool keeps: the
// application gets a pointer to the first member and gives it back. Its data starts on a
// 32-byte boundary and holds Size bytes.
//

// The boundary a frame's data starts on.
#define DT_AV_FRAME_ALIGNMENT 32

typedef struct DtAvFrame
{
    AvFifo_Frame Frame;           // Must be first
    uint8_t* Allocation;          // The allocation Frame.Data lies in
    size_t DataCapacity;          // Bytes after Frame.Data
    bool IsFree;                  // In the pool's free list
    struct DtAvFrame* NextFree;   // Free list
    struct DtAvFrame* NextInPool; // Every frame of the pool
} DtAvFrame;

// The pool frame around an application's frame, which must come from a pool.
static inline DtAvFrame* DtAvFrame_Of(AvFifo_Frame* Frame)
{
    return (DtAvFrame*)Frame;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The frames a FIFO has made, reused: a frame returns to the pool and comes out again,
// its data reallocated only when it holds fewer bytes than asked. The pool may be used
// from several threads.
//

typedef struct DtAvFramePool
{
    OsMutex* Mutex;
    DtAvFrame* FreeList;
    DtAvFrame* AllFrames;
    int NumFrames;
    int NumFree;
} DtAvFramePool;

// Makes an empty pool. DTAPI_E_OUT_OF_MEM when its mutex cannot be made.
DtapiResult DtAvFramePool_Init(DtAvFramePool* Pool);

// Frees every frame of the pool, those the application holds too, and its mutex.
void DtAvFramePool_Destroy(DtAvFramePool* Pool);

// A frame of Size bytes, free or new, with its fields cleared and NumRows -1; NULL when
// no memory is left.
DtAvFrame* DtAvFramePool_Get(DtAvFramePool* Pool, size_t Size);

// Returns a frame to the pool. False, leaving everything as it was, for NULL, a frame of
// another pool, or one that is free already.
bool DtAvFramePool_Return(DtAvFramePool* Pool, AvFifo_Frame* Frame);

// Whether Frame is a frame of the pool that is not free.
bool DtAvFramePool_Owns(DtAvFramePool* Pool, const AvFifo_Frame* Frame);

// The frames the pool made, and those of them that are free.
int DtAvFramePool_NumFrames(const DtAvFramePool* Pool);
int DtAvFramePool_NumFree(const DtAvFramePool* Pool);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= FIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Frames in order between two threads, at most MaxSize of them. A frame that finds the
// FIFO full is refused and marks it overflowed, until the mark is taken or the FIFO
// cleared.
//

// The frames a FIFO holds unless told otherwise.
#define DT_AV_FIFO_DEFAULT_MAX_SIZE 4

typedef struct DtAvFrameFifo
{
    OsMutex* Mutex;
    DtAvFrame** Ring; // A ring of Capacity items
    int RingSlots;
    int Head;
    int Load;
    int MaxSize;
    bool HasOverflowed;
} DtAvFrameFifo;

// Makes an empty FIFO of DT_AV_FIFO_DEFAULT_MAX_SIZE frames. DTAPI_E_OUT_OF_MEM when
// there is no memory.
DtapiResult DtAvFrameFifo_Init(DtAvFrameFifo* Fifo);

// Frees the FIFO's ring and mutex; the frames in it are left to their pool.
void DtAvFrameFifo_Destroy(DtAvFrameFifo* Fifo);

// Appends a frame; false, marking the FIFO overflowed, when it holds MaxSize frames.
bool DtAvFrameFifo_Push(DtAvFrameFifo* Fifo, DtAvFrame* Frame);

// Takes the oldest frame, or NULL when there is none.
DtAvFrame* DtAvFrameFifo_Pop(DtAvFrameFifo* Fifo);

// The frames in the FIFO.
int DtAvFrameFifo_Load(const DtAvFrameFifo* Fifo);

// The most frames the FIFO holds, and setting it: DTAPI_E_INVALID_ARG for less than 1,
// DTAPI_E_OUT_OF_MEM when the room cannot be made. Frames beyond a smaller maximum stay.
int DtAvFrameFifo_GetMaxSize(const DtAvFrameFifo* Fifo);
DtapiResult DtAvFrameFifo_SetMaxSize(DtAvFrameFifo* Fifo, int MaxSize);

// Whether a frame was refused since the last call or Clear; clears the mark.
bool DtAvFrameFifo_ReadAndClearOverflow(DtAvFrameFifo* Fifo);

// Empties the FIFO, returning its frames to Pool, and clears the overflow mark.
void DtAvFrameFifo_Clear(DtAvFrameFifo* Fifo, DtAvFramePool* Pool);

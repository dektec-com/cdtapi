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
// A frame as the pool keeps it: the AvFifo_Frame the application sees, followed by the
// pool's own bookkeeping. The application gets a pointer to the AvFifo_Frame and gives
// that pointer back, so the pool finds its frame without a lookup. The data starts on a
// DT_AV_FRAME_ALIGNMENT boundary.
//

// The alignment, in bytes, of a frame's data.
#define DT_AV_FRAME_ALIGNMENT 32

typedef struct DtAvFrame
{
    AvFifo_Frame Frame;           // What the application sees; must be first
    uint8_t* Allocation;          // The memory block Frame.Data points into
    size_t DataCapacity;          // How many bytes fit from Frame.Data on
    bool IsFree;                  // Whether the frame is in the pool's free list
    struct DtAvFrame* NextFree;   // The next frame in the free list
    struct DtAvFrame* NextInPool; // The next frame in the list of all the pool's frames
} DtAvFrame;

// Returns the pool frame that contains an application's frame. Frame must come from a
// pool.
static inline DtAvFrame* DtAvFrame_Of(AvFifo_Frame* Frame)
{
    return (DtAvFrame*)Frame;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A pool keeps the frames a FIFO has made, so that they are reused rather than allocated
// for every frame. A frame goes back to the pool when the application or the FIFO is
// done with it, and is handed out again later; its data is only reallocated when it is
// too small for the size asked. Several threads may use a pool at once.
//

typedef struct DtAvFramePool
{
    OsMutex* Mutex;       // Guards the lists and the counts
    DtAvFrame* FreeList;  // The frames ready to be handed out
    DtAvFrame* AllFrames; // Every frame the pool made, free or not
    int NumFrames;        // The number of frames in AllFrames
    int NumFree;          // The number of frames in FreeList
} DtAvFramePool;

// Makes an empty pool. Returns DTAPI_OK, or DTAPI_E_OUT_OF_MEM when its mutex cannot be
// made.
DtapiResult DtAvFramePool_Init(DtAvFramePool* Pool);

// Frees every frame of the pool, including those the application still holds, and the
// pool's mutex.
void DtAvFramePool_Destroy(DtAvFramePool* Pool);

// Hands out a frame with room for Size bytes: a free one, or a new one when none is
// free. Its fields are cleared, Size is set and NumRows is -1. Returns NULL when there is
// not enough memory.
DtAvFrame* DtAvFramePool_Get(DtAvFramePool* Pool, size_t Size);

// Puts a frame back in the pool's free list. Returns false, and changes nothing, when
// Frame is NULL, belongs to another pool or is already free.
bool DtAvFramePool_Return(DtAvFramePool* Pool, AvFifo_Frame* Frame);

// Returns whether Frame belongs to the pool and is handed out, not free.
bool DtAvFramePool_Owns(DtAvFramePool* Pool, const AvFifo_Frame* Frame);

// Return how many frames the pool has made, and how many of them are free.
int DtAvFramePool_NumFrames(const DtAvFramePool* Pool);
int DtAvFramePool_NumFree(const DtAvFramePool* Pool);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= FIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A frame FIFO passes frames, in order, from one thread to another. It holds at most
// MaxSize frames. When it is full, a new frame is refused and the FIFO is marked as
// overflowed; the mark stays until it is read or the FIFO is cleared.
//

// How many frames a FIFO holds unless it is told otherwise.
#define DT_AV_FIFO_DEFAULT_MAX_SIZE 4

typedef struct DtAvFrameFifo
{
    OsMutex* Mutex;     // Guards all the fields below
    DtAvFrame** Ring;   // A circular buffer of RingSlots frame pointers
    int RingSlots;      // The size of Ring; never less than MaxSize
    int Head;           // The index in Ring of the oldest frame
    int Load;           // How many frames the FIFO holds
    int MaxSize;        // The most frames the FIFO accepts
    bool HasOverflowed; // Whether a frame was refused since the mark was last read
} DtAvFrameFifo;

// Makes an empty FIFO that holds DT_AV_FIFO_DEFAULT_MAX_SIZE frames. Returns DTAPI_OK,
// or DTAPI_E_OUT_OF_MEM when there is not enough memory.
DtapiResult DtAvFrameFifo_Init(DtAvFrameFifo* Fifo);

// Frees the FIFO's buffer and mutex. Frames still in the FIFO are not freed; they belong
// to their pool.
void DtAvFrameFifo_Destroy(DtAvFrameFifo* Fifo);

// Adds a frame at the end of the FIFO. Returns false, and marks the FIFO as overflowed,
// when it is full.
bool DtAvFrameFifo_Push(DtAvFrameFifo* Fifo, DtAvFrame* Frame);

// Takes the oldest frame out of the FIFO. Returns NULL when the FIFO is empty.
DtAvFrame* DtAvFrameFifo_Pop(DtAvFrameFifo* Fifo);

// Returns how many frames the FIFO holds.
int DtAvFrameFifo_Load(const DtAvFrameFifo* Fifo);

// Get and set the most frames the FIFO accepts. Lowering it keeps the frames already in
// the FIFO. SetMaxSize returns:
//
//   DTAPI_OK              The maximum is set
//   DTAPI_E_INVALID_ARG   MaxSize is less than 1
//   DTAPI_E_OUT_OF_MEM    The buffer could not be enlarged
//
int DtAvFrameFifo_GetMaxSize(const DtAvFrameFifo* Fifo);
DtapiResult DtAvFrameFifo_SetMaxSize(DtAvFrameFifo* Fifo, int MaxSize);

// Returns whether the FIFO overflowed since the last call or the last Clear, and clears
// the mark.
bool DtAvFrameFifo_ReadAndClearOverflow(DtAvFrameFifo* Fifo);

// Empties the FIFO, putting its frames back in Pool, and clears the overflow mark.
void DtAvFrameFifo_Clear(DtAvFrameFifo* Fifo, DtAvFramePool* Pool);

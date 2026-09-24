// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtWork.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - A job in independent pieces, over a pool of threads that channels share
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "OAL/OsThread.h" // OsEvent.
#include "cdtapi.h"       // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWorkPool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Some of the library's work divides into pieces that are independent of one another,
// such as the lines of an SDI frame. A pool is where those pieces run: threads of its
// own, or the program's threads behind a dispatch function. Any number of channels may
// share one, and each divides its work over it.
//
// A pool is reference counted. The program holds it from DtWorkPool_Alloc until
// DtWorkPool_Free, a DtWork holds it from DtWork_SetPool until it lets go, and the pool
// goes when the last of them does. So a program may free its pool as soon as it has
// handed it to its channels.
//

typedef struct DtWorkPool DtWorkPool;

// A pool with neither threads nor a dispatch function, which runs every piece in the
// thread that asks for it. Returns NULL when memory runs out.
DtWorkPool* DtWorkPool_Alloc(void);

// Runs the pieces on NumThreads threads of the pool's own; the thread that asks for a
// job waits for it and takes no piece. The threads are named DtWork.1, DtWork.2 and so
// on, and live until the pool goes or is set again. Replaces a dispatch function or
// threads set before.
//
// Returns DTAPI_E_INVALID_ARG below 1, DTAPI_E_IN_USE while a DtWork holds the pool, as
// the holders have sized their buffers by it, and DTAPI_E_OUT_OF_MEM when a thread or an
// event cannot be had, leaving the pool with neither threads nor a dispatch function.
DtapiResult DtWorkPool_StartThreads(DtWorkPool* Pool, int NumThreads);

// Runs the pieces on the program's threads by handing every job to Dispatch, in at most
// NumThreads pieces. Dispatch NULL leaves the pool with neither threads nor a dispatch
// function. Replaces threads or a dispatch function set before. Dispatch may be called
// from more than one thread at once when more than one DtWork holds the pool.
//
// Returns DTAPI_E_INVALID_ARG for a NumThreads below 1 with a Dispatch, and
// DTAPI_E_IN_USE while a DtWork holds the pool.
DtapiResult DtWorkPool_SetDispatch(DtWorkPool* Pool, DtDispatchFunc Dispatch, void* User,
                                   int NumThreads);

// Lets go of the program's hold. Passing NULL does nothing.
void DtWorkPool_Free(DtWorkPool* Pool);
void DtWorkPool_Freep(DtWorkPool** Pool);

// How many pieces the pool runs at once: its threads, the NumThreads its dispatch
// function was given, or 1 with neither.
int DtWorkPool_NumThreads(const DtWorkPool* Pool);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWork +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What one user of a pool holds: the pool, the number of pieces it divides a job into,
// and the event its jobs finish on. A DtWork runs one job at a time; a channel's lock or
// its check for a read in progress sees to that. Without a pool every piece runs in the
// thread that calls DtWork_Run, which is what costs nothing and is the default.
//

typedef struct DtWork
{
    DtWorkPool* Pool; // NULL: every piece in the thread that calls DtWork_Run
    OsEvent* Done;    // Set by the piece that finishes a job on the pool's threads
    int Pieces;       // What DtWork_Run divides a job into, 1 or more
} DtWork;

// One piece, in the calling thread. A DtWork must be initialised before it is used and
// freed when it is done with; DtWork_Free leaves it initialised again, so freeing it
// twice does no harm.
void DtWork_Init(DtWork* Work);
void DtWork_Free(DtWork* Work);

// Divides the jobs over Pool, NULL for the calling thread alone. NumThreads 0 divides a
// job into as many pieces as the pool runs at once; N into N, but no more than that. The
// DtWork holds the pool until DtWork_Free or the next DtWork_SetPool.
//
// Returns DTAPI_E_INVALID_ARG below 0, and DTAPI_E_OUT_OF_MEM when the event cannot be
// had; either way the DtWork is left as it was.
DtapiResult DtWork_SetPool(DtWork* Work, DtWorkPool* Pool, int NumThreads);

// Divides the jobs into Threads pieces over a pool of the DtWork's own with as many
// threads; 1 is the calling thread alone. Returns DTAPI_E_INVALID_ARG below 1, changing
// nothing, and DTAPI_E_OUT_OF_MEM when the pool cannot be had, leaving the DtWork running
// every piece in the calling thread.
DtapiResult DtWork_SetThreads(DtWork* Work, int Threads);

// Divides the jobs into Pieces pieces and gives each job to Dispatch, over a pool of the
// DtWork's own. Dispatch NULL restores the calling thread. Returns DTAPI_E_INVALID_ARG
// for a Pieces below 1 with a Dispatch, and DTAPI_E_OUT_OF_MEM as DtWork_SetThreads does.
DtapiResult DtWork_SetDispatch(DtWork* Work, DtDispatchFunc Dispatch, void* User,
                               int Pieces);

// How many pieces DtWork_Run divides a job into: how many sets of working buffers a
// caller of it needs, and how many parts it should cut its work into.
static inline int DtWork_Pieces(const DtWork* Work)
{
    return Work->Pieces;
}

// Runs Func over DtWork_Pieces pieces and returns when every one of them has finished.
void DtWork_Run(const DtWork* Work, DtWorkFunc Func, void* Context);

// The half-open range [*First, *Last) of Total items that piece Index of Count takes.
// Every boundary other than Total is a multiple of Unit, which is 1 where the items are
// independent one by one and more where they are independent only in groups of that
// many. The ranges cover the items exactly and are as near equal in length as the unit
// allows; a range can be empty when there are fewer units than pieces.
void DtWork_Split(int Total, int Index, int Count, int Unit, int* First, int* Last);

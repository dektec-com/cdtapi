// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtWorkerPool.h *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A job in independent pieces, over a pool of threads that channels share
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "OAL/OsThread.h" // OsEvent.
#include "cdtapi.h"       // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWorkerPool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Some of the library's work splits into pieces that do not depend on each other, such
// as the lines of an SDI frame. A worker pool runs those pieces in parallel, on threads
// of its own or on threads of the program. Several channels can share one pool.
//
// A pool has a reference count. The program holds a reference from DtWorkerPool_Alloc
// until DtWorkerPool_Free, and each channel and DtJobRunner that uses the pool holds
// one too. The pool is freed when the last reference goes, so a program may free its
// pool as soon as it has given it to its channels.
//
// DtWorkerPool and the functions for programs are public, in cdtapi.h. The functions
// below are for the library itself.
//

// Adds a reference to Pool; DtWorkerPool_Free drops it again. A channel holds the pool
// it is given this way. Unlike the reference of a DtJobRunner, this one does not stop
// the program from setting the pool up again. A NULL Pool does nothing.
void DtWorkerPool_AddRef(DtWorkerPool* Pool);

// Returns how many pieces Pool runs at the same time: the number of threads given to
// DtWorkerPool_StartThreads, DtWorkerPool_SetDispatch or DtWorkerPool_ExpectThreads, or
// 1 when the pool is not set up.
int DtWorkerPool_NumThreads(const DtWorkerPool* Pool);

// Returns how many of the program's threads are in DtWorkerPool_Join now.
int DtWorkerPool_NumJoined(DtWorkerPool* Pool);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtJobRunner +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Runs a job of a channel on a worker pool: it splits the job into pieces and waits
// until all of them are done. Each channel has its own DtJobRunner.
//
// The steps are:
//   1. DtJobRunner_Init. Without a pool, every piece runs in the calling thread, which
//      costs nothing; this is the default.
//   2. DtJobRunner_SetPool, to use a pool, and DtJobRunner_NumPieces, to learn how many
//      sets of working buffers the job needs.
//   3. DtJobRunner_Run, for each job.
//   4. DtJobRunner_Free.
//
// A DtJobRunner runs one job at a time. The channel's lock, or its check for a read in
// progress, makes sure of that.
//

typedef struct DtJobRunner
{
    DtWorkerPool* Pool; // The pool, or NULL to run every piece in the calling thread
    OsEvent* Done;      // Set when the last piece of a job on the pool's threads is done
    int NumPieces;      // How many pieces DtJobRunner_Run splits a job into; 1 or more
} DtJobRunner;

// Prepares Runner to run every job as one piece, in the calling thread. A DtJobRunner
// must be initialised before it is used.
void DtJobRunner_Init(DtJobRunner* Runner);

// Lets go of the pool and frees the event, and prepares Runner again as DtJobRunner_Init
// does. Calling it twice does no harm.
void DtJobRunner_Free(DtJobRunner* Runner);

// Makes Runner run its jobs on Pool, or in the calling thread for a NULL Pool. The
// runner holds a reference to Pool until DtJobRunner_Free or the next
// DtJobRunner_SetPool. While it holds one, the program cannot set the pool up again,
// because the channel has sized its buffers by it.
//
// NumThreads sets how many pieces a job is split into: 0 for as many as the pool runs at
// the same time, and N for N, but no more than the pool runs.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_INVALID_ARG  NumThreads is negative
//   DTAPI_E_OUT_OF_MEM   the event cannot be created
// On an error, Runner is unchanged.
DtapiResult DtJobRunner_SetPool(DtJobRunner* Runner, DtWorkerPool* Pool, int NumThreads);

// Returns how many pieces DtJobRunner_Run splits a job into. The caller needs that many
// sets of working buffers, and cuts its work into that many parts.
static inline int DtJobRunner_NumPieces(const DtJobRunner* Runner)
{
    return Runner->NumPieces;
}

// Runs Func once for each piece, with Context, and returns when all pieces are done.
void DtJobRunner_Run(const DtJobRunner* Runner, DtJobFunc Func, void* Context);

// Computes which of Total items piece PieceIndex of NumPieces works on: items *First up
// to, not including, *End.
//
// Unit is the size of a group of items that must stay together; 1 when every item is
// independent. Every boundary except Total is a multiple of Unit. The ranges together
// cover all items once, and are as near equal in length as Unit allows. A range is empty
// when there are fewer groups than pieces.
void DtJobRunner_Split(int Total, int PieceIndex, int NumPieces, int Unit, int* First,
                       int* End);

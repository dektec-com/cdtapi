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
// Some of the library's work divides into pieces that are independent of one another,
// such as the lines of an SDI frame. A pool is where those pieces run: threads of its
// own, or the program's threads behind a dispatch function. Any number of channels may
// share one, and each divides its work over it.
//
// A pool is reference counted. The program holds it from DtWorkerPool_Alloc until
// DtWorkerPool_Free, a DtJobRunner holds it from DtJobRunner_SetPool until it lets go,
// and the pool goes when the last of them does. So a program may free its pool as soon
// as it has handed it to its channels.
//
// DtWorkerPool and the functions a program calls are public, in cdtapi.h; what follows is
// the library's own.
//

// Adds a reference to the pool, which DtWorkerPool_Free drops as it does the program's;
// a channel holds the pool it is given this way. Unlike a DtJobRunner's reference, it
// does not keep the pool from being set again, since it sizes no buffers by it. NULL does
// nothing.
void DtWorkerPool_AddRef(DtWorkerPool* Pool);

// How many pieces the pool runs at once: its threads, the NumThreads its dispatch
// function was given, the most threads of the program's that join it, or 1 with none.
int DtWorkerPool_NumThreads(const DtWorkerPool* Pool);

// How many of the program's threads are in DtWorkerPool_Join now.
int DtWorkerPool_NumJoined(DtWorkerPool* Pool);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtJobRunner +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What one user of a pool holds: the pool, the number of pieces it divides a job into,
// and the event its jobs finish on. A DtJobRunner runs one job at a time; a channel's
// lock or its check for a read in progress sees to that. Without a pool every piece runs
// in the thread that calls DtJobRunner_Run, which is what costs nothing and is the
// default.
//

typedef struct DtJobRunner
{
    DtWorkerPool* Pool; // NULL: every piece in the thread that calls DtJobRunner_Run
    OsEvent* Done;      // Set by the piece that finishes a job on the pool's threads
    int Pieces;         // What DtJobRunner_Run divides a job into, 1 or more
} DtJobRunner;

// One piece, in the calling thread. A DtJobRunner must be initialised before it is used
// and freed when it is done with; DtJobRunner_Free leaves it initialised again, so
// freeing it twice does no harm.
void DtJobRunner_Init(DtJobRunner* Runner);
void DtJobRunner_Free(DtJobRunner* Runner);

// Divides the jobs over Pool, NULL for the calling thread alone. NumThreads 0 divides a
// job into as many pieces as the pool runs at once; N into N, but no more than that. The
// DtJobRunner holds the pool until DtJobRunner_Free or the next DtJobRunner_SetPool.
//
// Returns DTAPI_E_INVALID_ARG below 0, and DTAPI_E_OUT_OF_MEM when the event cannot be
// had; either way the DtJobRunner is left as it was.
DtapiResult DtJobRunner_SetPool(DtJobRunner* Runner, DtWorkerPool* Pool, int NumThreads);

// How many pieces DtJobRunner_Run divides a job into: how many sets of working buffers a
// caller of it needs, and how many parts it should cut its work into.
static inline int DtJobRunner_NumPieces(const DtJobRunner* Runner)
{
    return Runner->Pieces;
}

// Runs Func over DtJobRunner_NumPieces pieces and returns when every one of them has
// finished.
void DtJobRunner_Run(const DtJobRunner* Runner, DtJobFunc Func, void* Context);

// The half-open range [*First, *Last) of Total items that piece Index of Count takes.
// Every boundary other than Total is a multiple of Unit, which is 1 where the items are
// independent one by one and more where they are independent only in groups of that
// many. The ranges cover the items exactly and are as near equal in length as the unit
// allows; a range can be empty when there are fewer units than pieces.
void DtJobRunner_Split(int Total, int Index, int Count, int Unit, int* First, int* Last);

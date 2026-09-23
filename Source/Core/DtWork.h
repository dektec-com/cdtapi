// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtWork.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - A job in independent pieces, over the caller's threads or the library's own
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWork +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Some of the library's work divides into pieces that are independent of one another:
// the lines of a 4K frame, which take a band of lines each. A DtWork says where those
// pieces run. Without one they run in the thread that asked for them, which is what
// costs nothing and is the default.
//
// A program that already has a thread pool gives its own executor, so that the library
// competes with nothing; a program that has none asks for a number of threads and gets a
// pool of the library's own, which exists for as long as the DtWork does.
//

// Does piece Index of Count pieces of a job. The pieces are independent and may run in
// any order, on any thread.
typedef void (*DtWorkFunc)(void* Context, int Index, int Count);

// Runs Work(Context, Index, Count) for every Index below Count, in parallel or not, and
// returns once every one of them has finished. Work must not be called after the return.
typedef void (*DtExecuteFunc)(void* User, DtWorkFunc Work, void* Context, int Count);

typedef struct DtWorkPool DtWorkPool;

typedef struct DtWork
{
    DtExecuteFunc Execute; // NULL: every piece in the thread that calls DtWork_Run
    void* User;
    DtWorkPool* Pool; // The library's own, which Execute then runs the job on
    int Pieces;       // What DtWork_Run divides a job into, 1 or more
} DtWork;

// One piece, in the calling thread. A DtWork must be initialised before it is used and
// freed once, and DtWork_Free leaves it initialised again.
void DtWork_Init(DtWork* Work);
void DtWork_Free(DtWork* Work);

// Runs the jobs on Threads threads of the library's own, 1 for the calling thread alone,
// and divides them into as many pieces. The threads are started here and live until
// DtWork_Free or the next DtWork_SetThreads, so a channel does this where it starts and
// stops rather than per frame. Replaces an executor set before it.
//
// Returns DTAPI_E_INVALID_ARG below 1, DTAPI_E_OUT_OF_MEM when a thread or an event
// cannot be created, and then leaves the DtWork running every piece in the calling
// thread.
DtapiResult DtWork_SetThreads(DtWork* Work, int Threads);

// Runs the jobs on the caller's executor, in Pieces pieces. Execute NULL restores the
// calling thread, whatever DtWork_SetThreads asked for before it.
DtapiResult DtWork_SetExecutor(DtWork* Work, DtExecuteFunc Execute, void* User,
                               int Pieces);

// How many pieces DtWork_Run divides a job into: how many sets of working buffers a
// caller of it needs, and how many bands it should cut its work into.
static inline int DtWork_Pieces(const DtWork* Work)
{
    return Work->Pieces;
}

// Runs Func over DtWork_Pieces pieces and returns when every one of them has finished.
void DtWork_Run(const DtWork* Work, DtWorkFunc Func, void* Context);

// The half-open range [*First, *Last) of Total items that piece Index of Count takes.
// Every boundary is a multiple of Unit, which is 1 where the items are independent one by
// one and more where they are independent only in groups of that many. The ranges cover
// the items exactly and are as near equal in length as the unit allows; a range can be
// empty when there are fewer units than pieces.
void DtWork_Band(int Total, int Index, int Count, int Unit, int* First, int* Last);

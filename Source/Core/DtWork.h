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
// the lines of an SDI frame, of which each piece takes a band. A DtWork says where those
// pieces run. Without one they run in the thread that asked for them, which is what
// costs nothing and is the default.
//
// A program that already has a thread pool gives a dispatch function that runs the pieces
// on it, so that the library competes with nothing; a program that has none asks for a
// number of threads and gets a pool of the library's own, which exists for as long as the
// DtWork does. DtWorkFunc and DtDispatchFunc are the public types, in cdtapi.h.
//

typedef struct DtWorkPool DtWorkPool;

typedef struct DtWork
{
    DtDispatchFunc Dispatch; // NULL: every piece in the thread that calls DtWork_Run
    void* User;
    DtWorkPool* Pool; // The library's own, which Dispatch then runs the job on
    int Pieces;       // What DtWork_Run divides a job into, 1 or more
} DtWork;

// One piece, in the calling thread. A DtWork must be initialised before it is used and
// freed when it is done with; DtWork_Free leaves it initialised again, so freeing it
// twice does no harm.
void DtWork_Init(DtWork* Work);
void DtWork_Free(DtWork* Work);

// Runs the jobs on Threads threads, the calling thread and Threads - 1 of the library's
// own, and divides them into as many pieces; 1 is the calling thread alone. The threads
// are started here and live until DtWork_Free or the next DtWork_SetThreads, so a channel
// does this where it starts and stops rather than per frame. Replaces a dispatch function
// set before it.
//
// Tag names the threads, so that a process viewer tells one pool's from another's: the
// threads are called Tag.1, Tag.2 and so on, counting from 1 because the calling thread
// takes pieces too, and DtConv.1 and so on without a Tag. A caller with more than one
// pool gives each a tag of its own, such as the direction and the port. Fifteen
// characters are shown, which is what Linux allows, so a tag of about ten leaves room for
// the number; a longer one is composed in full and cut when the thread is named.
//
// Returns DTAPI_E_INVALID_ARG below 1, changing nothing, and DTAPI_E_OUT_OF_MEM when a
// thread or an event cannot be created, leaving the DtWork running every piece in the
// calling thread.
DtapiResult DtWork_SetThreads(DtWork* Work, int Threads, const char* Tag);

// Runs the jobs on the caller's own threads, in Pieces pieces, by giving each of them to
// Dispatch. Dispatch NULL restores the calling thread, whatever DtWork_SetThreads asked
// for before it. Returns DTAPI_E_INVALID_ARG for a Pieces below 1 with a Dispatch.
DtapiResult DtWork_SetDispatch(DtWork* Work, DtDispatchFunc Dispatch, void* User,
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
// Every boundary other than Total is a multiple of Unit, which is 1 where the items are
// independent one by one and more where they are independent only in groups of that
// many. The ranges cover the items exactly and are as near equal in length as the unit
// allows; a range can be empty when there are fewer units than pieces.
void DtWork_Band(int Total, int Index, int Count, int Unit, int* First, int* Last);

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtWork.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - A job in independent pieces, over a pool of threads that channels share
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "DtAlloc.h"  // The library's allocator.
#include "DtAtomic.h" // The references to a pool, and the pieces of a job finished.
#include "DtWork.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A pool of its own threads keeps a queue of the jobs whose pieces are not all taken yet,
// one job from each DtWork that is running one. A thread takes the next piece of the
// first job in the queue, so the jobs of several channels run at the same time when the
// pool has threads enough, and a piece that takes longer than the others holds up no one.
//
// A job lives on the stack of the thread that asked for it, which waits until the job is
// finished. So a piece is taken under the pool's lock, which also publishes the job to
// the threads, and a thread touches the job no more once it has counted its piece
// finished: the piece that finishes the job may let its caller return.
//

typedef struct Job
{
    DtWorkFunc Func;
    void* Context;
    int NumPieces;
    int NextPiece; // Under the pool's lock
    DtAtomicInt NumFinished;
    OsEvent* Done;    // The DtWork's, set by the piece that finishes the job
    struct Job* Next; // Under the pool's lock
} Job;

typedef struct Worker
{
    DtWorkPool* Pool;
    OsEvent* Go; // Set for every job queued, and once more to stop
    OsThread* Thread;
    char Name[32]; // What a process viewer shows beside the thread, before cutting
} Worker;

struct DtWorkPool
{
    DtAtomicInt NumRefs;    // The program's hold, if it has not let go, and the holders
    DtAtomicInt NumHolders; // The DtWorks holding the pool

    // A dispatch function of the program's, or threads of the pool's own, or neither.
    DtDispatchFunc Dispatch;
    void* User;
    int NumThreads; // 0 with neither
    Worker* Worker; // NumThreads of them with threads of its own
    int NumStarted; // How many of them are running

    OsMutex* Lock; // Guards what follows
    Job* Head;     // The jobs with pieces not taken yet, oldest first
    Job* Tail;     //
    bool Stop;     // Set to end the threads
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakePiece -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The first queued job and the number of the piece taken from it, or NULL with the queue
// empty. A job leaves the queue with its last piece taken. Called under the pool's lock.
//
static Job* TakePiece(DtWorkPool* Pool, int* Piece)
{
    Job* Taken = Pool->Head;

    if (Taken == NULL)
        return NULL;
    *Piece = Taken->NextPiece++;
    if (Taken->NextPiece == Taken->NumPieces)
    {
        Pool->Head = Taken->Next;
        if (Pool->Head == NULL)
            Pool->Tail = NULL;
    }
    return Taken;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WorkerThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits to be woken, then does pieces until the queue is empty. A wake-up that comes
// while the thread is busy leaves its event set, so the next wait returns at once and a
// job queued meanwhile is not missed.
//
static void WorkerThread(void* Context)
{
    Worker* Self = (Worker*)Context;
    DtWorkPool* Pool = Self->Pool;

    OsThread_SetName(Self->Name);
    for (;;)
    {
        if (OsEvent_Wait(Self->Go, -1) != OS_WAIT_SIGNALLED)
            return;
        for (;;)
        {
            int Piece = 0;

            OsMutex_Lock(Pool->Lock);
            const bool Stop = Pool->Stop;
            Job* Taken = Stop ? NULL : TakePiece(Pool, &Piece);
            OsMutex_Unlock(Pool->Lock);
            if (Stop)
                return;
            if (Taken == NULL)
                break;

            Taken->Func(Taken->Context, Piece, Taken->NumPieces);

            // Read before the count, which may be the one that lets the caller return.
            const int NumPieces = Taken->NumPieces;
            OsEvent* Done = Taken->Done;
            if (DtAtomic_Increment(&Taken->NumFinished) == NumPieces)
                OsEvent_Set(Done);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Ends and releases the pool's threads and forgets a dispatch function, leaving it with
// neither. Called with no job running, which no holder means.
//
static void StopThreads(DtWorkPool* Pool)
{
    OsMutex_Lock(Pool->Lock);
    Pool->Stop = true;
    OsMutex_Unlock(Pool->Lock);
    for (int i = 0; i < Pool->NumStarted; i++)
    {
        OsEvent_Set(Pool->Worker[i].Go);
        OsThread_Join(Pool->Worker[i].Thread);
    }
    for (int i = 0; Pool->Worker != NULL && i < Pool->NumThreads; i++)
        OsEvent_Destroy(Pool->Worker[i].Go);
    DtAlloc_Free(Pool->Worker);
    Pool->Worker = NULL;
    Pool->NumStarted = 0;
    Pool->NumThreads = 0;
    Pool->Dispatch = NULL;
    Pool->User = NULL;
    Pool->Stop = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Release -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Drops one reference, and the pool with the last.
//
static void Release(DtWorkPool* Pool)
{
    if (DtAtomic_Decrement(&Pool->NumRefs) != 0)
        return;

    StopThreads(Pool);
    OsMutex_Destroy(Pool->Lock);
    DtAlloc_Free(Pool);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunQueued -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Queues the job for the pool's threads, wakes them all, and waits for the last piece.
//
static void RunQueued(DtWorkPool* Pool, OsEvent* Done, DtWorkFunc Func, void* Context,
                      int NumPieces)
{
    Job Queued;

    Queued.Func = Func;
    Queued.Context = Context;
    Queued.NumPieces = NumPieces;
    Queued.NextPiece = 0;
    DtAtomic_Init(&Queued.NumFinished, 0);
    Queued.Done = Done;
    Queued.Next = NULL;

    OsMutex_Lock(Pool->Lock);
    if (Pool->Tail != NULL)
        Pool->Tail->Next = &Queued;
    else
        Pool->Head = &Queued;
    Pool->Tail = &Queued;
    OsMutex_Unlock(Pool->Lock);

    for (int i = 0; i < Pool->NumStarted; i++)
        OsEvent_Set(Pool->Worker[i].Go);
    OsEvent_Wait(Done, -1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWorkPool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtWorkPool* DtWorkPool_Alloc(void)
{
    DtWorkPool* Pool = (DtWorkPool*)DtAlloc_Malloc(sizeof(DtWorkPool));

    if (Pool == NULL)
        return NULL;
    memset(Pool, 0, sizeof(*Pool));
    DtAtomic_Init(&Pool->NumRefs, 1);
    DtAtomic_Init(&Pool->NumHolders, 0);
    Pool->Lock = OsMutex_Create();
    if (Pool->Lock == NULL)
    {
        DtAlloc_Free(Pool);
        return NULL;
    }
    return Pool;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_StartThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtWorkPool_StartThreads(DtWorkPool* Pool, int NumThreads)
{
    if (Pool == NULL || NumThreads < 1)
        return DTAPI_E_INVALID_ARG;
    if (DtAtomic_Load(&Pool->NumHolders) != 0)
        return DTAPI_E_IN_USE;

    StopThreads(Pool);
    Pool->Worker = (Worker*)DtAlloc_Malloc((size_t)NumThreads * sizeof(Worker));
    if (Pool->Worker == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Pool->Worker, 0, (size_t)NumThreads * sizeof(Worker));
    Pool->NumThreads = NumThreads;

    for (int i = 0; i < NumThreads; i++)
    {
        Pool->Worker[i].Pool = Pool;
        snprintf(Pool->Worker[i].Name, sizeof(Pool->Worker[i].Name), "DtWork.%d", i + 1);
        Pool->Worker[i].Go = OsEvent_Create();
        if (Pool->Worker[i].Go == NULL)
        {
            StopThreads(Pool);
            return DTAPI_E_OUT_OF_MEM;
        }
    }
    for (int i = 0; i < NumThreads; i++)
    {
        Pool->Worker[i].Thread = OsThread_Start(WorkerThread, &Pool->Worker[i]);
        if (Pool->Worker[i].Thread == NULL)
        {
            StopThreads(Pool);
            return DTAPI_E_OUT_OF_MEM;
        }
        Pool->NumStarted = i + 1;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_SetDispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtWorkPool_SetDispatch(DtWorkPool* Pool, DtDispatchFunc Dispatch, void* User,
                                   int NumThreads)
{
    if (Pool == NULL || (Dispatch != NULL && NumThreads < 1))
        return DTAPI_E_INVALID_ARG;
    if (DtAtomic_Load(&Pool->NumHolders) != 0)
        return DTAPI_E_IN_USE;

    StopThreads(Pool);
    if (Dispatch == NULL)
        return DTAPI_OK;
    Pool->Dispatch = Dispatch;
    Pool->User = User;
    Pool->NumThreads = NumThreads;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_Hold -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWorkPool_Hold(DtWorkPool* Pool)
{
    if (Pool != NULL)
        DtAtomic_Increment(&Pool->NumRefs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWorkPool_Free(DtWorkPool* Pool)
{
    if (Pool != NULL)
        Release(Pool);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWorkPool_Freep(DtWorkPool** Pool)
{
    if (Pool == NULL)
        return;
    DtWorkPool_Free(*Pool);
    *Pool = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkPool_NumThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtWorkPool_NumThreads(const DtWorkPool* Pool)
{
    return Pool->NumThreads > 0 ? Pool->NumThreads : 1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWork +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWork_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWork_Init(DtWork* Work)
{
    memset(Work, 0, sizeof(*Work));
    Work->Pieces = 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWork_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWork_Free(DtWork* Work)
{
    if (Work->Pool != NULL)
    {
        DtAtomic_Decrement(&Work->Pool->NumHolders);
        Release(Work->Pool);
    }
    OsEvent_Destroy(Work->Done);
    DtWork_Init(Work);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWork_SetPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The new pool is held before the old one is let go, so that setting the pool a DtWork
// already holds does not drop it on the way.
//
DtapiResult DtWork_SetPool(DtWork* Work, DtWorkPool* Pool, int NumThreads)
{
    if (NumThreads < 0)
        return DTAPI_E_INVALID_ARG;

    OsEvent* Done = NULL;
    if (Pool != NULL)
    {
        Done = OsEvent_Create();
        if (Done == NULL)
            return DTAPI_E_OUT_OF_MEM;
        DtAtomic_Increment(&Pool->NumRefs);
        DtAtomic_Increment(&Pool->NumHolders);
    }
    DtWork_Free(Work);
    if (Pool == NULL)
        return DTAPI_OK;

    const int Cap = DtWorkPool_NumThreads(Pool);
    Work->Pool = Pool;
    Work->Done = Done;
    Work->Pieces = NumThreads > 0 && NumThreads < Cap ? NumThreads : Cap;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWork_Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A job of one piece runs in the calling thread whatever the pool, since handing it on
// would cost more than the piece.
//
void DtWork_Run(const DtWork* Work, DtWorkFunc Func, void* Context)
{
    const DtWorkPool* Pool = Work->Pool;

    if (Pool == NULL || Work->Pieces == 1 || Pool->NumThreads == 0)
    {
        for (int i = 0; i < Work->Pieces; i++)
            Func(Context, i, Work->Pieces);
        return;
    }
    if (Pool->Dispatch != NULL)
    {
        Pool->Dispatch(Pool->User, Func, Context, Work->Pieces);
        return;
    }
    RunQueued(Work->Pool, Work->Done, Func, Context, Work->Pieces);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWork_Split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWork_Split(int Total, int Index, int Count, int Unit, int* First, int* Last)
{
    // Counted in whole units, of which the last may be short.
    const int Units = (Total + Unit - 1) / Unit;
    const int Each = Units / Count;
    const int Over = Units % Count;
    const int FirstUnit = Index * Each + (Index < Over ? Index : Over);
    const int LastUnit = FirstUnit + Each + (Index < Over ? 1 : 0);

    *First = FirstUnit * Unit < Total ? FirstUnit * Unit : Total;
    *Last = LastUnit * Unit < Total ? LastUnit * Unit : Total;
}

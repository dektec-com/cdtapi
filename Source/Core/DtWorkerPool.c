// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtWorkerPool.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
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
#include "DtAlloc.h"      // The library's allocator.
#include "DtAtomic.h"     // The references to a pool, and the pieces of a job finished.
#include "DtWorkerPool.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A pool of its own threads keeps a queue of the jobs whose pieces are not all taken yet,
// one job from each DtJobRunner that is running one. A thread takes the next piece of the
// first job in the queue, so the jobs of several channels run at the same time when the
// pool has threads enough, and a piece that takes longer than the others holds up no one.
//
// A job lives on the stack of the thread that asked for it, which waits until the job is
// finished. So a piece is taken under the pool's lock, which also publishes the job to
// the threads, and a thread touches the job no more once it has counted its piece
// finished: the piece that finishes the job may let its caller return.
//
// A pool the program's threads join takes pieces from the same queue, and adds one rule:
// a job is queued only while a thread is joined, and the last thread to leave empties
// the queue first. Both happen under the lock, so no job waits for a thread that is not
// coming.
//

typedef struct Job
{
    DtJobFunc Func;
    void* Context;
    int NumPieces;
    int NextPiece; // Under the pool's lock
    DtAtomicInt NumFinished;
    OsEvent* Done;    // The DtJobRunner's, set by the piece that finishes the job
    struct Job* Next; // Under the pool's lock
} Job;

typedef struct PoolThread
{
    DtWorkerPool* Pool;
    OsEvent* Go; // Set for every job queued, and once more to stop
    OsThread* Thread;
    char Name[32]; // What a process viewer shows beside the thread, before cutting
} PoolThread;

struct DtWorker
{
    OsEvent* Go;    // Set for every job queued while joined, and to send it back
    bool Joined;    // Under the lock of the pool it joins
    bool Dismissed; // Under the lock of the pool it joins or is to join
    DtWorker* Next; // The next joined thread, under the pool's lock
};

struct DtWorkerPool
{
    DtAtomicInt NumRefs; // The program's hold, if it has not let go, and the holders
    DtAtomicInt NumRunnersSized; // The DtJobRunners that sized buffers by the pool

    // A dispatch function of the program's, threads of the pool's own, the program's
    // threads joining, or none of them.
    DtJobDispatchFunc Dispatch;
    void* User;
    int NumThreads;      // 0 with none; the most that join, for joining threads
    PoolThread* Workers; // NumThreads of them with threads of its own
    int NumStarted;      // How many of them are running

    OsMutex* Lock;         // Guards what follows
    Job* Head;             // The jobs with pieces not taken yet, oldest first
    Job* Tail;             //
    bool Stop;             // Set to end the threads
    bool Joinable;         // The program's threads join it
    DtWorker* FirstJoined; // Those in DtWorkerPool_Join
    int NumJoined;         //
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakePiece -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The first queued job and the number of the piece taken from it, or NULL with the queue
// empty. A job leaves the queue with its last piece taken. Called under the pool's lock.
//
static Job* TakePiece(DtWorkerPool* Pool, int* Piece)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunPiece -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Does piece Piece of Taken and counts it finished. The count that finishes the job may
// let its caller return, so nothing of the job is touched after it.
//
static void RunPiece(Job* Taken, int Piece)
{
    Taken->Func(Taken->Context, Piece, Taken->NumPieces);

    const int NumPieces = Taken->NumPieces;
    OsEvent* Done = Taken->Done;
    if (DtAtomic_Increment(&Taken->NumFinished) == NumPieces)
        OsEvent_Set(Done);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PoolThreadMain -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Waits to be woken, then does pieces until the queue is empty. A wake-up that comes
// while the thread is busy leaves its event set, so the next wait returns at once and a
// job queued meanwhile is not missed.
//
static void PoolThreadMain(void* Context)
{
    PoolThread* Self = (PoolThread*)Context;
    DtWorkerPool* Pool = Self->Pool;

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
            RunPiece(Taken, Piece);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StopThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Ends and releases the pool's threads and forgets a dispatch function and joining
// threads, leaving it with none of them. Called with no job running and no thread joined.
//
static void StopThreads(DtWorkerPool* Pool)
{
    OsMutex_Lock(Pool->Lock);
    Pool->Stop = true;
    OsMutex_Unlock(Pool->Lock);
    for (int i = 0; i < Pool->NumStarted; i++)
    {
        OsEvent_Set(Pool->Workers[i].Go);
        OsThread_Join(Pool->Workers[i].Thread);
    }
    for (int i = 0; Pool->Workers != NULL && i < Pool->NumThreads; i++)
        OsEvent_Destroy(Pool->Workers[i].Go);
    DtAlloc_Free(Pool->Workers);
    Pool->Workers = NULL;
    Pool->NumStarted = 0;
    Pool->NumThreads = 0;
    Pool->Dispatch = NULL;
    Pool->User = NULL;
    Pool->Stop = false;
    Pool->Joinable = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsInUse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True while the pool may not be set again: a DtJobRunner holds it and has sized its
// buffers by it, or a thread of the program's is joined.
//
static bool IsInUse(DtWorkerPool* Pool)
{
    OsMutex_Lock(Pool->Lock);
    const int NumJoined = Pool->NumJoined;
    OsMutex_Unlock(Pool->Lock);
    return DtAtomic_Load(&Pool->NumRunnersSized) != 0 || NumJoined != 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DropRef -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Drops one reference, and the pool with the last.
//
static void DropRef(DtWorkerPool* Pool)
{
    if (DtAtomic_Decrement(&Pool->NumRefs) != 0)
        return;

    StopThreads(Pool);
    OsMutex_Destroy(Pool->Lock);
    DtAlloc_Free(Pool);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RunQueued -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Queues the job for the pool's threads, wakes them all, and waits for the last piece. A
// pool that no thread of the program's has joined has no one to run it, so the calling
// thread does.
//
static void RunQueued(DtWorkerPool* Pool, OsEvent* Done, DtJobFunc Func, void* Context,
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
    const bool NoThreadJoined = Pool->Joinable && Pool->NumJoined == 0;
    if (!NoThreadJoined)
    {
        if (Pool->Tail != NULL)
            Pool->Tail->Next = &Queued;
        else
            Pool->Head = &Queued;
        Pool->Tail = &Queued;
        for (DtWorker* Worker = Pool->FirstJoined; Worker != NULL; Worker = Worker->Next)
            OsEvent_Set(Worker->Go);
    }
    OsMutex_Unlock(Pool->Lock);
    if (NoThreadJoined)
    {
        for (int i = 0; i < NumPieces; i++)
            Func(Context, i, NumPieces);
        return;
    }

    for (int i = 0; i < Pool->NumStarted; i++)
        OsEvent_Set(Pool->Workers[i].Go);
    OsEvent_Wait(Done, -1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtWorkerPool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtWorkerPool* DtWorkerPool_Alloc(void)
{
    DtWorkerPool* Pool = (DtWorkerPool*)DtAlloc_Malloc(sizeof(DtWorkerPool));

    if (Pool == NULL)
        return NULL;
    memset(Pool, 0, sizeof(*Pool));
    DtAtomic_Init(&Pool->NumRefs, 1);
    DtAtomic_Init(&Pool->NumRunnersSized, 0);
    Pool->Lock = OsMutex_Create();
    if (Pool->Lock == NULL)
    {
        DtAlloc_Free(Pool);
        return NULL;
    }
    return Pool;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_StartThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtWorkerPool_StartThreads(DtWorkerPool* Pool, int NumThreads)
{
    if (Pool == NULL || NumThreads < 2)
        return DTAPI_E_INVALID_ARG;
    if (IsInUse(Pool))
        return DTAPI_E_IN_USE;

    StopThreads(Pool);
    Pool->Workers = (PoolThread*)DtAlloc_Malloc((size_t)NumThreads * sizeof(PoolThread));
    if (Pool->Workers == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Pool->Workers, 0, (size_t)NumThreads * sizeof(PoolThread));
    Pool->NumThreads = NumThreads;

    for (int i = 0; i < NumThreads; i++)
    {
        Pool->Workers[i].Pool = Pool;
        snprintf(Pool->Workers[i].Name, sizeof(Pool->Workers[i].Name), "DtWorker.%d",
                 i + 1);
        Pool->Workers[i].Go = OsEvent_Create();
        if (Pool->Workers[i].Go == NULL)
        {
            StopThreads(Pool);
            return DTAPI_E_OUT_OF_MEM;
        }
    }
    for (int i = 0; i < NumThreads; i++)
    {
        Pool->Workers[i].Thread = OsThread_Start(PoolThreadMain, &Pool->Workers[i]);
        if (Pool->Workers[i].Thread == NULL)
        {
            StopThreads(Pool);
            return DTAPI_E_OUT_OF_MEM;
        }
        Pool->NumStarted = i + 1;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_SetDispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtWorkerPool_SetDispatch(DtWorkerPool* Pool, DtJobDispatchFunc Dispatch,
                                     void* User, int NumThreads)
{
    if (Pool == NULL || (Dispatch != NULL && NumThreads < 2))
        return DTAPI_E_INVALID_ARG;
    if (IsInUse(Pool))
        return DTAPI_E_IN_USE;

    StopThreads(Pool);
    if (Dispatch == NULL)
        return DTAPI_OK;
    Pool->Dispatch = Dispatch;
    Pool->User = User;
    Pool->NumThreads = NumThreads;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_ExpectThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtWorkerPool_ExpectThreads(DtWorkerPool* Pool, int NumThreads)
{
    if (Pool == NULL || NumThreads < 2)
        return DTAPI_E_INVALID_ARG;
    if (IsInUse(Pool))
        return DTAPI_E_IN_USE;

    StopThreads(Pool);
    OsMutex_Lock(Pool->Lock);
    Pool->Joinable = true;
    Pool->NumThreads = NumThreads;
    OsMutex_Unlock(Pool->Lock);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Leave -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes Worker out of the pool's joined threads, under the pool's lock.
//
static void Leave(DtWorkerPool* Pool, DtWorker* Worker)
{
    DtWorker** Link = &Pool->FirstJoined;

    while (*Link != Worker)
        Link = &(*Link)->Next;
    *Link = Worker->Next;
    Worker->Next = NULL;
    Worker->Joined = false;
    Worker->Dismissed = false;
    Pool->NumJoined--;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_Join -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A thread sent back leaves between pieces, never during one. It stays while the queue
// holds pieces and it is the last joined thread, since a job was queued on the promise
// that a thread would take it. It holds the pool while joined.
//
DtapiResult DtWorkerPool_Join(DtWorkerPool* Pool, DtWorker* Worker)
{
    if (Pool == NULL || Worker == NULL)
        return DTAPI_E_INVALID_ARG;

    OsMutex_Lock(Pool->Lock);
    DtapiResult Result = !Pool->Joinable ? DTAPI_E_NOT_SUPPORTED
                         : Worker->Joined || Pool->NumJoined >= Pool->NumThreads
                             ? DTAPI_E_IN_USE
                             : DTAPI_OK;
    if (Result != DTAPI_OK || Worker->Dismissed)
    {
        if (Result == DTAPI_OK)
            Worker->Dismissed = false;
        OsMutex_Unlock(Pool->Lock);
        return Result;
    }
    Worker->Joined = true;
    Worker->Next = Pool->FirstJoined;
    Pool->FirstJoined = Worker;
    Pool->NumJoined++;
    DtWorkerPool_AddRef(Pool);
    OsMutex_Unlock(Pool->Lock);

    for (;;)
    {
        int Piece = 0;

        OsMutex_Lock(Pool->Lock);
        if (Worker->Dismissed && (Pool->NumJoined > 1 || Pool->Head == NULL))
        {
            Leave(Pool, Worker);
            OsMutex_Unlock(Pool->Lock);
            break;
        }
        Job* Taken = TakePiece(Pool, &Piece);
        OsMutex_Unlock(Pool->Lock);

        if (Taken != NULL)
            RunPiece(Taken, Piece);
        else
            OsEvent_Wait(Worker->Go, -1);
    }
    DropRef(Pool);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_Dismiss -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWorkerPool_Dismiss(DtWorkerPool* Pool, DtWorker* Worker)
{
    if (Pool == NULL || Worker == NULL)
        return;

    OsMutex_Lock(Pool->Lock);
    Worker->Dismissed = true;
    OsEvent_Set(Worker->Go);
    OsMutex_Unlock(Pool->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_DismissAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWorkerPool_DismissAll(DtWorkerPool* Pool)
{
    if (Pool == NULL)
        return;

    OsMutex_Lock(Pool->Lock);
    for (DtWorker* Worker = Pool->FirstJoined; Worker != NULL; Worker = Worker->Next)
    {
        Worker->Dismissed = true;
        OsEvent_Set(Worker->Go);
    }
    OsMutex_Unlock(Pool->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_NumJoined -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtWorkerPool_NumJoined(DtWorkerPool* Pool)
{
    OsMutex_Lock(Pool->Lock);
    const int NumJoined = Pool->NumJoined;
    OsMutex_Unlock(Pool->Lock);
    return NumJoined;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorker_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtWorker* DtWorker_Alloc(void)
{
    DtWorker* Worker = (DtWorker*)DtAlloc_Malloc(sizeof(DtWorker));

    if (Worker == NULL)
        return NULL;
    memset(Worker, 0, sizeof(*Worker));
    Worker->Go = OsEvent_Create();
    if (Worker->Go == NULL)
    {
        DtAlloc_Free(Worker);
        return NULL;
    }
    return Worker;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorker_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWorker_Free(DtWorker* Worker)
{
    if (Worker == NULL)
        return;
    OsEvent_Destroy(Worker->Go);
    DtAlloc_Free(Worker);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorker_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWorker_Freep(DtWorker** Worker)
{
    if (Worker == NULL)
        return;
    DtWorker_Free(*Worker);
    *Worker = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_AddRef -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWorkerPool_AddRef(DtWorkerPool* Pool)
{
    if (Pool != NULL)
        DtAtomic_Increment(&Pool->NumRefs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWorkerPool_Free(DtWorkerPool* Pool)
{
    if (Pool != NULL)
        DropRef(Pool);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWorkerPool_Freep(DtWorkerPool** Pool)
{
    if (Pool == NULL)
        return;
    DtWorkerPool_Free(*Pool);
    *Pool = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWorkerPool_NumThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtWorkerPool_NumThreads(const DtWorkerPool* Pool)
{
    return Pool->NumThreads > 0 ? Pool->NumThreads : 1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtJobRunner +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtJobRunner_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtJobRunner_Init(DtJobRunner* Runner)
{
    memset(Runner, 0, sizeof(*Runner));
    Runner->Pieces = 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtJobRunner_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtJobRunner_Free(DtJobRunner* Runner)
{
    if (Runner->Pool != NULL)
    {
        DtAtomic_Decrement(&Runner->Pool->NumRunnersSized);
        DropRef(Runner->Pool);
    }
    OsEvent_Destroy(Runner->Done);
    DtJobRunner_Init(Runner);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtJobRunner_SetPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The new pool is held before the old one is let go, so that setting the pool a
// DtJobRunner already holds does not drop it on the way.
//
DtapiResult DtJobRunner_SetPool(DtJobRunner* Runner, DtWorkerPool* Pool, int NumThreads)
{
    if (NumThreads < 0)
        return DTAPI_E_INVALID_ARG;

    OsEvent* Done = NULL;
    if (Pool != NULL)
    {
        Done = OsEvent_Create();
        if (Done == NULL)
            return DTAPI_E_OUT_OF_MEM;
        DtWorkerPool_AddRef(Pool);
        DtAtomic_Increment(&Pool->NumRunnersSized);
    }
    DtJobRunner_Free(Runner);
    if (Pool == NULL)
        return DTAPI_OK;

    const int MaxPieces = DtWorkerPool_NumThreads(Pool);
    Runner->Pool = Pool;
    Runner->Done = Done;
    Runner->Pieces = NumThreads > 0 && NumThreads < MaxPieces ? NumThreads : MaxPieces;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtJobRunner_Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A job of one piece runs in the calling thread whatever the pool, since handing it on
// would cost more than the piece.
//
void DtJobRunner_Run(const DtJobRunner* Runner, DtJobFunc Func, void* Context)
{
    const DtWorkerPool* Pool = Runner->Pool;

    if (Pool == NULL || Runner->Pieces == 1 || Pool->NumThreads == 0)
    {
        for (int i = 0; i < Runner->Pieces; i++)
            Func(Context, i, Runner->Pieces);
        return;
    }
    if (Pool->Dispatch != NULL)
    {
        Pool->Dispatch(Pool->User, Func, Context, Runner->Pieces);
        return;
    }
    RunQueued(Runner->Pool, Runner->Done, Func, Context, Runner->Pieces);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtJobRunner_Split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtJobRunner_Split(int Total, int Index, int Count, int Unit, int* First, int* Last)
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

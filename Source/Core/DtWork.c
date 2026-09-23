// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtWork.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - A job in independent pieces, over the caller's threads or the library's own
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <string.h>

// CDTAPI includes
#include "DtAlloc.h"      // The library's allocator.
#include "DtAtomic.h"     // The pieces still to be taken, and the workers still running.
#include "DtWork.h"       // Interface being implemented.
#include "OAL/OsThread.h" // Threads and events.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The pool is one job at a time, which is all the library asks of it: a caller of
// DtWork_Run waits for the job it gave, so the next one cannot start before it returns.
// The pieces are taken from a counter rather than handed out, so that a piece which
// takes longer than the others does not hold up the pieces after it, and the thread that
// called does one of them itself rather than waiting for a worker to do it.
//

typedef struct DtWorkPool DtWorkPool;

typedef struct Worker
{
    DtWorkPool* Pool;
    OsEvent* Go; // Set for a job to do, and once more to stop
    OsThread* Thread;
} Worker;

struct DtWorkPool
{
    int Threads;    // The calling thread and Threads - 1 workers
    Worker* Worker; // Threads - 1 of them
    int Started;    // How many of them are running, which is all of them or none

    // The job. Written before the workers are woken and read after they are, so the
    // event is what publishes it.
    DtWorkFunc Func;
    void* Context;
    int Count;
    DtAtomicInt Next;    // The next piece to take
    DtAtomicInt Running; // The workers not yet finished with this job
    DtAtomicInt Stop;
    OsEvent* Done; // Set by the last worker to finish
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. TakePieces -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Does pieces of the job until none is left.
//
static void TakePieces(DtWorkPool* Pool)
{
    for (;;)
    {
        const int Index = DtAtomic_Increment(&Pool->Next) - 1;

        if (Index >= Pool->Count)
            return;
        Pool->Func(Pool->Context, Index, Pool->Count);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WorkerThread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void WorkerThread(void* Context)
{
    Worker* Self = (Worker*)Context;
    DtWorkPool* Pool = Self->Pool;

    for (;;)
    {
        if (OsEvent_Wait(Self->Go, -1) != OS_WAIT_SIGNALLED)
            return;
        if (DtAtomic_Load(&Pool->Stop) != 0)
            return;

        TakePieces(Pool);

        // The last one out sets the event, so that it is set once for a job however many
        // workers took part in it.
        if (DtAtomic_Decrement(&Pool->Running) == 0)
            OsEvent_Set(Pool->Done);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DestroyPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void DestroyPool(DtWorkPool* Pool)
{
    if (Pool == NULL)
        return;

    DtAtomic_Store(&Pool->Stop, 1);
    for (int i = 0; i < Pool->Started; i++)
    {
        OsEvent_Set(Pool->Worker[i].Go);
        OsThread_Join(Pool->Worker[i].Thread);
    }
    for (int i = 0; Pool->Worker != NULL && i < Pool->Threads - 1; i++)
        OsEvent_Destroy(Pool->Worker[i].Go);
    OsEvent_Destroy(Pool->Done);
    DtAlloc_Free(Pool->Worker);
    DtAlloc_Free(Pool);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. CreatePool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A pool for Threads threads counting the one that calls it, so Threads - 1 of its own.
// NULL when a thread or an event cannot be had, with everything it did take released.
//
static DtWorkPool* CreatePool(int Threads)
{
    DtWorkPool* Pool = (DtWorkPool*)DtAlloc_Malloc(sizeof(DtWorkPool));
    const int Helpers = Threads - 1;

    if (Pool == NULL)
        return NULL;
    memset(Pool, 0, sizeof(*Pool));
    Pool->Threads = Threads;
    DtAtomic_Init(&Pool->Next, 0);
    DtAtomic_Init(&Pool->Running, 0);
    DtAtomic_Init(&Pool->Stop, 0);
    Pool->Done = OsEvent_Create();
    Pool->Worker = (Worker*)DtAlloc_Malloc((size_t)Helpers * sizeof(Worker));
    if (Pool->Done == NULL || Pool->Worker == NULL)
    {
        DestroyPool(Pool);
        return NULL;
    }
    memset(Pool->Worker, 0, (size_t)Helpers * sizeof(Worker));

    for (int i = 0; i < Helpers; i++)
    {
        Pool->Worker[i].Pool = Pool;
        Pool->Worker[i].Go = OsEvent_Create();
        if (Pool->Worker[i].Go == NULL)
        {
            DestroyPool(Pool);
            return NULL;
        }
    }
    for (int i = 0; i < Helpers; i++)
    {
        Pool->Worker[i].Thread = OsThread_Start(WorkerThread, &Pool->Worker[i]);
        if (Pool->Worker[i].Thread == NULL)
        {
            DestroyPool(Pool);
            return NULL;
        }
        Pool->Started = i + 1;
    }
    return Pool;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PoolExecute -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The library's own pool as an executor. The calling thread takes pieces alongside the
// workers, so a pool of one thread is the calling thread and nothing else.
//
static void PoolExecute(void* User, DtWorkFunc Work, void* Context, int Count)
{
    DtWorkPool* Pool = (DtWorkPool*)User;
    const int Helpers = Pool->Started;

    if (Count < 1)
        return;
    Pool->Func = Work;
    Pool->Context = Context;
    Pool->Count = Count;
    DtAtomic_Store(&Pool->Next, 0);
    DtAtomic_Store(&Pool->Running, Helpers);
    for (int i = 0; i < Helpers; i++)
        OsEvent_Set(Pool->Worker[i].Go);

    TakePieces(Pool);

    if (Helpers > 0)
        OsEvent_Wait(Pool->Done, -1);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. DtWork_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWork_Init(DtWork* Work)
{
    memset(Work, 0, sizeof(*Work));
    Work->Pieces = 1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. DtWork_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWork_Free(DtWork* Work)
{
    DestroyPool(Work->Pool);
    DtWork_Init(Work);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- DtWork_SetThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtWork_SetThreads(DtWork* Work, int Threads)
{
    if (Threads < 1)
        return DTAPI_E_INVALID_ARG;

    DtWork_Free(Work);
    if (Threads == 1)
        return DTAPI_OK;

    Work->Pool = CreatePool(Threads);
    if (Work->Pool == NULL)
        return DTAPI_E_OUT_OF_MEM;
    Work->Execute = PoolExecute;
    Work->User = Work->Pool;
    Work->Pieces = Threads;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtWork_SetExecutor -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtWork_SetExecutor(DtWork* Work, DtExecuteFunc Execute, void* User,
                               int Pieces)
{
    if (Execute != NULL && Pieces < 1)
        return DTAPI_E_INVALID_ARG;

    DtWork_Free(Work);
    if (Execute == NULL)
        return DTAPI_OK;

    Work->Execute = Execute;
    Work->User = User;
    Work->Pieces = Pieces;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. DtWork_Run -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtWork_Run(const DtWork* Work, DtWorkFunc Func, void* Context)
{
    if (Work->Execute == NULL)
    {
        for (int i = 0; i < Work->Pieces; i++)
            Func(Context, i, Work->Pieces);
        return;
    }
    Work->Execute(Work->User, Func, Context, Work->Pieces);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. DtWork_Band -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtWork_Band(int Total, int Index, int Count, int* First, int* Last)
{
    const int Each = Total / Count;
    const int Over = Total % Count;

    *First = Index * Each + (Index < Over ? Index : Over);
    *Last = *First + Each + (Index < Over ? 1 : 0);
}

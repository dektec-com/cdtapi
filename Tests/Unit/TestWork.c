// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestWork.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Tests for the work pool: pieces, sharing, reference counts and the split
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "Core/DtAtomic.h" // The pieces counted from the pool's threads.
#include "Core/DtWork.h"   // Interface under test.
#include "DtTest.h"        // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

enum
{
    MAX_PIECES = 16,
    NUM_JOBS = 200, // Jobs a caller runs in the tests that share a pool
};

// How often each piece of one job ran, and with which count.
typedef struct Tally
{
    DtAtomicInt Runs[MAX_PIECES];
    DtAtomicInt WrongCount;
    int NumPieces;
} Tally;

static void TallyInit(Tally* T, int NumPieces)
{
    for (int i = 0; i < MAX_PIECES; i++)
        DtAtomic_Init(&T->Runs[i], 0);
    DtAtomic_Init(&T->WrongCount, 0);
    T->NumPieces = NumPieces;
}

static void CountPiece(void* Context, int Index, int Count)
{
    Tally* T = (Tally*)Context;

    if (Count != T->NumPieces || Index < 0 || Index >= Count)
        DtAtomic_Increment(&T->WrongCount);
    else
        DtAtomic_Increment(&T->Runs[Index]);
}

// True when every piece of the job ran once and was told the right count.
static int EachPieceOnce(Tally* T)
{
    if (DtAtomic_Load(&T->WrongCount) != 0)
        return 0;
    for (int i = 0; i < T->NumPieces; i++)
        if (DtAtomic_Load(&T->Runs[i]) != 1)
            return 0;
    return 1;
}

// A caller on a thread of its own that runs NUM_JOBS jobs on its DtWork, and counts the
// jobs whose pieces did not each run once.
typedef struct Caller
{
    DtWork* Work;
    int NumBad;
} Caller;

static void RunJobs(void* Context)
{
    Caller* C = (Caller*)Context;

    for (int j = 0; j < NUM_JOBS; j++)
    {
        Tally T;
        TallyInit(&T, DtWork_Pieces(C->Work));
        DtWork_Run(C->Work, CountPiece, &T);
        if (!EachPieceOnce(&T))
            C->NumBad++;
    }
}

// Runs NUM_JOBS jobs on each of two DtWorks at once, from two threads, and returns the
// jobs of both whose pieces did not each run once.
static int RunTwoCallers(DtWork* First, DtWork* Second)
{
    Caller A = {First, 0};
    Caller B = {Second, 0};
    OsThread* Thread = OsThread_Start(RunJobs, &B);

    if (Thread == NULL)
        return -1;
    RunJobs(&A);
    OsThread_Join(Thread);
    return A.NumBad + B.NumBad;
}

// A dispatch function of the program's: runs the pieces one after another in the thread
// that calls it, and counts the calls, and the most that were under way at once.
typedef struct Program
{
    DtAtomicInt NumCalls;
    DtAtomicInt NumInside;
    DtAtomicInt MaxInside;
} Program;

static void SerialDispatch(void* User, DtWorkFunc Work, void* Context, int Count)
{
    Program* P = (Program*)User;
    const int Inside = DtAtomic_Increment(&P->NumInside);

    DtAtomic_Increment(&P->NumCalls);
    if (Inside > DtAtomic_Load(&P->MaxInside))
        DtAtomic_Store(&P->MaxInside, Inside);
    for (int i = 0; i < Count; i++)
        Work(Context, i, Count);
    DtAtomic_Decrement(&P->NumInside);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pieces +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(WithoutPoolOnePieceInCallingThread)
{
    DtWork Work;
    Tally T;

    DtWork_Init(&Work);
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 1);
    TallyInit(&T, 1);
    DtWork_Run(&Work, CountPiece, &T);
    DT_ASSERT(EachPieceOnce(&T));
    DtWork_Free(&Work);
}

DT_TEST(PoolOfFourRunsEveryPieceOnce)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DtWork Work;

    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 4));
    DtWork_Init(&Work);
    DT_ASSERT_OK(DtWork_SetPool(&Work, Pool, 0));
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 4);

    for (int j = 0; j < NUM_JOBS; j++)
    {
        Tally T;
        TallyInit(&T, 4);
        DtWork_Run(&Work, CountPiece, &T);
        DT_ASSERT(EachPieceOnce(&T));
    }
    DtWork_Free(&Work);
    DtWorkPool_Freep(&Pool);
    DT_ASSERT(Pool == NULL);
}

DT_TEST(PiecesAreCappedByThePool)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DtWorkPool* Bare = DtWorkPool_Alloc();
    DtWork Work;

    DT_ASSERT(Pool != NULL && Bare != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 2));
    DtWork_Init(&Work);

    // 0 is as many as the pool runs at once; more than that is cut to it; fewer stays.
    DT_ASSERT_OK(DtWork_SetPool(&Work, Pool, 0));
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 2);
    DT_ASSERT_OK(DtWork_SetPool(&Work, Pool, 8));
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 2);
    DT_ASSERT_OK(DtWork_SetPool(&Work, Pool, 1));
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 1);

    // A pool with neither threads nor a dispatch function runs one piece.
    DT_ASSERT_OK(DtWork_SetPool(&Work, Bare, 0));
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 1);
    DT_ASSERT_OK(DtWork_SetPool(&Work, NULL, 0));
    DT_ASSERT_EQ(DtWork_Pieces(&Work), 1);
    DT_ASSERT_EQ(DtWork_SetPool(&Work, Pool, -1), DTAPI_E_INVALID_ARG);

    DtWork_Free(&Work);
    DtWorkPool_Free(Pool);
    DtWorkPool_Free(Bare);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sharing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(TwoCallersShareAPoolOfEight)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DtWork First;
    DtWork Second;

    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 8));
    DtWork_Init(&First);
    DtWork_Init(&Second);
    DT_ASSERT_OK(DtWork_SetPool(&First, Pool, 4));
    DT_ASSERT_OK(DtWork_SetPool(&Second, Pool, 4));

    DT_ASSERT_EQ(RunTwoCallers(&First, &Second), 0);

    DtWork_Free(&First);
    DtWork_Free(&Second);
    DtWorkPool_Free(Pool);
}

DT_TEST(TwoCallersShareAPoolSmallerThanTheirPieces)
{
    // Three threads for two jobs of three pieces each, so pieces wait in the queue.
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DtWork First;
    DtWork Second;

    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 3));
    DtWork_Init(&First);
    DtWork_Init(&Second);
    DT_ASSERT_OK(DtWork_SetPool(&First, Pool, 0));
    DT_ASSERT_OK(DtWork_SetPool(&Second, Pool, 0));
    DT_ASSERT_EQ(DtWork_Pieces(&First), 3);

    DT_ASSERT_EQ(RunTwoCallers(&First, &Second), 0);

    DtWork_Free(&First);
    DtWork_Free(&Second);
    DtWorkPool_Free(Pool);
}

DT_TEST(ProgramDispatchIsCalledFromTwoCallers)
{
    Program P;
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DtWork First;
    DtWork Second;

    DtAtomic_Init(&P.NumCalls, 0);
    DtAtomic_Init(&P.NumInside, 0);
    DtAtomic_Init(&P.MaxInside, 0);
    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_SetDispatch(Pool, SerialDispatch, &P, 4));
    DtWork_Init(&First);
    DtWork_Init(&Second);
    DT_ASSERT_OK(DtWork_SetPool(&First, Pool, 0));
    DT_ASSERT_OK(DtWork_SetPool(&Second, Pool, 2));
    DT_ASSERT_EQ(DtWork_Pieces(&First), 4);
    DT_ASSERT_EQ(DtWork_Pieces(&Second), 2);

    DT_ASSERT_EQ(RunTwoCallers(&First, &Second), 0);

    // Every job went to the dispatch function; whether two were ever inside it at once is
    // the scheduler's to decide, so only that it never saw more than the two callers.
    DT_ASSERT_EQ(DtAtomic_Load(&P.NumCalls), 2 * NUM_JOBS);
    DT_ASSERT(DtAtomic_Load(&P.MaxInside) <= 2);

    DtWork_Free(&First);
    DtWork_Free(&Second);
    DtWorkPool_Free(Pool);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(PoolFreedByProgramLivesWhileHeld)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    DtWork Work;
    Tally T;

    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 2));
    DtWork_Init(&Work);
    DT_ASSERT_OK(DtWork_SetPool(&Work, Pool, 0));
    DtWorkPool_Freep(&Pool);

    // The DtWork's hold keeps the threads; ASan reports it if it did not.
    TallyInit(&T, 2);
    DtWork_Run(&Work, CountPiece, &T);
    DT_ASSERT(EachPieceOnce(&T));

    // Setting the same pool again keeps it rather than dropping it on the way.
    DT_ASSERT_OK(DtWork_SetPool(&Work, Work.Pool, 0));
    TallyInit(&T, 2);
    DtWork_Run(&Work, CountPiece, &T);
    DT_ASSERT(EachPieceOnce(&T));

    DtWork_Free(&Work);
    DtWork_Free(&Work);
}

DT_TEST(HeldPoolRefusesToChange)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    Program P;
    DtWork Work;

    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 2));
    DtWork_Init(&Work);
    DT_ASSERT_OK(DtWork_SetPool(&Work, Pool, 0));

    DT_ASSERT_EQ(DtWorkPool_StartThreads(Pool, 4), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtWorkPool_SetDispatch(Pool, SerialDispatch, &P, 4), DTAPI_E_IN_USE);
    DT_ASSERT_EQ(DtWorkPool_NumThreads(Pool), 2);

    // Once no DtWork holds it, it changes again.
    DtWork_Free(&Work);
    DT_ASSERT_OK(DtWorkPool_StartThreads(Pool, 4));
    DT_ASSERT_EQ(DtWorkPool_NumThreads(Pool), 4);
    DT_ASSERT_OK(DtWorkPool_SetDispatch(Pool, NULL, NULL, 0));
    DT_ASSERT_EQ(DtWorkPool_NumThreads(Pool), 1);
    DtWorkPool_Free(Pool);
}

DT_TEST(PoolRefusesWhatIsInvalid)
{
    DtWorkPool* Pool = DtWorkPool_Alloc();
    Program P;

    DT_ASSERT(Pool != NULL);
    DT_ASSERT_EQ(DtWorkPool_StartThreads(Pool, 0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtWorkPool_StartThreads(NULL, 2), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtWorkPool_SetDispatch(Pool, SerialDispatch, &P, 0),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtWorkPool_NumThreads(Pool), 1);
    DtWorkPool_Free(Pool);
    DtWorkPool_Free(NULL);
    DtWorkPool_Freep(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Split +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(SplitCoversEveryItemOnce)
{
    // Every total up to 40 in 1 to 9 pieces with units of 1 to 8: the ranges follow one
    // another from 0 to Total, and every boundary but Total is a multiple of the unit.
    for (int Total = 0; Total <= 40; Total++)
        for (int Count = 1; Count <= 9; Count++)
            for (int Unit = 1; Unit <= 8; Unit++)
            {
                int Expected = 0;
                for (int Index = 0; Index < Count; Index++)
                {
                    int First = -1;
                    int Last = -1;
                    DtWork_Split(Total, Index, Count, Unit, &First, &Last);
                    DT_ASSERT_EQ(First, Expected);
                    DT_ASSERT(Last >= First);
                    DT_ASSERT(Last == Total || Last % Unit == 0);
                    Expected = Last;
                }
                DT_ASSERT_EQ(Expected, Total);
            }
}

DT_TEST(SplitIsEvenToAUnit)
{
    int First = 0;
    int Last = 0;

    // 10 units in 4 pieces: 3, 3, 2 and 2.
    DtWork_Split(20, 0, 4, 2, &First, &Last);
    DT_ASSERT_EQ(First, 0);
    DT_ASSERT_EQ(Last, 6);
    DtWork_Split(20, 3, 4, 2, &First, &Last);
    DT_ASSERT_EQ(First, 16);
    DT_ASSERT_EQ(Last, 20);

    // Fewer units than pieces leaves the last pieces empty.
    DtWork_Split(2, 3, 4, 1, &First, &Last);
    DT_ASSERT_EQ(First, 2);
    DT_ASSERT_EQ(Last, 2);
}

DT_TEST_MAIN("Work", DT_RUN(WithoutPoolOnePieceInCallingThread),
             DT_RUN(PoolOfFourRunsEveryPieceOnce), DT_RUN(PiecesAreCappedByThePool),
             DT_RUN(TwoCallersShareAPoolOfEight),
             DT_RUN(TwoCallersShareAPoolSmallerThanTheirPieces),
             DT_RUN(ProgramDispatchIsCalledFromTwoCallers),
             DT_RUN(PoolFreedByProgramLivesWhileHeld), DT_RUN(HeldPoolRefusesToChange),
             DT_RUN(PoolRefusesWhatIsInvalid), DT_RUN(SplitCoversEveryItemOnce),
             DT_RUN(SplitIsEvenToAUnit))

// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Tests for threads, events and mutexes on the host platform
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Timing assertions use generous bounds on purpose. They check that a wait waits and
// that a wake-up wakes, not how precise the scheduler is; a tight bound would fail on a
// loaded build machine and teach people to ignore this suite.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <time.h>

// CDtapiLite includes
#include "DtlTest.h"      // Test framework.
#include "OAL/OsThread.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Milliseconds on a clock that only moves forward, for measuring how long a wait took.
static long long NowMs(void)
{
    struct timespec Ts;

    timespec_get(&Ts, TIME_UTC);
    return (long long)Ts.tv_sec * 1000 + Ts.tv_nsec / 1000000;
}

// Pauses the calling thread, using an event that is never set.
static void PauseMs(int Ms)
{
    OsEvent* Never = OsEventCreate();

    if (Never != NULL)
    {
        OsEventWait(Never, Ms);
        OsEventDestroy(Never);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

static void SetFlag(void* Context)
{
    *(int*)Context = 42;
}

DTL_TEST(ThreadRunsWithItsContextAndJoins)
{
    int Flag = 0;
    OsThread* Thread = OsThreadStart(SetFlag, &Flag);

    DTL_ASSERT(Thread != NULL);
    OsThreadJoin(Thread);

    // Join waits for the function to return, so the write is complete by now.
    DTL_ASSERT_EQ(Flag, 42);
}

DTL_TEST(StartRejectsMissingFunction)
{
    DTL_ASSERT(OsThreadStart(NULL, NULL) == NULL);
    OsThreadJoin(NULL);
}

DTL_TEST(RaisingPriorityDoesNotFailHere)
{
    // Allowed to be refused on Linux without privilege, but not to misbehave. On Windows
    // HIGHEST needs no privilege, so there it has to succeed.
    int Result = OsThreadRaisePriority();

#if defined(_WIN32)
    DTL_ASSERT_EQ(Result, 0);
#else
    DTL_ASSERT(Result == 0 || Result == -1);
#endif
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Event +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(UnsetEventTimesOut)
{
    OsEvent* Event = OsEventCreate();
    long long Start;

    DTL_ASSERT(Event != NULL);

    Start = NowMs();
    DTL_ASSERT_EQ(OsEventWait(Event, 50), OS_WAIT_TIMEOUT);

    // It really waited. The lower bound allows for a coarse timer tick.
    DTL_ASSERT(NowMs() - Start >= 30);

    OsEventDestroy(Event);
}

// A set that happens before anyone waits must be remembered, not lost. That is the
// difference between an event and a bare condition variable.
DTL_TEST(SetBeforeWaitIsRemembered)
{
    OsEvent* Event = OsEventCreate();

    DTL_ASSERT(Event != NULL);

    OsEventSet(Event);
    DTL_ASSERT_EQ(OsEventWait(Event, 0), OS_WAIT_SIGNALLED);

    OsEventDestroy(Event);
}

DTL_TEST(EventResetsAfterOneWake)
{
    OsEvent* Event = OsEventCreate();

    DTL_ASSERT(Event != NULL);

    OsEventSet(Event);
    DTL_ASSERT_EQ(OsEventWait(Event, 0), OS_WAIT_SIGNALLED);
    DTL_ASSERT_EQ(OsEventWait(Event, 20), OS_WAIT_TIMEOUT);

    OsEventDestroy(Event);
}

DTL_TEST(SettingTwiceCountsOnce)
{
    OsEvent* Event = OsEventCreate();

    DTL_ASSERT(Event != NULL);

    OsEventSet(Event);
    OsEventSet(Event);
    DTL_ASSERT_EQ(OsEventWait(Event, 0), OS_WAIT_SIGNALLED);
    DTL_ASSERT_EQ(OsEventWait(Event, 20), OS_WAIT_TIMEOUT);

    OsEventDestroy(Event);
}

typedef struct DelayedSet
{
    OsEvent* Event;
    int DelayMs;
} DelayedSet;

static void SetAfterDelay(void* Context)
{
    DelayedSet* Job = (DelayedSet*)Context;

    PauseMs(Job->DelayMs);
    OsEventSet(Job->Event);
}

DTL_TEST(SetFromAnotherThreadWakesTheWaiter)
{
    DelayedSet Job;
    OsThread* Thread;
    long long Start;

    Job.Event = OsEventCreate();
    Job.DelayMs = 30;
    DTL_ASSERT(Job.Event != NULL);

    Start = NowMs();
    Thread = OsThreadStart(SetAfterDelay, &Job);
    DTL_ASSERT(Thread != NULL);

    // Woken by the other thread, long before the five-second timeout.
    DTL_ASSERT_EQ(OsEventWait(Job.Event, 5000), OS_WAIT_SIGNALLED);
    DTL_ASSERT(NowMs() - Start < 4000);

    OsThreadJoin(Thread);
    OsEventDestroy(Job.Event);
}

DTL_TEST(NullEventIsAccepted)
{
    DTL_ASSERT_EQ(OsEventWait(NULL, 0), OS_WAIT_ERROR);
    OsEventSet(NULL);
    OsEventDestroy(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Kill pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The shape a channel's receive thread will take: poll on a short timeout, do some work
// each time round, and stop when the kill event is set. DTAPI's DMA thread in
// AsiSdiInpChannel_Bb2.cpp has exactly this loop.
//

typedef struct Worker
{
    OsEvent* Kill;
    int Rounds;
} Worker;

static void PollUntilKilled(void* Context)
{
    Worker* Self = (Worker*)Context;

    while (OsEventWait(Self->Kill, 10) == OS_WAIT_TIMEOUT)
        Self->Rounds++;
}

DTL_TEST(KillEventStopsAPollingThread)
{
    Worker Self;
    OsThread* Thread;
    long long Start;

    Self.Kill = OsEventCreate();
    Self.Rounds = 0;
    DTL_ASSERT(Self.Kill != NULL);

    Thread = OsThreadStart(PollUntilKilled, &Self);
    DTL_ASSERT(Thread != NULL);

    PauseMs(80);

    Start = NowMs();
    OsEventSet(Self.Kill);
    OsThreadJoin(Thread);

    // It ran for a while, and it stopped promptly once told to.
    DTL_ASSERT(Self.Rounds > 0);
    DTL_ASSERT(NowMs() - Start < 2000);

    OsEventDestroy(Self.Kill);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Mutex +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define INCREMENTS_PER_THREAD 200000
#define INCREMENT_THREADS 4

typedef struct Counter
{
    OsMutex* Lock;
    long Value;
} Counter;

static void IncrementManyTimes(void* Context)
{
    Counter* Shared = (Counter*)Context;
    int i;

    for (i = 0; i < INCREMENTS_PER_THREAD; i++)
    {
        OsMutexLock(Shared->Lock);
        Shared->Value++;
        OsMutexUnlock(Shared->Lock);
    }
}

// Four threads each increment a shared counter many times. Under the lock no increment
// is lost, so the total is exact. Without it, lost updates would almost certainly show.
DTL_TEST(MutexLosesNoUpdates)
{
    Counter Shared;
    OsThread* Threads[INCREMENT_THREADS];
    int i;

    Shared.Lock = OsMutexCreate();
    Shared.Value = 0;
    DTL_ASSERT(Shared.Lock != NULL);

    for (i = 0; i < INCREMENT_THREADS; i++)
    {
        Threads[i] = OsThreadStart(IncrementManyTimes, &Shared);
        DTL_ASSERT(Threads[i] != NULL);
    }

    for (i = 0; i < INCREMENT_THREADS; i++)
        OsThreadJoin(Threads[i]);

    DTL_ASSERT_EQ(Shared.Value, (long)INCREMENT_THREADS * INCREMENTS_PER_THREAD);

    OsMutexDestroy(Shared.Lock);
}

DTL_TEST(MutexDestroyAcceptsNull)
{
    OsMutex* Lock = OsMutexCreate();

    DTL_ASSERT(Lock != NULL);
    OsMutexDestroy(Lock);
    OsMutexDestroy(NULL);
}

DTL_TEST_MAIN("Thread", DTL_RUN(ThreadRunsWithItsContextAndJoins),
              DTL_RUN(StartRejectsMissingFunction),
              DTL_RUN(RaisingPriorityDoesNotFailHere), DTL_RUN(UnsetEventTimesOut),
              DTL_RUN(SetBeforeWaitIsRemembered), DTL_RUN(EventResetsAfterOneWake),
              DTL_RUN(SettingTwiceCountsOnce),
              DTL_RUN(SetFromAnotherThreadWakesTheWaiter), DTL_RUN(NullEventIsAccepted),
              DTL_RUN(KillEventStopsAPollingThread), DTL_RUN(MutexLosesNoUpdates),
              DTL_RUN(MutexDestroyAcceptsNull))

// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Tests for threads, events, mutexes and time on the host platform
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
#include "DtTest.h"       // Test framework.
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

DT_TEST(ThreadRunsWithItsContextAndJoins)
{
    int Flag = 0;
    OsThread* Thread = OsThreadStart(SetFlag, &Flag);

    DT_ASSERT(Thread != NULL);
    OsThreadJoin(Thread);

    // Join waits for the function to return, so the write is complete by now.
    DT_ASSERT_EQ(Flag, 42);
}

DT_TEST(StartRejectsMissingFunction)
{
    DT_ASSERT(OsThreadStart(NULL, NULL) == NULL);
    OsThreadJoin(NULL);
}

DT_TEST(RaisingPriorityDoesNotFailHere)
{
    // Allowed to be refused on Linux without privilege, but not to misbehave. On Windows
    // HIGHEST needs no privilege, so there it has to succeed.
    int Result = OsThreadRaisePriority();

#if defined(_WIN32)
    DT_ASSERT_EQ(Result, 0);
#else
    DT_ASSERT(Result == 0 || Result == -1);
#endif
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Event +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(UnsetEventTimesOut)
{
    OsEvent* Event = OsEventCreate();
    long long Start;

    DT_ASSERT(Event != NULL);

    Start = NowMs();
    DT_ASSERT_EQ(OsEventWait(Event, 50), OS_WAIT_TIMEOUT);

    // It really waited. The lower bound allows for a coarse timer tick.
    DT_ASSERT(NowMs() - Start >= 30);

    OsEventDestroy(Event);
}

// A set that happens before anyone waits must be remembered, not lost. That is the
// difference between an event and a bare condition variable.
DT_TEST(SetBeforeWaitIsRemembered)
{
    OsEvent* Event = OsEventCreate();

    DT_ASSERT(Event != NULL);

    OsEventSet(Event);
    DT_ASSERT_EQ(OsEventWait(Event, 0), OS_WAIT_SIGNALLED);

    OsEventDestroy(Event);
}

DT_TEST(EventResetsAfterOneWake)
{
    OsEvent* Event = OsEventCreate();

    DT_ASSERT(Event != NULL);

    OsEventSet(Event);
    DT_ASSERT_EQ(OsEventWait(Event, 0), OS_WAIT_SIGNALLED);
    DT_ASSERT_EQ(OsEventWait(Event, 20), OS_WAIT_TIMEOUT);

    OsEventDestroy(Event);
}

DT_TEST(SettingTwiceCountsOnce)
{
    OsEvent* Event = OsEventCreate();

    DT_ASSERT(Event != NULL);

    OsEventSet(Event);
    OsEventSet(Event);
    DT_ASSERT_EQ(OsEventWait(Event, 0), OS_WAIT_SIGNALLED);
    DT_ASSERT_EQ(OsEventWait(Event, 20), OS_WAIT_TIMEOUT);

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

DT_TEST(SetFromAnotherThreadWakesTheWaiter)
{
    DelayedSet Job;
    OsThread* Thread;
    long long Start;

    Job.Event = OsEventCreate();
    Job.DelayMs = 30;
    DT_ASSERT(Job.Event != NULL);

    Start = NowMs();
    Thread = OsThreadStart(SetAfterDelay, &Job);
    DT_ASSERT(Thread != NULL);

    // Woken by the other thread, long before the five-second timeout.
    DT_ASSERT_EQ(OsEventWait(Job.Event, 5000), OS_WAIT_SIGNALLED);
    DT_ASSERT(NowMs() - Start < 4000);

    OsThreadJoin(Thread);
    OsEventDestroy(Job.Event);
}

DT_TEST(NullEventIsAccepted)
{
    DT_ASSERT_EQ(OsEventWait(NULL, 0), OS_WAIT_ERROR);
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

DT_TEST(KillEventStopsAPollingThread)
{
    Worker Self;
    OsThread* Thread;
    long long Start;

    Self.Kill = OsEventCreate();
    Self.Rounds = 0;
    DT_ASSERT(Self.Kill != NULL);

    Thread = OsThreadStart(PollUntilKilled, &Self);
    DT_ASSERT(Thread != NULL);

    PauseMs(80);

    Start = NowMs();
    OsEventSet(Self.Kill);
    OsThreadJoin(Thread);

    // It ran for a while, and it stopped promptly once told to.
    DT_ASSERT(Self.Rounds > 0);
    DT_ASSERT(NowMs() - Start < 2000);

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
DT_TEST(MutexLosesNoUpdates)
{
    Counter Shared;
    OsThread* Threads[INCREMENT_THREADS];
    int i;

    Shared.Lock = OsMutexCreate();
    Shared.Value = 0;
    DT_ASSERT(Shared.Lock != NULL);

    for (i = 0; i < INCREMENT_THREADS; i++)
    {
        Threads[i] = OsThreadStart(IncrementManyTimes, &Shared);
        DT_ASSERT(Threads[i] != NULL);
    }

    for (i = 0; i < INCREMENT_THREADS; i++)
        OsThreadJoin(Threads[i]);

    DT_ASSERT_EQ(Shared.Value, (long)INCREMENT_THREADS * INCREMENTS_PER_THREAD);

    OsMutexDestroy(Shared.Lock);
}

DT_TEST(MutexDestroyAcceptsNull)
{
    OsMutex* Lock = OsMutexCreate();

    DT_ASSERT(Lock != NULL);
    OsMutexDestroy(Lock);
    OsMutexDestroy(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A sleep lasts at least as long as asked, measured on the monotonic clock. The clock may
// tick in steps of a scheduler tick, up to about 16 ms on Windows, so the lower bound
// allows for one step.
DT_TEST(SleepIsMeasuredByTheClock)
{
    uint64_t Start = OsMonotonicMs();
    uint64_t Elapsed;

    OsSleepMs(60);
    Elapsed = OsMonotonicMs() - Start;
    if (Elapsed < 60 - 17 || Elapsed > 5000)
        DT_FAIL("a 60 ms sleep took %llu ms", (unsigned long long)Elapsed);
}

// No sleep for zero or a negative time, and the clock does not run backwards.
DT_TEST(NoSleepForNothing)
{
    uint64_t Start = OsMonotonicMs();
    uint64_t Last = Start;
    int i;

    for (i = 0; i < 1000; i++)
    {
        uint64_t Now;

        OsSleepMs(0);
        OsSleepMs(-10);
        Now = OsMonotonicMs();
        DT_ASSERT(Now >= Last);
        Last = Now;
    }
    DT_ASSERT(Last - Start < 1000);
}

DT_TEST_MAIN("Thread", DT_RUN(ThreadRunsWithItsContextAndJoins),
             DT_RUN(StartRejectsMissingFunction), DT_RUN(RaisingPriorityDoesNotFailHere),
             DT_RUN(UnsetEventTimesOut), DT_RUN(SetBeforeWaitIsRemembered),
             DT_RUN(EventResetsAfterOneWake), DT_RUN(SettingTwiceCountsOnce),
             DT_RUN(SetFromAnotherThreadWakesTheWaiter), DT_RUN(NullEventIsAccepted),
             DT_RUN(KillEventStopsAPollingThread), DT_RUN(MutexLosesNoUpdates),
             DT_RUN(MutexDestroyAcceptsNull), DT_RUN(SleepIsMeasuredByTheClock),
             DT_RUN(NoSleepForNothing))

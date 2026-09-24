// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Tests for threads, events, mutexes and time on the host platform
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Timing assertions use generous bounds on purpose. They check that a wait waits and
// that a wake-up wakes, not how precise the scheduler is; a tight bound would fail on a
// loaded build machine and teach people to ignore this suite.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h"       // Test framework.
#include "OAL/OsThread.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Milliseconds on a clock that only moves forward, for measuring how long a wait took.
static int64_t NowMs(void)
{
    return (int64_t)OsTime_MonotonicMs();
}

// Pauses the calling thread, using an event that is never set.
static void PauseMs(int Ms)
{
    OsEvent* Never = OsEvent_Create();

    if (Never != NULL)
    {
        OsEvent_Wait(Never, Ms);
        OsEvent_Destroy(Never);
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
    OsThread* Thread = OsThread_Start(SetFlag, &Flag);

    DT_ASSERT(Thread != NULL);
    OsThread_Join(Thread);

    // Join waits for the function to return, so the write is complete by now.
    DT_ASSERT_EQ(Flag, 42);
}

DT_TEST(StartRejectsMissingFunction)
{
    DT_ASSERT(OsThread_Start(NULL, NULL) == NULL);
    OsThread_Join(NULL);
}

DT_TEST(RaisingPriorityDoesNotFailHere)
{
    // Allowed to be refused on Linux without privilege, but not to misbehave. On Windows
    // HIGHEST needs no privilege, so there it has to succeed.
    int Result = OsThread_RaisePriority();

#if defined(_WIN32)
    DT_ASSERT_EQ(Result, 0);
#else
    DT_ASSERT(Result == 0 || Result == -1);
#endif
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Event +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(UnsetEventTimesOut)
{
    OsEvent* Event = OsEvent_Create();

    DT_ASSERT(Event != NULL);

    int64_t Start = NowMs();
    DT_ASSERT_EQ(OsEvent_Wait(Event, 50), OS_WAIT_TIMEOUT);

    // It really waited. The lower bound allows for a coarse timer tick.
    DT_ASSERT(NowMs() - Start >= 30);

    OsEvent_Destroy(Event);
}

// A set that happens before anyone waits must be remembered, not lost. That is the
// difference between an event and a bare condition variable.
DT_TEST(SetBeforeWaitIsRemembered)
{
    OsEvent* Event = OsEvent_Create();

    DT_ASSERT(Event != NULL);

    OsEvent_Set(Event);
    DT_ASSERT_EQ(OsEvent_Wait(Event, 0), OS_WAIT_SIGNALLED);

    OsEvent_Destroy(Event);
}

DT_TEST(EventResetsAfterOneWake)
{
    OsEvent* Event = OsEvent_Create();

    DT_ASSERT(Event != NULL);

    OsEvent_Set(Event);
    DT_ASSERT_EQ(OsEvent_Wait(Event, 0), OS_WAIT_SIGNALLED);
    DT_ASSERT_EQ(OsEvent_Wait(Event, 20), OS_WAIT_TIMEOUT);

    OsEvent_Destroy(Event);
}

DT_TEST(SettingTwiceCountsOnce)
{
    OsEvent* Event = OsEvent_Create();

    DT_ASSERT(Event != NULL);

    OsEvent_Set(Event);
    OsEvent_Set(Event);
    DT_ASSERT_EQ(OsEvent_Wait(Event, 0), OS_WAIT_SIGNALLED);
    DT_ASSERT_EQ(OsEvent_Wait(Event, 20), OS_WAIT_TIMEOUT);

    OsEvent_Destroy(Event);
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
    OsEvent_Set(Job->Event);
}

DT_TEST(SetFromAnotherThreadWakesTheWaiter)
{
    DelayedSet Job;

    Job.Event = OsEvent_Create();
    Job.DelayMs = 30;
    DT_ASSERT(Job.Event != NULL);

    int64_t Start = NowMs();
    OsThread* Thread = OsThread_Start(SetAfterDelay, &Job);
    DT_ASSERT(Thread != NULL);

    // Woken by the other thread, long before the five-second timeout.
    DT_ASSERT_EQ(OsEvent_Wait(Job.Event, 5000), OS_WAIT_SIGNALLED);
    DT_ASSERT(NowMs() - Start < 4000);

    OsThread_Join(Thread);
    OsEvent_Destroy(Job.Event);
}

DT_TEST(NullEventIsAccepted)
{
    DT_ASSERT_EQ(OsEvent_Wait(NULL, 0), OS_WAIT_ERROR);
    OsEvent_Set(NULL);
    OsEvent_Destroy(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Kill pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The shape a channel's receive thread will take: poll on a short timeout, do some work
// each time round, and stop when the kill event is set.
//

typedef struct Worker
{
    OsEvent* Kill;
    int Rounds;
} Worker;

static void PollUntilKilled(void* Context)
{
    Worker* Self = (Worker*)Context;

    while (OsEvent_Wait(Self->Kill, 10) == OS_WAIT_TIMEOUT)
        Self->Rounds++;
}

DT_TEST(KillEventStopsAPollingThread)
{
    Worker Self;

    Self.Kill = OsEvent_Create();
    Self.Rounds = 0;
    DT_ASSERT(Self.Kill != NULL);

    OsThread* Thread = OsThread_Start(PollUntilKilled, &Self);
    DT_ASSERT(Thread != NULL);

    PauseMs(80);

    int64_t Start = NowMs();
    OsEvent_Set(Self.Kill);
    OsThread_Join(Thread);

    // It ran for a while, and it stopped promptly once told to.
    DT_ASSERT(Self.Rounds > 0);
    DT_ASSERT(NowMs() - Start < 2000);

    OsEvent_Destroy(Self.Kill);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Mutex +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define INCREMENTS_PER_THREAD 200000
#define INCREMENT_THREADS 4

typedef struct Counter
{
    OsMutex* Lock;
    int32_t Value;
} Counter;

static void IncrementManyTimes(void* Context)
{
    Counter* Shared = (Counter*)Context;
    int i;

    for (i = 0; i < INCREMENTS_PER_THREAD; i++)
    {
        OsMutex_Lock(Shared->Lock);
        Shared->Value++;
        OsMutex_Unlock(Shared->Lock);
    }
}

// Four threads each increment a shared counter many times. Under the lock no increment
// is lost, so the total is exact. Without it, lost updates would almost certainly show.
DT_TEST(MutexLosesNoUpdates)
{
    Counter Shared;

    Shared.Lock = OsMutex_Create();
    Shared.Value = 0;
    DT_ASSERT(Shared.Lock != NULL);

    OsThread* Threads[INCREMENT_THREADS];
    int i;
    for (i = 0; i < INCREMENT_THREADS; i++)
    {
        Threads[i] = OsThread_Start(IncrementManyTimes, &Shared);
        DT_ASSERT(Threads[i] != NULL);
    }

    for (i = 0; i < INCREMENT_THREADS; i++)
        OsThread_Join(Threads[i]);

    DT_ASSERT_EQ(Shared.Value, INCREMENT_THREADS * INCREMENTS_PER_THREAD);

    OsMutex_Destroy(Shared.Lock);
}

DT_TEST(MutexDestroyAcceptsNull)
{
    OsMutex* Lock = OsMutex_Create();

    DT_ASSERT(Lock != NULL);
    OsMutex_Destroy(Lock);
    OsMutex_Destroy(NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// A sleep lasts at least as long as asked, measured on the monotonic clock. The clock may
// tick in steps of a scheduler tick, up to about 16 ms on Windows, so the lower bound
// allows for one step.
DT_TEST(SleepIsMeasuredByTheClock)
{
    uint64_t Start = OsTime_MonotonicMs();

    OsTime_SleepMs(60);
    uint64_t Elapsed = OsTime_MonotonicMs() - Start;
    if (Elapsed < 60 - 17 || Elapsed > 5000)
        DT_FAIL("a 60 ms sleep took %llu ms", (unsigned long long)Elapsed);
}

// No sleep for zero or a negative time, and the clock does not run backwards.
DT_TEST(NoSleepForNothing)
{
    uint64_t Start = OsTime_MonotonicMs();
    uint64_t Last = Start;

    for (int i = 0; i < 1000; i++)
    {
        OsTime_SleepMs(0);
        OsTime_SleepMs(-10);
        uint64_t Now = OsTime_MonotonicMs();
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

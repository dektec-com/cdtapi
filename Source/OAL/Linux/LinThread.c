// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Threads, events, mutexes and time on Linux
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The deadline arithmetic the timed wait depends on is in LinTime.c and is tested on
// every platform.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes the POSIX and Linux calls the backend makes.
#define _GNU_SOURCE

// Standard includes
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "LinTime.h"      // Deadline arithmetic.
#include "OAL/OsThread.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

struct OsThread
{
    pthread_t Handle;
    OsThreadFunc Func;
    void* Context;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ThreadEntry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* ThreadEntry(void* Arg)
{
    OsThread* Thread = (OsThread*)Arg;

    Thread->Func(Thread->Context);
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThreadStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
OsThread* OsThreadStart(OsThreadFunc Func, void* Context)
{
    OsThread* Thread;

    if (Func == NULL)
        return NULL;

    Thread = (OsThread*)DtMalloc(sizeof(OsThread));
    if (Thread == NULL)
        return NULL;

    Thread->Func = Func;
    Thread->Context = Context;

    if (pthread_create(&Thread->Handle, NULL, ThreadEntry, Thread) != 0)
    {
        DtFree(Thread);
        return NULL;
    }

    return Thread;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThreadJoin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsThreadJoin(OsThread* Thread)
{
    if (Thread == NULL)
        return;

    pthread_join(Thread->Handle, NULL);
    DtFree(Thread);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThreadRaisePriority -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A modest real-time priority under SCHED_FIFO. Without CAP_SYS_NICE or a matching
// rlimit the kernel refuses, which is reported rather than treated as an error.
//
int OsThreadRaisePriority(void)
{
    struct sched_param Param;

    Param.sched_priority = 20;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &Param) == 0 ? 0 : -1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Event +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// POSIX has no event object, so one is built from a mutex, a condition variable and a
// flag. The flag is what makes it an event rather than a bare condition: a set that
// happens before anyone waits is remembered instead of lost.
//
// The condition variable runs on CLOCK_MONOTONIC, so that a wall-clock adjustment, such
// as an NTP step, can neither cut a wait short nor stretch it out.
//

struct OsEvent
{
    pthread_mutex_t Mutex;
    pthread_cond_t Cond;
    int Signalled;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
OsEvent* OsEventCreate(void)
{
    pthread_condattr_t Attr;
    OsEvent* Event = (OsEvent*)DtMalloc(sizeof(OsEvent));

    if (Event == NULL)
        return NULL;

    if (pthread_mutex_init(&Event->Mutex, NULL) != 0)
        goto FreeEvent;

    if (pthread_condattr_init(&Attr) != 0)
        goto DestroyMutex;

    if (pthread_condattr_setclock(&Attr, CLOCK_MONOTONIC) != 0 ||
        pthread_cond_init(&Event->Cond, &Attr) != 0)
    {
        pthread_condattr_destroy(&Attr);
        goto DestroyMutex;
    }

    pthread_condattr_destroy(&Attr);
    Event->Signalled = 0;
    return Event;

DestroyMutex:
    pthread_mutex_destroy(&Event->Mutex);
FreeEvent:
    DtFree(Event);
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventDestroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsEventDestroy(OsEvent* Event)
{
    if (Event == NULL)
        return;

    pthread_cond_destroy(&Event->Cond);
    pthread_mutex_destroy(&Event->Mutex);
    DtFree(Event);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventSet -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsEventSet(OsEvent* Event)
{
    if (Event == NULL)
        return;

    pthread_mutex_lock(&Event->Mutex);
    Event->Signalled = 1;
    // One waiter, to match the auto-reset semantics: it consumes the flag.
    pthread_cond_signal(&Event->Cond);
    pthread_mutex_unlock(&Event->Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int OsEventWait(OsEvent* Event, int TimeoutMs)
{
    struct timespec Now;
    struct timespec Deadline;
    int64_t DeadlineSec;
    long DeadlineNsec;
    int Result = OS_WAIT_SIGNALLED;

    if (Event == NULL)
        return OS_WAIT_ERROR;

    if (TimeoutMs >= 0)
    {
        if (clock_gettime(CLOCK_MONOTONIC, &Now) != 0)
            return OS_WAIT_ERROR;

        LinTimeAddMs((int64_t)Now.tv_sec, Now.tv_nsec, TimeoutMs, &DeadlineSec,
                     &DeadlineNsec);
        Deadline.tv_sec = (time_t)DeadlineSec;
        Deadline.tv_nsec = DeadlineNsec;
    }

    pthread_mutex_lock(&Event->Mutex);

    // A loop, because a condition variable may wake without having been signalled.
    while (!Event->Signalled)
    {
        int Rc = TimeoutMs < 0
                     ? pthread_cond_wait(&Event->Cond, &Event->Mutex)
                     : pthread_cond_timedwait(&Event->Cond, &Event->Mutex, &Deadline);
        if (Rc == ETIMEDOUT)
        {
            Result = OS_WAIT_TIMEOUT;
            break;
        }
        if (Rc != 0)
        {
            Result = OS_WAIT_ERROR;
            break;
        }
    }

    // Consuming the flag is what makes the event auto-reset.
    if (Result == OS_WAIT_SIGNALLED)
        Event->Signalled = 0;

    pthread_mutex_unlock(&Event->Mutex);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Mutex +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct OsMutex
{
    pthread_mutex_t Handle;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
OsMutex* OsMutexCreate(void)
{
    OsMutex* Mutex = (OsMutex*)DtMalloc(sizeof(OsMutex));

    if (Mutex == NULL)
        return NULL;

    if (pthread_mutex_init(&Mutex->Handle, NULL) != 0)
    {
        DtFree(Mutex);
        return NULL;
    }

    return Mutex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexDestroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsMutexDestroy(OsMutex* Mutex)
{
    if (Mutex == NULL)
        return;

    pthread_mutex_destroy(&Mutex->Handle);
    DtFree(Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexLock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsMutexLock(OsMutex* Mutex)
{
    pthread_mutex_lock(&Mutex->Handle);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexUnlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsMutexUnlock(OsMutex* Mutex)
{
    pthread_mutex_unlock(&Mutex->Handle);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A signal cuts nanosleep short and leaves the rest in Remaining, which is slept again.
//
void OsSleepMs(int Ms)
{
    struct timespec Remaining;

    if (Ms <= 0)
        return;

    Remaining.tv_sec = Ms / 1000;
    Remaining.tv_nsec = (long)(Ms % 1000) * 1000000L;
    while (nanosleep(&Remaining, &Remaining) != 0 && errno == EINTR)
        ;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMonotonicMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint64_t OsMonotonicMs(void)
{
    struct timespec Now;

    if (clock_gettime(CLOCK_MONOTONIC, &Now) != 0)
        return 0;
    return (uint64_t)Now.tv_sec * 1000u + (uint64_t)Now.tv_nsec / 1000000u;
}

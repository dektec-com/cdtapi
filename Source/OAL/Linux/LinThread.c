// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Threads, events, mutexes, time and the process on Linux
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
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// CDTAPI includes
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-. OsThread_SetName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// pthread_setname_np refuses a name of more than fifteen characters rather than cutting
// it, so it is cut here.
//
void OsThread_SetName(const char* Name)
{
    char Short[16];

    if (Name == NULL)
        return;
    snprintf(Short, sizeof(Short), "%s", Name);
    pthread_setname_np(pthread_self(), Short);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThread_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
OsThread* OsThread_Start(OsThreadFunc Func, void* Context)
{
    if (Func == NULL)
        return NULL;

    OsThread* Thread = (OsThread*)DtAlloc_Malloc(sizeof(OsThread));
    if (Thread == NULL)
        return NULL;

    Thread->Func = Func;
    Thread->Context = Context;

    if (pthread_create(&Thread->Handle, NULL, ThreadEntry, Thread) != 0)
    {
        DtAlloc_Free(Thread);
        return NULL;
    }

    return Thread;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThread_Join -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsThread_Join(OsThread* Thread)
{
    if (Thread == NULL)
        return;

    pthread_join(Thread->Handle, NULL);
    DtAlloc_Free(Thread);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThread_RaisePriority -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A modest real-time priority under SCHED_FIFO. Without CAP_SYS_NICE or a matching
// rlimit the kernel refuses, which is reported rather than treated as an error.
//
int OsThread_RaisePriority(void)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
OsEvent* OsEvent_Create(void)
{
    OsEvent* Event = (OsEvent*)DtAlloc_Malloc(sizeof(OsEvent));
    if (Event == NULL)
        return NULL;
    if (pthread_mutex_init(&Event->Mutex, NULL) != 0)
    {
        DtAlloc_Free(Event);
        return NULL;
    }

    // The condition times its waits on the monotonic clock.
    pthread_condattr_t Attr;
    int Made = pthread_condattr_init(&Attr) == 0;
    if (Made)
    {
        Made = pthread_condattr_setclock(&Attr, CLOCK_MONOTONIC) == 0 &&
               pthread_cond_init(&Event->Cond, &Attr) == 0;
        pthread_condattr_destroy(&Attr);
    }
    if (!Made)
    {
        pthread_mutex_destroy(&Event->Mutex);
        DtAlloc_Free(Event);
        return NULL;
    }

    Event->Signalled = 0;
    return Event;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsEvent_Destroy(OsEvent* Event)
{
    if (Event == NULL)
        return;

    pthread_cond_destroy(&Event->Cond);
    pthread_mutex_destroy(&Event->Mutex);
    DtAlloc_Free(Event);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Set -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsEvent_Set(OsEvent* Event)
{
    if (Event == NULL)
        return;

    pthread_mutex_lock(&Event->Mutex);
    Event->Signalled = 1;
    // One waiter, to match the auto-reset semantics: it consumes the flag.
    pthread_cond_signal(&Event->Cond);
    pthread_mutex_unlock(&Event->Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Wait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsEvent_Wait(OsEvent* Event, int TimeoutMs)
{
    int Result = OS_WAIT_SIGNALLED;

    if (Event == NULL)
        return OS_WAIT_ERROR;

    struct timespec Deadline;
    if (TimeoutMs >= 0)
    {
        struct timespec Now;
        if (clock_gettime(CLOCK_MONOTONIC, &Now) != 0)
            return OS_WAIT_ERROR;

        int64_t DeadlineSec;
        long DeadlineNsec;
        LinTime_AddMs((int64_t)Now.tv_sec, Now.tv_nsec, TimeoutMs, &DeadlineSec,
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
OsMutex* OsMutex_Create(void)
{
    OsMutex* Mutex = (OsMutex*)DtAlloc_Malloc(sizeof(OsMutex));

    if (Mutex == NULL)
        return NULL;

    if (pthread_mutex_init(&Mutex->Handle, NULL) != 0)
    {
        DtAlloc_Free(Mutex);
        return NULL;
    }

    return Mutex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsMutex_Destroy(OsMutex* Mutex)
{
    if (Mutex == NULL)
        return;

    pthread_mutex_destroy(&Mutex->Handle);
    DtAlloc_Free(Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsMutex_Lock(OsMutex* Mutex)
{
    pthread_mutex_lock(&Mutex->Handle);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsMutex_Unlock(OsMutex* Mutex)
{
    pthread_mutex_unlock(&Mutex->Handle);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsTime_SleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A signal cuts nanosleep short and leaves the rest in Remaining, which is slept again.
//
void OsTime_SleepMs(int Ms)
{
    if (Ms <= 0)
        return;

    struct timespec Remaining;
    Remaining.tv_sec = Ms / 1000;
    Remaining.tv_nsec = (long)(Ms % 1000) * 1000000L;
    while (nanosleep(&Remaining, &Remaining) != 0 && errno == EINTR)
        ;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsTime_MonotonicMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint64_t OsTime_MonotonicMs(void)
{
    struct timespec Now;

    if (clock_gettime(CLOCK_MONOTONIC, &Now) != 0)
        return 0;
    return (uint64_t)Now.tv_sec * 1000u + (uint64_t)Now.tv_nsec / 1000000u;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsProcess_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The name the program was invoked with, from program_invocation_name.
//
void OsProcess_Name(char* Buf, size_t Size)
{
    if (Buf == NULL || Size == 0)
        return;
    snprintf(Buf, Size, "%s",
             program_invocation_name != NULL ? program_invocation_name : "");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsProcess_Id -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t OsProcess_Id(void)
{
    return (uint32_t)getpid();
}

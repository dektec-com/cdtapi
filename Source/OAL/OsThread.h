// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# OsThread.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Threads, events with a timeout, mutexes, sleeping and a monotonic clock
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_OS_THREAD_H
#define CDTAPILITE_OS_THREAD_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Primitives +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The receive path of a channel runs on its own thread: it waits on an event with a
// short timeout, drains the DMA ring, and stops when a kill event is set. That is the
// pattern DTAPI uses in AsiSdiInpChannel_Bb2.cpp, and it is why this layer provides
// these three primitives. Waiting for a signal polls instead, which takes a sleep and a
// clock to measure its timeout by.
//
// These belong to the operating system, not to a device, so they are the same whether
// the device behind a channel is real or emulated.
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Thread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

typedef struct OsThread OsThread;
typedef void (*OsThreadFunc)(void* Context);

// Starts Func(Context) on a new thread. Returns NULL when the thread cannot be created.
OsThread* OsThreadStart(OsThreadFunc Func, void* Context);

// Waits for the thread to return, then releases it. Passing NULL does nothing.
//
// There is no way to stop a thread from outside. A thread that must be stoppable waits
// on an event and returns when it is set; the caller sets that event and then joins.
void OsThreadJoin(OsThread* Thread);

// Raises the calling thread's scheduling priority, for the thread that drains the DMA
// ring. Returns 0 on success and -1 when the platform refuses, which on Linux is the
// normal answer without the right privilege. A refusal is not fatal: the thread still
// runs, only with less headroom against a busy system.
int OsThreadRaisePriority(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Event -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct OsEvent OsEvent;

// Wait outcomes.
#define OS_WAIT_SIGNALLED 0
#define OS_WAIT_TIMEOUT 1
#define OS_WAIT_ERROR -1

// Creates an auto-reset event, initially not set. A set event wakes one waiter and
// resets as it does so. Returns NULL when out of resources.
OsEvent* OsEventCreate(void);

// Destroys an event. No thread may still be waiting on it. Passing NULL does nothing.
void OsEventDestroy(OsEvent* Event);

// Sets the event. Setting an event that is already set leaves it set once, not twice.
void OsEventSet(OsEvent* Event);

// Waits until the event is set or TimeoutMs milliseconds have passed. A negative timeout
// waits indefinitely. Returns OS_WAIT_SIGNALLED, OS_WAIT_TIMEOUT or OS_WAIT_ERROR.
int OsEventWait(OsEvent* Event, int TimeoutMs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Mutex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct OsMutex OsMutex;

// Creates a mutex. Returns NULL when out of resources. Not recursive: a thread that
// locks a mutex it already holds deadlocks.
OsMutex* OsMutexCreate(void);
void OsMutexDestroy(OsMutex* Mutex);
void OsMutexLock(OsMutex* Mutex);
void OsMutexUnlock(OsMutex* Mutex);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Sleeps for at least Ms milliseconds, or returns at once for 0 or less. The system may
// sleep longer, by up to a scheduler tick.
void OsSleepMs(int Ms);

// Milliseconds on a clock that only moves forward, for measuring an interval. Where it
// starts is unspecified.
uint64_t OsMonotonicMs(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Process -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Writes the name the process runs under into Buf, which holds Size bytes, cut to fit:
// the executable's file name on Windows, the name it was invoked by on Linux, as DTAPI
// takes them for a channel's friendly name. Empty when it cannot be read.
void OsProcessName(char* Buf, size_t Size);

// The process's identifier.
unsigned long OsProcessId(void);

#endif // CDTAPILITE_OS_THREAD_H

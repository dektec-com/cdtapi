// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# OsThread.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Threads, events with a timeout, mutexes, sleeping and a monotonic clock
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Primitives +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Threads, auto-reset events with a time-out, non-recursive mutexes, sleeping, a
// monotonic clock and the identity of the process, with the same behaviour on Windows
// and on Linux. They belong to the operating system rather than to a device, so they are
// the same for a real and an emulated device.
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Thread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

typedef struct OsThread OsThread;
typedef void (*OsThreadFunc)(void* Context);

// Starts Func(Context) on a new thread. Returns NULL when the thread cannot be created.
OsThread* OsThread_Start(OsThreadFunc Func, void* Context);

// Waits for the thread to return, then releases it. Passing NULL does nothing.
//
// There is no way to stop a thread from outside. A thread that must be stoppable waits
// on an event and returns when it is set; the caller sets that event and then joins.
void OsThread_Join(OsThread* Thread);

// Gives the calling thread a name, which is what a debugger and a process viewer show
// beside it: "DtConv0" rather than the program's own name for every one of them, so that
// the cost of a thread of the library's can be told from the cost of the program's.
//
// Linux takes fifteen characters and a terminator, which is the shorter of the two
// limits, so a name is kept within that; a longer one is cut. A name is a convenience and
// nothing depends on it, so a platform that refuses one carries on without.
void OsThread_SetName(const char* Name);

// Raises the calling thread's scheduling priority. Returns 0 on success and -1 when the
// platform refuses, which on Linux is the normal answer without the right privilege. A
// refusal is not fatal: the thread still runs, only with less headroom against a busy
// system.
int OsThread_RaisePriority(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Event -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct OsEvent OsEvent;

// Wait outcomes.
#define OS_WAIT_SIGNALLED 0
#define OS_WAIT_TIMEOUT 1
#define OS_WAIT_ERROR -1

// Creates an auto-reset event, initially not set. A set event wakes one waiter and
// resets as it does so. Returns NULL when out of resources.
OsEvent* OsEvent_Create(void);

// Destroys an event. No thread may still be waiting on it. Passing NULL does nothing.
void OsEvent_Destroy(OsEvent* Event);

// Sets the event. Setting an event that is already set leaves it set once, not twice.
void OsEvent_Set(OsEvent* Event);

// Waits until the event is set or TimeoutMs milliseconds have passed. A negative timeout
// waits indefinitely. Returns OS_WAIT_SIGNALLED, OS_WAIT_TIMEOUT or OS_WAIT_ERROR.
int OsEvent_Wait(OsEvent* Event, int TimeoutMs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Mutex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct OsMutex OsMutex;

// Creates a mutex. Returns NULL when out of resources. Not recursive: a thread that
// locks a mutex it already holds deadlocks.
OsMutex* OsMutex_Create(void);
void OsMutex_Destroy(OsMutex* Mutex);
void OsMutex_Lock(OsMutex* Mutex);
void OsMutex_Unlock(OsMutex* Mutex);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Sleeps for at least Ms milliseconds, or returns at once for 0 or less. The system may
// sleep longer, by up to a scheduler tick.
void OsTime_SleepMs(int Ms);

// Milliseconds on a clock that only moves forward, for measuring an interval. Where it
// starts is unspecified.
uint64_t OsTime_MonotonicMs(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Process -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Writes the name the process runs under into Buf, which holds Size bytes, cut to fit:
// the executable's file name on Windows, the name it was invoked by on Linux. Empty when
// it cannot be read.
void OsProcess_Name(char* Buf, size_t Size);

// The process's identifier.
uint32_t OsProcess_Id(void);

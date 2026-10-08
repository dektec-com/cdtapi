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
// Threads, events, mutexes, sleeping, a clock and the identity of the process. They
// behave the same on Windows and Linux. They come from the operating system, not from a
// device, so they are the same with a real and with an emulated device.
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Thread -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

typedef struct OsThread OsThread;

// The function a thread runs, with the Context given to OsThread_Start.
typedef void (*OsThreadFunc)(void* Context);

// Starts a new thread that runs Func(Context). Returns the thread, or NULL when it
// cannot be created.
OsThread* OsThread_Start(OsThreadFunc Func, void* Context);

// Waits until the thread has returned, then frees it. A NULL Thread does nothing.
//
// A thread cannot be stopped from outside. A thread that must stop on request waits on
// an event and returns when it is set; the caller sets the event and then joins.
void OsThread_Join(OsThread* Thread);

// Names the calling thread, for example "DtWorker.1". Debuggers and process viewers
// show the name, so the time spent in the library's threads can be told apart from the
// program's.
//
// A name is cut to fifteen characters, the limit on Linux. Where the platform refuses a
// name, the thread carries on without one; nothing depends on it.
void OsThread_SetName(const char* Name);

// The priorities the library's threads ask for, highest first. Both are above normal.
typedef enum OsThreadPriority
{
    OS_THREAD_PRIORITY_RING, // A thread that keeps a card's buffer filled or emptied
    OS_THREAD_PRIORITY_POOL, // A worker pool thread, which does the bulk of the work
} OsThreadPriority;

// Raises the priority of the calling thread. Returns 0, or -1 if the platform refuses.
// On Linux a refusal is normal without the right privilege. It is not an error: the
// thread keeps running at normal priority.
int OsThread_RaisePriority(OsThreadPriority Priority);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Event -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// An event that one thread sets to wake another. It resets itself: setting it wakes one
// waiting thread, after which the event is clear again.
typedef struct OsEvent OsEvent;

// The results of OsEvent_Wait.
#define OS_WAIT_SIGNALLED 0
#define OS_WAIT_TIMEOUT 1
#define OS_WAIT_ERROR -1

// Creates an event that is not set. Returns NULL when there are not enough resources.
OsEvent* OsEvent_Create(void);

// Destroys an event. No thread may still wait on it. A NULL Event does nothing.
void OsEvent_Destroy(OsEvent* Event);

// Sets the event. Setting it twice before a thread waits wakes only one wait.
void OsEvent_Set(OsEvent* Event);

// Waits until the event is set or TimeoutMs milliseconds have passed; a negative
// TimeoutMs waits without limit. Returns OS_WAIT_SIGNALLED, OS_WAIT_TIMEOUT or
// OS_WAIT_ERROR.
int OsEvent_Wait(OsEvent* Event, int TimeoutMs);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Mutex -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// A lock that one thread at a time can hold. It is not recursive: a thread that locks a
// mutex it already holds deadlocks.
typedef struct OsMutex OsMutex;

// Creates a mutex. Returns NULL when there are not enough resources.
OsMutex* OsMutex_Create(void);

// Destroys a mutex that no thread holds. A NULL Mutex does nothing.
void OsMutex_Destroy(OsMutex* Mutex);

// Locks the mutex, waiting until no other thread holds it.
void OsMutex_Lock(OsMutex* Mutex);

// Unlocks the mutex, which the calling thread holds.
void OsMutex_Unlock(OsMutex* Mutex);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Time -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

// Sleeps for about Ms milliseconds; returns at once for 0 or less. The sleep follows the
// system's timer and can last longer than asked. On Linux it lasts at least Ms; on
// Windows a sleep shorter than one timer tick can also end early.
void OsTime_SleepMs(int Ms);

// Returns a time in milliseconds, on a clock that never goes back, to measure how long
// something takes. Where the clock starts is not defined.
uint64_t OsTime_MonotonicMs(void);

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Process -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Writes the name of the process into Buf, of Size bytes, cut to fit: the file name of
// the executable on Windows, the name it was started by on Linux. Writes an empty string
// when the name cannot be read.
void OsProcess_Name(char* Buf, size_t Size);

// Returns the ID of the process.
uint32_t OsProcess_Id(void);

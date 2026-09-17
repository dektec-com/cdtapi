// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# WinThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Threads, events, mutexes and time on Windows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Windows includes
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "OAL/OsThread.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Thread +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

struct OsThread
{
    HANDLE Handle;
    OsThreadFunc Func;
    void* Context;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ThreadEntry -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static unsigned __stdcall ThreadEntry(void* Arg)
{
    OsThread* Thread = (OsThread*)Arg;

    Thread->Func(Thread->Context);
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThreadStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// _beginthreadex rather than CreateThread, so that the C runtime sets up its per-thread
// state for the new thread.
//
OsThread* OsThreadStart(OsThreadFunc Func, void* Context)
{
    OsThread* Thread;
    uintptr_t Handle;

    if (Func == NULL)
        return NULL;

    Thread = (OsThread*)DtMalloc(sizeof(OsThread));
    if (Thread == NULL)
        return NULL;

    Thread->Func = Func;
    Thread->Context = Context;

    Handle = _beginthreadex(NULL, 0, ThreadEntry, Thread, 0, NULL);
    if (Handle == 0)
    {
        DtFree(Thread);
        return NULL;
    }

    Thread->Handle = (HANDLE)Handle;
    return Thread;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThreadJoin -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsThreadJoin(OsThread* Thread)
{
    if (Thread == NULL)
        return;

    WaitForSingleObject(Thread->Handle, INFINITE);
    CloseHandle(Thread->Handle);
    DtFree(Thread);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThreadRaisePriority -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// THREAD_PRIORITY_HIGHEST. Time-critical is deliberately not used: a stuck time-critical
// thread can starve the rest of the machine.
//
int OsThreadRaisePriority(void)
{
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) ? 0 : -1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Event +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct OsEvent
{
    HANDLE Handle;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
OsEvent* OsEventCreate(void)
{
    OsEvent* Event = (OsEvent*)DtMalloc(sizeof(OsEvent));

    if (Event == NULL)
        return NULL;

    // Auto-reset, initially not signalled.
    Event->Handle = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (Event->Handle == NULL)
    {
        DtFree(Event);
        return NULL;
    }

    return Event;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventDestroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsEventDestroy(OsEvent* Event)
{
    if (Event == NULL)
        return;

    CloseHandle(Event->Handle);
    DtFree(Event);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventSet -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsEventSet(OsEvent* Event)
{
    if (Event != NULL)
        SetEvent(Event->Handle);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEventWait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int OsEventWait(OsEvent* Event, int TimeoutMs)
{
    DWORD Timeout = TimeoutMs < 0 ? INFINITE : (DWORD)TimeoutMs;

    if (Event == NULL)
        return OS_WAIT_ERROR;

    switch (WaitForSingleObject(Event->Handle, Timeout))
    {
    case WAIT_OBJECT_0:
        return OS_WAIT_SIGNALLED;
    case WAIT_TIMEOUT:
        return OS_WAIT_TIMEOUT;
    default:
        return OS_WAIT_ERROR;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Mutex +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// An SRW lock in exclusive mode. It is lighter than a critical section, cannot fail to
// initialise, and needs no destruction of its own; the structure only exists so that the
// lock has a stable address.
//

struct OsMutex
{
    SRWLOCK Lock;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexCreate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
OsMutex* OsMutexCreate(void)
{
    OsMutex* Mutex = (OsMutex*)DtMalloc(sizeof(OsMutex));

    if (Mutex != NULL)
        InitializeSRWLock(&Mutex->Lock);

    return Mutex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexDestroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsMutexDestroy(OsMutex* Mutex)
{
    DtFree(Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexLock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsMutexLock(OsMutex* Mutex)
{
    AcquireSRWLockExclusive(&Mutex->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutexUnlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsMutexUnlock(OsMutex* Mutex)
{
    ReleaseSRWLockExclusive(&Mutex->Lock);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsSleepMs(int Ms)
{
    if (Ms > 0)
        Sleep((DWORD)Ms);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMonotonicMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint64_t OsMonotonicMs(void)
{
    return (uint64_t)GetTickCount64();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsProcessName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The file name of the executable, as GetModuleBaseName gives it to XpUtil's
// GetCurrentProcessName on Windows.
//
void OsProcessName(char* Buf, size_t Size)
{
    char Path[MAX_PATH];
    DWORD Length;
    const char* Name;
    size_t i;

    if (Buf == NULL || Size == 0)
        return;
    Buf[0] = '\0';

    Length = GetModuleFileNameA(NULL, Path, (DWORD)sizeof(Path));
    if (Length == 0 || Length >= sizeof(Path))
        return;

    Name = Path;
    for (i = 0; i < Length; i++)
    {
        if (Path[i] == '\\' || Path[i] == '/')
            Name = Path + i + 1;
    }
    for (i = 0; i + 1 < Size && Name[i] != '\0'; i++)
        Buf[i] = Name[i];
    Buf[i] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsProcessId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned long OsProcessId(void)
{
    return (unsigned long)GetCurrentProcessId();
}

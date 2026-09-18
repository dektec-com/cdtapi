// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# WinThread.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Threads, events, mutexes and time on Windows
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Windows includes
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>

// CDTAPI includes
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThread_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// _beginthreadex rather than CreateThread, so that the C runtime sets up its per-thread
// state for the new thread.
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

    uintptr_t Handle = _beginthreadex(NULL, 0, ThreadEntry, Thread, 0, NULL);
    if (Handle == 0)
    {
        DtAlloc_Free(Thread);
        return NULL;
    }

    Thread->Handle = (HANDLE)Handle;
    return Thread;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThread_Join -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsThread_Join(OsThread* Thread)
{
    if (Thread == NULL)
        return;

    WaitForSingleObject(Thread->Handle, INFINITE);
    CloseHandle(Thread->Handle);
    DtAlloc_Free(Thread);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsThread_RaisePriority -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// THREAD_PRIORITY_HIGHEST. Time-critical is deliberately not used: a stuck time-critical
// thread can starve the rest of the machine.
//
int OsThread_RaisePriority(void)
{
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) ? 0 : -1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Event +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct OsEvent
{
    HANDLE Handle;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
OsEvent* OsEvent_Create(void)
{
    OsEvent* Event = (OsEvent*)DtAlloc_Malloc(sizeof(OsEvent));

    if (Event == NULL)
        return NULL;

    // Auto-reset, initially not signalled.
    Event->Handle = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (Event->Handle == NULL)
    {
        DtAlloc_Free(Event);
        return NULL;
    }

    return Event;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsEvent_Destroy(OsEvent* Event)
{
    if (Event == NULL)
        return;

    CloseHandle(Event->Handle);
    DtAlloc_Free(Event);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Set -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsEvent_Set(OsEvent* Event)
{
    if (Event != NULL)
        SetEvent(Event->Handle);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsEvent_Wait -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsEvent_Wait(OsEvent* Event, int TimeoutMs)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Create -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
OsMutex* OsMutex_Create(void)
{
    OsMutex* Mutex = (OsMutex*)DtAlloc_Malloc(sizeof(OsMutex));

    if (Mutex != NULL)
        InitializeSRWLock(&Mutex->Lock);

    return Mutex;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Destroy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void OsMutex_Destroy(OsMutex* Mutex)
{
    DtAlloc_Free(Mutex);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Lock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsMutex_Lock(OsMutex* Mutex)
{
    AcquireSRWLockExclusive(&Mutex->Lock);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsMutex_Unlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsMutex_Unlock(OsMutex* Mutex)
{
    ReleaseSRWLockExclusive(&Mutex->Lock);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsTime_SleepMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsTime_SleepMs(int Ms)
{
    if (Ms > 0)
        Sleep((DWORD)Ms);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsTime_MonotonicMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint64_t OsTime_MonotonicMs(void)
{
    return (uint64_t)GetTickCount64();
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsProcess_Name -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The file name of the executable, as GetModuleBaseName gives it to XpUtil's
// GetCurrentProcessName on Windows.
//
void OsProcess_Name(char* Buf, size_t Size)
{
    if (Buf == NULL || Size == 0)
        return;
    Buf[0] = '\0';

    char Path[MAX_PATH];
    DWORD Length = GetModuleFileNameA(NULL, Path, (DWORD)sizeof(Path));
    if (Length == 0 || Length >= sizeof(Path))
        return;

    const char* Name = Path;
    size_t i;
    for (i = 0; i < Length; i++)
    {
        if (Path[i] == '\\' || Path[i] == '/')
            Name = Path + i + 1;
    }
    for (i = 0; i + 1 < Size && Name[i] != '\0'; i++)
        Buf[i] = Name[i];
    Buf[i] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsProcess_Id -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint32_t OsProcess_Id(void)
{
    return (uint32_t)GetCurrentProcessId();
}

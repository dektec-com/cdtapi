// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* WinPipe.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A connection to a local service through its named pipe, on Windows
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The pipe is opened for overlapped I/O, so that a read or a write can be given up when
// its time runs out. The service's pipe holds messages; the client reads it as bytes.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Windows includes
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "OAL/OsPipe.h"   // Interface being implemented.
#include "OAL/OsThread.h" // Monotonic time.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct OsPipe
{
    HANDLE Handle; // The client's end of the pipe
    HANDLE Event;  // Signalled when an overlapped read or write completes
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ErrorOutcome -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The outcome for a failed read or write: the service has gone, or anything else.
//
static int ErrorOutcome(DWORD Error)
{
    switch (Error)
    {
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
    case ERROR_PIPE_NOT_CONNECTED:
        return OS_PIPE_CLOSED;
    default:
        return OS_PIPE_ERROR;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RemainingMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The milliseconds left until Deadline, on the monotonic clock; 0 once it has passed.
//
static DWORD RemainingMs(uint64_t Deadline)
{
    uint64_t Now = OsTime_MonotonicMs();
    return Now >= Deadline ? 0 : (DWORD)(Deadline - Now);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Transfer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads or writes once, up to Size bytes, and sets *Done to the number of bytes it moved.
// Waits until Deadline at most; an operation that has not completed by then is cancelled.
//
static int Transfer(OsPipe* Pipe, bool Write, uint8_t* Buf, size_t Size,
                    uint64_t Deadline, DWORD* Done)
{
    OVERLAPPED Overlapped;
    memset(&Overlapped, 0, sizeof(Overlapped));
    Overlapped.hEvent = Pipe->Event;
    DWORD Chunk = Size > MAXDWORD ? MAXDWORD : (DWORD)Size;

    *Done = 0;
    BOOL Started = Write ? WriteFile(Pipe->Handle, Buf, Chunk, NULL, &Overlapped)
                         : ReadFile(Pipe->Handle, Buf, Chunk, NULL, &Overlapped);
    if (!Started)
    {
        DWORD Error = GetLastError();
        if (Error != ERROR_IO_PENDING && Error != ERROR_MORE_DATA)
            return ErrorOutcome(Error);
        if (Error == ERROR_IO_PENDING &&
            WaitForSingleObject(Pipe->Event, RemainingMs(Deadline)) != WAIT_OBJECT_0)
        {
            // Cancelled or not, the operation must have ended before Overlapped goes
            // out of scope.
            CancelIo(Pipe->Handle);
            GetOverlappedResult(Pipe->Handle, &Overlapped, Done, TRUE);
            return OS_PIPE_TIMEOUT;
        }
    }
    if (!GetOverlappedResult(Pipe->Handle, &Overlapped, Done, FALSE))
    {
        // A read in byte mode never ends a message early, but a message read in part
        // still delivered its bytes.
        DWORD Error = GetLastError();
        if (Error != ERROR_MORE_DATA)
            return ErrorOutcome(Error);
    }
    return OS_PIPE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TransferAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads or writes all Size bytes before TimeoutMs has passed.
//
static int TransferAll(OsPipe* Pipe, bool Write, uint8_t* Buf, size_t Size, int TimeoutMs)
{
    if (Pipe == NULL || (Buf == NULL && Size > 0) || TimeoutMs < 0)
        return OS_PIPE_ERROR;

    uint64_t Deadline = OsTime_MonotonicMs() + (uint64_t)TimeoutMs;
    while (Size > 0)
    {
        DWORD Done = 0;
        int Outcome = Transfer(Pipe, Write, Buf, Size, Deadline, &Done);
        if (Outcome != OS_PIPE_OK)
            return Outcome;
        if (Done == 0 && RemainingMs(Deadline) == 0)
            return OS_PIPE_TIMEOUT;
        Buf += Done;
        Size -= Done;
    }
    return OS_PIPE_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsPipe_Close(OsPipe* Pipe)
{
    if (Pipe == NULL)
        return;
    if (Pipe->Handle != INVALID_HANDLE_VALUE)
        CloseHandle(Pipe->Handle);
    if (Pipe->Event != NULL)
        CloseHandle(Pipe->Event);
    DtAlloc_Free(Pipe);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Opens the pipe, and while every instance of it is busy, waits for one to come free.
//
int OsPipe_Connect(const char* Name, int TimeoutMs, OsPipe** Pipe)
{
    if (Pipe != NULL)
        *Pipe = NULL;
    if (Name == NULL || Name[0] == '\0' || TimeoutMs < 0 || Pipe == NULL)
        return OS_PIPE_ERROR;

    char Path[MAX_PATH];
    int Length = snprintf(Path, sizeof(Path), "\\\\.\\pipe\\%s", Name);
    if (Length < 0 || (size_t)Length >= sizeof(Path))
        return OS_PIPE_ERROR;

    OsPipe* New = (OsPipe*)DtAlloc_Malloc(sizeof(OsPipe));
    if (New == NULL)
        return OS_PIPE_NO_MEMORY;
    New->Handle = INVALID_HANDLE_VALUE;
    New->Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (New->Event == NULL)
    {
        OsPipe_Close(New);
        return OS_PIPE_ERROR;
    }

    uint64_t Deadline = OsTime_MonotonicMs() + (uint64_t)TimeoutMs;
    int Outcome = OS_PIPE_OK;
    while (New->Handle == INVALID_HANDLE_VALUE && Outcome == OS_PIPE_OK)
    {
        New->Handle = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (New->Handle != INVALID_HANDLE_VALUE)
            break;
        DWORD Error = GetLastError();
        if (Error == ERROR_FILE_NOT_FOUND)
            Outcome = OS_PIPE_NOT_FOUND;
        else if (Error != ERROR_PIPE_BUSY)
            Outcome = OS_PIPE_ERROR;
        else if (RemainingMs(Deadline) == 0)
            Outcome = OS_PIPE_TIMEOUT;
        else
            WaitNamedPipeA(Path, RemainingMs(Deadline));
    }
    if (Outcome != OS_PIPE_OK)
    {
        OsPipe_Close(New);
        return Outcome;
    }
    *Pipe = New;
    return OS_PIPE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int OsPipe_Read(OsPipe* Pipe, void* Buf, size_t Size, int TimeoutMs)
{
    return TransferAll(Pipe, false, (uint8_t*)Buf, Size, TimeoutMs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsPipe_Write(OsPipe* Pipe, const void* Buf, size_t Size, int TimeoutMs)
{
    // WriteFile does not change the bytes; the cast only lets reads and writes share
    // one loop.
    return TransferAll(Pipe, true, (uint8_t*)(uintptr_t)Buf, Size, TimeoutMs);
}

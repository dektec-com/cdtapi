// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* LinPipe.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A connection to a local service through a pair of FIFOs, on Linux
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The handshake is DtapiService's: the client makes a FIFO for each direction, writes
// their two names to the service's listener FIFO in one write, which a FIFO keeps whole
// below PIPE_BUF bytes, and reads the service's answer, a 32-bit result that is 0 when
// the service has opened them. The FIFOs are opened without blocking and waited on with
// poll, so that every step has a time limit.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes the POSIX calls the backend makes.
#define _GNU_SOURCE

// Standard includes
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "OAL/OsPipe.h"   // Interface being implemented.
#include "OAL/OsThread.h" // Monotonic time.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The size of a FIFO's path in the connect message, with the terminating zero.
#define FIFO_PATH_SIZE 100

// How long to wait between attempts to open a listener that is not being read yet.
#define LISTENER_RETRY_MS 10

struct OsPipe
{
    int ReadFd;  // The FIFO the service writes to
    int WriteFd; // The FIFO the service reads from
};

// The message a client writes to the listener: the paths of its two FIFOs, each in a
// field of FIFO_PATH_SIZE bytes.
typedef struct ConnectMsg
{
    char ClientToService[FIFO_PATH_SIZE];
    char ServiceToClient[FIFO_PATH_SIZE];
} ConnectMsg;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- RemainingMs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The milliseconds left until Deadline, on the monotonic clock; 0 once it has passed.
//
static int RemainingMs(uint64_t Deadline)
{
    uint64_t Now = OsTime_MonotonicMs();
    if (Now >= Deadline)
        return 0;
    return Deadline - Now > INT32_MAX ? INT32_MAX : (int)(Deadline - Now);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitReady -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits until Fd can be read, or written when Write is true, or until Deadline. Returns
// OS_PIPE_OK when it can, which includes the other end having closed.
//
static int WaitReady(int Fd, bool Write, uint64_t Deadline)
{
    for (;;)
    {
        struct pollfd Poll = {Fd, Write ? POLLOUT : POLLIN, 0};
        int Ready = poll(&Poll, 1, RemainingMs(Deadline));
        if (Ready > 0)
            return OS_PIPE_OK;
        if (Ready == 0)
            return OS_PIPE_TIMEOUT;
        if (errno != EINTR)
            return OS_PIPE_ERROR;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads Size bytes from Fd before Deadline. It waits before it reads, as a read from a
// FIFO that no writer has opened yet returns 0, which would look like a closed one.
//
static int ReadAll(int Fd, uint8_t* Buf, size_t Size, uint64_t Deadline)
{
    while (Size > 0)
    {
        int Outcome = WaitReady(Fd, false, Deadline);
        if (Outcome != OS_PIPE_OK)
            return Outcome;
        ssize_t Done = read(Fd, Buf, Size);
        if (Done == 0)
            return OS_PIPE_CLOSED;
        if (Done < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return OS_PIPE_ERROR;
        }
        Buf += Done;
        Size -= (size_t)Done;
    }
    return OS_PIPE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteOnce -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes to Fd once, without letting a reader that has gone raise SIGPIPE: the signal
// would end the program, and a library may not change how the program handles it. So
// SIGPIPE is blocked in this thread for the write, and when the write raised it, taken
// before the old mask comes back. One that was pending before is left pending.
//
static ssize_t WriteOnce(int Fd, const uint8_t* Buf, size_t Size)
{
    sigset_t PipeSignal;
    sigemptyset(&PipeSignal);
    sigaddset(&PipeSignal, SIGPIPE);
    sigset_t Pending;
    sigemptyset(&Pending);
    sigpending(&Pending);
    bool WasPending = sigismember(&Pending, SIGPIPE) == 1;
    sigset_t OldMask;
    pthread_sigmask(SIG_BLOCK, &PipeSignal, &OldMask);

    ssize_t Done = write(Fd, Buf, Size);
    int WriteError = errno;
    if (Done < 0 && WriteError == EPIPE && !WasPending)
    {
        const struct timespec NoWait = {0, 0};
        while (sigtimedwait(&PipeSignal, NULL, &NoWait) < 0 && errno == EINTR)
        {
        }
    }

    pthread_sigmask(SIG_SETMASK, &OldMask, NULL);
    errno = WriteError;
    return Done;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes Size bytes to Fd before Deadline.
//
static int WriteAll(int Fd, const uint8_t* Buf, size_t Size, uint64_t Deadline)
{
    while (Size > 0)
    {
        int Outcome = WaitReady(Fd, true, Deadline);
        if (Outcome != OS_PIPE_OK)
            return Outcome;
        ssize_t Done = WriteOnce(Fd, Buf, Size);
        if (Done < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return errno == EPIPE ? OS_PIPE_CLOSED : OS_PIPE_ERROR;
        }
        Buf += Done;
        Size -= (size_t)Done;
    }
    return OS_PIPE_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PipeDir -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the directory of the FIFOs, ending in a slash, into Dir: DTAPI_PIPES_PATH, else
// /run/DtapiServiced/, falling back to /tmp/ when the directory does not exist. Returns
// false when DTAPI_PIPES_PATH does not fit.
//
static bool PipeDir(char* Dir, size_t Size)
{
    const char* Env = getenv("DTAPI_PIPES_PATH");
    const char* Chosen = Env != NULL && Env[0] != '\0' ? Env : "/run/DtapiServiced/";
    size_t Length = strlen(Chosen);
    bool NeedsSlash = Chosen[Length - 1] != '/';
    if (Length + (NeedsSlash ? 1 : 0) >= Size)
        return false;
    snprintf(Dir, Size, "%s%s", Chosen, NeedsSlash ? "/" : "");

    struct stat Info;
    if (stat(Dir, &Info) != 0)
        snprintf(Dir, Size, "%s", "/tmp/");
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes a FIFO of a name no other one has, <Dir><Name>_<process>_<number>, and writes
// its path into Path, which has FIFO_PATH_SIZE bytes. Anyone may open it, as the service
// may run as another user. Path is empty after a failure.
//
static int MakeFifo(const char* Dir, const char* Name, char* Path)
{
    for (int Attempt = 0; Attempt < 10; Attempt++)
    {
        struct timespec Now;
        clock_gettime(CLOCK_MONOTONIC, &Now);
        unsigned Number = (unsigned)Now.tv_nsec ^ ((unsigned)Attempt << 24);
        int Length = snprintf(Path, FIFO_PATH_SIZE, "%s%s_%d_%u", Dir, Name,
                              (int)getpid(), Number);
        if (Length < 0 || Length >= FIFO_PATH_SIZE)
            break;
        if (mkfifo(Path, 0777) == 0)
        {
            // mkfifo applies the umask; chmod does not.
            if (chmod(Path, 0777) == 0)
                return OS_PIPE_OK;
            unlink(Path);
            break;
        }
        if (errno != EEXIST)
            break;
    }
    Path[0] = '\0';
    return OS_PIPE_ERROR;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OpenListener -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Opens the service's listener FIFO for writing. Until the service has opened it for
// reading, the open fails with ENXIO, so it is tried again until Deadline. Sets *Fd.
//
static int OpenListener(const char* Path, uint64_t Deadline, int* Fd)
{
    for (;;)
    {
        *Fd = open(Path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (*Fd >= 0)
            return OS_PIPE_OK;
        if (errno == ENOENT)
            return OS_PIPE_NOT_FOUND;
        if (errno != ENXIO && errno != EINTR)
            return OS_PIPE_ERROR;
        if (RemainingMs(Deadline) == 0)
            return errno == ENXIO ? OS_PIPE_NOT_FOUND : OS_PIPE_TIMEOUT;
        OsTime_SleepMs(LISTENER_RETRY_MS);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Handshake -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Tells the service about the client's two FIFOs and opens them. Pipe's descriptors are
// set as far as it got.
//
static int Handshake(OsPipe* Pipe, const char* ListenerPath, const ConnectMsg* Msg,
                     uint64_t Deadline)
{
    // The client's read end is opened first, so that the service can open it for
    // writing as soon as it reads the message.
    Pipe->ReadFd = open(Msg->ServiceToClient, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (Pipe->ReadFd < 0)
        return OS_PIPE_ERROR;

    int Listener = -1;
    int Outcome = OpenListener(ListenerPath, Deadline, &Listener);
    if (Outcome != OS_PIPE_OK)
        return Outcome;
    Outcome = WriteAll(Listener, (const uint8_t*)Msg, sizeof(*Msg), Deadline);
    close(Listener);
    if (Outcome != OS_PIPE_OK)
        return Outcome;

    uint8_t Answer[4];
    Outcome = ReadAll(Pipe->ReadFd, Answer, sizeof(Answer), Deadline);
    if (Outcome != OS_PIPE_OK)
        return Outcome;
    uint32_t Result = (uint32_t)Answer[0] | (uint32_t)Answer[1] << 8 |
                      (uint32_t)Answer[2] << 16 | (uint32_t)Answer[3] << 24;
    if (Result != 0)
        return OS_PIPE_ERROR;

    // The service opened this one for reading before it answered.
    Pipe->WriteFd = open(Msg->ClientToService, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    return Pipe->WriteFd >= 0 ? OS_PIPE_OK : OS_PIPE_ERROR;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void OsPipe_Close(OsPipe* Pipe)
{
    if (Pipe == NULL)
        return;
    if (Pipe->ReadFd >= 0)
        close(Pipe->ReadFd);
    if (Pipe->WriteFd >= 0)
        close(Pipe->WriteFd);
    DtAlloc_Free(Pipe);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes the two FIFOs, shakes hands, and removes the FIFOs' names again: once both ends
// are open, the names are not needed, and nothing is left behind when the program ends.
//
int OsPipe_Connect(const char* Name, int TimeoutMs, OsPipe** Pipe)
{
    if (Pipe != NULL)
        *Pipe = NULL;
    if (Name == NULL || Name[0] == '\0' || strchr(Name, '/') != NULL || TimeoutMs < 0 ||
        Pipe == NULL)
        return OS_PIPE_ERROR;

    char Dir[FIFO_PATH_SIZE];
    char ListenerPath[FIFO_PATH_SIZE];
    if (!PipeDir(Dir, sizeof(Dir)))
        return OS_PIPE_ERROR;
    int Length = snprintf(ListenerPath, sizeof(ListenerPath), "%s%s", Dir, Name);
    if (Length < 0 || (size_t)Length >= sizeof(ListenerPath))
        return OS_PIPE_ERROR;

    OsPipe* New = (OsPipe*)DtAlloc_Malloc(sizeof(OsPipe));
    if (New == NULL)
        return OS_PIPE_NO_MEMORY;
    New->ReadFd = -1;
    New->WriteFd = -1;

    ConnectMsg Msg;
    memset(&Msg, 0, sizeof(Msg));
    uint64_t Deadline = OsTime_MonotonicMs() + (uint64_t)TimeoutMs;
    int Outcome = MakeFifo(Dir, Name, Msg.ClientToService);
    if (Outcome == OS_PIPE_OK)
        Outcome = MakeFifo(Dir, Name, Msg.ServiceToClient);
    if (Outcome == OS_PIPE_OK)
        Outcome = Handshake(New, ListenerPath, &Msg, Deadline);
    if (Msg.ClientToService[0] != '\0')
        unlink(Msg.ClientToService);
    if (Msg.ServiceToClient[0] != '\0')
        unlink(Msg.ServiceToClient);
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
    if (Pipe == NULL || (Buf == NULL && Size > 0) || TimeoutMs < 0)
        return OS_PIPE_ERROR;
    return ReadAll(Pipe->ReadFd, (uint8_t*)Buf, Size,
                   OsTime_MonotonicMs() + (uint64_t)TimeoutMs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPipe_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsPipe_Write(OsPipe* Pipe, const void* Buf, size_t Size, int TimeoutMs)
{
    if (Pipe == NULL || (Buf == NULL && Size > 0) || TimeoutMs < 0)
        return OS_PIPE_ERROR;
    return WriteAll(Pipe->WriteFd, (const uint8_t*)Buf, Size,
                    OsTime_MonotonicMs() + (uint64_t)TimeoutMs);
}

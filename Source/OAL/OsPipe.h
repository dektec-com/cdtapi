// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* OsPipe.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - A connection to a local service through its pipe
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Connects to a service on this computer that listens on a pipe, such as DtapiService,
// and carries bytes both ways. The bytes are a stream: a read gets what the service
// wrote, however it split its writes.
//
// On Windows the pipe is the named pipe \\.\pipe\<Name>.
//
// On Linux it is a pair of FIFOs, as DtapiService has them. The service listens on the
// FIFO <Dir><Name>. A client makes two FIFOs of its own in <Dir>, one for each
// direction, writes their names to the listener in one message, and waits for the
// service to answer that it has opened them. <Dir> is the environment variable
// DTAPI_PIPES_PATH when it is set, else /run/DtapiServiced/ when that exists, else /tmp/.
//
// There is no emulated pipe: CDTAPI_SIM does not change where a pipe connects.
//
// Every function returns one of the OS_PIPE_ outcomes below.
//

// The outcomes.
#define OS_PIPE_OK 0
#define OS_PIPE_NOT_FOUND -1 // No service listens on the pipe
#define OS_PIPE_TIMEOUT -2   // The time ran out before all bytes were read or written
#define OS_PIPE_CLOSED -3    // The service closed its end
#define OS_PIPE_NO_MEMORY -4 // Not enough memory
#define OS_PIPE_ERROR -5     // An invalid argument, or any other failure

typedef struct OsPipe OsPipe;

// Closes the connection. A NULL Pipe does nothing.
void OsPipe_Close(OsPipe* Pipe);

// Connects to the service listening on the pipe Name, waiting at most TimeoutMs for it
// to accept. *Pipe is NULL after a failure.
int OsPipe_Connect(const char* Name, int TimeoutMs, OsPipe** Pipe);

// Reads exactly Size bytes into Buf, waiting at most TimeoutMs for them all. After
// OS_PIPE_TIMEOUT some of them may have been read, and the connection is of no further
// use.
int OsPipe_Read(OsPipe* Pipe, void* Buf, size_t Size, int TimeoutMs);

// Writes the Size bytes of Buf, waiting at most TimeoutMs for the service to take them
// all. On Windows each call is one message of the pipe.
int OsPipe_Write(OsPipe* Pipe, const void* Buf, size_t Size, int TimeoutMs);

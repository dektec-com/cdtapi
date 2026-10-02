// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvPipe.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A pipe of an IP port, its shared buffer, and the packets in the buffer
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtAvStream.h"       // The packet sink.
#include "DtPcie/DtPcieCmd.h" // The pipe commands.
#include "OAL/OsDmaBuffer.h"  // The shared buffer.
#include "cdtapi.h"           // Results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pipe +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A pipe is the path between the network function of an IP port and the process: the
// card writes received packets into a buffer the process reads, or reads the packets to
// transmit from a buffer the process writes. The buffer is shared between the process
// and the card, and its size is a whole number of the pipe's prefetch size (PrefetchSize
// pages of DT_AV_PIPE_PAGE_BYTES).
//
// The card and the process each keep an offset in the buffer. For a receive pipe the
// process keeps the read offset and asks the driver for the write offset; for a transmit
// pipe it is the other way round. One data word before the other side's offset always
// stays free, so that a full buffer and an empty one look different.
//

// The page size a buffer is rounded to, whatever the operating system's page size.
#define DT_AV_PIPE_PAGE_BYTES 4096

// The largest packet a pipe holds, in bytes: a packet header counts at most 2,047 words
// of 8 bytes.
#define DT_AV_PIPE_MAX_PACKET (2047 * 8)

typedef struct DtAvPipe
{
    OsDrv* Drv;               // The driver the pipe is opened through
    DtDrvObject Nw;           // The network function the pipe belongs to
    DtDrvObject Object;       // The pipe; its UUID is 0 while no pipe is open
    DtPipeProps Props;        // The pipe's properties, read when it is opened
    OsDmaBuffer SharedBuffer; // The buffer shared with the card
    bool BufferRegistered;    // Whether the driver has been given the buffer
    uint32_t BufferSize;      // The size of the buffer in bytes
    uint32_t Offset; // The process's offset: read for receive, write for transmit
} DtAvPipe;

// Opens a pipe of network function Nw and reads its properties. Type is the kind of
// pipe wanted, a DT_PIPE_ value; when every pipe of that kind is in use, a pipe of kind
// Fallback is opened instead (-1 for no fallback). Returns DTAPI_OK, DTAPI_E_DEV_DRIVER
// when the pipe reports a data width that is not a multiple of 32 bits or a prefetch
// size under 1, or the errors of the driver.
DtapiResult DtAvPipe_Open(DtAvPipe* Pipe, OsDrv* Drv, DtDrvObject Nw, int Type,
                          int Fallback);

// Stops the pipe and gives it a shared buffer of at least Size bytes, rounded up to a
// whole number of prefetch sizes. The process's offset starts at 0. Returns:
//
//   DTAPI_OK              The buffer is set
//   DTAPI_E_INVALID_ARG   No pipe is open, it has a buffer already, or Size is 0 or
//                         larger than INT32_MAX / 2
//   DTAPI_E_OUT_OF_MEM    The buffer could not be allocated
//
// and the errors of the driver.
DtapiResult DtAvPipe_SetBuffer(DtAvPipe* Pipe, size_t Size);

// Stops the pipe, takes its buffer back from the driver, frees it and closes the pipe.
// Steps that do not apply, such as a buffer that was never set, are skipped. The pipe
// can be opened again afterwards.
void DtAvPipe_Close(DtAvPipe* Pipe);

// Return whether the pipe is a hardware pipe, whether it takes jumbo frames, and the
// size in bytes of its data word, to which every packet is padded.
bool DtAvPipe_IsHardware(const DtAvPipe* Pipe);
bool DtAvPipe_IsJumbo(const DtAvPipe* Pipe);
int DtAvPipe_Alignment(const DtAvPipe* Pipe);

// Returns how many bytes the buffer can hold at once: its size less the data word that
// always stays free.
uint32_t DtAvPipe_UsableBytes(const DtAvPipe* Pipe);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A writer writes packets into the buffer of a transmit pipe. It offers itself as a
// DtAvTxSink, so a stream writes its packets straight into the buffer. A packet that does
// not fit before the end of the buffer is first written into WrapPacket and then copied
// in two parts, around the end. The writer tells the driver how far it has written every
// DT_AV_WRITER_FLUSH_EVERY_PACKETS packets, and when it is flushed.
//

#define DT_AV_WRITER_FLUSH_EVERY_PACKETS 100

typedef struct DtAvWriter
{
    // The pipe written to.
    DtAvPipe* Pipe;

    // The sink a stream writes its packets through.
    DtAvTxSink Sink;

    // A packet that wraps around the end of the buffer, and whether the packet being
    // written is in it.
    uint8_t WrapPacket[DT_AV_PIPE_MAX_PACKET];
    bool IsInWrapPacket;

    // The packets written that the driver has not been told of yet.
    int UnflushedPackets;

    // The first automatic flush that failed since the last DtAvWriter_Flush.
    DtapiResult FirstFlushFailure;
} DtAvWriter;

// Sets up a writer on a pipe that has a buffer. Writing starts at the pipe's offset.
void DtAvWriter_Init(DtAvWriter* Writer, DtAvPipe* Pipe);

// Gets how many bytes can be written now, in *Free: up to one data word before the
// card's read offset. Returns DTAPI_OK, DTAPI_E_DEV_DRIVER when the driver reports a read
// offset beyond the buffer, or the errors of the driver.
DtapiResult DtAvWriter_FreeBytes(DtAvWriter* Writer, uint32_t* Free);

// Tells the driver how far the writer has written, so that the card sends every packet
// written so far. Returns the first failure of a flush since the last call, including
// the automatic ones, or DTAPI_OK.
DtapiResult DtAvWriter_Flush(DtAvWriter* Writer);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A reader takes the received packets out of the buffer of a receive pipe. Each pass
// hands every complete packet between the process's read offset and the card's write
// offset to a function, then moves the read offset past them. A packet that wraps around
// the end of the buffer is copied into WrapPacket first, so the function always gets it
// in one piece.
//
// A packet header that is not valid, or that gives a size larger than a pipe holds, means
// the reader has lost track of where packets start. The pass then skips everything up to
// the write offset and reports that it lost sync.
//

typedef struct DtAvReader
{
    DtAvPipe* Pipe;                            // The pipe read from
    uint8_t WrapPacket[DT_AV_PIPE_MAX_PACKET]; // A packet that wraps around the end
} DtAvReader;

// Handles one received packet of Size bytes.
typedef void (*DtAvPacketFunc)(void* Context, const uint8_t* Packet, int Size);

// Sets up a reader on a pipe that has a buffer. Reading starts at the pipe's offset.
void DtAvReader_Init(DtAvReader* Reader, DtAvPipe* Pipe);

// Hands every complete packet in the buffer to Func, and moves the read offset past them.
// *Packets gets how many packets were handed over, and *LostSync whether the reader lost
// track of the packet boundaries. Returns DTAPI_OK, DTAPI_E_DEV_DRIVER when the driver
// reports a write offset beyond the buffer, or the errors of the driver.
DtapiResult DtAvReader_Pass(DtAvReader* Reader, DtAvPacketFunc Func, void* Context,
                            int* Packets, bool* LostSync);

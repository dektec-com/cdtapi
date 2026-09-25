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
// A pipe of the network function, opened by type with a fallback, and its shared buffer,
// a whole number of the pipe's prefetch size, PrefetchSize pages of
// DT_AV_PIPE_PAGE_BYTES, page-aligned, with one data word kept free. The process keeps
// its own offset in the buffer: the read offset of a receive pipe, the write offset of a
// transmit pipe.
//

// A buffer is rounded to 4 KB pages, whatever the operating system's page size.
#define DT_AV_PIPE_PAGE_BYTES 4096

// The largest packet a pipe holds: the most words a header counts, 2,047, of 8 bytes.
#define DT_AV_PIPE_MAX_PACKET (2047 * 8)

typedef struct DtAvPipe
{
    OsDrv* Drv;
    DtDrvObject Nw;     // The network function
    DtDrvObject Object; // The pipe; its UUID 0 while none is open
    DtPipeProps Props;
    OsDmaBuffer SharedBuffer;
    bool BufferRegistered; // The driver has the buffer
    uint32_t BufferSize;   // Bytes of the buffer
    uint32_t Offset;
} DtAvPipe;

// Opens a pipe of network function Nw, of Type, a DT_PIPE_ value, or of Fallback, -1 for
// none, when every pipe of Type is in use, and reads its properties. DTAPI_E_DEV_DRIVER
// for a data width under 8 or not a multiple of 32 bits, or a prefetch size under 1.
DtapiResult DtAvPipe_Open(DtAvPipe* Pipe, OsDrv* Drv, DtDrvObject Nw, int Type,
                          int Fallback);

// Idles the pipe and gives it a buffer of at least Size bytes, rounded up to whole
// prefetch sizes. The offset starts at 0. DTAPI_E_INVALID_ARG for no open pipe, a buffer
// already given, or a Size of 0 or over INT32_MAX / 2; DTAPI_E_OUT_OF_MEM when the buffer
// cannot be allocated.
DtapiResult DtAvPipe_SetBuffer(DtAvPipe* Pipe, size_t Size);

// Idles the pipe, takes its buffer back, frees it and closes the pipe; what is not there
// is skipped. The pipe can be opened again.
void DtAvPipe_Close(DtAvPipe* Pipe);

// Whether the pipe is a hardware pipe; whether it takes jumbo frames; the bytes a packet
// pads to.
bool DtAvPipe_IsHardware(const DtAvPipe* Pipe);
bool DtAvPipe_IsJumbo(const DtAvPipe* Pipe);
int DtAvPipe_Alignment(const DtAvPipe* Pipe);

// The bytes the buffer can hold at once: its size less a data word.
uint32_t DtAvPipe_UsableBytes(const DtAvPipe* Pipe);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A writer is a DtAvTxSink over the buffer: a packet is written in place where it fits
// before the end of the buffer, and otherwise into WrapPacket, which is copied in two
// parts around the end. The driver hears of the written packets every
// DT_AV_WRITER_FLUSH_EVERY_PACKETS packets and at a flush.
//

#define DT_AV_WRITER_FLUSH_EVERY_PACKETS 100

typedef struct DtAvWriter
{
    DtAvPipe* Pipe;
    DtAvTxSink Sink; // Writes through this writer
    uint8_t WrapPacket[DT_AV_PIPE_MAX_PACKET];
    bool IsInWrapPacket;
    int UnflushedPackets; // Packets the driver has not heard of
    DtapiResult
        FirstFlushFailure; // The first failed flush since the last DtAvWriter_Flush
} DtAvWriter;

// Sets up a writer on a pipe with a buffer, from the pipe's offset.
void DtAvWriter_Init(DtAvWriter* Writer, DtAvPipe* Pipe);

// The bytes that can be written now: up to a data word before the pipe's read offset.
// DTAPI_E_DEV_DRIVER when the driver's read offset lies beyond the buffer.
DtapiResult DtAvWriter_FreeBytes(DtAvWriter* Writer, uint32_t* Free);

// Tells the driver of every packet written. Returns the first failure since the last
// flush.
DtapiResult DtAvWriter_Flush(DtAvWriter* Writer);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// One pass over the buffer: the packets between the process's read offset and the pipe's
// write offset, each in one piece, a packet around the end of the buffer copied into
// WrapPacket. A header that does not check, or a packet larger than a pipe holds,
// means the process lost the packet boundaries: the read offset jumps to the write
// offset and the pass reports it. The read offset moves on after the pass.
//

typedef struct DtAvReader
{
    DtAvPipe* Pipe;
    uint8_t WrapPacket[DT_AV_PIPE_MAX_PACKET];
} DtAvReader;

// Takes one packet of Size bytes.
typedef void (*DtAvPacketFunc)(void* Context, const uint8_t* Packet, int Size);

// Sets up a reader on a pipe with a buffer, from the pipe's offset.
void DtAvReader_Init(DtAvReader* Reader, DtAvPipe* Pipe);

// Hands every whole packet in the buffer to Func. *Packets receives their number and
// *LostSync whether the boundaries were lost. DTAPI_E_DEV_DRIVER when the driver's write
// offset lies beyond the buffer.
DtapiResult DtAvReader_Pass(DtAvReader* Reader, DtAvPacketFunc Func, void* Context,
                            int* Packets, bool* LostSync);

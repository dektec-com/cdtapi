// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvStream.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the packetizers and parsers of every substandard share
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "DtAvFrame.h"  // Frames and their pool.
#include "DtAvPacket.h" // Packet headers.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmission +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// A packetizer splits a frame into packets and writes each one into memory that a sink
// gives it. A transmit FIFO's sink is the pipe's shared buffer; a test's sink is plain
// memory. The packetizer does not check for room: its caller has made sure beforehand
// that all the frame's packets fit.
//

// Where a packetizer writes its packets.
typedef struct DtAvTxSink
{
    // Returns where to write the next packet, which is at most MaxSize bytes.
    uint8_t* (*ReserveRoom)(void* Context, int MaxSize);

    // Tells the sink the packet is written, and how big it is, padding included.
    void (*CommitPacket)(void* Context, int Size);

    void* Context; // Passed to both functions
} DtAvTxSink;

// What every packet of a transmitted stream has in common.
typedef struct DtAvTxStream
{
    DtAvNet Net;                 // The packets' addresses and network headers
    int PayloadType;             // The RTP payload type
    uint32_t Ssrc;               // The RTP synchronisation source
    int64_t OutputDelayNs;       // How much later than its time a packet leaves the card
    uint32_t NextSequenceNumber; // The sequence number of the next packet; ST 2110-20
                                 // uses all 32 bits
} DtAvTxStream;

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reception +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A parser takes the received packets of a stream one at a time and builds frames from
// them, in frames it gets from a pool. It hands each complete frame to a sink. When the
// sink refuses the frame because the FIFO is full, the frame counts as dropped and goes
// back to the pool.
//

// Where a parser delivers its frames.
typedef struct DtAvRxSink
{
    DtAvFramePool* Pool; // The pool the parser gets its frames from

    // Takes a complete frame. Returns false when there is no room for it.
    bool (*Deliver)(void* Context, DtAvFrame* Frame);

    void* Context; // Passed to Deliver
} DtAvRxSink;

// Delivers a complete frame to the sink and counts it as received. When the sink refuses
// it, counts it as dropped too and puts it back in the pool.
static inline void DtAvRxSink_Deliver(const DtAvRxSink* Target, RxStatistics* Stats,
                                      DtAvFrame* Frame)
{
    Stats->FramesOk++;
    if (!Target->Deliver(Target->Context, Frame))
    {
        Stats->DroppedFrames++;
        DtAvFramePool_Return(Target->Pool, &Frame->Frame);
    }
}

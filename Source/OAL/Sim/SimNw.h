// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimNw.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated network function of an IP port, its pipes and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Network function +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The NW driver function of the emulated DTA-2110's IP port. It behaves as the driver's
// network function and its pipes do:
//
//   EMAC        Returns the MAC address and the PHY speed. Other commands are refused as
//               not supported.
//   NW          Opens and closes pipes; other commands are refused. The pipes below
//               SIM_NW_FIRST_TX_HWP are the driver's own queues: opening their types
//               finds them in use. The hardware transmit pipes start at
//               SIM_NW_FIRST_TX_HWP and the hardware receive pipes at
//               SIM_NW_FIRST_RX_HWP; the first free one is opened. Software pipes are
//               created from SIM_NW_FIRST_SWP up to SIM_NW_MAX_PIPES. Only IN_USE makes
//               opening try the fallback type. Closing checks that the pipe exists, is in
//               use and was opened by the handle. A handle that closes closes its pipes.
//   PIPE        Takes every command except the events and the driver's own buffer, which
//               are refused as not supported. Works on any pipe from 5 up, in use or
//               not; pipes 1 to 4, and pipes that do not exist, are an invalid parameter.
//               The buffer is the process's: on Linux at the address in the input, on
//               Windows in the output. A hardware pipe takes a buffer of at most 256 MB
//               that starts on a page and is a multiple of its prefetch pages; a software
//               pipe takes any buffer. A pipe that has a buffer refuses a second one as
//               in use.
//
// Packets move on the card's time of day: the host's UTC time, or a clock a test sets.
// At each command or test control, the emulator moves them as the driver would have
// since the last one:
//
//   transmit    A hardware transmit pipe in RUN hands its packets to the scheduler at
//               once. A software transmit pipe in RUN does so only at the periodic
//               interval, every SIM_NW_INTERVAL_NS, and only the packets due within
//               SIM_NW_LOOKAHEAD_NS from now, earliest first over all software pipes. A
//               packet whose time lies more than SIM_NW_MAX_DELAY_NS from the card's
//               time of day sets the invalid-time error, and stops the pipe until it is
//               flushed. The scheduler sends each packet at its time, or at once when
//               that time has passed.
//   wire        The last SIM_NW_KEPT_PACKETS sent packets are kept for the tests. With
//               the loopback on, every sent packet also arrives at the receive side, as
//               does a frame a test injects.
//   receive     An arriving packet goes to the first hardware receive pipe whose filter
//               takes it. That pipe writes it at once when it runs, and loses it
//               otherwise. Any other packet waits for the next periodic interval. It is
//               then copied to every software receive pipe in RUN whose filter takes it,
//               or counted for the operating system when none does. A pipe without room
//               loses the packet and gets the overflow error, which its next packet
//               clears.
//
// A packet written to a receive pipe has the header of DtEthIp.h, with the arrival time
// and a valid time stamp, and is padded to the port's packet alignment.
//
// The caller of a command holds the emulator's lock. The test controls take it
// themselves.
//

// The periodic interval of the software pipes, and how far past an interval's tick a
// software transmit pipe looks.
#define SIM_NW_INTERVAL_NS 10000000ull
#define SIM_NW_LOOKAHEAD_NS (SIM_NW_INTERVAL_NS + SIM_NW_INTERVAL_NS / 2)

// How far a transmitted packet's time may lie from the card's time of day.
#define SIM_NW_MAX_DELAY_NS 10000000000ull

// The pipe numbers: the first hardware transmit pipe, the first hardware receive pipe,
// the first software pipe, and the highest pipe number. Pipes 1 to 4 are the driver's own
// queues.
#define SIM_NW_FIRST_TX_HWP 5
#define SIM_NW_FIRST_RX_HWP 8
#define SIM_NW_FIRST_SWP 11
#define SIM_NW_MAX_PIPES 2047

// The prefetch size, in pages, that the hardware pipes report.
#define SIM_NW_HWP_PREFETCH_PAGES 16

// The number of sent packets kept for the tests.
#define SIM_NW_KEPT_PACKETS 256

// Returns true when the network function takes commands with this DT_FUNC_CODE_.
bool SimNw_Handles(int FunctionCode);

// Carries out a command from Handle for the network function. For a PIPE command, and
// for closing a pipe, the header's UUID names the pipe. Returns the DtStatus the driver
// would, and fills Out and *OutSize for a command that answers.
uint32_t SimNw_Cmd(void* Handle, int Uuid, int FunctionCode, int Cmd, const void* In,
                   size_t InSize, void* Out, size_t* OutSize);

// Closes the pipes Handle opened, as closing a file does in the driver.
void SimNw_CloseHandle(void* Handle);

// Returns the card's time of day, in nanoseconds.
uint64_t SimDtPcie_Now(void);

// Frees everything, and restores the power-on state of the function and of the controls.
void SimNw_Reset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Makes a pipe take its buffer as the Linux driver does, at the address in the input,
// when AsLinux is true, and as the Windows driver does, in the output, otherwise. After a
// reset, it takes it as the driver of the platform the emulator is built for.
void SimDtPcie_RegisterPipeBufferAsLinux(bool AsLinux);

// Takes the link up, with a PHY speed of 10 Gb/s, as after a reset, or down.
void SimDtPcie_SetNwLink(bool Up);

// Sets the card's time of day to TodNs. It then stands still until a test advances it;
// after a reset, it follows the host's clock again. The time commands of every emulated
// device use this time of day.
void SimDtPcie_SetNwTime(uint64_t TodNs);

// Advances the time of day a test set by Ns, and moves the packets that are due.
void SimDtPcie_AdvanceNwTime(uint64_t Ns);

// Makes sent packets arrive at the receive side when Loopback is true. After a reset it
// is off, unless CDTAPI_SIM_LOOPBACK is set to something other than 0 or empty. That is
// how a program without test controls of its own, such as an example, turns it on.
void SimDtPcie_SetNwLoopback(bool Loopback);

// Makes the Ethernet frame of Size bytes at Frame arrive at the receive side at TodNs.
// Returns false when it cannot be kept.
bool SimDtPcie_InjectNwFrame(const uint8_t* Frame, size_t Size, uint64_t TodNs);

// A packet the port sent.
typedef struct SimNwPacket
{
    uint64_t TodNs;       // When it went out
    int PipeId;           // The pipe it came from
    const uint8_t* Frame; // Valid until the next command or control
    size_t Size;          // Bytes of the frame
} SimNwPacket;

// Returns the number of packets sent since the reset.
int SimDtPcie_NwSentCount(void);

// Returns in *Packet the kept packet at Index, 0 for the oldest kept. Returns false when
// there is none.
bool SimDtPcie_GetNwSent(int Index, SimNwPacket* Packet);

// Returns the number of packets kept, at most SIM_NW_KEPT_PACKETS.
int SimDtPcie_NwKeptCount(void);

// The state of a pipe.
typedef struct SimNwPipeState
{
    bool Exists;           // The pipe exists
    bool InUse;            // The pipe is open
    int Type;              // A DT_PIPE_ value
    int OpMode;            // Its operational mode
    bool BufferRegistered; // It has a buffer
    size_t BufferSize;     // The buffer's size, in bytes
    uint32_t ReadOffset;   // The buffer's read offset
    uint32_t WriteOffset;  // The buffer's write offset
    uint32_t ErrorFlags;   // Its error flags
    bool FilterSet;        // It has a receive filter
    uint32_t FilterFlags;  // The filter's flags
    int Scheduled;         // Packets of the pipe waiting in the scheduler
} SimNwPipeState;

// Returns in *State the state of pipe PipeId.
void SimDtPcie_GetNwPipeState(int PipeId, SimNwPipeState* State);

// What happened to received packets and transmitted headers.
typedef struct SimNwCounters
{
    int ToOperatingSystem; // Received packets no pipe took
    int Lost;              // Received packets that could not be written or queued
    int HeaderErrors;      // Headers at a transmit pipe's read offset that did not check
} SimNwCounters;

// Returns the counters in *Counters.
void SimDtPcie_GetNwCounters(SimNwCounters* Counters);

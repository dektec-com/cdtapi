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
// The NW driver function of the emulated DTA-2110's IP port, as the driver's network
// function and its pipes have it:
//
//   EMAC        the MAC address and the PHY speed are answered; the other commands are
//               refused as not supported
//   NW          opening and closing pipes are answered, the other commands refused. The
//               driver keeps pipes 1 to 4 for its own queues, which opening their types
//               finds in use; pipes 5 to 7 are the hardware transmit pipes and 8 to 10
//               the hardware receive pipes, opened first free first; software pipes are
//               created from 11 up to 2,047. Only IN_USE tries the fallback type. Closing
//               checks the pipe exists, is in use and was opened by the handle, and a
//               closing handle closes its pipes
//   PIPE        every command except the events and the driver's own buffer, which are
//               refused as not supported, on any pipe from 5 up whether in use or not;
//               pipes 1 to 4 and pipes that do not exist are an invalid parameter. The
//               buffer is the process's, as on Linux from the address in the input or as
//               on Windows from the output; a hardware pipe takes one of at most 256 MB
//               that starts on a page and is a multiple of its prefetch pages, a
//               software pipe any; a pipe that has a buffer refuses a second as in use
//
// Packets move on the card's time of day, which is the host's UTC time or a clock a test
// sets, whenever a command or test control comes, as the driver would have moved them
// since the last one:
//
//   transmit    a hardware transmit pipe in RUN hands its packets to the scheduler at
//               once; a software transmit pipe in RUN only at the periodic interval,
//               every 10 ms, and then only the packets whose time lies before the
//               interval plus 15 ms, earliest first over all software pipes. A packet
//               more than 10 s from the time sets invalid time and stops the pipe until
//               it is flushed. The scheduler sends each packet at its time, or at once
//               when that has passed
//   wire        the last SIM_NW_KEPT_PACKETS sent packets are kept for the tests, and,
//               with the loopback on, every sent packet also arrives at the receive
//               side, as does a frame a test injects
//   receive     an arriving packet goes to the first hardware receive pipe whose filter
//               takes it, which writes it at once when running and loses it otherwise;
//               any other packet waits for the next periodic interval, which copies it
//               to every software receive pipe in RUN whose filter takes it and counts it
//               for the operating system when none does. A pipe without room loses the
//               packet and gets the overflow error, which its next packet clears
//
// A packet written to a receive pipe has the header of DtEthIp.h with the arrival time
// and a valid time stamp, and is padded to the port's packet alignment.
//
// The commands are called with the emulator's lock held; the test controls take the lock
// themselves.
//

// The periodic interval of the software pipes, and how far past an interval's tick a
// software transmit pipe looks.
#define SIM_NW_INTERVAL_NS 10000000ull
#define SIM_NW_LOOKAHEAD_NS (SIM_NW_INTERVAL_NS + SIM_NW_INTERVAL_NS / 2)

// How far from the time a transmitted packet may be.
#define SIM_NW_MAX_DELAY_NS 10000000000ull

// Pipe numbers: the first hardware transmit, hardware receive and software pipe, and the
// highest pipe number. Pipes 1 to 4 are the driver's own queues.
#define SIM_NW_FIRST_TX_HWP 5
#define SIM_NW_FIRST_RX_HWP 8
#define SIM_NW_FIRST_SWP 11
#define SIM_NW_MAX_PIPES 2047

// The prefetch size in pages the hardware pipes report.
#define SIM_NW_HWP_PREFETCH_PAGES 16

// The sent packets kept for the tests.
#define SIM_NW_KEPT_PACKETS 256

// Whether the network function takes commands with this DT_FUNC_CODE_.
bool SimNw_Takes(int FunctionCode);

// Handles a command from Handle with this function code for the network function, the
// header's UUID naming the pipe for a PIPE command and for closing one. Returns the
// DtStatus the driver would, and fills Out and *OutSize for a command that answers.
uint32_t SimNw_Cmd(void* Handle, int Uuid, int FunctionCode, int Cmd, const void* In,
                   size_t InSize, void* Out, size_t* OutSize);

// Closes the pipes Handle opened, as closing a file does in the driver.
void SimNw_CloseHandle(void* Handle);

// The card's time of day in nanoseconds.
uint64_t SimNw_Now(void);

// Frees everything and restores the power-on state of the function and the controls.
void SimNw_Reset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Makes a pipe take its buffer as the Linux driver does, from the address in the input,
// when true, or as the Windows driver does, as the output. After a reset it takes it as
// the driver of the platform the emulator is built for.
void SimDtPcie_RegisterPipeBufferAsLinux(bool AsLinux);

// Takes the link up, as after a reset, with a PHY speed of 10 Gb/s, or down.
void SimDtPcie_SetNwLink(bool Up);

// Makes the card's time of day TodNs from now on, standing still until advanced; its time
// of day follows the host's clock again after a reset. It is the time of day of the time
// commands of every emulated device.
void SimDtPcie_SetNwTime(uint64_t TodNs);

// Advances the time of day a test set by Ns, and moves the packets that are due.
void SimDtPcie_AdvanceNwTime(uint64_t Ns);

// Makes sent packets arrive at the receive side when true. After a reset it is off,
// unless CDTAPI_SIM_LOOPBACK holds a value other than 0 or nothing, which is how a
// program with no test controls of its own, such as an example, asks for it.
void SimDtPcie_SetNwLoopback(bool Loopback);

// Lets the Ethernet frame of Size bytes at Frame arrive at the receive side at TodNs.
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

// The number of packets sent since the reset.
int SimDtPcie_NwSentCount(void);

// The kept packet at Index, 0 for the oldest kept. Returns false when there is none.
bool SimDtPcie_GetNwSent(int Index, SimNwPacket* Packet);

// The number of packets kept, at most SIM_NW_KEPT_PACKETS.
int SimDtPcie_NwKeptCount(void);

// What a pipe is and holds.
typedef struct SimNwPipeState
{
    bool Exists;
    bool InUse;
    int Type; // A DT_PIPE_ value
    int OpMode;
    bool BufferSet;
    size_t BufferSize;
    uint32_t ReadOffset;
    uint32_t WriteOffset;
    uint32_t ErrorFlags;
    bool FilterSet;
    uint32_t FilterFlags;
    int Scheduled; // Packets of the pipe waiting in the scheduler
} SimNwPipeState;

void SimDtPcie_GetNwPipeState(int PipeId, SimNwPipeState* State);

// What happened to received packets and transmitted headers.
typedef struct SimNwCounters
{
    int ToOperatingSystem; // Received packets no pipe took
    int Lost;              // Received packets that could not be written or queued
    int HeaderErrors;      // Headers at a transmit pipe's read offset that did not check
} SimNwCounters;

void SimDtPcie_GetNwCounters(SimNwCounters* Counters);

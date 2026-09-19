// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimChSdiRx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated SDI receive channels, their frame source and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Channels +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Each SDI port of the emulated card has a CHSDIRX channel that behaves as the driver's
// DtDfChSdiRx and as a DTA-2178 was seen to answer:
//
//   users        up to eight handles attach, each with a friendly name; an exclusive user
//                excludes every other, and a handle that is no user is not found
//   ring         configuring allocates it, rounded up to whole multiples of the prefetch
//                size in pages, from that size up to 256 MB; a size outside that range
//                is refused and leaves the channel unconfigured; the last data word is
//                kept free, so the load reaches the size less 32 bytes at most
//   mapping      as on Windows the command returns the ring's address; as on Linux it
//                returns 0 until the handle has mapped the ring from its port's segment
//   events       while a user runs, each wait for a format event is the next quarter of
//                a frame: the source writes that part of the frame into the ring and the
//                event names the frame, its quarter and whether the formatter is in sync;
//                a channel that does not run answers a wait with a time-out
//
// The source writes the SDI RX simple format of a video standard, as DtSdiFrame
// describes it, with the symbols SimChSdiRx_Line gives. It is in sync only when its
// standard has the geometry the channel is configured for; otherwise nothing is written
// and the events say so. A frame that does not fit in the ring is dropped from where it
// stopped fitting, and the events of its remaining quarters are out of sync.
//
// The emulator's commands are serialised by one lock, so a wait on one thread and a
// command on another do not interleave. The rest of the emulator is meant to be driven
// by one test at a time.
//

// The properties every channel reports, as the DTA-2178's did with driver 3.6.4.
#define SIM_RX_PREFETCH_PAGES 16
#define SIM_RX_PCIE_DATA_WIDTH 256
#define SIM_RX_REORDER_BUF_SIZE 8192
#define SIM_RX_STREAM_ALIGNMENT 128

// Handles a CHSDIRX command from Handle for the channel of the port at PortIndex.
// Returns the DtStatus the driver would, and fills Out and *OutSize for a command that
// answers. *SleepMs receives how long the caller sleeps after releasing the emulator's
// lock, to pace a wait without a source as a card paces its format events.
uint32_t SimChSdiRx_Cmd(void* Handle, int PortIndex, int Cmd, const void* In,
                        size_t InSize, void* Out, size_t* OutSize, int* SleepMs);

// Maps a ring for Handle as the Linux driver's mmap does: Offset names the port's
// segment, and Size must be the ring's size. NULL when that is not a configured channel
// the handle uses.
void* SimChSdiRx_Map(void* Handle, uint64_t Offset, size_t Size);

// Detaches Handle from every channel, as closing a file does in the driver.
void SimChSdiRx_CloseHandle(void* Handle);

// Frees every ring and restores the power-on state of every channel and control below.
void SimChSdiRx_Reset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The symbols of a frame are a function of the video standard, the frame number and the
// position, so that a test can work out every raw frame it should receive. A line starts
// with its EAV: SD has 3FF 000 000 XYZ, HD and 3G have each of those twice, one for each
// channel, followed by the line number and the CRC of each channel. The SAV is the same
// without the line number and CRC. XYZ carries the field, vertical blanking and EAV or
// SAV bits of the line with their protection bits. Every other symbol lies between 040
// and 3BF, so that none of them looks like timing reference.
//
// The CRC of a line's channel is SMPTE 292's CRC-18 over that channel's active part of
// the line before it, the last line of the previous frame for the first line, then the
// EAV and the line number.
//

// Fills Symbols with line Line, from 1, of frame FrameNumber of VidStd, from the first
// symbol of the EAV to the last of the active part. Symbols holds the line's symbols.
// Returns the number of symbols, or 0, writing nothing, for an unknown or 4K standard or
// a line the frame does not have.
int SimChSdiRx_Line(int VidStd, uint32_t FrameNumber, int Line, uint16_t* Symbols);

// Makes the port at PortIndex receive VidStd from now on; DTAPI_VIDSTD_UNKNOWN takes the
// source away, as after a reset. The frame numbers continue.
void SimDtPcie_SetRxSource(int PortIndex, int VidStd);

// Makes the port at PortIndex receive VidStd from now on with the frames of the file at
// Path in place of the ones SimChSdiRx_Line makes, the first again after the last. The
// file holds whole frames of 10-bit symbols, packed least significant bit first, each
// line from its EAV to the end of its active part, and each frame padded with zeros to a
// multiple of 8 bytes: what FFmpeg's sdi format holds without its header, and what
// SimDtPcie_SetSdiSink writes. Returns false, changing nothing, for a 4K or unknown
// standard, a file that cannot be read, or one that holds no whole number of frames.
bool SimChSdiRx_SetFileSource(int PortIndex, int VidStd, const char* Path);

// The one-off faults of a port's source, each applied to the next frame it starts.
typedef enum SimRxFault
{
    SIM_RX_FAULT_SYNC_WORD,   // The header's sync word is wrong
    SIM_RX_FAULT_FORMAT,      // The header names the 4K format
    SIM_RX_FAULT_SKIP_FRAME,  // A frame number is skipped before the frame
    SIM_RX_FAULT_OUT_OF_SYNC, // The frame is not written and its events are out of sync
} SimRxFault;

// Arms a fault for the next frame of the port at PortIndex.
void SimDtPcie_InjectRxFault(int PortIndex, SimRxFault Fault);

// Lets the running channel of the port at PortIndex produce Events format events without
// anyone waiting for them, as a card goes on while an application does not read.
void SimDtPcie_RunRxEvents(int PortIndex, int Events);

// Makes the channel of every port allocate at most Size bytes, so that a test can wrap
// the ring with few frames. 0 lifts the limit.
void SimDtPcie_LimitRxRing(size_t Size);

// Makes every channel report this stream alignment in bits.
void SimDtPcie_SetRxAlignment(int AlignmentBits);

// Makes the driver report a mapped ring as on Linux, when true, or as on Windows.
void SimDtPcie_MapRxRingAsLinux(bool AsLinux);

// Refuses CHSDIRX command Cmd, a DT_CHSDIRX_CMD_ value, with Status from now on; 0 ends
// it. One command can be refused at a time.
void SimDtPcie_FailRxCmd(int Cmd, uint32_t Status);

// Makes CHSDIRX command Cmd return Ms milliseconds later, after the emulator's lock is
// released, as a busy system can delay a call; Ms 0 ends it. One command at a time.
void SimDtPcie_SlowRxCmd(int Cmd, int Ms);

// What the channel of the port at PortIndex holds: whether it is configured, the ring's
// size, the number of users, and the frame number its source starts next.
typedef struct SimRxState
{
    bool Configured;
    size_t RingSize;
    int NumUsers;
    uint32_t NextFrame;
    uint32_t WriteOffset;
} SimRxState;

void SimDtPcie_GetRxState(int PortIndex, SimRxState* State);

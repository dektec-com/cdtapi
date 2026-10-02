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
// Each SDI port of the emulated card has a CHSDIRX channel. It behaves as the driver's
// does, and answers as a DTA-2178 was seen to:
//
//   users       Up to eight handles attach. An exclusive user excludes every other. A
//               handle that is not a user is not found.
//   ring        Configuring the channel allocates the ring. Its size is rounded up to a
//               whole multiple of the prefetch size in pages, and lies between that size
//               and 256 MB. A size outside that range is refused, and the channel stays
//               unconfigured. The last data word (SIM_RX_PCIE_DATA_WIDTH bits) is kept
//               free, so the load is at most the size less one data word.
//   mapping     As on Windows, the command returns the ring's address. As on Linux, it
//               returns 0 until the handle has mapped the ring from its port's segment.
//   events      While a user runs the channel, each wait for a format event brings the
//               next quarter of a frame: the source writes that part of the frame into
//               the ring, and the event names the frame, its quarter and whether the
//               formatter is in sync. A wait on a channel that does not run times out.
//
// The source writes the SDI RX simple format of a video standard, as DtSdiFrame
// describes it, with the symbols SimChSdiRx_Line() gives. It is in sync only when its
// video standard has the geometry the channel is configured for. Otherwise nothing is
// written, and the events say that it is out of sync. When a frame does not fit in the
// ring, the rest of it is dropped from where it stopped fitting, and the events of its
// remaining quarters are out of sync.
//
// One lock serialises the emulator's commands, so a wait on one thread and a command on
// another do not interleave. The test controls are meant for one test at a time.
//

// The number of symbols in the longest line the source writes: a raw 2160p50 line, which
// is four 1080p50 lines.
#define SIM_RX_MAX_LINE_SYMBOLS 21120

// The properties every channel reports, as the DTA-2178's did with driver 3.6.4.
#define SIM_RX_PREFETCH_PAGES 16
#define SIM_RX_PCIE_DATA_WIDTH 256
#define SIM_RX_REORDER_BUF_SIZE 8192
#define SIM_RX_STREAM_ALIGNMENT 128

// Carries out a CHSDIRX command from Handle for the channel of the port at PortIndex.
// Returns the DtStatus the driver would, and fills Out and *OutSize for a command that
// answers. *SleepMs is how long the caller sleeps after it releases the emulator's lock:
// a wait without a source is paced as a card paces its format events.
uint32_t SimChSdiRx_Cmd(void* Handle, int PortIndex, int Cmd, const void* In,
                        size_t InSize, void* Out, size_t* OutSize, int* SleepMs);

// Maps a ring for Handle, as the Linux driver's mmap does. Offset names the port's
// segment, and Size must be the ring's size. Returns NULL when that is not a configured
// channel the handle uses.
void* SimChSdiRx_Map(void* Handle, uint64_t Offset, size_t Size);

// Detaches Handle from every channel, as closing a file does in the driver.
void SimChSdiRx_CloseHandle(void* Handle);

// Frees every ring, and restores the power-on state of every channel and of every
// control below.
void SimChSdiRx_Reset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame source +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The symbols of a frame follow from the video standard, the frame number and the
// position. A test can therefore work out every raw frame it should receive.
//
// A line starts with its EAV. In SD, that is 3FF 000 000 XYZ. In HD and 3G, each of those
// comes twice, once for each channel, followed by the line number and the CRC of each
// channel. The SAV is the same without the line number and the CRC. XYZ holds the line's
// field, vertical blanking and EAV/SAV bits, with their protection bits. Every other
// symbol lies between 040 and 3BF, so that none looks like a timing reference.
//
// The CRC of a channel is SMPTE 292's CRC-18. It covers that channel's active part of the
// line before (for the first line, the last line of the previous frame), then the EAV and
// the line number.
//

// Fills Symbols with line Line (from 1) of frame FrameNumber of VidStd, from the first
// symbol of the EAV to the last of the active part. Symbols has room for
// SIM_RX_MAX_LINE_SYMBOLS symbols, enough for the longest line. Returns the number of
// symbols, or 0, and writes nothing, for an unknown standard, a 4K standard of level-B
// links, or a line the frame does not have.
//
// For a 4K standard, the line is the raw line of the four links. Each link carries the
// line of a frame number of its own: FrameNumber for link 1, up to FrameNumber + 3 for
// link 4. A test can then see which link a symbol came from.
int SimChSdiRx_Line(int VidStd, uint32_t FrameNumber, int Line, uint16_t* Symbols);

// Makes the port at PortIndex receive VidStd from now on. DTAPI_VIDSTD_UNKNOWN takes the
// source away, as after a reset. The frame numbers continue.
void SimDtPcie_SetRxSource(int PortIndex, int VidStd);

// Makes the receivers follow the clock when RealTime is true. A format event is then due
// every quarter of the source's frame, a read of the write offset writes the quarters
// that are due, and a wait returns when the next one is due. When RealTime is false, as
// after a reset, each wait writes the next quarter at once, and nothing else writes. A
// source from CDTAPI_SIM_SDI_SOURCE sets it to true, for a program that looks at the FIFO
// load before it reads.
void SimDtPcie_SetRxRealTime(bool RealTime);

// Makes the port at PortIndex receive VidStd from now on, with the frames of the file at
// Path instead of those SimChSdiRx_Line() makes. After the last frame, the first comes
// again. Returns false, and changes nothing, for an unknown standard, a file that cannot
// be read, or a file that does not hold a whole number of frames.
//
// The file holds whole frames of 10-bit symbols, packed least significant bit first.
// Each line runs from its EAV to the end of its active part, and each frame is padded
// with zeros to a multiple of 8 bytes. This is what FFmpeg's sdi format holds without its
// header, and what SimDtPcie_SetSdiSink() writes. For a 4K standard, the lines are the
// raw lines of its four links.
bool SimChSdiRx_SetFileSource(int PortIndex, int VidStd, const char* Path);

// The faults a port's source can give the next frame it starts.
typedef enum SimRxFault
{
    SIM_RX_FAULT_SYNC_WORD,   // The header's sync word is wrong
    SIM_RX_FAULT_FORMAT,      // The header names the other format, 4K or not
    SIM_RX_FAULT_SKIP_FRAME,  // A frame number is skipped before the frame
    SIM_RX_FAULT_OUT_OF_SYNC, // The frame is not written and its events are out of sync
} SimRxFault;

// Gives the next frame of the port at PortIndex the fault Fault, once.
void SimDtPcie_InjectRxFault(int PortIndex, SimRxFault Fault);

// Makes the running channel of the port at PortIndex produce Events format events without
// anyone waiting for them, as a card goes on while an application does not read.
void SimDtPcie_RunRxEvents(int PortIndex, int Events);

// Limits the ring of every channel to Size bytes, so that a test can wrap the ring with a
// few frames. The limit is rounded down to a whole multiple of the prefetch size, but is
// never less than one. 0 removes the limit.
void SimDtPcie_LimitRxRing(size_t Size);

// Makes every channel report this stream alignment, in bits.
void SimDtPcie_SetRxAlignment(int AlignmentInBits);

// Makes the driver return a mapped ring as on Linux when AsLinux is true, and as on
// Windows otherwise.
void SimDtPcie_MapRxRingAsLinux(bool AsLinux);

// Makes CHSDIRX command Cmd, a DT_CHSDIRX_CMD_ value, fail with Status from now on.
// Status 0 ends it. One command at a time can be made to fail.
void SimDtPcie_FailRxCmd(int Cmd, uint32_t Status);

// Makes CHSDIRX command Cmd return Ms milliseconds late, after the emulator's lock is
// released, as on a busy system. Ms 0 ends it. One command at a time can be slowed down.
void SimDtPcie_SlowRxCmd(int Cmd, int Ms);

// The state of a port's channel.
typedef struct SimRxState
{
    bool Configured;      // The channel is configured
    size_t RingSize;      // The ring's size, in bytes
    int NumUsers;         // The number of handles attached
    uint32_t NextFrame;   // The number of the frame the source starts next
    uint32_t WriteOffset; // The ring's write offset
} SimRxState;

// Returns in *State the state of the channel of the port at PortIndex.
void SimDtPcie_GetRxState(int PortIndex, SimRxState* State);

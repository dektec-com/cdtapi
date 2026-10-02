// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimSdiTx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The emulated SDI transmit blocks, their sink and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transmit blocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Each SDI port of the emulated card has the building blocks of AF_DMA and AF_ASISDITX
// that a transmit channel drives. They behave as the driver's blocks do, and as a
// DTA-2178 was seen to:
//
//   commands    are refused in the driver's order: a command the block does not have, a
//               wrong size, no exclusive access for a command that needs it, and a block
//               that is not enabled. The transmitter's blocks are enabled while the port
//               is an SDI output; SDITXPHY is enabled on an ASI output too. A command to
//               a transmitter block that is not enabled first sets them all to idle. The
//               DMA's blocks are always enabled, so a registered buffer survives a change
//               of direction, as on the card.
//   buffer      CDMAC registers a buffer of the process for transmit or receive: on
//               Windows in the output, on Linux at the address in the input. The buffer
//               must start on a page, be a multiple of the prefetch size in pages, and be
//               at most SIM_TX_MAX_BUFFER.
//   pipeline    While neither CDMAC nor the burst FIFO is idle, the card takes what lies
//               between the read and write offsets into its pipeline, in whole 32-byte
//               words, up to the burst FIFO's size plus 16 KB, and advances the read
//               offset.
//   output      runs while the formatter, the switches (in their single-link position),
//               the encoder and the PHY run and the demultiplexer is idle. Each wait for
//               a format event then sends the next part of a frame, reading on from the
//               buffer: the lines of one event, as the format event setting asks, with
//               the header before the first. The event names the frame and the part. The
//               parts follow the clock: a frame period of the port's video standard holds
//               as many parts as a frame has. A wait that comes early returns after the
//               time of its part, or times out when that is later.
//   underflow   A wait that finds too little data sends nothing and times out. It sets
//               the PHY's underflow flag and counts in the burst FIFO. Once the formatter
//               has given its first event, it also sets the formatter's flag for the next
//               event.
//
// A sink decodes what is sent. It checks every header: the sync word, the format, the SDI
// rate and the section sizes. After a header that does not check, it skips one alignment
// word. It keeps the last frames it received as 16-bit symbols, so that a test can
// compare them with what it wrote. The encoder's clamping, and its regeneration of
// checksums and CRCs, are not modelled: the symbols are kept as they were written.
//
// As for the receive channels, the caller of a command holds the emulator's lock. The
// test controls take it themselves.
//

// The largest buffer the DMA controller takes.
#define SIM_TX_MAX_BUFFER (256 * 1024 * 1024)

// The properties the blocks report, as the DTA-2178's did with driver 3.6.4.
#define SIM_TX_PREFETCH_PAGES 16
#define SIM_TX_PCIE_DATA_WIDTH 256
#define SIM_TX_REORDER_BUF_SIZE 8192
#define SIM_TX_BURST_FIFO_SIZE 524288
#define SIM_TX_STREAM_ALIGNMENT 128

// Returns true when the emulated blocks take commands with this DT_FUNC_CODE_.
bool SimSdiTx_Handles(int FunctionCode);

// Carries out a command from Handle for the block of type Type, with role Role, of the
// port at PortIndex. Returns the DtStatus the driver would, and fills Out and *OutSize
// for a command that answers.
//
//   Access      what SimDtPcie_CheckExclAccess() returns for Handle and the block
//   Enabled     true when the block is enabled
//   VidStd      the video standard of the port's I/O standard, which paces the output
//   *SleepMs    how long the caller sleeps after it releases the emulator's lock
uint32_t SimSdiTx_Cmd(void* Handle, int PortIndex, int FunctionCode, int Type,
                      const char* Role, int Cmd, uint32_t Access, bool Enabled,
                      int VidStd, const void* In, size_t InSize, void* Out,
                      size_t* OutSize, int* SleepMs);

// Stops the DMA controller of every port whose buffer Handle registered, and releases the
// buffer, as closing a file does in the driver.
void SimSdiTx_CloseHandle(void* Handle);

// Frees everything, and restores the power-on state of every port and of every control
// below.
void SimSdiTx_Reset(void);

// Writes every frame the port at PortIndex sends from now on to the file at Path, which
// is created or emptied. A reset closes the file. Returns false, and changes nothing,
// when the file cannot be created.
//
// Each frame's lines are written from the EAV on, as 10-bit symbols packed least
// significant bit first, and the frame is padded with zeros to a multiple of 8 bytes.
// This is how FFmpeg's sdi format holds a frame, without its header.
bool SimSdiTx_SetFileSink(int PortIndex, const char* Path);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= For the ASI blocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What SimAsi.c does with the DMA of a port. The caller holds the emulator's lock.
//

// Returns true when the port's DMA receives: a buffer is registered for receiving, and
// CDMAC and the burst FIFO run.
bool SimSdiTx_RxRuns(int PortIndex);

// Returns the number of bytes the card can still write into the receive buffer, 0 when
// it does not receive.
size_t SimSdiTx_RxFreeBytes(int PortIndex);

// Writes Size bytes at the receive buffer's write offset, and advances the offset. Writes
// nothing when they do not fit.
void SimSdiTx_RxWrite(int PortIndex, const uint8_t* Data, size_t Size);

// Adds one to the burst FIFO's overflow/underflow count. It counts data the card could
// not write into the receive buffer, and, for the ASI sender, data it did not find in the
// transmit buffer in time.
void SimSdiTx_CountOverflow(int PortIndex);

// Takes up to Max bytes of what the card read from the transmit buffer, in the order it
// read them.
size_t SimSdiTx_TakeTxBytes(int PortIndex, uint8_t* Out, size_t Max);

// Returns true when the port's SDITXPHY runs. An ASI output needs it, as an SDI output
// does.
bool SimSdiTx_PhyRuns(int PortIndex);

// Returns true when the output follows the clock, as SimDtPcie_SetTxRealTime() sets it.
bool SimSdiTx_IsRealTime(void);

// Returns the number of parts in which a frame of VidStd goes out on the clock: its coded
// lines in parts of NumLinesPerEvent, or four parts when NumLinesPerEvent is 0. Returns 0
// for an unknown standard.
int SimSdiTx_NumPartsPerFrame(int VidStd, int NumLinesPerEvent);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Makes CDMAC take a buffer as the Linux driver does, at the address in the input, when
// AsLinux is true, and as the Windows driver does, in the output, otherwise. After a
// reset, it takes it as the driver of the platform the emulator is built for.
void SimDtPcie_RegisterTxBufferAsLinux(bool AsLinux);

// Makes every formatter report this stream alignment, in bits.
void SimDtPcie_SetTxAlignment(int AlignmentInBits);

// Makes the port at PortIndex send Events parts of frames without anyone waiting for
// their events, as a card goes on while an application does not wait. Stops at the first
// event that cannot come, such as at an underflow or the frame limit. Returns the number
// of events that came.
int SimDtPcie_RunTxEvents(int PortIndex, int Events);

// Makes the next Events waits of the port at PortIndex underflow, whatever the pipeline
// holds.
void SimDtPcie_StarveTx(int PortIndex, int Events);

// Makes the output follow the clock when RealTime is true, as after a reset. When false,
// the output goes out as fast as the waits come.
void SimDtPcie_SetTxRealTime(bool RealTime);

// Makes the port at PortIndex send Count frames and then stop. The frames it keeps are
// then still there when a test compares them, however long the test takes to get there.
// Count 0, as after a reset, removes the limit. The count includes every frame the port
// sends, the black frames the library writes too.
void SimDtPcie_SetTxFrameLimit(int PortIndex, int Count);

// Makes the next Reads reads of the read offset of the port at PortIndex return Offset.
// A DTA-2178 returned an offset of an earlier run like this, right after its DMA
// controller was started.
void SimDtPcie_StaleTxReadOffset(int PortIndex, uint32_t Offset, int Reads);

// Makes command Cmd with DT_FUNC_CODE_ FunctionCode fail with Status, on every port, from
// now on. Status 0 ends it. One command at a time can be made to fail.
void SimDtPcie_FailTxCmd(int FunctionCode, int Cmd, uint32_t Status);

// The state of a port's blocks.
typedef struct SimTxState
{
    // The operational modes of the blocks, DT_BLOCK_OPMODE_ values
    int CdmacMode, BurstMode, TxfMode, SwitchInMode, SwitchOutMode, DmxMode, TxpMode;
    int PhyMode;      // DT_FUNC_OPMODE_ value
    int SwitchIn[2];  // Input and output index of SDI_DEMUX_IN
    int SwitchOut[2]; // The same of SDI_DEMUX_OUT
    bool ClampEnabled, AdpChecksumEnabled,
        LineCrcEnabled;               // The encoder's generation mode
    bool BufferRegistered;            // CDMAC has a buffer
    size_t BufferSize;                // The buffer's size, in bytes
    uint32_t ReadOffset, WriteOffset; // The buffer's offsets
    size_t PipelineLoad;              // Bytes taken from the buffer and not yet sent
    int NumLinesPerEvent, NumSofsBetweenTod; // The formatter's event settings
    int TestMode;                            // The formatter's test mode
    int StartOfFrameOffsetNs;                // The formatter's start-of-frame offset
    bool PhyUfl;                             // The PHY's underflow flag
    uint32_t BurstOvfUflCount;               // The burst FIFO's overflow/underflow count
    int FramesSent;                          // Whole frames the sink received
    int HeaderErrors;                        // Headers that did not check
} SimTxState;

// Returns in *State the state of the blocks of the port at PortIndex.
void SimDtPcie_GetTxState(int PortIndex, SimTxState* State);

// A frame the sink received. Symbols holds every line's symbols, EAV first:
// NumLines * (SymsHanc + SymsVideo) of them. A 4K frame is kept as the raw frame its
// coded lines carry: its lines are raw lines, and SymsHanc and SymsVideo are those of a
// raw line of the four links.
typedef struct SimTxFrame
{
    int FrameId;             // The frame ID in its header
    int NumLines;            // The number of lines
    int SymsHanc;            // Symbols per line in the HANC section
    int SymsVideo;           // Symbols per line in the video section
    const uint16_t* Symbols; // Valid until the next command or control
} SimTxFrame;

// Returns the number of frames kept for the port at PortIndex, at most
// SIM_TX_KEPT_FRAMES.
int SimDtPcie_TxFrameCount(int PortIndex);

// Returns in *Frame the kept frame at Index, 0 for the oldest. Returns false when there
// is none.
bool SimDtPcie_GetTxFrame(int PortIndex, int Index, SimTxFrame* Frame);

// Copies the newest kept frame with frame ID FrameId: its header's fields into *Frame,
// and its symbols into Symbols, which has room for MaxSymbols. Frame->Symbols then points
// to Symbols. Unlike SimDtPcie_GetTxFrame(), the copy stays valid while commands go on.
// Returns false, and copies nothing, when no kept frame has that ID or its symbols do
// not fit.
bool SimDtPcie_CopyTxFrame(int PortIndex, int FrameId, uint16_t* Symbols,
                           size_t MaxSymbols, SimTxFrame* Frame);

#define SIM_TX_KEPT_FRAMES 8

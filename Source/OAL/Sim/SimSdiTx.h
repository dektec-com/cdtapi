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
// that a transmit channel drives, and they behave as the driver's blocks and as a
// DTA-2178 was seen to:
//
//   commands    refused as the driver's I/O stubs do, in its order: a command the block
//               does not have, the sizes, exclusive access for the commands that need
//               it, and a block that is not enabled; the blocks of the transmitter are
//               enabled while the port is an SDI output, those of the DMA always, so a
//               registered buffer outlasts a change of direction, as on the card
//   buffer      CDMAC registers a buffer of the process for transmit, as on Windows from
//               the output or as on Linux from the address in the input; it must start
//               on a page, be a multiple of the prefetch size in pages, and be at most
//               256 MB
//   pipeline    while CDMAC and the burst FIFO run, the card takes what lies between the
//               read and write offsets into its pipeline, in whole 32-byte words, up to
//               the burst FIFO and 16 KB more, and advances the read offset
//   output      while the formatter, the switches in their single-link position, the
//               encoder and the PHY run and the demultiplexer is idle, each wait for a
//               format event sends the next part of a frame, reading on from the buffer
//               as it goes: the lines of one event, as the format event setting asks,
//               and the header before the first; the event names the frame and the part.
//               The parts follow the clock, as many per frame period of the port's
//               video standard as a frame has, and a wait that comes early returns after
//               the time of its part, or times out when that is later
//   underflow   a wait that finds too little sends nothing and times out; it sets the
//               PHY's flag, counts in the burst FIFO, and, once the formatter has given
//               its first event, sets the formatter's flag for the next event
//
// A sink decodes what is sent: it checks every header — sync word, format, SDI rate and
// section sizes — skipping one alignment word after a header that does not check, and
// keeps the last frames it received as 16-bit symbols, so that a test can compare them
// with what it wrote. The encoder's clamping and its regeneration of checksums and CRCs
// are not modelled; the symbols are kept as they were written.
//
// The commands are called with the emulator's lock held, as the receive channels are; the
// test controls take the lock themselves.
//

// The properties the blocks report, as the DTA-2178's did with driver 3.6.4.
#define SIM_TX_PREFETCH_PAGES 16
#define SIM_TX_PCIE_DATA_WIDTH 256
#define SIM_TX_REORDER_BUF_SIZE 8192
#define SIM_TX_BURST_FIFO_SIZE 524288
#define SIM_TX_STREAM_ALIGNMENT 128

// Whether the emulated blocks take commands with this DT_FUNC_CODE_.
bool SimSdiTx_Takes(int FunctionCode);

// Handles a command from Handle for the block of type Type, with role Role, of the port
// at PortIndex. Access is what SimDtPcie_CheckAccess answers for Handle and the block,
// Enabled whether the port is an SDI output, and VidStd the video standard of its I/O
// standard, which paces the output. Returns the DtStatus the driver would, and fills Out
// and *OutSize for a command that answers. *SleepMs receives how long the caller sleeps
// after releasing the emulator's lock.
uint32_t SimSdiTx_Cmd(void* Handle, int PortIndex, int FunctionCode, int Type,
                      const char* Role, int Cmd, uint32_t Access, bool Enabled,
                      int VidStd, const void* In, size_t InSize, void* Out,
                      size_t* OutSize, int* SleepMs);

// Stops the DMA controller of every port whose buffer Handle registered, and lets go of
// the buffer, as closing a file does in the driver.
void SimSdiTx_CloseHandle(void* Handle);

// Frees everything and restores the power-on state of every port and control below.
void SimSdiTx_Reset(void);

// Writes every frame the port at PortIndex sends from now on to the file at Path, which
// is created or emptied: its lines from the EAV on as 10-bit symbols, packed least
// significant bit first, the frame padded with zeros to a multiple of 8 bytes, as
// FFmpeg's sdi format holds a frame without its header. A reset closes the file. Returns
// false, changing nothing, when the file cannot be created.
bool SimSdiTx_SetFileSink(int PortIndex, const char* Path);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= For the ASI blocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// What SimAsi.c does with the DMA of a port, with the emulator's lock held.
//

// Whether the port's DMA receives: a buffer registered for receiving, CDMAC and the burst
// FIFO running.
bool SimSdiTx_RxOpen(int PortIndex);

// The bytes the card may still write into the receive buffer, 0 when it does not
// receive.
size_t SimSdiTx_RxFree(int PortIndex);

// Writes Size bytes at the receive buffer's write offset and moves it on; nothing when
// they do not fit.
void SimSdiTx_RxWrite(int PortIndex, const uint8_t* Data, size_t Size);

// Counts an overflow of the burst FIFO, as data the card could not write.
void SimSdiTx_CountOverflow(int PortIndex);

// Takes up to Max bytes of what the card has read from the transmit buffer, in the order
// it read them.
size_t SimSdiTx_TxTake(int PortIndex, uint8_t* Out, size_t Max);

// Whether the port's SDITXPHY runs, which an ASI output needs as an SDI output does.
bool SimSdiTx_PhyRuns(int PortIndex);

// Whether output follows the clock, as SimDtPcie_SetTxRealTime sets it.
bool SimSdiTx_RealTime(void);

// The parts a frame of VidStd goes out in on the clock: its coded lines in parts of
// NumLinesPerEvent, or in four parts for 0. 0 for a standard that is not known.
int SimSdiTx_NumPartsPerFrame(int VidStd, int NumLinesPerEvent);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Makes CDMAC take a buffer as the Linux driver does, from the address in the input, when
// true, or as the Windows driver does, as the output. After a reset it takes it as the
// driver of the platform the emulator is built for.
void SimDtPcie_RegisterTxBufferAsLinux(bool AsLinux);

// Makes every formatter report this stream alignment in bits.
void SimDtPcie_SetTxAlignment(int AlignmentInBits);

// Lets the port at PortIndex send Events parts of frames without anyone waiting for their
// events, as a card goes on while an application does not wait. Stops at an underflow.
// Returns the number of events that came.
int SimDtPcie_RunTxEvents(int PortIndex, int Events);

// Makes the next Events waits of the port at PortIndex underflow, whatever the pipeline
// holds.
void SimDtPcie_StarveTx(int PortIndex, int Events);

// Makes the output follow the clock when true, as after a reset, or go out as fast as
// waits come when false.
void SimDtPcie_SetTxRealTime(bool RealTime);

// Makes the port at PortIndex send Count frames and no more, so that the frames it keeps
// are still there when a test compares them, however long the test takes to get to them.
// A count of 0, as after a reset, sends without a limit. The port counts every frame it
// sends, the black ones the library writes included.
void SimDtPcie_SetTxFrameLimit(int PortIndex, int Count);

// Makes the next Reads reads of the read offset of the port at PortIndex answer Offset,
// as a DTA-2178 answered an offset of an earlier run right after its DMA controller was
// set running.
void SimDtPcie_StaleTxReadOffset(int PortIndex, uint32_t Offset, int Reads);

// Refuses command Cmd with DT_FUNC_CODE_ FunctionCode, of every port, with Status from
// now on; Status 0 ends it. One command can be refused at a time.
void SimDtPcie_FailTxCmd(int FunctionCode, int Cmd, uint32_t Status);

// What the blocks of a port hold.
typedef struct SimTxState
{
    int CdmacMode, BurstMode, TxfMode, SwitchInMode, SwitchOutMode, DmxMode, TxpMode;
    int PhyMode;                      // DT_FUNC_OPMODE_ value
    int SwitchIn[2];                  // Input and output index of SDI_DEMUX_IN
    int SwitchOut[2];                 // The same of SDI_DEMUX_OUT
    bool Clamp, AdpChecksum, LineCrc; // The encoder's generation mode
    bool BufferRegistered;
    size_t BufferSize;
    uint32_t ReadOffset, WriteOffset;
    size_t PipelineLoad; // Bytes taken from the buffer and not yet sent
    int NumLinesPerEvent, NumSofsBetweenTod;
    int TestMode;
    int StartOfFrameOffsetNs;
    bool PhyUnderflow;
    uint32_t BurstOvfUflCount;
    int FramesSent;   // Whole frames the sink received
    int HeaderErrors; // Headers that did not check
} SimTxState;

void SimDtPcie_GetTxState(int PortIndex, SimTxState* State);

// A frame the sink received: its header's frame ID and geometry, and every line's
// symbols, EAV first, NumCodedLines times SymsHanc plus SymsVideo of them.
typedef struct SimTxFrame
{
    int FrameId;
    int NumCodedLines;
    int SymsHanc;
    int SymsVideo;
    const uint16_t* Symbols; // Valid until the next command or control
} SimTxFrame;

// The number of frames kept for the port at PortIndex, at most SIM_TX_KEPT_FRAMES.
int SimDtPcie_TxFrameCount(int PortIndex);

// The kept frame at Index, 0 for the oldest. Returns false when there is none.
bool SimDtPcie_GetTxFrame(int PortIndex, int Index, SimTxFrame* Frame);

// Copies the newest kept frame with frame ID FrameId: its header's fields into *Frame,
// and its symbols into Symbols, which holds MaxSymbols, with Frame->Symbols pointing
// there. Returns false, copying nothing, when no kept frame has the ID or its symbols do
// not fit. Unlike SimDtPcie_GetTxFrame, the copy stays valid while commands go on.
bool SimDtPcie_CopyTxFrame(int PortIndex, int FrameId, uint16_t* Symbols,
                           size_t MaxSymbols, SimTxFrame* Frame);

#define SIM_TX_KEPT_FRAMES 8

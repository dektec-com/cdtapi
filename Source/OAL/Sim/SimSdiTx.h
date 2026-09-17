// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimSdiTx.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The emulated SDI transmit blocks, their sink and test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_SIM_SDI_TX_H
#define CDTAPILITE_SIM_SDI_TX_H

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
//               and the header before the first; the event names the frame and the part
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
// Called with the emulator's lock held, as the receive channels are.
//

// The properties the blocks report, as the DTA-2178's did with driver 3.6.4.
#define SIM_TX_PREFETCH_PAGES 16
#define SIM_TX_PCIE_DATA_WIDTH 256
#define SIM_TX_REORDER_BUF_SIZE 8192
#define SIM_TX_BURST_FIFO_SIZE 524288
#define SIM_TX_STREAM_ALIGNMENT 128

// Whether the emulated blocks take commands with this DT_FUNC_CODE_.
bool SimSdiTxTakes(int FunctionCode);

// Handles a command from Handle for the block of type Type, with role Role, of the port
// at PortIndex. Access is what SimDtPcieCheckAccess answers for Handle and the block, and
// Enabled whether the port is an SDI output. Returns the DtStatus the driver would, and
// fills Out and *OutSize for a command that answers. *SleepMs receives how long the
// caller sleeps after releasing the emulator's lock.
uint32_t SimSdiTxCmd(void* Handle, int PortIndex, int FunctionCode, int Type,
                     const char* Role, int Cmd, uint32_t Access, bool Enabled,
                     const void* In, size_t InSize, void* Out, size_t* OutSize,
                     int* SleepMs);

// Stops the DMA controller of every port whose buffer Handle registered, and lets go of
// the buffer, as closing a file does in the driver.
void SimSdiTxCloseHandle(void* Handle);

// Frees everything and restores the power-on state of every port and control below.
void SimSdiTxReset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Makes CDMAC take a buffer as the Linux driver does, from the address in the input, when
// true, or as the Windows driver does, as the output. After a reset it takes it as the
// driver of the platform the emulator is built for.
void SimDtPcieRegisterTxBufferAsLinux(bool AsLinux);

// Makes every formatter report this stream alignment in bits.
void SimDtPcieSetTxAlignment(int AlignmentBits);

// Lets the port at PortIndex send Events parts of frames without anyone waiting for their
// events, as a card goes on while an application does not wait. Stops at an underflow.
// Returns the number of events that came.
int SimDtPcieRunTxEvents(int PortIndex, int Events);

// Makes the next Events waits of the port at PortIndex underflow, whatever the pipeline
// holds.
void SimDtPcieStarveTx(int PortIndex, int Events);

// Refuses command Cmd with DT_FUNC_CODE_ FunctionCode, of every port, with Status from
// now on; Status 0 ends it. One command can be refused at a time.
void SimDtPcieFailTxCmd(int FunctionCode, int Cmd, uint32_t Status);

// What the blocks of a port hold.
typedef struct SimTxState
{
    int CdmacMode, BurstMode, TxfMode, SwitchInMode, SwitchOutMode, DmxMode, TxpMode;
    int PhyMode;                      // DT_FUNC_OPMODE_ value
    int SwitchIn[2];                  // Input and output index of SDI_DEMUX_IN
    int SwitchOut[2];                 // The same of SDI_DEMUX_OUT
    bool Clamp, AncChecksum, LineCrc; // The encoder's generation mode
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

void SimDtPcieGetTxState(int PortIndex, SimTxState* State);

// A frame the sink received: its header's frame ID and geometry, and every line's
// symbols, EAV first, NumLines times SymsHanc plus SymsVideo of them.
typedef struct SimTxFrame
{
    int FrameId;
    int NumLines;
    int SymsHanc;
    int SymsVideo;
    const uint16_t* Symbols; // Valid until the next command or control
} SimTxFrame;

// The number of frames kept for the port at PortIndex, at most SIM_TX_KEPT_FRAMES.
int SimDtPcieTxFrameCount(int PortIndex);

// The kept frame at Index, 0 for the oldest. Returns false when there is none.
bool SimDtPcieGetTxFrame(int PortIndex, int Index, SimTxFrame* Frame);

#define SIM_TX_KEPT_FRAMES 4

#endif // CDTAPILITE_SIM_SDI_TX_H

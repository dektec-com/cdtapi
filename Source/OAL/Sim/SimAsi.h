// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SimAsi.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated ASI blocks and their test controls
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ASI blocks +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Each SDI port of the emulated card has the driver function ASIRX of its AF_ASISDIRX and
// the gate ASITXG of its AF_ASISDITX, which behave as the driver's do:
//
//   commands    refused in the driver's order: a command the object does not have, the
//               sizes, exclusive access for the commands that change the object, and an
//               object that is not enabled; ASIRX is enabled while the port is an ASI
//               input, ASITXG while it is an ASI output
//   settings    ASIRX's operational mode, IDLE or RUN, its packet mode, polarity control
//               and synchronisation mode, and ASITXG's operational mode and polarity, are
//               kept and read back; a value the driver does not define is an invalid
//               parameter; ASIRX's operational status is RUN only while it runs and
//               its input has a carrier, as a DTA-2178's was
//   status      what ASIRX reports of its input comes from a signal a test sets; by
//               default no port carries one, which reads as no carrier and no lock
//
// The commands are called with the emulator's lock held; the test controls take it
// themselves.
//

// Whether the emulated ASI blocks take commands with this DT_FUNC_CODE_.
bool SimAsi_Takes(int FunctionCode);

// Handles a command from Handle for the object of type Type of the port at PortIndex.
// Access is what SimDtPcie_CheckAccess answers for Handle and the object, and Enabled
// whether the object is enabled. Returns the DtStatus the driver would, and fills Out and
// *OutSize for a command that answers.
uint32_t SimAsi_Cmd(void* Handle, int PortIndex, int FunctionCode, int Type, int Cmd,
                    uint32_t Access, bool Enabled, const void* In, size_t InSize,
                    void* Out, size_t* OutSize);

// Restores the power-on state of every port and control below, and frees what the
// ports kept.
void SimAsi_Reset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Data path +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// What passes through the DMA of an ASI port happens when a channel reads its offsets,
// with the emulator's lock held:
//
//   receiving   while ASIRX runs, its input has a carrier and the DMA receives, the
//               port's source, SimDtPcie_SetAsiSource, or the output looped to it,
//               SimDtPcie_SetAsiLoopback, writes transparent packets (DtTsTrp.h) into
//               the receive buffer: the time of day from SimNw_Now, the payload, the
//               sync nibble with the packet-sync bit, the valid count and a sequence
//               number that counts every packet the card receives, so that one it could
//               not write for want of room, which counts as a burst-FIFO overflow, is a
//               gap
//   sending     while ASITXG and SDITXPHY run, the card takes what CDMAC read from the
//               transmit buffer as 8b/10b symbols, one in each 16-bit word, at 54 MB a
//               second in real time and all of it otherwise; a buffer that runs dry
//               after the first symbol counts as a burst-FIFO underflow in real time.
//               The sink decodes the symbols, following the running disparity, counts
//               K28.5 and errors, and keeps the data bytes for SimDtPcie_TakeAsiTxBytes
//

// Writes into the receive buffer of the port at PortIndex what it received since the
// last call; SimSdiTx.c calls it before answering the receive write offset.
void SimAsi_Produce(int PortIndex);

// Sends what the card read from the transmit buffer of the port at PortIndex; SimSdiTx.c
// calls it before answering the transmit read offset.
void SimAsi_Drain(int PortIndex);

// The numbered packet a source sends: 0x47, PID 0x100 with the continuity counter, the
// number big endian, then bytes counting up from it; Size is 188 or 204.
void SimAsi_MakePacket(uint32_t Number, int Size, uint8_t* Out);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What ASIRX of a port reports of its input: DT_ASIRX_PCKSIZE_ and DT_ASIRX_POLARITY_
// values, the rate on the wire in bits a second, and the count of code violations.
typedef struct SimAsiSignal
{
    bool CarrierDetect;
    bool AsiLock;
    int PacketSize;
    int Polarity;
    int TsBitrate;
    int ViolCount;
} SimAsiSignal;

// Sets what ASIRX of the port at PortIndex reports; NULL restores the power-on state, no
// signal.
void SimDtPcie_SetAsiSignal(int PortIndex, const SimAsiSignal* Signal);

// What the ASI blocks of a port hold, for a test to check what a channel set.
typedef struct SimAsiState
{
    int RxMode;         // DT_FUNC_OPMODE_
    int RxPacketMode;   // DT_ASIRX_PCKMODE_
    int RxPolarityCtrl; // DT_ASIRX_POLARITY_
    int RxSyncMode;     // DT_ASIRX_SYNCMODE_
    int TxgMode;        // DT_BLOCK_OPMODE_
    int TxgPolarity;    // DT_ASITXG_POL_
    int TxgInputClears; // Times the gate's input was cleared
} SimAsiState;

void SimDtPcie_GetAsiState(int PortIndex, SimAsiState* State);

// Refuses command Cmd with DT_FUNC_CODE_ FunctionCode, of every port, with Status from
// now on; Status 0 ends it. One command can be refused at a time.
void SimDtPcie_FailAsiCmd(int FunctionCode, int Cmd, uint32_t Status);

// The stream a port receives: numbered packets, SimAsi_MakePacket, from 0.
typedef struct SimAsiSource
{
    int PacketSize;      // 188 or 204
    int64_t Rate;        // Bits a second of the packets on the wire
    int PacketsPerRead;  // Without real time, the packets each read of the offset brings
    int UnsyncedAtStart; // Pieces without packet sync when the receiver starts, as on a
                         // DTA-2178 (0011, step A)
} SimAsiSource;

// A source that receives 188-byte packets at 10 Mbit/s, 8 per read, 3 pieces unsynced.
void SimAsi_DefaultSource(SimAsiSource* Source);

// Makes the port at PortIndex receive Source, with a carrier, lock, the packet size and
// the rate as ASIRX's status; NULL ends it and restores the power-on signal. The packet
// numbers start again at 0.
void SimDtPcie_SetAsiSource(int PortIndex, const SimAsiSource* Source);

// What the next packet a source sends, not a piece without sync, has wrong.
#define SIM_ASI_FAULT_NIBBLE 1   // No sync nibble in its trailer
#define SIM_ASI_FAULT_VALID 2    // A valid count of 100
#define SIM_ASI_FAULT_SEQUENCE 3 // A sequence number skipped before it
#define SIM_ASI_FAULT_NOSYNC 4   // No packet-sync bit

void SimDtPcie_AsiRxFault(int PortIndex, int Fault);

// Makes the output at TxIndex send into the input at RxIndex, as a cable does: the
// input finds 188- or 204-byte packets in the data bytes by their sync bytes, and has a
// carrier while the output sends. A negative TxIndex ends it.
void SimDtPcie_SetAsiLoopback(int TxIndex, int RxIndex);

// What the sink of a port decoded since the port was reset.
typedef struct SimAsiTxStats
{
    int64_t Symbols;
    int64_t DataBytes;
    int64_t K28;             // K28.5 comma symbols
    int64_t CodeErrors;      // Symbols that are no code
    int64_t DisparityErrors; // Codes of the wrong running disparity
    int64_t TsBitrate;       // Data bits a second on the wire, the last measured
} SimAsiTxStats;

void SimDtPcie_GetAsiTxStats(int PortIndex, SimAsiTxStats* Stats);

// Takes up to Max of the data bytes the sink of a port decoded, oldest first. The sink
// keeps SIM_ASI_KEPT_BYTES, dropping the oldest.
#define SIM_ASI_KEPT_BYTES (4 * 1024 * 1024)
size_t SimDtPcie_TakeAsiTxBytes(int PortIndex, uint8_t* Out, size_t Max);

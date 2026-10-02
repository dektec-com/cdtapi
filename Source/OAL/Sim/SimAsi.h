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
// Each SDI port of the emulated card has the ASI receiver ASIRX, a driver function of its
// AF_ASISDIRX, and the ASI transmit gate ASITXG of its AF_ASISDITX. They behave as the
// driver's do:
//
//   commands    are refused in the driver's order: a command the object does not have,
//               a wrong size, no exclusive access for a command that changes the object,
//               and an object that is not enabled. ASIRX is enabled while the port is an
//               ASI input, ASITXG while it is an ASI output.
//   settings    are kept and read back: ASIRX's operational mode (IDLE or RUN), packet
//               mode, polarity control and synchronisation mode, and ASITXG's
//               operational mode and polarity. A value the driver does not define is an
//               invalid parameter. As on a DTA-2178, ASIRX's operational status is RUN
//               only while it runs and its input has a carrier.
//   status      ASIRX reports the signal a test sets at its input. By default no port
//               has one: no carrier and no lock.
//
// The caller of a command holds the emulator's lock. The test controls take it
// themselves.
//

// Returns true when the emulated ASI blocks take commands with this DT_FUNC_CODE_.
bool SimAsi_Handles(int FunctionCode);

// Carries out a command from Handle for the object of type Type of the port at
// PortIndex. Returns the DtStatus the driver would, and fills Out and *OutSize for a
// command that answers.
//
// Access is what SimDtPcie_CheckExclAccess() returns for Handle and the object. Enabled
// is true when the object is enabled.
uint32_t SimAsi_Cmd(void* Handle, int PortIndex, int FunctionCode, int Type, int Cmd,
                    uint32_t Access, bool Enabled, const void* In, size_t InSize,
                    void* Out, size_t* OutSize);

// Restores the power-on state of every port and of every control below, and frees what
// the ports kept.
void SimAsi_Reset(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Data path +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Data passes through the DMA of an ASI port when a channel reads the buffer's offsets.
// The emulator's lock is then held.
//
//   receiving   happens while ASIRX runs, its input has a carrier and the DMA receives.
//               The port's source (SimDtPcie_SetAsiSource()), or the output looped
//               back to it (SimDtPcie_SetAsiLoopback()), writes transparent packets
//               (DtTsTrp.h) into the receive buffer. Each holds the time of day from
//               SimDtPcie_Now(), the payload, the sync nibble with the packet-sync bit,
//               the valid count and a sequence number. The sequence number counts every
//               packet the card receives. A packet the card could not write for lack of
//               room counts as a burst-FIFO overflow, and shows as a gap in the sequence
//               numbers.
//   sending     happens while ASITXG and SDITXPHY run. The card takes what CDMAC read
//               from the transmit buffer as 8b/10b symbols, one in each 16-bit word. In
//               real time it takes 54 MB a second; otherwise it takes all of it. In real
//               time, a buffer that runs dry after the first symbol counts as a
//               burst-FIFO underflow. The sink decodes the symbols, following the running
//               disparity, counts K28.5 symbols and errors, and keeps the data bytes for
//               SimDtPcie_TakeAsiTxBytes().
//

// Writes into the receive buffer of the port at PortIndex what the port received since
// the last call. SimSdiTx.c calls it before it returns the receive buffer's write offset.
void SimAsi_ReceiveIntoBuffer(int PortIndex);

// Sends what the card read from the transmit buffer of the port at PortIndex. SimSdiTx.c
// calls it before it returns the transmit buffer's read offset.
void SimAsi_SendFromBuffer(int PortIndex);

// Makes the packet with number Number that a source sends, of Size bytes, 188 or 204: the
// sync byte 0x47, PID 0x100 with the continuity counter, the number big endian, and then
// bytes counting up from it.
void SimAsi_MakePacket(uint32_t Number, int Size, uint8_t* Out);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test controls +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What ASIRX of a port reports about its input.
typedef struct SimAsiSignal
{
    bool CarrierDetect; // A carrier is detected
    bool AsiLock;       // The receiver is locked to the ASI signal
    int PacketSize;     // A DT_ASIRX_PCKSIZE_ value
    int Polarity;       // A DT_ASIRX_POLARITY_ value
    int TsBitrate;      // The rate on the wire, in bits a second
    int ViolCount;      // The count of code violations
} SimAsiSignal;

// Sets what ASIRX of the port at PortIndex reports. NULL restores the power-on state: no
// signal.
void SimDtPcie_SetAsiSignal(int PortIndex, const SimAsiSignal* Signal);

// The settings of the ASI blocks of a port, for a test that checks what a channel set.
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

// Returns in *State the settings of the ASI blocks of the port at PortIndex.
void SimDtPcie_GetAsiState(int PortIndex, SimAsiState* State);

// Makes command Cmd with DT_FUNC_CODE_ FunctionCode fail with Status, on every port, from
// now on. Status 0 ends it. One command at a time can be made to fail.
void SimDtPcie_FailAsiCmd(int FunctionCode, int Cmd, uint32_t Status);

// The stream a port receives: numbered packets (SimAsi_MakePacket()), from 0 up.
typedef struct SimAsiSource
{
    int PacketSize;      // 188 or 204
    int64_t Rate;        // Bits a second of the packets on the wire
    int PacketsPerRead;  // Without real time, the packets each read of the offset brings
    int UnsyncedAtStart; // Pieces without packet sync when the receiver starts, as on a
                         // DTA-2178 (plan 0011, step A)
} SimAsiSource;

// Fills *Source with the default source: 188-byte packets at 10 Mbit/s, 8 per read, and 3
// pieces without sync at the start.
void SimAsi_DefaultSource(SimAsiSource* Source);

// Makes the port at PortIndex receive Source. ASIRX then reports a carrier, lock, the
// source's packet size and its rate. The packet numbers start again at 0. NULL, or a
// source with another packet size or without a rate, takes the source away and restores
// the power-on signal.
void SimDtPcie_SetAsiSource(int PortIndex, const SimAsiSource* Source);

// The faults SimDtPcie_InjectAsiRxFault() can give the next packet a source sends. A
// piece without sync does not count as that packet.
#define SIM_ASI_FAULT_NIBBLE 1   // No sync nibble in its trailer
#define SIM_ASI_FAULT_VALID 2    // A valid count of 100
#define SIM_ASI_FAULT_SEQUENCE 3 // A sequence number skipped before it
#define SIM_ASI_FAULT_NOSYNC 4   // No packet-sync bit

// Gives the next packet the source of the port at PortIndex sends a fault, a
// SIM_ASI_FAULT_ value.
void SimDtPcie_InjectAsiRxFault(int PortIndex, int Fault);

// Connects the output at TxIndex to the input at RxIndex, as a cable does. The input
// finds 188- or 204-byte packets in the data bytes by their sync bytes, and has a carrier
// while the output sends. A negative TxIndex removes the connection.
void SimDtPcie_SetAsiLoopback(int TxIndex, int RxIndex);

// What the sink of a port decoded since the emulator was reset.
typedef struct SimAsiTxStats
{
    int64_t Symbols;         // Symbols received
    int64_t DataBytes;       // Data bytes decoded
    int64_t K28;             // K28.5 comma symbols
    int64_t CodeErrors;      // Symbols that are not a code
    int64_t DisparityErrors; // Codes of the wrong running disparity
    int64_t TsBitrate;       // Data bits a second on the wire, the last measured
} SimAsiTxStats;

// Returns in *Stats what the sink of the port at PortIndex decoded.
void SimDtPcie_GetAsiTxStats(int PortIndex, SimAsiTxStats* Stats);

// Takes up to Max of the data bytes the sink of a port decoded, oldest first, and returns
// how many it took. The sink keeps the last SIM_ASI_KEPT_BYTES bytes, and drops older
// ones.
#define SIM_ASI_KEPT_BYTES (4 * 1024 * 1024)
size_t SimDtPcie_TakeAsiTxBytes(int PortIndex, uint8_t* Out, size_t Max);

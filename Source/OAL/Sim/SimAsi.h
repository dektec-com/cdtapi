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
//   commands    refused in the driver's order: a command the part does not have, the
//               sizes, exclusive access for the commands that change the part, and a
//               part that is not enabled; ASIRX is enabled while the port is an ASI
//               input, ASITXG while it is an ASI output (DtPtAsiSdiRxTx)
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

// Handles a command from Handle for the part of type Type of the port at PortIndex.
// Access is what SimDtPcie_CheckAccess answers for Handle and the part, and Enabled
// whether the part is enabled. Returns the DtStatus the driver would, and fills Out and
// *OutSize for a command that answers.
uint32_t SimAsi_Cmd(void* Handle, int PortIndex, int FunctionCode, int Type, int Cmd,
                    uint32_t Access, bool Enabled, const void* In, size_t InSize,
                    void* Out, size_t* OutSize);

// Restores the power-on state of every port and control below.
void SimAsi_Reset(void);

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

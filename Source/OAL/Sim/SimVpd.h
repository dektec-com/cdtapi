// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SimVpd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated card's Vital Product Data
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= EEPROM +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The card's EEPROM of SIM_VPD_EEPROM_SIZE bytes. It has a read-only section and a
// read-write section. The bytes behind them, the tail, belong to no section; they hold
// the data the card is activated with (SimActivate.h).
//

#define SIM_VPD_EEPROM_SIZE 1024
#define SIM_VPD_RO_OFFSET 0
#define SIM_VPD_RO_SIZE 256
#define SIM_VPD_RW_OFFSET 256
#define SIM_VPD_RW_SIZE 256
#define SIM_VPD_MAX_ITEM_LENGTH 255

// The offset and size of the tail.
#define SIM_VPD_TAIL_OFFSET (SIM_VPD_RO_SIZE + SIM_VPD_RW_SIZE)
#define SIM_VPD_TAIL_BYTES 256

// Fills the EEPROM as on a card that leaves the factory.
void SimVpd_Reset(void);

// With Blank true, makes every byte of the tail the same, as on a card whose tail was
// never written. With Blank false, fills the tail again.
void SimVpd_SetTailBlank(bool Blank);

// Returns true for the function code the EEPROM answers.
bool SimVpd_Handles(int FunctionCode);

// Carries out one command, and returns its DT_STATUS_ result.
uint32_t SimVpd_Cmd(int Cmd, const void* In, size_t InSize, void* Out, size_t* OutSize);

// Returns the tail, in the order a reader of the EEPROM sees its bytes.
const uint8_t* SimVpd_Tail(void);

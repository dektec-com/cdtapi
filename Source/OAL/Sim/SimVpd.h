// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimVpd.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= EEPROM +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// An EEPROM of SIM_VPD_EEPROM_SIZE bytes: a read-only section, a read-write section, and
// behind them the bytes that belong to no section, where the data a card is activated
// with lies.
//

#define SIM_VPD_EEPROM_SIZE 1024
#define SIM_VPD_RO_OFFSET 0
#define SIM_VPD_RO_SIZE 256
#define SIM_VPD_RW_OFFSET 256
#define SIM_VPD_RW_SIZE 256
#define SIM_VPD_MAX_ITEM_LENGTH 255

// Where the data behind the sections lies, and how much of it there is.
#define SIM_VPD_TAIL_OFFSET (SIM_VPD_RO_SIZE + SIM_VPD_RW_SIZE)
#define SIM_VPD_TAIL_BYTES 256

// Fills the EEPROM as a card leaves the factory.
void SimVpd_Reset(void);

// Makes the bytes behind the sections all the same, as they are on a card that was never
// given any, or fills them again.
void SimVpd_SetTailBlank(bool Blank);

// True for the function code the EEPROM answers.
bool SimVpd_Takes(int FunctionCode);

// Handles one command, and gives its DT_STATUS_ outcome.
uint32_t SimVpd_Cmd(int Cmd, const void* In, size_t InSize, void* Out, size_t* OutSize);

// The bytes behind the sections, in the order a reader of the EEPROM sees them.
const uint8_t* SimVpd_Tail(void);

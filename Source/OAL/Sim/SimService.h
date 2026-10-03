// #*#*#*#*#*#*#*#*#*#*#*#*#*#* SimService.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated DtapiService, with the PTP clock slave of the emulated DTA-2110
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Service +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Answers the commands of DtapiService's services pipe in the program, as DtapiService
// 5.2.8 does, for the emulated cards: DtService.c sends a command here instead of to the
// pipe when CDTAPI_SIM asks for the emulator. A message is a command or an answer as on
// the pipe, without its length.
//
// The service has the PTP clock slave of port 1 of the emulated DTA-2110, with the 19
// parameters of the real one under IDs of its own, so that a client that does not look
// them up by name fails. The slave keeps its settings for the process; every connection
// sees the same slave. It starts off: Enable false, Disabled, domain 127, no master. With
// Enable set it is at once Slave and Locked, following the grandmaster
// SIM_SERVICE_GRANDMASTER in its own domain: time traceable, clock class 6, one step
// removed. It refuses as the real service does: an attach while another holds the slave
// exclusively, a setting without exclusive access, a parameter that is unknown, of
// another type, read-only or out of range. Saving is only noted.
//

// The emulated grandmaster's clock identity, an EUI-64 made from a MAC address.
#define SIM_SERVICE_GRANDMASTER UINT64_C(0x001B19FFFE000001)

typedef struct SimService SimService;

// Ends a connection, which detaches what it attached. A NULL Connection does nothing.
void SimService_Close(SimService* Connection);

// Opens a connection to the emulated service. Returns NULL without memory.
SimService* SimService_Connect(void);

// Puts the emulated slave back as it starts, for tests. No connection may be attached.
void SimService_Reset(void);

// Answers the command Msg of Size bytes in a new *Answer of *AnswerSize bytes, to be
// freed with DtAlloc_Free; CLEANUP_CONNECTION has no answer, and leaves *Answer NULL.
// Returns false when memory runs out.
bool SimService_Transfer(SimService* Connection, const uint8_t* Msg, size_t Size,
                         uint8_t** Answer, size_t* AnswerSize);

// Returns whether the slave's settings were saved since it started or was reset.
bool SimService_WasSaved(void);

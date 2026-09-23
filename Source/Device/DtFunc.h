// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtFunc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Device layer: the driver functions and building blocks of an API function
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

// CDTAPI includes
#include "Core/DtVec.h" // The parts found.
#include "DtPcieCmd.h"  // Properties, the driver version and DtPartRef.
#include "cdtapi.h"     // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= API functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The hardware of a DtPcie card is reached through API functions, such as the ASI/SDI
// receiver of a port. The driver describes each as properties of the port: instances
// AF_<name>#1, #2 and so on, each with a role; and per instance its parts, #<n>.1, .2 and
// so on, each a driver function (DF_) or building block (BC_) with a role, a type and a
// UUID that commands are addressed to.
//
// Here a part is plain data, and what a driver command is addressed to is its DtPartRef.
// What is then done with a part is the business of the device layer that needs it.
//

typedef struct DtFuncPart
{
    char Name[DT_PROPERTY_STR_SIZE]; // Such as DF_SDIRX#1
    char Role[DT_PROPERTY_STR_SIZE]; // Empty for the part's plain role
    bool IsDf;                       // A driver function, otherwise a building block
    int Type;                        // A DT_FUNC_TYPE_ or DT_BLOCK_TYPE_ value
    DtPartRef Ref;                   // What commands to the part go to
} DtFuncPart;

typedef struct DtFuncInstance
{
    int PortIndex;
    DtVec Parts; // DtFuncPart, in the order the driver lists them
} DtFuncInstance;

// Finds the instance of API function Name, such as "AF_ASISDIRX", with role Role on the
// port at PortIndex, and reads its parts.
//
// Instances are read until one has the role; failing to read one is returned, so a
// function without an instance of the role ends in DTAPI_E_NOT_FOUND. Parts are read
// until one is not found; any other failure to read a part's name is returned. A part
// whose name starts neither with DF_ nor BC_, or whose role, type or UUID cannot be read,
// is left out. Returns DTAPI_E_OUT_OF_MEM when the parts do not fit in memory.
//
// Instance is empty after a failure. Release it with DtFunc_Release after a success.
DtapiResult DtFunc_Find(OsDrv* Drv, int PortIndex, const char* Name, const char* Role,
                        DtFuncInstance* Instance);

// Frees the parts of an instance, which is then empty.
void DtFunc_Release(DtFuncInstance* Instance);

// The part of the instance that is a driver function when IsDf, or a building block
// otherwise, of Type and with Role; NULL when there is none. The parts are walked from
// the back, so a later part of the same type and role wins.
const DtFuncPart* DtFunc_Get(const DtFuncInstance* Instance, bool IsDf, int Type,
                             const char* Role);

// Issues exclusive access command Cmd, a DT_EXCLUSIVE_ACCESS_CMD_ value, for every part
// of the instance. A part that does not support it is passed over. The first other
// failure stops the command and is returned; when acquiring, the
// parts acquired before it are released again. Releasing goes on past failures, and
// returns the first.
DtapiResult DtFunc_ExclAccess(OsDrv* Drv, const DtFuncInstance* Instance, int Cmd);

// Checks that the driver is new enough for a part before it is used: DTAPI_OK,
// DTAPI_E_DRIVER_INCOMP when it is older, and DTAPI_E_INTERNAL for a type the table does
// not have. The table holds the types CDTAPI uses.
DtapiResult DtFunc_CheckDriverVersion(const DtDriverVersion* Version, bool IsDf,
                                      int Type);

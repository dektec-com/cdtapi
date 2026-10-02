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
#include "Core/DtVec.h" // The objects found.
#include "DtPcieCmd.h"  // Properties, the driver version and DtDrvObject.
#include "cdtapi.h"     // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= API functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The library reaches the hardware of a DtPcie card through API functions, such as the
// ASI/SDI receiver of a port. The driver describes each as properties of the port:
// - an API function has instances, AF_<name>#1, #2 and so on, each with a role;
// - an instance has objects, #<n>.1, .2 and so on. Each object is a driver function (DF_)
//   or a building block (BC_), and has a role, a type and a UUID to address commands to.
//
// These functions find an instance and its objects. What is done with an object is up to
// the part of the device layer that needs it.
//

// One object of an instance: a driver function or a building block.
typedef struct DtFuncObject
{
    char Name[DT_PROPERTY_STR_SIZE]; // e.g. "DF_SDIRX#1"
    char Role[DT_PROPERTY_STR_SIZE]; // "" for the object's plain role
    bool IsDriverFunction;           // True: a driver function; false: a building block
    int FuncOrBlockType;             // A DT_FUNC_TYPE_ or DT_BLOCK_TYPE_ value, as
                                     // IsDriverFunction says
    DtDrvObject Object;              // Where commands to the object go
} DtFuncObject;

// One instance of an API function, with its objects.
typedef struct DtFuncInstance
{
    DtVec Objects; // DtFuncObject, in the order the driver lists them
} DtFuncInstance;

// Finds the instance of API function Name (e.g. "AF_ASISDIRX") with role Role on port
// PortIndex (from 0), and reads its objects into *Instance. Release them with
// DtFunc_Release() afterwards. After a failure *Instance is empty.
//
// The instances are read until one has the role; the objects until one is not found. An
// object whose name starts with neither DF_ nor BC_, or whose role, type or UUID cannot
// be read, is left out.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_NOT_FOUND      no instance has the role (as reading the next one fails)
//   DTAPI_E_OUT_OF_MEM     not enough memory for the objects
//   DTAPI_E_BUF_TOO_SMALL  a property name does not fit
// and the errors of reading an instance or an object's name.
DtapiResult DtFunc_Find(OsDrv* Drv, int PortIndex, const char* Name, const char* Role,
                        DtFuncInstance* Instance);

// Frees the objects of Instance, which is then empty.
void DtFunc_Release(DtFuncInstance* Instance);

// Returns the object of Instance with type FuncOrBlockType and role Role: a driver
// function when IsDriverFunction, a building block otherwise. Returns NULL when there is
// none. When several match, the last one in the driver's order wins.
const DtFuncObject* DtFunc_FindObject(const DtFuncInstance* Instance,
                                      bool IsDriverFunction, int FuncOrBlockType,
                                      const char* Role);

// Acquires or releases exclusive access to every object of Instance: Cmd is a
// DT_EXCLUSIVE_ACCESS_CMD_ value. Objects that do not support it are skipped.
//
// Acquiring stops at the first other failure, releases the objects acquired before it,
// and returns that failure. Releasing goes on past failures, and returns the first.
DtapiResult DtFunc_ExclAccess(OsDrv* Drv, const DtFuncInstance* Instance, int Cmd);

// Checks that the driver is new enough for an object of Type before it is used. The
// table of minimum versions holds the types CDTAPI uses.
//
// Returns DTAPI_OK, or:
//   DTAPI_E_DRIVER_INCOMP  the driver is older
//   DTAPI_E_INTERNAL       the table does not have the type
DtapiResult DtFunc_CheckDriverVersion(const DtDriverVersion* Version,
                                      bool IsDriverFunction, int Type);

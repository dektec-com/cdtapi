// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtFunc.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Device layer: the driver functions and building blocks of an API function
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_FUNC_H
#define CDTAPILITE_DT_FUNC_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

// CDtapiLite includes
#include "Core/DtVec.h" // The parts found.
#include "DtDrv.h"      // Properties and the driver version.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= API functions +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// DTAPI reaches the hardware of a DtPcie card through API functions, such as the
// ASI/SDI receiver of a port. The driver describes each as properties of the port:
// instances AF_<name>#1, #2 and so on, each with a role; and per instance its parts,
// #<n>.1, .2 and so on, each a driver function (DF_) or building block (BC_) with a role,
// a type and a UUID that commands are addressed to. DTAPI's DtAf and DtProxyFactory read
// these and make a proxy of each part; DtAf::GetPal then asks for the proxy of a type and
// role.
//
// Here a part is plain data, and the proxy is the UUID a driver command takes. What
// DTAPI's PALs do with a proxy is done by the device layer that needs it.
//

typedef struct DtFuncPart
{
    char Name[DT_PROPERTY_STR_SIZE]; // Such as DF_SDIRX#1
    char Role[DT_PROPERTY_STR_SIZE]; // Empty for the part's plain role
    bool IsDf;                       // A driver function, otherwise a building block
    int Type;                        // A DT_FUNC_TYPE_ or DT_BLOCK_TYPE_ value
    int Uuid;
} DtFuncPart;

typedef struct DtFuncInstance
{
    int PortIndex;
    DtVec Parts; // DtFuncPart, in the order the driver lists them
} DtFuncInstance;

// Finds the instance of API function Name, such as "AF_ASISDIRX", with role Role on the
// port at PortIndex, and reads its parts, as DtAfUtility::CreateProxies and
// DtProxyFactory::CreateProxies do.
//
// Instances are read until one has the role; failing to read one is returned, so a
// function without an instance of the role ends in DTAPI_E_NOT_FOUND. Parts are read
// until one is not found; any other failure to read a part's name is returned. A part
// whose name starts neither with DF_ nor BC_, or whose role, type or UUID cannot be read,
// is left out, as DTAPI makes no proxy of it. Returns DTAPI_E_OUT_OF_MEM when the parts
// do not fit in memory.
//
// Instance is empty after a failure. Release it with DtFuncRelease after a success.
unsigned int DtFuncFind(OsDrv* Drv, int PortIndex, const char* Name, const char* Role,
                        DtFuncInstance* Instance);

// Frees the parts of an instance, which is then empty.
void DtFuncRelease(DtFuncInstance* Instance);

// The part of the instance that is a driver function when IsDf, or a building block
// otherwise, of Type and with Role; NULL when there is none. When parts are alike the
// last is returned, because DTAPI's proxy collection keeps the last one it adds for a
// type and role.
const DtFuncPart* DtFuncGet(const DtFuncInstance* Instance, bool IsDf, int Type,
                            const char* Role);

// Issues exclusive access command Cmd, a DT_EXCLUSIVE_ACCESS_CMD_ value, for every part
// of the instance, as DtAf::ExclAccess does. A part that does not support it is passed
// over. The first other failure stops the command and is returned; when acquiring, the
// parts acquired before it are released again. Releasing goes on past failures, and
// returns the first.
unsigned int DtFuncExclAccess(OsDrv* Drv, const DtFuncInstance* Instance, int Cmd);

// Checks that the driver is new enough for a part's proxy, as DtAf::GetPal does before
// using it (DtProxy.cpp, PROXY_MIN_DRV_VERSIONS): DTAPI_OK, DTAPI_E_DRIVER_INCOMP when it
// is older, and DTAPI_E_INTERNAL for a type the table does not have. The table holds the
// types CDtapiLite uses.
unsigned int DtFuncCheckDriverVersion(const DtDriverVersion* Version, bool IsDf,
                                      int Type);

#endif // CDTAPILITE_DT_FUNC_H

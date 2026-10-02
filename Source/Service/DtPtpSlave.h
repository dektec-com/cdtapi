// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtPtpSlave.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The state of the PTP clock slave that DtapiService runs for a port
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Reads the PTP clock slave of a port from DtapiService, without configuring it: the
// service PtpClockSlave, attached without exclusive access. Its parameters are in the
// group General, and are found by name, as their IDs may change between versions of the
// service. The values are the service's own; DtDevice.c turns them into a DtPtpStatus.
//

// A master the slave has heard, from the parameter MasterInfo.
typedef struct DtPtpMaster
{
    uint64_t Identity; // The grandmaster's clock identity, an EUI-64
    int Domain;        // The domain it announces itself in
    bool IsTimeTraceable;
    bool IsFrequencyTraceable;
    int ClockClass;   // The grandmaster's clock class
    int StepsRemoved; // The boundary clocks between it and the slave
} DtPtpMaster;

typedef struct DtPtpSlaveValues
{
    int SlaveState;          // SlaveState: Disabled 0 to Slave 5
    int LockStatus;          // MasterLockingStatus: NotInUse 0 to Locked 4
    int Domain;              // DomainNumber: the domain the slave listens in
    uint64_t MasterIdentity; // MasterIdentity: the chosen grandmaster; 0 without one
    bool HasMasterInfo;      // MasterInfo has the chosen grandmaster, in Master
    DtPtpMaster Master;
} DtPtpSlaveValues;

// Returns the result for the service's exception Exception, the number of an
// Exc::Reason in DTAPI_Services.h, as 0029 decision 2 has them.
DtapiResult DtPtpSlave_ExceptionResult(int Exception);

// Finds the master with clock identity Identity in Xml, the value of the parameter
// MasterInfo, and writes it into *Master; *Found says whether it is there. An empty Xml
// lists no masters.
// Returns:
//   DTAPI_OK
//   DTAPI_E_COMMUNICATION  Xml is not such a list
//   DTAPI_E_INVALID_ARG    a NULL argument
//   DTAPI_E_OUT_OF_MEM
DtapiResult DtPtpSlave_FindMaster(const char* Xml, uint64_t Identity, DtPtpMaster* Master,
                                  bool* Found);

// Reads the slave of the port at PortIndex, counted from 0, of the card with serial
// number Serial, through the pipe PipeName: DT_SERVICE_PIPE_NAME but for tests. Values
// is all zero after a failure.
// Returns:
//   DTAPI_OK
//   DTAPI_E_INVALID_ARG    a NULL argument or a negative PortIndex
//   DTAPI_E_OUT_OF_MEM
//   and the results of DtService_Connect, DtService_Transfer and
//   DtPtpSlave_ExceptionResult, and DTAPI_E_SERVICE_INCOMP for a service older than
//   version 4 or without a parameter it needs
DtapiResult DtPtpSlave_Read(const char* PipeName, int64_t Serial, int PortIndex,
                            DtPtpSlaveValues* Values);

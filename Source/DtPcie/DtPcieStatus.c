// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieStatus.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: driver statuses as DTAPI results - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtPcieStatus.h"           // Interface being implemented.
#include "DtPcieAbi.h"              // The DT_STATUS_ codes.
#include "OAL/OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.
#include "cdtapi.h"                 // DTAPI result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieStatus_ToResult -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The DT_STATUS_ values are encoded differently on Windows and on Linux, so the cases are
// written with the vendored names and never with numbers. Statuses DTAPI deliberately
// reports as a driver failure, such as DT_STATUS_IO_PENDING and DT_STATUS_FAIL, have no
// case of their own and fall to the default.
//
DtapiResult DtPcieStatus_ToResult(uint32_t Status)
{
    switch (Status)
    {
    case DT_STATUS_OK:
        return DTAPI_OK;
    case DT_STATUS_OUT_OF_MEMORY:
        return DTAPI_E_OUT_OF_MEM;
    case DT_STATUS_BUFFER_OVERFLOW:
        return DTAPI_E_TOO_LONG;
    case DT_STATUS_INVALID_PARAMETER:
        return DTAPI_E_INVALID_ARG;
    case DT_STATUS_NOT_SUPPORTED:
        return DTAPI_E_NOT_SUPPORTED;
    case DT_STATUS_NOT_FOUND:
    case DT_STATUS_NOT_FOUND_INCOMP_FW:
        return DTAPI_E_NOT_FOUND;
    case DT_STATUS_TIMEOUT:
        return DTAPI_E_TIMEOUT;
    case DT_STATUS_NOT_INITIALISED:
        return DTAPI_E_NOT_INITIALIZED;
    case DT_STATUS_CONFIG_ERROR:
        return DTAPI_E_CONFIG;
    case DT_STATUS_IN_USE:
        return DTAPI_E_IN_USE;
    case DT_STATUS_OUT_OF_RESOURCES:
    case DT_STATUS_MULTICASTLIST_FULL:
        return DTAPI_E_OUT_OF_RESOURCES;
    case DT_STATUS_POWERDOWN:
        return DTAPI_E_EVENT_POWER;
    case DT_STATUS_BUSY:
        return DTAPI_E_BUSY;
    case DT_STATUS_UNKNOWN_PHY:
        return DTAPI_E_NOT_SUPPORTED;
    case DT_STATUS_NOT_STARTED:
        return DTAPI_E_NOT_STARTED;
    case DT_STATUS_VERSION_MISMATCH:
        return DTAPI_E_DRIVER_INCOMP;
    case DT_STATUS_BUF_TOO_SMALL:
        return DTAPI_E_BUF_TOO_SMALL;
    case DT_STATUS_BUF_TOO_LARGE:
        return DTAPI_E_BUF_TOO_LARGE;
    case DT_STATUS_NO_POWER:
        return DTAPI_E_NO_POWER;
    case DT_STATUS_EXCL_ACCESS_REQD:
        return DTAPI_E_EXCL_ACCESS_REQD;
    case DT_STATUS_LOCKED:
        return DTAPI_E_LOCKED;
    case DT_STATUS_NO_IOSTUB:
    case DT_STATUS_NOT_IMPLEMENTED:
        return DTAPI_E_NOT_IMPLEMENTED;
    case DT_STATUS_NOT_ENABLED:
    case DT_STATUS_INVALID_IN_OPMODE:
        return DTAPI_E_INVALID_MODE;
    case DT_STATUS_KEYWORD_ERROR:
        return DTAPI_E_KEYWORD;
    case DT_STATUS_TOO_LONG:
        return DTAPI_E_TOO_LONG;
    case DT_STATUS_EEPROM_FULL:
        return DTAPI_E_EEPROM_FULL;
    case DT_STATUS_ALREADY_OPEN_EXCL:
        return DTAPI_E_ALREADY_EXCL_ACCESS;
    default:
        return DTAPI_E_DEV_DRIVER;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieStatus_OutcomeToResult -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPcieStatus_OutcomeToResult(int Outcome, uint32_t Status)
{
    switch (Outcome)
    {
    case OS_IOCTL_OK:
        return DTAPI_OK;
    case OS_IOCTL_DRIVER_STATUS:
        return DtPcieStatus_ToResult(Status);
    case OS_IOCTL_NO_RESOURCES:
        return DTAPI_E_OUT_OF_RESOURCES;
    default:
        return DTAPI_E_COMMUNICATION;
    }
}

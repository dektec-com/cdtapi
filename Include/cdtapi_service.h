// #*#*#*#*#*#*#*#*#*#*#*#*#* cdtapi_service.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Public C API to the services of DtapiService, such as the PTP clock slave
//
// SPDX-License-Identifier: BSD-3-Clause
//
// DtapiService is a program of DekTec's that runs beside the driver. Among other things,
// it runs the PTP clock slave that keeps the time-of-day clock of a DTA-2110 or DTA-2125
// on the time of a PTP grandmaster. A program talks to such a service through a proxy:
//
// 1. DtServiceProxy_Attach attaches a proxy to the service of a port;
// 2. DtServiceProxy_GetParDescs describes the service's parameters, and
//    DtServiceProxy_FindParId finds a parameter's ID by its name, which stays the same
//    while IDs may change between versions of the service;
// 3. DtServiceProxy_GetParVal and _SetParVal get and set parameters, as variants;
// 4. DtServiceProxy_SaveSettings keeps the settings when the service starts again;
// 5. DtServiceProxy_Detach ends it.
//
// A proxy that only reads attaches without exclusive access. Setting a parameter needs
// exclusive access, which the service gives one proxy at a time; while one has it, the
// service refuses every other attach.
//
// The service must be installed and running: on Linux, the daemon DtapiServiced from the
// SDK. With CDTAPI_SIM, an emulated service in the program answers instead, for the
// emulated cards.
//
// A function that fails returns an error and sets the text that GetLastException(), in
// cdtapi_avfifo.h, returns on the calling thread. When the service refused,
// DtServiceProxy_LastException gives its own reason. The errors are:
//
//   DTAPI_E_INVALID_ARG         a NULL argument, a parameter's value of the wrong type,
//                               out of range or not writable
//   DTAPI_E_DEVICE              a device that is NULL or not attached
//   DTAPI_E_NO_SUCH_PORT        a port the device does not have
//   DTAPI_E_NOT_SUPPORTED       the port has no such service
//   DTAPI_E_NO_SUCH_DEVICE      the service does not find the card
//   DTAPI_E_DRIVER_INCOMP       the service finds the driver too old
//   DTAPI_E_CONNECT_TO_SERVICE  no service runs, or the connection to it broke
//   DTAPI_E_SERVICE_INCOMP      the service is older than version 4
//   DTAPI_E_IN_USE              another proxy has exclusive access, or this one sets a
//                               parameter without it
//   DTAPI_E_NOT_FOUND           no parameter or group with that ID or name
//   DTAPI_E_COMMUNICATION       an answer of the service CDTAPI cannot read
//   DTAPI_E_OUT_OF_MEM
//
// A proxy is used from one thread at a time. After DTAPI_E_CONNECT_TO_SERVICE or
// DTAPI_E_COMMUNICATION its connection is of no further use: detach it and attach again.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // Results and devices.

#ifdef __cplusplus
extern "C"
{
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Variants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The types of a variant, in the service's numbering.
typedef enum DtVariantType
{
    DT_VARIANT_EMPTY,
    DT_VARIANT_DOUBLE,
    DT_VARIANT_INT,
    DT_VARIANT_UINT64,
    DT_VARIANT_BOOL,
    DT_VARIANT_STRING,
} DtVariantType;

// A value of a parameter: the field that Type names holds it.
//
// A variant that CDTAPI fills owns its String, which DtVariant_Clear frees. A variant
// that a program fills to set a parameter points at the program's own string, which
// CDTAPI does not keep; the program does not clear it.
typedef struct DtVariant
{
    DtVariantType Type;
    double Double;
    int Int;
    uint64_t UInt64;
    bool Bool;
    const char* String; // UTF-8
} DtVariant;

// Frees the string of a variant that CDTAPI filled, and empties it. A NULL Value does
// nothing.
CDTAPI_API void DtVariant_Clear(DtVariant* Value);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Proxy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The services, for DtServiceProxy_Attach.
typedef enum DtServiceType
{
    DT_SERVICE_NONE,            // Refused
    DT_SERVICE_PTP_CLOCK_SLAVE, // The PTP clock slave of an IP port
} DtServiceType;

// A value of a parameter that is an enumeration, with its name.
typedef struct DtServiceEnumVal
{
    int Value;
    const char* Name; // E.g. "EndToEnd"
} DtServiceEnumVal;

// A parameter of a service, as DtServiceProxy_GetParDescs describes it.
typedef struct DtServiceParDesc
{
    int ParId;               // Its ID, which may change with the service's version
    const char* Name;        // Its name, e.g. "DomainNumber"
    const char* Description; // What it is, in English
    DtVariantType Type;
    bool IsWritable;
    DtVariant Min;                    // Empty for a string
    DtVariant Max;                    // Empty for a string
    DtVariant Default;                // The value the service starts with
    const DtServiceEnumVal* EnumVals; // The values of an enumeration; NULL for none
    int NumEnumVals;
} DtServiceParDesc;

// A parameter's ID with its value, for DtServiceProxy_GetParVals and _SetParVals.
typedef struct DtServiceParVal
{
    int ParId;
    DtVariant Value;
} DtServiceParVal;

// The service's reasons for refusing a call, in its numbering, as
// DtServiceProxy_LastException gives them.
typedef enum DtServiceExc
{
    DT_SERVICE_EXC_NONE = -1, // The service did not refuse
    DT_SERVICE_EXC_DEVICE_NOT_ATTACHED,
    DT_SERVICE_EXC_DEVICE_ALREADY_ATTACHED,
    DT_SERVICE_EXC_INVALID_PAR_ID,
    DT_SERVICE_EXC_INVALID_PORT_NO,
    DT_SERVICE_EXC_SERVICE_ALREADY_IN_USE,
    DT_SERVICE_EXC_TYPE_MISMATCH,
    DT_SERVICE_EXC_INVALID_PAR_TYPE,
    DT_SERVICE_EXC_PAR_OUT_OF_RANGE,
    DT_SERVICE_EXC_UNKNOWN_PAR_GROUP,
    DT_SERVICE_EXC_UNKNOWN_PAR_ID,
    DT_SERVICE_EXC_UNKNOWN_PAR_NAME,
    DT_SERVICE_EXC_UNKNOWN_SERVICE,
    DT_SERVICE_EXC_INVALID_SIZE,
    DT_SERVICE_EXC_INVALID_CMD,
    DT_SERVICE_EXC_OUT_OF_MEMORY,
    DT_SERVICE_EXC_UNKNOWN,
    DT_SERVICE_EXC_INVALID_XML_STRING,
    DT_SERVICE_EXC_INCOMPATIBLE_SERVICE,
    DT_SERVICE_EXC_CONNECT_TO_SERVICE,
    DT_SERVICE_EXC_NOT_IMPLEMENTED,
    DT_SERVICE_EXC_EXCLUSIVE_IN_USE,
    DT_SERVICE_EXC_UNKNOWN_GROUP_NAME,
    DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS,
    DT_SERVICE_EXC_READ_ONLY_PAR,
    DT_SERVICE_EXC_DRIVER_INCOMPATIBLE,
    DT_SERVICE_EXC_ERROR_ATTACHING_DEVICE,
    DT_SERVICE_EXC_DEVICE_NOT_FOUND,
    DT_SERVICE_EXC_NOT_SUPPORTED,
    DT_SERVICE_EXC_NO_ADAPTER_IP_ADDR,
    DT_SERVICE_EXC_MULTICAST_JOIN,
    DT_SERVICE_EXC_PROTOCOL_BIND,
    DT_SERVICE_EXC_PTP_TIMESTAMPING,
    DT_SERVICE_EXC_INVALID_STATE,
    DT_SERVICE_EXC_PTP_SEND,
} DtServiceExc;

typedef struct DtServiceProxy DtServiceProxy;

// Attaches a proxy to the service Service of the port Port, counted from 1, of Device,
// which must be attached. With Exclusive true, the proxy may set parameters, and the
// service refuses every other attach until it detaches. *Proxy is NULL after a failure.
CDTAPI_API DtapiResult DtServiceProxy_Attach(const DtDevice* Device, int Port,
                                             DtServiceType Service, bool Exclusive,
                                             DtServiceProxy** Proxy);

// Detaches the proxy from its service, closes its connection and frees it. A NULL Proxy
// does nothing.
CDTAPI_API void DtServiceProxy_Detach(DtServiceProxy* Proxy);

// Returns the ID of the parameter named Name among the NumDescs descriptions Descs, or
// -1 when none has that name.
CDTAPI_API int DtServiceProxy_FindParId(const DtServiceParDesc* Descs, int NumDescs,
                                        const char* Name);

// Frees the descriptions DtServiceProxy_GetParDescs gave. NULL Descs does nothing.
CDTAPI_API void DtServiceProxy_FreeParDescs(DtServiceParDesc* Descs, int NumDescs);

// Frees the names of groups DtServiceProxy_GetParGroups gave. NULL Groups does nothing.
CDTAPI_API void DtServiceProxy_FreeParGroups(char** Groups, int NumGroups);

// Describes the parameters of the group Group in a new *Descs of *NumDescs, to be freed
// with DtServiceProxy_FreeParDescs.
CDTAPI_API DtapiResult DtServiceProxy_GetParDescs(DtServiceProxy* Proxy,
                                                  const char* Group,
                                                  DtServiceParDesc** Descs,
                                                  int* NumDescs);

// Lists the names of the service's groups of parameters in a new *Groups of *NumGroups,
// to be freed with DtServiceProxy_FreeParGroups. The PTP clock slave has one, "General".
CDTAPI_API DtapiResult DtServiceProxy_GetParGroups(DtServiceProxy* Proxy, char*** Groups,
                                                   int* NumGroups);

// Gets the value of the parameter ParId into *Value, to be cleared with DtVariant_Clear.
CDTAPI_API DtapiResult DtServiceProxy_GetParVal(DtServiceProxy* Proxy, int ParId,
                                                DtVariant* Value);

// Gets the values of NumVals parameters at once: the program sets each ParId in Vals,
// and CDTAPI fills each Value, to be cleared with DtVariant_Clear. After a failure the
// values are empty.
CDTAPI_API DtapiResult DtServiceProxy_GetParVals(DtServiceProxy* Proxy,
                                                 DtServiceParVal* Vals, int NumVals);

// Returns the service's reason for refusing the last call that failed;
// DT_SERVICE_EXC_NONE when the last call succeeded or failed for another reason, and
// for a NULL Proxy.
CDTAPI_API DtServiceExc DtServiceProxy_LastException(const DtServiceProxy* Proxy);

// Saves the service's current settings, so that it starts with them after a restart of
// the service or of the computer. Needs exclusive access.
CDTAPI_API DtapiResult DtServiceProxy_SaveSettings(DtServiceProxy* Proxy);

// Sets the parameter ParId to *Value, which must be of the parameter's type; an empty
// variant is refused. Needs exclusive access. The setting lasts until the service
// stops, unless it is saved.
CDTAPI_API DtapiResult DtServiceProxy_SetParVal(DtServiceProxy* Proxy, int ParId,
                                                const DtVariant* Value);

// Sets NumVals parameters at once, as DtServiceProxy_SetParVal sets one.
CDTAPI_API DtapiResult DtServiceProxy_SetParVals(DtServiceProxy* Proxy,
                                                 const DtServiceParVal* Vals,
                                                 int NumVals);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP clock slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The PTP clock slave has its parameters in the group "General". Among them:
//
//   Enable               bool    Whether the slave runs; false until it is set
//   DomainNumber         int     The PTP domain it listens in, 0 to 127
//   DelayMechanism       int     A DtPtpDelayMechanism
//   NetworkProtocol      int     A DtPtpNetworkProtocol
//   IpV6Scope            int     A DtPtpIpV6Scope
//   PeerUnicastAddress   string  The peer's address in peer-to-peer mode; multicast
//                                when empty
//   SlaveState           int     A DtPtpSlaveState
//   MasterLockingStatus  int     A DtPtpLockStatus
//   MasterIdentity       uint64  The chosen grandmaster's clock identity; 0 for none
//   MasterInfo           string  Every master heard; see DtPtp_ParseMasterInfo
//
// The first six are writable. DtServiceProxy_GetParDescs lists them all. The enumerations
// below are the values of these parameters, in the service's numbering.
//

typedef enum DtPtpDelayMechanism
{
    DT_PTP_DELAY_AUTO,
    DT_PTP_DELAY_END_TO_END,
    DT_PTP_DELAY_PEER_TO_PEER,
} DtPtpDelayMechanism;

typedef enum DtPtpIpV6Scope
{
    DT_PTP_IPV6_SCOPE_INTERFACE_LOCAL = 1,
    DT_PTP_IPV6_SCOPE_LINK_LOCAL = 2,
    DT_PTP_IPV6_SCOPE_ADMIN_LOCAL = 4,
    DT_PTP_IPV6_SCOPE_SITE_LOCAL = 5,
    DT_PTP_IPV6_SCOPE_ORGANIZATION_LOCAL = 8,
    DT_PTP_IPV6_SCOPE_GLOBAL = 14,
} DtPtpIpV6Scope;

typedef enum DtPtpLockStatus
{
    DT_PTP_LOCK_NOT_IN_USE,
    DT_PTP_LOCK_FREE_RUN,
    DT_PTP_LOCK_COLD_LOCKING,
    DT_PTP_LOCK_WARM_LOCKING,
    DT_PTP_LOCK_LOCKED,
} DtPtpLockStatus;

typedef enum DtPtpNetworkProtocol
{
    DT_PTP_PROTOCOL_IPV4,
    DT_PTP_PROTOCOL_IPV6,
} DtPtpNetworkProtocol;

typedef enum DtPtpSlaveState
{
    DT_PTP_SLAVE_DISABLED,
    DT_PTP_SLAVE_INITIALIZING,
    DT_PTP_SLAVE_LISTENING,
    DT_PTP_SLAVE_FAULTY,
    DT_PTP_SLAVE_UNCALIBRATED,
    DT_PTP_SLAVE_SLAVE,
} DtPtpSlaveState;

// A master the slave has heard, from its parameter MasterInfo. A clock identity is the
// eight bytes of the EUI-64, the first the most significant: printed in hexadecimal, it
// is the EUI-64.
typedef struct DtPtpMasterInfo
{
    uint64_t GrandmasterIdentity;
    uint64_t ParentPortIdentity; // The clock the slave hears it through
    int ParentPortNumber;
    int DomainNumber;
    int StepsRemoved;
    bool IsTimeTraceable;
    bool IsFrequencyTraceable;
    bool IsAlternateMaster;
    bool IsLeap59;
    bool IsLeap61;
    bool IsCurrentUtcOffsetValid;
    int CurrentLocalOffset; // The UTC offset it announces, in seconds
    int GrandmasterClockClass;
    int GrandmasterClockAccuracy;
    int OffsetScaledLogVariance;
    int GrandmasterPriority1;
    int GrandmasterPriority2;
    int TimeSource;
    int MajorSdoId;
    int MinorSdoId;
    int AnnounceInterval;     // As a power of 2, in seconds
    int AnnounceMsg;          // The announce messages heard
    char OriginTimestamp[64]; // As the service writes it
    char IpAddress[64];       // The master's address, if the service knows it
} DtPtpMasterInfo;

// What DtDevice_GetPtpStatus reads. GrandmasterId is the identity as the bytes of the
// EUI-64, in the order it is written; DtPtpMasterInfo and the parameter MasterIdentity
// have it as a number.
typedef struct DtPtpStatus
{
    DtPtpSlaveState SlaveState;
    DtPtpLockStatus LockStatus;
    bool HasMaster;           // False: the fields below are zero
    uint8_t GrandmasterId[8]; // The chosen grandmaster's EUI-64
    int Domain;               // The domain the slave listens in, 0 to 127
    bool IsTimeTraceable;
    bool IsFrequencyTraceable;
    int ClockClass;   // The grandmaster's clock class
    int StepsRemoved; // The boundary clocks between it and the slave
} DtPtpStatus;

// Reads the PTP clock slave of the port Port, counted from 1, of Device: its state, and
// the grandmaster it follows. It attaches a proxy without exclusive access, reads and
// detaches. The traceability, clock class and steps removed are those the slave has
// heard the grandmaster announce; false and 0 while it has not.
CDTAPI_API DtapiResult DtDevice_GetPtpStatus(const DtDevice* Device, int Port,
                                             DtPtpStatus* Status);

// Frees the masters DtPtp_ParseMasterInfo gave. NULL Masters does nothing.
CDTAPI_API void DtPtp_FreeMasterInfo(DtPtpMasterInfo* Masters);

// Reads Xml, the value of the parameter MasterInfo, into a new *Masters of *NumMasters,
// to be freed with DtPtp_FreeMasterInfo. An empty Xml lists no masters.
CDTAPI_API DtapiResult DtPtp_ParseMasterInfo(const char* Xml, DtPtpMasterInfo** Masters,
                                             int* NumMasters);

#ifdef __cplusplus
}
#endif

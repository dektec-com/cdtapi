// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtPtpSlave.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The PTP clock slave, typed: its settings, its status and its masters
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h" // The text of the last failure.
#include "Core/DtAlloc.h"     // Allocation seam.
#include "DtPtpSlave.h"       // Interface being implemented.
#include "DtServiceProxy.h"   // The proxy it wraps.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The group of the slave's parameters.
#define DT_PTP_GROUP "General"

// The parameters the slave uses: first the six settings, in the order of the
// DT_PTP_CONFIG_ bits, then those of its status.
enum
{
    PAR_ENABLE,
    PAR_DOMAIN,
    PAR_DELAY_MECHANISM,
    PAR_NETWORK_PROTOCOL,
    PAR_IPV6_SCOPE,
    PAR_PEER_ADDRESS,
    NUM_CONFIG_PARS,
    PAR_SLAVE_STATE = NUM_CONFIG_PARS,
    PAR_LOCK_STATUS,
    PAR_MASTER_IDENTITY,
    PAR_MASTER_INFO,
    NUM_PARS
};
static const char* const ParNames[NUM_PARS] = {
    "Enable",         "DomainNumber",       "DelayMechanism", "NetworkProtocol",
    "IpV6Scope",      "PeerUnicastAddress", "SlaveState",     "MasterLockingStatus",
    "MasterIdentity", "MasterInfo"};
static const DtVariantType ParTypes[NUM_PARS] = {
    DT_VARIANT_BOOL,   DT_VARIANT_INT,    DT_VARIANT_INT, DT_VARIANT_INT,
    DT_VARIANT_INT,    DT_VARIANT_STRING, DT_VARIANT_INT, DT_VARIANT_INT,
    DT_VARIANT_UINT64, DT_VARIANT_STRING};

// The parameters of the status, in the order GetStatus reads them.
static const int StatusPars[] = {PAR_SLAVE_STATE, PAR_LOCK_STATUS, PAR_DOMAIN,
                                 PAR_MASTER_IDENTITY, PAR_MASTER_INFO};
#define NUM_STATUS_PARS (int)(sizeof(StatusPars) / sizeof(StatusPars[0]))

struct DtPtpSlave
{
    DtServiceProxy* Proxy;
    DtServiceParDesc* Descs; // The descriptions of the group, to check settings against
    int NumDescs;
    const DtServiceParDesc* Pars[NUM_PARS]; // Each parameter's description, in Descs
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Refuse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Records the failure of the function Where as the thread's last one and returns Result.
//
static DtapiResult Refuse(DtapiResult Result, const char* Where, const char* What)
{
    return DtAvError_Set(Result, Where, What);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Adopt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes a slave of the attached Proxy: reads the descriptions of the group and finds each
// parameter, of the type the slave expects. The slave owns Proxy from here, also when
// this fails, and then detaches it.
//
static DtapiResult Adopt(DtServiceProxy* Proxy, DtPtpSlave** Slave)
{
    static const char Where[] = "DtPtpSlave_Attach";
    DtPtpSlave* New = (DtPtpSlave*)DtAlloc_Malloc(sizeof(DtPtpSlave));
    if (New == NULL)
    {
        DtServiceProxy_Detach(Proxy);
        return Refuse(DTAPI_E_OUT_OF_MEM, Where, "no memory for the slave");
    }
    memset(New, 0, sizeof(*New));
    New->Proxy = Proxy;
    DtapiResult Result =
        DtServiceProxy_GetParDescs(Proxy, DT_PTP_GROUP, &New->Descs, &New->NumDescs);
    for (int p = 0; p < NUM_PARS && Result == DTAPI_OK; p++)
    {
        for (int i = 0; i < New->NumDescs && New->Pars[p] == NULL; i++)
        {
            if (strcmp(New->Descs[i].Name, ParNames[p]) == 0)
                New->Pars[p] = &New->Descs[i];
        }
        if (New->Pars[p] == NULL || New->Pars[p]->Type != ParTypes[p])
        {
            char What[96];
            snprintf(What, sizeof(What),
                     "the service has no parameter %s of the expected type", ParNames[p]);
            Result = Refuse(DTAPI_E_SERVICE_INCOMP, Where, What);
        }
    }
    if (Result != DTAPI_OK)
    {
        DtPtpSlave_Detach(New);
        return Result;
    }
    *Slave = New;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPars -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the NumPars parameters Pars, places in ParNames, into Vals in one GET_PARVALS,
// and checks that each is of its type. Vals are cleared after a failure.
//
static DtapiResult ReadPars(DtPtpSlave* Slave, const int* Pars, int NumPars,
                            DtServiceParVal* Vals, const char* Where)
{
    memset(Vals, 0, (size_t)NumPars * sizeof(DtServiceParVal));
    for (int i = 0; i < NumPars; i++)
        Vals[i].ParId = Slave->Pars[Pars[i]]->ParId;
    DtapiResult Result = DtServiceProxy_GetParVals(Slave->Proxy, Vals, NumPars);
    if (Result != DTAPI_OK)
        return Result;
    for (int i = 0; i < NumPars && Result == DTAPI_OK; i++)
    {
        if (Vals[i].Value.Type != ParTypes[Pars[i]])
            Result = Refuse(DTAPI_E_SERVICE_INCOMP, Where,
                            "the service answered a value of another type");
    }
    if (Result != DTAPI_OK)
    {
        for (int i = 0; i < NumPars; i++)
            DtVariant_Clear(&Vals[i].Value);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAllowed -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether Value is allowed for the integer parameter Desc: one of its enumeration's
// values, or within its minimum and maximum, as the service checks it.
//
static bool IsAllowed(const DtServiceParDesc* Desc, int Value)
{
    if (Desc->NumEnumVals > 0)
    {
        for (int i = 0; i < Desc->NumEnumVals; i++)
        {
            if (Desc->EnumVals[i].Value == Value)
                return true;
        }
        return false;
    }
    return Desc->Min.Type == DT_VARIANT_INT && Desc->Max.Type == DT_VARIANT_INT &&
           Value >= Desc->Min.Int && Value <= Desc->Max.Int;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FillMaster -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills the master's part of *Status from the list Xml: the entry of the chosen
// grandmaster Identity, if the slave has heard it announce itself.
//
static DtapiResult FillMaster(const char* Xml, uint64_t Identity, DtPtpStatus* Status)
{
    DtPtpMasterInfo* Masters = NULL;
    int NumMasters = 0;
    DtapiResult Result = DtPtp_ParseMasterInfo(Xml, &Masters, &NumMasters);
    if (Result != DTAPI_OK)
        return Result;
    for (int i = 0; i < NumMasters; i++)
    {
        if (Masters[i].GrandmasterIdentity != Identity)
            continue;
        Status->IsTimeTraceable = Masters[i].IsTimeTraceable;
        Status->IsFrequencyTraceable = Masters[i].IsFrequencyTraceable;
        Status->ClockClass = Masters[i].GrandmasterClockClass;
        Status->StepsRemoved = Masters[i].StepsRemoved;
        break;
    }
    DtPtp_FreeMasterInfo(Masters);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP clock slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevice_GetPtpStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtDevice_GetPtpStatus(const DtDevice* Device, int Port, DtPtpStatus* Status)
{
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Status == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, "DtDevice_GetPtpStatus", "Status is NULL");
    DtPtpSlave* Slave = NULL;
    DtapiResult Result = DtPtpSlave_Attach(Device, Port, false, &Slave);
    if (Result != DTAPI_OK)
        return Result;
    Result = DtPtpSlave_GetStatus(Slave, Status);
    DtPtpSlave_Detach(Slave);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPtpSlave_Attach(const DtDevice* Device, int Port, bool Exclusive,
                              DtPtpSlave** Slave)
{
    if (Slave != NULL)
        *Slave = NULL;
    if (Slave == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, "DtPtpSlave_Attach", "Slave is NULL");
    DtServiceProxy* Proxy = NULL;
    DtapiResult Result = DtServiceProxy_Attach(Device, Port, DT_SERVICE_PTP_CLOCK_SLAVE,
                                               Exclusive, &Proxy);
    return Result == DTAPI_OK ? Adopt(Proxy, Slave) : Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_AttachTo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPtpSlave_AttachTo(const char* PipeName, int64_t Serial, int PortIndex,
                                bool Exclusive, DtPtpSlave** Slave)
{
    if (Slave != NULL)
        *Slave = NULL;
    if (Slave == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, "DtPtpSlave_Attach", "Slave is NULL");
    DtServiceProxy* Proxy = NULL;
    DtapiResult Result = DtServiceProxy_AttachTo(
        PipeName, Serial, PortIndex, DT_SERVICE_PTP_CLOCK_SLAVE, Exclusive, &Proxy);
    return Result == DTAPI_OK ? Adopt(Proxy, Slave) : Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtPtpSlave_Detach(DtPtpSlave* Slave)
{
    if (Slave == NULL)
        return;
    DtServiceProxy_FreeParDescs(Slave->Descs, Slave->NumDescs);
    DtServiceProxy_Detach(Slave->Proxy);
    DtAlloc_Free(Slave);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_GetConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPtpSlave_GetConfig(DtPtpSlave* Slave, DtPtpConfig* Config)
{
    static const char Where[] = "DtPtpSlave_GetConfig";
    static const int Pars[NUM_CONFIG_PARS] = {PAR_ENABLE,          PAR_DOMAIN,
                                              PAR_DELAY_MECHANISM, PAR_NETWORK_PROTOCOL,
                                              PAR_IPV6_SCOPE,      PAR_PEER_ADDRESS};
    if (Config != NULL)
        memset(Config, 0, sizeof(*Config));
    if (Slave == NULL || Config == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, Where, "an argument is NULL");

    DtServiceParVal Vals[NUM_CONFIG_PARS];
    DtapiResult Result = ReadPars(Slave, Pars, NUM_CONFIG_PARS, Vals, Where);
    if (Result != DTAPI_OK)
        return Result;
    const char* Peer = Vals[PAR_PEER_ADDRESS].Value.String;
    if (strlen(Peer) >= sizeof(Config->PeerAddress))
        Result = Refuse(DTAPI_E_BUF_TOO_SMALL, Where, "the peer address is too long");
    else
    {
        Config->Fields = DT_PTP_CONFIG_ALL;
        Config->Enable = Vals[PAR_ENABLE].Value.Bool;
        Config->Domain = Vals[PAR_DOMAIN].Value.Int;
        Config->DelayMechanism = (DtPtpDelayMechanism)Vals[PAR_DELAY_MECHANISM].Value.Int;
        Config->NetworkProtocol =
            (DtPtpNetworkProtocol)Vals[PAR_NETWORK_PROTOCOL].Value.Int;
        Config->IpV6Scope = (DtPtpIpV6Scope)Vals[PAR_IPV6_SCOPE].Value.Int;
        snprintf(Config->PeerAddress, sizeof(Config->PeerAddress), "%s", Peer);
    }
    for (int i = 0; i < NUM_CONFIG_PARS; i++)
        DtVariant_Clear(&Vals[i].Value);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_GetMasters -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPtpSlave_GetMasters(DtPtpSlave* Slave, DtPtpMasterInfo** Masters,
                                  int* NumMasters)
{
    static const char Where[] = "DtPtpSlave_GetMasters";
    static const int Pars[1] = {PAR_MASTER_INFO};
    if (Masters != NULL)
        *Masters = NULL;
    if (NumMasters != NULL)
        *NumMasters = 0;
    if (Slave == NULL || Masters == NULL || NumMasters == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, Where, "an argument is NULL");
    DtServiceParVal Vals[1];
    DtapiResult Result = ReadPars(Slave, Pars, 1, Vals, Where);
    if (Result != DTAPI_OK)
        return Result;
    Result = DtPtp_ParseMasterInfo(Vals[0].Value.String, Masters, NumMasters);
    DtVariant_Clear(&Vals[0].Value);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_GetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the parameters of the status in one go, and takes the chosen grandmaster's
// announcement out of the list of masters.
//
DtapiResult DtPtpSlave_GetStatus(DtPtpSlave* Slave, DtPtpStatus* Status)
{
    static const char Where[] = "DtPtpSlave_GetStatus";
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Slave == NULL || Status == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, Where, "an argument is NULL");

    DtServiceParVal Vals[NUM_STATUS_PARS];
    DtapiResult Result = ReadPars(Slave, StatusPars, NUM_STATUS_PARS, Vals, Where);
    if (Result != DTAPI_OK)
        return Result;
    Status->SlaveState = (DtPtpSlaveState)Vals[0].Value.Int;
    Status->LockStatus = (DtPtpLockStatus)Vals[1].Value.Int;
    uint64_t Identity = Vals[3].Value.UInt64;
    Status->HasMaster = Identity != 0;
    if (Status->HasMaster)
    {
        for (int i = 0; i < 8; i++)
            Status->GrandmasterId[i] = (uint8_t)(Identity >> (56 - 8 * i));
        Status->Domain = Vals[2].Value.Int;
        Result = FillMaster(Vals[4].Value.String, Identity, Status);
    }
    for (int i = 0; i < NUM_STATUS_PARS; i++)
        DtVariant_Clear(&Vals[i].Value);
    if (Result != DTAPI_OK)
        memset(Status, 0, sizeof(*Status));
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_Proxy -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtServiceProxy* DtPtpSlave_Proxy(DtPtpSlave* Slave)
{
    return Slave != NULL ? Slave->Proxy : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_SaveSettings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPtpSlave_SaveSettings(DtPtpSlave* Slave)
{
    if (Slave == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, "DtPtpSlave_SaveSettings", "Slave is NULL");
    return DtServiceProxy_SaveSettings(Slave->Proxy);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_SetConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The values are checked as the service would, so that the service, which sets them in
// order and stops at the first it refuses, is left only what it alone can know.
//
DtapiResult DtPtpSlave_SetConfig(DtPtpSlave* Slave, const DtPtpConfig* Config)
{
    static const char Where[] = "DtPtpSlave_SetConfig";
    if (Slave == NULL || Config == NULL)
        return Refuse(DTAPI_E_INVALID_ARG, Where, "an argument is NULL");
    if (Config->Fields == 0 || (Config->Fields & ~DT_PTP_CONFIG_ALL) != 0)
        return Refuse(DTAPI_E_INVALID_ARG, Where, "Fields names no setting, or others");

    const int Values[NUM_CONFIG_PARS] = {0,
                                         Config->Domain,
                                         (int)Config->DelayMechanism,
                                         (int)Config->NetworkProtocol,
                                         (int)Config->IpV6Scope,
                                         0};
    DtServiceParVal Vals[NUM_CONFIG_PARS];
    int NumVals = 0;
    memset(Vals, 0, sizeof(Vals));
    for (int p = 0; p < NUM_CONFIG_PARS; p++)
    {
        if ((Config->Fields & (1u << p)) == 0)
            continue;
        DtServiceParVal* Val = &Vals[NumVals++];
        Val->ParId = Slave->Pars[p]->ParId;
        Val->Value.Type = ParTypes[p];
        if (p == PAR_ENABLE)
            Val->Value.Bool = Config->Enable;
        else if (p == PAR_PEER_ADDRESS)
        {
            if (memchr(Config->PeerAddress, '\0', sizeof(Config->PeerAddress)) == NULL)
                return Refuse(DTAPI_E_INVALID_ARG, Where,
                              "PeerAddress has no terminating zero");
            Val->Value.String = Config->PeerAddress;
        }
        else
        {
            if (!IsAllowed(Slave->Pars[p], Values[p]))
            {
                char What[96];
                snprintf(What, sizeof(What), "%s %d is not allowed", ParNames[p],
                         Values[p]);
                return Refuse(DTAPI_E_INVALID_ARG, Where, What);
            }
            Val->Value.Int = Values[p];
        }
    }
    return DtServiceProxy_SetParVals(Slave->Proxy, Vals, NumVals);
}

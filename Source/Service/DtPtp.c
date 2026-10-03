// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPtp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The PTP clock slave: its status and its list of masters
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h" // The text of the last failure.
#include "Core/DtAlloc.h"     // Allocation seam.
#include "DtPtp.h"            // Interface being implemented.
#include "DtXml.h"            // The list of masters.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The group of the slave's parameters.
#define DT_PTP_GROUP "General"

// The parameters the status is read from, in the order of ParNames and ParTypes.
enum
{
    PAR_SLAVE_STATE,
    PAR_LOCK_STATUS,
    PAR_DOMAIN,
    PAR_MASTER_IDENTITY,
    PAR_MASTER_INFO,
    NUM_PARS
};
static const char* const ParNames[NUM_PARS] = {
    "SlaveState", "MasterLockingStatus", "DomainNumber", "MasterIdentity", "MasterInfo"};
static const DtVariantType ParTypes[NUM_PARS] = {
    DT_VARIANT_INT, DT_VARIANT_INT, DT_VARIANT_INT, DT_VARIANT_UINT64, DT_VARIANT_STRING};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttrIntIn -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the attribute Name of Elem as an int.
//
static bool AttrIntIn(const DtXmlElem* Elem, const char* Name, int* Value)
{
    int64_t Wide = 0;
    if (!DtXml_AttrInt(Elem, Name, &Wide) || Wide < INT32_MIN || Wide > INT32_MAX)
        return false;
    *Value = (int)Wide;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttrIdIn -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a clock identity, which the service writes as a signed 64-bit integer, so that
// one with its top bit set is negative; an unsigned one is taken as well.
//
static bool AttrIdIn(const DtXmlElem* Elem, const char* Name, uint64_t* Value)
{
    int64_t Signed = 0;
    if (DtXml_AttrInt(Elem, Name, &Signed))
    {
        *Value = (uint64_t)Signed;
        return true;
    }
    return DtXml_AttrUInt(Elem, Name, Value);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeMaster -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes a master out of the element MI into *Master. The attributes are those DTAPI
// requires; IP may be missing.
//
static bool TakeMaster(const DtXmlElem* Elem, DtPtpMasterInfo* Master)
{
    const char* Origin = DtXml_Attr(Elem, "OT");
    const char* Ip = DtXml_Attr(Elem, "IP");
    if (Origin == NULL || !AttrIdIn(Elem, "ID", &Master->GrandmasterIdentity) ||
        !AttrIdIn(Elem, "PID", &Master->ParentPortIdentity) ||
        !AttrIntIn(Elem, "PPN", &Master->ParentPortNumber) ||
        !AttrIntIn(Elem, "DN", &Master->DomainNumber) ||
        !AttrIntIn(Elem, "SR", &Master->StepsRemoved) ||
        !DtXml_AttrBool(Elem, "TT", &Master->IsTimeTraceable) ||
        !DtXml_AttrBool(Elem, "FT", &Master->IsFrequencyTraceable) ||
        !DtXml_AttrBool(Elem, "AM", &Master->IsAlternateMaster) ||
        !DtXml_AttrBool(Elem, "L59", &Master->IsLeap59) ||
        !DtXml_AttrBool(Elem, "L61", &Master->IsLeap61) ||
        !DtXml_AttrBool(Elem, "CUOV", &Master->IsCurrentUtcOffsetValid) ||
        !AttrIntIn(Elem, "CUO", &Master->CurrentLocalOffset) ||
        !AttrIntIn(Elem, "CC", &Master->GrandmasterClockClass) ||
        !AttrIntIn(Elem, "CA", &Master->GrandmasterClockAccuracy) ||
        !AttrIntIn(Elem, "OSLV", &Master->OffsetScaledLogVariance) ||
        !AttrIntIn(Elem, "P1", &Master->GrandmasterPriority1) ||
        !AttrIntIn(Elem, "P2", &Master->GrandmasterPriority2) ||
        !AttrIntIn(Elem, "TS", &Master->TimeSource) ||
        !AttrIntIn(Elem, "MaId", &Master->MajorSdoId) ||
        !AttrIntIn(Elem, "MiId", &Master->MinorSdoId) ||
        !AttrIntIn(Elem, "AI", &Master->AnnounceInterval) ||
        !AttrIntIn(Elem, "AMC", &Master->AnnounceMsg))
        return false;
    snprintf(Master->OriginTimestamp, sizeof(Master->OriginTimestamp), "%s", Origin);
    snprintf(Master->IpAddress, sizeof(Master->IpAddress), "%s", Ip != NULL ? Ip : "");
    return true;
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
        return DtAvError_Set(DTAPI_E_INVALID_ARG, "DtDevice_GetPtpStatus",
                             "Status is NULL");
    DtServiceProxy* Proxy = NULL;
    DtapiResult Result =
        DtServiceProxy_Attach(Device, Port, DT_SERVICE_PTP_CLOCK_SLAVE, false, &Proxy);
    if (Result != DTAPI_OK)
        return Result;
    Result = DtPtp_ReadStatus(Proxy, Status);
    DtServiceProxy_Detach(Proxy);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtp_FreeMasterInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtPtp_FreeMasterInfo(DtPtpMasterInfo* Masters)
{
    DtAlloc_Free(Masters);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtp_ParseMasterInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The list is <V Cnt="n"> with an element MI per master.
//
DtapiResult DtPtp_ParseMasterInfo(const char* Xml, DtPtpMasterInfo** Masters,
                                  int* NumMasters)
{
    static const char Where[] = "DtPtp_ParseMasterInfo";
    if (Masters != NULL)
        *Masters = NULL;
    if (NumMasters != NULL)
        *NumMasters = 0;
    if (Xml == NULL || Masters == NULL || NumMasters == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "an argument is NULL");
    if (Xml[0] == '\0')
        return DTAPI_OK;

    DtXmlElem List;
    DtapiResult Result = DtXml_Parse(Xml, &List);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "MasterInfo is not XML");
    DtPtpMasterInfo* Found = NULL;
    int Count = 0;
    if (strcmp(List.Name, "V") != 0)
        Result = DTAPI_E_COMMUNICATION;
    else if (List.NumChildren > 0)
    {
        Found = (DtPtpMasterInfo*)DtAlloc_Malloc((size_t)List.NumChildren *
                                                 sizeof(DtPtpMasterInfo));
        if (Found == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }
    for (int i = 0; i < List.NumChildren && Result == DTAPI_OK; i++)
    {
        const DtXmlElem* Info = &List.Children[i];
        if (strcmp(Info->Name, "MI") != 0)
            continue;
        memset(&Found[Count], 0, sizeof(DtPtpMasterInfo));
        if (!TakeMaster(Info, &Found[Count]))
            Result = DTAPI_E_COMMUNICATION;
        Count++;
    }
    DtXml_Free(&List);
    if (Result != DTAPI_OK)
    {
        DtAlloc_Free(Found);
        return DtAvError_Set(Result, Where, "MasterInfo is not a list of masters");
    }
    *Masters = Found;
    *NumMasters = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtp_ReadStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Looks the parameters up by name, reads them in one go, and takes the chosen
// grandmaster's announcement out of the list of masters.
//
DtapiResult DtPtp_ReadStatus(DtServiceProxy* Proxy, DtPtpStatus* Status)
{
    static const char Where[] = "DtDevice_GetPtpStatus";
    if (Status != NULL)
        memset(Status, 0, sizeof(*Status));
    if (Proxy == NULL || Status == NULL)
        return DtAvError_Set(DTAPI_E_INVALID_ARG, Where, "an argument is NULL");

    DtServiceParDesc* Descs = NULL;
    int NumDescs = 0;
    DtapiResult Result =
        DtServiceProxy_GetParDescs(Proxy, DT_PTP_GROUP, &Descs, &NumDescs);
    if (Result != DTAPI_OK)
        return Result;
    DtServiceParVal Vals[NUM_PARS];
    memset(Vals, 0, sizeof(Vals));
    for (int p = 0; p < NUM_PARS; p++)
    {
        Vals[p].ParId = DtServiceProxy_FindParId(Descs, NumDescs, ParNames[p]);
        if (Vals[p].ParId < 0)
            Result = DTAPI_E_SERVICE_INCOMP;
    }
    DtServiceProxy_FreeParDescs(Descs, NumDescs);
    if (Result != DTAPI_OK)
        return DtAvError_Set(Result, Where, "the service lacks a parameter of the slave");
    Result = DtServiceProxy_GetParVals(Proxy, Vals, NUM_PARS);
    if (Result != DTAPI_OK)
        return Result;

    for (int p = 0; p < NUM_PARS; p++)
    {
        if (Vals[p].Value.Type != ParTypes[p])
            Result = DTAPI_E_SERVICE_INCOMP;
    }
    if (Result == DTAPI_OK)
    {
        Status->SlaveState = (DtPtpSlaveState)Vals[PAR_SLAVE_STATE].Value.Int;
        Status->LockStatus = (DtPtpLockStatus)Vals[PAR_LOCK_STATUS].Value.Int;
        uint64_t Identity = Vals[PAR_MASTER_IDENTITY].Value.UInt64;
        Status->HasMaster = Identity != 0;
        if (Status->HasMaster)
        {
            for (int i = 0; i < 8; i++)
                Status->GrandmasterId[i] = (uint8_t)(Identity >> (56 - 8 * i));
            Status->Domain = Vals[PAR_DOMAIN].Value.Int;
            Result = FillMaster(Vals[PAR_MASTER_INFO].Value.String, Identity, Status);
        }
    }
    for (int p = 0; p < NUM_PARS; p++)
        DtVariant_Clear(&Vals[p].Value);
    if (Result != DTAPI_OK)
    {
        memset(Status, 0, sizeof(*Status));
        return DtAvError_Set(Result, Where,
                             Result == DTAPI_E_SERVICE_INCOMP
                                 ? "a parameter of the slave is of another type"
                                 : "the list of masters cannot be read");
    }
    return DTAPI_OK;
}

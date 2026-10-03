// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtPtp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The list of masters of the PTP clock slave
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h" // The text of the last failure.
#include "Core/DtAlloc.h"     // Allocation seam.
#include "DtXml.h"            // The list of masters.
#include "cdtapi_service.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP clock slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

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

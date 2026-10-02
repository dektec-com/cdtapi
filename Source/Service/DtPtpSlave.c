// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtPtpSlave.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The state of the PTP clock slave that DtapiService runs for a port
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <limits.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtPtpSlave.h"   // Interface being implemented.
#include "DtService.h"    // The service's messages.
#include "DtXml.h"        // Their XML.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The oldest version of the service that has the PTP slave, as in DTAPI.
#define DT_PTP_SLAVE_MIN_VERSION 4

// The service's number of the PTP clock slave, the first of its Services.
#define DT_PTP_SLAVE_SERVICE_TYPE 0

// The group of the parameters.
#define DT_PTP_SLAVE_GROUP "General"

// The service's exceptions that have a result of their own: their places in
// Exc::Reason.
#define DT_PTP_EXC_INVALID_PORT_NO 3
#define DT_PTP_EXC_INCOMPATIBLE_SERVICE 17
#define DT_PTP_EXC_EXCLUSIVE_IN_USE 20
#define DT_PTP_EXC_DRIVER_INCOMPATIBLE 24
#define DT_PTP_EXC_DEVICE_NOT_FOUND 26
#define DT_PTP_EXC_NOT_SUPPORTED 27

// The types of a variant: their places in Variant::Type.
#define DT_PTP_VT_INT 2
#define DT_PTP_VT_UINT64 3
#define DT_PTP_VT_STRING 5

// The parameters read, in the order of ParNames and ParTypes.
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
static const int ParTypes[NUM_PARS] = {DT_PTP_VT_INT, DT_PTP_VT_INT, DT_PTP_VT_INT,
                                       DT_PTP_VT_UINT64, DT_PTP_VT_STRING};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends command Cmd with the XML Xml, which Ask frees, and reads the answer into Answer;
// a NULL Answer ignores it. An Xml of NULL is memory that ran out while it was written.
// An exception becomes its result.
//
static DtapiResult Ask(DtService* Service, DtServiceCmd Cmd, char* Xml, DtXmlElem* Answer)
{
    if (Xml == NULL)
        return DTAPI_E_OUT_OF_MEM;
    char* Text = NULL;
    int Exception = DT_SERVICE_NO_EXCEPTION;
    DtapiResult Result = DtService_Transfer(Service, Cmd, Xml, &Text, &Exception);
    DtAlloc_Free(Xml);
    if (Result == DTAPI_OK && Exception != DT_SERVICE_NO_EXCEPTION)
        Result = DtPtpSlave_ExceptionResult(Exception);
    if (Result == DTAPI_OK && Answer != NULL)
        Result = DtXml_Parse(Text, Answer);
    DtAlloc_Free(Text);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Finish -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The text of Out, or NULL when memory ran out.
//
static char* Finish(DtXmlOut* Out)
{
    char* Text = NULL;
    DtXmlOut_Finish(Out, &Text);
    return Text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValElem -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The document <Name Val="Value"/>, as the service has a single value.
//
static char* ValElem(const char* Name, int64_t Value)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, Name);
    DtXmlOut_AttrInt(&Out, "Val", Value);
    DtXmlOut_Close(&Out, Name);
    return Finish(&Out);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttrIntIn -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the attribute Name of Elem as an int.
//
static bool AttrIntIn(const DtXmlElem* Elem, const char* Name, int* Value)
{
    int64_t Wide = 0;
    if (!DtXml_AttrInt(Elem, Name, &Wide) || Wide < INT_MIN || Wide > INT_MAX)
        return false;
    *Value = (int)Wide;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult CheckVersion(DtService* Service)
{
    char* Xml = DtAlloc_Malloc(1);
    if (Xml != NULL)
        Xml[0] = '\0';
    DtXmlElem Answer;
    DtapiResult Result = Ask(Service, DT_SERVICE_CMD_VERSION, Xml, &Answer);
    if (Result != DTAPI_OK)
        return Result;
    int Major = 0;
    if (strcmp(Answer.Name, "Version") != 0 || !AttrIntIn(&Answer, "Major", &Major))
        Result = DTAPI_E_COMMUNICATION;
    else if (Major < DT_PTP_SLAVE_MIN_VERSION)
        Result = DTAPI_E_SERVICE_INCOMP;
    DtXml_Free(&Answer);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Attaches to the slave of the port without exclusive access, and sets *Id to the ID
// the service gives the attachment.
//
static DtapiResult Attach(DtService* Service, int64_t Serial, int PortIndex, int* Id)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "Attach");
    DtXmlOut_AttrInt(&Out, "Serial", Serial);
    DtXmlOut_AttrInt(&Out, "Port", PortIndex);
    DtXmlOut_AttrInt(&Out, "Instance", 0);
    DtXmlOut_AttrInt(&Out, "ServiceType", DT_PTP_SLAVE_SERVICE_TYPE);
    DtXmlOut_AttrBool(&Out, "Exclusive", false);
    DtXmlOut_Close(&Out, "Attach");

    DtXmlElem Answer;
    DtapiResult Result = Ask(Service, DT_SERVICE_CMD_ATTACH, Finish(&Out), &Answer);
    if (Result != DTAPI_OK)
        return Result;
    if (strcmp(Answer.Name, "Id") != 0 || !AttrIntIn(&Answer, "Val", Id) || *Id < 0)
        Result = DTAPI_E_COMMUNICATION;
    DtXml_Free(&Answer);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindParIds -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Looks the parameters up by name in the descriptions of the group, and writes their
// IDs into ParIds.
//
static DtapiResult FindParIds(DtService* Service, int* ParIds)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "GroupName");
    DtXmlOut_Attr(&Out, "Val", DT_PTP_SLAVE_GROUP);
    DtXmlOut_Close(&Out, "GroupName");

    DtXmlElem Answer;
    DtapiResult Result = Ask(Service, DT_SERVICE_CMD_GET_PARDESCS, Finish(&Out), &Answer);
    if (Result != DTAPI_OK)
        return Result;
    if (strcmp(Answer.Name, "ParDescs") != 0)
        Result = DTAPI_E_COMMUNICATION;
    for (int p = 0; p < NUM_PARS && Result == DTAPI_OK; p++)
    {
        ParIds[p] = -1;
        for (int i = 0; i < Answer.NumChildren && ParIds[p] < 0; i++)
        {
            const DtXmlElem* Desc = &Answer.Children[i];
            const char* Name = DtXml_Attr(Desc, "Name");
            if (strcmp(Desc->Name, "ParDesc") != 0 || Name == NULL ||
                strcmp(Name, ParNames[p]) != 0)
                continue;
            if (!AttrIntIn(Desc, "ParId", &ParIds[p]) || ParIds[p] < 0)
                Result = DTAPI_E_COMMUNICATION;
        }
        if (Result == DTAPI_OK && ParIds[p] < 0)
            Result = DTAPI_E_SERVICE_INCOMP;
    }
    DtXml_Free(&Answer);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The answer's value of parameter ParId, if it is of type Type; NULL otherwise.
//
static const DtXmlElem* FindValue(const DtXmlElem* Answer, int ParId, int Type)
{
    for (int i = 0; i < Answer->NumChildren; i++)
    {
        const DtXmlElem* Value = &Answer->Children[i];
        int Id = -1;
        int ValueType = -1;
        if (strcmp(Value->Name, "ParIdVal") == 0 && AttrIntIn(Value, "ParId", &Id) &&
            Id == ParId)
            return AttrIntIn(Value, "VT", &ValueType) && ValueType == Type ? Value : NULL;
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeValues -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the values out of the answer to GET_PARVALS into Values.
//
static DtapiResult TakeValues(const DtXmlElem* Answer, const int* ParIds,
                              DtPtpSlaveValues* Values)
{
    const DtXmlElem* Found[NUM_PARS];
    for (int p = 0; p < NUM_PARS; p++)
    {
        Found[p] = FindValue(Answer, ParIds[p], ParTypes[p]);
        if (Found[p] == NULL)
            return DTAPI_E_COMMUNICATION;
    }
    if (!AttrIntIn(Found[PAR_SLAVE_STATE], "VV", &Values->SlaveState) ||
        !AttrIntIn(Found[PAR_LOCK_STATUS], "VV", &Values->LockStatus) ||
        !AttrIntIn(Found[PAR_DOMAIN], "VV", &Values->Domain) ||
        !DtXml_AttrUInt(Found[PAR_MASTER_IDENTITY], "VV", &Values->MasterIdentity))
        return DTAPI_E_COMMUNICATION;
    if (Values->MasterIdentity == 0)
        return DTAPI_OK;

    // An empty string has no VV, as DTAPI writes one.
    const char* Masters = DtXml_Attr(Found[PAR_MASTER_INFO], "VV");
    return DtPtpSlave_FindMaster(Masters != NULL ? Masters : "", Values->MasterIdentity,
                                 &Values->Master, &Values->HasMasterInfo);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadValues -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the parameters with IDs ParIds in one GET_PARVALS.
//
static DtapiResult ReadValues(DtService* Service, const int* ParIds,
                              DtPtpSlaveValues* Values)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "ParIds");
    DtXmlOut_AttrInt(&Out, "Cnt", NUM_PARS);
    for (int p = 0; p < NUM_PARS; p++)
    {
        DtXmlOut_Open(&Out, "I");
        DtXmlOut_AttrInt(&Out, "Val", ParIds[p]);
        DtXmlOut_Close(&Out, "I");
    }
    DtXmlOut_Close(&Out, "ParIds");

    DtXmlElem Answer;
    DtapiResult Result = Ask(Service, DT_SERVICE_CMD_GET_PARVALS, Finish(&Out), &Answer);
    if (Result != DTAPI_OK)
        return Result;
    Result = strcmp(Answer.Name, "ParIdVals") == 0 ? TakeValues(&Answer, ParIds, Values)
                                                   : DTAPI_E_COMMUNICATION;
    DtXml_Free(&Answer);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= PTP slave +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_ExceptionResult -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtPtpSlave_ExceptionResult(int Exception)
{
    switch (Exception)
    {
    case DT_SERVICE_NO_EXCEPTION:
        return DTAPI_OK;
    case DT_PTP_EXC_INVALID_PORT_NO:
    case DT_PTP_EXC_NOT_SUPPORTED:
        return DTAPI_E_NOT_SUPPORTED;
    case DT_PTP_EXC_INCOMPATIBLE_SERVICE:
        return DTAPI_E_SERVICE_INCOMP;
    case DT_PTP_EXC_EXCLUSIVE_IN_USE:
        return DTAPI_E_IN_USE;
    case DT_PTP_EXC_DRIVER_INCOMPATIBLE:
        return DTAPI_E_DRIVER_INCOMP;
    case DT_PTP_EXC_DEVICE_NOT_FOUND:
        return DTAPI_E_NO_SUCH_DEVICE;
    default:
        return DTAPI_E_COMMUNICATION;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_FindMaster -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The list is <V Cnt="n"> with an element MI per master. The service writes a master's
// ID as a signed 64-bit integer, so an identity with its top bit set is negative.
//
DtapiResult DtPtpSlave_FindMaster(const char* Xml, uint64_t Identity, DtPtpMaster* Master,
                                  bool* Found)
{
    if (Master != NULL)
        memset(Master, 0, sizeof(*Master));
    if (Found != NULL)
        *Found = false;
    if (Xml == NULL || Master == NULL || Found == NULL)
        return DTAPI_E_INVALID_ARG;
    if (Xml[0] == '\0')
        return DTAPI_OK;

    DtXmlElem List;
    DtapiResult Result = DtXml_Parse(Xml, &List);
    if (Result != DTAPI_OK)
        return Result;
    if (strcmp(List.Name, "V") != 0)
        Result = DTAPI_E_COMMUNICATION;
    for (int i = 0; i < List.NumChildren && Result == DTAPI_OK && !*Found; i++)
    {
        const DtXmlElem* Info = &List.Children[i];
        int64_t Id = 0;
        if (strcmp(Info->Name, "MI") != 0)
            continue;
        if (!DtXml_AttrInt(Info, "ID", &Id))
        {
            Result = DTAPI_E_COMMUNICATION;
            break;
        }
        if ((uint64_t)Id != Identity)
            continue;
        Master->Identity = Identity;
        if (!AttrIntIn(Info, "DN", &Master->Domain) ||
            !DtXml_AttrBool(Info, "TT", &Master->IsTimeTraceable) ||
            !DtXml_AttrBool(Info, "FT", &Master->IsFrequencyTraceable) ||
            !AttrIntIn(Info, "CC", &Master->ClockClass) ||
            !AttrIntIn(Info, "SR", &Master->StepsRemoved))
            Result = DTAPI_E_COMMUNICATION;
        else
            *Found = true;
    }
    DtXml_Free(&List);
    if (Result != DTAPI_OK)
    {
        memset(Master, 0, sizeof(*Master));
        *Found = false;
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPtpSlave_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Connects, checks the version, attaches, finds the parameters, reads them in one go,
// detaches and disconnects. The detach is tried after a failure too.
//
DtapiResult DtPtpSlave_Read(const char* PipeName, int64_t Serial, int PortIndex,
                            DtPtpSlaveValues* Values)
{
    if (Values != NULL)
        memset(Values, 0, sizeof(*Values));
    if (PipeName == NULL || PortIndex < 0 || Values == NULL)
        return DTAPI_E_INVALID_ARG;

    DtService* Service = NULL;
    DtapiResult Result = DtService_Connect(PipeName, &Service);
    if (Result != DTAPI_OK)
        return Result;
    Result = CheckVersion(Service);
    int Id = -1;
    if (Result == DTAPI_OK)
        Result = Attach(Service, Serial, PortIndex, &Id);
    int ParIds[NUM_PARS];
    if (Result == DTAPI_OK)
        Result = FindParIds(Service, ParIds);
    if (Result == DTAPI_OK)
        Result = ReadValues(Service, ParIds, Values);
    if (Id >= 0)
        Ask(Service, DT_SERVICE_CMD_DETACH, ValElem("Id", Id), NULL);
    DtService_Close(Service);
    if (Result != DTAPI_OK)
        memset(Values, 0, sizeof(*Values));
    return Result;
}

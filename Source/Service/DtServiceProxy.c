// #*#*#*#*#*#*#*#*#*#*#*#*#* DtServiceProxy.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The proxy to DtapiService's services
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "AvFifo/DtAvError.h" // The text of the last failure.
#include "Core/DtAlloc.h"     // Allocation seam.
#include "Device/DtDevice.h"  // The device's serial number and port capabilities.
#include "DtService.h"        // The service's messages.
#include "DtServiceProxy.h"   // Interface being implemented.
#include "DtXml.h"            // Their XML.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The oldest version of the service that has the services, as in DTAPI.
#define DT_PROXY_MIN_VERSION 4

struct DtServiceProxy
{
    DtService* Service;
    int Id;                     // The ID the service gave the attachment
    DtServiceExc LastException; // Why the service refused the last call that failed
};

// The names of the service's exceptions, for the text of a failure.
static const char* const ExceptionNames[] = {
    "DeviceNotAttached",   "DeviceAlreadyAttached",
    "InvalidParId",        "InvalidPortNo",
    "ServiceAlreadyInUse", "TypeMismatch",
    "InvalidParType",      "ParOutOfRange",
    "UnknownParGroup",     "UnknownParId",
    "UnknownParName",      "UnknownService",
    "InvalidSize",         "InvalidCmd",
    "OutOfMemory",         "Unknown",
    "InvalidXmlString",    "IncompatibleService",
    "ConnectToService",    "NotImplemented",
    "ExclusiveInUse",      "UnknownGroupName",
    "NotExclusiveAccess",  "ReadOnlyPar",
    "DriverIncompatible",  "ErrorAttachingDevice",
    "DeviceNotFound",      "NotSupported",
    "NoAdapterIpAddr",     "MulticastJoin",
    "ProtocolBind",        "PtpTimestamping",
    "InvalidState",        "PtpSend",
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A new copy of Text, or NULL without memory.
//
static char* CopyText(const char* Text)
{
    size_t Size = strlen(Text) + 1;
    char* Copy = (char*)DtAlloc_Malloc(Size);
    if (Copy != NULL)
        memcpy(Copy, Text, Size);
    return Copy;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- KeepVariant -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Makes the string of *Value, which points into an answer, a copy of its own. Returns
// false without memory, and leaves *Value empty then.
//
static bool KeepVariant(DtVariant* Value)
{
    if (Value->Type != DT_VARIANT_STRING)
    {
        Value->String = NULL;
        return true;
    }
    Value->String = CopyText(Value->String);
    if (Value->String == NULL)
        memset(Value, 0, sizeof(*Value));
    return Value->String != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Fail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Records a failure of the function Where as the thread's last one, and returns Result.
//
static DtapiResult Fail(const DtServiceProxy* Proxy, DtapiResult Result,
                        const char* Where)
{
    char What[96];
    DtServiceExc Exception = Proxy != NULL ? Proxy->LastException : DT_SERVICE_EXC_NONE;
    int NumNames = (int)(sizeof(ExceptionNames) / sizeof(ExceptionNames[0]));
    if (Exception >= 0 && Exception < NumNames)
        snprintf(What, sizeof(What), "DtapiService refused: %s (%d)",
                 ExceptionNames[Exception], (int)Exception);
    else if (Exception != DT_SERVICE_EXC_NONE)
        snprintf(What, sizeof(What), "DtapiService refused: exception %d",
                 (int)Exception);
    else
        snprintf(What, sizeof(What), "%s", "the call failed");
    return DtAvError_Set(Result, Where, What);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearException -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Starts a call of the proxy: until the service refuses it, it has no exception.
//
static void ClearException(DtServiceProxy* Proxy)
{
    if (Proxy != NULL)
        Proxy->LastException = DT_SERVICE_EXC_NONE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ask -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Sends command Cmd with the XML Xml, which Ask frees, and reads the answer into Answer;
// a NULL Answer ignores it. An Xml of NULL is memory that ran out while it was written.
// An exception becomes its result, and the proxy's last exception.
//
static DtapiResult Ask(DtServiceProxy* Proxy, DtServiceCmd Cmd, char* Xml,
                       DtXmlElem* Answer)
{
    if (Answer != NULL)
        memset(Answer, 0, sizeof(*Answer));
    if (Xml == NULL)
        return DTAPI_E_OUT_OF_MEM;
    char* Text = NULL;
    int Exception = DT_SERVICE_NO_EXCEPTION;
    DtapiResult Result = DtService_Transfer(Proxy->Service, Cmd, Xml, &Text, &Exception);
    DtAlloc_Free(Xml);
    if (Result == DTAPI_OK && Exception != DT_SERVICE_NO_EXCEPTION)
    {
        Proxy->LastException = (DtServiceExc)Exception;
        Result = DtServiceProxy_ExceptionResult(Proxy->LastException);
    }
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValTextElem -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The document <Name Val="Value"/> for a text.
//
static char* ValTextElem(const char* Name, const char* Value)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, Name);
    DtXmlOut_Attr(&Out, "Val", Value);
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
    if (!DtXml_AttrInt(Elem, Name, &Wide) || Wide < INT32_MIN || Wide > INT32_MAX)
        return false;
    *Value = (int)Wide;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsElem -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether Elem is named Name.
//
static bool IsElem(const DtXmlElem* Elem, const char* Name)
{
    return Elem->Name != NULL && strcmp(Elem->Name, Name) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsValueOk -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Whether a program's *Value can be sent: of a type, with a string when it is one.
//
static bool IsValueOk(const DtVariant* Value)
{
    return Value != NULL && Value->Type > DT_VARIANT_EMPTY &&
           Value->Type <= DT_VARIANT_STRING &&
           (Value->Type != DT_VARIANT_STRING || Value->String != NULL);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static DtapiResult CheckVersion(DtServiceProxy* Proxy)
{
    DtXmlElem Answer;
    DtapiResult Result = Ask(Proxy, DT_SERVICE_CMD_VERSION, CopyText(""), &Answer);
    if (Result != DTAPI_OK)
        return Result;
    int Major = 0;
    if (!IsElem(&Answer, "Version") || !AttrIntIn(&Answer, "Major", &Major))
        Result = DTAPI_E_COMMUNICATION;
    else if (Major < DT_PROXY_MIN_VERSION)
        Result = DTAPI_E_SERVICE_INCOMP;
    DtXml_Free(&Answer);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TakeParDesc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes a description out of the element ParDesc of an answer into *Desc, which owns
// copies of its texts.
//
static DtapiResult TakeParDesc(const DtXmlElem* Elem, DtServiceParDesc* Desc)
{
    const char* Name = DtXml_Attr(Elem, "Name");
    const char* Text = DtXml_Attr(Elem, "Desc");
    int Type = 0;
    int NumEnums = 0;
    if (!IsElem(Elem, "ParDesc") || Name == NULL ||
        !AttrIntIn(Elem, "ParId", &Desc->ParId) || !AttrIntIn(Elem, "VT", &Type) ||
        !DtXml_AttrBool(Elem, "W", &Desc->IsWritable) ||
        !AttrIntIn(Elem, "ECount", &NumEnums) || NumEnums < 0 ||
        !DtService_ReadVariant(Elem, "Min", &Desc->Min) ||
        !DtService_ReadVariant(Elem, "Max", &Desc->Max) ||
        !DtService_ReadVariant(Elem, "Def", &Desc->Default))
        return DTAPI_E_COMMUNICATION;
    Desc->Type = (DtVariantType)Type;

    // The texts become copies of their own; whatever is there when memory runs out is
    // freed with the rest.
    Desc->Name = CopyText(Name);
    Desc->Description = CopyText(Text != NULL ? Text : "");
    bool Kept = KeepVariant(&Desc->Min);
    Kept = KeepVariant(&Desc->Max) && Kept;
    Kept = KeepVariant(&Desc->Default) && Kept;
    if (Desc->Name == NULL || Desc->Description == NULL || !Kept)
        return DTAPI_E_OUT_OF_MEM;
    if (NumEnums == 0)
        return DTAPI_OK;

    DtServiceEnumVal* Enums =
        (DtServiceEnumVal*)DtAlloc_Malloc((size_t)NumEnums * sizeof(DtServiceEnumVal));
    if (Enums == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Enums, 0, (size_t)NumEnums * sizeof(DtServiceEnumVal));
    Desc->EnumVals = Enums;
    Desc->NumEnumVals = NumEnums;
    int Found = 0;
    for (int i = 0; i < Elem->NumChildren && Found < NumEnums; i++)
    {
        const DtXmlElem* Item = &Elem->Children[i];
        const char* ValueName = DtXml_Attr(Item, "Idf");
        if (!IsElem(Item, "EValDesc"))
            continue;
        if (!AttrIntIn(Item, "IntVal", &Enums[Found].Value))
            return DTAPI_E_COMMUNICATION;
        Enums[Found].Name = CopyText(ValueName != NULL ? ValueName : "");
        if (Enums[Found].Name == NULL)
            return DTAPI_E_OUT_OF_MEM;
        Found++;
    }
    return Found == NumEnums ? DTAPI_OK : DTAPI_E_COMMUNICATION;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteParVals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the element ParIdVals with the NumVals values Vals.
//
static void WriteParVals(DtXmlOut* Out, const DtServiceParVal* Vals, int NumVals)
{
    DtXmlOut_Open(Out, "ParIdVals");
    DtXmlOut_AttrInt(Out, "Cnt", NumVals);
    for (int i = 0; i < NumVals; i++)
    {
        DtXmlOut_Open(Out, "ParIdVal");
        DtXmlOut_AttrInt(Out, "ParId", Vals[i].ParId);
        DtService_WriteVariant(Out, "", &Vals[i].Value);
        DtXmlOut_Close(Out, "ParIdVal");
    }
    DtXmlOut_Close(Out, "ParIdVals");
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Variants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtVariant_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The string of a variant CDTAPI filled is its own copy, which it may free, though the
// public field is const for a program's own strings.
//
void DtVariant_Clear(DtVariant* Value)
{
    if (Value == NULL)
        return;
    DtAlloc_Free((void*)(uintptr_t)Value->String);
    memset(Value, 0, sizeof(*Value));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Proxy +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_Attach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The port is checked here, before a connection: that the device has it, and, for the
// PTP clock slave, that it has PTP.
//
DtapiResult DtServiceProxy_Attach(const DtDevice* Device, int Port, DtServiceType Service,
                                  bool Exclusive, DtServiceProxy** Proxy)
{
    static const char Where[] = "DtServiceProxy_Attach";
    if (Proxy != NULL)
        *Proxy = NULL;
    if (Proxy == NULL || Service != DT_SERVICE_PTP_CLOCK_SLAVE)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);
    if (Device == NULL || Device->Drv == NULL)
        return Fail(NULL, DTAPI_E_DEVICE, Where);
    if (Port < 1 || Port > Device->NumPublicPorts)
        return Fail(NULL, DTAPI_E_NO_SUCH_PORT, Where);
    if ((Device->PortCaps[Port - 1] & DT_CAP_PTP) == 0)
        return Fail(NULL, DTAPI_E_NOT_SUPPORTED, Where);
    return DtServiceProxy_AttachTo(NULL, Device->Info.Serial, Port - 1, Service,
                                   Exclusive, Proxy);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_AttachTo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Connects, checks the service's version and attaches; the PTP clock slave is the
// service's first, 0.
//
DtapiResult DtServiceProxy_AttachTo(const char* PipeName, int64_t Serial, int PortIndex,
                                    DtServiceType Service, bool Exclusive,
                                    DtServiceProxy** Proxy)
{
    static const char Where[] = "DtServiceProxy_Attach";
    if (Proxy != NULL)
        *Proxy = NULL;
    if (Proxy == NULL || PortIndex < 0 || Service != DT_SERVICE_PTP_CLOCK_SLAVE)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);

    DtServiceProxy* New = (DtServiceProxy*)DtAlloc_Malloc(sizeof(DtServiceProxy));
    if (New == NULL)
        return Fail(NULL, DTAPI_E_OUT_OF_MEM, Where);
    New->Id = -1;
    New->LastException = DT_SERVICE_EXC_NONE;
    DtapiResult Result = DtService_Connect(PipeName, &New->Service);
    if (Result == DTAPI_OK)
        Result = CheckVersion(New);

    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "Attach");
    DtXmlOut_AttrInt(&Out, "Serial", Serial);
    DtXmlOut_AttrInt(&Out, "Port", PortIndex);
    DtXmlOut_AttrInt(&Out, "Instance", 0);
    DtXmlOut_AttrInt(&Out, "ServiceType", (int)Service - 1);
    DtXmlOut_AttrBool(&Out, "Exclusive", Exclusive);
    DtXmlOut_Close(&Out, "Attach");
    char* Xml = Finish(&Out);
    DtXmlElem Answer;
    memset(&Answer, 0, sizeof(Answer));
    if (Result == DTAPI_OK)
        Result = Ask(New, DT_SERVICE_CMD_ATTACH, Xml, &Answer);
    else
        DtAlloc_Free(Xml);
    if (Result == DTAPI_OK &&
        (!IsElem(&Answer, "Id") || !AttrIntIn(&Answer, "Val", &New->Id) || New->Id < 0))
        Result = DTAPI_E_COMMUNICATION;
    DtXml_Free(&Answer);
    if (Result != DTAPI_OK)
    {
        Fail(New, Result, Where);
        New->Id = -1;
        DtServiceProxy_Detach(New);
        return Result;
    }
    *Proxy = New;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtServiceProxy_Detach(DtServiceProxy* Proxy)
{
    if (Proxy == NULL)
        return;
    if (Proxy->Id >= 0)
        Ask(Proxy, DT_SERVICE_CMD_DETACH, ValElem("Id", Proxy->Id), NULL);
    DtService_Close(Proxy->Service);
    DtAlloc_Free(Proxy);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_ExceptionResult -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtServiceProxy_ExceptionResult(DtServiceExc Exception)
{
    switch (Exception)
    {
    case DT_SERVICE_EXC_NONE:
        return DTAPI_OK;
    case DT_SERVICE_EXC_INVALID_PORT_NO:
    case DT_SERVICE_EXC_UNKNOWN_SERVICE:
    case DT_SERVICE_EXC_NOT_SUPPORTED:
        return DTAPI_E_NOT_SUPPORTED;
    case DT_SERVICE_EXC_DEVICE_NOT_FOUND:
        return DTAPI_E_NO_SUCH_DEVICE;
    case DT_SERVICE_EXC_DRIVER_INCOMPATIBLE:
        return DTAPI_E_DRIVER_INCOMP;
    case DT_SERVICE_EXC_INCOMPATIBLE_SERVICE:
        return DTAPI_E_SERVICE_INCOMP;
    case DT_SERVICE_EXC_EXCLUSIVE_IN_USE:
    case DT_SERVICE_EXC_NOT_EXCLUSIVE_ACCESS:
        return DTAPI_E_IN_USE;
    case DT_SERVICE_EXC_INVALID_PAR_ID:
    case DT_SERVICE_EXC_UNKNOWN_PAR_GROUP:
    case DT_SERVICE_EXC_UNKNOWN_PAR_ID:
    case DT_SERVICE_EXC_UNKNOWN_PAR_NAME:
    case DT_SERVICE_EXC_UNKNOWN_GROUP_NAME:
        return DTAPI_E_NOT_FOUND;
    case DT_SERVICE_EXC_TYPE_MISMATCH:
    case DT_SERVICE_EXC_INVALID_PAR_TYPE:
    case DT_SERVICE_EXC_PAR_OUT_OF_RANGE:
    case DT_SERVICE_EXC_READ_ONLY_PAR:
        return DTAPI_E_INVALID_ARG;
    default:
        return DTAPI_E_COMMUNICATION;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_FindParId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtServiceProxy_FindParId(const DtServiceParDesc* Descs, int NumDescs,
                             const char* Name)
{
    if (Descs == NULL || Name == NULL)
        return -1;
    for (int i = 0; i < NumDescs; i++)
    {
        if (Descs[i].Name != NULL && strcmp(Descs[i].Name, Name) == 0)
            return Descs[i].ParId;
    }
    return -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_FreeParDescs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtServiceProxy_FreeParDescs(DtServiceParDesc* Descs, int NumDescs)
{
    if (Descs == NULL)
        return;
    for (int i = 0; i < NumDescs; i++)
    {
        DtServiceParDesc* Desc = &Descs[i];
        DtAlloc_Free((void*)(uintptr_t)Desc->Name);
        DtAlloc_Free((void*)(uintptr_t)Desc->Description);
        DtVariant_Clear(&Desc->Min);
        DtVariant_Clear(&Desc->Max);
        DtVariant_Clear(&Desc->Default);
        for (int j = 0; Desc->EnumVals != NULL && j < Desc->NumEnumVals; j++)
            DtAlloc_Free((void*)(uintptr_t)Desc->EnumVals[j].Name);
        DtAlloc_Free((void*)(uintptr_t)Desc->EnumVals);
    }
    DtAlloc_Free(Descs);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_FreeParGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtServiceProxy_FreeParGroups(char** Groups, int NumGroups)
{
    if (Groups == NULL)
        return;
    for (int i = 0; i < NumGroups; i++)
        DtAlloc_Free(Groups[i]);
    DtAlloc_Free(Groups);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_GetParDescs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtServiceProxy_GetParDescs(DtServiceProxy* Proxy, const char* Group,
                                       DtServiceParDesc** Descs, int* NumDescs)
{
    static const char Where[] = "DtServiceProxy_GetParDescs";
    ClearException(Proxy);
    if (Descs != NULL)
        *Descs = NULL;
    if (NumDescs != NULL)
        *NumDescs = 0;
    if (Proxy == NULL || Group == NULL || Descs == NULL || NumDescs == NULL)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);

    DtXmlElem Answer;
    DtapiResult Result =
        Ask(Proxy, DT_SERVICE_CMD_GET_PARDESCS, ValTextElem("GroupName", Group), &Answer);
    if (Result != DTAPI_OK)
        return Fail(Proxy, Result, Where);
    DtServiceParDesc* List = NULL;
    int Count = 0;
    if (!IsElem(&Answer, "ParDescs"))
        Result = DTAPI_E_COMMUNICATION;
    else if (Answer.NumChildren > 0)
    {
        List = (DtServiceParDesc*)DtAlloc_Malloc((size_t)Answer.NumChildren *
                                                 sizeof(DtServiceParDesc));
        if (List == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }
    for (int i = 0; i < Answer.NumChildren && Result == DTAPI_OK; i++)
    {
        memset(&List[Count], 0, sizeof(DtServiceParDesc));
        Result = TakeParDesc(&Answer.Children[i], &List[Count]);
        Count++;
    }
    DtXml_Free(&Answer);
    if (Result != DTAPI_OK)
    {
        DtServiceProxy_FreeParDescs(List, Count);
        return Fail(Proxy, Result, Where);
    }
    *Descs = List;
    *NumDescs = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_GetParGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtServiceProxy_GetParGroups(DtServiceProxy* Proxy, char*** Groups,
                                        int* NumGroups)
{
    static const char Where[] = "DtServiceProxy_GetParGroups";
    ClearException(Proxy);
    if (Groups != NULL)
        *Groups = NULL;
    if (NumGroups != NULL)
        *NumGroups = 0;
    if (Proxy == NULL || Groups == NULL || NumGroups == NULL)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);

    DtXmlElem Answer;
    DtapiResult Result = Ask(Proxy, DT_SERVICE_CMD_GET_PARGROUPS, CopyText(""), &Answer);
    if (Result != DTAPI_OK)
        return Fail(Proxy, Result, Where);
    char** List = NULL;
    int Count = 0;
    if (!IsElem(&Answer, "ParGroups"))
        Result = DTAPI_E_COMMUNICATION;
    else if (Answer.NumChildren > 0)
    {
        List = (char**)DtAlloc_Malloc((size_t)Answer.NumChildren * sizeof(char*));
        if (List == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
    }
    for (int i = 0; i < Answer.NumChildren && Result == DTAPI_OK; i++)
    {
        const char* Name = DtXml_Attr(&Answer.Children[i], "Val");
        if (!IsElem(&Answer.Children[i], "S") || Name == NULL)
            Result = DTAPI_E_COMMUNICATION;
        else if ((List[Count] = CopyText(Name)) == NULL)
            Result = DTAPI_E_OUT_OF_MEM;
        else
            Count++;
    }
    DtXml_Free(&Answer);
    if (Result != DTAPI_OK)
    {
        DtServiceProxy_FreeParGroups(List, Count);
        return Fail(Proxy, Result, Where);
    }
    *Groups = List;
    *NumGroups = Count;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_GetParVal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtServiceProxy_GetParVal(DtServiceProxy* Proxy, int ParId, DtVariant* Value)
{
    static const char Where[] = "DtServiceProxy_GetParVal";
    ClearException(Proxy);
    if (Value != NULL)
        memset(Value, 0, sizeof(*Value));
    if (Proxy == NULL || Value == NULL)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);

    DtXmlElem Answer;
    DtapiResult Result =
        Ask(Proxy, DT_SERVICE_CMD_GET_PARVAL, ValElem("ParId", ParId), &Answer);
    if (Result != DTAPI_OK)
        return Fail(Proxy, Result, Where);
    if (!IsElem(&Answer, "ParVal") || !DtService_ReadVariant(&Answer, "", Value))
        Result = DTAPI_E_COMMUNICATION;
    else if (!KeepVariant(Value))
        Result = DTAPI_E_OUT_OF_MEM;
    DtXml_Free(&Answer);
    if (Result != DTAPI_OK)
    {
        memset(Value, 0, sizeof(*Value));
        return Fail(Proxy, Result, Where);
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_GetParVals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The service answers the values in the order they were asked for.
//
DtapiResult DtServiceProxy_GetParVals(DtServiceProxy* Proxy, DtServiceParVal* Vals,
                                      int NumVals)
{
    static const char Where[] = "DtServiceProxy_GetParVals";
    ClearException(Proxy);
    if (Proxy == NULL || (Vals == NULL && NumVals > 0) || NumVals < 0)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);
    for (int i = 0; i < NumVals; i++)
        memset(&Vals[i].Value, 0, sizeof(Vals[i].Value));

    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "ParIds");
    DtXmlOut_AttrInt(&Out, "Cnt", NumVals);
    for (int i = 0; i < NumVals; i++)
    {
        DtXmlOut_Open(&Out, "I");
        DtXmlOut_AttrInt(&Out, "Val", Vals[i].ParId);
        DtXmlOut_Close(&Out, "I");
    }
    DtXmlOut_Close(&Out, "ParIds");
    DtXmlElem Answer;
    DtapiResult Result = Ask(Proxy, DT_SERVICE_CMD_GET_PARVALS, Finish(&Out), &Answer);
    if (Result != DTAPI_OK)
        return Fail(Proxy, Result, Where);
    if (!IsElem(&Answer, "ParIdVals") || Answer.NumChildren != NumVals)
        Result = DTAPI_E_COMMUNICATION;
    for (int i = 0; i < NumVals && Result == DTAPI_OK; i++)
    {
        const DtXmlElem* Item = &Answer.Children[i];
        int ParId = -1;
        if (!IsElem(Item, "ParIdVal") || !AttrIntIn(Item, "ParId", &ParId) ||
            ParId != Vals[i].ParId || !DtService_ReadVariant(Item, "", &Vals[i].Value))
            Result = DTAPI_E_COMMUNICATION;
        else if (!KeepVariant(&Vals[i].Value))
            Result = DTAPI_E_OUT_OF_MEM;
    }
    DtXml_Free(&Answer);
    if (Result != DTAPI_OK)
    {
        // Every string read was copied before the next value was read, or the variant
        // emptied, so each value can be cleared.
        for (int i = 0; i < NumVals; i++)
            DtVariant_Clear(&Vals[i].Value);
        return Fail(Proxy, Result, Where);
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_LastException -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtServiceExc DtServiceProxy_LastException(const DtServiceProxy* Proxy)
{
    return Proxy != NULL ? Proxy->LastException : DT_SERVICE_EXC_NONE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_SaveSettings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtServiceProxy_SaveSettings(DtServiceProxy* Proxy)
{
    static const char Where[] = "DtServiceProxy_SaveSettings";
    ClearException(Proxy);
    if (Proxy == NULL)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);
    DtapiResult Result =
        Ask(Proxy, DT_SERVICE_CMD_SAVE_SETTINGS, ValElem("Id", Proxy->Id), NULL);
    return Result == DTAPI_OK ? DTAPI_OK : Fail(Proxy, Result, Where);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_SetParVal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtServiceProxy_SetParVal(DtServiceProxy* Proxy, int ParId,
                                     const DtVariant* Value)
{
    static const char Where[] = "DtServiceProxy_SetParVal";
    ClearException(Proxy);
    if (Proxy == NULL || !IsValueOk(Value))
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);

    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "SetParVal");
    DtXmlOut_AttrInt(&Out, "Id", Proxy->Id);
    DtXmlOut_AttrInt(&Out, "ParId", ParId);
    DtService_WriteVariant(&Out, "", Value);
    DtXmlOut_Close(&Out, "SetParVal");
    DtapiResult Result = Ask(Proxy, DT_SERVICE_CMD_SET_PARVAL, Finish(&Out), NULL);
    return Result == DTAPI_OK ? DTAPI_OK : Fail(Proxy, Result, Where);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtServiceProxy_SetParVals -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtServiceProxy_SetParVals(DtServiceProxy* Proxy, const DtServiceParVal* Vals,
                                      int NumVals)
{
    static const char Where[] = "DtServiceProxy_SetParVals";
    ClearException(Proxy);
    if (Proxy == NULL || (Vals == NULL && NumVals > 0) || NumVals < 0)
        return Fail(NULL, DTAPI_E_INVALID_ARG, Where);
    for (int i = 0; i < NumVals; i++)
    {
        if (!IsValueOk(&Vals[i].Value))
            return Fail(NULL, DTAPI_E_INVALID_ARG, Where);
    }

    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "SetParVals");
    DtXmlOut_AttrInt(&Out, "Id", Proxy->Id);
    WriteParVals(&Out, Vals, NumVals);
    DtXmlOut_Close(&Out, "SetParVals");
    DtapiResult Result = Ask(Proxy, DT_SERVICE_CMD_SET_PARVALS, Finish(&Out), NULL);
    return Result == DTAPI_OK ? DTAPI_OK : Fail(Proxy, Result, Where);
}

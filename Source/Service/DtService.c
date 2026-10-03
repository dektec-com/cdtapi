// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtService.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Messages to and from DtapiService through its services pipe
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "DtService.h"          // Interface being implemented.
#include "OAL/OsBackend.h"      // Whether the emulator is asked for.
#include "OAL/OsPipe.h"         // The pipe.
#include "OAL/Sim/SimService.h" // The emulated service.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// How long the service may take to accept a connection.
#define DT_SERVICE_CONNECT_MS 2000

// How long the service may take to start its answer, and to take a message.
#define DT_SERVICE_ANSWER_MS 5000

// How long the rest of a message may take once its length has come, as in DTAPI.
#define DT_SERVICE_BODY_MS 500

// The longest answer accepted. The service's answers are a few kilobytes; a longer
// length means the stream is out of step.
#define DT_SERVICE_MAX_MSG (16u * 1024u * 1024u)

// The sizes of a command's header and of an answer's.
#define DT_SERVICE_CMD_HEADER 4
#define DT_SERVICE_RESULT_HEADER 8

struct DtService
{
    OsPipe* Pipe;    // The pipe, or NULL for the emulated service
    SimService* Sim; // The emulated service, or NULL for the pipe
    bool Failed;     // A transfer failed: the stream may be out of step
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetLe32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t GetLe32(const uint8_t* Bytes)
{
    return (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8 | (uint32_t)Bytes[2] << 16 |
           (uint32_t)Bytes[3] << 24;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutLe32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PutLe32(uint8_t* Bytes, uint32_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
    Bytes[2] = (uint8_t)(Value >> 16);
    Bytes[3] = (uint8_t)(Value >> 24);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetUnit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The code unit of CharSize bytes, little endian, at Bytes.
//
static uint32_t GetUnit(const uint8_t* Bytes, int CharSize)
{
    return CharSize == 2 ? (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8 : GetLe32(Bytes);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutUnit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the code unit Unit in CharSize bytes, little endian, at Bytes, and returns the
// byte after it.
//
static uint8_t* PutUnit(uint8_t* Bytes, uint32_t Unit, int CharSize)
{
    if (CharSize == 2)
    {
        Bytes[0] = (uint8_t)Unit;
        Bytes[1] = (uint8_t)(Unit >> 8);
        return Bytes + 2;
    }
    PutLe32(Bytes, Unit);
    return Bytes + 4;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsScalar -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether Code is a Unicode scalar value: a code point that is not a surrogate.
//
static bool IsScalar(uint32_t Code)
{
    return Code <= 0x10FFFF && (Code < 0xD800 || Code > 0xDFFF);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DecodeUtf8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads one character of UTF-8 at *Text into *Code and moves *Text past it. Returns
// false for bytes that are not UTF-8, such as an overlong form or a surrogate.
//
static bool DecodeUtf8(const uint8_t** Text, uint32_t* Code)
{
    const uint8_t* Next = *Text;
    uint32_t Lead = *Next++;
    int More = Lead < 0x80   ? 0
               : Lead < 0xC2 ? -1
               : Lead < 0xE0 ? 1
               : Lead < 0xF0 ? 2
               : Lead < 0xF5 ? 3
                             : -1;
    if (More < 0)
        return false;
    static const uint32_t Least[4] = {0, 0x80, 0x800, 0x10000};
    uint32_t Value = More == 0 ? Lead : Lead & (0x3Fu >> More);
    for (int i = 0; i < More; i++)
    {
        if ((*Next & 0xC0) != 0x80)
            return false;
        Value = Value << 6 | (*Next++ & 0x3Fu);
    }
    if (Value < Least[More] || !IsScalar(Value))
        return false;
    *Code = Value;
    *Text = Next;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeUtf8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Writes the scalar value Code as UTF-8 at Text, and returns the byte after it.
//
static char* EncodeUtf8(char* Text, uint32_t Code)
{
    if (Code < 0x80)
    {
        *Text++ = (char)Code;
    }
    else if (Code < 0x800)
    {
        *Text++ = (char)(0xC0 | Code >> 6);
        *Text++ = (char)(0x80 | (Code & 0x3F));
    }
    else if (Code < 0x10000)
    {
        *Text++ = (char)(0xE0 | Code >> 12);
        *Text++ = (char)(0x80 | (Code >> 6 & 0x3F));
        *Text++ = (char)(0x80 | (Code & 0x3F));
    }
    else
    {
        *Text++ = (char)(0xF0 | Code >> 18);
        *Text++ = (char)(0x80 | (Code >> 12 & 0x3F));
        *Text++ = (char)(0x80 | (Code >> 6 & 0x3F));
        *Text++ = (char)(0x80 | (Code & 0x3F));
    }
    return Text;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PipeResult -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The result for an OS_PIPE_ outcome other than OS_PIPE_OK.
//
static DtapiResult PipeResult(int Outcome)
{
    return Outcome == OS_PIPE_NO_MEMORY ? DTAPI_E_OUT_OF_MEM : DTAPI_E_CONNECT_TO_SERVICE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReceiveMsg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads one message into a new *Msg of *Size bytes, to be freed with DtAlloc_Free.
//
static DtapiResult ReceiveMsg(DtService* Service, uint8_t** Msg, size_t* Size)
{
    uint8_t Prefix[8];
    int Outcome = OsPipe_Read(Service->Pipe, Prefix, 2, DT_SERVICE_ANSWER_MS);
    if (Outcome != OS_PIPE_OK)
        return PipeResult(Outcome);
    uint32_t Length = (uint32_t)Prefix[0] | (uint32_t)Prefix[1] << 8;
    if (Length == 0xFFFF)
    {
        Outcome = OsPipe_Read(Service->Pipe, Prefix + 2, 6, DT_SERVICE_BODY_MS);
        if (Outcome != OS_PIPE_OK)
            return PipeResult(Outcome);
        Length = GetLe32(Prefix + 4);
    }
    if (Length > DT_SERVICE_MAX_MSG)
        return DTAPI_E_COMMUNICATION;

    uint8_t* Bytes = (uint8_t*)DtAlloc_Malloc(Length > 0 ? Length : 1);
    if (Bytes == NULL)
        return DTAPI_E_OUT_OF_MEM;
    Outcome = OsPipe_Read(Service->Pipe, Bytes, Length, DT_SERVICE_BODY_MS);
    if (Outcome != OS_PIPE_OK)
    {
        DtAlloc_Free(Bytes);
        return PipeResult(Outcome);
    }
    *Msg = Bytes;
    *Size = Length;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SendMsg -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the length of Msg, then Msg, in two writes.
//
static DtapiResult SendMsg(DtService* Service, const uint8_t* Msg, size_t Size)
{
    if (Size > UINT32_MAX)
        return DTAPI_E_INVALID_ARG;
    uint8_t Prefix[8];
    size_t PrefixSize = DtService_EncodeLength((uint32_t)Size, Prefix);
    int Outcome = OsPipe_Write(Service->Pipe, Prefix, PrefixSize, DT_SERVICE_ANSWER_MS);
    if (Outcome == OS_PIPE_OK)
        Outcome = OsPipe_Write(Service->Pipe, Msg, Size, DT_SERVICE_BODY_MS);
    return Outcome == OS_PIPE_OK ? DTAPI_OK : PipeResult(Outcome);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseAnswer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Takes the answer to command Cmd apart into its exception or its XML.
//
static DtapiResult ParseAnswer(const uint8_t* Answer, size_t Size, DtServiceCmd Cmd,
                               char** ResultXml, int* Exception)
{
    if (Size < DT_SERVICE_RESULT_HEADER || GetLe32(Answer) != (uint32_t)Cmd)
        return DTAPI_E_COMMUNICATION;
    int32_t Reason = (int32_t)GetLe32(Answer + 4);
    if (Reason != DT_SERVICE_NO_EXCEPTION)
    {
        // The service answers an exception with the header alone.
        *Exception = (int)Reason;
        return DTAPI_OK;
    }
    return DtService_TextFromWire(Answer + DT_SERVICE_RESULT_HEADER,
                                  Size - DT_SERVICE_RESULT_HEADER, DT_SERVICE_CHAR_SIZE,
                                  ResultXml);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Service +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtService_Close(DtService* Service)
{
    if (Service == NULL)
        return;
    if (!Service->Failed && Service->Pipe != NULL)
    {
        uint8_t Cleanup[DT_SERVICE_CMD_HEADER];
        PutLe32(Cleanup, DT_SERVICE_CMD_CLEANUP_CONNECTION);
        SendMsg(Service, Cleanup, sizeof(Cleanup));
    }
    OsPipe_Close(Service->Pipe);
    SimService_Close(Service->Sim);
    DtAlloc_Free(Service);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_Connect -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtService_Connect(const char* PipeName, DtService** Service)
{
    if (Service != NULL)
        *Service = NULL;
    if (Service == NULL)
        return DTAPI_E_INVALID_ARG;

    DtService* New = (DtService*)DtAlloc_Malloc(sizeof(DtService));
    if (New == NULL)
        return DTAPI_E_OUT_OF_MEM;
    New->Failed = false;
    New->Pipe = NULL;
    New->Sim = NULL;
    if (PipeName == NULL && OsSim_IsRequested())
    {
        New->Sim = SimService_Connect();
        if (New->Sim == NULL)
        {
            DtAlloc_Free(New);
            return DTAPI_E_OUT_OF_MEM;
        }
        *Service = New;
        return DTAPI_OK;
    }
    int Outcome = OsPipe_Connect(PipeName != NULL ? PipeName : DT_SERVICE_PIPE_NAME,
                                 DT_SERVICE_CONNECT_MS, &New->Pipe);
    if (Outcome != OS_PIPE_OK)
    {
        DtAlloc_Free(New);
        return PipeResult(Outcome);
    }
    *Service = New;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_EncodeLength -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtService_EncodeLength(uint32_t Length, uint8_t* Prefix)
{
    if (Length < 0xFFFF)
    {
        Prefix[0] = (uint8_t)Length;
        Prefix[1] = (uint8_t)(Length >> 8);
        return 2;
    }
    Prefix[0] = 0xFF;
    Prefix[1] = 0xFF;
    Prefix[2] = 0;
    Prefix[3] = 0;
    PutLe32(Prefix + 4, Length);
    return 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_ReadVariant -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtService_ReadVariant(const DtXmlElem* Elem, const char* Prefix, DtVariant* Value)
{
    if (Value != NULL)
        memset(Value, 0, sizeof(*Value));
    if (Elem == NULL || Prefix == NULL || Value == NULL || strlen(Prefix) > 16)
        return false;
    char TypeName[24];
    char ValueName[24];
    snprintf(TypeName, sizeof(TypeName), "%sVT", Prefix);
    snprintf(ValueName, sizeof(ValueName), "%sVV", Prefix);
    int64_t Type = -1;
    int64_t Int = 0;
    if (!DtXml_AttrInt(Elem, TypeName, &Type))
        return false;
    bool Read = false;
    switch (Type)
    {
    case DT_VARIANT_EMPTY:
        Read = true;
        break;
    case DT_VARIANT_DOUBLE:
        Read = DtXml_AttrDouble(Elem, ValueName, &Value->Double);
        break;
    case DT_VARIANT_INT:
        Read =
            DtXml_AttrInt(Elem, ValueName, &Int) && Int >= INT32_MIN && Int <= INT32_MAX;
        Value->Int = (int)Int;
        break;
    case DT_VARIANT_UINT64:
        Read = DtXml_AttrUInt(Elem, ValueName, &Value->UInt64);
        break;
    case DT_VARIANT_BOOL:
        Read = DtXml_AttrBool(Elem, ValueName, &Value->Bool);
        break;
    case DT_VARIANT_STRING:
        Value->String = DtXml_Attr(Elem, ValueName);
        if (Value->String == NULL)
            Value->String = "";
        Read = true;
        break;
    default:
        break;
    }
    if (!Read)
    {
        memset(Value, 0, sizeof(*Value));
        return false;
    }
    Value->Type = (DtVariantType)Type;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_TextFromWire -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A UTF-16 character above U+FFFF is a pair of surrogates; a surrogate on its own, or
// in UTF-32, is not text.
//
DtapiResult DtService_TextFromWire(const uint8_t* Wire, size_t WireSize, int CharSize,
                                   char** Text)
{
    if (Text != NULL)
        *Text = NULL;
    if (Wire == NULL || Text == NULL || (CharSize != 2 && CharSize != 4))
        return DTAPI_E_INVALID_ARG;
    size_t Units = WireSize / (size_t)CharSize;
    if (WireSize % (size_t)CharSize != 0 || Units == 0 ||
        GetUnit(Wire + WireSize - (size_t)CharSize, CharSize) != 0)
        return DTAPI_E_COMMUNICATION;

    // A unit gives at most 3 bytes of UTF-8 in UTF-16, and a pair 4; 4 in UTF-32.
    char* Out = (char*)DtAlloc_Malloc(Units * 4 + 1);
    if (Out == NULL)
        return DTAPI_E_OUT_OF_MEM;
    char* Next = Out;
    for (size_t i = 0; i < Units; i++)
    {
        uint32_t Code = GetUnit(Wire + i * (size_t)CharSize, CharSize);
        if (Code == 0)
            break;
        if (CharSize == 2 && Code >= 0xD800 && Code <= 0xDBFF && i + 1 < Units)
        {
            uint32_t Low = GetUnit(Wire + (i + 1) * 2, 2);
            if (Low >= 0xDC00 && Low <= 0xDFFF)
            {
                Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
                i++;
            }
        }
        if (!IsScalar(Code))
        {
            DtAlloc_Free(Out);
            return DTAPI_E_COMMUNICATION;
        }
        Next = EncodeUtf8(Next, Code);
    }
    *Next = '\0';
    *Text = Out;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_TextToWire -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtService_TextToWire(const char* Text, int CharSize, uint8_t** Wire,
                                 size_t* WireSize)
{
    if (Wire != NULL)
        *Wire = NULL;
    if (WireSize != NULL)
        *WireSize = 0;
    if (Text == NULL || Wire == NULL || WireSize == NULL ||
        (CharSize != 2 && CharSize != 4))
        return DTAPI_E_INVALID_ARG;

    // A byte of UTF-8 gives at most one unit: a character in a pair of UTF-16 units
    // takes four bytes.
    size_t Length = strlen(Text);
    uint8_t* Out = (uint8_t*)DtAlloc_Malloc((Length + 1) * (size_t)CharSize);
    if (Out == NULL)
        return DTAPI_E_OUT_OF_MEM;
    uint8_t* Next = Out;
    const uint8_t* In = (const uint8_t*)Text;
    while (*In != 0)
    {
        uint32_t Code = 0;
        if (!DecodeUtf8(&In, &Code))
        {
            DtAlloc_Free(Out);
            return DTAPI_E_INVALID_ARG;
        }
        if (CharSize == 2 && Code >= 0x10000)
        {
            Next = PutUnit(Next, 0xD800 + ((Code - 0x10000) >> 10), 2);
            Next = PutUnit(Next, 0xDC00 + ((Code - 0x10000) & 0x3FF), 2);
        }
        else
        {
            Next = PutUnit(Next, Code, CharSize);
        }
    }
    Next = PutUnit(Next, 0, CharSize);
    *Wire = Out;
    *WireSize = (size_t)(Next - Out);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_Transfer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtService_Transfer(DtService* Service, DtServiceCmd Cmd, const char* Xml,
                               char** ResultXml, int* Exception)
{
    if (ResultXml != NULL)
        *ResultXml = NULL;
    if (Exception != NULL)
        *Exception = DT_SERVICE_NO_EXCEPTION;
    if (Service == NULL || Xml == NULL || ResultXml == NULL || Exception == NULL ||
        Cmd == DT_SERVICE_CMD_CLEANUP_CONNECTION)
        return DTAPI_E_INVALID_ARG;
    if (Service->Failed)
        return DTAPI_E_CONNECT_TO_SERVICE;

    uint8_t* Wire = NULL;
    size_t WireSize = 0;
    DtapiResult Result =
        DtService_TextToWire(Xml, DT_SERVICE_CHAR_SIZE, &Wire, &WireSize);
    if (Result != DTAPI_OK)
        return Result;
    uint8_t* Msg = (uint8_t*)DtAlloc_Malloc(DT_SERVICE_CMD_HEADER + WireSize);
    if (Msg == NULL)
    {
        DtAlloc_Free(Wire);
        return DTAPI_E_OUT_OF_MEM;
    }
    PutLe32(Msg, (uint32_t)Cmd);
    memcpy(Msg + DT_SERVICE_CMD_HEADER, Wire, WireSize);
    DtAlloc_Free(Wire);

    uint8_t* Answer = NULL;
    size_t AnswerSize = 0;
    if (Service->Sim != NULL)
    {
        Result = SimService_Transfer(Service->Sim, Msg, DT_SERVICE_CMD_HEADER + WireSize,
                                     &Answer, &AnswerSize)
                     ? DTAPI_OK
                     : DTAPI_E_OUT_OF_MEM;
        DtAlloc_Free(Msg);
    }
    else
    {
        Result = SendMsg(Service, Msg, DT_SERVICE_CMD_HEADER + WireSize);
        DtAlloc_Free(Msg);
        if (Result == DTAPI_OK)
            Result = ReceiveMsg(Service, &Answer, &AnswerSize);
    }
    if (Result == DTAPI_OK)
        Result = ParseAnswer(Answer, AnswerSize, Cmd, ResultXml, Exception);
    DtAlloc_Free(Answer);

    // Once the command is on its way, any failure, even of memory for the answer, may
    // leave part of a message in the pipe.
    if (Result != DTAPI_OK)
        Service->Failed = true;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtService_WriteVariant -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtService_WriteVariant(DtXmlOut* Out, const char* Prefix, const DtVariant* Value)
{
    if (Out == NULL || Prefix == NULL || Value == NULL || strlen(Prefix) > 16)
        return;
    char TypeName[24];
    char ValueName[24];
    char Number[24];
    snprintf(TypeName, sizeof(TypeName), "%sVT", Prefix);
    snprintf(ValueName, sizeof(ValueName), "%sVV", Prefix);
    DtXmlOut_AttrInt(Out, TypeName, Value->Type);
    switch (Value->Type)
    {
    case DT_VARIANT_DOUBLE:
        DtXmlOut_AttrDouble(Out, ValueName, Value->Double);
        break;
    case DT_VARIANT_INT:
        DtXmlOut_AttrInt(Out, ValueName, Value->Int);
        break;
    case DT_VARIANT_UINT64:
        snprintf(Number, sizeof(Number), "%" PRIu64, Value->UInt64);
        DtXmlOut_Attr(Out, ValueName, Number);
        break;
    case DT_VARIANT_BOOL:
        DtXmlOut_AttrBool(Out, ValueName, Value->Bool);
        break;
    case DT_VARIANT_STRING:
        DtXmlOut_Attr(Out, ValueName, Value->String != NULL ? Value->String : "");
        break;
    default:
        break;
    }
}

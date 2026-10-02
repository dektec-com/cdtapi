// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtXml.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The XML of DtapiService's messages: elements with attributes
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "cdtapi.h" // DtapiResult.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reader +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Reads a document of elements with attributes, which is all DtapiService writes. Text
// between elements, an XML declaration and comments are skipped. Attribute values are
// decoded: the five named entities and character references become their characters.
// Text is UTF-8.
//

typedef struct DtXmlAttr
{
    char* Name;
    char* Value;
} DtXmlAttr;

typedef struct DtXmlElem
{
    char* Name;
    DtXmlAttr* Attrs;
    int NumAttrs;
    struct DtXmlElem* Children;
    int NumChildren;
} DtXmlElem;

// Returns the value of the attribute Name of Elem, or NULL when it has none.
const char* DtXml_Attr(const DtXmlElem* Elem, const char* Name);

// Reads the attribute Name of Elem as true or false, as DtapiService writes a bool, also
// in capitals. Returns false when it is absent or something else.
bool DtXml_AttrBool(const DtXmlElem* Elem, const char* Name, bool* Value);

// Reads the attribute Name of Elem as a decimal integer, with a minus sign or without.
// Returns false when it is absent, not such a number, or outside the range of int64_t.
bool DtXml_AttrInt(const DtXmlElem* Elem, const char* Name, int64_t* Value);

// Reads the attribute Name of Elem as a decimal integer without a sign, up to
// UINT64_MAX. Returns false when it is absent or not such a number.
bool DtXml_AttrUInt(const DtXmlElem* Elem, const char* Name, uint64_t* Value);

// Returns the first child of Elem named Name, or NULL when it has none.
const DtXmlElem* DtXml_Child(const DtXmlElem* Elem, const char* Name);

// Frees what DtXml_Parse filled Root with, and empties it. A NULL Root does nothing.
void DtXml_Free(DtXmlElem* Root);

// Reads the document Text into Root, its root element and everything in it. Root is
// empty after a failure.
// Returns:
//   DTAPI_OK
//   DTAPI_E_COMMUNICATION  Text is not such a document, or nests more than 32 deep
//   DTAPI_E_INVALID_ARG    a NULL argument
//   DTAPI_E_OUT_OF_MEM
DtapiResult DtXml_Parse(const char* Text, DtXmlElem* Root);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Writes a document of elements with attributes, as DtapiService reads it. An element
// is opened, given its attributes, given its children, and closed; one without children
// closes as <Name .../>. A failure to grow the text is remembered and reported by
// DtXmlOut_Finish, so the calls in between need no checks.
//

typedef struct DtXmlOut
{
    char* Text;
    size_t Length;
    size_t Capacity;
    bool StartOpen; // The last element opened has no children yet
    bool Failed;    // Memory ran out
} DtXmlOut;

// Adds the attribute Name with Value, escaped, to the element just opened.
void DtXmlOut_Attr(DtXmlOut* Out, const char* Name, const char* Value);

// Adds the attribute Name with Value as true or false.
void DtXmlOut_AttrBool(DtXmlOut* Out, const char* Name, bool Value);

// Adds the attribute Name with the decimal Value.
void DtXmlOut_AttrInt(DtXmlOut* Out, const char* Name, int64_t Value);

// Closes the element Name, the one opened last that is still open.
void DtXmlOut_Close(DtXmlOut* Out, const char* Name);

// Hands the document over in *Text, to be freed with DtAlloc_Free, and empties Out. When
// memory ran out on the way, frees it instead and returns DTAPI_E_OUT_OF_MEM.
DtapiResult DtXmlOut_Finish(DtXmlOut* Out, char** Text);

// Starts an empty document.
void DtXmlOut_Init(DtXmlOut* Out);

// Opens the element Name, a child of the element that is open, if any.
void DtXmlOut_Open(DtXmlOut* Out, const char* Name);

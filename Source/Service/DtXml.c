// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtXml.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The XML of DtapiService's messages: elements with attributes
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtXml.h"        // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The deepest nesting accepted; the service's documents nest three deep.
#define DT_XML_MAX_DEPTH 32

// The size a document being written starts at; a command fits in it.
#define DT_XML_MIN_CAPACITY 256

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsSpace -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsSpace(char Char)
{
    return Char == ' ' || Char == '\t' || Char == '\r' || Char == '\n';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsNameChar -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Whether Char may be in a name: a letter, a digit, or one of _ : - . The service's names
// are ASCII; bytes of UTF-8 above it are allowed too.
//
static bool IsNameChar(char Char)
{
    unsigned char Byte = (unsigned char)Char;
    return (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z') ||
           (Byte >= '0' && Byte <= '9') || Byte == '_' || Byte == ':' || Byte == '-' ||
           Byte == '.' || Byte >= 0x80;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartsWith -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static bool StartsWith(const char* Text, const char* Prefix)
{
    return strncmp(Text, Prefix, strlen(Prefix)) == 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A new zero-terminated copy of the Length bytes at Text, or NULL without memory.
//
static char* CopyText(const char* Text, size_t Length)
{
    char* Copy = (char*)DtAlloc_Malloc(Length + 1);
    if (Copy != NULL)
    {
        memcpy(Copy, Text, Length);
        Copy[Length] = '\0';
    }
    return Copy;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DecodeEntity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Decodes the entity or character reference at *Text, which starts with &, into UTF-8 at
// *Out, and moves both past it. Returns false for one that is unknown, unterminated, or
// refers to no character.
//
static bool DecodeEntity(const char** Text, char** Out)
{
    static const struct
    {
        const char* Name;
        char Char;
    } Named[] = {
        {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}};
    for (size_t i = 0; i < sizeof(Named) / sizeof(Named[0]); i++)
    {
        if (StartsWith(*Text, Named[i].Name))
        {
            *(*Out)++ = Named[i].Char;
            *Text += strlen(Named[i].Name);
            return true;
        }
    }
    if (!StartsWith(*Text, "&#"))
        return false;

    const char* Next = *Text + 2;
    bool Hex = *Next == 'x';
    if (Hex)
        Next++;
    uint32_t Code = 0;
    int NumDigits = 0;
    for (; *Next != ';'; Next++, NumDigits++)
    {
        char Char = *Next;
        uint32_t Digit;
        if (Char >= '0' && Char <= '9')
            Digit = (uint32_t)(Char - '0');
        else if (Hex && Char >= 'a' && Char <= 'f')
            Digit = (uint32_t)(Char - 'a' + 10);
        else if (Hex && Char >= 'A' && Char <= 'F')
            Digit = (uint32_t)(Char - 'A' + 10);
        else
            return false;
        Code = Code * (Hex ? 16u : 10u) + Digit;
        if (Code > 0x10FFFF)
            return false;
    }
    if (NumDigits == 0 || Code == 0 || (Code >= 0xD800 && Code <= 0xDFFF))
        return false;
    *Out = EncodeUtf8(*Out, Code);
    *Text = Next + 1;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseName -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads a name at *Text into a new *Name, and moves *Text past it.
//
static DtapiResult ParseName(const char** Text, char** Name)
{
    const char* Start = *Text;
    while (IsNameChar(**Text))
        (*Text)++;
    if (*Text == Start)
        return DTAPI_E_COMMUNICATION;
    *Name = CopyText(Start, (size_t)(*Text - Start));
    return *Name != NULL ? DTAPI_OK : DTAPI_E_OUT_OF_MEM;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads a quoted attribute value at *Text, decoded, into a new *Value, and moves *Text
// past its closing quote. Decoding only shortens the text, so the copy is never longer
// than the value as written.
//
static DtapiResult ParseValue(const char** Text, char** Value)
{
    char Quote = **Text;
    if (Quote != '"' && Quote != '\'')
        return DTAPI_E_COMMUNICATION;
    const char* Start = *Text + 1;
    const char* End = strchr(Start, Quote);
    if (End == NULL)
        return DTAPI_E_COMMUNICATION;

    char* Out = (char*)DtAlloc_Malloc((size_t)(End - Start) + 1);
    if (Out == NULL)
        return DTAPI_E_OUT_OF_MEM;
    char* Next = Out;
    const char* In = Start;
    while (In < End)
    {
        bool Valid = *In != '<';
        if (Valid && *In == '&')
            Valid = DecodeEntity(&In, &Next);
        else if (Valid)
            *Next++ = *In++;
        if (!Valid)
        {
            DtAlloc_Free(Out);
            return DTAPI_E_COMMUNICATION;
        }
    }
    *Next = '\0';
    *Value = Out;
    *Text = End + 1;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SkipMisc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Moves *Text past white space, XML declarations and comments, and, when SkipText is
// true, past the text between elements. Returns false at a declaration or comment
// without its end.
//
static bool SkipMisc(const char** Text, bool SkipText)
{
    for (;;)
    {
        const char* Next = *Text;
        while (*Next != '\0' && *Next != '<' && (SkipText || IsSpace(*Next)))
            Next++;
        const char* End = NULL;
        if (StartsWith(Next, "<?"))
            End = strstr(Next + 2, "?>");
        else if (StartsWith(Next, "<!--"))
            End = strstr(Next + 4, "-->");
        else
        {
            *Text = Next;
            return true;
        }
        if (End == NULL)
            return false;
        *Text = End + (Next[1] == '?' ? 2 : 3);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddAttr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Adds an empty attribute to Elem and returns it, or NULL without memory.
//
static DtXmlAttr* AddAttr(DtXmlElem* Elem)
{
    DtXmlAttr* Attrs = (DtXmlAttr*)DtAlloc_Realloc(
        Elem->Attrs, (size_t)(Elem->NumAttrs + 1) * sizeof(DtXmlAttr));
    if (Attrs == NULL)
        return NULL;
    Elem->Attrs = Attrs;
    DtXmlAttr* Attr = &Attrs[Elem->NumAttrs++];
    memset(Attr, 0, sizeof(*Attr));
    return Attr;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AddChild -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds an empty child to Elem and returns it, or NULL without memory.
//
static DtXmlElem* AddChild(DtXmlElem* Elem)
{
    DtXmlElem* Children = (DtXmlElem*)DtAlloc_Realloc(
        Elem->Children, (size_t)(Elem->NumChildren + 1) * sizeof(DtXmlElem));
    if (Children == NULL)
        return NULL;
    Elem->Children = Children;
    DtXmlElem* Child = &Children[Elem->NumChildren++];
    memset(Child, 0, sizeof(*Child));
    return Child;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseAttrs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads the attributes of a start tag into Elem, and moves *Text to the / or > that ends
// them.
//
static DtapiResult ParseAttrs(const char** Text, DtXmlElem* Elem)
{
    for (;;)
    {
        while (IsSpace(**Text))
            (*Text)++;
        if (**Text == '/' || **Text == '>')
            return DTAPI_OK;
        DtXmlAttr* Attr = AddAttr(Elem);
        if (Attr == NULL)
            return DTAPI_E_OUT_OF_MEM;
        DtapiResult Result = ParseName(Text, &Attr->Name);
        if (Result != DTAPI_OK)
            return Result;
        while (IsSpace(**Text))
            (*Text)++;
        if (**Text != '=')
            return DTAPI_E_COMMUNICATION;
        (*Text)++;
        while (IsSpace(**Text))
            (*Text)++;
        Result = ParseValue(Text, &Attr->Value);
        if (Result != DTAPI_OK)
            return Result;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseElem -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the element at *Text, which starts with <, into Elem, and moves *Text past it.
// What it has read is in Elem after a failure too, for DtXml_Free.
//
static DtapiResult ParseElem(const char** Text, DtXmlElem* Elem, int Depth)
{
    if (Depth > DT_XML_MAX_DEPTH || **Text != '<')
        return DTAPI_E_COMMUNICATION;
    (*Text)++;
    DtapiResult Result = ParseName(Text, &Elem->Name);
    if (Result == DTAPI_OK)
        Result = ParseAttrs(Text, Elem);
    if (Result != DTAPI_OK)
        return Result;
    if (StartsWith(*Text, "/>"))
    {
        *Text += 2;
        return DTAPI_OK;
    }
    (*Text)++;

    // The children, up to the end tag.
    for (;;)
    {
        if (!SkipMisc(Text, true) || **Text == '\0')
            return DTAPI_E_COMMUNICATION;
        if (StartsWith(*Text, "</"))
            break;
        DtXmlElem* Child = AddChild(Elem);
        if (Child == NULL)
            return DTAPI_E_OUT_OF_MEM;
        Result = ParseElem(Text, Child, Depth + 1);
        if (Result != DTAPI_OK)
            return Result;
    }
    *Text += 2;
    size_t NameLength = strlen(Elem->Name);
    if (strncmp(*Text, Elem->Name, NameLength) != 0)
        return DTAPI_E_COMMUNICATION;
    *Text += NameLength;
    while (IsSpace(**Text))
        (*Text)++;
    if (**Text != '>')
        return DTAPI_E_COMMUNICATION;
    (*Text)++;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Append -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds the Length bytes of Text to the document, growing it as needed.
//
static void Append(DtXmlOut* Out, const char* Text, size_t Length)
{
    if (Out->Failed)
        return;
    if (Out->Length + Length + 1 > Out->Capacity)
    {
        size_t Capacity = 0;
        char* Grown = NULL;
        if (DtAlloc_GrowCapacity(Out->Capacity, Out->Length + Length + 1, 1,
                                 DT_XML_MIN_CAPACITY, &Capacity) == 0)
            Grown = (char*)DtAlloc_Realloc(Out->Text, Capacity);
        if (Grown == NULL)
        {
            Out->Failed = true;
            return;
        }
        Out->Text = Grown;
        Out->Capacity = Capacity;
    }
    memcpy(Out->Text + Out->Length, Text, Length);
    Out->Length += Length;
    Out->Text[Out->Length] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AppendText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void AppendText(DtXmlOut* Out, const char* Text)
{
    Append(Out, Text, strlen(Text));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reader +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_Attr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* DtXml_Attr(const DtXmlElem* Elem, const char* Name)
{
    if (Elem == NULL || Name == NULL)
        return NULL;
    for (int i = 0; i < Elem->NumAttrs; i++)
    {
        if (strcmp(Elem->Attrs[i].Name, Name) == 0)
            return Elem->Attrs[i].Value;
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_AttrBool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtXml_AttrBool(const DtXmlElem* Elem, const char* Name, bool* Value)
{
    const char* Text = DtXml_Attr(Elem, Name);
    if (Text == NULL || Value == NULL)
        return false;
    if (strcmp(Text, "true") == 0 || strcmp(Text, "TRUE") == 0)
        *Value = true;
    else if (strcmp(Text, "false") == 0 || strcmp(Text, "FALSE") == 0)
        *Value = false;
    else
        return false;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_AttrDouble -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtXml_AttrDouble(const DtXmlElem* Elem, const char* Name, double* Value)
{
    const char* Text = DtXml_Attr(Elem, Name);
    if (Text == NULL || Value == NULL || Text[0] == '\0' || strlen(Text) >= 64)
        return false;

    // strtod reads the decimal point of the locale, so the point is replaced by it.
    // Only the characters of a number in the C locale are accepted.
    char Local[80];
    const char* Point = localeconv()->decimal_point;
    size_t Length = 0;
    for (const char* Next = Text; *Next != '\0'; Next++)
    {
        if (*Next == '.')
        {
            size_t PointLength = strlen(Point);
            if (Length + PointLength >= sizeof(Local))
                return false;
            memcpy(Local + Length, Point, PointLength);
            Length += PointLength;
            continue;
        }
        if (strchr("0123456789+-eE", *Next) == NULL)
            return false;
        Local[Length++] = *Next;
    }
    Local[Length] = '\0';
    char* End = NULL;
    double Number = strtod(Local, &End);
    if (End == Local || *End != '\0')
        return false;
    *Value = Number;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_AttrInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtXml_AttrInt(const DtXmlElem* Elem, const char* Name, int64_t* Value)
{
    const char* Text = DtXml_Attr(Elem, Name);
    if (Text == NULL || Value == NULL)
        return false;
    bool Negative = *Text == '-';
    if (Negative)
        Text++;

    // The magnitude, up to that of INT64_MIN, which is one more than INT64_MAX.
    uint64_t Limit = Negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    uint64_t Magnitude = 0;
    if (*Text == '\0')
        return false;
    for (; *Text != '\0'; Text++)
    {
        if (*Text < '0' || *Text > '9')
            return false;
        uint64_t Digit = (uint64_t)(*Text - '0');
        if (Magnitude > (Limit - Digit) / 10)
            return false;
        Magnitude = Magnitude * 10 + Digit;
    }
    *Value = Negative ? (Magnitude == (uint64_t)INT64_MAX + 1 ? INT64_MIN
                                                              : -(int64_t)Magnitude)
                      : (int64_t)Magnitude;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_AttrUInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtXml_AttrUInt(const DtXmlElem* Elem, const char* Name, uint64_t* Value)
{
    const char* Text = DtXml_Attr(Elem, Name);
    if (Text == NULL || Value == NULL || *Text == '\0')
        return false;
    uint64_t Number = 0;
    for (; *Text != '\0'; Text++)
    {
        if (*Text < '0' || *Text > '9')
            return false;
        uint64_t Digit = (uint64_t)(*Text - '0');
        if (Number > (UINT64_MAX - Digit) / 10)
            return false;
        Number = Number * 10 + Digit;
    }
    *Value = Number;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_Child -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtXmlElem* DtXml_Child(const DtXmlElem* Elem, const char* Name)
{
    if (Elem == NULL || Name == NULL)
        return NULL;
    for (int i = 0; i < Elem->NumChildren; i++)
    {
        if (strcmp(Elem->Children[i].Name, Name) == 0)
            return &Elem->Children[i];
    }
    return NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtXml_Free(DtXmlElem* Root)
{
    if (Root == NULL)
        return;
    for (int i = 0; i < Root->NumAttrs; i++)
    {
        DtAlloc_Free(Root->Attrs[i].Name);
        DtAlloc_Free(Root->Attrs[i].Value);
    }
    for (int i = 0; i < Root->NumChildren; i++)
        DtXml_Free(&Root->Children[i]);
    DtAlloc_Free(Root->Name);
    DtAlloc_Free(Root->Attrs);
    DtAlloc_Free(Root->Children);
    memset(Root, 0, sizeof(*Root));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXml_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtXml_Parse(const char* Text, DtXmlElem* Root)
{
    if (Root != NULL)
        memset(Root, 0, sizeof(*Root));
    if (Text == NULL || Root == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!SkipMisc(&Text, false))
        return DTAPI_E_COMMUNICATION;
    DtapiResult Result = ParseElem(&Text, Root, 1);
    if (Result == DTAPI_OK && (!SkipMisc(&Text, false) || *Text != '\0'))
        Result = DTAPI_E_COMMUNICATION;
    if (Result != DTAPI_OK)
        DtXml_Free(Root);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_Attr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtXmlOut_Attr(DtXmlOut* Out, const char* Name, const char* Value)
{
    if (Out == NULL || Name == NULL || Value == NULL)
        return;
    AppendText(Out, " ");
    AppendText(Out, Name);
    AppendText(Out, "=\"");
    for (const char* Next = Value; *Next != '\0'; Next++)
    {
        switch (*Next)
        {
        case '&':
            AppendText(Out, "&amp;");
            break;
        case '<':
            AppendText(Out, "&lt;");
            break;
        case '>':
            AppendText(Out, "&gt;");
            break;
        case '"':
            AppendText(Out, "&quot;");
            break;
        default:
            Append(Out, Next, 1);
            break;
        }
    }
    AppendText(Out, "\"");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_AttrBool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtXmlOut_AttrBool(DtXmlOut* Out, const char* Name, bool Value)
{
    DtXmlOut_Attr(Out, Name, Value ? "true" : "false");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_AttrDouble -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtXmlOut_AttrDouble(DtXmlOut* Out, const char* Name, double Value)
{
    // The locale's decimal point, which snprintf writes, becomes a point.
    char Number[64];
    snprintf(Number, sizeof(Number), "%.17g", Value);
    const char* Point = localeconv()->decimal_point;
    char* Found = strstr(Number, Point);
    if (Found != NULL && strcmp(Point, ".") != 0)
    {
        size_t PointLength = strlen(Point);
        *Found = '.';
        memmove(Found + 1, Found + PointLength, strlen(Found + PointLength) + 1);
    }
    DtXmlOut_Attr(Out, Name, Number);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_AttrInt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtXmlOut_AttrInt(DtXmlOut* Out, const char* Name, int64_t Value)
{
    char Number[24];
    snprintf(Number, sizeof(Number), "%" PRId64, Value);
    DtXmlOut_Attr(Out, Name, Number);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtXmlOut_Close(DtXmlOut* Out, const char* Name)
{
    if (Out == NULL || Name == NULL)
        return;
    if (Out->StartOpen)
    {
        AppendText(Out, "/>");
        Out->StartOpen = false;
        return;
    }
    AppendText(Out, "</");
    AppendText(Out, Name);
    AppendText(Out, ">");
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_Finish -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtXmlOut_Finish(DtXmlOut* Out, char** Text)
{
    if (Text != NULL)
        *Text = NULL;
    if (Out == NULL || Text == NULL)
        return DTAPI_E_INVALID_ARG;

    // An empty document is an empty text, not a NULL one.
    if (!Out->Failed && Out->Text == NULL)
        AppendText(Out, "");
    DtapiResult Result = Out->Failed ? DTAPI_E_OUT_OF_MEM : DTAPI_OK;
    if (Result == DTAPI_OK)
        *Text = Out->Text;
    else
        DtAlloc_Free(Out->Text);
    DtXmlOut_Init(Out);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtXmlOut_Init(DtXmlOut* Out)
{
    if (Out != NULL)
        memset(Out, 0, sizeof(*Out));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtXmlOut_Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtXmlOut_Open(DtXmlOut* Out, const char* Name)
{
    if (Out == NULL || Name == NULL)
        return;
    if (Out->StartOpen)
        AppendText(Out, ">");
    AppendText(Out, "<");
    AppendText(Out, Name);
    Out->StartOpen = true;
}

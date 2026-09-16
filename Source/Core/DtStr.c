// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtStr.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Growable string with a small-buffer optimisation - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDtapiLite includes
#include "DtAlloc.h" // Allocation seam and growth policy.
#include "DtStr.h"   // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Grow -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Makes room for Needed characters plus the terminator, moving out of the small buffer
// the first time that is not enough. Returns 0 on success and -1 when out of memory,
// leaving Str untouched on failure.
//
static int Grow(DtStr* Str, size_t Needed)
{
    size_t Required = Needed + 1;
    size_t NewCapacity;
    char* NewData;

    // Needed is a character count, so the terminator can push it past the maximum.
    if (Required < Needed)
        return -1;

    // The failure branch here cannot be reached: DtGrowCapacity only fails on a byte
    // count that does not fit, and with an element size of one the character count is
    // the byte count, which Required has already been checked against. The call stays
    // because the growth policy is shared and its contract may widen.
    if (DtGrowCapacity(Str->Capacity, Required, 1, DT_STR_SMALL_CAPACITY, &NewCapacity) !=
        0)
    {
        return -1;
    }

    if (NewCapacity == Str->Capacity)
        return 0;

    if (Str->Data == Str->Small)
    {
        // Moving out of the embedded buffer. realloc cannot be used on it, so the
        // contents including the terminator are copied across by hand.
        NewData = (char*)DtMalloc(NewCapacity);
        if (NewData == NULL)
            return -1;

        memcpy(NewData, Str->Small, Str->Length + 1);
    }
    else
    {
        NewData = (char*)DtRealloc(Str->Data, NewCapacity);
        if (NewData == NULL)
            return -1;
    }

    Str->Data = NewData;
    Str->Capacity = NewCapacity;
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtStrInit(DtStr* Str)
{
    if (Str == NULL)
        return;

    Str->Data = Str->Small;
    Str->Length = 0;
    Str->Capacity = DT_STR_SMALL_CAPACITY;
    Str->Small[0] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtStrFree(DtStr* Str)
{
    if (Str == NULL)
        return;

    if (Str->Data != Str->Small)
        DtFree(Str->Data);

    DtStrInit(Str);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Modifiers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrReserve -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtStrReserve(DtStr* Str, size_t Capacity)
{
    if (Str == NULL)
        return -1;

    return Grow(Str, Capacity);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrClear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtStrClear(DtStr* Str)
{
    if (Str == NULL)
        return;

    Str->Length = 0;
    Str->Data[0] = '\0';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrAppendLen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtStrAppendLen(DtStr* Str, const char* Text, size_t Length)
{
    if (Str == NULL || Text == NULL)
        return -1;

    if (Length == 0)
        return 0;

    // The new length is computed before it is checked, so it has to be checked before it
    // is computed. Without this, a length close to the maximum wraps to a small number,
    // Grow is satisfied by the room already there, and the memcpy below runs off the end
    // of the buffer. The room for the terminator is part of what must fit.
    if (Length > (size_t)-1 - Str->Length - 1)
        return -1;

    if (Grow(Str, Str->Length + Length) != 0)
        return -1;

    memcpy(Str->Data + Str->Length, Text, Length);
    Str->Length += Length;
    Str->Data[Str->Length] = '\0';
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrAppend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtStrAppend(DtStr* Str, const char* Text)
{
    if (Text == NULL)
        return -1;

    return DtStrAppendLen(Str, Text, strlen(Text));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrAppendChar -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtStrAppendChar(DtStr* Str, char Ch)
{
    return DtStrAppendLen(Str, &Ch, 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AppendVaList -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// vsnprintf is called twice: once to measure and once to write. The argument list is
// consumed by the first call, so the caller passes a copy for the second.
//
static int AppendVaList(DtStr* Str, const char* Format, va_list Measure, va_list Write)
{
    int Needed;

    // A negative result means an encoding error rather than a size. There is no
    // portable way to provoke one, so this branch is not covered by the tests.
    Needed = vsnprintf(NULL, 0, Format, Measure);
    if (Needed < 0)
        return -1;

    if (Grow(Str, Str->Length + (size_t)Needed) != 0)
        return -1;

    vsnprintf(Str->Data + Str->Length, (size_t)Needed + 1, Format, Write);
    Str->Length += (size_t)Needed;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrAppendFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtStrAppendFormat(DtStr* Str, const char* Format, ...)
{
    va_list Measure;
    va_list Write;
    int Result;

    if (Str == NULL || Format == NULL)
        return -1;

    va_start(Measure, Format);
    va_start(Write, Format);
    Result = AppendVaList(Str, Format, Measure, Write);
    va_end(Write);
    va_end(Measure);

    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrSetFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtStrSetFormat(DtStr* Str, const char* Format, ...)
{
    va_list Measure;
    va_list Write;
    int Result;

    if (Str == NULL || Format == NULL)
        return -1;

    DtStrClear(Str);

    va_start(Measure, Format);
    va_start(Write, Format);
    Result = AppendVaList(Str, Format, Measure, Write);
    va_end(Write);
    va_end(Measure);

    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Accessors +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrCStr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const char* DtStrCStr(const DtStr* Str)
{
    return Str != NULL ? Str->Data : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrLength -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtStrLength(const DtStr* Str)
{
    return Str != NULL ? Str->Length : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtStrIsSmall -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtStrIsSmall(const DtStr* Str)
{
    return Str != NULL && Str->Data == Str->Small;
}

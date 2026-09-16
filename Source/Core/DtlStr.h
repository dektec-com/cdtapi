// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlStr.h *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Growable string with a small-buffer optimisation
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_STR_H
#define CDTAPILITE_DTL_STR_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdarg.h>
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtlStr +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Building the strings this library has to produce: the group, value and subvalue names
// the driver expects in DtIoctlIoConfig, and the device description in DtHwFuncDesc.
//
// Those are all short, and they are built on paths that run per device and per port, so
// the first 64 characters live in the structure itself and cost no allocation. Longer
// contents move to the heap, and once there the string stays there.
//
// The contents are always terminated, so DtlStrCStr can be handed straight to strcpy or
// to snprintf. The structure is visible so that it can live on the stack.
//
// Every call that can allocate returns 0 on success and -1 when out of memory. On
// failure the string keeps the contents it had.
//

#define DTL_STR_SMALL_CAPACITY 64

typedef struct DtlStr
{
    char* Data;
    size_t Length;
    size_t Capacity;
    char Small[DTL_STR_SMALL_CAPACITY];
} DtlStr;

// Prepares an empty string. No allocation.
void DtlStrInit(DtlStr* Str);

// Releases any heap storage and returns the string to the empty state. Safe to call
// twice.
void DtlStrFree(DtlStr* Str);

// Makes room for at least Capacity characters plus the terminator.
int DtlStrReserve(DtlStr* Str, size_t Capacity);

// Empties the string but keeps the storage.
void DtlStrClear(DtlStr* Str);

// Appends a null-terminated string.
int DtlStrAppend(DtlStr* Str, const char* Text);

// Appends Length characters, which may contain anything, including a null byte.
int DtlStrAppendLen(DtlStr* Str, const char* Text, size_t Length);

// Appends one character.
int DtlStrAppendChar(DtlStr* Str, char Ch);

// Appends printf-formatted text.
int DtlStrAppendFormat(DtlStr* Str, const char* Format, ...);

// Replaces the contents with printf-formatted text.
int DtlStrSetFormat(DtlStr* Str, const char* Format, ...);

// The contents, always null-terminated. Never NULL for an initialised string. The
// pointer is invalidated by anything that can reallocate.
const char* DtlStrCStr(const DtlStr* Str);

// The number of characters, not counting the terminator. Zero when Str is NULL.
size_t DtlStrLength(const DtlStr* Str);

// True while the contents still fit in the structure and no allocation has happened.
// Intended for tests: it is the only way to tell that the optimisation is working.
int DtlStrIsSmall(const DtlStr* Str);

#endif // CDTAPILITE_DTL_STR_H

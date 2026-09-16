// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtStr.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Growable string with a small-buffer optimisation
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_STR_H
#define CDTAPILITE_DT_STR_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdarg.h>
#include <stddef.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtStr +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Building the strings this library has to produce: the group, value and subvalue names
// the driver expects in DtIoctlIoConfig, and the device description in DtHwFuncDesc.
//
// Those are all short, and they are built on paths that run per device and per port, so
// the first 64 characters live in the structure itself and cost no allocation. Longer
// contents move to the heap, and once there the string stays there.
//
// The contents are always terminated, so DtStrCStr can be handed straight to strcpy or
// to snprintf. The structure is visible so that it can live on the stack.
//
// Every call that can allocate returns 0 on success and -1 when out of memory. On
// failure the string keeps the contents it had.
//

#define DT_STR_SMALL_CAPACITY 64

typedef struct DtStr
{
    char* Data;
    size_t Length;
    size_t Capacity;
    char Small[DT_STR_SMALL_CAPACITY];
} DtStr;

// Prepares an empty string. No allocation.
void DtStrInit(DtStr* Str);

// Releases any heap storage and returns the string to the empty state. Safe to call
// twice.
void DtStrFree(DtStr* Str);

// Makes room for at least Capacity characters plus the terminator.
int DtStrReserve(DtStr* Str, size_t Capacity);

// Empties the string but keeps the storage.
void DtStrClear(DtStr* Str);

// Appends a null-terminated string.
int DtStrAppend(DtStr* Str, const char* Text);

// Appends Length characters, which may contain anything, including a null byte.
int DtStrAppendLen(DtStr* Str, const char* Text, size_t Length);

// Appends one character.
int DtStrAppendChar(DtStr* Str, char Ch);

// Appends printf-formatted text.
int DtStrAppendFormat(DtStr* Str, const char* Format, ...);

// Replaces the contents with printf-formatted text.
int DtStrSetFormat(DtStr* Str, const char* Format, ...);

// The contents, always null-terminated. Never NULL for an initialised string. The
// pointer is invalidated by anything that can reallocate.
const char* DtStrCStr(const DtStr* Str);

// The number of characters, not counting the terminator. Zero when Str is NULL.
size_t DtStrLength(const DtStr* Str);

// True while the contents still fit in the structure and no allocation has happened.
// Intended for tests: it is the only way to tell that the optimisation is working.
int DtStrIsSmall(const DtStr* Str);

#endif // CDTAPILITE_DT_STR_H

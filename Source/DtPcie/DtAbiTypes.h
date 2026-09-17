// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAbiTypes.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Base types required by the vendored driver ABI headers
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Types +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Source/DtPcie/Abi/DtCommon.h describes a binary interface to the DtPcie driver and
// spells its fields as Int, UInt, UInt8 and friends. Those names come from a DekTec
// header that carries no redistribution grant, so CDtapiLite defines them here instead,
// sized and aligned to match the driver exactly.
//
// The A suffix on Int64A/UInt64A means "aligned". A 64-bit field inside an ioctl struct
// must sit on an 8-byte boundary so that a 32-bit library and a 64-bit driver agree on
// the layout. On x86-32 System V a long long is only 4-byte aligned by default, so the
// alignment is stated explicitly. Getting this wrong does not fail loudly: the structs
// simply shift and the driver reads the wrong fields. The ASSERT_SIZE checks that
// DtCommon.h carries on every struct are what catch it, so they must all compile.
//

typedef signed int Int;
typedef signed char Int8;
typedef signed short Int16;
typedef signed int Int32;

typedef unsigned int UInt;
typedef unsigned char UInt8;
typedef unsigned short UInt16;
typedef unsigned int UInt32;

typedef intptr_t IntPtr;
typedef uintptr_t UIntPtr;

typedef signed long Long;
typedef unsigned long ULong;

typedef double Double;
typedef float Float;

typedef bool Bool;

#if defined(_WIN32) || defined(_WIN64)
typedef signed __int64 DtInt64;
typedef unsigned __int64 DtUInt64;
typedef DtInt64 Int64A;
typedef DtUInt64 UInt64A;
#else
typedef signed long long DtInt64;
typedef unsigned long long DtUInt64;
typedef DtInt64 Int64A __attribute__((aligned(8)));
typedef DtUInt64 UInt64A __attribute__((aligned(8)));
#endif

// DtCommon.h deliberately poisons the unaligned spellings so that they cannot be used by
// accident in a structure that crosses the driver boundary. The definitions are repeated
// here because code may include this header on its own.
#ifndef Int64
    #define Int64 ERROR_DO_NOT_USE_UNALIGNED_INT64
#endif
#ifndef UInt64
    #define UInt64 ERROR_DO_NOT_USE_UNALIGNED_UINT64
#endif

// Alignment is a promise the compiler has to keep; check it here rather than trust it.
_Static_assert(sizeof(Int64A) == 8, "Int64A must be 8 bytes");
_Static_assert(sizeof(UInt64A) == 8, "UInt64A must be 8 bytes");
_Static_assert(_Alignof(Int64A) == 8, "Int64A must be 8-byte aligned");
_Static_assert(_Alignof(UInt64A) == 8, "UInt64A must be 8-byte aligned");
_Static_assert(sizeof(Int) == 4, "Int must be 4 bytes");
_Static_assert(sizeof(UInt32) == 4, "UInt32 must be 4 bytes");
_Static_assert(sizeof(UInt16) == 2, "UInt16 must be 2 bytes");
_Static_assert(sizeof(UInt8) == 1, "UInt8 must be 1 byte");

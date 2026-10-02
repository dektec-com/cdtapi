// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAbiTypes.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Base types required by the vendored driver ABI headers
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Types +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Defines the base types that the driver's ABI headers use. Abi/DtCommon.h declares the
// structures of the DtPcie driver with types such as Int, UInt and UInt8. DekTec defines
// those in a header that may not be redistributed, so CDTAPI defines them here, with the
// same size and alignment as in the driver.
//
// The A in Int64A and UInt64A means "aligned". A 64-bit field in an IOCTL structure must
// be on an 8-byte boundary, so that a 32-bit library and a 64-bit driver agree on the
// layout. On 32-bit x86 Linux, a long long is only 4-byte aligned by default, so the
// alignment is set explicitly. A wrong alignment does not fail loudly: the fields shift
// and the driver reads the wrong values. The ASSERT_SIZE checks in DtCommon.h catch it,
// so they must all compile.
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

// Makes the unaligned names Int64 and UInt64 fail to compile, so that no structure for
// the driver uses them by accident. DtCommon.h does the same; this header repeats it
// because code may include it on its own.
#ifndef Int64
    #define Int64 ERROR_DO_NOT_USE_UNALIGNED_INT64
#endif
#ifndef UInt64
    #define UInt64 ERROR_DO_NOT_USE_UNALIGNED_UINT64
#endif

// Checks that the compiler gives the types the sizes and alignment the driver expects.
_Static_assert(sizeof(Int64A) == 8, "Int64A must be 8 bytes");
_Static_assert(sizeof(UInt64A) == 8, "UInt64A must be 8 bytes");
_Static_assert(_Alignof(Int64A) == 8, "Int64A must be 8-byte aligned");
_Static_assert(_Alignof(UInt64A) == 8, "UInt64A must be 8-byte aligned");
_Static_assert(sizeof(Int) == 4, "Int must be 4 bytes");
_Static_assert(sizeof(UInt32) == 4, "UInt32 must be 4 bytes");
_Static_assert(sizeof(UInt16) == 2, "UInt16 must be 2 bytes");
_Static_assert(sizeof(UInt8) == 1, "UInt8 must be 1 byte");

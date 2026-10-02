// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAtomic.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A 32-bit atomic integer, portable across MSVC and GCC/Clang
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

#if defined(_MSC_VER)
    #include <intrin.h>
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Atomics +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A 32-bit integer that several threads can count up and down, read and write safely,
// such as a reference count.
//
// C11's <stdatomic.h> is not used: MSVC compiles it only with the experimental switch
// /experimental:c11atomics. These functions use the compiler's own intrinsics instead.
//
// The type is long with MSVC, whose intrinsics take a long (32 bits on Windows), and
// int32_t elsewhere.
//

#if defined(_MSC_VER)
typedef volatile long DtAtomicInt;
#else
typedef volatile int32_t DtAtomicInt;
#endif

// Sets the first value of *Value. It is not atomic, and need not be: an object is
// initialised before another thread can reach it.
static inline void DtAtomic_Init(DtAtomicInt* Value, int32_t Initial)
{
    *Value = Initial;
}

#if defined(_MSC_VER)

// Adds one to *Value and returns the new value.
static inline int32_t DtAtomic_Increment(DtAtomicInt* Value)
{
    return _InterlockedIncrement(Value);
}

// Subtracts one from *Value and returns the new value.
static inline int32_t DtAtomic_Decrement(DtAtomicInt* Value)
{
    return _InterlockedDecrement(Value);
}

// Returns the value of *Value.
static inline int32_t DtAtomic_Load(const DtAtomicInt* Value)
{
    // A plain read of an aligned 32-bit value is atomic on every architecture MSVC
    // targets, and volatile keeps the compiler from caching it.
    return *Value;
}

// Sets *Value to Desired.
static inline void DtAtomic_Store(DtAtomicInt* Value, int32_t Desired)
{
    _InterlockedExchange(Value, Desired);
}

#else

// Adds one to *Value and returns the new value.
static inline int32_t DtAtomic_Increment(DtAtomicInt* Value)
{
    return __atomic_add_fetch(Value, 1, __ATOMIC_ACQ_REL);
}

// Subtracts one from *Value and returns the new value.
static inline int32_t DtAtomic_Decrement(DtAtomicInt* Value)
{
    return __atomic_sub_fetch(Value, 1, __ATOMIC_ACQ_REL);
}

// Returns the value of *Value.
static inline int32_t DtAtomic_Load(const DtAtomicInt* Value)
{
    return __atomic_load_n(Value, __ATOMIC_ACQUIRE);
}

// Sets *Value to Desired.
static inline void DtAtomic_Store(DtAtomicInt* Value, int32_t Desired)
{
    __sync_lock_test_and_set(Value, Desired);
}

#endif

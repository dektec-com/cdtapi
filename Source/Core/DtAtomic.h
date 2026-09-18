// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAtomic.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Atomic reference counting, portable across MSVC and GCC/Clang
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
// C11 <stdatomic.h> would express this directly, but MSVC compiles it only behind
// /experimental:c11atomics and otherwise stops with "C atomic support is not enabled".
// Requiring an experimental compiler switch to build a library that is meant to be
// packaged widely is a poor trade for the handful of operations used here, so the ones
// that are needed are mapped onto compiler intrinsics instead.
//
// The value is 32 bits. MSVC's interlocked intrinsics take a long, which is 32 bits on
// Windows, so there the type is long; elsewhere it is int32_t.
//

#if defined(_MSC_VER)
typedef volatile long DtAtomicInt;
#else
typedef volatile int32_t DtAtomicInt;
#endif

// Stores an initial value. Not atomic, and does not need to be: an object is initialised
// before it becomes reachable from another thread.
static inline void DtAtomic_Init(DtAtomicInt* Value, int32_t Initial)
{
    *Value = Initial;
}

#if defined(_MSC_VER)

static inline int32_t DtAtomic_Increment(DtAtomicInt* Value)
{
    return _InterlockedIncrement(Value);
}

static inline int32_t DtAtomic_Decrement(DtAtomicInt* Value)
{
    return _InterlockedDecrement(Value);
}

static inline int32_t DtAtomic_Load(const DtAtomicInt* Value)
{
    // A plain read of an aligned 32-bit value is atomic on every architecture MSVC
    // targets, and volatile keeps the compiler from caching it.
    return *Value;
}

static inline void DtAtomic_Store(DtAtomicInt* Value, int32_t Desired)
{
    _InterlockedExchange(Value, Desired);
}

#else

static inline int32_t DtAtomic_Increment(DtAtomicInt* Value)
{
    return __atomic_add_fetch(Value, 1, __ATOMIC_ACQ_REL);
}

static inline int32_t DtAtomic_Decrement(DtAtomicInt* Value)
{
    return __atomic_sub_fetch(Value, 1, __ATOMIC_ACQ_REL);
}

static inline int32_t DtAtomic_Load(const DtAtomicInt* Value)
{
    return __atomic_load_n(Value, __ATOMIC_ACQUIRE);
}

static inline void DtAtomic_Store(DtAtomicInt* Value, int32_t Desired)
{
    __sync_lock_test_and_set(Value, Desired);
}

#endif

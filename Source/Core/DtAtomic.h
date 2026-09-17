// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAtomic.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Atomic reference counting, portable across MSVC and GCC/Clang
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DT_ATOMIC_H
#define CDTAPILITE_DT_ATOMIC_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

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
// long rather than int, because the Windows interlocked intrinsics are defined on long.
// Both are at least 32 bits on every platform this library targets.
//

typedef volatile long DtAtomicInt;

// Stores an initial value. Not atomic, and does not need to be: an object is initialised
// before it becomes reachable from another thread.
static inline void DtAtomicInit(DtAtomicInt* Value, long Initial)
{
    *Value = Initial;
}

#if defined(_MSC_VER)

static inline long DtAtomicIncrement(DtAtomicInt* Value)
{
    return _InterlockedIncrement(Value);
}

static inline long DtAtomicDecrement(DtAtomicInt* Value)
{
    return _InterlockedDecrement(Value);
}

static inline long DtAtomicLoad(const DtAtomicInt* Value)
{
    // A plain read of an aligned long is atomic on every architecture MSVC targets, and
    // volatile keeps the compiler from caching it.
    return *Value;
}

static inline void DtAtomicStore(DtAtomicInt* Value, long Desired)
{
    _InterlockedExchange(Value, Desired);
}

#else

static inline long DtAtomicIncrement(DtAtomicInt* Value)
{
    return __atomic_add_fetch(Value, 1, __ATOMIC_ACQ_REL);
}

static inline long DtAtomicDecrement(DtAtomicInt* Value)
{
    return __atomic_sub_fetch(Value, 1, __ATOMIC_ACQ_REL);
}

static inline long DtAtomicLoad(const DtAtomicInt* Value)
{
    return __atomic_load_n(Value, __ATOMIC_ACQUIRE);
}

static inline void DtAtomicStore(DtAtomicInt* Value, long Desired)
{
    __sync_lock_test_and_set(Value, Desired);
}

#endif

#endif // CDTAPILITE_DT_ATOMIC_H

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlBuf.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Reference-counted byte buffer with a release callback - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtlBuf.h"    // Interface being implemented.
#include "DtlAlloc.h"  // Allocation seam.
#include "DtlAtomic.h" // Atomic reference counting.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Definition +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtlBuf
{
    DtlAtomicInt RefCount;
    uint8_t* Data;
    size_t Size;
    DtlBufReleaseFunc Release;
    void* Opaque;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseOwned -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The release callback used by DtlBufAlloc. Having one rather than a flag keeps the
// teardown path in DtlBufUnref down to a single branch.
//
static void ReleaseOwned(void* Opaque, uint8_t* Data, size_t Size)
{
    (void)Opaque;
    (void)Size;
    DtlFree(Data);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Construction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlBufWrap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtlBuf* DtlBufWrap(uint8_t* Data, size_t Size, DtlBufReleaseFunc Release, void* Opaque)
{
    DtlBuf* Buf;

    if (Data == NULL || Size == 0)
        return NULL;

    Buf = (DtlBuf*)DtlMalloc(sizeof(DtlBuf));
    if (Buf == NULL)
        return NULL;

    DtlAtomicInit(&Buf->RefCount, 1);
    Buf->Data = Data;
    Buf->Size = Size;
    Buf->Release = Release;
    Buf->Opaque = Opaque;

    return Buf;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlBufAlloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtlBuf* DtlBufAlloc(size_t Size)
{
    uint8_t* Data;
    DtlBuf* Buf;

    if (Size == 0)
        return NULL;

    Data = (uint8_t*)DtlMalloc(Size);
    if (Data == NULL)
        return NULL;

    Buf = DtlBufWrap(Data, Size, ReleaseOwned, NULL);
    if (Buf == NULL)
    {
        DtlFree(Data);
        return NULL;
    }

    return Buf;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Referencing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- DtlBufRef -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtlBuf* DtlBufRef(DtlBuf* Buf)
{
    if (Buf == NULL)
        return NULL;

    DtlAtomicIncrement(&Buf->RefCount);
    return Buf;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlBufUnref -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtlBufUnref(DtlBuf** Buf)
{
    DtlBuf* Target;

    if (Buf == NULL || *Buf == NULL)
        return;

    Target = *Buf;

    // Cleared before the count is dropped. After the decrement another thread may
    // already have destroyed the buffer, so Target must not be touched again on that
    // path, and the caller's pointer must not be left pointing at freed memory.
    *Buf = NULL;

    if (DtlAtomicDecrement(&Target->RefCount) != 0)
        return;

    if (Target->Release != NULL)
        Target->Release(Target->Opaque, Target->Data, Target->Size);

    DtlFree(Target);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Accessors +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlBufData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint8_t* DtlBufData(const DtlBuf* Buf)
{
    return Buf != NULL ? Buf->Data : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlBufSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtlBufSize(const DtlBuf* Buf)
{
    return Buf != NULL ? Buf->Size : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlBufRefCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
long DtlBufRefCount(const DtlBuf* Buf)
{
    return Buf != NULL ? DtlAtomicLoad(&Buf->RefCount) : 0;
}

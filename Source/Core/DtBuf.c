// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtBuf.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Reference-counted byte buffer with a release callback - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtBuf.h"    // Interface being implemented.
#include "DtAlloc.h"  // Allocation seam.
#include "DtAtomic.h" // Atomic reference counting.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Definition +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

struct DtBuf
{
    DtAtomicInt RefCount;
    uint8_t* Data;
    size_t Size;
    DtBufReleaseFunc Release;
    void* Opaque;
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseOwned -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The release callback used by DtBufAlloc. Having one rather than a flag keeps the
// teardown path in DtBufUnref down to a single branch.
//
static void ReleaseOwned(void* Opaque, uint8_t* Data, size_t Size)
{
    (void)Opaque;
    (void)Size;
    DtFree(Data);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Construction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufWrap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtBuf* DtBufWrap(uint8_t* Data, size_t Size, DtBufReleaseFunc Release, void* Opaque)
{
    if (Data == NULL || Size == 0)
        return NULL;

    DtBuf* Buf = (DtBuf*)DtMalloc(sizeof(DtBuf));
    if (Buf == NULL)
        return NULL;

    DtAtomicInit(&Buf->RefCount, 1);
    Buf->Data = Data;
    Buf->Size = Size;
    Buf->Release = Release;
    Buf->Opaque = Opaque;

    return Buf;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufAlloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtBuf* DtBufAlloc(size_t Size)
{
    if (Size == 0)
        return NULL;

    uint8_t* Data = (uint8_t*)DtMalloc(Size);
    if (Data == NULL)
        return NULL;

    DtBuf* Buf = DtBufWrap(Data, Size, ReleaseOwned, NULL);
    if (Buf == NULL)
    {
        DtFree(Data);
        return NULL;
    }

    return Buf;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Referencing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufRef -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtBuf* DtBufRef(DtBuf* Buf)
{
    if (Buf == NULL)
        return NULL;

    DtAtomicIncrement(&Buf->RefCount);
    return Buf;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufUnref -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtBufUnref(DtBuf** Buf)
{
    if (Buf == NULL || *Buf == NULL)
        return;

    DtBuf* Target = *Buf;

    // Cleared before the count is dropped. After the decrement another thread may
    // already have destroyed the buffer, so Target must not be touched again on that
    // path, and the caller's pointer must not be left pointing at freed memory.
    *Buf = NULL;

    if (DtAtomicDecrement(&Target->RefCount) != 0)
        return;

    if (Target->Release != NULL)
        Target->Release(Target->Opaque, Target->Data, Target->Size);

    DtFree(Target);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Accessors +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint8_t* DtBufData(const DtBuf* Buf)
{
    return Buf != NULL ? Buf->Data : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtBufSize(const DtBuf* Buf)
{
    return Buf != NULL ? Buf->Size : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBufRefCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtBufRefCount(const DtBuf* Buf)
{
    return Buf != NULL ? DtAtomicLoad(&Buf->RefCount) : 0;
}

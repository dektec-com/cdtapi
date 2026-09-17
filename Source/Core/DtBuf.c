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
// The release callback used by DtBuf_Alloc. Having one rather than a flag keeps the
// teardown path in DtBuf_Unref down to a single branch.
//
static void ReleaseOwned(void* Opaque, uint8_t* Data, size_t Size)
{
    (void)Opaque;
    (void)Size;
    DtAlloc_Free(Data);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Construction +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_Wrap -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtBuf* DtBuf_Wrap(uint8_t* Data, size_t Size, DtBufReleaseFunc Release, void* Opaque)
{
    if (Data == NULL || Size == 0)
        return NULL;

    DtBuf* Buf = (DtBuf*)DtAlloc_Malloc(sizeof(DtBuf));
    if (Buf == NULL)
        return NULL;

    DtAtomic_Init(&Buf->RefCount, 1);
    Buf->Data = Data;
    Buf->Size = Size;
    Buf->Release = Release;
    Buf->Opaque = Opaque;

    return Buf;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtBuf* DtBuf_Alloc(size_t Size)
{
    if (Size == 0)
        return NULL;

    uint8_t* Data = (uint8_t*)DtAlloc_Malloc(Size);
    if (Data == NULL)
        return NULL;

    DtBuf* Buf = DtBuf_Wrap(Data, Size, ReleaseOwned, NULL);
    if (Buf == NULL)
    {
        DtAlloc_Free(Data);
        return NULL;
    }

    return Buf;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Referencing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_Ref -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtBuf* DtBuf_Ref(DtBuf* Buf)
{
    if (Buf == NULL)
        return NULL;

    DtAtomic_Increment(&Buf->RefCount);
    return Buf;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_Unref -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtBuf_Unref(DtBuf** Buf)
{
    if (Buf == NULL || *Buf == NULL)
        return;

    DtBuf* Target = *Buf;

    // Cleared before the count is dropped. After the decrement another thread may
    // already have destroyed the buffer, so Target must not be touched again on that
    // path, and the caller's pointer must not be left pointing at freed memory.
    *Buf = NULL;

    if (DtAtomic_Decrement(&Target->RefCount) != 0)
        return;

    if (Target->Release != NULL)
        Target->Release(Target->Opaque, Target->Data, Target->Size);

    DtAlloc_Free(Target);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Accessors +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_Data -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint8_t* DtBuf_Data(const DtBuf* Buf)
{
    return Buf != NULL ? Buf->Data : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_Size -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtBuf_Size(const DtBuf* Buf)
{
    return Buf != NULL ? Buf->Size : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtBuf_RefCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtBuf_RefCount(const DtBuf* Buf)
{
    return Buf != NULL ? DtAtomic_Load(&Buf->RefCount) : 0;
}

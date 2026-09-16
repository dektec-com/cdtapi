// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlRing.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Read side of the shared DMA ring buffer - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "DtlRing.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlRingInit(DtlRing* Ring, uint8_t* Base, size_t Size, size_t Reserve)
{
    // A reserve of zero would make a full ring indistinguishable from an empty one, and a
    // reserve of the whole buffer would leave a ring that can never hold anything.
    if (Ring == NULL || Base == NULL || Reserve == 0 || Reserve >= Size)
        return -1;

    Ring->Base = Base;
    Ring->Size = Size;
    Ring->MaxLoad = Size - Reserve;
    Ring->ReadOffset = 0;
    Ring->WriteOffset = 0;
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Offsets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingSetWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlRingSetWriteOffset(DtlRing* Ring, size_t Offset)
{
    size_t Load;

    // The offset comes from the driver. A value outside the buffer means the two sides
    // disagree about the ring, and continuing would read arbitrary memory.
    if (Ring == NULL || Ring->Base == NULL || Offset >= Ring->Size)
        return -1;

    // The producer never fills the reserve, so a load beyond MaxLoad cannot come from a
    // consistent driver either. It is refused rather than read.
    Load = (Offset + Ring->Size - Ring->ReadOffset) % Ring->Size;
    if (Load > Ring->MaxLoad)
        return -1;

    Ring->WriteOffset = Offset;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtlRingReadOffset(const DtlRing* Ring)
{
    return Ring != NULL ? Ring->ReadOffset : 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Capacity +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtlRingLoad(const DtlRing* Ring)
{
    if (Ring == NULL || Ring->Base == NULL)
        return 0;

    return (Ring->WriteOffset + Ring->Size - Ring->ReadOffset) % Ring->Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingFree -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtlRingFree(const DtlRing* Ring)
{
    size_t Load;

    if (Ring == NULL || Ring->Base == NULL)
        return 0;

    // Equal to DTAPI's (Read + MaxLoad - Write) % Size whenever the load is within
    // MaxLoad, which SetWriteOffset guarantees; written this way it cannot wrap.
    Load = DtlRingLoad(Ring);
    return Load >= Ring->MaxLoad ? 0 : Ring->MaxLoad - Load;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reading +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingPeek -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlRingPeek(const DtlRing* Ring, void* Dst, size_t Length)
{
    size_t ToEnd;

    if (Ring == NULL || Ring->Base == NULL || Dst == NULL)
        return -1;

    if (Length == 0)
        return 0;

    if (Length > DtlRingLoad(Ring))
        return -1;

    ToEnd = Ring->Size - Ring->ReadOffset;
    if (Length <= ToEnd)
    {
        memcpy(Dst, Ring->Base + Ring->ReadOffset, Length);
        return 0;
    }

    // The read crosses the end of the buffer, so it is two copies: the tail first, then
    // the rest from the start.
    memcpy(Dst, Ring->Base + Ring->ReadOffset, ToEnd);
    memcpy((uint8_t*)Dst + ToEnd, Ring->Base, Length - ToEnd);
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingSkip -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlRingSkip(DtlRing* Ring, size_t Length)
{
    if (Ring == NULL || Ring->Base == NULL)
        return -1;

    if (Length > DtlRingLoad(Ring))
        return -1;

    Ring->ReadOffset = (Ring->ReadOffset + Length) % Ring->Size;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingRead -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtlRingRead(DtlRing* Ring, void* Dst, size_t Length)
{
    if (DtlRingPeek(Ring, Dst, Length) != 0)
        return -1;

    return DtlRingSkip(Ring, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlRingClear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtlRingClear(DtlRing* Ring)
{
    if (Ring == NULL)
        return;

    Ring->ReadOffset = Ring->WriteOffset;
}

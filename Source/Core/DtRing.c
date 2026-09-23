// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtRing.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Read side of the shared DMA ring buffer - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <string.h>

// CDTAPI includes
#include "DtRing.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtRing_Init(DtRing* Ring, uint8_t* Base, size_t Size, size_t Reserve)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_SetWriteOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtRing_SetWriteOffset(DtRing* Ring, size_t Offset)
{
    // The offset comes from the driver. A value outside the buffer means the two sides
    // disagree about the ring, and continuing would read arbitrary memory.
    if (Ring == NULL || Ring->Base == NULL || Offset >= Ring->Size)
        return -1;

    // The producer never fills the reserve, so a load beyond MaxLoad cannot come from a
    // consistent driver either. It is refused rather than read.
    size_t Load = (Offset + Ring->Size - Ring->ReadOffset) % Ring->Size;
    if (Load > Ring->MaxLoad)
        return -1;

    Ring->WriteOffset = Offset;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Restart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtRing_Restart(DtRing* Ring, size_t Offset)
{
    if (Ring == NULL || Ring->Base == NULL || Offset >= Ring->Size)
        return -1;

    Ring->ReadOffset = Offset;
    Ring->WriteOffset = Offset;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_ReadOffset -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtRing_ReadOffset(const DtRing* Ring)
{
    return Ring != NULL ? Ring->ReadOffset : 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Capacity +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Load -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtRing_Load(const DtRing* Ring)
{
    if (Ring == NULL || Ring->Base == NULL)
        return 0;

    return (Ring->WriteOffset + Ring->Size - Ring->ReadOffset) % Ring->Size;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtRing_Free(const DtRing* Ring)
{
    if (Ring == NULL || Ring->Base == NULL)
        return 0;

    // Equal to (Read + MaxLoad - Write) % Size whenever the load is within MaxLoad,
    // which DtRing_SetWriteOffset guarantees; written this way it cannot wrap.
    size_t Load = DtRing_Load(Ring);
    return Load >= Ring->MaxLoad ? 0 : Ring->MaxLoad - Load;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reading +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Peek -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtRing_Peek(const DtRing* Ring, void* Dst, size_t Length)
{
    return DtRing_PeekAt(Ring, 0, Dst, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsAvailable -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// True when the Length bytes from Offset past the read offset are all in the ring.
// Written so that it cannot wrap: the load is less than the buffer's size.
//
static bool IsAvailable(const DtRing* Ring, size_t Offset, size_t Length)
{
    size_t Load = DtRing_Load(Ring);

    return Offset <= Load && Length <= Load - Offset;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_PeekAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtRing_PeekAt(const DtRing* Ring, size_t Offset, void* Dst, size_t Length)
{
    if (Ring == NULL || Ring->Base == NULL || Dst == NULL)
        return -1;

    if (Length == 0)
        return 0;

    if (!IsAvailable(Ring, Offset, Length))
        return -1;

    size_t Start = (Ring->ReadOffset + Offset) % Ring->Size;
    size_t ToEnd = Ring->Size - Start;
    if (Length <= ToEnd)
    {
        memcpy(Dst, Ring->Base + Start, Length);
        return 0;
    }

    // The read crosses the end of the buffer, so it is two copies: the tail first, then
    // the rest from the start.
    memcpy(Dst, Ring->Base + Start, ToEnd);
    memcpy((uint8_t*)Dst + ToEnd, Ring->Base, Length - ToEnd);
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Span -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const uint8_t* DtRing_Span(const DtRing* Ring, size_t Offset, size_t Length)
{
    if (Ring == NULL || Ring->Base == NULL || !IsAvailable(Ring, Offset, Length))
        return NULL;

    size_t Start = (Ring->ReadOffset + Offset) % Ring->Size;
    return Length <= Ring->Size - Start ? Ring->Base + Start : NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Skip -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtRing_Skip(DtRing* Ring, size_t Length)
{
    if (Ring == NULL || Ring->Base == NULL)
        return -1;

    if (Length > DtRing_Load(Ring))
        return -1;

    Ring->ReadOffset = (Ring->ReadOffset + Length) % Ring->Size;
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtRing_Read(DtRing* Ring, void* Dst, size_t Length)
{
    if (DtRing_Peek(Ring, Dst, Length) != 0)
        return -1;

    return DtRing_Skip(Ring, Length);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtRing_Clear -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtRing_Clear(DtRing* Ring)
{
    if (Ring == NULL)
        return;

    Ring->ReadOffset = Ring->WriteOffset;
}

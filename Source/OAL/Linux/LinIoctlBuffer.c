// #*#*#*#*#*#*#*#*#*#*#*#*#* LinIoctlBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Linux IOCTL in/out buffer layout - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "LinIoctlBuffer.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoctlBuffer_Size -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t LinIoctlBuffer_Size(bool HasSizeHeader, size_t InSize, size_t OutSize)
{
    size_t HeaderBytes = HasSizeHeader ? LIN_IOCTL_SIZE_HEADER_BYTES : 0;
    size_t ForInput = HeaderBytes + InSize;

    return ForInput > OutSize ? ForInput : OutSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoctlBuffer_Pack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int LinIoctlBuffer_Pack(bool HasSizeHeader, const void* In, size_t InSize, size_t OutSize,
                        uint8_t* Buf, size_t BufSize)
{
    size_t HeaderBytes = HasSizeHeader ? LIN_IOCTL_SIZE_HEADER_BYTES : 0;

    // Everything is validated before anything is written. BufSize is the caller's claim
    // about the buffer; if a check could still fail after the memset below, a bad claim
    // would already have cleared memory beyond the real buffer.
    if (Buf == NULL || (In == NULL && InSize != 0))
        return -1;

    if (HasSizeHeader && (InSize > UINT32_MAX || OutSize > UINT32_MAX))
        return -1;

    if (BufSize < LinIoctlBuffer_Size(HasSizeHeader, InSize, OutSize))
        return -1;

    // Clear everything first. Without a size header the driver takes the structure size
    // the IOCTL number encodes for both directions, which can be more than the input
    // given, and must not find garbage there; with one it takes the sizes the header
    // gives.
    memset(Buf, 0, BufSize);

    if (HasSizeHeader)
    {
        uint32_t Sizes[2];

        Sizes[0] = (uint32_t)InSize;
        Sizes[1] = (uint32_t)OutSize;
        memcpy(Buf, Sizes, sizeof(Sizes));
    }

    if (InSize != 0)
        memcpy(Buf + HeaderBytes, In, InSize);

    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoctlBuffer_Unpack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int LinIoctlBuffer_Unpack(const uint8_t* Buf, size_t BufSize, void* Out, size_t OutSize)
{
    if (Buf == NULL || (Out == NULL && OutSize != 0) || BufSize < OutSize)
        return -1;

    if (OutSize != 0)
        memcpy(Out, Buf, OutSize);

    return 0;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#* LinIoctlBuffer.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Linux IOCTL in/out buffer layout - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "LinIoctlBuffer.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoctlBufferSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t LinIoctlBufferSize(bool SizeHeader, size_t InSize, size_t OutSize)
{
    size_t Reserve = SizeHeader ? LIN_IOCTL_SIZE_HEADER_BYTES : 0;
    size_t ForInput = Reserve + InSize;

    return ForInput > OutSize ? ForInput : OutSize;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoctlPack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int LinIoctlPack(bool SizeHeader, const void* In, size_t InSize, size_t OutSize,
                 uint8_t* Buf, size_t BufSize)
{
    size_t Reserve = SizeHeader ? LIN_IOCTL_SIZE_HEADER_BYTES : 0;

    // Everything is validated before anything is written. BufSize is the caller's claim
    // about the buffer; if a check could still fail after the memset below, a bad claim
    // would already have cleared memory beyond the real buffer.
    if (Buf == NULL || (In == NULL && InSize != 0))
        return -1;

    if (SizeHeader && (InSize > UINT32_MAX || OutSize > UINT32_MAX))
        return -1;

    if (BufSize < LinIoctlBufferSize(SizeHeader, InSize, OutSize))
        return -1;

    // Clear everything first. The driver may read past the input it was given, up to the
    // structure size encoded in the IOCTL number, and must not find garbage there.
    memset(Buf, 0, BufSize);

    if (SizeHeader)
    {
        uint32_t Sizes[2];

        Sizes[0] = (uint32_t)InSize;
        Sizes[1] = (uint32_t)OutSize;
        memcpy(Buf, Sizes, sizeof(Sizes));
    }

    if (InSize != 0)
        memcpy(Buf + Reserve, In, InSize);

    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoctlUnpack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int LinIoctlUnpack(const uint8_t* Buf, size_t BufSize, void* Out, size_t OutSize)
{
    if (Buf == NULL || (Out == NULL && OutSize != 0) || BufSize < OutSize)
        return -1;

    if (OutSize != 0)
        memcpy(Out, Buf, OutSize);

    return 0;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The line CRC of SMPTE ST 292 over a stream's words: the portable version and
// the choice of the fastest
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

#if defined(CDTAPI_HAVE_SSSE3)
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#endif

// CDTAPI includes
#include "DtSdiCrc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Choice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasPclmul -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// CPUID leaf 1, ECX bit 1, and bit 9 for the SSSE3 that packs the words.
//
#if defined(CDTAPI_HAVE_SSSE3)
static bool HasPclmul(void)
{
    const unsigned int Wanted = 1u << 1 | 1u << 9;
    #if defined(_MSC_VER)
    int Info[4] = {0};
    __cpuid(Info, 1);
    return ((unsigned int)Info[2] & Wanted) == Wanted;
    #else
    unsigned int Eax = 0;
    unsigned int Ebx = 0;
    unsigned int Ecx = 0;
    unsigned int Edx = 0;
    return __get_cpuid(1, &Eax, &Ebx, &Ecx, &Edx) != 0 && (Ecx & Wanted) == Wanted;
    #endif
}
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiCrcFunc DtSdiCrc_Best(void)
{
    const DtSdiCrcFunc Clmul = DtSdiCrc_Clmul();
    return Clmul != NULL ? Clmul : DtSdiCrc_Streams;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Clmul -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtSdiCrcFunc DtSdiCrc_Clmul(void)
{
#if defined(CDTAPI_HAVE_SSSE3)
    return HasPclmul() ? DtSdiCrc_StreamsClmulUnchecked : NULL;
#else
    return NULL;
#endif
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Portable C +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Streams -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The register's lower ten bits and the word select one entry of the table, and the
// bits above are shifted in on it: the CRC is linear.
//
void DtSdiCrc_Streams(const uint16_t* Words, size_t Count, int Streams,
                      const uint32_t* Table, uint32_t* Crcs)
{
    for (int s = 0; s < Streams; s++)
    {
        uint32_t Crc = 0;
        for (size_t k = 0; k < Count; k++)
            Crc = (Crc >> 10) ^
                  Table[(Crc ^ Words[k * (size_t)Streams + (size_t)s]) & 0x3FF];
        Crcs[s] = Crc;
    }
}

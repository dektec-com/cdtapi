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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Portable C +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Words -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The register's lower ten bits and the word select one entry of the table, and the
// bits above are shifted in on it: the CRC is linear.
//
uint32_t DtSdiCrc_Words(const uint16_t* Words, size_t Count, size_t Step,
                        const uint32_t* Table)
{
    uint32_t Crc = 0;
    for (size_t k = 0; k < Count; k++)
        Crc = (Crc >> 10) ^ Table[(Crc ^ Words[k * Step]) & 0x3FF];
    return Crc;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Choice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasPclmul -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// CPUID leaf 1, ECX bit 1.
//
#if defined(CDTAPI_HAVE_SSSE3)
static bool HasPclmul(void)
{
    #if defined(_MSC_VER)
    int Info[4] = {0};
    __cpuid(Info, 1);
    return (Info[2] & (1 << 1)) != 0;
    #else
    unsigned int Eax = 0;
    unsigned int Ebx = 0;
    unsigned int Ecx = 0;
    unsigned int Edx = 0;
    return __get_cpuid(1, &Eax, &Ebx, &Ecx, &Edx) != 0 && (Ecx & (1u << 1)) != 0;
    #endif
}
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiCrcFunc DtSdiCrc_Best(void)
{
    const DtSdiCrcFunc Clmul = DtSdiCrc_Clmul();
    return Clmul != NULL ? Clmul : DtSdiCrc_Words;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Clmul -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtSdiCrcFunc DtSdiCrc_Clmul(void)
{
#if defined(CDTAPI_HAVE_SSSE3)
    return HasPclmul() ? DtSdiCrc_WordsClmulUnchecked : NULL;
#else
    return NULL;
#endif
}

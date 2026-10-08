// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiCrc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Computes the line CRCs of an HD-SDI line in portable C, and picks the fastest
// version for the processor
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
#include "AvFifo/DtAvPixConv.h" // The check for AVX2.
#include "DtSdiCrc.h"           // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Choice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasPclmul -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns whether the processor has PCLMULQDQ (CPUID leaf 1, ECX bit 1) and SSSE3 (bit
// 9). The SSSE3 version needs both: SSSE3 packs the words, PCLMULQDQ reduces them.
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Avx2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiCrcFunc DtSdiCrc_Avx2(void)
{
#if defined(CDTAPI_HAVE_AVX2)
    return DtSdiCrc_Clmul() != NULL && DtAvPixConv_Avx2() != NULL
               ? DtSdiCrc_StreamsAvx2Unchecked
               : NULL;
#else
    return NULL;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiCrc_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiCrcFunc DtSdiCrc_Best(void)
{
    const DtSdiCrcFunc Avx2 = DtSdiCrc_Avx2();
    if (Avx2 != NULL)
        return Avx2;
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
// Computes each stream's CRC one word at a time. The table gives the effect of the low
// 10 bits of the register combined with the next word; the rest of the register is
// shifted down by 10 bits and added. This works because the CRC is linear.
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

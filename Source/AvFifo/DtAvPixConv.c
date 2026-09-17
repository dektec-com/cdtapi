// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvPixConv.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Pixel conversions between ST 2110-20 pixel groups and the frame formats
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#if defined(CDTAPILITE_HAVE_SSSE3)
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#endif

// CDtapiLite includes
#include "DtAvPixConv.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Portable C +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPgroup10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The 40 bits of a 10-bit pixel group, first sample on top.
//
static uint64_t ReadPgroup10(const uint8_t* Src)
{
    return (uint64_t)Src[0] << 32 | (uint64_t)Src[1] << 24 | (uint64_t)Src[2] << 16 |
           (uint64_t)Src[3] << 8 | Src[4];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Pg10ToUyvy10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (size_t i = 0; i < NumPgroups; i++, Src += 5, Dst += 5)
    {
        uint64_t Bits = ReadPgroup10(Src);
        uint64_t Packed = (Bits >> 30 & 0x3FF) | (Bits >> 20 & 0x3FF) << 10 |
                          (Bits >> 10 & 0x3FF) << 20 | (Bits & 0x3FF) << 30;
        for (int k = 0; k < 5; k++)
            Dst[k] = (uint8_t)(Packed >> 8 * k);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Pg10ToUyvy8(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (size_t i = 0; i < NumPgroups; i++, Src += 5, Dst += 4)
    {
        uint64_t Bits = ReadPgroup10(Src);
        Dst[0] = (uint8_t)(Bits >> 32);
        Dst[1] = (uint8_t)(Bits >> 22);
        Dst[2] = (uint8_t)(Bits >> 12);
        Dst[3] = (uint8_t)(Bits >> 2);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy10ToPg10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Uyvy10ToPg10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (size_t i = 0; i < NumPgroups; i++, Src += 5, Dst += 5)
    {
        uint64_t Packed = 0;
        for (int k = 0; k < 5; k++)
            Packed |= (uint64_t)Src[k] << 8 * k;
        uint64_t Bits = (Packed & 0x3FF) << 30 | (Packed >> 10 & 0x3FF) << 20 |
                        (Packed >> 20 & 0x3FF) << 10 | (Packed >> 30 & 0x3FF);
        for (int k = 0; k < 5; k++)
            Dst[k] = (uint8_t)(Bits >> 8 * (4 - k));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy8ToYuv422p -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Uyvy8ToYuv422p(const uint8_t* Src, size_t NumPgroups, uint8_t* Y, uint8_t* U,
                           uint8_t* V)
{
    for (size_t i = 0; i < NumPgroups; i++, Src += 4)
    {
        U[i] = Src[0];
        Y[2 * i] = Src[1];
        V[i] = Src[2];
        Y[2 * i + 1] = Src[3];
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_C -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtAvPixConv* DtAvPixConv_C(void)
{
    static const DtAvPixConv Table = {Pg10ToUyvy10, Pg10ToUyvy8, Uyvy10ToPg10,
                                      Uyvy8ToYuv422p};
    return &Table;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Choice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasSsse3 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CPUID leaf 1, ECX bit 9.
//
#if defined(CDTAPILITE_HAVE_SSSE3)
static bool HasSsse3(void)
{
    #if defined(_MSC_VER)
    int Info[4] = {0};
    __cpuid(Info, 1);
    return (Info[2] & (1 << 9)) != 0;
    #else
    unsigned int Eax = 0;
    unsigned int Ebx = 0;
    unsigned int Ecx = 0;
    unsigned int Edx = 0;
    return __get_cpuid(1, &Eax, &Ebx, &Ecx, &Edx) != 0 && (Ecx & (1u << 9)) != 0;
    #endif
}
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_Ssse3 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtAvPixConv* DtAvPixConv_Ssse3(void)
{
#if defined(CDTAPILITE_HAVE_SSSE3)
    return HasSsse3() ? DtAvPixConv_Ssse3Table() : NULL;
#else
    return NULL;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtAvPixConv* DtAvPixConv_Best(void)
{
    const DtAvPixConv* Ssse3 = DtAvPixConv_Ssse3();
    return Ssse3 != NULL ? Ssse3 : DtAvPixConv_C();
}

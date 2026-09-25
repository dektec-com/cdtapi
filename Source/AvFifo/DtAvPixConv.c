// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtAvPixConv.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Pixel conversions between ST 2110-20 pixel groups and the frame formats
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

#if defined(CDTAPI_HAVE_SSSE3)
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <cpuid.h>
        #include <immintrin.h>
    #endif
#endif

// CDTAPI includes
#include "DtAvPixConv.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Portable C +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The 10-bit conversions take whole words rather than single bytes: a step reads eight
// bytes, which hold at least the pixel group it converts, and writes up to eight, of
// which the next step overwrites what was too much. A step therefore runs while two pixel
// groups remain, and the byte-by-byte conversion beside it does the last pixel group.
// Compilers turn Swap64 into one instruction.
//

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Swap64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint64_t Swap64(uint64_t Value)
{
    Value = (Value & UINT64_C(0x00FF00FF00FF00FF)) << 8 |
            (Value >> 8 & UINT64_C(0x00FF00FF00FF00FF));
    Value = (Value & UINT64_C(0x0000FFFF0000FFFF)) << 16 |
            (Value >> 16 & UINT64_C(0x0000FFFF0000FFFF));
    return Value << 32 | Value >> 32;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoadLe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The eight bytes at Src as a word, first byte lowest.
//
static uint64_t LoadLe(const uint8_t* Src)
{
    uint64_t Value;
    memcpy(&Value, Src, sizeof(Value));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    Value = Swap64(Value);
#endif
    return Value;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StoreLe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The word at Dst, lowest byte first.
//
static void StoreLe(uint8_t* Dst, uint64_t Value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    Value = Swap64(Value);
#endif
    memcpy(Dst, &Value, sizeof(Value));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadPgroup10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The 40 bits of a 10-bit pixel group, first sample on top.
//
static uint64_t ReadPgroup10(const uint8_t* Src)
{
    return (uint64_t)Src[0] << 32 | (uint64_t)Src[1] << 24 | (uint64_t)Src[2] << 16 |
           (uint64_t)Src[3] << 8 | Src[4];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReverseSamples10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The four samples of a pixel group in the other bit order: the first sample lowest
// rather than highest, which is what UYVY 10 packs, and the same the other way about.
//
static uint64_t ReverseSamples10(uint64_t Bits)
{
    return (Bits >> 30 & 0x3FF) | (Bits >> 20 & 0x3FF) << 10 |
           (Bits >> 10 & 0x3FF) << 20 | (Bits & 0x3FF) << 30;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void Pg10ToUyvy10(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 2; NumPgroups--, Src += 5, Dst += 5)
        StoreLe(Dst, ReverseSamples10(Swap64(LoadLe(Src)) >> 24));
    for (; NumPgroups > 0; NumPgroups--, Src += 5, Dst += 5)
    {
        uint64_t Packed = ReverseSamples10(ReadPgroup10(Src));
        for (int k = 0; k < 5; k++)
            Dst[k] = (uint8_t)(Packed >> 8 * k);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pg10ToUyvy8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The eight highest bits of each sample, which lie ten bits apart.
//
static void Pg10ToUyvy8(const uint8_t* Src, uint8_t* Dst, size_t NumPgroups)
{
    for (; NumPgroups >= 2; NumPgroups--, Src += 5, Dst += 4)
    {
        uint64_t Bits = Swap64(LoadLe(Src)) >> 24;
        uint32_t Bytes = (uint32_t)(Bits >> 32 & 0xFF) | (uint32_t)(Bits >> 14 & 0xFF00) |
                         (uint32_t)(Bits << 4 & 0xFF0000) |
                         (uint32_t)(Bits << 22 & 0xFF000000);
        memcpy(Dst, &Bytes, sizeof(Bytes));
    }
    for (; NumPgroups > 0; NumPgroups--, Src += 5, Dst += 4)
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
    for (; NumPgroups >= 2; NumPgroups--, Src += 5, Dst += 5)
        StoreLe(Dst, Swap64(ReverseSamples10(LoadLe(Src)) << 24));
    for (; NumPgroups > 0; NumPgroups--, Src += 5, Dst += 5)
    {
        uint64_t Packed = 0;
        for (int k = 0; k < 5; k++)
            Packed |= (uint64_t)Src[k] << 8 * k;
        uint64_t Bits = ReverseSamples10(Packed);
        for (int k = 0; k < 5; k++)
            Dst[k] = (uint8_t)(Bits >> 8 * (4 - k));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Uyvy8ToYuv422p -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Byte by byte, unlike the conversions above: this loop is simple enough for a compiler
// to vectorise whole, and gathering its bytes into words by hand keeps GCC from doing so.
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
const DtAvPixConvTable* DtAvPixConv_C(void)
{
    static const DtAvPixConvTable Table = {Pg10ToUyvy10, Pg10ToUyvy8, Uyvy10ToPg10,
                                           Uyvy8ToYuv422p};
    return &Table;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Choice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasSsse3 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CPUID leaf 1, ECX bit 9.
//
#if defined(CDTAPI_HAVE_SSSE3)
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
const DtAvPixConvTable* DtAvPixConv_Ssse3(void)
{
#if defined(CDTAPI_HAVE_SSSE3)
    return HasSsse3() ? DtAvPixConv_Ssse3Unchecked() : NULL;
#else
    return NULL;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Xgetbv0 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The operating system's extended state: bits 1 and 2 are set when it saves the SSE and
// AVX registers on a thread switch. Only to be called when CPUID reports OSXSAVE.
//
#if defined(CDTAPI_HAVE_AVX2)
    #if defined(_MSC_VER)
static uint64_t Xgetbv0(void)
{
    return (uint64_t)_xgetbv(0);
}
    #else
__attribute__((target("xsave"))) static uint64_t Xgetbv0(void)
{
    return (uint64_t)_xgetbv(0);
}
    #endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasAvx2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// CPUID leaf 1, ECX bits 27 (OSXSAVE) and 28 (AVX), the operating system saving the AVX
// registers, and CPUID leaf 7, EBX bit 5 (AVX2).
//
static bool HasAvx2(void)
{
    #if defined(_MSC_VER)
    int Info[4] = {0};
    __cpuid(Info, 0);
    if (Info[0] < 7)
        return false;
    __cpuid(Info, 1);
    uint32_t Features = (uint32_t)Info[2];
    #else
    unsigned int Eax = 0;
    unsigned int Ebx = 0;
    unsigned int Ecx = 0;
    unsigned int Edx = 0;
    if (__get_cpuid_max(0, NULL) < 7 || __get_cpuid(1, &Eax, &Ebx, &Ecx, &Edx) == 0)
        return false;
    uint32_t Features = Ecx;
    #endif
    uint32_t OsAvx = (1u << 27) | (1u << 28);
    if ((Features & OsAvx) != OsAvx || (Xgetbv0() & 6u) != 6u)
        return false;
    #if defined(_MSC_VER)
    __cpuidex(Info, 7, 0);
    return ((uint32_t)Info[1] & (1u << 5)) != 0;
    #else
    __cpuid_count(7, 0, Eax, Ebx, Ecx, Edx);
    return (Ebx & (1u << 5)) != 0;
    #endif
}
#endif

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_Avx2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtAvPixConvTable* DtAvPixConv_Avx2(void)
{
#if defined(CDTAPI_HAVE_AVX2)
    return HasSsse3() && HasAvx2() ? DtAvPixConv_Avx2Unchecked() : NULL;
#else
    return NULL;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvPixConv_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtAvPixConvTable* DtAvPixConv_Best(void)
{
    const DtAvPixConvTable* Avx2 = DtAvPixConv_Avx2();
    if (Avx2 != NULL)
        return Avx2;
    const DtAvPixConvTable* Ssse3 = DtAvPixConv_Ssse3();
    return Ssse3 != NULL ? Ssse3 : DtAvPixConv_C();
}

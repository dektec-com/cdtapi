// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdi4k.c #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Which conversion of a 4K line the processor runs
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#if defined(CDTAPI_HAVE_SSSE3)
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#endif

// CDTAPI includes
#include "DtSdi4k.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Choice +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HasSsse3 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// CPUID leaf 1, ECX bit 9, as DtAvPixConv reads it.
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdi4kConv_Ssse3 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtSdi4kConv* DtSdi4kConv_Ssse3(void)
{
#if defined(CDTAPI_HAVE_SSSE3)
    return HasSsse3() ? DtSdi4kConv_Ssse3Table() : NULL;
#else
    return NULL;
#endif
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdi4kConv_Best -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtSdi4kConv* DtSdi4kConv_Best(void)
{
    const DtSdi4kConv* Ssse3 = DtSdi4kConv_Ssse3();

    return Ssse3 != NULL ? Ssse3 : DtSdi4kConv_C();
}

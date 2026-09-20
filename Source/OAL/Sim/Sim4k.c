// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# Sim4k.c #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - How the emulated card lays 2160p over one link out, in symbols
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// CDTAPI includes
#include "Sim4k.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The 4K layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Where the links lie in a raw line's eight streams: link 4 first, then 2, 3 and 1. The
// permutation is its own inverse, so it also says where link L's symbols go.
static const int g_LinkAt[4] = {3, 1, 2, 0};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sim4k_RawAt -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t Sim4k_RawAt(int Link, int Index)
{
    return (size_t)(8 * (Index / 2) + 4 * (Index % 2) + g_LinkAt[Link]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Where -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Where symbol Index of the line of link Link, from 0, lies in the raw line and in the
// coded line of its pair, links 1 and 2 taking the first coded line and 3 and 4 the
// second.
//
static void Where(int Link, int Index, int Hanc, int Act, bool Blanking, size_t* RawAt,
                  size_t* CodedAt)
{
    int Half = Link % 2;

    *RawAt = Sim4k_RawAt(Link, Index);
    if (Index < Hanc)
        *CodedAt = (size_t)(Half * Hanc + Index);
    else
    {
        int i = Index - Hanc;

        *CodedAt = (size_t)(2 * Hanc) +
                   (size_t)(Blanking ? Half * Act + i : (2 * (i / 4) + Half) * 4 + i % 4);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sim4k_Split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void Sim4k_Split(int Hanc, int Act, bool Blanking, const uint16_t* Raw, uint16_t* CodedA,
                 uint16_t* CodedB)
{
    for (int Link = 0; Link < 4; Link++)
    {
        uint16_t* Coded = Link < 2 ? CodedA : CodedB;

        for (int Index = 0; Index < Hanc + Act; Index++)
        {
            size_t RawAt, CodedAt;

            Where(Link, Index, Hanc, Act, Blanking, &RawAt, &CodedAt);
            Coded[CodedAt] = Raw[RawAt];
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Sim4k_Merge -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void Sim4k_Merge(int Hanc, int Act, bool Blanking, const uint16_t* CodedA,
                 const uint16_t* CodedB, uint16_t* Raw)
{
    for (int Link = 0; Link < 4; Link++)
    {
        const uint16_t* Coded = Link < 2 ? CodedA : CodedB;

        for (int Index = 0; Index < Hanc + Act; Index++)
        {
            size_t RawAt, CodedAt;

            Where(Link, Index, Hanc, Act, Blanking, &RawAt, &CodedAt);
            Raw[RawAt] = Coded[CodedAt];
        }
    }
}

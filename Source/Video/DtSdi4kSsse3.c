// #*#*#*#*#*#*#*#*#*#*#*#*#* DtSdi4kSsse3.c #*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The conversion of a 4K line between the ring and a raw frame, with SSSE3
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Built only for x86 processors, with SSSE3 enabled for this file alone: the library
// calls this conversion only after CPUID reports SSSE3.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>
#include <tmmintrin.h>

// CDTAPI includes
#include "DtSdi4k.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= SSSE3 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A step takes four pixels of each of the four links, eight symbols and ten bytes each,
// and makes the four groups of eight words the raw line carries them in, forty bytes.
// Symbol k of a link starts at bit 10k of its five-byte blocks, in byte 10k / 8 at bit
// 10k mod 8, which is 0, 2, 4 or 6: the two bytes around it form a word from which a
// multiply by 64, 16, 4 or 1 and a shift right by six cut the bits around the symbol.
// The reverse puts each symbol back into its two bytes; symbols 0, 2, 4 and 6 share no
// byte, nor do 1, 3, 5 and 7, so two shuffles and an OR assemble the ten bytes.
//
// A load and a store take sixteen bytes where ten or forty are wanted, so a step that
// would run past the end of a section or a line copies through a buffer instead.
//

// A shuffle index that gives zero.
#define Z -128

// The links, from 0, whose C words and then Y words take the four places of a group of
// eight words of a raw 4K line: links 4, 2, 3 and 1.
static const size_t g_LinkOrder[4] = {3, 1, 2, 0};

// The bytes the eight symbols a step takes from each link pack into.
#define STEP_BYTES 10

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unpack8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Eight symbols, each in its own word, from the bytes at Src that Pairs picks the two
// bytes of each symbol from. Reads sixteen bytes.
//
static __m128i Unpack8(const uint8_t* Src, __m128i Pairs)
{
    __m128i Bytes = _mm_loadu_si128((const __m128i*)Src);
    __m128i Words = _mm_shuffle_epi8(Bytes, Pairs);
    __m128i Moved = _mm_mullo_epi16(Words, _mm_set_epi16(1, 4, 16, 64, 1, 4, 16, 64));

    return _mm_and_si128(_mm_srli_epi16(Moved, 6), _mm_set1_epi16(0x3FF));
}

// The two bytes of each symbol of ten packed bytes, and of two blocks of five bytes ten
// bytes apart, which is how a picture line gives the two links of a coded line their
// pixel pairs.
static __m128i PairsRun(void)
{
    return _mm_set_epi8(9, 8, 8, 7, 7, 6, 6, 5, 4, 3, 3, 2, 2, 1, 1, 0);
}

static __m128i PairsSplit(void)
{
    return _mm_set_epi8(14, 13, 13, 12, 12, 11, 11, 10, 4, 3, 3, 2, 2, 1, 1, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- Pack8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The ten bytes of eight symbols, in the low ten bytes of the result.
//
static __m128i Pack8(__m128i Symbols)
{
    __m128i Moved = _mm_mullo_epi16(Symbols, _mm_set_epi16(64, 16, 4, 1, 64, 16, 4, 1));
    __m128i Even = _mm_shuffle_epi8(
        Moved, _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, Z, 5, 4, 1, 0));
    __m128i Odd = _mm_shuffle_epi8(
        Moved, _mm_set_epi8(Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, Z, 7, 6, 3, 2, Z));

    return _mm_or_si128(Even, Odd);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The four groups of eight words of four pixels, from the four links' symbols in the
// order the groups take them: the chrominance of the four links and then their
// luminance.
//
static void ToGroups(const __m128i Link[4], __m128i Group[4])
{
    const __m128i Split =
        _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0);
    __m128i W0 = _mm_shuffle_epi8(Link[0], Split);
    __m128i W1 = _mm_shuffle_epi8(Link[1], Split);
    __m128i W2 = _mm_shuffle_epi8(Link[2], Split);
    __m128i W3 = _mm_shuffle_epi8(Link[3], Split);
    __m128i C01 = _mm_unpacklo_epi16(W0, W1);
    __m128i C23 = _mm_unpacklo_epi16(W2, W3);
    __m128i Y01 = _mm_unpackhi_epi16(W0, W1);
    __m128i Y23 = _mm_unpackhi_epi16(W2, W3);
    __m128i C0 = _mm_unpacklo_epi32(C01, C23);
    __m128i C2 = _mm_unpackhi_epi32(C01, C23);
    __m128i Y0 = _mm_unpacklo_epi32(Y01, Y23);
    __m128i Y2 = _mm_unpackhi_epi32(Y01, Y23);

    Group[0] = _mm_unpacklo_epi64(C0, Y0);
    Group[1] = _mm_unpackhi_epi64(C0, Y0);
    Group[2] = _mm_unpacklo_epi64(C2, Y2);
    Group[3] = _mm_unpackhi_epi64(C2, Y2);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FromGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The four links' symbols from the four groups of eight words, the other way round.
//
static void FromGroups(const __m128i Group[4], __m128i Link[4])
{
    const __m128i Weave =
        _mm_set_epi8(15, 14, 7, 6, 13, 12, 5, 4, 11, 10, 3, 2, 9, 8, 1, 0);
    const __m128i EvenLow =
        _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 13, 12, 9, 8, 5, 4, 1, 0);
    const __m128i OddLow =
        _mm_set_epi8(Z, Z, Z, Z, Z, Z, Z, Z, 15, 14, 11, 10, 7, 6, 3, 2);
    __m128i C0 = _mm_unpacklo_epi64(Group[0], Group[1]);
    __m128i Y0 = _mm_unpackhi_epi64(Group[0], Group[1]);
    __m128i C2 = _mm_unpacklo_epi64(Group[2], Group[3]);
    __m128i Y2 = _mm_unpackhi_epi64(Group[2], Group[3]);
    __m128i Cx = _mm_shuffle_epi32(C0, _MM_SHUFFLE(3, 1, 2, 0));
    __m128i Cy = _mm_shuffle_epi32(C2, _MM_SHUFFLE(3, 1, 2, 0));
    __m128i Yx = _mm_shuffle_epi32(Y0, _MM_SHUFFLE(3, 1, 2, 0));
    __m128i Yy = _mm_shuffle_epi32(Y2, _MM_SHUFFLE(3, 1, 2, 0));
    __m128i C01 = _mm_unpacklo_epi64(Cx, Cy);
    __m128i C23 = _mm_unpackhi_epi64(Cx, Cy);
    __m128i Y01 = _mm_unpacklo_epi64(Yx, Yy);
    __m128i Y23 = _mm_unpackhi_epi64(Yx, Yy);

    __m128i W0 = _mm_unpacklo_epi64(_mm_shuffle_epi8(C01, EvenLow),
                                    _mm_shuffle_epi8(Y01, EvenLow));
    __m128i W1 =
        _mm_unpacklo_epi64(_mm_shuffle_epi8(C01, OddLow), _mm_shuffle_epi8(Y01, OddLow));
    __m128i W2 = _mm_unpacklo_epi64(_mm_shuffle_epi8(C23, EvenLow),
                                    _mm_shuffle_epi8(Y23, EvenLow));
    __m128i W3 =
        _mm_unpacklo_epi64(_mm_shuffle_epi8(C23, OddLow), _mm_shuffle_epi8(Y23, OddLow));

    Link[0] = _mm_shuffle_epi8(W0, Weave);
    Link[1] = _mm_shuffle_epi8(W1, Weave);
    Link[2] = _mm_shuffle_epi8(W2, Weave);
    Link[3] = _mm_shuffle_epi8(W3, Weave);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StoreGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The four groups into a raw line, forty bytes of ten-bit symbols or sixty-four of
// sixteen-bit ones. Safe copies through a buffer, for a step at the end of a line.
//
static void StoreGroups(const __m128i Group[4], int SymbolBits, uint8_t* Raw, bool Safe)
{
    if (SymbolBits == 16)
    {
        for (int i = 0; i < 4; i++)
            _mm_storeu_si128((__m128i*)(Raw + 16 * i), Group[i]);
        return;
    }

    uint8_t Buffer[64];
    uint8_t* Out = Safe ? Buffer : Raw;

    for (int i = 0; i < 4; i++)
        _mm_storeu_si128((__m128i*)(Out + 10 * i), Pack8(Group[i]));
    if (Safe)
        memcpy(Raw, Buffer, 40);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Store10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The ten bytes of eight symbols. A wide store writes six bytes more, which the bytes
// after them are; at the end of a section, where they are not, Safe copies them.
//
static void Store10(uint8_t* To, __m128i Packed, bool Safe)
{
    if (!Safe)
    {
        _mm_storeu_si128((__m128i*)To, Packed);
        return;
    }

    uint8_t Buffer[16];

    _mm_storeu_si128((__m128i*)Buffer, Packed);
    memcpy(To, Buffer, STEP_BYTES);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LoadGroups -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The four groups of a raw line. Safe copies through a buffer, for a step at the end of
// a line.
//
static void LoadGroups(const uint8_t* Raw, int SymbolBits, __m128i Group[4], bool Safe)
{
    if (SymbolBits == 16)
    {
        for (int i = 0; i < 4; i++)
        {
            Group[i] = _mm_and_si128(_mm_loadu_si128((const __m128i*)(Raw + 16 * i)),
                                     _mm_set1_epi16(0x3FF));
        }
        return;
    }

    uint8_t Buffer[64];
    const uint8_t* In = Raw;

    if (Safe)
    {
        memcpy(Buffer, Raw, 40);
        memset(Buffer + 40, 0, sizeof(Buffer) - 40);
        In = Buffer;
    }
    for (int i = 0; i < 4; i++)
        Group[i] = Unpack8(In + 10 * i, PairsRun());
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- TileC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// One tile without SSSE3, four symbols of each link and sixteen of the raw line: the
// tail of a section whose tiles do not pair up. Gathers when Gather is true and scatters
// otherwise.
//
static void TileC(const DtSdiFrameLayout* Layout, bool Blanking, size_t Tile,
                  uint8_t* CodedA, uint8_t* CodedB, int SymbolBits, uint8_t* RawLine,
                  bool Gather)
{
    const size_t RawBytes = SymbolBits == 16 ? 32 : 20;
    uint8_t* Raw = RawLine + Tile * RawBytes;
    uint16_t Symbols[16];
    size_t Offset[4];

    if (!Gather)
    {
        for (size_t i = 0; i < 16; i++)
        {
            const size_t Bit = i * 10;
            uint32_t Value;

            if (SymbolBits == 16)
                Value = (uint32_t)Raw[2 * i] | (uint32_t)Raw[2 * i + 1] << 8;
            else
            {
                Value = ((uint32_t)Raw[Bit / 8] | (uint32_t)Raw[Bit / 8 + 1] << 8) >>
                        (Bit % 8);
            }
            Symbols[i] = (uint16_t)(Value & 0x3FF);
        }
    }

    DtSdi4k_TileBlocks(Layout, Blanking, Tile, Offset);
    for (size_t p = 0; p < 4; p++)
    {
        const size_t L = g_LinkOrder[p];
        uint8_t* Block = (L < 2 ? CodedA : CodedB) + Offset[L];

        if (Gather)
        {
            uint64_t Word = (uint64_t)Block[0] | (uint64_t)Block[1] << 8 |
                            (uint64_t)Block[2] << 16 | (uint64_t)Block[3] << 24 |
                            (uint64_t)Block[4] << 32;

            Symbols[p] = (uint16_t)(Word & 0x3FF);
            Symbols[4 + p] = (uint16_t)(Word >> 10 & 0x3FF);
            Symbols[8 + p] = (uint16_t)(Word >> 20 & 0x3FF);
            Symbols[12 + p] = (uint16_t)(Word >> 30 & 0x3FF);
        }
        else
        {
            uint64_t Word = (uint64_t)Symbols[p] | (uint64_t)Symbols[4 + p] << 10 |
                            (uint64_t)Symbols[8 + p] << 20 |
                            (uint64_t)Symbols[12 + p] << 30;

            for (int b = 0; b < 5; b++)
                Block[b] = (uint8_t)(Word >> (8 * b));
        }
    }

    if (Gather)
    {
        if (SymbolBits == 16)
        {
            for (size_t i = 0; i < 16; i++)
            {
                Raw[2 * i] = (uint8_t)Symbols[i];
                Raw[2 * i + 1] = (uint8_t)(Symbols[i] >> 8);
            }
        }
        else
        {
            for (size_t i = 0; i < 16; i += 4, Raw += 5)
            {
                uint64_t Word = (uint64_t)Symbols[i] | (uint64_t)Symbols[i + 1] << 10 |
                                (uint64_t)Symbols[i + 2] << 20 |
                                (uint64_t)Symbols[i + 3] << 30;

                for (int b = 0; b < 5; b++)
                    Raw[b] = (uint8_t)(Word >> (8 * b));
            }
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinkSources -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Where the four links' ten bytes of the step that starts at tile Tile lie: a run of ten
// in the HANC sections and in the halves of a blanking line, two blocks of five ten
// bytes apart in the active part of a picture line, which is what Split says.
//
static void LinkSources(const DtSdiFrameLayout* Layout, bool Blanking, size_t Tile,
                        uint8_t* CodedA, uint8_t* CodedB, uint8_t* Src[4], bool* Split)
{
    size_t Offset[4];

    DtSdi4k_TileBlocks(Layout, Blanking, Tile, Offset);
    *Split = !Blanking && Tile >= (size_t)Layout->SectionSymsHanc / 4;
    for (size_t L = 0; L < 4; L++)
        Src[L] = (L < 2 ? CodedA : CodedB) + Offset[L];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConvertLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ConvertLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                        const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                        uint8_t* RawLine, uint16_t* Scratch)
{
    const bool Blanking = DtSdiFrame_IsBlankingLine(Layout, LineIndex);
    const size_t HancTiles = (size_t)Layout->SectionSymsHanc / 4;
    const size_t Tiles = (size_t)(Layout->LineSymsHanc + Layout->LineSymsVideo) / 16;
    const size_t RawBytes = SymbolBits == 16 ? 32 : 20;
    uint8_t* A = (uint8_t*)(uintptr_t)CodedA;
    uint8_t* B = (uint8_t*)(uintptr_t)CodedB;

    if (SymbolBits == 8)
    {
        DtSdi4kConv_C()->ConvertLine(Layout, SymbolBits, CodedA, CodedB, LineIndex,
                                     RawLine, Scratch);
        return;
    }

    for (int Region = 0; Region < 2; Region++)
    {
        const size_t First = Region == 0 ? 0 : HancTiles;
        const size_t End = Region == 0 ? HancTiles : Tiles;
        size_t Tile = First;

        for (; Tile + 2 <= End; Tile += 2)
        {
            const bool Edge = Tile + 2 >= End;
            uint8_t* Src[4];
            bool Split = false;
            __m128i Link[4];
            __m128i Group[4];
            uint8_t Buffer[4][16];

            LinkSources(Layout, Blanking, Tile, A, B, Src, &Split);
            const __m128i Pairs = Split ? PairsSplit() : PairsRun();
            for (size_t p = 0; p < 4; p++)
            {
                const uint8_t* From = Src[g_LinkOrder[p]];

                if (Edge)
                {
                    memset(Buffer[p], 0, sizeof(Buffer[p]));
                    memcpy(Buffer[p], From, Split ? 15 : STEP_BYTES);
                    From = Buffer[p];
                }
                Link[p] = Unpack8(From, Pairs);
            }
            ToGroups(Link, Group);
            StoreGroups(Group, SymbolBits, RawLine + Tile * RawBytes,
                        Edge && Region == 1);
        }
        if (Tile < End)
            TileC(Layout, Blanking, Tile, A, B, SymbolBits, RawLine, true);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CodeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void CodeLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                     const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                     uint8_t* CodedB, uint16_t* Scratch)
{
    const bool Blanking = DtSdiFrame_IsBlankingLine(Layout, LineIndex);
    const size_t HancTiles = (size_t)Layout->SectionSymsHanc / 4;
    const size_t Tiles = (size_t)(Layout->LineSymsHanc + Layout->LineSymsVideo) / 16;
    const size_t RawBytes = SymbolBits == 16 ? 32 : 20;
    uint8_t* Raw = (uint8_t*)(uintptr_t)RawLine;

    if (SymbolBits == 8)
    {
        DtSdi4kConv_C()->CodeLine(Layout, SymbolBits, RawLine, LineIndex, CodedA, CodedB,
                                  Scratch);
        return;
    }

    for (int Region = 0; Region < 2; Region++)
    {
        const size_t First = Region == 0 ? 0 : HancTiles;
        const size_t End = Region == 0 ? HancTiles : Tiles;
        size_t Tile = First;

        for (; Tile + 2 <= End; Tile += 2)
        {
            const bool Edge = Tile + 2 >= End;
            uint8_t* Src[4];
            bool Split = false;
            __m128i Link[4];
            __m128i Group[4];

            LinkSources(Layout, Blanking, Tile, CodedA, CodedB, Src, &Split);
            LoadGroups(Raw + Tile * RawBytes, SymbolBits, Group, Edge && Region == 1);
            FromGroups(Group, Link);
            if (Split)
            {
                // The two links of a coded line share twenty bytes, four symbols of each
                // in turn, so the pair is written as two runs of ten.
                for (size_t Pair = 0; Pair < 2; Pair++)
                {
                    const __m128i Even = Link[g_LinkOrder[2 * Pair]];
                    const __m128i Odd = Link[g_LinkOrder[2 * Pair + 1]];
                    uint8_t* To = Src[2 * Pair];

                    Store10(To, Pack8(_mm_unpacklo_epi64(Even, Odd)), false);
                    Store10(To + STEP_BYTES, Pack8(_mm_unpackhi_epi64(Even, Odd)), Edge);
                }
            }
            else
            {
                for (size_t p = 0; p < 4; p++)
                    Store10(Src[g_LinkOrder[p]], Pack8(Link[p]), Edge);
            }
        }
        if (Tile < End)
            TileC(Layout, Blanking, Tile, CodedA, CodedB, SymbolBits, Raw, false);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdi4kConv_Ssse3Table -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const DtSdi4kConv* DtSdi4kConv_Ssse3Table(void)
{
    static const DtSdi4kConv Table = {ConvertLine, CodeLine};
    return &Table;
}

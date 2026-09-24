// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The firmware's coded SDI frames, the raw SDI frame, and black frames
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Working memory of a black 4K frame.
#include "DtFrameProps.h" // Frame geometry.
#include "DtSdi4k.h"      // The conversion of a 4K line, per instruction set.
#include "DtSdiFrame.h"   // Interface being implemented.
#include "DtVidStd.h"     // Which standards are 4K.
#include "cdtapi.h"       // DTAPI_VIDSTD_ codes and result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Coded frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PadToAlignment -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes Symbols 10-bit symbols take, padded to Alignment bytes.
//
static int PadToAlignment(int Symbols, int Alignment)
{
    int Bits = Symbols * 10;
    int AlignmentInBits = Alignment * 8;

    return (Bits + AlignmentInBits - 1) / AlignmentInBits * Alignment;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AlignUp -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int AlignUp(int Bytes, int Alignment)
{
    return (Bytes + Alignment - 1) / Alignment * Alignment;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_LayoutInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The frame properties of a 2160p standard describe one of its links.
//
bool DtSdiFrame_LayoutInit(DtSdiFrameLayout* Layout, int VidStd, int AlignmentInBits)
{
    memset(Layout, 0, sizeof(*Layout));
    Layout->VidStd = DTAPI_VIDSTD_UNKNOWN;

    const DtVidStdInfo* Info = DtVidStd_Find(VidStd);
    const bool Is4k = DtVidStd_Is4k(VidStd);
    DtFrameProps Props;
    if (AlignmentInBits <= 0 || AlignmentInBits % 8 != 0 || Info == NULL ||
        (Is4k && Info->IsLevelB) || !DtFrameProps_Init(&Props, VidStd))
    {
        return false;
    }

    const int Links = Is4k ? 4 : 1;
    Layout->Is4k = Is4k;
    Layout->Alignment = AlignmentInBits / 8;
    Layout->HeaderNumBytes = AlignUp(DT_SDIFRAME_HEADER_BYTES, Layout->Alignment);
    Layout->TxHeaderNumBytes = AlignUp(DT_SDIFRAME_TX_HEADER_BYTES, Layout->Alignment);
    Layout->NumLines = DtFrameProps_NumLines(&Props);
    Layout->NumCodedLines = Layout->NumLines * (Is4k ? 2 : 1);
    Layout->NumHancSections = Is4k ? 2 : 1;
    Layout->SectionNumSymsHanc = DtFrameProps_LineSymbolsHanc(&Props);
    Layout->SectionNumSymsVideo = Props.LineNumSymVanc * (Is4k ? 2 : 1);
    Layout->LineNumSymsHanc = Layout->SectionNumSymsHanc * Links;
    Layout->LineNumSymsVideo = Props.LineNumSymVanc * Links;
    Layout->SectionBytesHanc =
        PadToAlignment(Layout->SectionNumSymsHanc, Layout->Alignment);
    Layout->SectionBytesVideo =
        PadToAlignment(Layout->SectionNumSymsVideo, Layout->Alignment);
    Layout->Stride =
        Layout->NumHancSections * Layout->SectionBytesHanc + Layout->SectionBytesVideo;
    Layout->TxLineHeaderNumBytes = Is4k ? AlignUp(4, Layout->Alignment) : 0;
    Layout->TxStride = Layout->TxLineHeaderNumBytes + Layout->Stride;
    Layout->PictureStart = Props.Fields[0].VidStartLine;
    Layout->PictureEnd = Props.Fields[0].VidEndLine;
    Layout->Format =
        Is4k ? DT_SDIFRAME_FORMAT_UNCOMPRESSED_4K : DT_SDIFRAME_FORMAT_UNCOMPRESSED;
    if (Is4k)
        Layout->SdiRate =
            Info->IoStd == DTAPI_IOCONFIG_12GSDI ? DT_SDIRATE_12G : DT_SDIRATE_6G;
    else if (DtFrameProps_IsSd(&Props))
        Layout->SdiRate = DT_SDIRATE_SD;
    else if (DtFrameProps_Is3g(&Props))
        Layout->SdiRate = DT_SDIRATE_3G;
    else
        Layout->SdiRate = DT_SDIRATE_HD;
    Layout->VidStd = VidStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_CodedSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtSdiFrame_CodedSize(const DtSdiFrameLayout* Layout)
{
    return (size_t)Layout->HeaderNumBytes +
           (size_t)Layout->NumCodedLines * (size_t)Layout->Stride;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_TxCodedSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtSdiFrame_TxCodedSize(const DtSdiFrameLayout* Layout)
{
    return (size_t)Layout->TxHeaderNumBytes +
           (size_t)Layout->NumCodedLines * (size_t)Layout->TxStride;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Read32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t Read32(const uint8_t* Bytes)
{
    return (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8 | (uint32_t)Bytes[2] << 16 |
           (uint32_t)Bytes[3] << 24;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Write32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Write32(uint8_t* Bytes, uint32_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
    Bytes[2] = (uint8_t)(Value >> 16);
    Bytes[3] = (uint8_t)(Value >> 24);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_DecodeHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The second word's fields are packed from the least significant bit upwards.
//
void DtSdiFrame_DecodeHeader(const uint8_t* Bytes, DtSdiFrameHeader* Header)
{
    uint32_t Word = Read32(Bytes + 4);

    Header->SyncWord = Read32(Bytes);
    Header->ProtocolVersion = (int)(Word & 0xF);
    Header->Format = (int)((Word >> 4) & 0xF);
    Header->FrameId = (int)(Word >> 16);
    Header->PtpSeconds = Read32(Bytes + 8);
    Header->PtpNanoseconds = Read32(Bytes + 12);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_EncodeHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiFrame_EncodeHeader(const DtSdiFrameHeader* Header, uint8_t* Bytes)
{
    uint32_t Word = ((uint32_t)Header->ProtocolVersion & 0xF) |
                    ((uint32_t)Header->Format & 0xF) << 4 |
                    ((uint32_t)Header->FrameId & 0xFFFF) << 16;

    Write32(Bytes, Header->SyncWord);
    Write32(Bytes + 4, Word);
    Write32(Bytes + 8, Header->PtpSeconds);
    Write32(Bytes + 12, Header->PtpNanoseconds);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_CheckHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiFrame_CheckHeader(const DtSdiFrameLayout* Layout,
                                   const DtSdiFrameHeader* Header, int ExpectedId)
{
    if (Header->SyncWord != DT_SDIFRAME_SYNC_WORD)
        return DTAPI_E_OUT_OF_SYNC;
    if (ExpectedId != -1 && Header->FrameId != ExpectedId)
        return DTAPI_E_INVALID;
    if (Header->Format != Layout->Format)
        return DTAPI_E_INVALID_FORMAT;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_TxHeaderInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The header's SDI rate takes the driver's DT_DRV_SDIRATE_ values, which DT_SDIRATE_
// equals. Every header counts the coded lines and gives the sizes of one HANC section and
// of the video section; a 4K coded line holds two HANC sections.
//
void DtSdiFrame_TxHeaderInit(const DtSdiFrameLayout* Layout, int FrameId,
                             DtSdiFrameTxHeader* Header)
{
    Header->SyncWord = DT_SDIFRAME_SYNC_WORD;
    Header->ProtocolVersion = 0;
    Header->Format = Layout->Format;
    Header->SdiRateValid = true;
    Header->SdiRate = Layout->SdiRate;
    Header->FrameId = FrameId & 0xFFFF;
    Header->NumCodedLines = Layout->NumCodedLines;
    Header->NumWordsHanc = Layout->SectionBytesHanc / Layout->Alignment;
    Header->NumSymsHanc = Layout->SectionNumSymsHanc;
    Header->NumWordsVideo = Layout->SectionBytesVideo / Layout->Alignment;
    Header->NumSymsVideo = Layout->SectionNumSymsVideo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_IsBlankingLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSdiFrame_IsBlankingLine(const DtSdiFrameLayout* Layout, int LineIndex)
{
    int Line = LineIndex + 1;
    return Layout->Is4k && (Line < Layout->PictureStart || Line > Layout->PictureEnd);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_EncodeTxLineHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiFrame_EncodeTxLineHeader(const DtSdiFrameLayout* Layout, int CodedIndex,
                                   uint8_t* Bytes)
{
    if (!Layout->Is4k)
        return;
    memset(Bytes, 0, (size_t)Layout->TxLineHeaderNumBytes);
    Bytes[0] = DtSdiFrame_IsBlankingLine(Layout, CodedIndex / 2) ? 1 : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_DecodeTxHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Bit-fields as in the receive header, allocated from the least significant bit.
//
void DtSdiFrame_DecodeTxHeader(const uint8_t* Bytes, DtSdiFrameTxHeader* Header)
{
    uint32_t Word1 = Read32(Bytes + 4);
    uint32_t Word2 = Read32(Bytes + 8);
    uint32_t Word3 = Read32(Bytes + 12);
    uint32_t Word4 = Read32(Bytes + 16);

    Header->SyncWord = Read32(Bytes);
    Header->ProtocolVersion = (int)(Word1 & 0xF);
    Header->Format = (int)((Word1 >> 4) & 0xF);
    Header->SdiRateValid = ((Word1 >> 8) & 1) != 0;
    Header->SdiRate = (int)((Word1 >> 9) & 0x7);
    Header->FrameId = (int)(Word2 & 0xFFFF);
    Header->NumCodedLines = (int)(Word2 >> 16);
    Header->NumWordsHanc = (int)(Word3 & 0xFFFF);
    Header->NumSymsHanc = (int)(Word3 >> 16);
    Header->NumWordsVideo = (int)(Word4 & 0xFFFF);
    Header->NumSymsVideo = (int)(Word4 >> 16);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_EncodeTxHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiFrame_EncodeTxHeader(const DtSdiFrameTxHeader* Header, uint8_t* Bytes)
{
    uint32_t Word1 = ((uint32_t)Header->ProtocolVersion & 0xF) |
                     ((uint32_t)Header->Format & 0xF) << 4 |
                     (Header->SdiRateValid ? 1u : 0u) << 8 |
                     ((uint32_t)Header->SdiRate & 0x7) << 9;

    Write32(Bytes, Header->SyncWord);
    Write32(Bytes + 4, Word1);
    Write32(Bytes + 8, ((uint32_t)Header->FrameId & 0xFFFF) |
                           ((uint32_t)Header->NumCodedLines & 0xFFFF) << 16);
    Write32(Bytes + 12, ((uint32_t)Header->NumWordsHanc & 0xFFFF) |
                            ((uint32_t)Header->NumSymsHanc & 0xFFFF) << 16);
    Write32(Bytes + 16, ((uint32_t)Header->NumWordsVideo & 0xFFFF) |
                            ((uint32_t)Header->NumSymsVideo & 0xFFFF) << 16);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_RawSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The frame's symbols, at BitsPerSymbol each, padded to an alignment of 64 bits.
//
size_t DtSdiFrame_RawSize(const DtSdiFrameLayout* Layout, int BitsPerSymbol)
{
    size_t Symbols = (size_t)Layout->NumLines *
                     (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo);

    if (BitsPerSymbol != 8 && BitsPerSymbol != 10 && BitsPerSymbol != 16)
        return 0;

    return (Symbols * (size_t)BitsPerSymbol + 63) / 64 * 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_RawLineNumBits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtSdiFrame_RawLineNumBits(const DtSdiFrameLayout* Layout, int BitsPerSymbol)
{
    if (BitsPerSymbol != 8 && BitsPerSymbol != 10 && BitsPerSymbol != 16)
        return 0;
    return (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo) *
           (size_t)BitsPerSymbol;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Symbol Index of a packed 10-bit section. Reads no byte beyond the symbol.
//
static uint32_t ReadSymbol(const uint8_t* Section, size_t Index)
{
    size_t Bit = Index * 10;
    size_t Byte = Bit / 8;
    uint32_t Shift = (uint32_t)(Bit % 8);
    uint32_t Value = (uint32_t)Section[Byte] >> Shift;
    uint32_t Have = 8 - Shift;

    while (Have < 10)
    {
        Value |= (uint32_t)Section[++Byte] << Have;
        Have += 8;
    }
    return Value & 0x3FF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OrBits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds the Count low bits of Value to Raw at bit position Bit, into bits that are zero.
//
static void OrBits(uint8_t* Raw, size_t Bit, uint32_t Value, uint32_t Count)
{
    size_t Byte = Bit / 8;
    uint32_t Shift = (uint32_t)(Bit % 8);
    uint32_t Bits = ((uint32_t)Value & ((1u << Count) - 1)) << Shift;

    for (Count += Shift; Count > 0; Count = Count > 8 ? Count - 8 : 0)
    {
        Raw[Byte++] |= (uint8_t)Bits;
        Bits >>= 8;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopySection10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A packed section to its bit position in a 10-bit raw frame. Whole bytes are copied
// when the position is on a byte boundary; the rest goes bit by bit.
//
static void CopySection10(const uint8_t* Section, size_t Symbols, uint8_t* Raw,
                          size_t Bit)
{
    size_t Bits = Symbols * 10;

    if (Bit % 8 == 0)
    {
        size_t Whole = Bits / 8;

        memcpy(Raw + Bit / 8, Section, Whole);
        if (Bits % 8 != 0)
            OrBits(Raw, Bit + Whole * 8, Section[Whole], (uint32_t)(Bits % 8));
        return;
    }

    for (size_t i = 0; i < Symbols; i++)
        OrBits(Raw, Bit + i * 10, ReadSymbol(Section, i), 10);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopySection8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Four symbols take five bytes, so the section is taken five bytes at a time, and the
// symbols left over one by one.
//
static void CopySection8(const uint8_t* Section, size_t Symbols, uint8_t* Raw)
{
    size_t Groups = Symbols / 4;
    size_t i;

    for (i = 0; i < Groups; i++)
    {
        const uint8_t* In = Section + 5 * i;
        uint32_t Word = Read32(In);
        uint8_t* Out = Raw + 4 * i;

        Out[0] = (uint8_t)(Word >> 2);
        Out[1] = (uint8_t)(Word >> 12);
        Out[2] = (uint8_t)(Word >> 22);
        Out[3] = In[4];
    }

    for (i = 4 * Groups; i < Symbols; i++)
        Raw[i] = (uint8_t)(ReadSymbol(Section, i) >> 2);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopySection16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Five bytes at a time as well.
//
static void CopySection16(const uint8_t* Section, size_t Symbols, uint8_t* Raw)
{
    size_t Groups = Symbols / 4;
    size_t i;

    for (i = 0; i < Groups; i++)
    {
        const uint8_t* In = Section + 5 * i;
        uint32_t Word = Read32(In);
        uint8_t* Out = Raw + 8 * i;
        uint32_t Values[4];

        Values[0] = Word & 0x3FF;
        Values[1] = (Word >> 10) & 0x3FF;
        Values[2] = (Word >> 20) & 0x3FF;
        Values[3] = (Word >> 30) | (uint32_t)In[4] << 2;
        for (int j = 0; j < 4; j++)
        {
            Out[2 * j] = (uint8_t)Values[j];
            Out[2 * j + 1] = (uint8_t)(Values[j] >> 8);
        }
    }

    for (i = 4 * Groups; i < Symbols; i++)
    {
        uint32_t Value = ReadSymbol(Section, i);

        Raw[2 * i] = (uint8_t)Value;
        Raw[2 * i + 1] = (uint8_t)(Value >> 8);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_ConvertLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The line starts at LineIndex times the bits of a line.
//
void DtSdiFrame_ConvertLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                            const uint8_t* CodedLine, int LineIndex, uint8_t* Raw)
{
    size_t Hanc = (size_t)Layout->LineNumSymsHanc;
    size_t Video = (size_t)Layout->LineNumSymsVideo;
    size_t Start = (size_t)LineIndex * (Hanc + Video);
    const uint8_t* VideoSection = CodedLine + Layout->SectionBytesHanc;

    switch (BitsPerSymbol)
    {
    case 8:
        CopySection8(CodedLine, Hanc, Raw + Start);
        CopySection8(VideoSection, Video, Raw + Start + Hanc);
        break;
    case 10:
        CopySection10(CodedLine, Hanc, Raw, Start * 10);
        CopySection10(VideoSection, Video, Raw, (Start + Hanc) * 10);
        break;
    case 16:
        CopySection16(CodedLine, Hanc, Raw + 2 * Start);
        CopySection16(VideoSection, Video, Raw + 2 * (Start + Hanc));
        break;
    default:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyBits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies Count bits, from bit Bit of In, to the start of the Bytes bytes at Out, and
// clears the bits of Out after them. Whole bytes are copied when Bit is on a byte
// boundary; otherwise each byte of Out takes the upper bits of one byte of In and the
// lower bits of the next. No byte of In after the one holding the last bit is read.
//
static void CopyBits(const uint8_t* In, size_t Bit, size_t Count, uint8_t* Out,
                     size_t Bytes)
{
    const uint8_t* Src = In + Bit / 8;
    uint32_t Shift = (uint32_t)(Bit % 8);
    size_t Whole = Count / 8;
    uint32_t Rest = (uint32_t)(Count % 8);

    if (Shift == 0)
        memcpy(Out, Src, Whole);
    else
    {
        for (size_t i = 0; i < Whole; i++)
        {
            uint32_t Pair = (uint32_t)Src[i] | (uint32_t)Src[i + 1] << 8;

            Out[i] = (uint8_t)(Pair >> Shift);
        }
    }
    memset(Out + Whole, 0, Bytes - Whole);

    if (Rest != 0)
    {
        uint32_t Value = (uint32_t)Src[Whole] >> Shift;

        if (Shift + Rest > 8)
            Value |= (uint32_t)Src[Whole + 1] << (8 - Shift);
        Out[Whole] = (uint8_t)(Value & ((1u << Rest) - 1));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs Count 8-bit symbols from In, each shifted up by two bits, into the Bytes bytes at
// Out, and clears the bits of Out after them.
//
static void Pack8(const uint8_t* In, size_t Count, uint8_t* Out, size_t Bytes)
{
    uint64_t Accu = 0;
    uint32_t Have = 0;
    size_t Byte = 0;

    memset(Out, 0, Bytes);
    for (size_t i = 0; i < Count; i++)
    {
        Accu |= (uint64_t)In[i] << (Have + 2);
        Have += 10;
        while (Have >= 8)
        {
            Out[Byte++] = (uint8_t)Accu;
            Accu >>= 8;
            Have -= 8;
        }
    }
    if (Have > 0)
        Out[Byte] = (uint8_t)Accu;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Symbol16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The lower ten bits of the 16-bit symbol at Bytes.
//
static uint64_t Symbol16(const uint8_t* Bytes)
{
    return ((uint64_t)Bytes[0] | (uint64_t)Bytes[1] << 8) & 0x3FF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Packs Count 16-bit symbols from In into the Bytes bytes at Out, and clears the bits of
// Out after them. Four symbols make five bytes; the symbols left over go one by one.
//
static void Pack16(const uint8_t* In, size_t Count, uint8_t* Out, size_t Bytes)
{
    size_t Groups = Count / 4;
    size_t Byte = 5 * Groups;
    uint64_t Accu = 0;
    uint32_t Have = 0;
    size_t i;

    for (i = 0; i < Groups; i++)
    {
        const uint8_t* Syms = In + 8 * i;
        uint8_t* Dst = Out + 5 * i;
        uint64_t Word = Symbol16(Syms) | Symbol16(Syms + 2) << 10 |
                        Symbol16(Syms + 4) << 20 | Symbol16(Syms + 6) << 30;

        Dst[0] = (uint8_t)Word;
        Dst[1] = (uint8_t)(Word >> 8);
        Dst[2] = (uint8_t)(Word >> 16);
        Dst[3] = (uint8_t)(Word >> 24);
        Dst[4] = (uint8_t)(Word >> 32);
    }
    memset(Out + Byte, 0, Bytes - Byte);

    for (i = 4 * Groups; i < Count; i++)
    {
        Accu |= Symbol16(In + 2 * i) << Have;
        Have += 10;
        while (Have >= 8)
        {
            Out[Byte++] = (uint8_t)Accu;
            Accu >>= 8;
            Have -= 8;
        }
    }
    if (Have > 0)
        Out[Byte] = (uint8_t)Accu;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_CodeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The video section starts where the HANC section ends, which in a line of 10-bit symbols
// need not be a byte boundary either.
//
bool DtSdiFrame_CodeLine(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                         const uint8_t* RawLine, int Phase, uint8_t* CodedLine)
{
    size_t Hanc = (size_t)Layout->LineNumSymsHanc;
    size_t Video = (size_t)Layout->LineNumSymsVideo;
    uint8_t* VideoSection = CodedLine + Layout->SectionBytesHanc;

    if (Phase < 0 || Phase > 7)
        return false;

    switch (BitsPerSymbol)
    {
    case 10:
        CopyBits(RawLine, (size_t)Phase, Hanc * 10, CodedLine,
                 (size_t)Layout->SectionBytesHanc);
        CopyBits(RawLine, (size_t)Phase + Hanc * 10, Video * 10, VideoSection,
                 (size_t)Layout->SectionBytesVideo);
        return true;
    case 8:
        if (Phase != 0)
            return false;
        Pack8(RawLine, Hanc, CodedLine, (size_t)Layout->SectionBytesHanc);
        Pack8(RawLine + Hanc, Video, VideoSection, (size_t)Layout->SectionBytesVideo);
        return true;
    case 16:
        if (Phase != 0)
            return false;
        Pack16(RawLine, Hanc, CodedLine, (size_t)Layout->SectionBytesHanc);
        Pack16(RawLine + 2 * Hanc, Video, VideoSection,
               (size_t)Layout->SectionBytesVideo);
        return true;
    default:
        return false;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= 4K lines +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The tiles are described in DtSdi4k.h. The scratch buffer holds the raw line's symbols,
// which one pass then writes in the symbol size of the raw frame.
//

// The links, from 0, whose C words and then Y words take the four places of a group of
// eight words of a raw 4K line: links 4, 2, 3 and 1.
static const size_t g_LinkOrder[4] = {3, 1, 2, 0};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Unpack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Count packed 10-bit symbols from In, a multiple of four.
//
static void Unpack10(const uint8_t* In, size_t Count, uint16_t* Out)
{
    for (size_t i = 0; i < Count; i += 4, In += 5)
    {
        uint64_t Word = (uint64_t)In[0] | (uint64_t)In[1] << 8 | (uint64_t)In[2] << 16 |
                        (uint64_t)In[3] << 24 | (uint64_t)In[4] << 32;

        Out[i] = (uint16_t)(Word & 0x3FF);
        Out[i + 1] = (uint16_t)(Word >> 10 & 0x3FF);
        Out[i + 2] = (uint16_t)(Word >> 20 & 0x3FF);
        Out[i + 3] = (uint16_t)(Word >> 30 & 0x3FF);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Pack10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Packs Count 10-bit symbols, a multiple of four, into Count * 10 / 8 bytes at Out.
//
static void Pack10(const uint16_t* In, size_t Count, uint8_t* Out)
{
    for (size_t i = 0; i < Count; i += 4, Out += 5)
    {
        uint64_t Word = (uint64_t)(In[i] & 0x3FF) | (uint64_t)(In[i + 1] & 0x3FF) << 10 |
                        (uint64_t)(In[i + 2] & 0x3FF) << 20 |
                        (uint64_t)(In[i + 3] & 0x3FF) << 30;

        for (int b = 0; b < 5; b++)
            Out[b] = (uint8_t)(Word >> (8 * b));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ClearPadding -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The bytes of a section of Bytes bytes that its Count packed symbols do not reach.
//
static void ClearPadding(uint8_t* Section, size_t Count, size_t Bytes)
{
    memset(Section + Count * 10 / 8, 0, Bytes - Count * 10 / 8);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadRaw -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Count symbols of a raw line whose symbols take BitsPerSymbol, 8, 10 or 16.
//
static void ReadRaw(const uint8_t* Raw, int BitsPerSymbol, size_t Count, uint16_t* Out)
{
    if (BitsPerSymbol == 10)
        Unpack10(Raw, Count, Out);
    else if (BitsPerSymbol == 16)
    {
        for (size_t i = 0; i < Count; i++)
            Out[i] = (uint16_t)(((uint32_t)Raw[2 * i] | (uint32_t)Raw[2 * i + 1] << 8) &
                                0x3FF);
    }
    else
    {
        for (size_t i = 0; i < Count; i++)
            Out[i] = (uint16_t)(Raw[i] << 2);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteRaw -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Count symbols into a raw line whose symbols take BitsPerSymbol, 8, 10 or 16.
//
static void WriteRaw(const uint16_t* In, int BitsPerSymbol, size_t Count, uint8_t* Raw)
{
    if (BitsPerSymbol == 10)
        Pack10(In, Count, Raw);
    else if (BitsPerSymbol == 16)
    {
        for (size_t i = 0; i < Count; i++)
        {
            Raw[2 * i] = (uint8_t)In[i];
            Raw[2 * i + 1] = (uint8_t)(In[i] >> 8);
        }
    }
    else
    {
        for (size_t i = 0; i < Count; i++)
            Raw[i] = (uint8_t)(In[i] >> 2);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GatherLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The symbols of a raw 4K line from the two coded lines it is made of.
//
static void GatherLine(const DtSdiFrameLayout* Layout, const uint8_t* CodedA,
                       const uint8_t* CodedB, int LineIndex, uint16_t* Raw)
{
    const bool Blanking = DtSdiFrame_IsBlankingLine(Layout, LineIndex);
    const size_t Tiles =
        (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo) / 16;

    for (size_t Tile = 0; Tile < Tiles; Tile++, Raw += 16)
    {
        size_t Offset[4];

        DtSdi4k_TileBlocks(Layout, Blanking, Tile, Offset);
        for (size_t p = 0; p < 4; p++)
        {
            const size_t L = g_LinkOrder[p];
            const uint8_t* Src = (L < 2 ? CodedA : CodedB) + Offset[L];
            uint16_t Four[4];

            Unpack10(Src, 4, Four);
            Raw[p] = Four[0];
            Raw[4 + p] = Four[1];
            Raw[8 + p] = Four[2];
            Raw[12 + p] = Four[3];
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ScatterLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The symbols of a raw 4K line into the two coded lines it is made of.
//
static void ScatterLine(const DtSdiFrameLayout* Layout, const uint16_t* Raw,
                        int LineIndex, uint8_t* CodedA, uint8_t* CodedB)
{
    const bool Blanking = DtSdiFrame_IsBlankingLine(Layout, LineIndex);
    const size_t Tiles =
        (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo) / 16;

    for (size_t Tile = 0; Tile < Tiles; Tile++, Raw += 16)
    {
        size_t Offset[4];

        DtSdi4k_TileBlocks(Layout, Blanking, Tile, Offset);
        for (size_t p = 0; p < 4; p++)
        {
            const size_t L = g_LinkOrder[p];
            uint8_t* Dst = (L < 2 ? CodedA : CodedB) + Offset[L];
            const uint16_t Four[4] = {Raw[p], Raw[4 + p], Raw[8 + p], Raw[12 + p]};

            Pack10(Four, 4, Dst);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_BandLineStep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSdiFrame_BandLineStep(const DtSdiFrameLayout* Layout, int BitsPerSymbol)
{
    const size_t Bits = DtSdiFrame_RawLineNumBits(Layout, BitsPerSymbol);
    int Lines = 1;

    // Eight lines of any whole number of bits make whole bytes, so the search ends.
    while (Lines < 8 && Bits * (size_t)Lines % 8 != 0)
        Lines++;
    return Lines;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_NumScratchSymbols -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
size_t DtSdiFrame_NumScratchSymbols(const DtSdiFrameLayout* Layout)
{
    if (!Layout->Is4k)
        return 0;
    return (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConvertLineC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The portable conversion of DtSdi4kConv_C: gathers the line into Scratch, then writes it
// in the raw frame's symbol size.
//
static void ConvertLineC(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                         const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                         uint8_t* RawLine, uint16_t* Scratch)
{
    const size_t LineSyms = (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo);

    GatherLine(Layout, CodedA, CodedB, LineIndex, Scratch);
    WriteRaw(Scratch, BitsPerSymbol, LineSyms, RawLine);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_ConvertLine4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiFrame_ConvertLine4k(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                              const uint8_t* CodedA, const uint8_t* CodedB, int LineIndex,
                              uint8_t* RawLine, uint16_t* Scratch)
{
    if (!Layout->Is4k ||
        (BitsPerSymbol != 8 && BitsPerSymbol != 10 && BitsPerSymbol != 16))
        return;

    DtSdi4kConv_Best()->ConvertLine(Layout, BitsPerSymbol, CodedA, CodedB, LineIndex,
                                    RawLine, Scratch);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CodeLineC -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The portable coding of DtSdi4kConv_C: reads the raw line into Scratch, then scatters
// it over the two coded lines. The sections' padding is left as it was.
//
static void CodeLineC(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                      const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                      uint8_t* CodedB, uint16_t* Scratch)
{
    const size_t LineSyms = (size_t)(Layout->LineNumSymsHanc + Layout->LineNumSymsVideo);

    ReadRaw(RawLine, BitsPerSymbol, LineSyms, Scratch);
    ScatterLine(Layout, Scratch, LineIndex, CodedA, CodedB);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdi4kConv_C -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const DtSdi4kConv* DtSdi4kConv_C(void)
{
    static const DtSdi4kConv Table = {ConvertLineC, CodeLineC};
    return &Table;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_CodeLine4k -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
bool DtSdiFrame_CodeLine4k(const DtSdiFrameLayout* Layout, int BitsPerSymbol,
                           const uint8_t* RawLine, int LineIndex, uint8_t* CodedA,
                           uint8_t* CodedB, uint16_t* Scratch)
{
    const size_t SectionHanc = (size_t)Layout->SectionNumSymsHanc;
    const size_t SectionVideo = (size_t)Layout->SectionNumSymsVideo;
    const size_t HancNumBytes = (size_t)Layout->SectionBytesHanc;
    const size_t VideoBytes = (size_t)Layout->SectionBytesVideo;

    if (!Layout->Is4k ||
        (BitsPerSymbol != 8 && BitsPerSymbol != 10 && BitsPerSymbol != 16))
        return false;

    DtSdi4kConv_Best()->CodeLine(Layout, BitsPerSymbol, RawLine, LineIndex, CodedA,
                                 CodedB, Scratch);

    // The padding of every section, which its symbols do not reach.
    ClearPadding(CodedA, SectionHanc, HancNumBytes);
    ClearPadding(CodedA + HancNumBytes, SectionHanc, HancNumBytes);
    ClearPadding(CodedB, SectionHanc, HancNumBytes);
    ClearPadding(CodedB + HancNumBytes, SectionHanc, HancNumBytes);
    ClearPadding(CodedA + 2 * HancNumBytes, SectionVideo, VideoBytes);
    ClearPadding(CodedB + 2 * HancNumBytes, SectionVideo, VideoBytes);
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame sync +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LineNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The line number of an HD line, from its chrominance and luminance words; -1 without a
// valid EAV or when the two differ.
//
static int LineNumber(const uint8_t* Line)
{
    if ((ReadSymbol(Line, 0) & 0x3FC) != 0x3FC ||
        (ReadSymbol(Line, 1) & 0x3FC) != 0x3FC || ReadSymbol(Line, 2) != 0 ||
        ReadSymbol(Line, 3) != 0 || ReadSymbol(Line, 4) != 0 || ReadSymbol(Line, 5) != 0)
    {
        return -1;
    }

    int Chroma = (int)((ReadSymbol(Line, 8) >> 2) & 0x7F) |
                 (int)((ReadSymbol(Line, 10) >> 2) & 0xF) << 7;
    int Luma = (int)((ReadSymbol(Line, 9) >> 2) & 0x7F) |
               (int)((ReadSymbol(Line, 11) >> 2) & 0xF) << 7;
    return Chroma == Luma ? Chroma : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MatchesSdEav -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// True when the four symbols of an SD line's EAV equal Eav in their upper eight bits.
//
static bool MatchesSdEav(const uint8_t* Line, const uint32_t Eav[4])
{
    for (size_t i = 0; i < 4; i++)
    {
        if ((ReadSymbol(Line, i) & 0x3FC) != Eav[i])
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_CheckLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiFrame_CheckLines(const DtSdiFrameLayout* Layout,
                                  const uint8_t* FirstLine, const uint8_t* LastLine)
{
    // The EAV of the first line, in the first field's vertical blanking, and of the last,
    // in the second field's.
    static const uint32_t SdEavFirstLine[4] = {0x3FC, 0x000, 0x000, 0x2D8};
    static const uint32_t SdEavLastLine[4] = {0x3FC, 0x000, 0x000, 0x3C4};
    bool InSync;

    if (Layout->NumLines <= 625)
        InSync = MatchesSdEav(FirstLine, SdEavFirstLine) &&
                 MatchesSdEav(LastLine, SdEavLastLine);
    else
        InSync = LineNumber(FirstLine) == 1 && LineNumber(LastLine) == Layout->NumLines;
    return InSync ? DTAPI_OK : DTAPI_E_OUT_OF_SYNC;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Black frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The chrominance and the luminance of black and of empty blanking.
#define BLACK_C 0x200u
#define BLACK_Y 0x040u

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Replaces symbol Index of a packed 10-bit section with Value.
//
static void SetSymbol(uint8_t* Section, size_t Index, uint32_t Value)
{
    size_t Bit = Index * 10;
    size_t Byte = Bit / 8;
    uint32_t Shift = (uint32_t)(Bit % 8);
    uint32_t Bits = (Value & 0x3FF) << Shift;
    uint32_t Mask = 0x3FFu << Shift;

    for (uint32_t Count = 10 + Shift; Count > 0; Count = Count > 8 ? Count - 8 : 0)
    {
        Section[Byte] = (uint8_t)(((uint32_t)Section[Byte] & ~Mask) | Bits);
        Byte++;
        Bits >>= 8;
        Mask >>= 8;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FillBlack -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills a section of Symbols symbols and Bytes bytes with the chrominance and the
// luminance of black in turn, chrominance first, and clears its padding.
//
static void FillBlack(uint8_t* Section, size_t Symbols, size_t Bytes)
{
    // 200 040 200 040, packed.
    static const uint8_t Group[5] = {0x00, 0x02, 0x01, 0x20, 0x10};
    size_t Groups = Symbols / 4;
    size_t i;

    memset(Section, 0, Bytes);
    for (i = 0; i < Groups; i++)
        memcpy(Section + 5 * i, Group, sizeof(Group));
    for (i = 4 * Groups; i < Symbols; i++)
        SetSymbol(Section, i, i % 2 == 0 ? BLACK_C : BLACK_Y);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Crc18 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// SMPTE 292's CRC-18, x^18 + x^5 + x^4 + 1, over one 10-bit word, least significant bit
// first.
//
static uint32_t Crc18(uint32_t Crc, uint32_t Word)
{
    for (int Bit = 0; Bit < 10; Bit++)
    {
        uint32_t Feedback = (Crc ^ (Word >> Bit)) & 1;

        Crc >>= 1;
        if (Feedback != 0)
            Crc ^= 0x23000;
    }
    return Crc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Xyz -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The fourth word of a timing reference of line Line, from 1: the field, vertical
// blanking and EAV or SAV bits, and the protection bits over those three.
//
static uint32_t Xyz(const DtFrameProps* Props, int Line, bool Eav)
{
    uint32_t F = Props->NumFields == 2 && Line >= Props->Fields[1].StartLine ? 1 : 0;
    const DtFieldProps* Field = &Props->Fields[F];
    uint32_t V = Line < Field->VidStartLine || Line > Field->VidEndLine ? 1 : 0;
    uint32_t H = Eav ? 1 : 0;

    return 0x200 | F << 8 | V << 7 | H << 6 | (V ^ H) << 5 | (F ^ H) << 4 | (F ^ V) << 3 |
           (F ^ V ^ H) << 2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WithParity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Nine bits with bit 9 the inverse of bit 8, as line numbers and CRC words carry them.
//
static uint32_t WithParity(uint32_t Nine)
{
    Nine &= 0x1FF;
    return Nine | ((Nine >> 8) ^ 1) << 9;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- BlackLink -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The black frame of a standard that is not 4K, one coded line per line.
//
static void BlackLink(const DtSdiFrameLayout* Layout, uint8_t* Lines)
{
    const size_t Hanc = (size_t)Layout->LineNumSymsHanc;
    const size_t Video = (size_t)Layout->LineNumSymsVideo;
    uint32_t ActiveCrc[2] = {0, 0};
    DtFrameProps Props;

    if (!DtFrameProps_Init(&Props, Layout->VidStd))
        return;

    // Every line's active part is black, so each channel's CRC starts the same.
    for (size_t i = 0; i < Video; i++)
        ActiveCrc[i % 2] = Crc18(ActiveCrc[i % 2], i % 2 == 0 ? BLACK_C : BLACK_Y);

    for (int Line = 1; Line <= Layout->NumLines; Line++)
    {
        uint8_t* Coded = Lines + (size_t)(Line - 1) * (size_t)Layout->Stride;
        const uint32_t Sync[3] = {0x3FF, 0x000, 0x000};
        const uint32_t Eav = Xyz(&Props, Line, true);
        const uint32_t Sav = Xyz(&Props, Line, false);

        FillBlack(Coded, Hanc, (size_t)Layout->SectionBytesHanc);
        FillBlack(Coded + Layout->SectionBytesHanc, Video,
                  (size_t)Layout->SectionBytesVideo);

        size_t j;
        if (Props.LineNumSymEav == 4)
        {
            for (j = 0; j < 3; j++)
            {
                SetSymbol(Coded, j, Sync[j]);
                SetSymbol(Coded, Hanc - 4 + j, Sync[j]);
            }
            SetSymbol(Coded, 3, Eav);
            SetSymbol(Coded, Hanc - 1, Sav);
            continue;
        }

        // HD and 3G: each word once for each channel, chrominance first.
        for (size_t Channel = 0; Channel < 2; Channel++)
        {
            uint32_t Words[8] = {0x3FF,
                                 0x000,
                                 0x000,
                                 Eav,
                                 WithParity((uint32_t)Line << 2),
                                 WithParity((uint32_t)(Line >> 7) << 2 & 0x3C),
                                 0,
                                 0};
            uint32_t Crc = ActiveCrc[Channel];

            for (j = 0; j < 6; j++)
                Crc = Crc18(Crc, Words[j]);
            Words[6] = WithParity(Crc);
            Words[7] = WithParity(Crc >> 9);

            for (j = 0; j < 8; j++)
                SetSymbol(Coded, 2 * j + Channel, Words[j]);
            for (j = 0; j < 3; j++)
                SetSymbol(Coded, Hanc - 8 + 2 * j + Channel, Sync[j]);
            SetSymbol(Coded, Hanc - 2 + Channel, Sav);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrame_BlackLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Each coded line of a 4K frame takes the HANC section of its link line from the black
// frame of one link, twice, and a video section that is black on a picture and on a
// blanking line alike.
//
bool DtSdiFrame_BlackLines(const DtSdiFrameLayout* Layout, uint8_t* Lines)
{
    if (!Layout->Is4k)
    {
        BlackLink(Layout, Lines);
        return true;
    }

    const DtVidStdInfo* Info = DtVidStd_Find(Layout->VidStd);
    DtSdiFrameLayout Link;
    if (Info == NULL ||
        !DtSdiFrame_LayoutInit(&Link, Info->OneLinkVidStd, 8 * Layout->Alignment))
        return false;
    uint8_t* LinkLines =
        (uint8_t*)DtAlloc_Malloc((size_t)Link.NumLines * (size_t)Link.Stride);
    if (LinkLines == NULL)
        return false;
    BlackLink(&Link, LinkLines);

    const size_t HancNumBytes = (size_t)Layout->SectionBytesHanc;
    for (int Coded = 0; Coded < Layout->NumCodedLines; Coded++)
    {
        uint8_t* Line = Lines + (size_t)Coded * (size_t)Layout->TxStride;
        const uint8_t* LinkHanc = LinkLines + (size_t)(Coded / 2) * (size_t)Link.Stride;
        uint8_t* Sections = Line + Layout->TxLineHeaderNumBytes;

        DtSdiFrame_EncodeTxLineHeader(Layout, Coded, Line);
        memcpy(Sections, LinkHanc, HancNumBytes);
        memcpy(Sections + HancNumBytes, LinkHanc, HancNumBytes);
        FillBlack(Sections + 2 * HancNumBytes, (size_t)Layout->SectionNumSymsVideo,
                  (size_t)Layout->SectionBytesVideo);
    }
    DtAlloc_Free(LinkLines);
    return true;
}

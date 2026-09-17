// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The firmware's coded SDI frames, DTAPI's raw SDI frame, and black frames
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "CDtapiLite.h"   // DTAPI_VIDSTD_ codes and result codes.
#include "DtFrameProps.h" // Frame geometry.
#include "DtSdiFrame.h"   // Interface being implemented.
#include "DtVidStd.h"     // Which standards are 4K.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Coded frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PaddedBytes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bytes Symbols 10-bit symbols take, padded to Alignment bytes, as
// DtMxPlaneProps::NumBytesPerLine computes it for a 10-bit plane.
//
static int PaddedBytes(int Symbols, int Alignment)
{
    int Bits = Symbols * 10;
    int AlignmentBits = Alignment * 8;

    return (Bits + AlignmentBits - 1) / AlignmentBits * Alignment;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameLayoutInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtSdiFrameLayoutInit(DtSdiFrameLayout* Layout, int VidStd, int AlignmentBits)
{
    DtFrameProps Props;

    memset(Layout, 0, sizeof(*Layout));
    Layout->VidStd = DTAPI_VIDSTD_UNKNOWN;

    if (AlignmentBits <= 0 || AlignmentBits % 8 != 0 || DtVidStdIs4k(VidStd) ||
        !DtFramePropsInit(&Props, VidStd))
    {
        return false;
    }

    Layout->Alignment = AlignmentBits / 8;
    Layout->HeaderBytes = (DT_SDIFRAME_HEADER_BYTES + Layout->Alignment - 1) /
                          Layout->Alignment * Layout->Alignment;
    Layout->TxHeaderBytes = (DT_SDIFRAME_TX_HEADER_BYTES + Layout->Alignment - 1) /
                            Layout->Alignment * Layout->Alignment;
    Layout->NumLines = DtFramePropsNumLines(&Props);
    Layout->LineSymsHanc = DtFramePropsLineSymbolsHanc(&Props);
    Layout->LineBytesHanc = PaddedBytes(Layout->LineSymsHanc, Layout->Alignment);
    Layout->LineSymsVideo = Props.LineNumSymVanc;
    Layout->LineBytesVideo = PaddedBytes(Layout->LineSymsVideo, Layout->Alignment);
    Layout->Stride = Layout->LineBytesHanc + Layout->LineBytesVideo;
    Layout->Format = DT_SDIFRAME_FORMAT_UNCOMPRESSED;
    if (DtFramePropsIsSd(&Props))
        Layout->SdiRate = DT_SDIRATE_SD;
    else if (DtFramePropsIs3g(&Props))
        Layout->SdiRate = DT_SDIRATE_3G;
    else
        Layout->SdiRate = DT_SDIRATE_HD;
    Layout->VidStd = VidStd;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameCodedSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtSdiFrameCodedSize(const DtSdiFrameLayout* Layout)
{
    return (size_t)Layout->HeaderBytes +
           (size_t)Layout->NumLines * (size_t)Layout->Stride;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameTxCodedSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtSdiFrameTxCodedSize(const DtSdiFrameLayout* Layout)
{
    return (size_t)Layout->TxHeaderBytes +
           (size_t)Layout->NumLines * (size_t)Layout->Stride;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameDecodeHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The second word is declared in DTAPI as bit-fields, which the compilers DTAPI is built
// with allocate from the least significant bit.
//
void DtSdiFrameDecodeHeader(const uint8_t* Bytes, DtSdiFrameHeader* Header)
{
    uint32_t Word = Read32(Bytes + 4);

    Header->SyncWord = Read32(Bytes);
    Header->ProtocolVersion = (int)(Word & 0xF);
    Header->Format = (int)((Word >> 4) & 0xF);
    Header->FrameId = (int)(Word >> 16);
    Header->PtpSeconds = Read32(Bytes + 8);
    Header->PtpNanoseconds = Read32(Bytes + 12);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameEncodeHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiFrameEncodeHeader(const DtSdiFrameHeader* Header, uint8_t* Bytes)
{
    uint32_t Word = ((uint32_t)Header->ProtocolVersion & 0xF) |
                    ((uint32_t)Header->Format & 0xF) << 4 |
                    ((uint32_t)Header->FrameId & 0xFFFF) << 16;

    Write32(Bytes, Header->SyncWord);
    Write32(Bytes + 4, Word);
    Write32(Bytes + 8, Header->PtpSeconds);
    Write32(Bytes + 12, Header->PtpNanoseconds);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameCheckHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtSdiFrameCheckHeader(const DtSdiFrameLayout* Layout,
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameTxHeaderInit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The header's SDI rate takes the driver's DT_DRV_SDIRATE_ values, which DT_SDIRATE_
// equals for SD, HD and 3G.
//
void DtSdiFrameTxHeaderInit(const DtSdiFrameLayout* Layout, int FrameId,
                            DtSdiFrameTxHeader* Header)
{
    Header->SyncWord = DT_SDIFRAME_SYNC_WORD;
    Header->ProtocolVersion = 0;
    Header->Format = Layout->Format;
    Header->SdiRateValid = true;
    Header->SdiRate = Layout->SdiRate;
    Header->FrameId = FrameId & 0xFFFF;
    Header->NumLines = Layout->NumLines;
    Header->NumWordsHanc = Layout->LineBytesHanc / Layout->Alignment;
    Header->NumSymsHanc = Layout->LineSymsHanc;
    Header->NumWordsVideo = Layout->LineBytesVideo / Layout->Alignment;
    Header->NumSymsVideo = Layout->LineSymsVideo;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameDecodeTxHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Bit-fields as in the receive header, allocated from the least significant bit.
//
void DtSdiFrameDecodeTxHeader(const uint8_t* Bytes, DtSdiFrameTxHeader* Header)
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
    Header->NumLines = (int)(Word2 >> 16);
    Header->NumWordsHanc = (int)(Word3 & 0xFFFF);
    Header->NumSymsHanc = (int)(Word3 >> 16);
    Header->NumWordsVideo = (int)(Word4 & 0xFFFF);
    Header->NumSymsVideo = (int)(Word4 >> 16);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameEncodeTxHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiFrameEncodeTxHeader(const DtSdiFrameTxHeader* Header, uint8_t* Bytes)
{
    uint32_t Word1 = ((uint32_t)Header->ProtocolVersion & 0xF) |
                     ((uint32_t)Header->Format & 0xF) << 4 |
                     (Header->SdiRateValid ? 1u : 0u) << 8 |
                     ((uint32_t)Header->SdiRate & 0x7) << 9;

    Write32(Bytes, Header->SyncWord);
    Write32(Bytes + 4, Word1);
    Write32(Bytes + 8, ((uint32_t)Header->FrameId & 0xFFFF) |
                           ((uint32_t)Header->NumLines & 0xFFFF) << 16);
    Write32(Bytes + 12, ((uint32_t)Header->NumWordsHanc & 0xFFFF) |
                            ((uint32_t)Header->NumSymsHanc & 0xFFFF) << 16);
    Write32(Bytes + 16, ((uint32_t)Header->NumWordsVideo & 0xFFFF) |
                            ((uint32_t)Header->NumSymsVideo & 0xFFFF) << 16);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Raw frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameRawSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// HdSdiUtil::NumSymbols2DmaSize with an alignment of 64 bits.
//
size_t DtSdiFrameRawSize(const DtSdiFrameLayout* Layout, int SymbolBits)
{
    size_t Symbols =
        (size_t)Layout->NumLines * (size_t)(Layout->LineSymsHanc + Layout->LineSymsVideo);

    if (SymbolBits != 8 && SymbolBits != 10 && SymbolBits != 16)
        return 0;

    return (Symbols * (size_t)SymbolBits + 63) / 64 * 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameRawLineBits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtSdiFrameRawLineBits(const DtSdiFrameLayout* Layout, int SymbolBits)
{
    if (SymbolBits != 8 && SymbolBits != 10 && SymbolBits != 16)
        return 0;
    return (size_t)(Layout->LineSymsHanc + Layout->LineSymsVideo) * (size_t)SymbolBits;
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
// when the position is on a byte boundary, as PxCnv::Concat_Uyvy10 does; the rest goes
// bit by bit.
//
static void CopySection10(const uint8_t* Section, size_t Symbols, uint8_t* Raw,
                          size_t Bit)
{
    size_t Bits = Symbols * 10;
    size_t i;

    if (Bit % 8 == 0)
    {
        size_t Whole = Bits / 8;

        memcpy(Raw + Bit / 8, Section, Whole);
        if (Bits % 8 != 0)
            OrBits(Raw, Bit + Whole * 8, Section[Whole], (uint32_t)(Bits % 8));
        return;
    }

    for (i = 0; i < Symbols; i++)
        OrBits(Raw, Bit + i * 10, ReadSymbol(Section, i), 10);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopySection8 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Four symbols take five bytes, so the section is taken five bytes at a time, as
// Cnv10_8_OptC does, and the symbols left over one by one.
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
// Five bytes at a time as well, as Cnv10_16_OptC does.
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
        int j;

        Values[0] = Word & 0x3FF;
        Values[1] = (Word >> 10) & 0x3FF;
        Values[2] = (Word >> 20) & 0x3FF;
        Values[3] = (Word >> 30) | (uint32_t)In[4] << 2;
        for (j = 0; j < 4; j++)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameConvertLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The line starts at LineIndex times the bits of a line, as PxCnvTaskRaw::Run places it.
//
void DtSdiFrameConvertLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                           const uint8_t* CodedLine, int LineIndex, uint8_t* Raw)
{
    size_t Hanc = (size_t)Layout->LineSymsHanc;
    size_t Video = (size_t)Layout->LineSymsVideo;
    size_t Start = (size_t)LineIndex * (Hanc + Video);
    const uint8_t* VideoSection = CodedLine + Layout->LineBytesHanc;

    switch (SymbolBits)
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
// boundary, as PxCnv::Split_Uyvy10 does; otherwise each byte of Out takes the upper bits
// of one byte of In and the lower bits of the next. No byte of In after the one holding
// the last bit is read.
//
static void CopyBits(const uint8_t* In, size_t Bit, size_t Count, uint8_t* Out,
                     size_t Bytes)
{
    const uint8_t* Src = In + Bit / 8;
    uint32_t Shift = (uint32_t)(Bit % 8);
    size_t Whole = Count / 8;
    uint32_t Rest = (uint32_t)(Count % 8);
    size_t i;

    if (Shift == 0)
        memcpy(Out, Src, Whole);
    else
    {
        for (i = 0; i < Whole; i++)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameCodeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The video section starts where the HANC section ends, which in a line of 10-bit symbols
// need not be a byte boundary either.
//
bool DtSdiFrameCodeLine(const DtSdiFrameLayout* Layout, int SymbolBits,
                        const uint8_t* RawLine, int Phase, uint8_t* CodedLine)
{
    size_t Hanc = (size_t)Layout->LineSymsHanc;
    size_t Video = (size_t)Layout->LineSymsVideo;
    uint8_t* VideoSection = CodedLine + Layout->LineBytesHanc;

    if (Phase < 0 || Phase > 7)
        return false;

    switch (SymbolBits)
    {
    case 10:
        CopyBits(RawLine, (size_t)Phase, Hanc * 10, CodedLine,
                 (size_t)Layout->LineBytesHanc);
        CopyBits(RawLine, (size_t)Phase + Hanc * 10, Video * 10, VideoSection,
                 (size_t)Layout->LineBytesVideo);
        return true;
    case 16:
        if (Phase != 0)
            return false;
        Pack16(RawLine, Hanc, CodedLine, (size_t)Layout->LineBytesHanc);
        Pack16(RawLine + 2 * Hanc, Video, VideoSection, (size_t)Layout->LineBytesVideo);
        return true;
    default:
        return false;
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame sync +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LineNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// HdSdiUtil::GetLineNumber: the line number of an HD line, from its chrominance and
// luminance words; -1 without a valid EAV or when the two differ.
//
static int LineNumber(const uint8_t* Line)
{
    int Chroma, Luma;

    if ((ReadSymbol(Line, 0) & 0x3FC) != 0x3FC ||
        (ReadSymbol(Line, 1) & 0x3FC) != 0x3FC || ReadSymbol(Line, 2) != 0 ||
        ReadSymbol(Line, 3) != 0 || ReadSymbol(Line, 4) != 0 || ReadSymbol(Line, 5) != 0)
    {
        return -1;
    }

    Chroma = (int)((ReadSymbol(Line, 8) >> 2) & 0x7F) |
             (int)((ReadSymbol(Line, 10) >> 2) & 0xF) << 7;
    Luma = (int)((ReadSymbol(Line, 9) >> 2) & 0x7F) |
           (int)((ReadSymbol(Line, 11) >> 2) & 0xF) << 7;
    return Chroma == Luma ? Chroma : -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MatchesSdEav -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// True when the four symbols of an SD line's EAV equal Eav in their upper eight bits.
//
static bool MatchesSdEav(const uint8_t* Line, const uint32_t Eav[4])
{
    size_t i;

    for (i = 0; i < 4; i++)
    {
        if ((ReadSymbol(Line, i) & 0x3FC) != Eav[i])
            return false;
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameCheckLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtSdiFrameCheckLines(const DtSdiFrameLayout* Layout,
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
    uint32_t Count;

    for (Count = 10 + Shift; Count > 0; Count = Count > 8 ? Count - 8 : 0)
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
    int Bit;

    for (Bit = 0; Bit < 10; Bit++)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiFrameBlackLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiFrameBlackLines(const DtSdiFrameLayout* Layout, uint8_t* Lines)
{
    const size_t Hanc = (size_t)Layout->LineSymsHanc;
    const size_t Video = (size_t)Layout->LineSymsVideo;
    uint32_t ActiveCrc[2] = {0, 0};
    DtFrameProps Props;
    int Line;
    size_t i;

    if (!DtFramePropsInit(&Props, Layout->VidStd))
        return;

    // Every line's active part is black, so each channel's CRC starts the same.
    for (i = 0; i < Video; i++)
        ActiveCrc[i % 2] = Crc18(ActiveCrc[i % 2], i % 2 == 0 ? BLACK_C : BLACK_Y);

    for (Line = 1; Line <= Layout->NumLines; Line++)
    {
        uint8_t* Coded = Lines + (size_t)(Line - 1) * (size_t)Layout->Stride;
        const uint32_t Sync[3] = {0x3FF, 0x000, 0x000};
        const uint32_t Eav = Xyz(&Props, Line, true);
        const uint32_t Sav = Xyz(&Props, Line, false);
        size_t Channel, j;

        FillBlack(Coded, Hanc, (size_t)Layout->LineBytesHanc);
        FillBlack(Coded + Layout->LineBytesHanc, Video, (size_t)Layout->LineBytesVideo);

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
        for (Channel = 0; Channel < 2; Channel++)
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

// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The frame format a receive channel writes, and DTAPI's raw SDI frame
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
    Layout->NumLines = DtFramePropsNumLines(&Props);
    Layout->LineSymsHanc = DtFramePropsLineSymbolsHanc(&Props);
    Layout->LineBytesHanc = PaddedBytes(Layout->LineSymsHanc, Layout->Alignment);
    Layout->LineSymsVideo = Props.LineNumSymVanc;
    Layout->LineBytesVideo = PaddedBytes(Layout->LineSymsVideo, Layout->Alignment);
    Layout->Stride = Layout->LineBytesHanc + Layout->LineBytesVideo;
    Layout->Format = DT_SDIFRAME_FORMAT_UNCOMPRESSED;
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadSymbol -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Symbol Index of a packed 10-bit section. Reads no byte beyond the symbol.
//
static unsigned ReadSymbol(const uint8_t* Section, size_t Index)
{
    size_t Bit = Index * 10;
    size_t Byte = Bit / 8;
    unsigned Shift = (unsigned)(Bit % 8);
    unsigned Value = (unsigned)Section[Byte] >> Shift;
    unsigned Have = 8 - Shift;

    while (Have < 10)
    {
        Value |= (unsigned)Section[++Byte] << Have;
        Have += 8;
    }
    return Value & 0x3FF;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OrBits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Adds the Count low bits of Value to Raw at bit position Bit, into bits that are zero.
//
static void OrBits(uint8_t* Raw, size_t Bit, unsigned Value, unsigned Count)
{
    size_t Byte = Bit / 8;
    unsigned Shift = (unsigned)(Bit % 8);
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
            OrBits(Raw, Bit + Whole * 8, Section[Whole], (unsigned)(Bits % 8));
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
        unsigned Values[4];
        int j;

        Values[0] = Word & 0x3FF;
        Values[1] = (Word >> 10) & 0x3FF;
        Values[2] = (Word >> 20) & 0x3FF;
        Values[3] = (Word >> 30) | (unsigned)In[4] << 2;
        for (j = 0; j < 4; j++)
        {
            Out[2 * j] = (uint8_t)Values[j];
            Out[2 * j + 1] = (uint8_t)(Values[j] >> 8);
        }
    }

    for (i = 4 * Groups; i < Symbols; i++)
    {
        unsigned Value = ReadSymbol(Section, i);

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

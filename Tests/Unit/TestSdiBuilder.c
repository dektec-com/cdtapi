// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSdiBuilder.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The builder's image, raster and ancillary packets
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Each frame the builder makes is compared byte for byte with a frame made here from the
// standards' own numbers: the lines and their active parts of SMPTE ST 125, BT.656,
// ST 274 and ST 296 as SdiFormats.inc has them; the timing references of ST 125's table;
// the line numbers and CRC-18 of ST 292, the CRC worked out bit by bit; the packets of
// ST 291; and for 2160p the division over four links of ST 2082-10. Where the standards
// leave a choice, the frame is made as DTAPI's matrix makes it: blanking 200 (hex) in a
// C and 040 in a Y stream; the payload ID right after EAV on the lines ST 352 names, in
// the Y stream in HD and in every stream from 3G up, with byte 3 zero and byte 4 saying
// 10 bits. The program's packets follow it, in the order given. Unless the builder is
// asked for them, the CRC words are 200 (hex) and the checksums 0CC, for the
// transmitter to fill in.
//
// With CDTAPI_TEST_SDI_DIR set, one more case builds frames from the images FFmpeg's sdi
// muxer was given, <Name>.yuv, and compares them with the muxer's, <Name>.raw. They
// differ where plan 0032 says: the muxer's blanking is 200 in a Y stream as well, its
// line CRCs are wrong (it takes the line's first word over and over), its horizontal
// blanking holds its audio, and its payload ID has bytes of its own.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"     // Test framework.
#include "SdiFormat.h"  // Every standard's line timing.
#include "cdtapi_sdi.h" // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Standards +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

static bool Is4k(const SdiFormat* F)
{
    return strncmp(F->Name, "2160", 4) == 0;
}

static bool IsSd(const SdiFormat* F)
{
    return F->Lines <= 625;
}

// The streams of a raw line: one in SD, C and Y in HD and 3G, C and Y of four links in
// 2160p. Stream 2 * (Link - 1) is a link's C stream, the one after it its Y stream.
static int NumStreams(const SdiFormat* F)
{
    return IsSd(F) ? 1 : Is4k(F) ? 8 : 2;
}

// The words of one stream of a line, of its horizontal blanking with EAV and SAV, and
// of EAV with the line number and CRC.
static int StreamWords(const SdiFormat* F)
{
    return IsSd(F) ? 2 * F->Samples : F->Samples;
}

static int HancWords(const SdiFormat* F)
{
    return IsSd(F) ? 2 * (F->Samples - F->Active) : F->Samples - F->Active;
}

static int EavWords(const SdiFormat* F)
{
    return IsSd(F) ? 4 : 8;
}

// The symbols of a raw line.
static size_t LineSymbols(const SdiFormat* F)
{
    return (size_t)(2 * F->Samples) * (Is4k(F) ? 4 : 1);
}

// Where word k of stream s lies in a raw line. 2160p takes word k of the C streams of
// links 4, 2, 3 and 1, then of their Y streams.
static size_t SymbolOf(const SdiFormat* F, int s, int k)
{
    static const int Place[4] = {3, 1, 2, 0};
    if (IsSd(F))
        return (size_t)k;
    if (Is4k(F))
        return 8 * (size_t)k + (size_t)Place[s / 2] + (s & 1 ? 4 : 0);
    return 2 * (size_t)k + (size_t)s;
}

// The active lines of each field, as a raw frame numbers its lines from 1; of one link
// in 2160p.
typedef struct ActiveLines
{
    int NumFields;
    int First[2];
    int Last[2];
} ActiveLines;

static ActiveLines GetActiveLines(const SdiFormat* F)
{
    if (F->Lines == 525)
        return (ActiveLines){2, {17, 280}, {260, 522}}; // ST 125, three lower
    if (F->Lines == 625)
        return (ActiveLines){2, {23, 336}, {310, 623}}; // BT.656
    if (F->Lines == 750)
        return (ActiveLines){1, {26, 0}, {745, 0}}; // ST 296
    if (F->Scan == SDI_SCAN_P)
        return (ActiveLines){1, {42, 0}, {1121, 0}}; // ST 274, one field
    return (ActiveLines){2, {21, 584}, {560, 1123}}; // ST 274, two fields
}

// The field (0 or 1) of line Line, and whether it is a line of the image.
static int FieldOf(const SdiFormat* F, int Line)
{
    return F->Scan != SDI_SCAN_P && Line > F->LinesF1 ? 1 : 0;
}

static bool IsImageLine(const SdiFormat* F, int Line)
{
    const ActiveLines A = GetActiveLines(F);
    const int f = FieldOf(F, Line);
    return Line >= A.First[f] && Line <= A.Last[f];
}

// The fourth word of a timing reference, from ST 125's table of the eight by F, V, H.
static uint16_t Xyz(const SdiFormat* F, int Line, bool Eav)
{
    static const uint16_t Legal[8] = {0x200, 0x274, 0x2AC, 0x2D8,
                                      0x31C, 0x368, 0x3B0, 0x3C4};
    const int V = IsImageLine(F, Line) ? 0 : 1;
    return Legal[FieldOf(F, Line) * 4 + V * 2 + (Eav ? 1 : 0)];
}

// The lines ST 352 puts the payload ID on: 13 and 276 of ST 125, which a raw frame
// counts as 10 and 273; 9 and 322 of BT.656; 10 of ST 296; 10 and 572 of ST 274.
static bool IsPayloadIdLine(const SdiFormat* F, int Line)
{
    if (F->Lines == 525)
        return Line == 10 || Line == 273;
    if (F->Lines == 625)
        return Line == 9 || Line == 322;
    return Line == 10 || (F->Scan != SDI_SCAN_P && Line == 572);
}

// The VPID the matrix sends, byte 1 in the lowest bits: the payload, the picture rate
// with bit 7 for a progressive transport (not for 720p) and bit 6 for a progressive
// picture, byte 3 zero, byte 4 one for 10 bits.
static uint32_t MatrixVpid(const SdiFormat* F)
{
    uint32_t Byte2 = SdiFormat_RateCode(F);
    if (F->Scan == SDI_SCAN_P && F->Lines != 750)
        Byte2 |= 0x80;
    if (F->Scan != SDI_SCAN_I)
        Byte2 |= 0x40;
    return (uint32_t)F->Payload | Byte2 << 8 | 0x01000000u;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Words +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// SMPTE 292's CRC-18 after one more word, least significant bit first, as the 18
// stages of a shift register with feedback into x^0, x^4 and x^5.
static uint32_t BitCrc18(uint32_t Crc, uint32_t Word)
{
    uint8_t Stage[18];
    for (int i = 0; i < 18; i++)
        Stage[i] = (uint8_t)(Crc >> i & 1);
    for (int b = 0; b < 10; b++)
    {
        const uint8_t In = (uint8_t)((Word >> b & 1) ^ Stage[0]);
        for (int i = 0; i < 17; i++)
            Stage[i] = Stage[i + 1];
        Stage[17] = In;
        Stage[13] = (uint8_t)(Stage[13] ^ In);
        Stage[12] = (uint8_t)(Stage[12] ^ In);
    }
    Crc = 0;
    for (int i = 0; i < 18; i++)
        Crc |= (uint32_t)Stage[i] << i;
    return Crc;
}

// The same over Count words, ten bits at a time: the CRC is linear, so the register's
// lower ten bits and the word give one table entry, which the bits above are shifted in
// on. The table comes from BitCrc18, so that a whole frame is quick to check.
static uint32_t RefCrc18(uint32_t Crc, const uint16_t* Words, int Count)
{
    static uint32_t Table[1024];
    static bool HasTable = false;
    if (!HasTable)
    {
        for (uint32_t i = 0; i < 1024; i++)
            Table[i] = BitCrc18(i, 0);
        HasTable = true;
    }
    for (int w = 0; w < Count; w++)
        Crc = (Crc >> 10) ^ Table[(Crc ^ Words[w]) & 0x3FF];
    return Crc;
}

// Nine bits with bit 9 the inverse of bit 8.
static uint16_t Protected(uint32_t Value)
{
    Value &= 0x1FF;
    return (uint16_t)((Value & 0x100) != 0 ? Value : Value | 0x200);
}

// Eight bits with even parity in bit 8 and its inverse in bit 9.
static uint16_t Parity8(unsigned Value)
{
    int Ones = 0;
    for (int b = 0; b < 8; b++)
        Ones += (int)(Value >> b & 1);
    const unsigned P = (unsigned)(Ones & 1);
    return (uint16_t)((Value & 0xFF) | P << 8 | (P ^ 1) << 9);
}

// Writes a packet of ST 291 at Words[Pos], its checksum worked out or 0CC; returns the
// word after it.
static int RefPacket(uint16_t* Words, int Pos, unsigned Did, unsigned Sdid,
                     const uint16_t* Data, int Count, bool Checksum)
{
    Words[Pos++] = 0x000;
    Words[Pos++] = 0x3FF;
    Words[Pos++] = 0x3FF;
    uint32_t Sum = 0;
    const uint16_t Head[3] = {Parity8(Did), Parity8(Sdid), Parity8((unsigned)Count)};
    for (int i = 0; i < 3; i++)
    {
        Words[Pos++] = Head[i];
        Sum += Head[i] & 0x1FFu;
    }
    for (int i = 0; i < Count; i++)
    {
        Words[Pos++] = Data[i];
        Sum += Data[i] & 0x1FFu;
    }
    Words[Pos++] = Checksum ? Protected(Sum) : 0x0CC;
    return Pos;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Images +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The value of symbol s of image line y in one of two patterns: Full takes in 0 to
// 1023, which the builder limits to 4..1019; else multiples of 4 from 4 to 1016, which
// every pixel format holds.
typedef uint16_t (*Pattern)(int y, int s);

static uint16_t FullPattern(int y, int s)
{
    return (uint16_t)((y * 31 + s * 7 + (s & 1) * 300) % 1024);
}

static uint16_t EvenPattern(int y, int s)
{
    return (uint16_t)(4 * (1 + (y * 29 + s * 5 + (s & 1) * 300) % 254));
}

static uint16_t Legal(uint16_t Value)
{
    return Value < 4 ? 4 : Value > 1019 ? 1019 : Value;
}

// A yuv422p10le image of F's size: Planes[0] Y, then Cb and Cr.
typedef struct TestImage
{
    DtSdiImage Image;
    uint8_t* Memory;
    int Width;
    int Height;
} TestImage;

static bool TestImage_Alloc(TestImage* T, const SdiFormat* F, Pattern Value)
{
    memset(T, 0, sizeof(*T));
    int Strides[3];
    if (DtSdiImage_GetSize(F->VidStd, DT_SDI_PIXFMT_YUV422P_10B, &T->Width, &T->Height,
                           Strides) != DTAPI_OK)
        return false;
    const size_t Y = 2 * (size_t)T->Width * (size_t)T->Height;
    T->Memory = (uint8_t*)malloc(2 * Y);
    if (T->Memory == NULL)
        return false;
    T->Image.Format = DT_SDI_PIXFMT_YUV422P_10B;
    T->Image.Fields = DT_SDI_FIELDS_WOVEN;
    T->Image.Planes[0] = T->Memory;
    T->Image.Planes[1] = T->Memory + Y;
    T->Image.Planes[2] = T->Memory + Y + Y / 2;
    T->Image.Strides[0] = 2 * T->Width;
    T->Image.Strides[1] = T->Width;
    T->Image.Strides[2] = T->Width;
    for (int y = 0; Value != NULL && y < T->Height; y++)
    {
        for (int s = 0; s < 2 * T->Width; s++)
        {
            const int p = (s & 1) != 0 ? 0 : (s & 2) == 0 ? 1 : 2;
            const int x = p == 0 ? s / 2 : s / 4;
            uint8_t* At = T->Image.Planes[p] + (size_t)y * (size_t)T->Image.Strides[p];
            At[2 * x] = (uint8_t)Value(y, s);
            At[2 * x + 1] = (uint8_t)(Value(y, s) >> 8);
        }
    }
    return true;
}

static void TestImage_Free(TestImage* T)
{
    free(T->Memory);
    memset(T, 0, sizeof(*T));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reference frames +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Makes the words of every stream of line Line into Words[s], as the builder must:
// Value gives the image (NULL for none), Packets the program's packets, Crc each
// stream's CRC over the active part of the line before, which it updates; NULL leaves
// the CRCs and the checksums to the transmitter.
static void RefLine(const SdiFormat* F, int Line, Pattern Value,
                    const DtSdiAncPacket* Packets, int NumPackets, uint32_t* Crc,
                    uint16_t Words[8][8400])
{
    const int Hanc = HancWords(F);
    const int Total = StreamWords(F);
    const bool Image = IsImageLine(F, Line);

    for (int s = 0; s < NumStreams(F); s++)
    {
        uint16_t* W = Words[s];
        const bool C = (s & 1) == 0;
        for (int k = 0; k < Total; k++)
            W[k] = IsSd(F) ? ((k & 1) == 0 ? 0x200 : 0x040) : C ? 0x200 : 0x040;
        const uint16_t Eav[4] = {0x3FF, 0x000, 0x000, Xyz(F, Line, true)};
        const uint16_t Sav[4] = {0x3FF, 0x000, 0x000, Xyz(F, Line, false)};
        memcpy(W, Eav, sizeof(Eav));
        memcpy(W + Hanc - 4, Sav, sizeof(Sav));
        if (!IsSd(F))
        {
            W[4] = Protected((uint32_t)Line << 2);
            W[5] = Protected((uint32_t)(Line >> 7) << 2);
        }

        int Pos = EavWords(F);
        const bool ThreeGUp = F->SdiRate != DT_SDIRATE_HD;
        if (IsPayloadIdLine(F, Line) && (IsSd(F) || ThreeGUp || !C))
        {
            const uint32_t Vpid = MatrixVpid(F);
            const uint16_t Bytes[4] = {Parity8(Vpid & 0xFF), Parity8(Vpid >> 8 & 0xFF),
                                       Parity8(Vpid >> 16 & 0xFF), Parity8(Vpid >> 24)};
            Pos = RefPacket(W, Pos, 0x41, 0x01, Bytes, 4, Crc != NULL);
        }
        int VancPos = Hanc;
        for (int p = 0; p < NumPackets; p++)
        {
            const DtSdiAncPacket* P = &Packets[p];
            const int Link = P->VirtualInterface == 0 ? 1 : P->VirtualInterface;
            const int Stream = IsSd(F) ? 0 : 2 * (Link - 1) + (P->OnChroma ? 0 : 1);
            if (P->Line != Line || Stream != s)
                continue;
            if (P->InHanc)
                Pos = RefPacket(W, Pos, P->Did, P->SdidOrDbn, P->Words, P->NumWords,
                                Crc != NULL);
            else
                VancPos = RefPacket(W, VancPos, P->Did, P->SdidOrDbn, P->Words,
                                    P->NumWords, Crc != NULL);
        }

        // The image: of 2160p, the pixel pairs of image line 2k in turn on links 1 and
        // 2, those of line 2k + 1 on links 3 and 4.
        if (Image && Value != NULL)
        {
            const ActiveLines A = GetActiveLines(F);
            const int f = FieldOf(F, Line);
            const int j = Line - A.First[f];
            for (int k = 0; k < Total - Hanc; k++)
            {
                int y;
                int Symbol;
                if (Is4k(F))
                {
                    const int Link = s / 2;
                    y = 2 * j + Link / 2;
                    const int X = 2 * (2 * (k / 2) + (Link & 1)) + (k & 1);
                    Symbol = 2 * X + (C ? 0 : 1);
                }
                else
                {
                    y = A.NumFields == 1 ? j : 2 * j + f;
                    Symbol = IsSd(F) ? k : 2 * k + (C ? 0 : 1);
                }
                W[Hanc + k] = Legal(Value(y, Symbol));
            }
        }

        if (!IsSd(F) && Crc == NULL)
        {
            W[6] = 0x200;
            W[7] = 0x200;
        }
        else if (!IsSd(F))
        {
            const uint32_t LineCrc = RefCrc18(Crc[s], W, 6);
            W[6] = Protected(LineCrc);
            W[7] = Protected(LineCrc >> 9);
            Crc[s] = RefCrc18(0, W + Hanc, Total - Hanc);
        }
    }
}

// Writes Value as symbol Index of a raw frame with Bits bits a symbol.
static void PutSymbol(uint8_t* Frame, int Bits, size_t Index, uint16_t Value)
{
    if (Bits == 16)
    {
        Frame[2 * Index] = (uint8_t)Value;
        Frame[2 * Index + 1] = (uint8_t)(Value >> 8);
        return;
    }
    // A 10-bit symbol starts at an even bit, so it lies within two bytes.
    const size_t Bit = 10 * Index;
    const uint32_t Shifted = (uint32_t)(Value & 0x3FF) << (Bit % 8);
    Frame[Bit / 8] |= (uint8_t)Shifted;
    Frame[Bit / 8 + 1] |= (uint8_t)(Shifted >> 8);
}

static uint16_t GetSymbol(const uint8_t* Frame, int Bits, size_t Index)
{
    if (Bits == 16)
        return (uint16_t)(Frame[2 * Index] | Frame[2 * Index + 1] << 8);
    uint16_t Value = 0;
    for (int b = 0; b < 10; b++)
    {
        const size_t Bit = 10 * Index + (size_t)b;
        Value |= (uint16_t)((Frame[Bit / 8] >> (Bit % 8) & 1) << b);
    }
    return Value;
}

// The words of the line being made.
static uint16_t g_Words[8][8400];

// Makes the frame the builder must make into Frame, Size bytes, zeroed here.
static void RefFrame(const SdiFormat* F, int Bits, Pattern Value,
                     const DtSdiAncPacket* Packets, int NumPackets, uint32_t* Crc,
                     uint8_t* Frame, size_t Size)
{
    memset(Frame, 0, Size);
    const size_t PerLine = LineSymbols(F);
    for (int Line = 1; Line <= F->Lines; Line++)
    {
        RefLine(F, Line, Value, Packets, NumPackets, Crc, g_Words);
        const size_t First = (size_t)(Line - 1) * PerLine;
        for (int s = 0; s < NumStreams(F); s++)
            for (int k = 0; k < StreamWords(F); k++)
                PutSymbol(Frame, Bits, First + SymbolOf(F, s, k), g_Words[s][k]);
    }
}

// Returns NULL when Built equals Expected, else where it differs first.
static const char* Compare(const SdiFormat* F, int Bits, const uint8_t* Built,
                           const uint8_t* Expected, size_t Size, char* Message,
                           size_t MessageSize)
{
    if (memcmp(Built, Expected, Size) == 0)
        return NULL;
    const size_t PerLine = LineSymbols(F);
    const size_t Symbols = (size_t)F->Lines * PerLine;
    for (size_t i = 0; i < Symbols; i++)
    {
        const uint16_t A = GetSymbol(Built, Bits, i);
        const uint16_t B = GetSymbol(Expected, Bits, i);
        if (A != B)
        {
            snprintf(Message, MessageSize,
                     "%d bits: line %d, symbol %d: built %03X, expected %03X", Bits,
                     (int)(i / PerLine) + 1, (int)(i % PerLine), A, B);
            return Message;
        }
    }
    snprintf(Message, MessageSize, "%d bits: the padding differs", Bits);
    return Message;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The standards that are also built with 16 bits a symbol and twice in a row: one of
// each kind of line, 720p24 for its lines that start half-way through a byte, and the
// widest line of 2160p. The others share their lines' layout with one of these.
static bool IsBuiltInFull(const SdiFormat* F)
{
    static const char* Names[] = {"525I59_94", "625I50",  "720P24",
                                  "1080I50",   "1080P50", "2160P23_98"};
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
        if (strcmp(F->Name, Names[i]) == 0)
            return true;
    return false;
}

// Builds a frame and compares it with the reference: of every standard with 10 bits a
// symbol, with an image whose samples need limiting and the checksums worked out; and
// for the standards of IsBuiltInFull with 16 bits as well, and twice in a row, the
// second frame's first CRC covering the first frame's last line.
DT_TEST(EveryStandard)
{
    char Message[160];
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];
        if (SdiFormat_IsLevelB(F))
            continue;
        const bool Full = IsBuiltInFull(F);
        for (int Bits = 10; Bits <= (Full ? 16 : 10); Bits += 6)
        {
            size_t Size = 0;
            DT_ASSERT_EQ(DtSdiView_RawFrameSize(F->VidStd, Bits, &Size), DTAPI_OK);
            uint8_t* Built = (uint8_t*)malloc(Size);
            uint8_t* Expected = (uint8_t*)malloc(Size);
            DtSdiView* View = DtSdiView_Alloc();
            DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
            TestImage T;
            memset(&T, 0, sizeof(T));
            const bool Ready = Built != NULL && Expected != NULL && View != NULL &&
                               Builder != NULL && TestImage_Alloc(&T, F, FullPattern);
            const char* Failure = Ready ? NULL : "out of memory";
            uint32_t Crc[8] = {0};
            if (Failure == NULL && DtSdiBuilder_SetChecksums(Builder, true) != DTAPI_OK)
                Failure = "SetChecksums refused";
            for (int n = 0; Failure == NULL && n < (Full ? 2 : 1); n++)
            {
                memset(Built, 0xA5, Size);
                if (DtSdiView_SetRawFrame(View, Built, Size, F->VidStd, Bits) !=
                        DTAPI_OK ||
                    DtSdiBuilder_Build(Builder, View, &T.Image, NULL, NULL) != DTAPI_OK)
                    Failure = "the build failed";
                else
                {
                    RefFrame(F, Bits, FullPattern, NULL, 0, Crc, Expected, Size);
                    Failure =
                        Compare(F, Bits, Built, Expected, Size, Message, sizeof(Message));
                }
            }
            // The parser reads the payload ID back, byte 1 at the top.
            uint32_t PayloadId = 0;
            if (Failure == NULL &&
                (DtSdiView_GetPayloadId(View, &PayloadId) != DTAPI_OK ||
                 (int)(PayloadId >> 24) != F->Payload))
                Failure = "the parser does not find the payload ID";

            TestImage_Free(&T);
            DtSdiBuilder_Free(Builder);
            DtSdiView_Free(View);
            free(Expected);
            free(Built);
            if (Failure != NULL)
                DT_FAIL("%s: %s", F->Name, Failure);
        }
    }
}

// Without an image the active part is black, with blanking's values; the CRCs are left
// to the transmitter, and so is the payload ID's checksum.
DT_TEST(BlackWithoutImage)
{
    char Message[160];
    const char* Names[] = {"525I59_94", "720P50", "1080I50", "2160P50"};
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        size_t Size = 0;
        DT_ASSERT_EQ(DtSdiView_RawFrameSize(F->VidStd, 10, &Size), DTAPI_OK);
        uint8_t* Built = (uint8_t*)malloc(Size);
        uint8_t* Expected = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        const char* Failure = "out of memory";
        if (Built != NULL && Expected != NULL && View != NULL && Builder != NULL)
        {
            if (DtSdiView_SetRawFrame(View, Built, Size, F->VidStd, 10) != DTAPI_OK ||
                DtSdiBuilder_Build(Builder, View, NULL, NULL, NULL) != DTAPI_OK)
                Failure = "the build failed";
            else
            {
                RefFrame(F, 10, NULL, NULL, 0, NULL, Expected, Size);
                Failure = Compare(F, 10, Built, Expected, Size, Message, sizeof(Message));
            }
        }
        DtSdiBuilder_Free(Builder);
        DtSdiView_Free(View);
        free(Expected);
        free(Built);
        if (Failure != NULL)
            DT_FAIL("%s: %s", F->Name, Failure);
    }
}

// The user data words of test packet n: eight bits with parity, as most packets carry.
static uint16_t g_PacketWords[8][255];

static void FillPacketWords(void)
{
    for (int n = 0; n < 8; n++)
        for (int i = 0; i < 255; i++)
            g_PacketWords[n][i] = Parity8((unsigned)(n * 53 + i * 37));
}

// Packets in the horizontal blanking after the payload ID and on a line without, in the
// vertical blanking two after each other, on C and Y and on links 3 and 4, their
// checksums worked out; the parser lists them back.
DT_TEST(AncPackets)
{
    FillPacketWords();
    char Message[160];
    const char* Names[] = {"525I59_94",  "625I50",  "720P24",
                           "1080I59_94", "1080P50", "2160P59_94"};
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        const int PidLine = F->Lines == 625 ? 9 : 10;
        const bool Sd = IsSd(F);
        const int Link = Is4k(F) ? 3 : F->SdiRate == DT_SDIRATE_3G ? 1 : 0;
        DtSdiAncPacket Packets[6] = {
            {PidLine, true, false, 0, 0x60, 0x60, 16, g_PacketWords[0], false},
            {12, false, !Sd, 0, 0x61, 0x01, 30, g_PacketWords[1], false},
            {12, false, !Sd, 0, 0x61, 0x02, 0, NULL, false},
            {12, false, false, Link, 0x41, 0x05, 255, g_PacketWords[2], false},
            {100, true, !Sd, Link, 0x50, 0x03, 7, g_PacketWords[3], false},
            {F->Lines, true, false, Is4k(F) ? 4 : 0, 0x88, 0x17, 1, g_PacketWords[4],
             false}};
        const int NumPackets = 6;
        DtSdiAncData Anc;
        memset(&Anc, 0, sizeof(Anc));
        Anc.Packets = Packets;
        Anc.NumPackets = NumPackets;

        size_t Size = 0;
        DT_ASSERT_EQ(DtSdiView_RawFrameSize(F->VidStd, 10, &Size), DTAPI_OK);
        uint8_t* Built = (uint8_t*)malloc(Size);
        uint8_t* Expected = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        DtSdiParser* Parser = DtSdiParser_Alloc();
        TestImage T;
        memset(&T, 0, sizeof(T));
        const bool Ready = Built != NULL && Expected != NULL && View != NULL &&
                           Builder != NULL && Parser != NULL &&
                           TestImage_Alloc(&T, F, EvenPattern);
        const char* Failure = Ready ? NULL : "out of memory";
        uint32_t Crc[8] = {0};
        if (Failure == NULL &&
            (DtSdiBuilder_SetChecksums(Builder, true) != DTAPI_OK ||
             DtSdiView_SetRawFrame(View, Built, Size, F->VidStd, 10) != DTAPI_OK ||
             DtSdiBuilder_Build(Builder, View, &T.Image, NULL, &Anc) != DTAPI_OK))
            Failure = "the build failed";
        if (Failure == NULL)
        {
            RefFrame(F, 10, EvenPattern, Packets, NumPackets, Crc, Expected, Size);
            Failure = Compare(F, 10, Built, Expected, Size, Message, sizeof(Message));
        }

        // The parser finds each packet where it was put, its checksum right.
        DtSdiAncPacket Found[16];
        uint16_t Words[2048];
        DtSdiAncData List = {Found, 16, 0, Words, 2048, 0, 0};
        if (Failure == NULL &&
            DtSdiParser_Parse(Parser, View, NULL, NULL, &List) != DTAPI_OK)
            Failure = "the parser refused the frame";
        if (Failure == NULL && List.NumPackets != NumPackets)
            Failure = "the parser lists another number of packets";
        for (int p = 0; Failure == NULL && p < NumPackets; p++)
        {
            const DtSdiAncPacket* E = &Packets[p];
            bool Seen = false;
            for (int q = 0; q < List.NumPackets && !Seen; q++)
            {
                const DtSdiAncPacket* P = &Found[q];
                const int ELink = E->VirtualInterface == 0 ? 1 : E->VirtualInterface;
                Seen =
                    P->Line == E->Line && P->InHanc == E->InHanc &&
                    P->OnChroma == E->OnChroma && P->Did == E->Did &&
                    P->SdidOrDbn == E->SdidOrDbn && P->NumWords == E->NumWords &&
                    P->ChecksumOk &&
                    (Sd || F->SdiRate == DT_SDIRATE_HD || P->VirtualInterface == ELink) &&
                    (E->NumWords == 0 ||
                     memcmp(P->Words, E->Words, (size_t)E->NumWords * sizeof(uint16_t)) ==
                         0);
            }
            if (!Seen)
            {
                snprintf(Message, sizeof(Message), "the parser misses packet %d", p);
                Failure = Message;
            }
        }

        TestImage_Free(&T);
        DtSdiParser_Free(Parser);
        DtSdiBuilder_Free(Builder);
        DtSdiView_Free(View);
        free(Expected);
        free(Built);
        if (Failure != NULL)
            DT_FAIL("%s: %s", F->Name, Failure);
    }
}

// An image in every pixel format builds the same frame as the 10-bit planar one it
// came from; the parser makes the images.
DT_TEST(EveryPixelFormat)
{
    static const DtSdiPixelFormat Formats[] = {
        DT_SDI_PIXFMT_UYVY_10B, DT_SDI_PIXFMT_UYVY_8B,     DT_SDI_PIXFMT_V210,
        DT_SDI_PIXFMT_Y210,     DT_SDI_PIXFMT_YUV422P_10B, DT_SDI_PIXFMT_YUV422P_8B};
    const char* Names[] = {"525I59_94", "720P24", "1080I50", "2160P50"};
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        size_t Size = 0;
        DT_ASSERT_EQ(DtSdiView_RawFrameSize(F->VidStd, 10, &Size), DTAPI_OK);
        uint8_t* First = (uint8_t*)malloc(Size);
        uint8_t* Again = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiParser* Parser = DtSdiParser_Alloc();
        TestImage T;
        memset(&T, 0, sizeof(T));
        const bool Ready = First != NULL && Again != NULL && View != NULL &&
                           Parser != NULL && TestImage_Alloc(&T, F, EvenPattern);
        const char* Failure = Ready ? NULL : "out of memory";
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        if (Failure == NULL &&
            (Builder == NULL ||
             DtSdiView_SetRawFrame(View, First, Size, F->VidStd, 10) != DTAPI_OK ||
             DtSdiBuilder_Build(Builder, View, &T.Image, NULL, NULL) != DTAPI_OK))
            Failure = "the first build failed";
        DtSdiBuilder_Free(Builder);

        for (size_t f = 0; Failure == NULL && f < sizeof(Formats) / sizeof(Formats[0]);
             f++)
        {
            int Width = 0;
            int Height = 0;
            int Strides[3] = {0, 0, 0};
            DtSdiImage_GetSize(F->VidStd, Formats[f], &Width, &Height, Strides);
            uint8_t* Planes = (uint8_t*)calloc(
                (size_t)(Strides[0] + Strides[1] + Strides[2]) * (size_t)Height, 1);
            DtSdiImage Image = {Formats[f],
                                DT_SDI_FIELDS_WOVEN,
                                {NULL, NULL, NULL},
                                {Strides[0], Strides[1], Strides[2]}};
            Builder = DtSdiBuilder_Alloc();
            if (Planes == NULL || Builder == NULL)
                Failure = "out of memory";
            else
            {
                Image.Planes[0] = Planes;
                if (Strides[1] != 0)
                {
                    Image.Planes[1] = Planes + (size_t)Strides[0] * (size_t)Height;
                    Image.Planes[2] =
                        Image.Planes[1] + (size_t)Strides[1] * (size_t)Height;
                }
                if (DtSdiView_SetRawFrame(View, First, Size, F->VidStd, 10) != DTAPI_OK ||
                    DtSdiParser_Parse(Parser, View, &Image, NULL, NULL) != DTAPI_OK ||
                    DtSdiView_SetRawFrame(View, Again, Size, F->VidStd, 10) != DTAPI_OK ||
                    DtSdiBuilder_Build(Builder, View, &Image, NULL, NULL) != DTAPI_OK)
                    Failure = "a build or parse failed";
                else if (memcmp(First, Again, Size) != 0)
                    Failure = "the frames differ";
            }
            DtSdiBuilder_Free(Builder);
            free(Planes);
            if (Failure != NULL)
                DT_FAIL("%s, format %d: %s", F->Name, (int)Formats[f], Failure);
        }

        TestImage_Free(&T);
        DtSdiParser_Free(Parser);
        DtSdiView_Free(View);
        free(Again);
        free(First);
        if (Failure != NULL)
            DT_FAIL("%s: %s", F->Name, Failure);
    }
}

// What the builder refuses, the frame left as it was.
DT_TEST(Refusals)
{
    FillPacketWords();
    const SdiFormat* Hd = NULL;
    const SdiFormat* Sd = NULL;
    for (int j = 0; j < SDI_FORMAT_COUNT; j++)
    {
        if (strcmp(g_SdiFormats[j].Name, "1080I50") == 0)
            Hd = &g_SdiFormats[j];
        if (strcmp(g_SdiFormats[j].Name, "625I50") == 0)
            Sd = &g_SdiFormats[j];
    }
    DT_ASSERT(Hd != NULL && Sd != NULL);
    size_t Size = 0;
    DT_ASSERT_EQ(DtSdiView_RawFrameSize(Hd->VidStd, 10, &Size), DTAPI_OK);
    uint8_t* Frame = (uint8_t*)malloc(Size);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DT_ASSERT(Frame != NULL && View != NULL && Builder != NULL);
    memset(Frame, 0xA5, Size);

    DT_ASSERT_EQ(DtSdiBuilder_SetChecksums(NULL, true), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtSdiBuilder_Build(NULL, View, NULL, NULL, NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, NULL, NULL, NULL, NULL),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, NULL, NULL), DTAPI_E_STATE);
    DT_ASSERT_EQ(DtSdiView_SetRawFrame(View, Frame, Size, Hd->VidStd, 10), DTAPI_OK);

    // An image without planes.
    DtSdiImage Image = {
        DT_SDI_PIXFMT_V210, DT_SDI_FIELDS_WOVEN, {NULL, NULL, NULL}, {0, 0, 0}};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, &Image, NULL, NULL),
                 DTAPI_E_INVALID_ARG);

    // Packets that do not fit, or name what they cannot.
    struct
    {
        DtSdiAncPacket Packet;
        DtapiResult Result;
    } Cases[] = {
        {{0, true, false, 0, 0x60, 0x60, 1, g_PacketWords[0], false},
         DTAPI_E_INVALID_LINE},
        {{1126, true, false, 0, 0x60, 0x60, 1, g_PacketWords[0], false},
         DTAPI_E_INVALID_LINE},
        {{100, false, false, 0, 0x60, 0x60, 1, g_PacketWords[0], false},
         DTAPI_E_INVALID_LINE},
        {{12, false, false, 2, 0x60, 0x60, 1, g_PacketWords[0], false},
         DTAPI_E_INVALID_ARG},
        {{12, false, false, 0, 0x160, 0x60, 1, g_PacketWords[0], false},
         DTAPI_E_INVALID_ARG},
        {{12, false, false, 0, 0x60, 0x60, 256, g_PacketWords[0], false},
         DTAPI_E_INVALID_ARG},
        {{12, false, false, 0, 0x60, 0x60, 3, NULL, false}, DTAPI_E_INVALID_ARG},
        {{12, true, false, 0, 0xE7, 0x00, 24, g_PacketWords[0], false},
         DTAPI_E_INVALID_ARG},
        {{12, true, false, 0, 0x41, 0x01, 4, g_PacketWords[0], false},
         DTAPI_E_INVALID_ARG},
    };
    for (size_t c = 0; c < sizeof(Cases) / sizeof(Cases[0]); c++)
    {
        DtSdiAncData Anc = {&Cases[c].Packet, 0, 1, NULL, 0, 0, 0};
        const DtapiResult Result = DtSdiBuilder_Build(Builder, View, NULL, NULL, &Anc);
        if (Result != Cases[c].Result)
            DT_FAIL("case %d: expected %d, got %d", (int)c, (int)Cases[c].Result,
                    (int)Result);
    }

    // More packets in one horizontal blanking than it holds: 1080i50's has 708 words
    // for packets, three of 255 words take 786.
    DtSdiAncPacket Three[3];
    for (int p = 0; p < 3; p++)
        Three[p] =
            (DtSdiAncPacket){50, true, true, 0, 0x60, 0x60, 255, g_PacketWords[p], false};
    DtSdiAncData Full = {Three, 0, 3, NULL, 0, 0, 0};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, NULL, &Full), DTAPI_E_TOO_LONG);
    Full.NumPackets = 2;
    for (size_t b = 0; b < Size; b++)
        DT_ASSERT_EQ(Frame[b], 0xA5);

    // Audio is plan 0032's step E.
    int32_t Samples[2048];
    DtSdiAudio Audio;
    memset(&Audio, 0, sizeof(Audio));
    Audio.Formats[0] = DT_SDI_AUDIO_PCM;
    Audio.Channels[0].Samples = Samples;
    Audio.Channels[0].Stride = 1;
    Audio.Channels[0].NumSamples = 1920;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, NULL),
                 DTAPI_E_NOT_SUPPORTED);
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, NULL, &Full), DTAPI_OK);

    // In SD, a packet has no chrominance stream to go in.
    size_t SdSize = 0;
    DT_ASSERT_EQ(DtSdiView_RawFrameSize(Sd->VidStd, 10, &SdSize), DTAPI_OK);
    DT_ASSERT(SdSize <= Size);
    DT_ASSERT_EQ(DtSdiView_SetRawFrame(View, Frame, SdSize, Sd->VidStd, 10), DTAPI_OK);
    DtSdiAncPacket OnC = {12, false, true, 0, 0x60, 0x60, 1, g_PacketWords[0], false};
    DtSdiAncData Anc = {&OnC, 0, 1, NULL, 0, 0, 0};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, NULL, &Anc),
                 DTAPI_E_INVALID_ARG);

    DtSdiBuilder_Freep(&Builder);
    DT_ASSERT(Builder == NULL);
    DtSdiView_Free(View);
    free(Frame);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frames of the sdi muxer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Builds each frame of <Name>.yuv and compares it with the muxer's in <Name>.raw, symbol
// by symbol, but for what plan 0032 names: the muxer's blanking is 200 in the Y streams
// too; its line CRCs in HD and up are wrong, the same on every line from line 2 on,
// so they are only counted; its horizontal blanking holds its audio and its payload ID,
// which are compared elsewhere. The rest must be equal: the timing references, line
// numbers, the image and the vertical blanking.
DT_TEST(FramesOfTheSdiMuxer)
{
    const char* Dir = getenv("CDTAPI_TEST_SDI_DIR");
    if (Dir == NULL || Dir[0] == '\0')
    {
        printf("    skipped: CDTAPI_TEST_SDI_DIR is not set\n");
        return;
    }

    int Checked = 0;
    for (int i = 0; i < SDI_FORMAT_COUNT; i++)
    {
        const SdiFormat* F = &g_SdiFormats[i];
        char Name[512];
        snprintf(Name, sizeof(Name), "%s/%s.raw", Dir, F->Name);
        FILE* Raw = fopen(Name, "rb");
        snprintf(Name, sizeof(Name), "%s/%s.yuv", Dir, F->Name);
        FILE* Yuv = fopen(Name, "rb");
        if (Raw == NULL || Yuv == NULL)
        {
            if (Raw != NULL)
                fclose(Raw);
            if (Yuv != NULL)
                fclose(Yuv);
            continue;
        }

        size_t Size = 0;
        DtSdiView_RawFrameSize(F->VidStd, 10, &Size);
        uint8_t* Muxed = (uint8_t*)malloc(Size);
        uint8_t* Built = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        TestImage T;
        memset(&T, 0, sizeof(T));
        const bool Ready = Muxed != NULL && Built != NULL && View != NULL &&
                           Builder != NULL && TestImage_Alloc(&T, F, NULL);
        const size_t ImageBytes = 4 * (size_t)T.Width * (size_t)T.Height;
        const char* Failure = Ready ? NULL : "out of memory";
        int NumFrames = 0;
        long BlankingY = 0;
        long Crcs = 0;
        const size_t PerLine = LineSymbols(F);
        const int Hanc = HancWords(F);

        while (Failure == NULL && fread(Muxed, 1, Size, Raw) == Size &&
               fread(T.Memory, 1, ImageBytes, Yuv) == ImageBytes)
        {
            if (DtSdiView_SetRawFrame(View, Built, Size, F->VidStd, 10) != DTAPI_OK ||
                DtSdiBuilder_Build(Builder, View, &T.Image, NULL, NULL) != DTAPI_OK)
            {
                Failure = "the build failed";
                break;
            }
            for (int Line = 1; Line <= F->Lines && Failure == NULL; Line++)
            {
                for (int s = 0; s < NumStreams(F) && Failure == NULL; s++)
                {
                    for (int k = 0; k < StreamWords(F); k++)
                    {
                        const size_t At =
                            (size_t)(Line - 1) * PerLine + SymbolOf(F, s, k);
                        const uint16_t A = GetSymbol(Built, 10, At);
                        const uint16_t B = GetSymbol(Muxed, 10, At);
                        const bool InHanc = k >= EavWords(F) && k < Hanc - 4;
                        const bool IsCrc = !IsSd(F) && (k == 6 || k == 7);
                        if (A == B || InHanc)
                            continue;
                        if (IsCrc)
                        {
                            Crcs++;
                            continue;
                        }
                        if (A == 0x040 && B == 0x200)
                        {
                            BlankingY++;
                            continue;
                        }
                        printf("    %s frame %d line %d stream %d word %d: built %03X, "
                               "muxed %03X\n",
                               F->Name, NumFrames, Line, s, k, A, B);
                        Failure = "the frames differ";
                        break;
                    }
                }
            }
            NumFrames++;
        }

        TestImage_Free(&T);
        DtSdiBuilder_Free(Builder);
        DtSdiView_Free(View);
        free(Built);
        free(Muxed);
        fclose(Raw);
        fclose(Yuv);
        if (Failure != NULL)
            DT_FAIL("%s: frame %d: %s", F->Name, NumFrames, Failure);
        if (IsSd(F))
            printf("    %s: %d frames; Y blanking 040 for 200: %ld words\n", F->Name,
                   NumFrames, BlankingY);
        else
            printf("    %s: %d frames; Y blanking 040 for 200: %ld words; line CRCs "
                   "that differ: %ld\n",
                   F->Name, NumFrames, BlankingY, Crcs);
        Checked++;
    }
    printf("    %d standards checked\n", Checked);
}

DT_TEST_MAIN("SdiBuilder", DT_RUN(EveryStandard), DT_RUN(BlackWithoutImage),
             DT_RUN(AncPackets), DT_RUN(EveryPixelFormat), DT_RUN(Refusals),
             DT_RUN(FramesOfTheSdiMuxer))

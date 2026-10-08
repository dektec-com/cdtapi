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
// every Y stream above SD, those of each link of 2160p too, with byte 3 zero and byte 4
// saying 10 bits. The program's packets follow it, in the order given. Unless the
// builder is asked for them, the CRC words are 200 (hex) and the checksums 0CC, for
// the transmitter to fill in.
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
        if (IsPayloadIdLine(F, Line) && (IsSd(F) || !C))
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

    // A packet with the DID of audio while the builder embeds audio of its own.
    int32_t Own[1920] = {0};
    DtSdiAudio Embedded;
    memset(&Embedded, 0, sizeof(Embedded));
    Embedded.Formats[0] = DT_SDI_AUDIO_PCM;
    Embedded.Channels[0].Samples = Own;
    Embedded.Channels[0].Stride = 1;
    Embedded.Channels[0].NumSamples = 1920;
    DtSdiAncPacket AudioPacket = {12,   true, false, 0, 0xE7, 0x00, 24, g_PacketWords[0],
                                  false};
    DtSdiAncData WithAudio = {&AudioPacket, 0, 1, NULL, 0, 0, 0};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Embedded, &WithAudio),
                 DTAPI_E_INVALID_ARG);

    // Nor an audio control packet of HD, which the builder writes itself.
    AudioPacket.Did = 0xE3;
    AudioPacket.NumWords = 11;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Embedded, &WithAudio),
                 DTAPI_E_INVALID_ARG);

    // Audio that a frame of 1080i50, 1920 samples a channel, cannot take.
    static int32_t Samples[2048];
    DtSdiAudio Audio;
    memset(&Audio, 0, sizeof(Audio));
    Audio.Formats[0] = DT_SDI_AUDIO_PCM;
    Audio.Channels[0].Samples = Samples;
    Audio.Channels[0].Stride = 1;
    Audio.Channels[0].NumSamples = 1919;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, NULL),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_EQ(Audio.NumSamplesUsed, 1920);
    Audio.Channels[0].NumSamples = 1920;
    Audio.FrameNumber = 2; // 25 Hz has no cadence
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, NULL),
                 DTAPI_E_INVALID_ARG);
    Audio.FrameNumber = 0;
    Audio.Channels[0].Stride = -1;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, NULL),
                 DTAPI_E_INVALID_ARG);
    Audio.Channels[0].Stride = 1;
    Audio.Formats[1] = (DtSdiAudioFormat)7;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, NULL),
                 DTAPI_E_INVALID_FORMAT);
    Audio.Formats[1] = DT_SDI_AUDIO_NONE;

    // Packets that fill a line's horizontal blanking of the C stream to the last word
    // fit without audio, and not with it: every line but the one after a switching line
    // carries a sample of group 1 there.
    DtSdiAncPacket Fill[3];
    for (int p = 0; p < 3; p++)
        Fill[p] = (DtSdiAncPacket){
            50, true, true, 0, 0x60, 0x60, p < 2 ? 255 : 177, g_PacketWords[p], false};
    DtSdiAncData Filled = {Fill, 0, 3, NULL, 0, 0, 0};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, &Filled),
                 DTAPI_E_TOO_LONG);
    for (size_t b = 0; b < Size; b++)
        DT_ASSERT_EQ(Frame[b], 0xA5);
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, NULL, &Filled), DTAPI_OK);
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Audio, &Full), DTAPI_OK);

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Audio +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The builder's audio is read back by the parser, which step C checked against the
// matrix and the muxer; what the parser cannot tell, where the packets lie, the test
// sees in the parser's list of packets. The expectations come from the standards and
// from the matrix: the cadence of 1001 rates as DTAPI divides it, no audio on the line
// after a switching line, the control packets two lines after it, a frame's share of
// samples a line at most, the channel status of decision 8.
//

// The samples per channel of the frame at place Place (from 1) of F's cadence.
static int ExpectedSamples(const SdiFormat* F, int Place)
{
    static const int At2997[5] = {1602, 1601, 1602, 1601, 1602};
    static const int At5994[5] = {801, 800, 801, 801, 801};
    if (F->FpsDen == 1001 && F->FpsNum == 30000)
        return At2997[Place - 1];
    if (F->FpsDen == 1001 && F->FpsNum == 60000)
        return At5994[Place - 1];
    return (int)(48000LL * F->FpsDen / F->FpsNum);
}

static int CadenceOf(const SdiFormat* F)
{
    return F->FpsDen == 1001 && F->FpsNum != 24000 ? 5 : 1;
}

// The switching lines, as a raw frame numbers its lines: ST 125's 10 and 273 three
// lower; BT.656's 6 and 319; 7 of ST 296; 7 and 569 of ST 274.
static int SwitchingLines(const SdiFormat* F, int Lines[2])
{
    if (F->Lines == 525)
    {
        Lines[0] = 7;
        Lines[1] = 270;
        return 2;
    }
    if (F->Lines == 625)
    {
        Lines[0] = 6;
        Lines[1] = 319;
        return 2;
    }
    Lines[0] = 7;
    Lines[1] = 569;
    return F->Lines == 1125 && F->Scan != SDI_SCAN_P ? 2 : 1;
}

static bool IsAfterSwitching(const SdiFormat* F, int Line, int After)
{
    int Lines[2];
    const int N = SwitchingLines(F, Lines);
    for (int i = 0; i < N; i++)
        if (Line == Lines[i] + After)
            return true;
    return false;
}

// A 24-bit value for each channel and sample.
static uint32_t Value24(int Channel, long Sample)
{
    uint32_t X = (uint32_t)Channel * 2654435761u + (uint32_t)Sample * 40503u + 12345u;
    X ^= X >> 13;
    X *= 0x5BD1E995u;
    X ^= X >> 15;
    return X & 0xFFFFFF;
}

static uint32_t Parity32(uint32_t Bits)
{
    uint32_t P = 0;
    for (; Bits != 0; Bits &= Bits - 1)
        P ^= 1;
    return P;
}

// The program's AES3 subframe of channel Channel: V, U and C in patterns of their own,
// Z every 192 samples on the first channel of the pair, and P set whether right or not.
static uint32_t ProgramAes3(int Channel, long Sample)
{
    uint32_t W = Value24(Channel, Sample) << 4;
    if (Sample % 7 == 0)
        W |= DT_SDI_AES3_V;
    if (Sample % 5 == 0)
        W |= DT_SDI_AES3_U;
    if ((Sample / 3) % 2 != 0)
        W |= DT_SDI_AES3_C;
    if (Channel % 2 == 0 && Sample % 192 == 0)
        W |= DT_SDI_AES3_Z;
    return W | DT_SDI_AES3_P;
}

// W with P even parity over bits 4 to 30.
static uint32_t WithRightP(uint32_t W)
{
    W &= ~DT_SDI_AES3_P;
    return W | Parity32(W & 0x7FFFFFF0u) << 31;
}

// The channel status block, bit n in the order it is sent: of PCM, professional, 48 kHz,
// stereo, 24 bits; of a channel its group lacks, professional, 48 kHz, 16 bits. AES3's
// tables give a field's bits in the order they are sent. The CRC is AES3's: x^8 + x^4
// + x^3 + x^2 + 1 over bits 0 to 183, the register all ones at the start, its highest
// bit sent first.
static void ExpectedStatus(bool Mute, uint8_t Bits[192])
{
    memset(Bits, 0, 192);
    Bits[0] = 1; // Professional
    Bits[7] = 1; // 48 kHz: bits 6 and 7 are 0 1
    if (Mute)
        Bits[19] = 1; // Word length 1 0 0: 16 bits of at most 20
    else
    {
        Bits[9] = 1;  // Channel mode 0 1 0 0: stereo
        Bits[18] = 1; // Auxiliary bits 0 0 1: at most 24 bits
        Bits[19] = 1; // Word length 1 0 1: 24 bits
        Bits[21] = 1;
    }
    unsigned Crc = 0xFF;
    for (int n = 0; n < 184; n++)
    {
        const unsigned Feedback = (Crc >> 7 & 1) ^ Bits[n];
        Crc = Crc << 1 & 0xFF;
        if (Feedback != 0)
            Crc ^= 0x1D;
    }
    for (int n = 0; n < 8; n++)
        Bits[184 + n] = (uint8_t)(Crc >> (7 - n) & 1);
}

#define AUDIO_MAX 2048
#define AUDIO_MAX_FRAMES 10
#define AUDIO_MAX_PACKETS 8192

// The buffers of the audio tests.
typedef struct AudioBufs
{
    int32_t Pcm[8][AUDIO_MAX];
    uint32_t Aes3[DT_SDI_AUDIO_MAX_CHANNELS][AUDIO_MAX];
    uint32_t Got[DT_SDI_AUDIO_MAX_CHANNELS][AUDIO_MAX];
    uint32_t Kept[3]
                 [AUDIO_MAX * AUDIO_MAX_FRAMES]; // Channels 1, 2 and 11 over the frames
    DtSdiAncPacket Packets[AUDIO_MAX_PACKETS];
    DtSdiAudio In;
    DtSdiAudio Out;
} AudioBufs;

// Sets B->Out to take every channel as AES3 subframes.
static void AudioBufs_Receive(AudioBufs* B)
{
    memset(&B->Out, 0, sizeof(B->Out));
    for (int p = 0; p < DT_SDI_AUDIO_MAX_CHANNELS / 2; p++)
        B->Out.Formats[p] = DT_SDI_AUDIO_AES3;
    for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS; c++)
    {
        B->Out.Channels[c].Samples = B->Got[c];
        B->Out.Channels[c].MaxSamples = AUDIO_MAX;
    }
}

// Checks where the audio packets of a frame of F lie, as the parser listed them, and
// that each group of Groups (a bit per group) carries Expected samples. Returns NULL, or
// what is wrong.
static const char* CheckAudioPackets(const SdiFormat* F, const DtSdiAncData* List,
                                     int Expected, unsigned Groups, char* Message,
                                     size_t Size)
{
    static int PerLine[1126][4];
    memset(PerLine, 0, sizeof(PerLine));
    int Total[4] = {0, 0, 0, 0};
    int Controls[4] = {0, 0, 0, 0};
    const bool Sd = IsSd(F);
    int Switching[2];
    const int NumFields = SwitchingLines(F, Switching);
    const int Lines = F->Lines;
    const int MaxPerLine = (Expected + Lines - NumFields - 1) / (Lines - NumFields);

    for (int p = 0; p < List->NumPackets; p++)
    {
        const DtSdiAncPacket* P = &List->Packets[p];
        const int Did = P->Did;
        const bool Data =
            Sd ? (Did >= 0xF9 && Did % 2 == 1) : (Did >= 0xE4 && Did <= 0xE7);
        const bool Control = !Sd && Did >= 0xE0 && Did <= 0xE3;
        if (!Data && !Control)
            continue;
        const int Group = Sd ? (0xFF - Did) / 2 : Data ? 0xE7 - Did : 0xE3 - Did;
        if (!P->InHanc || !P->ChecksumOk || (Is4k(F) && P->VirtualInterface != 1) ||
            (Groups >> Group & 1) == 0)
        {
            snprintf(Message, Size, "line %d: a packet of DID %02X where none belongs",
                     P->Line, Did);
            return Message;
        }
        if (Control)
        {
            if (P->OnChroma || !IsAfterSwitching(F, P->Line, 2) || P->NumWords != 11)
            {
                snprintf(Message, Size, "line %d: a control packet out of place",
                         P->Line);
                return Message;
            }
            Controls[Group]++;
            continue;
        }
        const int Samples = Sd ? P->NumWords / 12 : 1;
        if ((!Sd && (!P->OnChroma || P->NumWords != 24)) ||
            (Sd && P->NumWords % 12 != 0) || IsAfterSwitching(F, P->Line, 1))
        {
            snprintf(Message, Size, "line %d: a data packet out of place", P->Line);
            return Message;
        }
        PerLine[P->Line][Group] += Samples;
        Total[Group] += Samples;
    }

    for (int g = 0; g < 4; g++)
    {
        const bool On = (Groups >> g & 1) != 0;
        if (Total[g] != (On ? Expected : 0) || Controls[g] != (On && !Sd ? NumFields : 0))
        {
            snprintf(Message, Size, "group %d: %d samples and %d control packets", g + 1,
                     Total[g], Controls[g]);
            return Message;
        }
        for (int Line = 1; Line <= Lines; Line++)
        {
            if (PerLine[Line][g] > MaxPerLine)
            {
                snprintf(Message, Size, "line %d: %d samples, at most %d a line", Line,
                         PerLine[Line][g], MaxPerLine);
                return Message;
            }
        }
    }
    return NULL;
}

// Checks the channel status and the block starts of the samples kept of one channel:
// Z on every 192nd sample from the first when ZExpected, and the C bits of Mute's or
// PCM's status.
static const char* CheckStatus(const uint32_t* Kept, long Count, bool Mute,
                               bool ZExpected, char* Message, size_t Size)
{
    uint8_t Bits[192];
    ExpectedStatus(Mute, Bits);
    for (long s = 0; s < Count; s++)
    {
        const bool Z = (Kept[s] & DT_SDI_AES3_Z) != 0;
        const bool C = (Kept[s] & DT_SDI_AES3_C) != 0;
        if (Z != (ZExpected && s % 192 == 0) || C != (Bits[s % 192] != 0))
        {
            snprintf(Message, Size, "sample %ld: Z %d, C %d", s, (int)Z, (int)C);
            return Message;
        }
    }
    return NULL;
}

// Builds and parses frames of F with audio: channels 1 to 8 PCM, 9 and 10 the program's
// AES3, 11 and 12 not sent, so silent in group 3, and group 4 absent. Returns NULL, or
// what failed.
static const char* AudioOfStandard(const SdiFormat* F, AudioBufs* B, char* Message,
                                   size_t Size)
{
    const bool Sd = IsSd(F);
    const int Cadence = CadenceOf(F);
    const int NumFrames = Cadence == 5 ? (Is4k(F) ? 5 : AUDIO_MAX_FRAMES) : 3;
    size_t FrameSize = 0;
    DtSdiView_RawFrameSize(F->VidStd, 10, &FrameSize);
    uint8_t* Frame = (uint8_t*)malloc(FrameSize);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    const char* Failure = NULL;
    if (Frame == NULL || View == NULL || Builder == NULL || Parser == NULL)
        Failure = "out of memory";
    const DtSdiAncFilter All = {true, 0, true, 0, DT_SDI_ANC_SPACE_HANC, 0, 0};
    if (Failure == NULL && (DtSdiBuilder_SetChecksums(Builder, true) != DTAPI_OK ||
                            DtSdiParser_SetAudioChecks(Parser, true) != DTAPI_OK ||
                            DtSdiParser_SetAncFilter(Parser, &All, 1) != DTAPI_OK))
        Failure = "a setting was refused";

    long Sent = 0;
    for (int n = 0; Failure == NULL && n < NumFrames; n++)
    {
        const int Place = n % Cadence + 1;
        const int Expected = ExpectedSamples(F, Place);
        int Asked = 0;
        if (DtSdiBuilder_GetNumAudioSamples(Builder, F->VidStd, 0, &Asked) != DTAPI_OK ||
            Asked != Expected)
        {
            snprintf(Message, Size, "frame %d: %d samples asked, %d expected", n, Asked,
                     Expected);
            Failure = Message;
            break;
        }

        memset(&B->In, 0, sizeof(B->In));
        for (int c = 0; c < 10; c++)
        {
            B->In.Formats[c / 2] = c < 8 ? DT_SDI_AUDIO_PCM : DT_SDI_AUDIO_AES3;
            for (int i = 0; i < AUDIO_MAX; i++)
            {
                if (c < 8)
                    B->Pcm[c][i] = (int32_t)(Value24(c, Sent + i) << 8);
                else
                    B->Aes3[c][i] = ProgramAes3(c, Sent + i);
            }
            B->In.Channels[c].Samples = c < 8 ? (void*)B->Pcm[c] : (void*)B->Aes3[c];
            B->In.Channels[c].NumSamples = AUDIO_MAX;
        }
        if (DtSdiView_SetRawFrame(View, Frame, FrameSize, F->VidStd, 10) != DTAPI_OK ||
            DtSdiBuilder_Build(Builder, View, NULL, &B->In, NULL) != DTAPI_OK)
        {
            Failure = "the build failed";
            break;
        }
        if (B->In.NumSamplesUsed != Expected)
        {
            Failure = "NumSamplesUsed differs";
            break;
        }

        AudioBufs_Receive(B);
        DtSdiAncData List = {B->Packets, AUDIO_MAX_PACKETS, 0, NULL, 0, 0, 0};
        if (DtSdiParser_Parse(Parser, View, NULL, &B->Out, &List) != DTAPI_OK)
        {
            Failure = "the parse failed";
            break;
        }
        const int Number = Cadence == 1 || Sd ? 0 : Place;
        if (B->Out.FrameNumber != Number)
        {
            snprintf(Message, Size, "frame %d: frame number %d, expected %d", n,
                     B->Out.FrameNumber, Number);
            Failure = Message;
            break;
        }
        for (int g = 0; g < 4 && Failure == NULL; g++)
            if (B->Out.NumPacketErrors[g] != 0)
                Failure = "a packet's BCH code or checksum failed";
        if (Failure == NULL)
            Failure = CheckAudioPackets(F, &List, Expected, 0x7, Message, Size);

        // The samples, and what each subframe carries besides.
        const uint32_t Audio = Sd ? 0x0FFFFF00u : DT_SDI_AES3_AUDIO;
        for (int c = 0; c < DT_SDI_AUDIO_MAX_CHANNELS && Failure == NULL; c++)
        {
            const DtSdiAudioChannel* C = &B->Out.Channels[c];
            if (C->Present != (c < 12) || (c < 12 && C->NumSamples != Expected) ||
                C->Invalid != (c >= 8 && c < 12))
            {
                snprintf(Message, Size, "frame %d, channel %d: present %d, %d samples", n,
                         c + 1, (int)C->Present, C->NumSamples);
                Failure = Message;
                break;
            }
            for (int i = 0; i < (c < 12 ? Expected : 0); i++)
            {
                const uint32_t W = B->Got[c][i];
                // SD's parity covers the channel number and Z too, so only HD's is
                // that of the subframe.
                bool Right = Sd || Parity32(W & 0xFFFFFFF0u) == 0;
                if (c < 8)
                    Right = Right && (W & Audio) == (Value24(c, Sent + i) << 4 & Audio) &&
                            (W & (DT_SDI_AES3_V | DT_SDI_AES3_U)) == 0;
                else if (c < 10)
                {
                    const uint32_t Mask =
                        Sd ? Audio | DT_SDI_AES3_V | DT_SDI_AES3_U | DT_SDI_AES3_C
                           : 0xFFFFFFFFu;
                    Right = Right &&
                            (W & Mask) == (WithRightP(ProgramAes3(c, Sent + i)) & Mask);
                }
                else
                    Right =
                        Right && (W & DT_SDI_AES3_AUDIO) == 0 && (W & DT_SDI_AES3_V) != 0;
                if (!Right)
                {
                    snprintf(Message, Size, "frame %d, channel %d, sample %d: %08X", n,
                             c + 1, i, W);
                    Failure = Message;
                    break;
                }
            }
        }
        for (int i = 0; i < Expected && Failure == NULL; i++)
        {
            B->Kept[0][Sent + i] = B->Got[0][i];
            B->Kept[1][Sent + i] = B->Got[1][i];
            B->Kept[2][Sent + i] = B->Got[10][i];
        }
        Sent += Expected;
    }

    // The channel status over the frames: in HD Z on channels 1 and 3 of a group; in SD
    // on every channel of a sample from the first that has it.
    if (Failure == NULL)
        Failure = CheckStatus(B->Kept[0], Sent, false, true, Message, Size);
    if (Failure == NULL)
        Failure = CheckStatus(B->Kept[1], Sent, false, Sd, Message, Size);
    if (Failure == NULL)
        Failure = CheckStatus(B->Kept[2], Sent, true, true, Message, Size);

    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Builder);
    DtSdiView_Free(View);
    free(Frame);
    return Failure;
}

// Audio in a standard of each kind of line and rate, read back by the parser.
DT_TEST(AudioEveryKind)
{
    static const char* Names[] = {"525I59_94",  "625I50",  "720P50",
                                  "720P59_94",  "1080I50", "1080I59_94",
                                  "1080P23_98", "1080P50", "2160P59_94"};
    AudioBufs* B = (AudioBufs*)malloc(sizeof(AudioBufs));
    DT_ASSERT(B != NULL);
    char Message[160];
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        const char* Failure = AudioOfStandard(F, B, Message, sizeof(Message));
        if (Failure != NULL)
        {
            free(B);
            DT_FAIL("%s: %s", F->Name, Failure);
        }
    }
    free(B);
}

// The cadence: a place given goes on from there; a frame without audio moves it on too;
// another standard starts it afresh. Without the checksums, the parser's checks fail on
// every packet.
DT_TEST(AudioCadence)
{
    const SdiFormat* F = NULL;
    const SdiFormat* Other = NULL;
    for (int j = 0; j < SDI_FORMAT_COUNT; j++)
    {
        if (strcmp(g_SdiFormats[j].Name, "1080I59_94") == 0)
            F = &g_SdiFormats[j];
        if (strcmp(g_SdiFormats[j].Name, "1080I50") == 0)
            Other = &g_SdiFormats[j];
    }
    DT_ASSERT(F != NULL && Other != NULL);
    AudioBufs* B = (AudioBufs*)malloc(sizeof(AudioBufs));
    size_t Size = 0;
    size_t OtherSize = 0;
    DtSdiView_RawFrameSize(F->VidStd, 10, &Size);
    DtSdiView_RawFrameSize(Other->VidStd, 10, &OtherSize);
    uint8_t* Frame = (uint8_t*)malloc(Size > OtherSize ? Size : OtherSize);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DT_ASSERT(B != NULL && Frame != NULL && View != NULL && Builder != NULL &&
              Parser != NULL);
    DT_ASSERT_OK(DtSdiParser_SetAudioChecks(Parser, true));
    memset(&B->In, 0, sizeof(B->In));
    memset(B->Pcm[0], 0, sizeof(B->Pcm[0]));
    B->In.Formats[0] = DT_SDI_AUDIO_PCM;
    B->In.Channels[0].Samples = B->Pcm[0];
    B->In.Channels[0].NumSamples = AUDIO_MAX;

    // Place 3, then the builder's own: 4; a frame without audio, 5; then 1.
    const int Expected[] = {3, 4, 5, 1};
    for (int k = 0; k < 4; k++)
    {
        B->In.FrameNumber = k == 0 ? 3 : 0;
        int Asked = 0;
        DT_ASSERT_OK(DtSdiBuilder_GetNumAudioSamples(Builder, F->VidStd,
                                                     B->In.FrameNumber, &Asked));
        DT_ASSERT_EQ(Asked, ExpectedSamples(F, Expected[k]));
        DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 10));
        DT_ASSERT_OK(
            DtSdiBuilder_Build(Builder, View, NULL, k == 2 ? NULL : &B->In, NULL));
        if (k == 2)
            continue;
        AudioBufs_Receive(B);
        DT_ASSERT_OK(DtSdiParser_Parse(Parser, View, NULL, &B->Out, NULL));
        DT_ASSERT_EQ(B->Out.FrameNumber, Expected[k]);
        DT_ASSERT_EQ(B->Out.Channels[0].NumSamples, ExpectedSamples(F, Expected[k]));
        DT_ASSERT(B->Out.NumPacketErrors[0] > 0);
    }

    // Another standard, and back: the cadence starts at 1.
    B->In.FrameNumber = 0;
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, OtherSize, Other->VidStd, 10));
    DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, NULL, &B->In, NULL));
    DT_ASSERT_EQ(B->In.NumSamplesUsed, 1920);
    int Asked = 0;
    DT_ASSERT_OK(DtSdiBuilder_GetNumAudioSamples(Builder, F->VidStd, 0, &Asked));
    DT_ASSERT_EQ(Asked, ExpectedSamples(F, 1));
    DT_ASSERT_EQ(DtSdiBuilder_GetNumAudioSamples(Builder, F->VidStd, 6, &Asked),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtSdiBuilder_GetNumAudioSamples(Builder, 12345, 0, &Asked),
                 DTAPI_E_INVALID_VIDSTD);

    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Builder);
    DtSdiView_Free(View);
    free(Frame);
    free(B);
}

// Raw AES3 through the parser and the builder and back, frame for frame with the
// parser's frame number: the frames come out the same, channel status and block starts
// included.
DT_TEST(Aes3RoundTrip)
{
    static const char* Names[] = {"625I50", "1080I59_94"};
    AudioBufs* B = (AudioBufs*)malloc(sizeof(AudioBufs));
    DT_ASSERT(B != NULL);
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        size_t Size = 0;
        DtSdiView_RawFrameSize(F->VidStd, 10, &Size);
        uint8_t* First = (uint8_t*)malloc(Size);
        uint8_t* Again = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiBuilder* Pcm = DtSdiBuilder_Alloc();
        DtSdiBuilder* Raw = DtSdiBuilder_Alloc();
        DtSdiParser* Parser = DtSdiParser_Alloc();
        DT_ASSERT(First != NULL && Again != NULL && View != NULL && Pcm != NULL &&
                  Raw != NULL && Parser != NULL);
        DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Pcm, true));
        DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Raw, true));

        long Sent = 0;
        for (int n = 0; n < 6; n++)
        {
            // A frame of PCM in groups 1 and 2.
            memset(&B->In, 0, sizeof(B->In));
            for (int c = 0; c < 8; c++)
            {
                B->In.Formats[c / 2] = DT_SDI_AUDIO_PCM;
                for (int s = 0; s < AUDIO_MAX; s++)
                    B->Pcm[c][s] = (int32_t)(Value24(c, Sent + s) << 8);
                B->In.Channels[c].Samples = B->Pcm[c];
                B->In.Channels[c].NumSamples = AUDIO_MAX;
            }
            DT_ASSERT_OK(DtSdiView_SetRawFrame(View, First, Size, F->VidStd, 10));
            DT_ASSERT_OK(DtSdiBuilder_Build(Pcm, View, NULL, &B->In, NULL));
            Sent += B->In.NumSamplesUsed;

            // Its subframes, sent again as they came.
            AudioBufs_Receive(B);
            DT_ASSERT_OK(DtSdiParser_Parse(Parser, View, NULL, &B->Out, NULL));
            memset(&B->In, 0, sizeof(B->In));
            B->In.FrameNumber = B->Out.FrameNumber;
            for (int c = 0; c < 8; c++)
            {
                B->In.Formats[c / 2] = DT_SDI_AUDIO_AES3;
                memcpy(B->Aes3[c], B->Got[c], sizeof(B->Aes3[c]));
                B->In.Channels[c].Samples = B->Aes3[c];
                B->In.Channels[c].NumSamples = B->Out.Channels[c].NumSamples;
            }
            DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Again, Size, F->VidStd, 10));
            DT_ASSERT_OK(DtSdiBuilder_Build(Raw, View, NULL, &B->In, NULL));
            if (memcmp(First, Again, Size) != 0)
            {
                free(B);
                DT_FAIL("%s: frame %d: the frame built from AES3 differs", F->Name, n);
            }
        }
        DtSdiParser_Free(Parser);
        DtSdiBuilder_Free(Raw);
        DtSdiBuilder_Free(Pcm);
        DtSdiView_Free(View);
        free(Again);
        free(First);
    }
    free(B);
}

// A worker pool builds the frames one thread builds, in as many pieces as the standard
// calls for and in three and five, with audio, packets and checksums, twice in a row so
// that the CRC and the audio run on from frame to frame: of 2160p50, 1080i50, and
// 720p24, whose lines start half-way through a byte in 10 bits.
DT_TEST(WorkerPool)
{
    static const char* Names[] = {"2160P50", "1080I50", "720P24"};
    FillPacketWords();
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkerPool_StartThreads(Pool, 4));
    AudioBufs* B = (AudioBufs*)malloc(sizeof(AudioBufs));
    DT_ASSERT(B != NULL);
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        size_t Size = 0;
        DT_ASSERT_OK(DtSdiView_RawFrameSize(F->VidStd, 10, &Size));
        uint8_t* One = (uint8_t*)malloc(Size);
        uint8_t* Many = (uint8_t*)malloc(Size);
        DtSdiView* View = DtSdiView_Alloc();
        TestImage T;
        memset(&T, 0, sizeof(T));
        DT_ASSERT(One != NULL && Many != NULL && View != NULL &&
                  TestImage_Alloc(&T, F, FullPattern));
        DtSdiAncPacket Packets[2] = {
            {12, false, false, 0, 0x61, 0x01, 30, g_PacketWords[1], false},
            {600, true, !IsSd(F), 0, 0x60, 0x60, 16, g_PacketWords[0], false}};
        DtSdiAncData Anc = {Packets, 0, 2, NULL, 0, 0, 0};

        for (int Threads = 0; Threads <= 5; Threads += Threads == 0 ? 3 : 2)
        {
            DtSdiBuilder* Single = DtSdiBuilder_Alloc();
            DtSdiBuilder* Pooled = DtSdiBuilder_Alloc();
            DT_ASSERT(Single != NULL && Pooled != NULL);
            DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Single, true));
            DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Pooled, true));
            DT_ASSERT_OK(DtSdiBuilder_SetWorkerPool(Pooled, Pool, Threads));
            long Sent = 0;
            for (int n = 0; n < 2; n++)
            {
                memset(&B->In, 0, sizeof(B->In));
                for (int c = 0; c < 6; c++)
                {
                    B->In.Formats[c / 2] = DT_SDI_AUDIO_PCM;
                    for (int s = 0; s < AUDIO_MAX; s++)
                        B->Pcm[c][s] = (int32_t)(Value24(c, Sent + s) << 8);
                    B->In.Channels[c].Samples = B->Pcm[c];
                    B->In.Channels[c].NumSamples = AUDIO_MAX;
                }
                DT_ASSERT_OK(DtSdiView_SetRawFrame(View, One, Size, F->VidStd, 10));
                DT_ASSERT_OK(DtSdiBuilder_Build(Single, View, &T.Image, &B->In, &Anc));
                DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Many, Size, F->VidStd, 10));
                DT_ASSERT_OK(DtSdiBuilder_Build(Pooled, View, &T.Image, &B->In, &Anc));
                Sent += B->In.NumSamplesUsed;
                if (memcmp(One, Many, Size) != 0)
                {
                    free(B);
                    DT_FAIL("%s, %d threads, frame %d: the frames differ", F->Name,
                            Threads, n);
                }
            }
            DtSdiBuilder_Free(Pooled);
            DtSdiBuilder_Free(Single);
        }
        TestImage_Free(&T);
        DtSdiView_Free(View);
        free(Many);
        free(One);
    }
    free(B);
    DtWorkerPool_Free(Pool);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The program's own packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define OWN_MAX_PACKETS 8192
#define OWN_MAX_WORDS (OWN_MAX_PACKETS * 64)

// The buffers of OwnAudioAndPayloadId.
typedef struct OwnBufs
{
    int32_t Pcm[6][AUDIO_MAX];
    DtSdiAncPacket Packets[OWN_MAX_PACKETS];
    uint16_t Words[OWN_MAX_WORDS];
} OwnBufs;

// A frame's packets, every DID, as the parser lists them, built again by a builder that
// embeds no audio of its own, give the frame back word for word: the payload ID and the
// audio are then the program's, and the builder writes neither of its own. A payload ID
// of the program's own takes the place of the builder's. In SD, HD, 3G and 2160p.
DT_TEST(OwnAudioAndPayloadId)
{
    static const char* Names[] = {"625I50", "1080I50", "1080P50", "2160P50"};
    FillPacketWords();
    OwnBufs* B = (OwnBufs*)malloc(sizeof(OwnBufs));
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DtSdiView* View = DtSdiView_Alloc();
    DT_ASSERT(B != NULL && Parser != NULL && View != NULL);
    const DtSdiAncFilter All = {true, 0, true, 0, DT_SDI_ANC_SPACE_BOTH, 0, 0};
    DT_ASSERT_OK(DtSdiParser_SetAncFilter(Parser, &All, 1));

    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
    {
        const SdiFormat* F = NULL;
        for (int j = 0; j < SDI_FORMAT_COUNT; j++)
            if (strcmp(g_SdiFormats[j].Name, Names[i]) == 0)
                F = &g_SdiFormats[j];
        DT_ASSERT(F != NULL);
        size_t Size = 0;
        DT_ASSERT_OK(DtSdiView_RawFrameSize(F->VidStd, 10, &Size));
        uint8_t* First = (uint8_t*)malloc(Size);
        uint8_t* Again = (uint8_t*)malloc(Size);
        TestImage T;
        memset(&T, 0, sizeof(T));
        DT_ASSERT(First != NULL && Again != NULL && TestImage_Alloc(&T, F, FullPattern));

        // The first frame: the builder's audio and payload ID, and two packets of the
        // program's.
        DtSdiAudio Audio;
        memset(&Audio, 0, sizeof(Audio));
        for (int c = 0; c < 6; c++)
        {
            Audio.Formats[c / 2] = DT_SDI_AUDIO_PCM;
            for (int s = 0; s < AUDIO_MAX; s++)
                B->Pcm[c][s] = (int32_t)(Value24(c, s) << 8);
            Audio.Channels[c].Samples = B->Pcm[c];
            Audio.Channels[c].NumSamples = AUDIO_MAX;
        }
        DtSdiAncPacket Program[2] = {
            {12, false, false, 0, 0x61, 0x01, 30, g_PacketWords[1], false},
            {600, true, !IsSd(F), 0, 0x60, 0x60, 16, g_PacketWords[0], false}};
        DtSdiAncData Anc = {Program, 0, 2, NULL, 0, 0, 0};
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        DT_ASSERT(Builder != NULL);
        DT_ASSERT_OK(DtSdiView_SetRawFrame(View, First, Size, F->VidStd, 10));
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, &T.Image, &Audio, &Anc));
        DtSdiBuilder_Free(Builder);

        // Every packet of it, built again without audio of the builder's own.
        DtSdiAncData List;
        memset(&List, 0, sizeof(List));
        List.Packets = B->Packets;
        List.MaxPackets = OWN_MAX_PACKETS;
        List.Words = B->Words;
        List.MaxWords = OWN_MAX_WORDS;
        DT_ASSERT_OK(DtSdiParser_Parse(Parser, View, NULL, NULL, &List));
        int PayloadIds = 0;
        int AudioPackets = 0;
        for (int p = 0; p < List.NumPackets; p++)
        {
            PayloadIds += B->Packets[p].Did == 0x41 && B->Packets[p].SdidOrDbn == 0x01;
            AudioPackets += B->Packets[p].Did >= 0xE0 && B->Packets[p].Did <= 0xFF;
        }
        DT_ASSERT(PayloadIds > 0 && AudioPackets > 0);
        Builder = DtSdiBuilder_Alloc();
        DT_ASSERT(Builder != NULL);
        DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Again, Size, F->VidStd, 10));
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, &T.Image, NULL, &List));
        if (memcmp(First, Again, Size) != 0)
        {
            size_t b = 0;
            while (First[b] == Again[b])
                b++;
            DT_FAIL("%s: the frame built again differs from byte %zu on", F->Name, b);
        }

        // A payload ID of the program's own in the place of the builder's.
        for (int p = 0; p < List.NumPackets; p++)
        {
            const DtSdiAncPacket* P = &B->Packets[p];
            if (P->Did == 0x41 && P->SdidOrDbn == 0x01)
            {
                const ptrdiff_t At = P->Words - B->Words;
                for (int w = 0; w < 4; w++)
                    B->Words[At + w] = 0x2AA;
            }
        }
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, &T.Image, NULL, &List));
        uint32_t PayloadId = 0;
        DT_ASSERT_OK(DtSdiView_GetPayloadId(View, &PayloadId));
        DT_ASSERT_EQ(PayloadId, 0xAAAAAAAAu);
        DtSdiAncData Rebuilt;
        memset(&Rebuilt, 0, sizeof(Rebuilt));
        Rebuilt.Packets = B->Packets;
        Rebuilt.MaxPackets = OWN_MAX_PACKETS;
        Rebuilt.Words = B->Words;
        Rebuilt.MaxWords = OWN_MAX_WORDS;
        DT_ASSERT_OK(DtSdiParser_Parse(Parser, View, NULL, NULL, &Rebuilt));
        int Again41 = 0;
        for (int p = 0; p < Rebuilt.NumPackets; p++)
            Again41 += B->Packets[p].Did == 0x41 && B->Packets[p].SdidOrDbn == 0x01;
        DT_ASSERT_EQ(Again41, PayloadIds);

        DtSdiBuilder_Free(Builder);
        TestImage_Free(&T);
        free(Again);
        free(First);
    }
    DtSdiView_Free(View);
    DtSdiParser_Free(Parser);
    free(B);
}

// In SD the builder writes no audio control packet, and takes the program's own beside
// its audio, before the audio of its line, as SMPTE ST 272 has it. The parser takes the
// place in the cadence from it: in 525i the lowest three bits of AF1-2, here 4 below a
// counter of 3 that the bits above may carry.
DT_TEST(SdAudioControlOfTheProgram)
{
    const SdiFormat* F = NULL;
    for (int j = 0; j < SDI_FORMAT_COUNT; j++)
        if (strcmp(g_SdiFormats[j].Name, "525I59_94") == 0)
            F = &g_SdiFormats[j];
    DT_ASSERT(F != NULL);
    int Switching[2] = {0, 0};
    SwitchingLines(F, Switching);
    const int Line = Switching[0] + 2;

    size_t Size = 0;
    DT_ASSERT_OK(DtSdiView_RawFrameSize(F->VidStd, 10, &Size));
    uint8_t* Frame = (uint8_t*)malloc(Size);
    OwnBufs* B = (OwnBufs*)malloc(sizeof(OwnBufs));
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DT_ASSERT(Frame != NULL && B != NULL && View != NULL && Builder != NULL &&
              Parser != NULL);

    DtSdiAudio Audio;
    memset(&Audio, 0, sizeof(Audio));
    Audio.Formats[0] = DT_SDI_AUDIO_PCM;
    for (int c = 0; c < 2; c++)
    {
        for (int s = 0; s < AUDIO_MAX; s++)
            B->Pcm[c][s] = (int32_t)(Value24(c, s) << 8);
        Audio.Channels[c].Samples = B->Pcm[c];
        Audio.Channels[c].NumSamples = AUDIO_MAX;
    }

    // AF1-2 and AF3-4 of frame 4 with a counter of 3 above it, 48 kHz, channels 1 and 2
    // active, no delay: each word's bit 9 the complement of bit 8, ACT's bit 8 its
    // parity.
    uint16_t Words[18];
    for (int w = 0; w < 18; w++)
        Words[w] = 0x200;
    Words[0] = 0x200 | (3 << 3 | 4);
    Words[1] = Words[0];
    Words[3] = 0x200 | 0x3;
    DtSdiAncPacket Control = {Line, true, false, 0, 0xEF, 0x00, 18, Words, false};
    DtSdiAncData Anc = {&Control, 0, 1, NULL, 0, 0, 0};
    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, F->VidStd, 10));
    DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, NULL, &Audio, &Anc));

    // The program's packet as sent, before the audio of its line.
    const DtSdiAncFilter All = {true, 0, true, 0, DT_SDI_ANC_SPACE_BOTH, 0, 0};
    DT_ASSERT_OK(DtSdiParser_SetAncFilter(Parser, &All, 1));
    DtSdiAncData List;
    memset(&List, 0, sizeof(List));
    List.Packets = B->Packets;
    List.MaxPackets = OWN_MAX_PACKETS;
    List.Words = B->Words;
    List.MaxWords = OWN_MAX_WORDS;
    int32_t Got[2][AUDIO_MAX];
    DtSdiAudio Received;
    memset(&Received, 0, sizeof(Received));
    Received.Formats[0] = DT_SDI_AUDIO_PCM;
    for (int c = 0; c < 2; c++)
    {
        Received.Channels[c].Samples = Got[c];
        Received.Channels[c].MaxSamples = AUDIO_MAX;
    }
    DT_ASSERT_OK(DtSdiParser_Parse(Parser, View, NULL, &Received, &List));
    DT_ASSERT_EQ(Received.FrameNumber, 4);
    int Controls = 0;
    int DataBefore = 0;
    int DataOnLine = 0;
    for (int p = 0; p < List.NumPackets; p++)
    {
        const DtSdiAncPacket* P = &B->Packets[p];
        if (P->Did == 0xEF)
        {
            Controls++;
            DT_ASSERT_EQ(P->Line, Line);
            DT_ASSERT_EQ(P->NumWords, 18);
            DT_ASSERT(memcmp(P->Words, Words, sizeof(Words)) == 0);
            DataBefore = DataOnLine;
        }
        DataOnLine += P->Did == 0xFF && P->Line == Line;
    }
    DT_ASSERT_EQ(Controls, 1);
    DT_ASSERT_EQ(DataBefore, 0);
    DT_ASSERT(DataOnLine > 0);

    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Builder);
    DtSdiView_Free(View);
    free(B);
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
             DT_RUN(AudioEveryKind), DT_RUN(AudioCadence), DT_RUN(Aes3RoundTrip),
             DT_RUN(WorkerPool), DT_RUN(OwnAudioAndPayloadId),
             DT_RUN(SdAudioControlOfTheProgram), DT_RUN(FramesOfTheSdiMuxer))

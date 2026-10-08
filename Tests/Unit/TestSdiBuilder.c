// #*#*#*#*#*#*#*#*#*#*#*#*#* TestSdiBuilder.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The builder's image, raster and ancillary packets
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Checks each frame the builder makes, byte for byte, against a reference frame that
// this file makes from the standards' own numbers. These numbers are:
// - the lines and their active parts of SMPTE ST 125, BT.656, ST 274 and ST 296, as
//   SdiFormats.inc has them;
// - the timing references from ST 125's table;
// - the line numbers and CRC-18 of ST 292, with the CRC worked out bit by bit;
// - the packets of ST 291;
// - for 2160p, how ST 2082-10 divides the image over four links.
//
// Where the standards leave a choice, the reference frame uses these values:
// - The blanking is 200 (hex) in a C stream and 040 in a Y stream.
// - The payload ID comes right after EAV on the lines that ST 352 names. Above SD, it is
//   in every Y stream, which in 2160p means in each link. Its byte 3 is zero and its
//   byte 4 says 10 bits.
// - The program's packets follow the payload ID, in the order given.
// - Unless the builder is asked to compute them, the CRC words are 200 (hex) and the
//   checksums 0CC, for the transmitter to fill in.
//
// When CDTAPI_TEST_SDI_DIR is set, one more case builds frames from the images that
// FFmpeg's sdi muxer was given, <Name>.yuv. It compares them with the muxer's frames,
// <Name>.raw. The frames differ where plan 0032 says:
// - the muxer's blanking is 200 in a Y stream as well;
// - the muxer's line CRCs are wrong, because it takes the line's first word over and
//   over;
// - the muxer's horizontal blanking holds its audio;
// - the muxer's payload ID has bytes of its own.

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

// Returns the number of streams in a raw line. SD has one stream. HD and 3G have a C and
// a Y stream. 2160p has a C and a Y stream for each of four links. Stream 2 * (Link - 1)
// is a link's C stream, and the stream after it is the link's Y stream.
static int NumStreams(const SdiFormat* F)
{
    return IsSd(F) ? 1 : Is4k(F) ? 8 : 2;
}

// The next three functions return a number of words in one stream of a line.
// StreamWords() counts the whole line. HancWords() counts the horizontal blanking,
// including EAV and SAV. EavWords() counts EAV together with the line number and CRC.
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

// Returns the number of symbols in a raw line.
static size_t LineSymbols(const SdiFormat* F)
{
    return (size_t)(2 * F->Samples) * (Is4k(F) ? 4 : 1);
}

// Returns the symbol in a raw line that holds word k of stream s. In 2160p, the line
// holds word k of the C streams of links 4, 2, 3 and 1, followed by word k of their Y
// streams.
static size_t SymbolOf(const SdiFormat* F, int s, int k)
{
    static const int Place[4] = {3, 1, 2, 0};
    if (IsSd(F))
        return (size_t)k;
    if (Is4k(F))
        return 8 * (size_t)k + (size_t)Place[s / 2] + (s & 1 ? 4 : 0);
    return 2 * (size_t)k + (size_t)s;
}

// Holds the first and last active line of each field. Lines count from 1, as in a raw
// frame. In 2160p, these are the lines of one link.
typedef struct ActiveLines
{
    int NumFields;
    int First[2];
    int Last[2];
} ActiveLines;

static ActiveLines GetActiveLines(const SdiFormat* F)
{
    if (F->Lines == 525)
        return (ActiveLines){2, {17, 280}, {260, 522}}; // ST 125, counted three lower
    if (F->Lines == 625)
        return (ActiveLines){2, {23, 336}, {310, 623}}; // BT.656
    if (F->Lines == 750)
        return (ActiveLines){1, {26, 0}, {745, 0}}; // ST 296
    if (F->Scan == SDI_SCAN_P)
        return (ActiveLines){1, {42, 0}, {1121, 0}}; // ST 274, one field
    return (ActiveLines){2, {21, 584}, {560, 1123}}; // ST 274, two fields
}

// Returns the field of line Line, 0 or 1.
static int FieldOf(const SdiFormat* F, int Line)
{
    return F->Scan != SDI_SCAN_P && Line > F->LinesF1 ? 1 : 0;
}

// Returns whether line Line holds part of the image.
static bool IsImageLine(const SdiFormat* F, int Line)
{
    const ActiveLines A = GetActiveLines(F);
    const int f = FieldOf(F, Line);
    return Line >= A.First[f] && Line <= A.Last[f];
}

// Returns the fourth word of a timing reference. ST 125 gives a table of eight values,
// chosen by the bits F, V and H.
static uint16_t Xyz(const SdiFormat* F, int Line, bool Eav)
{
    static const uint16_t Legal[8] = {0x200, 0x274, 0x2AC, 0x2D8,
                                      0x31C, 0x368, 0x3B0, 0x3C4};
    const int V = IsImageLine(F, Line) ? 0 : 1;
    return Legal[FieldOf(F, Line) * 4 + V * 2 + (Eav ? 1 : 0)];
}

// Returns whether ST 352 puts the payload ID on line Line. These lines are:
// - 13 and 276 of ST 125, which a raw frame counts as 10 and 273;
// - 9 and 322 of BT.656;
// - 10 of ST 296;
// - 10 and 572 of ST 274.
static bool IsPayloadIdLine(const SdiFormat* F, int Line)
{
    if (F->Lines == 525)
        return Line == 10 || Line == 273;
    if (F->Lines == 625)
        return Line == 9 || Line == 322;
    return Line == 10 || (F->Scan != SDI_SCAN_P && Line == 572);
}

// Returns the VPID that the reference frames carry, which is the one the DekTec matrix
// sends. Byte 1 is in the lowest bits. The bytes are:
// - byte 1, the payload;
// - byte 2, the picture rate, with bit 7 set for a progressive transport except in
//   720p, and bit 6 set for a progressive picture;
// - byte 3, zero;
// - byte 4, one for 10 bits.
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

// Returns SMPTE 292's CRC-18 after one more word, taking the least significant bit
// first. The function models the 18 stages of a shift register with feedback into x^0,
// x^4 and x^5.
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

// Returns the same CRC as BitCrc18() over Count words, ten bits at a time. The CRC is
// linear. So the register's lower ten bits, combined with the word, select one table
// entry, and the register's upper bits are shifted down and combined with it. The table
// comes from BitCrc18(), and makes a whole frame quick to check.
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

// Returns the lower nine bits of Value, with bit 9 set to the inverse of bit 8.
static uint16_t Protected(uint32_t Value)
{
    Value &= 0x1FF;
    return (uint16_t)((Value & 0x100) != 0 ? Value : Value | 0x200);
}

// Returns the lower eight bits of Value, with their even parity in bit 8 and the
// inverse of that bit in bit 9.
static uint16_t Parity8(unsigned Value)
{
    int Ones = 0;
    for (int b = 0; b < 8; b++)
        Ones += (int)(Value >> b & 1);
    const unsigned P = (unsigned)(Ones & 1);
    return (uint16_t)((Value & 0xFF) | P << 8 | (P ^ 1) << 9);
}

// Writes a packet of ST 291 at Words[Pos]. The checksum is worked out when Checksum is
// true, and is 0CC otherwise. Returns the word after the packet.
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

// Returns the value of symbol s of image line y in a test pattern. There are two
// patterns. FullPattern() uses values from 0 to 1023, which the builder limits to 4 to
// 1019. EvenPattern() uses multiples of 4 from 4 to 1016, which every pixel format can
// hold.
typedef uint16_t (*Pattern)(int y, int s);

static uint16_t FullPattern(int y, int s)
{
    return (uint16_t)((y * 31 + s * 7 + (s & 1) * 300) % 1024);
}

static uint16_t EvenPattern(int y, int s)
{
    return (uint16_t)(4 * (1 + (y * 29 + s * 5 + (s & 1) * 300) % 254));
}

// Returns Value limited to 4 to 1019, as the builder limits it.
static uint16_t Legal(uint16_t Value)
{
    return Value < 4 ? 4 : Value > 1019 ? 1019 : Value;
}

// Holds a yuv422p10le image of F's size. Planes[0] holds Y, Planes[1] Cb and Planes[2]
// Cr.
typedef struct TestImage
{
    DtSdiImage Image;
    uint8_t* Memory;
    int Width;
    int Height;
} TestImage;

// Allocates T for standard F and fills it with Value, unless Value is NULL. Returns
// false when the size is unknown or there is no memory.
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

// Writes into Words[s] the words that the builder must make for stream s of line Line.
// - Value gives the image, or is NULL for no image.
// - Packets holds the program's packets.
// - Crc holds each stream's CRC over the active part of the line before, and the
//   function updates it. When Crc is NULL, the CRCs and the checksums are left to the
//   transmitter.
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

        // Writes the image. In 2160p, the pixel pairs of image line 2k go to links 1 and
        // 2 in turn, and those of line 2k + 1 go to links 3 and 4.
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

// Writes Value as symbol Index of a raw frame with Bits bits a symbol. In 10 bits, the
// function ORs the bits in, so the frame must start out zeroed.
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

// Returns symbol Index of a raw frame with Bits bits a symbol.
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

// Holds the words of each stream of the line that RefFrame() is making.
static uint16_t g_Words[8][8400];

// Writes into Frame the frame that the builder must make. The function first zeroes the
// Size bytes of Frame. The other parameters are those of RefLine().
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

// Returns NULL when Built equals Expected. Otherwise returns a message that names the
// first symbol that differs, or says that the padding differs.
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

// Returns whether F is one of the standards that are also built with 16 bits a symbol,
// and twice in a row. These are one standard of each kind of line, 720p24 and the widest
// line of 2160p. 720p24 is included because its lines start part-way through a byte.
// Every other standard shares its line layout with one of these.
static bool IsBuiltInFull(const SdiFormat* F)
{
    static const char* Names[] = {"525I59_94", "625I50",  "720P24",
                                  "1080I50",   "1080P50", "2160P23_98"};
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
        if (strcmp(F->Name, Names[i]) == 0)
            return true;
    return false;
}

// Checks that the builder makes the reference frame for every standard, with 10 bits a
// symbol. The image has samples that need limiting, and the builder works out the
// checksums. For the standards of IsBuiltInFull(), the test also builds with 16 bits a
// symbol, and builds twice in a row. The second frame's first CRC must then cover the
// first frame's last line. The parser must also find the payload ID, with the
// standard's payload in byte 1.
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
            // The parser reads back the payload ID, with byte 1 in the top bits.
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

// Checks that without an image the builder makes the active part black, with the
// blanking's values. The test runs in 525i59.94, 720p50, 1080i50 and 2160p50. The CRCs
// and the payload ID's checksum are left to the transmitter.
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

// Holds the user data words of test packet n. Each word holds eight bits with parity,
// as most packets carry.
static uint16_t g_PacketWords[8][255];

// Fills g_PacketWords.
static void FillPacketWords(void)
{
    for (int n = 0; n < 8; n++)
        for (int i = 0; i < 255; i++)
            g_PacketWords[n][i] = Parity8((unsigned)(n * 53 + i * 37));
}

// Checks that the builder writes the program's packets where the reference frame has
// them, with their checksums worked out. The packets go:
// - in the horizontal blanking, after the payload ID and on a line without one;
// - in the vertical blanking, two after each other;
// - on C and on Y, and on links 3 and 4.
// The parser must then list each packet back, with a correct checksum.
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

        // Checks that the parser finds each packet where it was put, with a correct
        // checksum.
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

// Checks that an image in every pixel format builds the same frame as the 10-bit planar
// image it came from. The parser makes the image in each format from the first frame.
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

// Checks what the builder refuses, and that it leaves the frame unchanged when it
// refuses.
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

    // The builder refuses an image without planes.
    DtSdiImage Image = {
        DT_SDI_PIXFMT_V210, DT_SDI_FIELDS_WOVEN, {NULL, NULL, NULL}, {0, 0, 0}};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, &Image, NULL, NULL),
                 DTAPI_E_INVALID_ARG);

    // The builder refuses packets that do not fit, or that name a line, link, DID or
    // size that is not possible.
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

    // The builder refuses more packets in one horizontal blanking than it holds. 1080i50
    // has 708 words for packets there, and three packets of 255 words take 786.
    DtSdiAncPacket Three[3];
    for (int p = 0; p < 3; p++)
        Three[p] =
            (DtSdiAncPacket){50, true, true, 0, 0x60, 0x60, 255, g_PacketWords[p], false};
    DtSdiAncData Full = {Three, 0, 3, NULL, 0, 0, 0};
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, NULL, &Full), DTAPI_E_TOO_LONG);
    Full.NumPackets = 2;
    for (size_t b = 0; b < Size; b++)
        DT_ASSERT_EQ(Frame[b], 0xA5);

    // The builder refuses an audio packet from the program while it embeds audio.
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

    // The builder also refuses an HD audio control packet, which it writes itself.
    AudioPacket.Did = 0xE3;
    AudioPacket.NumWords = 11;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, View, NULL, &Embedded, &WithAudio),
                 DTAPI_E_INVALID_ARG);

    // The builder refuses audio that a frame of 1080i50 cannot take. Such a frame takes
    // 1920 samples a channel.
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

    // Packets that fill the horizontal blanking of a line's C stream to the last word fit
    // without audio. They do not fit with audio, because every line except the one after
    // a switching line carries a sample of group 1 there.
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

    // In SD, the builder refuses a packet on chroma, because SD has no C stream.
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
// These tests read the builder's audio back with the parser. Step C of plan 0032 checked
// the parser against frames from the DekTec matrix and the muxer. The parser's samples
// do not show where the packets lie, so the tests check that in the parser's list of
// packets. The expected
// values come from the standards and from the DekTec matrix:
// - the number of samples in each frame of the cadence of the 1001 rates;
// - no audio on the line after a switching line;
// - the control packets two lines after a switching line;
// - at most a frame's share of samples on a line;
// - the channel status of plan 0032's decision 8.
//

// Returns the number of samples a channel has in frame Place of F's cadence. Places
// count from 1.
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

// Returns the number of frames in F's audio cadence: 5 at 29.97 and 59.94 Hz, else 1.
static int CadenceOf(const SdiFormat* F)
{
    return F->FpsDen == 1001 && F->FpsNum != 24000 ? 5 : 1;
}

// Fills Lines with the switching lines of F and returns how many there are. Lines count
// as in a raw frame. The switching lines are:
// - 10 and 273 of ST 125, which a raw frame counts three lower;
// - 6 and 319 of BT.656;
// - 7 of ST 296;
// - 7 and 569 of ST 274.
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

// Returns whether Line comes After lines after a switching line of F.
static bool IsAfterSwitching(const SdiFormat* F, int Line, int After)
{
    int Lines[2];
    const int N = SwitchingLines(F, Lines);
    for (int i = 0; i < N; i++)
        if (Line == Lines[i] + After)
            return true;
    return false;
}

// Returns a pseudo-random 24-bit value for each channel and sample.
static uint32_t Value24(int Channel, long Sample)
{
    uint32_t X = (uint32_t)Channel * 2654435761u + (uint32_t)Sample * 40503u + 12345u;
    X ^= X >> 13;
    X *= 0x5BD1E995u;
    X ^= X >> 15;
    return X & 0xFFFFFF;
}

// Returns 1 when Bits has an odd number of ones, and 0 otherwise.
static uint32_t Parity32(uint32_t Bits)
{
    uint32_t P = 0;
    for (; Bits != 0; Bits &= Bits - 1)
        P ^= 1;
    return P;
}

// Returns the AES3 subframe that the program gives for sample Sample of channel Channel.
// V, U and C each follow a pattern of their own. Z is set every 192 samples on the first
// channel of the pair. P is always set, whether that parity is right or not.
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

// Returns W with P set to the even parity over bits 4 to 30.
static uint32_t WithRightP(uint32_t W)
{
    W &= ~DT_SDI_AES3_P;
    return W | Parity32(W & 0x7FFFFFF0u) << 31;
}

// Fills Bits with the channel status block that the builder must send. Bits[n] is bit n
// in the order it is sent. For PCM, the status says professional, 48 kHz, stereo and 24
// bits. For a channel that its group lacks (Mute), it says professional, 48 kHz and 16
// bits. AES3's tables give a field's bits in the order they are sent.
//
// The CRC is that of AES3, x^8 + x^4 + x^3 + x^2 + 1, over bits 0 to 183. The register
// starts as all ones, and its highest bit is sent first.
static void ExpectedStatus(bool Mute, uint8_t Bits[192])
{
    memset(Bits, 0, 192);
    Bits[0] = 1; // Professional
    Bits[7] = 1; // 48 kHz, where bits 6 and 7 are 0 1
    if (Mute)
        Bits[19] = 1; // Word length 1 0 0, which is 16 bits of at most 20
    else
    {
        Bits[9] = 1;  // Channel mode 0 1 0 0, which is stereo
        Bits[18] = 1; // Auxiliary bits 0 0 1, which allow at most 24 bits
        Bits[19] = 1; // Word length 1 0 1, which is 24 bits
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

// Holds the buffers of the audio tests.
typedef struct AudioBufs
{
    int32_t Pcm[8][AUDIO_MAX];
    uint32_t Aes3[DT_SDI_AUDIO_MAX_CHANNELS][AUDIO_MAX];
    uint32_t Got[DT_SDI_AUDIO_MAX_CHANNELS][AUDIO_MAX];
    uint32_t Kept[3]
                 [AUDIO_MAX * AUDIO_MAX_FRAMES]; // Channels 1, 2 and 11 over all frames
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

// Checks where the audio packets of a frame of F lie, from the parser's list of packets.
// Also checks that each group in Groups carries Expected samples. Groups has a bit for
// each group. The function checks that:
// - each packet is in HANC, of a group in Groups, with a correct checksum, and in 2160p
//   on link 1;
// - each control packet is on Y, two lines after a switching line, with 11 words;
// - no data packet is on the line after a switching line, and HD data packets are on C
//   with 24 words;
// - each group has one control packet a switching line in HD, and none in SD;
// - no line carries more than a frame's share of samples.
// Returns NULL, or a message that says what is wrong.
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

// Checks the channel status and the block starts in the kept samples of one channel.
// When ZExpected is true, Z must be set on every 192nd sample from the first, and clear
// otherwise. The C bits must carry the status of ExpectedStatus(Mute). Returns NULL, or
// a message that says which sample is wrong.
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

// Builds frames of F with audio and parses them back. The audio is:
// - PCM on channels 1 to 8;
// - the program's AES3 subframes on channels 9 and 10;
// - nothing on channels 11 and 12, so these are silent in group 3;
// - no group 4.
// The function checks the number of samples, the frame number, the packets, the samples
// and the channel status. Returns NULL, or a message that says what failed.
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

        // Checks the samples, and the other bits that each subframe carries.
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
                // Checks the parity in HD only. In SD, the parity also covers the
                // channel number and Z, so it is not the parity of the subframe.
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

    // Checks the channel status over all frames. In HD, Z is set on channels 1 and 3 of
    // a group. In SD, Z is set on every channel of a sample, from the first channel that
    // has it.
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

// Checks the builder's audio, as the parser reads it back, in a standard of each kind
// of line and rate. The checks are those of AudioOfStandard().
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

// Checks how the builder follows the audio cadence of 1080i59.94. The test checks that:
// - the cadence goes on from a frame number that the program gives;
// - a frame without audio also moves the cadence on;
// - a frame of another standard starts the cadence again at 1;
// - frame number 6 and an unknown standard are refused.
// The builder does not work out the checksums here, so the parser's checks fail on
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

    // The program gives frame 3. The builder then counts on to 4, to 5 for a frame
    // without audio, and to 1.
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

    // After a frame of another standard, the cadence starts again at 1.
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

// Checks that raw AES3 survives a round trip through the parser and the builder. The
// test builds frames from PCM, parses them as AES3 and builds them again from that AES3,
// with the parser's frame number. This runs for six frames each of 625i50 and
// 1080i59.94. The frames must come out the same, including the channel status and the
// block starts.
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
            // Builds a frame with PCM in groups 1 and 2.
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

            // Parses the frame's subframes and builds them again as they came.
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

// Checks that a worker pool of four threads builds the same frames as one thread. The
// test runs in 2160p50, 1080i50 and 720p24. In 10 bits, the lines of 720p24 start
// part-way through a byte. The work is split into as many pieces as the standard calls
// for, and into three and five pieces. The frames have audio, packets and checksums.
// Each case builds two frames in a row, so that the CRC and the audio run on from one
// frame to the next.
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

// Holds the buffers of OwnAudioAndPayloadId and SdAudioControlOfTheProgram.
typedef struct OwnBufs
{
    int32_t Pcm[6][AUDIO_MAX];
    DtSdiAncPacket Packets[OWN_MAX_PACKETS];
    uint16_t Words[OWN_MAX_WORDS];
} OwnBufs;

// Checks that a program can supply its own audio packets and payload ID.
//
// The test builds a frame with audio and parses it with a filter for every DID. Those
// packets go to a builder that has no audio of its own. That builder must make the same
// frame, word for word, so it must add no audio and no payload ID of its own. Then a
// different payload ID from the program must replace the builder's. The test runs in
// 625i50, 1080i50, 1080p50 and 2160p50.
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

        // Builds the first frame with the builder's audio and payload ID, and two
        // packets of the program.
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

        // Parses all the frame's packets and builds the frame again from them, without
        // audio.
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

        // Replaces the payload ID in the list with a different one.
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

// Checks SD audio control packets from the program. The builder writes none itself. It
// accepts the program's packet alongside its own audio, and puts it before the audio of
// its line, as SMPTE ST 272 asks. The parser reads the frame number from the packet. In
// 525i, that is the lowest three bits of AF1-2. Here the frame number is 4, with a
// counter of 3 in the higher bits, which the parser must ignore.
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

    // Fills the 18 words of the control packet. AF1-2 and AF3-4 hold frame 4 with
    // counter 3. RATE is 48 kHz, ACT marks channels 1 and 2 active, and there is no
    // delay. Bit 9 of each word is the inverse of bit 8, and bit 8 of ACT is its parity.
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

    // The program's packet must come back unchanged, before the audio on its line.
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

// Checks that the builder makes the frames of FFmpeg's sdi muxer from the same images.
// The test builds each frame of <Name>.yuv and compares it with the muxer's frame in
// <Name>.raw, symbol by symbol. The timing references, the line numbers, the image and
// the vertical blanking must be equal. The test allows the differences that plan 0032
// names:
// - The muxer's blanking is 200 in the Y streams too.
// - In HD and up, the muxer's line CRCs are wrong. They are the same on every line from
//   line 2 on, so the test only counts them.
// - The muxer's horizontal blanking holds its audio and its payload ID. Other tests
//   compare those.
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

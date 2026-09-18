// #*#*#*#*#*#*#*#*#*#*#*#*#* ExampleTsStream.c *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Test transport streams: numbered packets, and one MPEG-2 video service -
// Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// Example includes
#include "ExampleTsStream.h" // Interface being implemented.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Constants -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

#define TS_SIZE 188
#define PID_PAT 0x0000
#define PID_SDT 0x0011
#define PID_PMT 0x1000
#define PID_VIDEO 0x0100
#define PID_NULL 0x1FFF

#define FPS 25
#define MB_COLS 45 // 720 / 16
#define MB_ROWS 36 // 576 / 16

// Presentation 0.5 s after the frame's first packet, in 90 kHz ticks.
#define PTS_DELAY 45000

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Numbered packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleTs_Numbered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void ExampleTs_Numbered(uint64_t Number, int Size, uint8_t* Packet)
{
    Packet[0] = 0x47;
    Packet[1] = (uint8_t)(PID_VIDEO >> 8 & 0x1F);
    Packet[2] = (uint8_t)(PID_VIDEO & 0xFF);
    Packet[3] = (uint8_t)(0x10 | (Number & 0x0F));
    for (int i = 0; i < 8; i++)
        Packet[4 + i] = (uint8_t)(Number >> (56 - 8 * i));
    for (int i = 12; i < Size; i++)
        Packet[i] = (uint8_t)(Number + (uint64_t)i);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleTs_IsNumbered -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool ExampleTs_IsNumbered(const uint8_t* Packet, int Size, uint64_t* Number)
{
    uint64_t N = 0;
    for (int i = 0; i < 8; i++)
        N = N << 8 | Packet[4 + i];

    uint8_t Expected[204];
    ExampleTs_Numbered(N, Size, Expected);
    if (memcmp(Packet, Expected, (size_t)Size) != 0)
        return false;
    *Number = N;
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The picture +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A frame is 45 x 36 macroblocks, each one colour: 75 % colour bars over the upper half,
// and on black below them "CDTAPI", the time code in white, and a red block bouncing
// along the bottom. Letters and digits are 3 x 5 macroblocks.
//

typedef struct Colour
{
    uint8_t Y, Cb, Cr;
} Colour;

// ITU-R BT.601, 8 bits.
static const Colour Bars[7] = {
    {180, 128, 128}, // White
    {162, 44, 142},  // Yellow
    {131, 156, 44},  // Cyan
    {112, 72, 58},   // Green
    {84, 184, 198},  // Magenta
    {65, 100, 212},  // Red
    {35, 212, 114},  // Blue
};
static const int BarWidths[7] = {7, 6, 6, 7, 6, 6, 7};
static const Colour Black = {16, 128, 128};
static const Colour White = {235, 128, 128};
static const Colour Yellow = {210, 16, 146};
static const Colour Red = {81, 90, 240};

// One glyph: five rows of three bits, the most significant on the left.
typedef struct Glyph
{
    char Char;
    uint8_t Rows[5];
} Glyph;

static const Glyph Glyphs[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}},
    {'3', {7, 1, 7, 1, 7}}, {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}},
    {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 1, 2, 2}}, {'8', {7, 5, 7, 5, 7}},
    {'9', {7, 5, 7, 1, 7}}, {':', {0, 2, 0, 2, 0}}, {'C', {7, 4, 4, 4, 7}},
    {'D', {6, 5, 5, 5, 6}}, {'T', {7, 2, 2, 2, 2}}, {'A', {2, 5, 7, 5, 5}},
    {'P', {7, 5, 7, 4, 4}}, {'I', {7, 2, 2, 2, 7}},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrawText -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Draws Text in Colour with its top left corner at macroblock Col, Row, one macroblock
// between characters; a colon is one macroblock wide.
//
static void DrawText(Colour Picture[MB_ROWS][MB_COLS], int Col, int Row, const char* Text,
                     Colour Ink)
{
    for (; *Text != '\0'; Text++)
    {
        const Glyph* G = NULL;
        for (size_t i = 0; i < sizeof(Glyphs) / sizeof(Glyphs[0]); i++)
        {
            if (Glyphs[i].Char == *Text)
                G = &Glyphs[i];
        }
        if (G == NULL)
            continue;
        const bool Narrow = *Text == ':';
        for (int y = 0; y < 5; y++)
        {
            for (int x = 0; x < 3; x++)
            {
                if (Narrow && x != 1)
                    continue;
                const int c = Col + (Narrow ? 0 : x);
                if ((G->Rows[y] >> (2 - x) & 1) != 0 && c >= 0 && c < MB_COLS)
                    Picture[Row + y][c] = Ink;
            }
        }
        Col += Narrow ? 2 : 4;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrawPicture -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void DrawPicture(Colour Picture[MB_ROWS][MB_COLS], int64_t Frame)
{
    for (int Row = 0; Row < MB_ROWS; Row++)
    {
        int Bar = 0, BarEnd = BarWidths[0];
        for (int Col = 0; Col < MB_COLS; Col++)
        {
            if (Col == BarEnd)
                BarEnd += BarWidths[++Bar];
            Picture[Row][Col] = Row < 18 ? Bars[Bar] : Black;
        }
    }
    DrawText(Picture, 11, 20, "CDTAPI", Yellow);

    const int64_t Ff = Frame % FPS, Ss = Frame / FPS % 60;
    const int64_t Mm = Frame / (FPS * 60) % 60, Hh = Frame / (FPS * 3600) % 24;
    char Text[12] = "00:00:00:00";
    const int64_t Fields[4] = {Hh, Mm, Ss, Ff};
    for (int i = 0; i < 4; i++)
    {
        Text[3 * i] = (char)('0' + Fields[i] / 10);
        Text[3 * i + 1] = (char)('0' + Fields[i] % 10);
    }
    DrawText(Picture, 4, 27, Text, White);

    // Back and forth along the bottom, two macroblocks square.
    const int Span = MB_COLS - 2, Phase = (int)(Frame % (2 * Span));
    const int Col = Phase < Span ? Phase : 2 * Span - Phase;
    for (int y = 33; y < 35; y++)
        Picture[y][Col] = Picture[y][Col + 1] = Red;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= MPEG-2 video +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// ISO/IEC 13818-2, main profile at main level. A macroblock of one colour has in each of
// its six blocks only a DC coefficient, coded as the difference from the previous block's
// of the same component (7.2.1), and an end of block.
//

typedef struct BitWriter
{
    uint8_t* Buf;
    size_t Bits;
} BitWriter;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Put(BitWriter* W, uint32_t Value, int NumBits)
{
    for (int i = NumBits - 1; i >= 0; i--)
    {
        if ((Value >> i & 1) != 0)
            W->Buf[W->Bits / 8] |= (uint8_t)(0x80 >> (W->Bits % 8));
        W->Bits++;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutStartCode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PutStartCode(BitWriter* W, uint8_t Code)
{
    W->Bits = (W->Bits + 7) / 8 * 8;
    Put(W, 0x000001, 24);
    Put(W, Code, 8);
}

// dct_dc_size_luminance and dct_dc_size_chrominance, Tables B.12 and B.13: code, length.
static const uint16_t DcSizeLuma[12][2] = {
    {0x4, 3},  {0x0, 2},  {0x1, 2},  {0x5, 3},  {0x6, 3},   {0xE, 4},
    {0x1E, 5}, {0x3E, 6}, {0x7E, 7}, {0xFE, 8}, {0x1FE, 9}, {0x1FF, 9},
};
static const uint16_t DcSizeChroma[12][2] = {
    {0x0, 2},  {0x1, 2},  {0x2, 2},  {0x6, 3},   {0xE, 4},    {0x1E, 5},
    {0x3E, 6}, {0x7E, 7}, {0xFE, 8}, {0x1FE, 9}, {0x3FE, 10}, {0x3FF, 10},
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutBlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One block of a flat colour: the DC difference from *Pred, and the end of block.
//
static void PutBlock(BitWriter* W, int Value, int* Pred, bool Luma)
{
    const int Diff = Value - *Pred;
    *Pred = Value;
    int Size = 0;
    for (int Magnitude = Diff < 0 ? -Diff : Diff; Magnitude != 0; Magnitude >>= 1)
        Size++;
    const uint16_t* Code = Luma ? DcSizeLuma[Size] : DcSizeChroma[Size];
    Put(W, Code[0], Code[1]);
    if (Size > 0)
        Put(W, (uint32_t)(Diff > 0 ? Diff : Diff + (1 << Size) - 1), Size);
    Put(W, 0x2, 2); // End of block, Table B.14
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodePicture -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the frame as a sequence header, a group of pictures and an I picture into Buf,
// which must be cleared and large enough; returns the bytes written.
//
static size_t EncodePicture(uint8_t* Buf, int64_t Frame)
{
    Colour Picture[MB_ROWS][MB_COLS];
    DrawPicture(Picture, Frame);
    BitWriter W = {Buf, 0};

    PutStartCode(&W, 0xB3); // sequence_header
    Put(&W, 720, 12);
    Put(&W, 576, 12);
    Put(&W, 2, 4);      // 4:3
    Put(&W, 3, 4);      // 25 frames a second
    Put(&W, 37500, 18); // 15 Mbit/s, the level's maximum, in units of 400 bit/s
    Put(&W, 1, 1);
    Put(&W, 112, 10); // VBV buffer of 112 x 16 kbit, the level's maximum
    Put(&W, 0, 3);    // Not constrained; no quantiser matrices

    PutStartCode(&W, 0xB5); // sequence_extension
    Put(&W, 1, 4);
    Put(&W, 0x48, 8); // Main profile, main level
    Put(&W, 1, 1);    // Progressive
    Put(&W, 1, 2);    // 4:2:0
    Put(&W, 0, 16);   // Size and bit rate extensions
    Put(&W, 1, 1);
    Put(&W, 0, 8);
    Put(&W, 1, 1); // Low delay: no B pictures
    Put(&W, 0, 7);

    PutStartCode(&W, 0xB8); // group_of_pictures_header, time code of the frame
    Put(&W, 0, 1);
    Put(&W, (uint32_t)(Frame / (FPS * 3600) % 24), 5);
    Put(&W, (uint32_t)(Frame / (FPS * 60) % 60), 6);
    Put(&W, 1, 1);
    Put(&W, (uint32_t)(Frame / FPS % 60), 6);
    Put(&W, (uint32_t)(Frame % FPS), 6);
    Put(&W, 1, 1); // Closed
    Put(&W, 0, 1);

    PutStartCode(&W, 0x00); // picture_header
    Put(&W, 0, 10);         // Temporal reference
    Put(&W, 1, 3);          // I picture
    Put(&W, 0xFFFF, 16);    // No VBV delay
    Put(&W, 0, 1);

    PutStartCode(&W, 0xB5); // picture_coding_extension
    Put(&W, 8, 4);
    Put(&W, 0xFFFF, 16); // f_codes, unused
    Put(&W, 0, 2);       // DC in 8 bits
    Put(&W, 3, 2);       // Frame picture
    Put(&W, 0, 1);
    Put(&W, 1, 1); // frame_pred_frame_dct
    Put(&W, 0, 5); // Concealment vectors, q_scale_type, VLC format, scan, repeat
    Put(&W, 1, 1); // chroma_420_type
    Put(&W, 1, 1); // Progressive frame
    Put(&W, 0, 1);

    for (int Row = 0; Row < MB_ROWS; Row++)
    {
        PutStartCode(&W, (uint8_t)(Row + 1)); // slice
        Put(&W, 8, 5);                        // quantiser_scale_code
        Put(&W, 0, 1);
        int PredY = 128, PredCb = 128, PredCr = 128;
        for (int Col = 0; Col < MB_COLS; Col++)
        {
            const Colour C = Picture[Row][Col];
            Put(&W, 1, 1); // macroblock_address_increment 1
            Put(&W, 1, 1); // macroblock_type intra
            for (int b = 0; b < 4; b++)
                PutBlock(&W, C.Y, &PredY, true);
            PutBlock(&W, C.Cb, &PredCb, false);
            PutBlock(&W, C.Cr, &PredCr, false);
        }
    }
    return (W.Bits + 7) / 8;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Transport stream +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Crc32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The CRC of PSI sections, ISO/IEC 13818-1 annex A.
//
static uint32_t Crc32(const uint8_t* Data, size_t Size)
{
    uint32_t Crc = 0xFFFFFFFF;
    for (size_t i = 0; i < Size; i++)
    {
        Crc ^= (uint32_t)Data[i] << 24;
        for (int b = 0; b < 8; b++)
            Crc = (Crc & 0x80000000) != 0 ? Crc << 1 ^ 0x04C11DB7 : Crc << 1;
    }
    return Crc;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PutHeader(uint8_t* P, int Pid, bool Start, int AdaptationControl, uint8_t Cc)
{
    P[0] = 0x47;
    P[1] = (uint8_t)((Start ? 0x40 : 0) | Pid >> 8);
    P[2] = (uint8_t)(Pid & 0xFF);
    P[3] = (uint8_t)(AdaptationControl << 4 | (Cc & 0xF));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutPsi -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A packet with one section: Body is the section from its table_id up to the CRC, which
// this adds, with the section_length still to be filled in.
//
static void PutPsi(uint8_t* P, int Pid, uint8_t* Cc, uint8_t* Body, size_t Size)
{
    const size_t Length = Size - 3 + 4;
    Body[1] = (uint8_t)(0xB0 | Length >> 8);
    Body[2] = (uint8_t)(Length & 0xFF);
    const uint32_t Crc = Crc32(Body, Size);
    Body[Size] = (uint8_t)(Crc >> 24);
    Body[Size + 1] = (uint8_t)(Crc >> 16);
    Body[Size + 2] = (uint8_t)(Crc >> 8);
    Body[Size + 3] = (uint8_t)Crc;

    PutHeader(P, Pid, true, 1, (*Cc)++);
    P[4] = 0; // pointer_field
    memcpy(P + 5, Body, Size + 4);
    memset(P + 5 + Size + 4, 0xFF, TS_SIZE - 5 - Size - 4);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutTable -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PutTable(ExampleTsStream* Stream, int Table, uint8_t* P)
{
    uint8_t S[TS_SIZE];
    size_t n = 0;
    if (Table == 0) // PAT: program 1 in PID_PMT
    {
        const uint8_t Pat[] = {
            0x00,          0, 0, 0x00, 0x01, 0xC1, 0, 0, 0x00, 0x01, 0xE0 | PID_PMT >> 8,
            PID_PMT & 0xFF};
        memcpy(S, Pat, n = sizeof(Pat));
        PutPsi(P, PID_PAT, &Stream->Cc[0], S, n);
    }
    else if (Table == 1) // PMT: MPEG-2 video, which carries the PCR
    {
        const uint8_t Pmt[] = {0x02,
                               0,
                               0,
                               0x00,
                               0x01,
                               0xC1,
                               0,
                               0,
                               0xE0 | PID_VIDEO >> 8,
                               PID_VIDEO & 0xFF,
                               0xF0,
                               0x00,
                               0x02,
                               0xE0 | PID_VIDEO >> 8,
                               PID_VIDEO & 0xFF,
                               0xF0,
                               0x00};
        memcpy(S, Pmt, n = sizeof(Pmt));
        PutPsi(P, PID_PMT, &Stream->Cc[1], S, n);
    }
    else // SDT: the service's name
    {
        static const char Provider[] = "DekTec", Name[] = "CDTAPI ASI test";
        const uint8_t Head[] = {0x42, 0,    0,    0x00, 0x01, 0xC1, 0,    0,
                                0x00, 0x01, 0xFF, 0x00, 0x01, 0xFC, 0x80, 0};
        memcpy(S, Head, n = sizeof(Head));
        const size_t Start = n;
        S[n++] = 0x48; // service_descriptor
        S[n++] = (uint8_t)(3 + strlen(Provider) + strlen(Name));
        S[n++] = 0x01; // Digital television
        S[n++] = (uint8_t)strlen(Provider);
        memcpy(S + n, Provider, strlen(Provider));
        n += strlen(Provider);
        S[n++] = (uint8_t)strlen(Name);
        memcpy(S + n, Name, strlen(Name));
        n += strlen(Name);
        S[Start - 1] = (uint8_t)(n - Start); // Running, not scrambled; loop length
        PutPsi(P, PID_SDT, &Stream->Cc[2], S, n);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FirstPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The first packet of frame Frame, as the constant rate places frames.
//
static uint64_t FirstPacket(int64_t Rate, int64_t Frame)
{
    return (uint64_t)(Frame * Rate / (FPS * TS_SIZE * 8));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NextFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void NextFrame(ExampleTsStream* Stream)
{
    Stream->Frame++;
    Stream->FrameEnd = FirstPacket(Stream->Rate, Stream->Frame + 1);
    memset(Stream->Es, 0, (size_t)Stream->EsCapacity);

    // PES header with the presentation time.
    const uint64_t Pts =
        (uint64_t)(Stream->Frame * (90000 / FPS) + PTS_DELAY) & 0x1FFFFFFFF;
    uint8_t* H = Stream->Es;
    H[0] = 0x00, H[1] = 0x00, H[2] = 0x01, H[3] = 0xE0;
    H[4] = H[5] = 0x00; // Unbounded, as a video PES in a transport stream may be
    H[6] = 0x84;        // Data aligned
    H[7] = 0x80;        // PTS
    H[8] = 5;
    H[9] = (uint8_t)(0x21 | (Pts >> 29 & 0x0E));
    H[10] = (uint8_t)(Pts >> 22);
    H[11] = (uint8_t)(0x01 | (Pts >> 14 & 0xFE));
    H[12] = (uint8_t)(Pts >> 7);
    H[13] = (uint8_t)(0x01 | (Pts << 1 & 0xFE));
    Stream->EsSize = 14 + (int)EncodePicture(Stream->Es + 14, Stream->Frame);
    Stream->EsSent = 0;
    Stream->PsiSent = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutVideo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The next packet of the frame's PES packet; the first carries the PCR of its own time.
//
static void PutVideo(ExampleTsStream* Stream, uint8_t* P)
{
    const bool First = Stream->EsSent == 0;
    const int Left = Stream->EsSize - Stream->EsSent;
    const int Room = 184 - (First ? 8 : 0);
    const int Payload = Left < Room ? Left : Room;
    const int Adaptation =
        184 - Payload; // Bytes of adaptation field, its length included

    PutHeader(P, PID_VIDEO, First, Adaptation > 0 ? 3 : 1, Stream->Cc[3]++);
    uint8_t* A = P + 4;
    if (Adaptation > 0)
    {
        A[0] = (uint8_t)(Adaptation - 1);
        if (Adaptation > 1)
        {
            A[1] = First ? 0x10 : 0x00;
            size_t n = 2;
            if (First)
            {
                // The PCR is the time of its last byte, 11 bytes into the packet.
                const uint64_t Bits = (Stream->Packet * TS_SIZE + 11) * 8;
                const uint64_t Rate = (uint64_t)Stream->Rate;
                const uint64_t Pcr =
                    (Bits / Rate) * 27000000 + Bits % Rate * 27000000 / Rate;
                const uint64_t Base = Pcr / 300 & 0x1FFFFFFFF, Ext = Pcr % 300;
                A[2] = (uint8_t)(Base >> 25);
                A[3] = (uint8_t)(Base >> 17);
                A[4] = (uint8_t)(Base >> 9);
                A[5] = (uint8_t)(Base >> 1);
                A[6] = (uint8_t)((Base & 1) << 7 | 0x7E | Ext >> 8);
                A[7] = (uint8_t)Ext;
                n = 8;
            }
            memset(A + n, 0xFF, (size_t)Adaptation - n);
        }
    }
    memcpy(A + Adaptation, Stream->Es + Stream->EsSent, (size_t)Payload);
    Stream->EsSent += Payload;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Interface +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleTsStream_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool ExampleTsStream_Init(ExampleTsStream* Stream, int64_t Rate)
{
    memset(Stream, 0, sizeof(*Stream));
    if (Rate < EXAMPLE_TS_MIN_RATE || Rate > EXAMPLE_TS_MAX_RATE)
        return false;
    Stream->Rate = Rate;
    Stream->Frame = -1;
    // A macroblock takes at most 2 + 4 x 19 + 2 x 20 bits, and each slice 6 bytes more.
    Stream->EsCapacity = 14 + 64 + MB_ROWS * (6 + MB_COLS * 118 / 8 + 1);
    Stream->Es = (uint8_t*)calloc(1, (size_t)Stream->EsCapacity);
    return Stream->Es != NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleTsStream_Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void ExampleTsStream_Close(ExampleTsStream* Stream)
{
    free(Stream->Es);
    Stream->Es = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleTsStream_Next -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame period sends the three tables, then the frame, then null packets. A frame that
// has not been sent by the end of its period finishes first, and the next one follows.
//
void ExampleTsStream_Next(ExampleTsStream* Stream, uint8_t* Packet)
{
    if (Stream->Packet >= Stream->FrameEnd && Stream->EsSent == Stream->EsSize)
        NextFrame(Stream);

    if (Stream->PsiSent < 3)
        PutTable(Stream, Stream->PsiSent++, Packet);
    else if (Stream->EsSent < Stream->EsSize)
        PutVideo(Stream, Packet);
    else
    {
        PutHeader(Packet, PID_NULL, false, 1, 0);
        memset(Packet + 4, 0xFF, TS_SIZE - 4);
    }
    Stream->Packet++;
}

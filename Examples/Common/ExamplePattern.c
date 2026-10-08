// #*#*#*#*#*#*#*#*#*#*#*#*#* ExamplePattern.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Examples: the test pattern and test tone that the sending examples send
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// Example includes
#include "ExamplePattern.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The 10-bit luma of black and of white, and the colour difference of no colour.
#define BLACK_Y 64
#define WHITE_Y 940
#define NO_COLOUR 512

// A code block reads as light from this luma up, and as dark up to this luma. Between the
// two the line holds no code.
#define LIGHT_MIN 600
#define DARK_MAX 300

// The blocks of the code: two marks, the 32 bits, and two marks.
#define CODE_BLOCKS 36

// The digits of the frame number in the box.
#define NUM_DIGITS 8

// One period of a 1 kHz sine at 48 kHz, at -20 dBFS, as 24-bit values.
static const int32_t g_Tone[48] = {
    0,       109493,  217113,  321018,  419430,  510666,  593164,  665513,
    726475,  775007,  810278,  831684,  838861,  831684,  810278,  775007,
    726475,  665513,  593164,  510666,  419430,  321018,  217113,  109493,
    0,       -109493, -217113, -321018, -419430, -510666, -593164, -665513,
    -726475, -775007, -810278, -831684, -838861, -831684, -810278, -775007,
    -726475, -665513, -593164, -510666, -419430, -321018, -217113, -109493};

// The digits 0 to 9 in a font of 5 by 7 pixels: seven rows from the top, the leftmost
// pixel in bit 4.
static const uint8_t g_Digits[10][7] = {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
                                        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
                                        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
                                        {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
                                        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
                                        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
                                        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
                                        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
                                        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
                                        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

// The colours of the 75% bars of SMPTE RP 219, from left to right, as red, green and
// blue from 0 to 1.
static const double g_Bars[7][3] = {{0.75, 0.75, 0.75}, {0.75, 0.75, 0}, {0, 0.75, 0.75},
                                    {0, 0.75, 0},       {0.75, 0, 0.75}, {0.75, 0, 0},
                                    {0, 0, 0.75}};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Where everything lies follows from the image size alone, so that a receiver finds the
// code from the size of the image it receives: the code's columns from the width alone,
// and its lines from the height alone. Positions and sizes that the colour difference
// samples depend on are even.
//

// Where the parts of the pattern lie in an image of one size.
typedef struct Layout
{
    int GreyLines;  // Lines of the grey scale, from the top; the colour bars follow
    int BarWidth;   // Pixels of the moving bar
    int Block;      // Pixels of one code block
    int Scale;      // Pixels of one font pixel, across and down
    int BoxX, BoxY; // The box's top left corner
    int BoxWidth;   // The box's width in pixels
    int BoxLines;   // The box's height in lines
    int CodeX;      // The left edge of the first code block
    int CodeY;      // The first line of the code
    int CodeLines;  // Lines of the code
    int DigitsY;    // The first line of the digits
} Layout;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LayoutOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Works out the layout of an image of Width by Height pixels. The box holds, from the
// top, a margin, the code, a gap, the eight digits, and a margin.
//
static Layout LayoutOf(int Width, int Height)
{
    Layout L;
    L.GreyLines = Height * 2 / 3 / 2 * 2;
    L.BarWidth = Width / 16;
    L.Block = Width / 96 / 2 * 2 > 4 ? Width / 96 / 2 * 2 : 4;
    L.Scale = Height / 120 > 1 ? Height / 120 : 1;
    L.BoxX = Width / 32 / 2 * 2;
    L.BoxY = Height / 16 / 2 * 2;
    const int DigitsWidth = NUM_DIGITS * 6 * L.Scale - L.Scale;
    const int CodeWidth = CODE_BLOCKS * L.Block;
    L.BoxWidth = 2 * L.Block + (CodeWidth > DigitsWidth ? CodeWidth : DigitsWidth);
    L.BoxWidth = (L.BoxWidth + 1) / 2 * 2;
    L.CodeX = L.BoxX + L.Block;
    L.CodeY = L.BoxY + 2 * L.Scale;
    L.CodeLines = Height / 36 / 2 * 2 > 4 ? Height / 36 / 2 * 2 : 4;
    L.DigitsY = L.CodeY + L.CodeLines + 2 * L.Scale;
    L.BoxLines = L.DigitsY + 7 * L.Scale + 2 * L.Scale - L.BoxY;
    return L;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Drawing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ToYCbCr -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Converts a colour from red, green and blue, each from 0 to 1, to 10-bit luma and
// colour difference samples. Kr and Kb are the weights of red and blue in the luma:
// 0.2126 and 0.0722 in BT.709, 0.299 and 0.114 in BT.601.
//
static void ToYCbCr(const double Rgb[3], double Kr, double Kb, uint16_t Out[3])
{
    const double Y = Kr * Rgb[0] + (1 - Kr - Kb) * Rgb[1] + Kb * Rgb[2];
    const double Cb = (Rgb[2] - Y) / (2 * (1 - Kb));
    const double Cr = (Rgb[0] - Y) / (2 * (1 - Kr));
    Out[0] = (uint16_t)(64 + 876 * Y + 0.5);
    Out[1] = (uint16_t)(NO_COLOUR + 896 * Cb + 0.5);
    Out[2] = (uint16_t)(NO_COLOUR + 896 * Cr + 0.5);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FillLuma -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sets the luma of a rectangle of Width by Lines pixels, at X and Y, to Luma. The colour
// difference samples stay as they are.
//
static void FillLuma(ExamplePattern* Pattern, int X, int Y, int Width, int Lines,
                     uint16_t Luma)
{
    for (int y = Y; y < Y + Lines; y++)
    {
        uint16_t* Line = Pattern->Y + (size_t)y * (size_t)Pattern->Width;
        for (int x = X; x < X + Width; x++)
            Line[x] = Luma;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DrawBox -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Draws the box with frame number Number: the code of its lower 32 bits, and below it
// the number's last eight digits. The box lies in the grey scale, where the colour
// difference is that of no colour everywhere, so only the luma is drawn.
//
static void DrawBox(ExamplePattern* Pattern, const Layout* L, uint64_t Number)
{
    FillLuma(Pattern, L->BoxX, L->BoxY, L->BoxWidth, L->BoxLines, BLACK_Y);

    // The code: the marks 1 0, the bits from the most significant, the marks 0 1.
    const uint32_t Code = (uint32_t)Number;
    for (int k = 0; k < CODE_BLOCKS; k++)
    {
        bool Light;
        if (k < 2)
            Light = k == 0;
        else if (k >= CODE_BLOCKS - 2)
            Light = k == CODE_BLOCKS - 1;
        else
            Light = (Code >> (CODE_BLOCKS - 3 - k) & 1) != 0;
        if (Light)
            FillLuma(Pattern, L->CodeX + k * L->Block, L->CodeY, L->Block, L->CodeLines,
                     WHITE_Y);
    }

    // The digits, from the most significant, each six font pixels apart.
    uint64_t Rest = Number;
    for (int d = NUM_DIGITS - 1; d >= 0; d--, Rest /= 10)
    {
        const uint8_t* Glyph = g_Digits[Rest % 10];
        const int X = L->CodeX + d * 6 * L->Scale;
        for (int Row = 0; Row < 7; Row++)
            for (int Col = 0; Col < 5; Col++)
                if ((Glyph[Row] >> (4 - Col) & 1) != 0)
                    FillLuma(Pattern, X + Col * L->Scale, L->DigitsY + Row * L->Scale,
                             L->Scale, L->Scale, WHITE_Y);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pattern +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExamplePattern_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The grey scale has seven bars from black to white, the colour bars seven bars of the
// colours of SMPTE RP 219. Both have their colour difference samples set once here; the
// frames only change luma.
//
bool ExamplePattern_Init(ExamplePattern* Pattern, int Width, int Height)
{
    memset(Pattern, 0, sizeof(*Pattern));
    if (Width < 320 || Height < 240 || Width % 2 != 0)
        return false;
    const Layout L = LayoutOf(Width, Height);
    if (L.BoxX + L.BoxWidth > Width || L.BoxY + L.BoxLines > L.GreyLines)
        return false;

    const size_t Pixels = (size_t)Width * (size_t)Height;
    Pattern->Y = (uint16_t*)malloc(Pixels * sizeof(uint16_t));
    Pattern->Cb = (uint16_t*)malloc(Pixels / 2 * sizeof(uint16_t));
    Pattern->Cr = (uint16_t*)malloc(Pixels / 2 * sizeof(uint16_t));
    Pattern->GreyY = (uint16_t*)malloc((size_t)Width * sizeof(uint16_t));
    if (Pattern->Y == NULL || Pattern->Cb == NULL || Pattern->Cr == NULL ||
        Pattern->GreyY == NULL)
    {
        ExamplePattern_Free(Pattern);
        return false;
    }
    Pattern->Width = Width;
    Pattern->Height = Height;
    Pattern->BarX = -1;

    // One line of the grey scale and one of the colour bars, in the colours of BT.601 for
    // SD and of BT.709 above it.
    for (int x = 0; x < Width; x++)
        Pattern->GreyY[x] = (uint16_t)(BLACK_Y + (x * 7 / Width) * 146);
    const bool Sd = Height <= 576;
    uint16_t Bars[7][3];
    for (int b = 0; b < 7; b++)
        ToYCbCr(g_Bars[b], Sd ? 0.299 : 0.2126, Sd ? 0.114 : 0.0722, Bars[b]);

    for (int y = 0; y < Height; y++)
    {
        uint16_t* Y = Pattern->Y + (size_t)y * (size_t)Width;
        uint16_t* Cb = Pattern->Cb + (size_t)y * (size_t)(Width / 2);
        uint16_t* Cr = Pattern->Cr + (size_t)y * (size_t)(Width / 2);
        if (y < L.GreyLines)
        {
            memcpy(Y, Pattern->GreyY, (size_t)Width * sizeof(uint16_t));
            for (int x = 0; x < Width / 2; x++)
                Cb[x] = Cr[x] = NO_COLOUR;
            continue;
        }
        for (int x = 0; x < Width; x++)
            Y[x] = Bars[x * 7 / Width][0];
        for (int x = 0; x < Width / 2; x++)
        {
            Cb[x] = Bars[2 * x * 7 / Width][1];
            Cr[x] = Bars[2 * x * 7 / Width][2];
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExamplePattern_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void ExamplePattern_Free(ExamplePattern* Pattern)
{
    free(Pattern->Y);
    free(Pattern->Cb);
    free(Pattern->Cr);
    free(Pattern->GreyY);
    memset(Pattern, 0, sizeof(*Pattern));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExamplePattern_Draw -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The bar runs through the whole grey scale. Where it was, the grey scale comes back;
// then it is drawn at its new place, and the box over both.
//
void ExamplePattern_Draw(ExamplePattern* Pattern, int64_t Number)
{
    const Layout L = LayoutOf(Pattern->Width, Pattern->Height);
    const int Width = Pattern->Width;
    const int OldX = Pattern->BarX;
    const int NewX = (int)((uint64_t)Number * 16 % (uint64_t)Width);
    const int OldEnd =
        OldX < 0 ? 0 : (OldX + L.BarWidth < Width ? OldX + L.BarWidth : Width);
    const int NewEnd = NewX + L.BarWidth < Width ? NewX + L.BarWidth : Width;

    for (int y = 0; y < L.GreyLines; y++)
    {
        uint16_t* Y = Pattern->Y + (size_t)y * (size_t)Width;
        if (OldX >= 0)
            memcpy(Y + OldX, Pattern->GreyY + OldX,
                   (size_t)(OldEnd - OldX) * sizeof(uint16_t));
        for (int x = NewX; x < NewEnd; x++)
            Y[x] = WHITE_Y;
    }
    Pattern->BarX = NewX;
    DrawBox(Pattern, &L, (uint64_t)Number);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExamplePattern_Changes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int ExamplePattern_Changes(const ExamplePattern* Pattern, ExamplePatternArea Areas[2])
{
    if (Pattern->BarX < 0)
        return 0;
    const Layout L = LayoutOf(Pattern->Width, Pattern->Height);
    const int BarEnd = Pattern->BarX + L.BarWidth < Pattern->Width
                           ? Pattern->BarX + L.BarWidth
                           : Pattern->Width;
    Areas[0].X = Pattern->BarX;
    Areas[0].Y = 0;
    Areas[0].Width = BarEnd - Pattern->BarX;
    Areas[0].Lines = L.GreyLines;
    Areas[1].X = L.BoxX;
    Areas[1].Y = L.BoxY;
    Areas[1].Width = L.BoxWidth;
    Areas[1].Lines = L.BoxLines;
    return 2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExamplePattern_CodeLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int ExamplePattern_CodeLine(int Height)
{
    const Layout L = LayoutOf(320, Height);
    return L.CodeY + L.CodeLines / 2;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExamplePattern_ReadNumber -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads each block in its middle, where a scaler leaves the value most nearly as it was.
//
bool ExamplePattern_ReadNumber(const uint16_t* Luma, int Width, uint32_t* Number)
{
    *Number = 0;
    if (Width < 320)
        return false;
    const Layout L = LayoutOf(Width, 240);
    if (L.CodeX + CODE_BLOCKS * L.Block > Width)
        return false;

    uint32_t Code = 0;
    for (int k = 0; k < CODE_BLOCKS; k++)
    {
        const uint16_t Value = Luma[L.CodeX + k * L.Block + L.Block / 2];
        if (Value > DARK_MAX && Value < LIGHT_MIN)
            return false;
        const bool Light = Value >= LIGHT_MIN;
        if (k < 2 || k >= CODE_BLOCKS - 2)
        {
            const bool Mark = k == 0 || k == CODE_BLOCKS - 1;
            if (Light != Mark)
                return false;
        }
        else
            Code = Code << 1 | (Light ? 1u : 0u);
    }
    *Number = Code;
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ExampleTone_Next -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void ExampleTone_Next(ExampleTone* Tone, int32_t* Samples, int Count)
{
    for (int s = 0; s < Count; s++)
        Samples[s] = (int32_t)((uint32_t)g_Tone[(Tone->Sample + s) % 48] << 8);
    Tone->Sample += Count;
}

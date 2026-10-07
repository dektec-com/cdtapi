// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiImage.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The images of SDI frames: their sizes and their pixel formats
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Every conversion starts from a line's symbols, one value a 16-bit word, in the order
// SDI carries them: Cb, Y, Cr, Y. This is the portable version, the reference that the
// vector versions of plan 0032's step F must equal.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtSdiImage.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// v210 holds six pixels in 16 bytes, and pads each line to a multiple of 128 bytes.
#define DT_V210_PIXELS_PER_BLOCK 6
#define DT_V210_BYTES_PER_BLOCK 16
#define DT_V210_LINE_ALIGNMENT 128

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NumPlanes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the planes of Format: 3 for the planar formats, 1 for the others, 0 for one
// that is not a format.
//
static int NumPlanes(DtSdiPixelFormat Format)
{
    switch (Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
    case DT_SDI_PIXFMT_UYVY_8B:
    case DT_SDI_PIXFMT_V210:
    case DT_SDI_PIXFMT_Y210:
        return 1;
    case DT_SDI_PIXFMT_YUV422P_10B:
    case DT_SDI_PIXFMT_YUV422P_8B:
        return 3;
    default:
        return 0;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LeastStrides -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills Strides with the least stride of each plane of an image Width pixels wide in
// Format, 0 for a plane it does not have.
//
static void LeastStrides(DtSdiPixelFormat Format, int Width, int Strides[3])
{
    Strides[0] = Strides[1] = Strides[2] = 0;
    switch (Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        Strides[0] = Width * 2 * 10 / 8;
        break;
    case DT_SDI_PIXFMT_UYVY_8B:
        Strides[0] = Width * 2;
        break;
    case DT_SDI_PIXFMT_V210:
    {
        const int Blocks =
            (Width + DT_V210_PIXELS_PER_BLOCK - 1) / DT_V210_PIXELS_PER_BLOCK;
        const int Bytes = Blocks * DT_V210_BYTES_PER_BLOCK;
        Strides[0] = (Bytes + DT_V210_LINE_ALIGNMENT - 1) / DT_V210_LINE_ALIGNMENT *
                     DT_V210_LINE_ALIGNMENT;
        break;
    }
    case DT_SDI_PIXFMT_Y210:
        Strides[0] = Width * 4;
        break;
    case DT_SDI_PIXFMT_YUV422P_10B:
        Strides[0] = Width * 2;
        Strides[1] = Strides[2] = Width;
        break;
    case DT_SDI_PIXFMT_YUV422P_8B:
        Strides[0] = Width;
        Strides[1] = Strides[2] = Width / 2;
        break;
    default:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutLe16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PutLe16(uint8_t* Bytes, uint16_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutLe32 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PutLe32(uint8_t* Bytes, uint32_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
    Bytes[2] = (uint8_t)(Value >> 16);
    Bytes[3] = (uint8_t)(Value >> 24);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutUyvy10 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the symbols ten bits each, least significant bit first: four symbols in five
// bytes. A line always has a multiple of four symbols.
//
static void PutUyvy10(uint8_t* Out, const uint16_t* Symbols, int NumSymbols)
{
    for (int i = 0; i < NumSymbols; i += 4)
    {
        const uint64_t Bits = (uint64_t)Symbols[i] | (uint64_t)Symbols[i + 1] << 10 |
                              (uint64_t)Symbols[i + 2] << 20 |
                              (uint64_t)Symbols[i + 3] << 30;
        for (int b = 0; b < 5; b++)
            *Out++ = (uint8_t)(Bits >> (8 * b));
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutV210 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the symbols three in a 32-bit little-endian word, in the order they come, and
// fills the last block of six pixels and the rest of the line's stride with zeros.
//
static void PutV210(uint8_t* Out, const uint16_t* Symbols, int NumSymbols, int Stride)
{
    int Written = 0;
    for (int i = 0; i < NumSymbols; i += 3)
    {
        uint32_t Word = 0;
        for (int k = 0; k < 3 && i + k < NumSymbols; k++)
            Word |= (uint32_t)Symbols[i + k] << (10 * k);
        PutLe32(Out + Written, Word);
        Written += 4;
    }
    memset(Out + Written, 0, (size_t)(Stride - Written));
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Images +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_GetSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiImage_GetSize(int VidStd, DtSdiPixelFormat Format, int* Width,
                               int* Height, int MinStrides[3])
{
    if (Width != NULL)
        *Width = 0;
    if (Height != NULL)
        *Height = 0;
    if (MinStrides != NULL)
        MinStrides[0] = MinStrides[1] = MinStrides[2] = 0;

    DtSdiGeometry Geo;
    DtapiResult Result = DtSdiGeometry_Init(&Geo, VidStd);
    if (Result != DTAPI_OK)
        return Result;
    if (NumPlanes(Format) == 0)
        return DTAPI_E_INVALID_FORMAT;

    if (Width != NULL)
        *Width = Geo.Width;
    if (Height != NULL)
        *Height = Geo.Height;
    if (MinStrides != NULL)
        LeastStrides(Format, Geo.Width, MinStrides);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_Check -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiImage_Check(const DtSdiImage* Image, const DtSdiGeometry* Geo)
{
    const int Planes = NumPlanes(Image->Format);
    if (Planes == 0)
        return DTAPI_E_INVALID_FORMAT;
    if (Image->Fields != DT_SDI_FIELDS_WOVEN && Image->Fields != DT_SDI_FIELDS_SEPARATE)
        return DTAPI_E_INVALID_FORMAT;
    if (Image->Fields == DT_SDI_FIELDS_SEPARATE)
        return DTAPI_E_NOT_SUPPORTED;

    int Strides[3];
    LeastStrides(Image->Format, Geo->Width, Strides);
    for (int p = 0; p < Planes; p++)
    {
        if (Image->Planes[p] == NULL || Image->Strides[p] < Strides[p])
            return DTAPI_E_INVALID_ARG;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetLe16 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint16_t GetLe16(const uint8_t* Bytes)
{
    return (uint16_t)(Bytes[0] | Bytes[1] << 8);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Legal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Limits a sample to 4..1019: SDI keeps 0 to 3 and 1020 to 1023 for timing references.
//
static uint16_t Legal(unsigned Sample)
{
    return (uint16_t)(Sample < 4 ? 4 : Sample > 1019 ? 1019 : Sample);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_GetLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiImage_GetLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        uint16_t* Symbols)
{
    const int Width = Geo->Width;
    const int NumSymbols = 2 * Width;
    const uint8_t* In[3];
    for (int p = 0; p < 3; p++)
        In[p] = Image->Planes[p] == NULL
                    ? NULL
                    : Image->Planes[p] + (size_t)Line * (size_t)Image->Strides[p];

    switch (Image->Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        for (int i = 0; i < NumSymbols; i += 4)
        {
            const uint8_t* B = In[0] + 5 * (i / 4);
            const uint64_t Bits = (uint64_t)B[0] | (uint64_t)B[1] << 8 |
                                  (uint64_t)B[2] << 16 | (uint64_t)B[3] << 24 |
                                  (uint64_t)B[4] << 32;
            for (int k = 0; k < 4; k++)
                Symbols[i + k] = Legal((unsigned)(Bits >> (10 * k)) & 0x3FF);
        }
        break;

    case DT_SDI_PIXFMT_UYVY_8B:
        for (int i = 0; i < NumSymbols; i++)
            Symbols[i] = Legal((unsigned)In[0][i] << 2);
        break;

    case DT_SDI_PIXFMT_V210:
        for (int i = 0; i < NumSymbols; i++)
        {
            const uint8_t* W = In[0] + 4 * (i / 3);
            const uint32_t Word = (uint32_t)W[0] | (uint32_t)W[1] << 8 |
                                  (uint32_t)W[2] << 16 | (uint32_t)W[3] << 24;
            Symbols[i] = Legal((Word >> (10 * (i % 3))) & 0x3FF);
        }
        break;

    case DT_SDI_PIXFMT_Y210:
        // Each pair of pixels: Y0, Cb, Y1, Cr, the 10 bits at the top of each word.
        for (int i = 0; i < NumSymbols; i += 4)
        {
            const uint8_t* Pair = In[0] + 2 * i;
            Symbols[i + 0] = Legal(GetLe16(Pair + 2) >> 6);
            Symbols[i + 1] = Legal(GetLe16(Pair + 0) >> 6);
            Symbols[i + 2] = Legal(GetLe16(Pair + 6) >> 6);
            Symbols[i + 3] = Legal(GetLe16(Pair + 4) >> 6);
        }
        break;

    case DT_SDI_PIXFMT_YUV422P_10B:
        for (int x = 0; x < Width; x++)
            Symbols[2 * x + 1] = Legal(GetLe16(In[0] + 2 * x) & 0x3FF);
        for (int c = 0; c < Width / 2; c++)
        {
            Symbols[4 * c] = Legal(GetLe16(In[1] + 2 * c) & 0x3FF);
            Symbols[4 * c + 2] = Legal(GetLe16(In[2] + 2 * c) & 0x3FF);
        }
        break;

    case DT_SDI_PIXFMT_YUV422P_8B:
        for (int x = 0; x < Width; x++)
            Symbols[2 * x + 1] = Legal((unsigned)In[0][x] << 2);
        for (int c = 0; c < Width / 2; c++)
        {
            Symbols[4 * c] = Legal((unsigned)In[1][c] << 2);
            Symbols[4 * c + 2] = Legal((unsigned)In[2][c] << 2);
        }
        break;

    default:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_PutLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiImage_PutLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        const uint16_t* Symbols)
{
    const int Width = Geo->Width;
    const int NumSymbols = 2 * Width;
    uint8_t* Out[3];
    for (int p = 0; p < 3; p++)
        Out[p] = Image->Planes[p] == NULL
                     ? NULL
                     : Image->Planes[p] + (size_t)Line * (size_t)Image->Strides[p];

    switch (Image->Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        PutUyvy10(Out[0], Symbols, NumSymbols);
        break;

    case DT_SDI_PIXFMT_UYVY_8B:
        for (int i = 0; i < NumSymbols; i++)
            Out[0][i] = (uint8_t)(Symbols[i] >> 2);
        break;

    case DT_SDI_PIXFMT_V210:
    {
        int Strides[3];
        LeastStrides(DT_SDI_PIXFMT_V210, Width, Strides);
        PutV210(Out[0], Symbols, NumSymbols, Strides[0]);
        break;
    }

    case DT_SDI_PIXFMT_Y210:
        // Each pair of pixels: Y0, Cb, Y1, Cr, the 10 bits at the top of each word.
        for (int i = 0; i < NumSymbols; i += 4)
        {
            uint8_t* Pair = Out[0] + 2 * i;
            PutLe16(Pair + 0, (uint16_t)(Symbols[i + 1] << 6));
            PutLe16(Pair + 2, (uint16_t)(Symbols[i + 0] << 6));
            PutLe16(Pair + 4, (uint16_t)(Symbols[i + 3] << 6));
            PutLe16(Pair + 6, (uint16_t)(Symbols[i + 2] << 6));
        }
        break;

    case DT_SDI_PIXFMT_YUV422P_10B:
        for (int x = 0; x < Width; x++)
            PutLe16(Out[0] + 2 * x, Symbols[2 * x + 1]);
        for (int c = 0; c < Width / 2; c++)
        {
            PutLe16(Out[1] + 2 * c, Symbols[4 * c]);
            PutLe16(Out[2] + 2 * c, Symbols[4 * c + 2]);
        }
        break;

    case DT_SDI_PIXFMT_YUV422P_8B:
        for (int x = 0; x < Width; x++)
            Out[0][x] = (uint8_t)(Symbols[2 * x + 1] >> 2);
        for (int c = 0; c < Width / 2; c++)
        {
            Out[1][c] = (uint8_t)(Symbols[4 * c] >> 2);
            Out[2][c] = (uint8_t)(Symbols[4 * c + 2] >> 2);
        }
        break;

    default:
        break;
    }
}

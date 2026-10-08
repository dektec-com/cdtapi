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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_GetLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiImage_GetLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        uint16_t* Symbols, const DtSdiConv* Conv)
{
    const size_t Count = 2 * (size_t)Geo->Width;
    const uint8_t* In[3];
    for (int p = 0; p < 3; p++)
        In[p] = Image->Planes[p] == NULL
                    ? NULL
                    : Image->Planes[p] + (size_t)Line * (size_t)Image->Strides[p];

    switch (Image->Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        Conv->Unpack10(In[0], Count, Symbols);
        Conv->Limit(Symbols, Count);
        break;
    case DT_SDI_PIXFMT_UYVY_8B:
        Conv->FromUyvy8(In[0], Count, Symbols);
        break;
    case DT_SDI_PIXFMT_V210:
        Conv->FromV210(In[0], Count, Symbols);
        break;
    case DT_SDI_PIXFMT_Y210:
        Conv->FromY210(In[0], Count, Symbols);
        break;
    case DT_SDI_PIXFMT_YUV422P_10B:
        Conv->FromPlanar10(In[0], In[1], In[2], Count, Symbols);
        break;
    case DT_SDI_PIXFMT_YUV422P_8B:
        Conv->FromPlanar8(In[0], In[1], In[2], Count, Symbols);
        break;
    default:
        break;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiImage_PutLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiImage_PutLine(const DtSdiImage* Image, const DtSdiGeometry* Geo, int Line,
                        const uint16_t* Symbols, const DtSdiConv* Conv)
{
    const size_t Count = 2 * (size_t)Geo->Width;
    uint8_t* Out[3];
    for (int p = 0; p < 3; p++)
        Out[p] = Image->Planes[p] == NULL
                     ? NULL
                     : Image->Planes[p] + (size_t)Line * (size_t)Image->Strides[p];

    switch (Image->Format)
    {
    case DT_SDI_PIXFMT_UYVY_10B:
        Conv->Pack10(Symbols, Count, Out[0]);
        break;
    case DT_SDI_PIXFMT_UYVY_8B:
        Conv->ToUyvy8(Symbols, Count, Out[0]);
        break;
    case DT_SDI_PIXFMT_V210:
    {
        // The words of the line, then zeros up to the least stride: a line is padded
        // to 128 bytes.
        int Strides[3];
        LeastStrides(DT_SDI_PIXFMT_V210, Geo->Width, Strides);
        const size_t Written = (Count + 2) / 3 * 4;
        Conv->ToV210(Symbols, Count, Out[0]);
        memset(Out[0] + Written, 0, (size_t)Strides[0] - Written);
        break;
    }
    case DT_SDI_PIXFMT_Y210:
        Conv->ToY210(Symbols, Count, Out[0]);
        break;
    case DT_SDI_PIXFMT_YUV422P_10B:
        Conv->ToPlanar10(Symbols, Count, Out[0], Out[1], Out[2]);
        break;
    case DT_SDI_PIXFMT_YUV422P_8B:
        Conv->ToPlanar8(Symbols, Count, Out[0], Out[1], Out[2]);
        break;
    default:
        break;
    }
}

// #*#*#*#*#*#*#*#*#*#*#*#*#* DtAvFrameProps.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The video format of a received frame
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "cdtapi_avfifo.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Formats +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// The table: 4:2:2 in 8, 10, 12 and 16 bits for each size. An interlaced frame is a
// field of half the lines.
//

typedef struct Format
{
    int Width;
    int Height;
    int IsInterlaced;
} Format;

static const Format Formats[] = {
    {720, 480, 1},   {720, 576, 1},   {1920, 1080, 1}, {1280, 720, 0},
    {1920, 1080, 0}, {2048, 1080, 0}, {3840, 2160, 0},
};

static const int BitDepths[] = {8, 10, 12, 16};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeProperties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A 4:2:2 pixel takes twice its bit depth.
//
static FrameProperties MakeProperties(const Format* Fmt, int BitDepth)
{
    FrameProperties Props;

    Props.Is420 = 0;
    Props.NLines = Fmt->IsInterlaced ? Fmt->Height / 2 : Fmt->Height;
    Props.BytesPerLine = Fmt->Width * 2 * BitDepth / 8;
    Props.BytesPerFrame = Props.BytesPerLine * Props.NLines;
    Props.Width = Fmt->Width;
    Props.Height = Fmt->Height;
    Props.IsInterlaced = Fmt->IsInterlaced;
    Props.Subsampling = ChromaSubsampling_422;
    Props.BitDepth = BitDepth;
    return Props;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetFrameProperties -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The first format, in the table's order, whose 4:2:0 flag, valid bytes and rows the
// frame has.
//
DtapiResult GetFrameProperties(const AvFifo_Frame* Frame, FrameProperties* Properties)
{
    if (Frame == NULL || Properties == NULL)
        return DTAPI_E_INVALID_ARG;
    for (size_t f = 0; f < sizeof(Formats) / sizeof(Formats[0]); f++)
    {
        for (size_t b = 0; b < sizeof(BitDepths) / sizeof(BitDepths[0]); b++)
        {
            FrameProperties Props = MakeProperties(&Formats[f], BitDepths[b]);
            if ((Frame->Is420 != 0) == (Props.Is420 != 0) &&
                Frame->NumValidBytes == Props.BytesPerFrame &&
                Frame->NumRows == Props.NLines)
            {
                *Properties = Props;
                return DTAPI_OK;
            }
        }
    }
    return DTAPI_E_UNSUP_FORMAT;
}

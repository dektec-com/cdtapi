// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiView.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Views of SDI frames: where a frame's lines lie in memory
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A view of a raw frame is the frame's address and its geometry: where each line starts
// follows from the line's index, as every raw line has the same number of bits. The
// payload ID is read in plan 0032's step C, with the other ancillary data.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtSdiAnc.h"     // Finding the payload ID packet.
#include "DtSdiView.h"    // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The most words of horizontal blanking a stream of a line has between its timing
// references: 4125 - 1280 - 12 in 720p23.98 and 720p24.
#define DT_SDIVIEW_MAX_HANC_WORDS 4096

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_RawSymbols -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtSdiSymbolPtr DtSdiView_RawSymbols(const DtSdiView* View, int LineIndex, size_t Symbol)
{
    const size_t FirstBit =
        (size_t)LineIndex * View->LineNumBits + Symbol * (size_t)View->BitsPerSymbol;
    DtSdiSymbolPtr Ptr;
    Ptr.Byte = View->Frame + FirstBit / 8;
    Ptr.Bit = (int)(FirstBit % 8);
    Ptr.BitsPerSymbol = View->BitsPerSymbol;
    return Ptr;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= API +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtSdiView* DtSdiView_Alloc(void)
{
    DtSdiView* View = (DtSdiView*)DtAlloc_Malloc(sizeof(DtSdiView));
    if (View == NULL)
        return NULL;
    memset(View, 0, sizeof(*View));
    return View;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiView_Free(DtSdiView* View)
{
    DtAlloc_Free(View);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiView_Freep(DtSdiView** View)
{
    if (View == NULL)
        return;
    DtSdiView_Free(*View);
    *View = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_GetActiveLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiView_GetActiveLine(const DtSdiView* View, int Line,
                                    DtSdiSymbolPtr* Symbols)
{
    if (View == NULL || Symbols == NULL)
        return DTAPI_E_INVALID_ARG;
    memset(Symbols, 0, sizeof(*Symbols));
    if (!View->HasFrame)
        return DTAPI_E_STATE;
    if (Line < 0 || Line >= View->Geo.Height)
        return DTAPI_E_INVALID_LINE;
    if (View->Geo.Is4k)
        return DTAPI_E_NOT_SUPPORTED;

    *Symbols = DtSdiView_RawSymbols(View, DtSdiGeometry_RawLine(&View->Geo, Line),
                                    (size_t)View->Geo.Layout.LineNumSymsHanc);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_GetFormat -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiView_GetFormat(const DtSdiView* View, int* VidStd, int* BitsPerSymbol)
{
    if (View == NULL)
        return DTAPI_E_INVALID_ARG;
    if (!View->HasFrame)
        return DTAPI_E_STATE;
    if (VidStd != NULL)
        *VidStd = View->Geo.VidStd;
    if (BitsPerSymbol != NULL)
        *BitsPerSymbol = View->BitsPerSymbol;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindPayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Looks for the payload ID in the horizontal blanking of the Y stream of link 1 of line
// LineIndex, and sets *PayloadId to its four bytes when it is there.
//
static bool FindPayloadId(const DtSdiView* View, int LineIndex, uint32_t* PayloadId)
{
    const DtSdiGeometry* Geo = &View->Geo;
    const int Stream = Geo->NumStreams == 1 ? 0 : 1;
    const int Words = Geo->StreamHancWords - Geo->StreamEavWords - Geo->StreamSavWords;
    uint16_t Hanc[DT_SDIVIEW_MAX_HANC_WORDS];
    if (Words > DT_SDIVIEW_MAX_HANC_WORDS)
        return false;

    const DtSdiSymbolPtr Line = DtSdiView_RawSymbols(View, LineIndex, 0);
    for (int k = 0; k < Words; k++)
    {
        const size_t Symbol = (size_t)Geo->StreamFirst[Stream] +
                              (size_t)(Geo->StreamEavWords + k) * (size_t)Geo->NumStreams;
        Hanc[k] = DtSdiSymbolPtr_Get(&Line, Symbol);
    }

    int Pos = 0;
    DtSdiAncFound Found;
    while (DtSdiAnc_Find(Hanc, Words, &Pos, &Found))
    {
        if (Found.Did == DT_SDIANC_DID_PAYLOAD_ID &&
            Found.SdidOrDbn == DT_SDIANC_SDID_PAYLOAD_ID && Found.NumWords >= 4)
        {
            *PayloadId = (uint32_t)(Found.Words[0] & 0xFF) << 24 |
                         (uint32_t)(Found.Words[1] & 0xFF) << 16 |
                         (uint32_t)(Found.Words[2] & 0xFF) << 8 |
                         (uint32_t)(Found.Words[3] & 0xFF);
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_GetPayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// SMPTE ST 352 puts the payload ID three lines after field 1's switching line. A frame
// that has it elsewhere in field 1's vertical blanking is read too: FFmpeg's sdi muxer,
// for one, puts it three lines later in 525 lines.
//
DtapiResult DtSdiView_GetPayloadId(const DtSdiView* View, uint32_t* PayloadId)
{
    if (View == NULL || PayloadId == NULL)
        return DTAPI_E_INVALID_ARG;
    *PayloadId = 0;
    if (!View->HasFrame)
        return DTAPI_E_STATE;

    const DtSdiGeometry* Geo = &View->Geo;
    const int Standard = Geo->SwitchingIndex + 3;
    if (FindPayloadId(View, Standard, PayloadId))
        return DTAPI_OK;
    const int FirstActive = Geo->Is4k ? Geo->PictureFirstIndex : Geo->FieldFirstIndex[0];
    for (int Line = 0; Line < FirstActive; Line++)
    {
        if (Line != Standard && FindPayloadId(View, Line, PayloadId))
            return DTAPI_OK;
    }
    return DTAPI_E_NOT_FOUND;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_RawFrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiView_RawFrameSize(int VidStd, int BitsPerSymbol, size_t* Size)
{
    if (Size == NULL)
        return DTAPI_E_INVALID_ARG;
    *Size = 0;

    DtSdiGeometry Geo;
    DtapiResult Result = DtSdiGeometry_Init(&Geo, VidStd);
    if (Result != DTAPI_OK)
        return Result;
    if (BitsPerSymbol != 10 && BitsPerSymbol != 16)
        return DTAPI_E_INVALID_ARG;
    *Size = DtSdiFrame_RawSize(&Geo.Layout, BitsPerSymbol);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetRawFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiView_SetRawFrame(DtSdiView* View, void* Frame, size_t Size, int VidStd,
                                  int BitsPerSymbol)
{
    if (View == NULL)
        return DTAPI_E_INVALID_ARG;
    if (View->Holder != NULL)
        return DTAPI_E_IN_USE;
    View->HasFrame = false;

    DtSdiGeometry Geo;
    DtapiResult Result = DtSdiGeometry_Init(&Geo, VidStd);
    if (Result != DTAPI_OK)
        return Result;
    if (Frame == NULL || (BitsPerSymbol != 10 && BitsPerSymbol != 16))
        return DTAPI_E_INVALID_ARG;
    if (Size != DtSdiFrame_RawSize(&Geo.Layout, BitsPerSymbol))
        return DTAPI_E_INVALID_SIZE;

    View->Geo = Geo;
    View->BitsPerSymbol = BitsPerSymbol;
    View->Frame = (uint8_t*)Frame;
    View->FrameSize = Size;
    View->LineNumBits = DtSdiFrame_RawLineNumBits(&Geo.Layout, BitsPerSymbol);
    View->HasFrame = true;
    return DTAPI_OK;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiView.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Views of SDI frames: where a frame's lines lie in memory
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A view of a raw frame stores the frame's address and its geometry. Every raw line has
// the same number of bits, so a line's start follows from its index. A view of a frame
// in a ring finds a line's coded lines the same way: in the ring, or in the view's copy
// for the one line that wraps around the ring's end.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtSdiAnc.h"     // Finding the payload ID packet.
#include "DtSdiView.h"    // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Bounds the words of horizontal blanking that one stream of a line has between its
// timing references. The largest number is 4125 - 1280 - 12, in 720p23.98 and 720p24.
#define DT_SDIVIEW_MAX_HANC_WORDS 4096

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CodedLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the coded lines of raw line LineIndex of a frame in a ring, in one piece.
//
static const uint8_t* CodedLine(const DtSdiView* View, int LineIndex)
{
    if (LineIndex == View->WrapLineIndex)
        return View->WrapLine;
    const size_t Offset =
        (View->LinesStart + (size_t)LineIndex * View->CodedBytesPerLine) % View->RingSize;
    return View->RingBase + Offset;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLineScratch_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtSdiLineScratch_Alloc(DtSdiLineScratch* Scratch, const DtSdiView* View)
{
    memset(Scratch, 0, sizeof(*Scratch));
    if (View->RingBase == NULL || !View->Geo.Is4k)
        return true;
    const DtSdiFrameLayout* Layout = &View->Geo.Layout;
    Scratch->Raw =
        (uint8_t*)DtAlloc_Malloc(DtSdiFrame_RawLineNumBits(Layout, 10) / 8 + 16);
    Scratch->Symbols =
        (uint16_t*)DtAlloc_Malloc(DtSdiFrame_NumBandSymbols(Layout) * sizeof(uint16_t));
    if (Scratch->Raw != NULL && Scratch->Symbols != NULL)
        return true;
    DtSdiLineScratch_Free(Scratch);
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLineScratch_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiLineScratch_Free(DtSdiLineScratch* Scratch)
{
    DtAlloc_Free(Scratch->Raw);
    DtAlloc_Free(Scratch->Symbols);
    memset(Scratch, 0, sizeof(*Scratch));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_Forget -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiView_Forget(DtSdiView* View)
{
    View->HasFrame = false;
    View->LevelBField = 0;
    View->Holder = NULL;
    View->RingBase = NULL;
    View->WrapLineIndex = -1;
    View->IsTx = false;
    View->IsBuilt = false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_LinkHanc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The first coded line of a 2160p line holds the horizontal blanking of links 1 and 2,
// the second that of links 3 and 4. Each section is padded to the card's alignment.
//
DtSdiSymbolPtr DtSdiView_LinkHanc(const DtSdiView* View, int LineIndex, int Link)
{
    const DtSdiFrameLayout* Layout = &View->Geo.Layout;
    const uint8_t* Coded = CodedLine(View, LineIndex);
    if (Link > 2)
        Coded += Layout->RxStride;
    DtSdiSymbolPtr Ptr;
    Ptr.Byte = Coded + (size_t)((Link - 1) & 1) * (size_t)Layout->SectionBytesHanc;
    Ptr.Bit = 0;
    Ptr.BitsPerSymbol = 10;
    return Ptr;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_LineSymbols -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Up to 3G, a coded line holds the horizontal blanking and then the active part. Each is
// a section that starts on a byte and stores the symbols as a raw line does.
//
DtSdiSymbolPtr DtSdiView_LineSymbols(const DtSdiView* View, int LineIndex, size_t Symbol,
                                     DtSdiLineScratch* Scratch)
{
    if (View->RingBase == NULL)
        return DtSdiView_RawSymbols(View, LineIndex, Symbol);

    const DtSdiFrameLayout* Layout = &View->Geo.Layout;
    const uint8_t* Coded = CodedLine(View, LineIndex);
    const uint8_t* Section = Coded;
    size_t Within = Symbol;
    if (Layout->Is4k)
    {
        DtSdiFrame_DecodeLine4k(Layout, 10, Coded, Coded + Layout->RxStride, LineIndex,
                                Scratch->Raw, Scratch->Symbols);
        Section = Scratch->Raw;
    }
    else if (Symbol >= (size_t)Layout->LineNumSymsHanc)
    {
        Section = Coded + Layout->SectionBytesHanc;
        Within = Symbol - (size_t)Layout->LineNumSymsHanc;
    }
    DtSdiSymbolPtr Ptr;
    Ptr.Byte = Section + Within * 10 / 8;
    Ptr.Bit = (int)(Within * 10 % 8);
    Ptr.BitsPerSymbol = 10;
    return Ptr;
}

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
    View->WrapLineIndex = -1;
    return View;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiView_Free(DtSdiView* View)
{
    if (View == NULL)
        return;
    DtAlloc_Free(View->WrapLine);
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
    if (!View->HasFrame || View->IsTx)
        return DTAPI_E_STATE;
    if (Line < 0 || Line >= View->Geo.Height)
        return DTAPI_E_INVALID_LINE;
    if (View->Geo.Is4k)
        return DTAPI_E_NOT_SUPPORTED;

    *Symbols = DtSdiView_LineSymbols(View, DtSdiGeometry_RawLine(&View->Geo, Line),
                                     (size_t)View->Geo.Layout.LineNumSymsHanc, NULL);
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
// Looks for the payload ID in raw line LineIndex and, when it is there, sets *PayloadId
// to its four bytes. It searches the horizontal blanking in link 1's Y stream.
//
static bool FindPayloadId(const DtSdiView* View, int LineIndex, DtSdiLineScratch* Scratch,
                          uint32_t* PayloadId)
{
    const DtSdiGeometry* Geo = &View->Geo;
    const int Stream = Geo->NumStreams == 1 ? 0 : 1;
    const int Words = Geo->StreamHancWords - Geo->StreamEavWords - Geo->StreamSavWords;
    uint16_t Hanc[DT_SDIVIEW_MAX_HANC_WORDS];
    if (Words > DT_SDIVIEW_MAX_HANC_WORDS)
        return false;

    const DtSdiSymbolPtr Line = DtSdiView_LineSymbols(View, LineIndex, 0, Scratch);
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
// Looks first in the line where SMPTE ST 352 puts the payload ID, three lines after the
// switching line of field 1. If it is not there, looks in the other lines of field 1's
// vertical blanking. Some sources put it elsewhere. FFmpeg's sdi muxer, for one, puts it
// three lines later in 525-line video.
//
DtapiResult DtSdiView_GetPayloadId(const DtSdiView* View, uint32_t* PayloadId)
{
    if (View == NULL || PayloadId == NULL)
        return DTAPI_E_INVALID_ARG;
    *PayloadId = 0;
    if (!View->HasFrame || View->IsTx)
        return DTAPI_E_STATE;

    DtSdiLineScratch Scratch;
    if (!DtSdiLineScratch_Alloc(&Scratch, View))
        return DTAPI_E_OUT_OF_MEM;
    const DtSdiGeometry* Geo = &View->Geo;
    const int Standard = Geo->SwitchingIndex + 3;
    bool Found = FindPayloadId(View, Standard, &Scratch, PayloadId);
    const int FirstActive = Geo->Is4k ? Geo->PictureFirstIndex : Geo->FieldFirstIndex[0];
    for (int Line = 0; Line < FirstActive && !Found; Line++)
        Found = Line != Standard && FindPayloadId(View, Line, &Scratch, PayloadId);
    DtSdiLineScratch_Free(&Scratch);
    return Found ? DTAPI_OK : DTAPI_E_NOT_FOUND;
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
    if (Geo.IsLevelB)
        return DTAPI_E_INVALID_VIDSTD;
    if (BitsPerSymbol != 10 && BitsPerSymbol != 16)
        return DTAPI_E_INVALID_ARG;
    *Size = DtSdiFrame_RawSize(&Geo.Layout, BitsPerSymbol);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetLevelBField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtSdiView_SetLevelBField(DtSdiView* View, int Field)
{
    View->LevelBField = View->Geo.IsLevelB ? Field : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetRaw -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Points View at a raw frame of the layout of level A in the program's memory, as
// DtSdiView_SetRawFrame() describes. LevelB says whether a 3G level-B standard is taken;
// the picture's field is then Field.
//
static DtapiResult SetRaw(DtSdiView* View, void* Frame, size_t Size, int VidStd,
                          int BitsPerSymbol, bool LevelB, int Field)
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
    if (Geo.IsLevelB != LevelB)
        return DTAPI_E_INVALID_VIDSTD;
    if (Frame == NULL || (BitsPerSymbol != 10 && BitsPerSymbol != 16) ||
        (LevelB && Field != 1 && Field != 2))
        return DTAPI_E_INVALID_ARG;
    if (Size != DtSdiFrame_RawSize(&Geo.Layout, BitsPerSymbol))
        return DTAPI_E_INVALID_SIZE;

    View->Geo = Geo;
    View->LevelBField = LevelB ? Field : 0;
    View->BitsPerSymbol = BitsPerSymbol;
    View->RingBase = NULL;
    View->IsTx = false;
    View->IsBuilt = false;
    View->WrapLineIndex = -1;
    View->Frame = (uint8_t*)Frame;
    View->FrameSize = Size;
    View->LineNumBits = DtSdiFrame_RawLineNumBits(&Geo.Layout, BitsPerSymbol);
    View->HasFrame = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetRawFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiView_SetRawFrame(DtSdiView* View, void* Frame, size_t Size, int VidStd,
                                  int BitsPerSymbol)
{
    return SetRaw(View, Frame, Size, VidStd, BitsPerSymbol, false, 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetRawPicture -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiView_SetRawPicture(DtSdiView* View, void* Frame, size_t Size, int VidStd,
                                    int BitsPerSymbol, int Field)
{
    return SetRaw(View, Frame, Size, VidStd, BitsPerSymbol, true, Field);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetRingFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame is smaller than the ring, so at most one of its lines wraps around the end.
//
DtapiResult DtSdiView_SetRingFrame(DtSdiView* View, const DtSdiFrameLayout* Layout,
                                   uint8_t* RingBase, size_t RingSize, size_t LinesStart,
                                   void* Holder)
{
    DtSdiView_Forget(View);
    DtSdiGeometry Geo;
    DtapiResult Result = DtSdiGeometry_Init(&Geo, Layout->VidStd);
    if (Result != DTAPI_OK)
        return Result;
    // Use the card's alignment for the sections, not the geometry's default.
    Geo.Layout = *Layout;

    const size_t Bytes = DtSdiFrame_RxCodedBytesPerLine(&Geo.Layout);
    int Wrap = -1;
    for (int Line = 0; Line < Geo.Layout.NumLines && Wrap < 0; Line++)
    {
        const size_t Offset = (LinesStart + (size_t)Line * Bytes) % RingSize;
        if (Offset + Bytes > RingSize)
            Wrap = Line;
    }
    if (Wrap >= 0)
    {
        if (View->WrapLineRoom < Bytes)
        {
            DtAlloc_Free(View->WrapLine);
            View->WrapLine = (uint8_t*)DtAlloc_Malloc(Bytes);
            View->WrapLineRoom = View->WrapLine == NULL ? 0 : Bytes;
            if (View->WrapLine == NULL)
                return DTAPI_E_OUT_OF_MEM;
        }
        const size_t Offset = (LinesStart + (size_t)Wrap * Bytes) % RingSize;
        const size_t First = RingSize - Offset;
        memcpy(View->WrapLine, RingBase + Offset, First);
        memcpy(View->WrapLine + First, RingBase, Bytes - First);
    }

    View->Geo = Geo;
    View->BitsPerSymbol = 10;
    View->Frame = NULL;
    View->FrameSize = 0;
    View->LineNumBits = DtSdiFrame_RawLineNumBits(&Geo.Layout, 10);
    View->RingBase = RingBase;
    View->RingSize = RingSize;
    View->LinesStart = LinesStart % RingSize;
    View->CodedBytesPerLine = Bytes;
    View->WrapLineIndex = Wrap;
    View->Holder = Holder;
    View->HasFrame = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetTxFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A frame is smaller than the ring, so at most one of its lines wraps around the end.
// The view only reserves a buffer for that line; nothing is copied until the frame is
// committed.
//
DtapiResult DtSdiView_SetTxFrame(DtSdiView* View, const DtSdiFrameLayout* Layout,
                                 uint8_t* RingBase, size_t RingSize, size_t LinesStart,
                                 void* Holder)
{
    DtSdiView_Forget(View);
    DtSdiGeometry Geo;
    DtapiResult Result = DtSdiGeometry_Init(&Geo, Layout->VidStd);
    if (Result != DTAPI_OK)
        return Result;
    // Use the card's alignment for the sections, not the geometry's default.
    Geo.Layout = *Layout;

    const size_t Bytes = DtSdiFrame_TxCodedBytesPerLine(&Geo.Layout);
    int Wrap = -1;
    for (int Line = 0; Line < Geo.Layout.NumLines && Wrap < 0; Line++)
    {
        const size_t Offset = (LinesStart + (size_t)Line * Bytes) % RingSize;
        if (Offset + Bytes > RingSize)
            Wrap = Line;
    }
    if (Wrap >= 0 && View->WrapLineRoom < Bytes)
    {
        DtAlloc_Free(View->WrapLine);
        View->WrapLine = (uint8_t*)DtAlloc_Malloc(Bytes);
        View->WrapLineRoom = View->WrapLine == NULL ? 0 : Bytes;
        if (View->WrapLine == NULL)
            return DTAPI_E_OUT_OF_MEM;
    }

    View->Geo = Geo;
    View->BitsPerSymbol = 10;
    View->Frame = NULL;
    View->FrameSize = 0;
    View->LineNumBits = DtSdiFrame_RawLineNumBits(&Geo.Layout, 10);
    View->RingBase = RingBase;
    View->RingSize = RingSize;
    View->LinesStart = LinesStart % RingSize;
    View->CodedBytesPerLine = Bytes;
    View->WrapLineIndex = Wrap;
    View->IsTx = true;
    View->IsBuilt = false;
    View->Holder = Holder;
    View->HasFrame = true;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_StoreWrapLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtSdiView_StoreWrapLine(const DtSdiView* View)
{
    if (View->WrapLineIndex < 0)
        return;
    const size_t Bytes = View->CodedBytesPerLine;
    const size_t Offset =
        (View->LinesStart + (size_t)View->WrapLineIndex * Bytes) % View->RingSize;
    const size_t First = View->RingSize - Offset;
    memcpy(View->RingBase + Offset, View->WrapLine, First);
    memcpy(View->RingBase, View->WrapLine + First, Bytes - First);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_TxCodedLines -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
uint8_t* DtSdiView_TxCodedLines(const DtSdiView* View, int LineIndex)
{
    if (LineIndex == View->WrapLineIndex)
        return View->WrapLine;
    const size_t Offset =
        (View->LinesStart + (size_t)LineIndex * View->CodedBytesPerLine) % View->RingSize;
    return View->RingBase + Offset;
}

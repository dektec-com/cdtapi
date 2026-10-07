// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiView.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Views of SDI frames: where a frame's lines lie in memory
//
// SPDX-License-Identifier: BSD-3-Clause
//
// For now partly a stub: a view can be created and freed and tells its format, and the
// rest returns DTAPI_E_NOT_SUPPORTED; plan 0032 fills it in.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h" // Allocation seam.
#include "DtSdiView.h"    // Interface being implemented.

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
    (void)Line;
    if (View == NULL || Symbols == NULL)
        return DTAPI_E_INVALID_ARG;
    memset(Symbols, 0, sizeof(*Symbols));
    if (!View->HasFrame)
        return DTAPI_E_STATE;
    return DTAPI_E_NOT_SUPPORTED;
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
        *VidStd = View->VidStd;
    if (BitsPerSymbol != NULL)
        *BitsPerSymbol = View->BitsPerSymbol;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_GetPayloadId -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiView_GetPayloadId(const DtSdiView* View, uint32_t* PayloadId)
{
    if (View == NULL || PayloadId == NULL)
        return DTAPI_E_INVALID_ARG;
    *PayloadId = 0;
    if (!View->HasFrame)
        return DTAPI_E_STATE;
    return DTAPI_E_NOT_SUPPORTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_RawFrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiView_RawFrameSize(int VidStd, int BitsPerSymbol, size_t* Size)
{
    (void)VidStd;
    (void)BitsPerSymbol;
    if (Size == NULL)
        return DTAPI_E_INVALID_ARG;
    *Size = 0;
    return DTAPI_E_NOT_SUPPORTED;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiView_SetRawFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtSdiView_SetRawFrame(DtSdiView* View, void* Frame, size_t Size, int VidStd,
                                  int BitsPerSymbol)
{
    (void)Size;
    (void)VidStd;
    (void)BitsPerSymbol;
    if (View == NULL || Frame == NULL)
        return DTAPI_E_INVALID_ARG;
    if (View->Holder != NULL)
        return DTAPI_E_IN_USE;
    View->HasFrame = false;
    return DTAPI_E_NOT_SUPPORTED;
}

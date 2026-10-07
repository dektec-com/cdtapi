// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtSdiGeometry.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Where the image of an SDI frame lies in its raw frame
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtSdiGeometry.h"      // Interface being implemented.
#include "Video/DtFrameProps.h" // The lines of each field.
#include "Video/DtVidStd.h"     // Level B.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The stream alignment the layout is made with. A raw frame does not depend on it.
#define DT_SDIGEOMETRY_ALIGNMENT_BITS 64

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiGeometry_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiGeometry_Init(DtSdiGeometry* Geo, int VidStd)
{
    memset(Geo, 0, sizeof(*Geo));

    const DtVidStdEntry* Info = DtVidStd_Find(VidStd);
    DtFrameProps Props;
    if (Info == NULL || Info->IsLevelB || !DtFrameProps_Init(&Props, VidStd) ||
        !DtSdiFrame_LayoutInit(&Geo->Layout, VidStd, DT_SDIGEOMETRY_ALIGNMENT_BITS))
    {
        return DTAPI_E_INVALID_VIDSTD;
    }

    Geo->VidStd = VidStd;
    Geo->Is4k = Geo->Layout.Is4k;
    if (Geo->Is4k)
    {
        // Props describes one link: its 1080p frame.
        Geo->LinkWidth = Props.LineNumSymActive / 2;
        Geo->Width = 2 * Geo->LinkWidth;
        Geo->NumFields = 1;
        Geo->PictureFirstIndex = Geo->Layout.PictureStartLine - 1;
        Geo->Height = 2 * (Geo->Layout.PictureEndLine - Geo->Layout.PictureStartLine + 1);
        return DTAPI_OK;
    }

    Geo->Width = Props.LineNumSymActive / 2;
    Geo->NumFields = Props.NumFields;
    for (int f = 0; f < Props.NumFields; f++)
    {
        Geo->FieldFirstIndex[f] = Props.Fields[f].ActiveStartLine - 1;
        Geo->FieldNumLines[f] =
            Props.Fields[f].ActiveEndLine - Props.Fields[f].ActiveStartLine + 1;
        Geo->Height += Geo->FieldNumLines[f];
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiGeometry_RawLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSdiGeometry_RawLine(const DtSdiGeometry* Geo, int ImageLine)
{
    if (Geo->NumFields == 1)
        return Geo->FieldFirstIndex[0] + ImageLine;
    return Geo->FieldFirstIndex[ImageLine & 1] + ImageLine / 2;
}

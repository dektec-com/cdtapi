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

// Where each link's words lie in a group of eight words of a raw 2160p line: the C words
// of links 4, 2, 3 and 1, then their Y words. g_LinkPlace[L] is the place of link L + 1.
static const int g_LinkPlace[4] = {3, 1, 2, 0};

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
    Geo->SwitchingIndex = Props.Fields[0].SwitchingLine - 1;

    // The streams. Of 2160p, Props describes one link, whose streams are those of HD.
    const bool IsSd = DtFrameProps_IsSd(&Props);
    const int PerLink = IsSd ? 1 : 2;
    Geo->NumStreams = Geo->Is4k ? 8 : PerLink;
    Geo->StreamEavWords = Props.LineNumSymEav / PerLink;
    Geo->StreamSavWords = Props.LineNumSymSav / PerLink;
    Geo->StreamHancWords = DtFrameProps_LineNumSymHancInclTiming(&Props) / PerLink;
    Geo->StreamActiveWords = Props.LineNumSymActive / PerLink;
    if (Geo->Is4k)
    {
        for (int Link = 0; Link < 4; Link++)
        {
            Geo->StreamFirst[2 * Link] = g_LinkPlace[Link];
            Geo->StreamFirst[2 * Link + 1] = 4 + g_LinkPlace[Link];
            Geo->StreamIsChroma[2 * Link] = true;
            Geo->StreamLink[2 * Link] = Geo->StreamLink[2 * Link + 1] = Link + 1;
        }
    }
    else
    {
        Geo->StreamFirst[0] = 0;
        Geo->StreamFirst[1] = 1;
        Geo->StreamIsChroma[0] = !IsSd;
        Geo->StreamLink[0] = Geo->StreamLink[1] = 1;
    }

    if (Geo->Is4k)
    {
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiGeometry_IsVanc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtSdiGeometry_IsVanc(const DtSdiGeometry* Geo, int LineIndex)
{
    if (Geo->Is4k)
        return LineIndex < Geo->PictureFirstIndex ||
               LineIndex >= Geo->PictureFirstIndex + Geo->Height / 2;
    for (int f = 0; f < Geo->NumFields; f++)
    {
        if (LineIndex >= Geo->FieldFirstIndex[f] &&
            LineIndex < Geo->FieldFirstIndex[f] + Geo->FieldNumLines[f])
        {
            return false;
        }
    }
    return true;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiGeometry_RawLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int DtSdiGeometry_RawLine(const DtSdiGeometry* Geo, int ImageLine)
{
    if (Geo->NumFields == 1)
        return Geo->FieldFirstIndex[0] + ImageLine;
    return Geo->FieldFirstIndex[ImageLine & 1] + ImageLine / 2;
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimLevelB.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - How the emulated card carries 3G level B on the cable, in symbols
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A line of a picture and a line of a link hold the same words. Moving a line from one
// to the other gives it the fourth word of its EAV and SAV, its line number and its CRC
// words anew, and keeps the rest.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "SimLevelB.h"      // Interface being implemented.
#include "SimSdiWords.h"    // Timing references, line numbers and CRCs.
#include "Video/DtVidStd.h" // Which standards are level B.
#include "cdtapi.h"         // DTAPI_VIDSTD_ codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The last interface line of field 1.
#define SIM_LEVELB_FIELD1_END 562

// The DID and SDID of the payload ID (SMPTE ST 352), and its byte 1 for 1080 lines on
// 3G level B.
#define SIM_LEVELB_DID_PAYLOAD_ID 0x41
#define SIM_LEVELB_SDID_PAYLOAD_ID 0x01
#define SIM_LEVELB_PAYLOAD_ID_1080_B 0x8A

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InterfaceOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the 1080i standard that carries VidStd: the interface runs at half the picture
// rate. Returns DTAPI_VIDSTD_UNKNOWN for another standard.
//
static int InterfaceOf(int VidStd)
{
    switch (VidStd)
    {
    case DTAPI_VIDSTD_1080P50B:
        return DTAPI_VIDSTD_1080I50;
    case DTAPI_VIDSTD_1080P59_94B:
        return DTAPI_VIDSTD_1080I59_94;
    case DTAPI_VIDSTD_1080P60B:
        return DTAPI_VIDSTD_1080I60;
    default:
        return DTAPI_VIDSTD_UNKNOWN;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeBlank -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills the line of Count symbols at Words, every Stride symbols, with blanking and the
// first six words of its EAV and SAV; SetTiming and SetCrcs add the rest.
//
static void MakeBlank(const SimLevelB* LevelB, uint16_t* Words, size_t Stride)
{
    const int Sav = LevelB->HancSyms - 8;

    for (int i = 0; i < LevelB->LineSyms; i++)
        Words[(size_t)i * Stride] = (uint16_t)(i % 2 == 0 ? 0x200 : 0x040);
    for (int s = 0; s < 2; s++)
    {
        Words[(size_t)s * Stride] = Words[(size_t)(Sav + s) * Stride] = 0x3FF;
        for (int k = 1; k < 3; k++)
        {
            Words[(size_t)(2 * k + s) * Stride] = 0x000;
            Words[(size_t)(Sav + 2 * k + s) * Stride] = 0x000;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CopyLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Copies the LineSyms symbols at From, every FromStride symbols, to To, every ToStride.
//
static void CopyLine(const SimLevelB* LevelB, const uint16_t* From, size_t FromStride,
                     uint16_t* To, size_t ToStride)
{
    for (int i = 0; i < LevelB->LineSyms; i++)
        To[(size_t)i * ToStride] = From[(size_t)i * FromStride];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTiming -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives the line at Words, every Stride symbols, the fourth words of the EAV and SAV and
// the line number of line Line, from 1, of a frame of Props.
//
static void SetTiming(const SimLevelB* LevelB, uint16_t* Words, size_t Stride,
                      const DtFrameProps* Props, int Line)
{
    const uint16_t Eav = (uint16_t)SimSdiWords_Xyz(Props, Line, true);
    const uint16_t Sav = (uint16_t)SimSdiWords_Xyz(Props, Line, false);
    const uint16_t Ln0 = (uint16_t)SimSdiWords_WithParity((uint32_t)Line << 2);
    const uint16_t Ln1 =
        (uint16_t)SimSdiWords_WithParity((uint32_t)(Line >> 7) << 2 & 0x3C);

    for (int s = 0; s < 2; s++)
    {
        Words[(size_t)(6 + s) * Stride] = Eav;
        Words[(size_t)(8 + s) * Stride] = Ln0;
        Words[(size_t)(10 + s) * Stride] = Ln1;
        Words[(size_t)(LevelB->HancSyms - 2 + s) * Stride] = Sav;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetCrcs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives the line at Words, every Stride symbols, its CRC words: each stream's CRC covers
// the active part of the line before, which Crc holds, and then this line's EAV and line
// number. Then sets Crc to the CRCs of this line's active part, for the line after.
//
static void SetCrcs(const SimLevelB* LevelB, uint16_t* Words, size_t Stride,
                    uint32_t* Crc)
{
    for (int s = 0; s < 2; s++)
    {
        uint32_t Value = Crc[s];

        for (int k = 0; k < 6; k++)
            Value = SimSdiWords_Crc18(Value, Words[(size_t)(2 * k + s) * Stride]);
        Words[(size_t)(12 + s) * Stride] = (uint16_t)SimSdiWords_WithParity(Value);
        Words[(size_t)(14 + s) * Stride] = (uint16_t)SimSdiWords_WithParity(Value >> 9);

        Value = 0;
        for (int i = LevelB->HancSyms + s; i < LevelB->LineSyms; i += 2)
            Value = SimSdiWords_Crc18(Value, Words[(size_t)i * Stride]);
        Crc[s] = Value;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinkStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns where the symbols of link Link (0 for A, 1 for B) of interface line Line, from
// 1, start in a frame of the interface; they lie every second symbol, B's first.
//
static size_t LinkStart(const SimLevelB* LevelB, int Line, int Link)
{
    return (size_t)(Line - 1) * 2 * (size_t)LevelB->LineSyms + (Link == 0 ? 1 : 0);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PictureLineStart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Returns where line Line, from 1, starts in a picture.
//
static size_t PictureLineStart(const SimLevelB* LevelB, int Line)
{
    return (size_t)(Line - 1) * (size_t)LevelB->LineSyms;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Level B on the cable +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLevelB_BlackPicture -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void SimLevelB_BlackPicture(const SimLevelB* LevelB, uint16_t* Picture)
{
    uint32_t Crc[2] = {0, 0};

    for (int Line = 1; Line <= LevelB->NumLines; Line++)
    {
        uint16_t* To = Picture + PictureLineStart(LevelB, Line);

        MakeBlank(LevelB, To, 1);
        SetTiming(LevelB, To, 1, &LevelB->Picture, Line);
        SetCrcs(LevelB, To, 1, Crc);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLevelB_Field -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
int SimLevelB_Field(const SimLevelB* LevelB, const uint16_t* Picture)
{
    // The words of the Y stream between the CRC words and the SAV.
    const int First = 8;
    const int End = LevelB->HancSyms / 2 - 4;

    for (int Line = 1; Line < LevelB->Picture.Fields[0].ActiveStartLine; Line++)
    {
        const uint16_t* Y = Picture + PictureLineStart(LevelB, Line) + 1;

        for (int k = First; k + 7 <= End; k++)
        {
            if (Y[2 * k] != 0x000 || Y[2 * (k + 1)] != 0x3FF || Y[2 * (k + 2)] != 0x3FF)
                continue;
            const int Count = Y[2 * (k + 5)] & 0xFF;
            if (k + 7 + Count > End)
                break;
            if ((Y[2 * (k + 3)] & 0xFF) == SIM_LEVELB_DID_PAYLOAD_ID &&
                (Y[2 * (k + 4)] & 0xFF) == SIM_LEVELB_SDID_PAYLOAD_ID && Count >= 4 &&
                (Y[2 * (k + 6)] & 0xFF) == SIM_LEVELB_PAYLOAD_ID_1080_B &&
                (Y[2 * (k + 9)] & 0x40) == 0)
            {
                return Line % 2 != 0 ? 2 : 1;
            }
            k += 6 + Count;
        }
    }
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLevelB_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool SimLevelB_Init(SimLevelB* LevelB, int VidStd)
{
    memset(LevelB, 0, sizeof(*LevelB));
    const DtVidStdEntry* Info = DtVidStd_Find(VidStd);
    const int Interface = InterfaceOf(VidStd);
    if (Info == NULL || !Info->IsLevelB || Interface == DTAPI_VIDSTD_UNKNOWN ||
        !DtFrameProps_Init(&LevelB->Picture, VidStd) ||
        !DtFrameProps_Init(&LevelB->Interface, Interface))
    {
        return false;
    }
    LevelB->NumLines = DtFrameProps_NumLines(&LevelB->Picture);
    LevelB->HancSyms = DtFrameProps_LineNumSymHancInclTiming(&LevelB->Picture);
    LevelB->LineSyms = LevelB->HancSyms + LevelB->Picture.LineNumSymActive;

    // Each interface line carries one picture line on each link.
    const int InterfaceSyms = DtFrameProps_LineNumSymHancInclTiming(&LevelB->Interface) +
                              LevelB->Interface.LineNumSymActive;
    return DtFrameProps_NumLines(&LevelB->Interface) == LevelB->NumLines &&
           InterfaceSyms == LevelB->LineSyms;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLevelB_Merge -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimLevelB_Merge(SimLevelB* LevelB, const uint16_t* Field1, const uint16_t* Field2,
                     uint16_t* Frame)
{
    for (int Line = 1; Line <= LevelB->NumLines; Line++)
    {
        const bool InField1 = Line <= SIM_LEVELB_FIELD1_END;
        const uint16_t* Picture = InField1 ? Field1 : Field2;

        for (int Link = 0; Link < 2; Link++)
        {
            const int From = InField1 ? 2 * Line + Link
                                      : 2 * (Line - SIM_LEVELB_FIELD1_END - 1) + 1 + Link;
            uint16_t* To = Frame + LinkStart(LevelB, Line, Link);

            if (From > LevelB->NumLines)
                MakeBlank(LevelB, To, 2);
            else
                CopyLine(LevelB, Picture + PictureLineStart(LevelB, From), 1, To, 2);
            SetTiming(LevelB, To, 2, &LevelB->Interface, Line);
            SetCrcs(LevelB, To, 2, LevelB->LinkCrc[Link]);
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLevelB_Split -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void SimLevelB_Split(SimLevelB* LevelB, const uint16_t* Frame, uint16_t* Field1,
                     uint16_t* Field2)
{
    for (int Field = 1; Field <= 2; Field++)
    {
        uint16_t* Picture = Field == 1 ? Field1 : Field2;

        for (int Line = 1; Line <= LevelB->NumLines; Line++)
        {
            uint16_t* To = Picture + PictureLineStart(LevelB, Line);

            if (Field == 1 && Line == 1)
                MakeBlank(LevelB, To, 1);
            else if (Field == 1)
                CopyLine(LevelB, Frame + LinkStart(LevelB, Line / 2, Line % 2), 2, To, 1);
            else
            {
                const int From = (Line - 1) / 2 + SIM_LEVELB_FIELD1_END + 1;
                CopyLine(LevelB, Frame + LinkStart(LevelB, From, (Line - 1) % 2), 2, To,
                         1);
            }
            SetTiming(LevelB, To, 1, &LevelB->Picture, Line);
            SetCrcs(LevelB, To, 1, LevelB->PictureCrc);
        }
    }
}

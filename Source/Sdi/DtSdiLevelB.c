// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtSdiLevelB.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - 3G level B: between two pictures and the frame on the line
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A line of a picture and a line of a link have the same words: EAV, the line number and
// the CRC, the horizontal blanking, SAV and the active part, in a C and a Y stream, C
// first. Moving a line from one to the other changes the fourth word of EAV and SAV, the
// line number and the CRC words, and nothing else.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtSdiLevelB.h"      // Interface being implemented.
#include "DtSdiSymbols.h"     // Reading symbols.
#include "Video/DtSdiFrame.h" // Timing references, line numbers and CRCs.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The blanking values of a C word and of a Y word, and the CRC word left for the
// transmitter.
#define DT_SDILEVELB_BLANK_C 0x200
#define DT_SDILEVELB_BLANK_Y 0x040
#define DT_SDILEVELB_CRC_FOR_TRANSMITTER 0x200

// The last interface line of field 1.
#define DT_SDILEVELB_FIELD1_END DT_SDIGEOMETRY_LEVELB_FIELD1_END

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinkWords -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the words of one line of a picture or a link: both streams.
//
static int LinkWords(const DtSdiLevelB* Converter)
{
    const DtSdiGeometry* Geo = &Converter->Geo;
    return 2 * (Geo->StreamHancWords + Geo->StreamActiveWords);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Reads Count symbols of line LineIndex (from 0) of a raw frame whose lines are Count
// symbols of Bits bits each, into Words.
//
static void ReadLine(const DtSdiLevelB* Converter, const uint8_t* Frame, int Bits,
                     int LineIndex, size_t Count, uint16_t* Words)
{
    const size_t FirstBit = (size_t)LineIndex * Count * (size_t)Bits;
    DtSdiSymbolPtr Ptr;
    Ptr.Byte = Frame + FirstBit / 8;
    Ptr.Bit = (int)(FirstBit % 8);
    Ptr.BitsPerSymbol = Bits;
    DtSdiSymbols_Read(&Ptr, Count, Words, Converter->Conv);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WriteLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes the Count symbols of Words as line LineIndex (from 0) of a raw frame whose lines
// are Count symbols of Bits bits each. Every line of level B starts on a byte boundary,
// and Count is a multiple of four.
//
static void WriteLine(const DtSdiLevelB* Converter, uint8_t* Frame, int Bits,
                      int LineIndex, size_t Count, const uint16_t* Words)
{
    uint8_t* Out = Frame + (size_t)LineIndex * Count * (size_t)Bits / 8;
    if (Bits == 10)
    {
        Converter->Conv->Pack10(Words, Count, Out);
        return;
    }
    for (size_t k = 0; k < Count; k++)
    {
        Out[2 * k] = (uint8_t)(Words[k] & 0xFF);
        Out[2 * k + 1] = (uint8_t)(Words[k] >> 8);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MakeBlank -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Fills the line in Words with blanking and the timing references' first three words;
// SetTiming adds the rest.
//
static void MakeBlank(const DtSdiLevelB* Converter, uint16_t* Words)
{
    const int Hanc = Converter->Geo.StreamHancWords;
    const int Count = LinkWords(Converter);
    for (int k = 0; k < Count; k++)
        Words[k] = (k & 1) == 0 ? DT_SDILEVELB_BLANK_C : DT_SDILEVELB_BLANK_Y;
    for (int s = 0; s < 2; s++)
    {
        Words[s] = Words[2 * (Hanc - 4) + s] = 0x3FF;
        Words[2 + s] = Words[4 + s] = 0x000;
        Words[2 * (Hanc - 3) + s] = Words[2 * (Hanc - 2) + s] = 0x000;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetTiming -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives the line in Words the fourth words of EAV and SAV and the line number of line
// Line (from 1) of a frame of Props, in both streams.
//
static void SetTiming(const DtSdiLevelB* Converter, uint16_t* Words,
                      const DtFrameProps* Props, int Line)
{
    const int Hanc = Converter->Geo.StreamHancWords;
    const uint16_t Eav = (uint16_t)DtSdiFrame_Xyz(Props, Line, true);
    const uint16_t Sav = (uint16_t)DtSdiFrame_Xyz(Props, Line, false);
    for (int s = 0; s < 2; s++)
    {
        Words[6 + s] = Eav;
        Words[8 + s] = (uint16_t)DtSdiFrame_WithParity((uint32_t)Line << 2);
        Words[10 + s] =
            (uint16_t)DtSdiFrame_WithParity((uint32_t)(Line >> 7) << 2 & 0x3C);
        Words[2 * (Hanc - 1) + s] = Sav;
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetCrcs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Gives the line in Words, of link Link (0 for A, 1 for B), its CRC words: each stream's
// CRC covers the active part of the link's line before, from Converter->LastCrc, then
// this line's EAV and line number. Then keeps the CRCs of this line's active part for
// the next line.
//
static void SetCrcs(DtSdiLevelB* Converter, int Link, uint16_t* Words)
{
    const DtSdiGeometry* Geo = &Converter->Geo;
    const uint32_t* Table = Converter->CrcTable;
    for (int s = 0; s < 2; s++)
    {
        uint32_t Crc = Converter->LastCrc[Link][s];
        for (int k = 0; k < 6; k++)
            Crc = (Crc >> 10) ^ Table[(Crc ^ Words[2 * k + s]) & 0x3FF];
        Words[12 + s] = (uint16_t)DtSdiFrame_WithParity(Crc);
        Words[14 + s] = (uint16_t)DtSdiFrame_WithParity(Crc >> 9);
    }
    uint32_t Crcs[DT_SDICRC_MAX_STREAMS];
    Converter->Crc(Words + 2 * Geo->StreamHancWords, (size_t)Geo->StreamActiveWords, 2,
                   Table, Crcs);
    Converter->LastCrc[Link][0] = Crcs[0];
    Converter->LastCrc[Link][1] = Crcs[1];
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PictureLine -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Returns the line of the picture, from 1, that link Link (0 for A, 1 for B) of interface
// line Line carries; past the picture's last line, the next picture's line 1. See SMPTE
// ST 372, Figure 2.
//
static int PictureLine(int Line, int Link)
{
    if (Line <= DT_SDILEVELB_FIELD1_END)
        return 2 * Line + Link;
    return 2 * (Line - DT_SDILEVELB_FIELD1_END - 1) + 1 + Link;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Level B +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_FrameSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
size_t DtSdiLevelB_FrameSize(const DtSdiLevelB* Converter, int BitsPerSymbol)
{
    const size_t Symbols =
        (size_t)Converter->Geo.Layout.NumLines * 2 * (size_t)LinkWords(Converter);
    return (Symbols * (size_t)BitsPerSymbol + 63) / 64 * 8;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtSdiLevelB_Init(DtSdiLevelB* Converter, int VidStd)
{
    memset(Converter, 0, sizeof(*Converter));
    DtapiResult Result = DtSdiGeometry_Init(&Converter->Geo, VidStd);
    if (Result == DTAPI_OK && !Converter->Geo.IsLevelB)
        Result = DTAPI_E_INVALID_VIDSTD;
    if (Result == DTAPI_OK &&
        !DtFrameProps_Init(&Converter->Interface, Converter->Geo.InterfaceVidStd))
        Result = DTAPI_E_INVALID_VIDSTD;
    if (Result != DTAPI_OK)
        return Result;
    Converter->Conv = DtSdiConv_Best();
    Converter->Crc = DtSdiCrc_Best();
    for (uint32_t i = 0; i < 1024; i++)
        Converter->CrcTable[i] = DtSdiFrame_Crc18(i, 0);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_PutField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Line by line of the interface: the two picture lines it carries, or blanking past the
// picture's end, get the interface's timing and their CRCs, and are interleaved, link B's
// word first.
//
void DtSdiLevelB_PutField(DtSdiLevelB* Converter, int Field, const uint8_t* Picture,
                          int PictureBits, uint8_t* Frame, int FrameBits)
{
    const int Words = LinkWords(Converter);
    const int NumLines = Converter->Geo.Layout.NumLines;
    const int First = Field == 1 ? 1 : DT_SDILEVELB_FIELD1_END + 1;
    const int Last = Field == 1 ? DT_SDILEVELB_FIELD1_END : NumLines;

    // The CRCs of the line before the field's first.
    const bool Follows =
        Converter->CrcLine == First - 1 || (First == 1 && Converter->CrcLine == NumLines);
    if (!Follows && First == 1)
        memset(Converter->LastCrc, 0, sizeof(Converter->LastCrc));
    else if (!Follows)
    {
        ReadLine(Converter, Frame, FrameBits, First - 2, 2 * (size_t)Words,
                 Converter->Line);
        for (int Link = 0; Link < 2; Link++)
        {
            uint16_t* L = Converter->Link[Link];
            for (int k = 0; k < Words; k++)
                L[k] = Converter->Line[2 * k + 1 - Link];
            uint32_t Crcs[DT_SDICRC_MAX_STREAMS];
            Converter->Crc(L + 2 * Converter->Geo.StreamHancWords,
                           (size_t)Converter->Geo.StreamActiveWords, 2,
                           Converter->CrcTable, Crcs);
            Converter->LastCrc[Link][0] = Crcs[0];
            Converter->LastCrc[Link][1] = Crcs[1];
        }
    }

    for (int Line = First; Line <= Last; Line++)
    {
        for (int Link = 0; Link < 2; Link++)
        {
            uint16_t* L = Converter->Link[Link];
            const int From = PictureLine(Line, Link);
            if (From > NumLines)
                MakeBlank(Converter, L);
            else
                ReadLine(Converter, Picture, PictureBits, From - 1, (size_t)Words, L);
            SetTiming(Converter, L, &Converter->Interface, Line);
            SetCrcs(Converter, Link, L);
        }
        for (int k = 0; k < Words; k++)
        {
            Converter->Line[2 * k] = Converter->Link[1][k];
            Converter->Line[2 * k + 1] = Converter->Link[0][k];
        }
        WriteLine(Converter, Frame, FrameBits, Line - 1, 2 * (size_t)Words,
                  Converter->Line);
    }
    Converter->CrcLine = Last;

    const size_t End = (size_t)NumLines * 2 * (size_t)Words * (size_t)FrameBits / 8;
    memset(Frame + End, 0, DtSdiLevelB_FrameSize(Converter, FrameBits) - End);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtSdiLevelB_TakeField -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Line by line of the interface: the two links are taken apart, and each line of the
// picture gets the picture's timing and CRC words for the transmitter. Line 1 of field 1
// is not in the frame, and becomes blanking.
//
void DtSdiLevelB_TakeField(DtSdiLevelB* Converter, int Field, const uint8_t* Frame,
                           int FrameBits, uint8_t* Picture, int PictureBits)
{
    const int Words = LinkWords(Converter);
    const int NumLines = Converter->Geo.Layout.NumLines;
    const DtFrameProps* Props = &Converter->Geo.Props;
    const int First = Field == 1 ? 1 : DT_SDILEVELB_FIELD1_END + 1;
    const int Last = Field == 1 ? DT_SDILEVELB_FIELD1_END : NumLines;

    for (int Line = First; Line <= Last; Line++)
    {
        ReadLine(Converter, Frame, FrameBits, Line - 1, 2 * (size_t)Words,
                 Converter->Line);
        for (int Link = 0; Link < 2; Link++)
        {
            const int To = PictureLine(Line, Link);
            if (To > NumLines)
                continue;
            uint16_t* L = Converter->Link[Link];
            for (int k = 0; k < Words; k++)
                L[k] = Converter->Line[2 * k + 1 - Link];
            SetTiming(Converter, L, Props, To);
            for (int s = 0; s < 4; s++)
                L[12 + s] = DT_SDILEVELB_CRC_FOR_TRANSMITTER;
            WriteLine(Converter, Picture, PictureBits, To - 1, (size_t)Words, L);
        }
    }
    if (Field == 1)
    {
        uint16_t* L = Converter->Link[0];
        MakeBlank(Converter, L);
        SetTiming(Converter, L, Props, 1);
        for (int s = 0; s < 4; s++)
            L[12 + s] = DT_SDILEVELB_CRC_FOR_TRANSMITTER;
        WriteLine(Converter, Picture, PictureBits, 0, (size_t)Words, L);
    }

    const size_t End = (size_t)NumLines * (size_t)Words * (size_t)PictureBits / 8;
    memset(Picture + End, 0,
           DtSdiFrame_RawSize(&Converter->Geo.Layout, PictureBits) - End);
}

// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSimLevelB.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The emulated card's 3G level B on the cable, against the library's
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The emulator converts between the pictures of the card's ring and the frame of the
// interface with a converter of its own, SimLevelB. These cases check it against the
// library's, DtSdiLevelB, word for word: the frames it merges, CRCs included, over two
// frames in a row; the pictures it splits, but for their CRC words, which the library
// leaves for the transmitter; that a split gives back the pictures merged; and that it
// finds a picture's field where the library's view does.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"             // Test framework.
#include "OAL/Sim/SimChSdiRx.h" // The emulator's pictures.
#include "OAL/Sim/SimLevelB.h"  // Unit under test.
#include "Sdi/DtSdiLevelB.h"    // The library's converter.
#include "Sdi/DtSdiView.h"      // The field of a picture, as the library finds it.
#include "cdtapi_sdi.h"         // The builder.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The standards of 3G level B.
static const int g_LevelBStds[3] = {DTAPI_VIDSTD_1080P50B, DTAPI_VIDSTD_1080P59_94B,
                                    DTAPI_VIDSTD_1080P60B};

// The buffers of one case: two pictures and a frame, in symbols for SimLevelB and in
// 16-bit raw frames for the library.
typedef struct Buffers
{
    SimLevelB Sim;
    DtSdiLevelB Lib;
    size_t PictureSyms;
    size_t FrameSyms;
    size_t PictureBytes; // A raw picture of 16 bits a symbol, with its padding
    size_t FrameBytes;   // A raw frame of the interface of 16 bits a symbol
    uint16_t* Pictures[2];
    uint16_t* Frame;
    uint8_t* RawPicture;
    uint8_t* RawFrame;
} Buffers;

// Frees the buffers of the case; Context is its Buffers. It is the case's cleanup too.
static void Buffers_Free(void* Context)
{
    Buffers* B = (Buffers*)Context;
    DtSdiLevelB_Free(&B->Lib);
    for (int i = 0; i < 2; i++)
        free(B->Pictures[i]);
    free(B->Frame);
    free(B->RawPicture);
    free(B->RawFrame);
    memset(B, 0, sizeof(*B));
}

static bool Buffers_Init(Buffers* B, int VidStd)
{
    memset(B, 0, sizeof(*B));
    if (!SimLevelB_Init(&B->Sim, VidStd) || DtSdiLevelB_Init(&B->Lib, VidStd) != DTAPI_OK)
        return false;
    B->PictureSyms = (size_t)B->Sim.NumLines * (size_t)B->Sim.LineSyms;
    B->FrameSyms = 2 * B->PictureSyms;
    B->PictureBytes = DtSdiFrame_RawSize(&B->Lib.Geo.Layout, 16);
    B->FrameBytes = DtSdiLevelB_FrameSize(&B->Lib.Geo, 16);
    for (int i = 0; i < 2; i++)
        B->Pictures[i] = (uint16_t*)malloc(B->PictureSyms * sizeof(uint16_t));
    B->Frame = (uint16_t*)malloc(B->FrameSyms * sizeof(uint16_t));
    B->RawPicture = (uint8_t*)calloc(1, B->PictureBytes);
    B->RawFrame = (uint8_t*)calloc(1, B->FrameBytes);
    return B->Pictures[0] != NULL && B->Pictures[1] != NULL && B->Frame != NULL &&
           B->RawPicture != NULL && B->RawFrame != NULL &&
           B->PictureBytes >= 2 * B->PictureSyms && B->FrameBytes >= 2 * B->FrameSyms;
}

// Writes Count symbols as 16-bit little-endian words.
static void ToRaw(const uint16_t* Symbols, size_t Count, uint8_t* Raw)
{
    for (size_t i = 0; i < Count; i++)
    {
        Raw[2 * i] = (uint8_t)(Symbols[i] & 0xFF);
        Raw[2 * i + 1] = (uint8_t)(Symbols[i] >> 8);
    }
}

// Reads Count symbols from 16-bit little-endian words.
static void FromRaw(const uint8_t* Raw, size_t Count, uint16_t* Symbols)
{
    for (size_t i = 0; i < Count; i++)
        Symbols[i] = (uint16_t)(Raw[2 * i] | Raw[2 * i + 1] << 8);
}

// Fills Picture with picture Number of the emulator's own pattern.
static bool MakePicture(const Buffers* B, int VidStd, uint32_t Number, uint16_t* Picture)
{
    for (int Line = 1; Line <= B->Sim.NumLines; Line++)
    {
        uint16_t* At = Picture + (size_t)(Line - 1) * (size_t)B->Sim.LineSyms;
        if (SimChSdiRx_Line(VidStd, Number, Line, At) != B->Sim.LineSyms)
            return false;
    }
    return true;
}

// Returns the first symbol from which Mine and the library's 16-bit raw frame differ,
// or -1 when the first Count are the same. With LineSyms not 0, the CRC words of each
// line of LineSyms symbols are not compared.
static long FirstDifference(const uint16_t* Mine, const uint8_t* Raw, size_t Count,
                            int LineSyms)
{
    for (size_t i = 0; i < Count; i++)
    {
        const size_t InLine = LineSyms != 0 ? i % (size_t)LineSyms : 0;
        if (LineSyms != 0 && InLine >= 12 && InLine < 16)
            continue;
        if (Mine[i] != (uint16_t)(Raw[2 * i] | Raw[2 * i + 1] << 8))
            return (long)i;
    }
    return -1;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Takes the standards of 3G level B, and refuses the others.
DT_TEST(InitTakesLevelBOnly)
{
    SimLevelB Sim;
    for (int i = 0; i < 3; i++)
    {
        DT_ASSERT(SimLevelB_Init(&Sim, g_LevelBStds[i]));
        DT_ASSERT_EQ(Sim.NumLines, 1125);
        DT_ASSERT_EQ(Sim.LineSyms,
                     g_LevelBStds[i] == DTAPI_VIDSTD_1080P50B ? 5280 : 4400);
    }
    DT_ASSERT(!SimLevelB_Init(&Sim, DTAPI_VIDSTD_1080P50));
    DT_ASSERT(!SimLevelB_Init(&Sim, DTAPI_VIDSTD_1080I50));
    DT_ASSERT(!SimLevelB_Init(&Sim, DTAPI_VIDSTD_2160P50B));
    DT_ASSERT(!SimLevelB_Init(&Sim, DTAPI_VIDSTD_UNKNOWN));
}

// Merges two frames in a row of the emulator's pictures, and splits them again. The
// frames are the library's, word for word, the CRCs of the second frame's line 1
// following on from the first frame. The pictures split are the library's but for their
// CRC words, and the pictures merged: a picture of field 2 whole, and a picture of
// field 1 from its line 3, its line 1 being blanking and the CRC of its line 2 covering
// that.
DT_TEST(MergesAndSplitsAsTheLibrary)
{
    for (int s = 0; s < 3; s++)
    {
        const int VidStd = g_LevelBStds[s];
        Buffers B;
        DT_ASSERT(Buffers_Init(&B, VidStd));
        DtTest_SetCleanup(Buffers_Free, &B);
        SimLevelB Splitter;
        DT_ASSERT(SimLevelB_Init(&Splitter, VidStd));
        const size_t LineSyms = (size_t)B.Sim.LineSyms;

        for (uint32_t Frame = 0; Frame < 2; Frame++)
        {
            for (int f = 0; f < 2; f++)
            {
                DT_ASSERT(
                    MakePicture(&B, VidStd, 2 * Frame + (uint32_t)f, B.Pictures[f]));
                ToRaw(B.Pictures[f], B.PictureSyms, B.RawPicture);
                DtSdiLevelB_PutField(&B.Lib, f + 1, B.RawPicture, 16, B.RawFrame, 16,
                                     NULL);
            }
            SimLevelB_Merge(&B.Sim, B.Pictures[0], B.Pictures[1], B.Frame);
            DT_ASSERT_EQ(FirstDifference(B.Frame, B.RawFrame, B.FrameSyms, 0), -1);

            SimLevelB_Split(&Splitter, B.Frame, B.Pictures[0], B.Pictures[1]);
            for (int f = 0; f < 2; f++)
            {
                DtSdiLevelB_TakeField(&B.Lib, f + 1, B.RawFrame, 16, B.RawPicture, 16,
                                      NULL);
                DT_ASSERT_EQ(FirstDifference(B.Pictures[f], B.RawPicture, B.PictureSyms,
                                             (int)LineSyms),
                             -1);
            }

            uint16_t* Made = (uint16_t*)B.RawPicture;
            DT_ASSERT(MakePicture(&B, VidStd, 2 * Frame + 1, Made));
            DT_ASSERT_MEM(B.Pictures[1], Made, B.PictureSyms * sizeof(uint16_t));
            DT_ASSERT(MakePicture(&B, VidStd, 2 * Frame, Made));
            DT_ASSERT_MEM(B.Pictures[0] + 2 * LineSyms, Made + 2 * LineSyms,
                          (B.PictureSyms - 2 * LineSyms) * sizeof(uint16_t));
            DT_ASSERT_EQ(B.Pictures[0][B.Sim.HancSyms], 0x200);
            DT_ASSERT_EQ(B.Pictures[0][B.Sim.HancSyms + 1], 0x040);
        }
        DtTest_SetCleanup(NULL, NULL);
        Buffers_Free(&B);
    }
}

// Finds the field of the builder's pictures from their payload ID, as the library's
// view does, also after a merge and a split; and none in a picture without one.
DT_TEST(FindsTheFieldAsTheLibrary)
{
    for (int s = 0; s < 3; s++)
    {
        const int VidStd = g_LevelBStds[s];
        Buffers B;
        DT_ASSERT(Buffers_Init(&B, VidStd));
        DtTest_SetCleanup(Buffers_Free, &B);
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        DtSdiView* View = DtSdiView_Alloc();
        DT_ASSERT(Builder != NULL && View != NULL);

        for (int f = 0; f < 2; f++)
        {
            DT_ASSERT(MakePicture(&B, VidStd, (uint32_t)f, B.Pictures[f]));
            ToRaw(B.Pictures[f], B.PictureSyms, B.RawPicture);
            DT_ASSERT_OK(DtSdiView_SetRawPicture(View, B.RawPicture, B.PictureBytes,
                                                 VidStd, 16, f + 1));
            DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, NULL, NULL, NULL));
            DT_ASSERT_EQ(DtSdiView_FindLevelBField(View), f + 1);
            FromRaw(B.RawPicture, B.PictureSyms, B.Pictures[f]);
            DT_ASSERT_EQ(SimLevelB_Field(&B.Sim, B.Pictures[f]), f + 1);
        }
        SimLevelB_Merge(&B.Sim, B.Pictures[0], B.Pictures[1], B.Frame);
        SimLevelB_Split(&B.Sim, B.Frame, B.Pictures[0], B.Pictures[1]);
        DT_ASSERT_EQ(SimLevelB_Field(&B.Sim, B.Pictures[0]), 1);
        DT_ASSERT_EQ(SimLevelB_Field(&B.Sim, B.Pictures[1]), 2);

        DT_ASSERT(MakePicture(&B, VidStd, 7, B.Pictures[0]));
        DT_ASSERT_EQ(SimLevelB_Field(&B.Sim, B.Pictures[0]), 0);

        DtSdiView_Free(View);
        DtSdiBuilder_Free(Builder);
        DtTest_SetCleanup(NULL, NULL);
        Buffers_Free(&B);
    }
}

// Checks the black picture of each field, in each standard: the emulator's is the one
// the builder makes without an image, which the transmit channel sends, word for word
// but for the CRC words, and both carry the payload ID of their field, which
// SimLevelB_Field and DtSdiView_FindLevelBField find.
DT_TEST(BlackPicturesCarryTheirField)
{
    for (int s = 0; s < 3; s++)
    {
        const int VidStd = g_LevelBStds[s];
        Buffers B;
        DT_ASSERT(Buffers_Init(&B, VidStd));
        DtTest_SetCleanup(Buffers_Free, &B);
        DtSdiView* View = DtSdiView_Alloc();
        DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
        DT_ASSERT(View != NULL && Builder != NULL);
        // The emulator writes the packet's checksum, as a card does for the builder.
        DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Builder, true));
        for (int Field = 1; Field <= 2; Field++)
        {
            SimLevelB_BlackPicture(&B.Sim, Field, B.Pictures[0]);
            memset(B.RawPicture, 0, B.PictureBytes);
            DT_ASSERT_OK(DtSdiView_SetRawPicture(View, B.RawPicture, B.PictureBytes,
                                                 VidStd, 16, Field));
            DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, NULL, NULL, NULL));
            DT_ASSERT_EQ(FirstDifference(B.Pictures[0], B.RawPicture, B.PictureSyms,
                                         B.Sim.LineSyms),
                         -1);
            DT_ASSERT_EQ(SimLevelB_Field(&B.Sim, B.Pictures[0]), Field);
            DT_ASSERT_OK(DtSdiView_SetRawPicture(View, B.RawPicture, B.PictureBytes,
                                                 VidStd, 16, Field == 1 ? 2 : 1));
            DT_ASSERT_EQ(DtSdiView_FindLevelBField(View), Field);
        }
        DtSdiBuilder_Free(Builder);
        DtSdiView_Free(View);
        DtTest_SetCleanup(NULL, NULL);
        Buffers_Free(&B);
    }
}

DT_TEST_MAIN("SimLevelB", DT_RUN(InitTakesLevelBOnly),
             DT_RUN(MergesAndSplitsAsTheLibrary), DT_RUN(FindsTheFieldAsTheLibrary),
             DT_RUN(BlackPicturesCarryTheirField))

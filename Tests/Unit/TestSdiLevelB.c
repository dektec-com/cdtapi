// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestSdiLevelB.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - 3G level B in the builder and the parser
//
// SPDX-License-Identifier: BSD-3-Clause
//
// A 3G level-B frame is one picture in the layout of level A, as the card holds it. The
// tests check that the builder lays out its ancillary data as SMPTE ST 372, ST 352 and
// ST 299-1 have it on the interface, and that the parser reads it back:
// - the lines of link A and link B: in field 1 the even lines are link A's, in field 2
//   the odd ones;
// - a payload ID on each link, byte 1 8A and byte 4 01 on link A or 41 on link B, on
//   interface lines 10 and 572: picture lines 20 and 21 in field 1, 19 and 20 in
//   field 2;
// - the audio on link A only, as on the 1080i interface of the same rate: a control
//   packet on interface line 9 or 571 (picture line 18 or 17), and no data on the line
//   after a switching line, interface line 8 or 570 (picture line 16 or 15);
// - the samples of a picture: its field's part of the interface frame's, which together
//   make the interface's cadence.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"            // Test framework.
#include "Sdi/DtSdiGeometry.h" // The lines of the links.
#include "Sdi/DtSdiLevelB.h"   // Pictures into frames of the interface.
#include "Sdi/DtSdiView.h"     // Pictures of level B in the program's memory.
#include "Video/DtSmpte352.h"  // The payload ID.
#include "cdtapi_sdi.h"        // Public API under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The most samples per channel a picture carries in these tests, and the most packets
// and words the parser lists.
#define LEVELB_MAX_SAMPLES 1024
#define LEVELB_MAX_PACKETS 8192
#define LEVELB_MAX_WORDS (LEVELB_MAX_PACKETS * 32)

// The DIDs of group 1's audio data and control packets in HD.
#define LEVELB_DID_DATA 0xE7
#define LEVELB_DID_CONTROL 0xE3

// A picture's buffers, and what the parser found in it.
typedef struct Picture
{
    uint8_t* Frame;
    size_t Size;
    DtSdiView* View;
    int32_t In[2][LEVELB_MAX_SAMPLES];
    int32_t Out[2][LEVELB_MAX_SAMPLES];
    DtSdiAudio AudioIn;
    DtSdiAudio AudioOut;
    DtSdiAncPacket Packets[LEVELB_MAX_PACKETS];
    uint16_t Words[LEVELB_MAX_WORDS];
    DtSdiAncData Anc;
} Picture;

static Picture* Picture_Alloc(int VidStd)
{
    Picture* P = (Picture*)calloc(1, sizeof(Picture));
    if (P == NULL)
        return NULL;
    DtSdiGeometry Geo;
    if (DtSdiGeometry_Init(&Geo, VidStd) != DTAPI_OK)
        return NULL;
    P->Size = DtSdiFrame_RawSize(&Geo.Layout, 10);
    P->Frame = (uint8_t*)malloc(P->Size);
    P->View = DtSdiView_Alloc();
    if (P->Frame == NULL || P->View == NULL)
        return NULL;
    return P;
}

static void Picture_Free(Picture* P)
{
    if (P == NULL)
        return;
    DtSdiView_Free(P->View);
    free(P->Frame);
    free(P);
}

// Fills channels 1 and 2 of the program's audio with samples that count on from First,
// so that each picture's samples follow on from the last picture's.
static void Picture_SetAudio(Picture* P, int First)
{
    memset(&P->AudioIn, 0, sizeof(P->AudioIn));
    P->AudioIn.Formats[0] = DT_SDI_AUDIO_PCM;
    for (int c = 0; c < 2; c++)
    {
        for (int s = 0; s < LEVELB_MAX_SAMPLES; s++)
            P->In[c][s] = (int32_t)((uint32_t)((First + s) * 2 + c) << 8);
        P->AudioIn.Channels[c].Samples = P->In[c];
        P->AudioIn.Channels[c].NumSamples = LEVELB_MAX_SAMPLES;
    }
}

// Parses the picture: its audio and all its packets, the audio and payload ID included.
static DtapiResult Picture_Parse(Picture* P, DtSdiParser* Parser)
{
    memset(&P->AudioOut, 0, sizeof(P->AudioOut));
    P->AudioOut.Formats[0] = DT_SDI_AUDIO_PCM;
    for (int c = 0; c < 2; c++)
    {
        P->AudioOut.Channels[c].Samples = P->Out[c];
        P->AudioOut.Channels[c].MaxSamples = LEVELB_MAX_SAMPLES;
    }
    memset(&P->Anc, 0, sizeof(P->Anc));
    P->Anc.Packets = P->Packets;
    P->Anc.MaxPackets = LEVELB_MAX_PACKETS;
    P->Anc.Words = P->Words;
    P->Anc.MaxWords = LEVELB_MAX_WORDS;
    return DtSdiParser_Parse(Parser, P->View, NULL, &P->AudioOut, &P->Anc);
}

// Returns how many of the listed packets have DID Did on line Line, or on any line for
// Line 0.
static int CountPackets(const Picture* P, int Did, int Line)
{
    int Count = 0;
    for (int i = 0; i < P->Anc.NumPackets; i++)
        if (P->Packets[i].Did == Did && (Line == 0 || P->Packets[i].Line == Line))
            Count++;
    return Count;
}

// Makes a parser that lists every packet.
static DtSdiParser* ParserOfEverything(void)
{
    DtSdiParser* Parser = DtSdiParser_Alloc();
    if (Parser == NULL)
        return NULL;
    DtSdiAncFilter All;
    memset(&All, 0, sizeof(All));
    All.AnyDid = true;
    All.Space = DT_SDI_ANC_SPACE_BOTH;
    DtSdiParser_SetAncFilter(Parser, &All, 1);
    DtSdiParser_SetAudioChecks(Parser, true);
    return Parser;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Checks the lines of the links against SMPTE ST 372 Figure 2, and the payload ID.
DT_TEST(LinesAndPayloadId)
{
    DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(1, 0), 2);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(1, 0), 0);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(1, 1), 1);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(1, 1), 1);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(1, 41), 21);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(1, 1124), 562);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(1, 1124), 2);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(2, 0), 1);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(2, 0), 563);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(2, 1), 563);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(2, 40), 583);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(2, 41), 2);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(2, 41), 583);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBInterfaceLine(2, 1124), 1125);
    DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(2, 1124), 1);

    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_1080P50B), 0x0100498Au);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_1080P59_94B), 0x01004A8Au);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_1080P60B), 0x01004B8Au);
    DT_ASSERT_EQ(DtSmpte352_Make(DTAPI_VIDSTD_2160P50B), 0u);

    DtSdiGeometry Geo;
    DT_ASSERT_OK(DtSdiGeometry_Init(&Geo, DTAPI_VIDSTD_1080P59_94B));
    DT_ASSERT(Geo.IsLevelB);
    DT_ASSERT_EQ(Geo.InterfaceVidStd, DTAPI_VIDSTD_1080I59_94);
    DT_ASSERT_EQ(DtSdiGeometry_Init(&Geo, DTAPI_VIDSTD_2160P50B), DTAPI_E_INVALID_VIDSTD);
}

// Checks what a view and the builder and parser refuse: a raw frame of level B, which
// is the layout of the line and not yet taken, a field other than 1 or 2, and a picture
// whose field is not known.
DT_TEST(Refusals)
{
    Picture* P = Picture_Alloc(DTAPI_VIDSTD_1080P50B);
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = DtSdiParser_Alloc();
    DT_ASSERT(P != NULL && Builder != NULL && Parser != NULL);
    size_t Size = 0;
    DT_ASSERT_OK(DtSdiView_RawFrameSize(DTAPI_VIDSTD_1080P50B, 10, &Size));
    DT_ASSERT_EQ(Size, 2 * P->Size);
    DT_ASSERT_EQ(
        DtSdiView_SetRawFrame(P->View, P->Frame, P->Size, DTAPI_VIDSTD_1080P50B, 10),
        DTAPI_E_INVALID_SIZE);
    DT_ASSERT_EQ(DtSdiView_SetLevelBField(P->View, 1), DTAPI_E_STATE);
    DT_ASSERT_EQ(
        DtSdiView_SetRawPicture(P->View, P->Frame, P->Size, DTAPI_VIDSTD_1080P50B, 10, 3),
        DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(
        DtSdiView_SetRawPicture(P->View, P->Frame, P->Size, DTAPI_VIDSTD_1080P50, 10, 1),
        DTAPI_E_INVALID_VIDSTD);
    DT_ASSERT_OK(DtSdiView_SetRawPicture(P->View, P->Frame, P->Size,
                                         DTAPI_VIDSTD_1080P50B, 10, 1));
    DT_ASSERT_EQ(DtSdiView_SetLevelBField(P->View, 3), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtSdiView_SetLevelBField(NULL, 1), DTAPI_E_INVALID_ARG);
    P->View->LevelBField = 0;
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, P->View, NULL, NULL, NULL), DTAPI_E_STATE);
    DT_ASSERT_EQ(DtSdiParser_Parse(Parser, P->View, NULL, NULL, NULL), DTAPI_E_STATE);
    DtSdiView* Other = DtSdiView_Alloc();
    DT_ASSERT(Other != NULL);
    uint8_t* Hd = (uint8_t*)malloc(P->Size);
    DT_ASSERT(Hd != NULL);
    DT_ASSERT_OK(DtSdiView_SetRawFrame(Other, Hd, P->Size, DTAPI_VIDSTD_1080P50, 10));
    DT_ASSERT_EQ(DtSdiView_SetLevelBField(Other, 1), DTAPI_E_INVALID_VIDSTD);
    free(Hd);
    DtSdiView_Free(Other);
    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Builder);
    Picture_Free(P);
}

// Builds the two pictures of an interface frame of 1080p50 level B, field 2 first as a
// channel sends them, with audio on channels 1 and 2 and a packet of the program on each
// link. Checks where the parser finds each packet, and that the samples come back.
DT_TEST(Layout)
{
    Picture* P = Picture_Alloc(DTAPI_VIDSTD_1080P50B);
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = ParserOfEverything();
    DT_ASSERT(P != NULL && Builder != NULL && Parser != NULL);
    DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Builder, true));
    int Most = 0;
    DT_ASSERT_OK(DtSdiAudio_MaxSamples(DTAPI_VIDSTD_1080P50B, &Most));

    const uint16_t Words[3] = {0x101, 0x102, 0x203};
    int First = 0;
    int Total = 0;
    for (int k = 0; k < 4; k++)
    {
        const int Field = k % 2 == 0 ? 2 : 1;
        // The program's packets: one on a line of link A, one on a line of link B.
        DtSdiAncPacket Own[2];
        memset(Own, 0, sizeof(Own));
        for (int i = 0; i < 2; i++)
        {
            Own[i].Line = 30 + i;
            Own[i].InHanc = true;
            Own[i].Did = 0x50;
            Own[i].SdidOrDbn = 0x01;
            Own[i].NumWords = 3;
            Own[i].Words = Words;
            Own[i].VirtualInterface = DtSdiGeometry_LevelBLink(Field, 29 + i);
        }
        DtSdiAncData Anc;
        memset(&Anc, 0, sizeof(Anc));
        Anc.Packets = Own;
        Anc.NumPackets = 2;

        int Asked = 0;
        DT_ASSERT_OK(
            DtSdiBuilder_GetNumAudioSamples(Builder, DTAPI_VIDSTD_1080P50B, 0, &Asked));
        DT_ASSERT_OK(DtSdiView_SetRawPicture(P->View, P->Frame, P->Size,
                                             DTAPI_VIDSTD_1080P50B, 10, Field));
        Picture_SetAudio(P, First);
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, P->View, NULL, &P->AudioIn, &Anc));
        DT_ASSERT_EQ(P->AudioIn.NumSamplesUsed, Asked);
        DT_ASSERT(Asked <= Most);
        DT_ASSERT_OK(Picture_Parse(P, Parser));

        // The payload IDs, one on each link.
        const int LineA = Field == 1 ? 20 : 19;
        DT_ASSERT_EQ(CountPackets(P, 0x41, 0), 2);
        for (int i = 0; i < P->Anc.NumPackets; i++)
        {
            const DtSdiAncPacket* Q = &P->Packets[i];
            if (Q->Did != 0x41)
                continue;
            const bool OnA = Q->Line == LineA;
            DT_ASSERT(OnA || Q->Line == LineA + 1);
            DT_ASSERT_EQ(Q->VirtualInterface, OnA ? 1 : 2);
            DT_ASSERT(!Q->OnChroma);
            DT_ASSERT_EQ(Q->Words[0] & 0xFF, 0x8A);
            DT_ASSERT_EQ(Q->Words[1] & 0xFF, 0x49);
            DT_ASSERT_EQ(Q->Words[3] & 0xFF, OnA ? 0x01 : 0x41);
        }

        // The audio: control on interface line 9 or 571, no data on the line after a
        // switching line, and nothing on link B.
        DT_ASSERT_EQ(CountPackets(P, LEVELB_DID_CONTROL, 0), 1);
        DT_ASSERT_EQ(CountPackets(P, LEVELB_DID_CONTROL, Field == 1 ? 18 : 17), 1);
        DT_ASSERT_EQ(CountPackets(P, LEVELB_DID_DATA, Field == 1 ? 16 : 15), 0);
        for (int i = 0; i < P->Anc.NumPackets; i++)
        {
            const DtSdiAncPacket* Q = &P->Packets[i];
            if (Q->Did == LEVELB_DID_DATA || Q->Did == LEVELB_DID_CONTROL)
                DT_ASSERT_EQ(Q->VirtualInterface, 1);
            if (Q->Did == LEVELB_DID_DATA)
                DT_ASSERT_EQ(DtSdiGeometry_LevelBLink(Field, Q->Line - 1), 1);
        }
        DT_ASSERT_EQ(CountPackets(P, LEVELB_DID_DATA, 0), Asked);
        DT_ASSERT_EQ(P->AudioOut.NumPacketErrors[0], 0);
        DT_ASSERT_EQ(P->AudioOut.FrameNumber, Field);
        DT_ASSERT_EQ(P->AudioOut.Channels[0].NumSamples, Asked);
        for (int s = 0; s < Asked; s++)
        {
            DT_ASSERT_EQ(P->Out[0][s], P->In[0][s]);
            DT_ASSERT_EQ(P->Out[1][s], P->In[1][s]);
        }

        // The program's packets, back on their links.
        DT_ASSERT_EQ(CountPackets(P, 0x50, 0), 2);
        for (int i = 0; i < P->Anc.NumPackets; i++)
            if (P->Packets[i].Did == 0x50)
                DT_ASSERT_EQ(P->Packets[i].VirtualInterface,
                             DtSdiGeometry_LevelBLink(Field, P->Packets[i].Line - 1));
        First += Asked;
        Total += Asked;
    }
    // Field 2 and field 1, twice: two interface frames of 1920 samples.
    DT_ASSERT_EQ(Total, 2 * 1920);

    // A packet that names the wrong link for its line is refused.
    DtSdiAncPacket Wrong;
    memset(&Wrong, 0, sizeof(Wrong));
    Wrong.Line = 30;
    Wrong.InHanc = true;
    Wrong.Did = 0x50;
    Wrong.SdidOrDbn = 0x01;
    Wrong.VirtualInterface = 3 - DtSdiGeometry_LevelBLink(1, 29);
    DtSdiAncData Anc;
    memset(&Anc, 0, sizeof(Anc));
    Anc.Packets = &Wrong;
    Anc.NumPackets = 1;
    DT_ASSERT_OK(DtSdiView_SetRawPicture(P->View, P->Frame, P->Size,
                                         DTAPI_VIDSTD_1080P50B, 10, 1));
    DT_ASSERT_EQ(DtSdiBuilder_Build(Builder, P->View, NULL, NULL, &Anc),
                 DTAPI_E_INVALID_LINE);

    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Builder);
    Picture_Free(P);
}

// Follows the cadence of 1080p59.94 level B over two interface cadences: ten pictures
// each, field 2 first. The pictures' samples add up to the interface's 8008 per cadence,
// and the parser gives each picture's place.
DT_TEST(Cadence)
{
    Picture* P = Picture_Alloc(DTAPI_VIDSTD_1080P59_94B);
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = ParserOfEverything();
    DT_ASSERT(P != NULL && Builder != NULL && Parser != NULL);
    DT_ASSERT_OK(DtSdiBuilder_SetChecksums(Builder, true));
    int Most = 0;
    DT_ASSERT_OK(DtSdiAudio_MaxSamples(DTAPI_VIDSTD_1080P59_94B, &Most));

    int First = 0;
    int Total = 0;
    int Place = 2;
    for (int k = 0; k < 20; k++)
    {
        const int Field = k % 2 == 0 ? 2 : 1;
        int Asked = 0;
        DT_ASSERT_OK(DtSdiBuilder_GetNumAudioSamples(Builder, DTAPI_VIDSTD_1080P59_94B, 0,
                                                     &Asked));
        DT_ASSERT(Asked <= Most);
        DT_ASSERT_OK(DtSdiView_SetRawPicture(P->View, P->Frame, P->Size,
                                             DTAPI_VIDSTD_1080P59_94B, 10, Field));
        Picture_SetAudio(P, First);
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, P->View, NULL, &P->AudioIn, NULL));
        DT_ASSERT_EQ(P->AudioIn.NumSamplesUsed, Asked);
        DT_ASSERT_OK(Picture_Parse(P, Parser));
        DT_ASSERT_EQ(P->AudioOut.FrameNumber, Place);
        DT_ASSERT_EQ(P->AudioOut.Channels[1].NumSamples, Asked);
        DT_ASSERT_EQ(P->Out[1][0], P->In[1][0]);
        DT_ASSERT_EQ(P->Out[1][Asked - 1], P->In[1][Asked - 1]);
        First += Asked;
        Total += Asked;
        Place = Place % 10 + 1;
    }
    // Places 2 to 10 and 1, twice: each place of the cadence twice.
    DT_ASSERT_EQ(Total, 2 * 8008);

    // A place of the other field gives way to the next place, of the picture's field.
    P->AudioIn.FrameNumber = 3;
    int Asked = 0;
    DT_ASSERT_OK(
        DtSdiBuilder_GetNumAudioSamples(Builder, DTAPI_VIDSTD_1080P59_94B, 4, &Asked));
    DT_ASSERT_OK(DtSdiView_SetRawPicture(P->View, P->Frame, P->Size,
                                         DTAPI_VIDSTD_1080P59_94B, 10, 2));
    DT_ASSERT_OK(DtSdiBuilder_Build(Builder, P->View, NULL, &P->AudioIn, NULL));
    DT_ASSERT_EQ(P->AudioIn.NumSamplesUsed, Asked);
    DT_ASSERT_OK(Picture_Parse(P, Parser));
    DT_ASSERT_EQ(P->AudioOut.FrameNumber, 4);
    DT_ASSERT_EQ(
        DtSdiBuilder_GetNumAudioSamples(Builder, DTAPI_VIDSTD_1080P59_94B, 11, &Asked),
        DTAPI_E_INVALID_ARG);

    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Builder);
    Picture_Free(P);
}

// Returns symbol Index of interface line Line (from 1) of a 10-bit frame of level B whose
// lines have Words symbols.
static uint16_t FrameWord(const uint8_t* Frame, int Words, int Line, int Index)
{
    DtSdiSymbolPtr Ptr;
    const size_t Bit = ((size_t)(Line - 1) * (size_t)Words + (size_t)Index) * 10;
    Ptr.Byte = Frame + Bit / 8;
    Ptr.Bit = (int)(Bit % 8);
    Ptr.BitsPerSymbol = 10;
    return DtSdiSymbolPtr_Get(&Ptr, 0);
}

// Returns the word of stream Stream (0 for C, 1 for Y) of link Link ('A' or 'B') at word
// Index of that stream, on interface line Line. The interface interleaves link B and link
// A, B first (SMPTE ST 424), and each link C and Y, C first: B-C, A-C, B-Y, A-Y.
static uint16_t LinkWord(const uint8_t* Frame, int Words, int Line, char Link, int Stream,
                         int Index)
{
    return FrameWord(Frame, Words, Line, 4 * Index + 2 * Stream + (Link == 'A' ? 1 : 0));
}

// Puts two pictures of 1080p50 level B into one frame of the interface and checks the
// frame against the standards, not against the converter:
// - the payload ID with byte 4 01 is in link A's Y stream and the one with 41 in link
//   B's, on interface line 10 for field 1 and 572 for field 2 (SMPTE ST 424 order);
// - each link's line numbers are the interface's, and its EAV has the F and V bits of
//   1080i (SMPTE ST 274): F from line 564 on, V on lines 1 to 20, 561 to 583 and 1124
//   and 1125. The second picture starts on line 563, the last line of the interface's
//   first field, as ST 372 Figure 2 maps it;
// - each link's CRCs, worked out bit by bit, cover its line before;
// Then takes the pictures out again: they must equal the pictures put in, except line 1
// of field 1, which the frame does not carry and which comes out as blanking.
DT_TEST(PutAndTakeFields)
{
    const int VidStd = DTAPI_VIDSTD_1080P50B;
    Picture* P[2] = {Picture_Alloc(VidStd), Picture_Alloc(VidStd)};
    Picture* Back = Picture_Alloc(VidStd);
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiLevelB* Converter = (DtSdiLevelB*)malloc(sizeof(DtSdiLevelB));
    DT_ASSERT(P[0] != NULL && P[1] != NULL && Back != NULL && Builder != NULL &&
              Converter != NULL);
    DT_ASSERT_OK(DtSdiLevelB_Init(Converter, VidStd));
    const size_t FrameSize = DtSdiLevelB_FrameSize(&Converter->Geo, 10);
    DT_ASSERT_EQ(FrameSize, 2 * P[0]->Size);
    uint8_t* Frame = (uint8_t*)malloc(FrameSize);
    DT_ASSERT(Frame != NULL);

    for (int f = 0; f < 2; f++)
    {
        DT_ASSERT_OK(DtSdiView_SetRawPicture(P[f]->View, P[f]->Frame, P[f]->Size, VidStd,
                                             10, f + 1));
        Picture_SetAudio(P[f], 0);
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, P[f]->View, NULL, &P[f]->AudioIn, NULL));
    }
    DtSdiLevelB_PutField(Converter, 1, P[0]->Frame, 10, Frame, 10, NULL);
    DtSdiLevelB_PutField(Converter, 2, P[1]->Frame, 10, Frame, 10, NULL);

    // The payload IDs: the packet starts right after EAV, the line number and the CRC,
    // at word 8 of the Y stream; its user data words are words 14 to 17.
    const int Words = 2 * 2 * 2640;
    for (int f = 0; f < 2; f++)
    {
        const int Line = f == 0 ? 10 : 572;
        for (int l = 0; l < 2; l++)
        {
            const char Link = l == 0 ? 'A' : 'B';
            DT_ASSERT_EQ(LinkWord(Frame, Words, Line, Link, 1, 11) & 0xFF, 0x41);
            DT_ASSERT_EQ(LinkWord(Frame, Words, Line, Link, 1, 14) & 0xFF, 0x8A);
            DT_ASSERT_EQ(LinkWord(Frame, Words, Line, Link, 1, 17) & 0xFF,
                         Link == 'A' ? 0x01 : 0x41);
        }
    }

    // Line numbers, F and V, and the CRCs, per link and stream.
    for (int Line = 1; Line <= 1125; Line++)
    {
        const bool Field2 = Line >= 564;
        const bool Vanc = Line <= 20 || (Line >= 561 && Line <= 583) || Line >= 1124;
        for (int l = 0; l < 2; l++)
        {
            const char Link = l == 0 ? 'A' : 'B';
            for (int s = 0; s < 2; s++)
            {
                const uint16_t Xyz = LinkWord(Frame, Words, Line, Link, s, 3);
                DT_ASSERT_EQ((Xyz >> 8) & 1, Field2 ? 1 : 0);
                DT_ASSERT_EQ((Xyz >> 7) & 1, Vanc ? 1 : 0);
                const int Number =
                    (LinkWord(Frame, Words, Line, Link, s, 4) >> 2 & 0x7F) |
                    (LinkWord(Frame, Words, Line, Link, s, 5) >> 2 & 0xF) << 7;
                DT_ASSERT_EQ(Number, Line);
                if (Line == 1)
                    continue;
                uint32_t Crc = 0;
                for (int k = 720; k < 2640; k++)
                    Crc = DtSdiFrame_Crc18(Crc,
                                           LinkWord(Frame, Words, Line - 1, Link, s, k));
                for (int k = 0; k < 6; k++)
                    Crc = DtSdiFrame_Crc18(Crc, LinkWord(Frame, Words, Line, Link, s, k));
                const uint32_t Got =
                    ((uint32_t)LinkWord(Frame, Words, Line, Link, s, 6) & 0x1FFu) |
                    ((uint32_t)LinkWord(Frame, Words, Line, Link, s, 7) & 0x1FFu) << 9;
                if (Got != Crc)
                    DT_FAIL("CRC of line %d, link %c, stream %d: %05X, expected %05X",
                            Line, Link, s, (unsigned)Got, (unsigned)Crc);
            }
        }
    }

    // And back.
    const size_t LineBytes = (size_t)Words / 2 * 10 / 8;
    for (int f = 0; f < 2; f++)
    {
        DtSdiLevelB_TakeField(Converter, f + 1, Frame, 10, Back->Frame, 10, NULL);
        const size_t From = f == 0 ? LineBytes : 0;
        DT_ASSERT_MEM(Back->Frame + From, P[f]->Frame + From, P[f]->Size - From);
    }

    free(Frame);
    DtSdiLevelB_Free(Converter);
    free(Converter);
    DtSdiBuilder_Free(Builder);
    Picture_Free(Back);
    Picture_Free(P[1]);
    Picture_Free(P[0]);
}

// Puts two pictures of 1080p59.94 level B into a frame and takes them out again, on one
// thread and over a pool of four pieces, in 10 and in 16 bits a symbol. The frames and
// the pictures must be the same byte for byte: each band works out the CRCs of the line
// before it from the picture, as the line before it would have left them.
DT_TEST(PoolGivesTheSame)
{
    const int VidStd = DTAPI_VIDSTD_1080P59_94B;
    DtWorkerPool* Pool = DtWorkerPool_Alloc();
    DtJobRunner Runner;
    DtJobRunner_Init(&Runner);
    DT_ASSERT(Pool != NULL);
    DT_ASSERT_OK(DtWorkerPool_StartThreads(Pool, 4));
    DT_ASSERT_OK(DtJobRunner_SetPool(&Runner, Pool, 4));
    DT_ASSERT_EQ(DtJobRunner_NumPieces(&Runner), 4);

    for (int Bits = 10; Bits <= 16; Bits += 6)
    {
        DtSdiLevelB* Converter[2] = {(DtSdiLevelB*)calloc(1, sizeof(DtSdiLevelB)),
                                     (DtSdiLevelB*)calloc(1, sizeof(DtSdiLevelB))};
        DT_ASSERT(Converter[0] != NULL && Converter[1] != NULL);
        for (int c = 0; c < 2; c++)
            DT_ASSERT_OK(DtSdiLevelB_Init(Converter[c], VidStd));
        DtSdiGeometry Geo;
        DT_ASSERT_OK(DtSdiGeometry_Init(&Geo, VidStd));
        const size_t PictureSize = DtSdiFrame_RawSize(&Geo.Layout, Bits);
        const size_t FrameSize = DtSdiLevelB_FrameSize(&Geo, Bits);
        uint8_t* Pictures[2] = {(uint8_t*)malloc(PictureSize),
                                (uint8_t*)malloc(PictureSize)};
        uint8_t* Frames[2] = {(uint8_t*)calloc(FrameSize, 1),
                              (uint8_t*)calloc(FrameSize, 1)};
        uint8_t* Back[2] = {(uint8_t*)malloc(PictureSize), (uint8_t*)malloc(PictureSize)};
        DT_ASSERT(Pictures[0] != NULL && Pictures[1] != NULL && Frames[0] != NULL &&
                  Frames[1] != NULL && Back[0] != NULL && Back[1] != NULL);

        // Pictures of symbols that every line differs in, with the timing of level A.
        for (int p = 0; p < 2; p++)
        {
            DtSdiLevelB_BlackPicture(Converter[0], Pictures[p], Bits);
            for (size_t i = PictureSize / 4; i < PictureSize * 3 / 4; i++)
                Pictures[p][i] = (uint8_t)(i * 7 + (size_t)p * 13);
        }
        // Twice, so that the second frame's line 1 follows the first frame's line 1125.
        for (int Round = 0; Round < 2; Round++)
            for (int c = 0; c < 2; c++)
                for (int f = 1; f <= 2; f++)
                    DtSdiLevelB_PutField(Converter[c], f, Pictures[f - 1], Bits,
                                         Frames[c], Bits, c == 0 ? NULL : &Runner);
        DT_ASSERT_MEM(Frames[1], Frames[0], FrameSize);
        for (int f = 1; f <= 2; f++)
        {
            for (int c = 0; c < 2; c++)
                DtSdiLevelB_TakeField(Converter[c], f, Frames[0], Bits, Back[c], Bits,
                                      c == 0 ? NULL : &Runner);
            DT_ASSERT_MEM(Back[1], Back[0], PictureSize);
        }

        for (int i = 0; i < 2; i++)
        {
            free(Pictures[i]);
            free(Frames[i]);
            free(Back[i]);
            DtSdiLevelB_Free(Converter[i]);
            free(Converter[i]);
        }
    }
    DtJobRunner_Free(&Runner);
    DtWorkerPool_Free(Pool);
}

// Builds and parses a raw frame of 1080p50 level B, a frame of the interface, through a
// view, as a program with an .sdi file does:
// - DtSdiView_RawFrameSize gives twice a frame of level A, and GetActiveLine refuses it;
// - field 1, then field 2 after DtSdiView_SetLevelBField, built with audio, give the
//   same frame as the two pictures built alone and put in by the converter;
// - parsed back, each field gives its payload IDs, its place in the cadence and its
//   samples.
DT_TEST(InterfaceFrameThroughAView)
{
    const int VidStd = DTAPI_VIDSTD_1080P50B;
    size_t Size = 0;
    DT_ASSERT_OK(DtSdiView_RawFrameSize(VidStd, 10, &Size));
    Picture* P[2] = {Picture_Alloc(VidStd), Picture_Alloc(VidStd)};
    uint8_t* Frame = (uint8_t*)malloc(Size);
    uint8_t* Expected = (uint8_t*)malloc(Size);
    DtSdiView* View = DtSdiView_Alloc();
    DtSdiBuilder* Builder = DtSdiBuilder_Alloc();
    DtSdiBuilder* Alone = DtSdiBuilder_Alloc();
    DtSdiParser* Parser = ParserOfEverything();
    DtSdiLevelB* Converter = (DtSdiLevelB*)malloc(sizeof(DtSdiLevelB));
    DT_ASSERT(P[0] != NULL && P[1] != NULL && Frame != NULL && Expected != NULL &&
              View != NULL && Builder != NULL && Alone != NULL && Parser != NULL &&
              Converter != NULL);
    DT_ASSERT_EQ(Size, 2 * P[0]->Size);
    DT_ASSERT_OK(DtSdiLevelB_Init(Converter, VidStd));
    memset(Frame, 0, Size);
    memset(Expected, 0, Size);

    DT_ASSERT_OK(DtSdiView_SetRawFrame(View, Frame, Size, VidStd, 10));
    DtSdiSymbolPtr Line;
    DT_ASSERT_EQ(DtSdiView_GetActiveLine(View, 0, &Line), DTAPI_E_NOT_SUPPORTED);
    int Used[2];
    for (int f = 0; f < 2; f++)
    {
        DT_ASSERT_OK(DtSdiView_SetLevelBField(View, f + 1));
        Picture_SetAudio(P[f], 0);
        DT_ASSERT_OK(DtSdiBuilder_Build(Builder, View, NULL, &P[f]->AudioIn, NULL));
        Used[f] = P[f]->AudioIn.NumSamplesUsed;
        DT_ASSERT_OK(DtSdiView_SetRawPicture(P[f]->View, P[f]->Frame, P[f]->Size, VidStd,
                                             10, f + 1));
        DT_ASSERT_OK(DtSdiBuilder_Build(Alone, P[f]->View, NULL, &P[f]->AudioIn, NULL));
        DtSdiLevelB_PutField(Converter, f + 1, P[f]->Frame, 10, Expected, 10, NULL);
    }
    DT_ASSERT_MEM(Frame, Expected, Size);

    for (int f = 0; f < 2; f++)
    {
        DT_ASSERT_OK(DtSdiView_SetLevelBField(View, f + 1));
        Picture* Q = P[f];
        memset(&Q->AudioOut, 0, sizeof(Q->AudioOut));
        Q->AudioOut.Formats[0] = DT_SDI_AUDIO_PCM;
        for (int c = 0; c < 2; c++)
        {
            Q->AudioOut.Channels[c].Samples = Q->Out[c];
            Q->AudioOut.Channels[c].MaxSamples = LEVELB_MAX_SAMPLES;
        }
        memset(&Q->Anc, 0, sizeof(Q->Anc));
        Q->Anc.Packets = Q->Packets;
        Q->Anc.MaxPackets = LEVELB_MAX_PACKETS;
        Q->Anc.Words = Q->Words;
        Q->Anc.MaxWords = LEVELB_MAX_WORDS;
        DT_ASSERT_OK(DtSdiParser_Parse(Parser, View, NULL, &Q->AudioOut, &Q->Anc));
        DT_ASSERT_EQ(CountPackets(Q, 0x41, 0), 2);
        DT_ASSERT_EQ(Q->AudioOut.Channels[0].NumSamples, Used[f]);
        DT_ASSERT_EQ(Q->AudioOut.FrameNumber % 2 == 1 ? 1 : 2, f + 1);
        DT_ASSERT_EQ(Q->Out[1][Used[f] - 1], Q->In[1][Used[f] - 1]);
        uint32_t PayloadId = 0;
        DT_ASSERT_OK(DtSdiView_GetPayloadId(View, &PayloadId));
        DT_ASSERT_EQ(PayloadId, 0x8A490001u);
    }

    DtSdiLevelB_Free(Converter);
    free(Converter);
    DtSdiParser_Free(Parser);
    DtSdiBuilder_Free(Alone);
    DtSdiBuilder_Free(Builder);
    DtSdiView_Free(View);
    free(Expected);
    free(Frame);
    Picture_Free(P[1]);
    Picture_Free(P[0]);
}

DT_TEST_MAIN("SdiLevelB", DT_RUN(LinesAndPayloadId), DT_RUN(Refusals), DT_RUN(Layout),
             DT_RUN(Cadence), DT_RUN(PutAndTakeFields), DT_RUN(PoolGivesTheSame),
             DT_RUN(InterfaceFrameThroughAView))

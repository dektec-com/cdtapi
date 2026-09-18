// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestAvTime.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Unit tests for the 128-bit arithmetic and the media clock conversions
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The reference values in AvTimeCases.inc come from AvTimeCases.py, which evaluates the
// formulas of DTAPI's TimeConversion with Python's integers of any size.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvTime.h" // Functions under test.
#include "DtTest.h"          // Test framework.
#include "cdtapi_avfifo.h"   // The public timing helpers.

// The reference values.
#include "AvTimeCases.inc"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define COUNT(Array) (sizeof(Array) / sizeof((Array)[0]))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(MulAddDivMatchesBignums)
{
    for (size_t i = 0; i < COUNT(MulAddDivCases); i++)
    {
        const uint64_t* Case = MulAddDivCases[i];
        uint64_t Got = DtAvTime_MulAddDiv(Case[0], Case[1], Case[2], Case[3]);
        if (Got != Case[4])
            DT_FAIL("case %zu: got %" PRIu64 ", expected %" PRIu64, i, Got, Case[4]);
    }
}

DT_TEST(AlignMatchesBignums)
{
    for (size_t i = 0; i < COUNT(AlignCases); i++)
    {
        const uint64_t* Case = AlignCases[i];
        uint64_t Got = DtAvTime_Align(Case[0], (int)Case[1], (int)Case[2]);
        if (Got != Case[3])
            DT_FAIL("case %zu: got %" PRIu64 ", expected %" PRIu64, i, Got, Case[3]);
    }
}

DT_TEST(Tod2RtpMatchesBignums)
{
    for (size_t i = 0; i < COUNT(Tod2RtpCases); i++)
    {
        const uint64_t* Case = Tod2RtpCases[i];
        uint32_t Got = DtAvTime_Tod2Rtp((int)Case[0], Case[1]);
        if (Got != Case[2])
            DT_FAIL("case %zu: got %u, expected %" PRIu64, i, Got, Case[2]);
    }
}

DT_TEST(Rtp2TodMatchesBignums)
{
    for (size_t i = 0; i < COUNT(Rtp2TodCases); i++)
    {
        const uint64_t* Case = Rtp2TodCases[i];
        uint64_t Got = DtAvTime_Rtp2Tod((int)Case[0], (uint32_t)Case[1], Case[2]);
        if (Got != Case[3])
            DT_FAIL("case %zu: got %" PRIu64 ", expected %" PRIu64, i, Got, Case[3]);
    }
}

// A frame on the 59.94 Hz grid converts to its own timestamp, the timestamps advance by
// 1501 and 1502 in turn, and they convert back to the time of their tick: the frame's
// time or up to half a tick, 5,556 ns, before it, truncated.
DT_TEST(FractionalGridRoundTrips)
{
    const FrameRate Rate = {60000, 1001};
    DtTimeOfDay Start = {1800000000, 123456789};
    uint64_t First = DtAvTime_ToNs(&Start);
    uint32_t PrevRtp = 0;

    for (int Frame = 0; Frame < 1000; Frame++)
    {
        uint64_t Ns = First + DtAvTime_MulAddDiv((uint64_t)Frame, 1001 * DT_AV_NS_PER_SEC,
                                                 0, 60000);
        DtTimeOfDay ToD = DtAvTime_FromNs(Ns);
        DtTimeOfDay Grid = Tod2Grid_Video(&ToD, &Rate);
        uint32_t Rtp = Tod2Rtp_Video(&Grid);
        if (Frame > 0 && Rtp - PrevRtp != 1501 && Rtp - PrevRtp != 1502)
            DT_FAIL("frame %d: step %u", Frame, Rtp - PrevRtp);
        DtTimeOfDay Back = Rtp2Tod_Video(Rtp, &ToD);
        uint64_t BackNs = DtAvTime_ToNs(&Back);
        uint64_t GridNs = DtAvTime_ToNs(&Grid);
        DT_ASSERT(BackNs <= GridNs && GridNs - BackNs <= 5556);
        DT_ASSERT_EQ(Tod2Rtp_Video(&Back), Rtp);
        DtTimeOfDay Again = Tod2Grid_Video(&Grid, &Rate);
        DT_ASSERT_EQ(DtAvTime_ToNs(&Again), DtAvTime_ToNs(&Grid));
        PrevRtp = Rtp;
    }
}

// Audio samples at 48 kHz: on the grid every 20,833.3 ns, timestamps one apart, and back.
DT_TEST(AudioGridRoundTrips)
{
    DtTimeOfDay Start = {1726570000, 999990000};
    DtTimeOfDay First = Tod2Grid_Audio(&Start, 48000);
    uint64_t FirstNs = DtAvTime_ToNs(&First);
    uint32_t FirstRtp = Tod2Rtp_Audio(&First, 48000);

    for (uint64_t Sample = 0; Sample < 100000; Sample += 997)
    {
        uint64_t Ns =
            FirstNs + DtAvTime_MulAddDiv(Sample, DT_AV_NS_PER_SEC, 24000, 48000);
        DtTimeOfDay ToD = DtAvTime_FromNs(Ns);
        uint32_t Rtp = Tod2Rtp_Audio(&ToD, 48000);
        DT_ASSERT_EQ(Rtp, (uint32_t)(FirstRtp + Sample));
        DtTimeOfDay Back = Rtp2Tod_Audio(Rtp, &Start, 48000);
        uint64_t BackNs = DtAvTime_ToNs(&Back);
        DT_ASSERT(BackNs + 1 >= Ns && BackNs <= Ns + 1);
    }
}

// An approximate time far from the timestamp's own time picks the nearest wrap.
DT_TEST(NearestWrap)
{
    DtTimeOfDay ToD = {1800000000, 0};
    uint32_t Rtp = Tod2Rtp_Video(&ToD);
    uint64_t Ns = DtAvTime_ToNs(&ToD);
    uint64_t Wrap = DtAvTime_MulAddDiv(UINT64_C(1) << 32, DT_AV_NS_PER_SEC, 0, 90000);

    DtTimeOfDay Later = DtAvTime_FromNs(Ns + Wrap / 2 - DT_AV_NS_PER_SEC);
    DtTimeOfDay Earlier = DtAvTime_FromNs(Ns - Wrap / 2 + DT_AV_NS_PER_SEC);
    DtTimeOfDay FromLater = Rtp2Tod_Video(Rtp, &Later);
    DtTimeOfDay FromEarlier = Rtp2Tod_Video(Rtp, &Earlier);
    DT_ASSERT_EQ(DtAvTime_ToNs(&FromLater), Ns);
    DT_ASSERT_EQ(DtAvTime_ToNs(&FromEarlier), Ns);
}

DT_TEST(InvalidRatesAndNulls)
{
    DtTimeOfDay ToD = {12, 34};
    const FrameRate Zero = {0, 1};

    DtTimeOfDay Same = Tod2Grid_Video(&ToD, &Zero);
    DT_ASSERT_EQ(DtAvTime_ToNs(&Same), DtAvTime_ToNs(&ToD));
    Same = Tod2Grid_Audio(&ToD, -1);
    DT_ASSERT_EQ(DtAvTime_ToNs(&Same), DtAvTime_ToNs(&ToD));
    DT_ASSERT_EQ(Tod2Rtp_Audio(&ToD, 0), 0);
    DT_ASSERT_EQ(Tod2Rtp_Video(NULL), 0);
    DtTimeOfDay Null = Rtp2Tod_Video(5, NULL);
    DT_ASSERT_EQ(DtAvTime_ToNs(&Null), 0);
}

DT_TEST_MAIN("AvTime", DT_RUN(MulAddDivMatchesBignums), DT_RUN(AlignMatchesBignums),
             DT_RUN(Tod2RtpMatchesBignums), DT_RUN(Rtp2TodMatchesBignums),
             DT_RUN(FractionalGridRoundTrips), DT_RUN(AudioGridRoundTrips),
             DT_RUN(NearestWrap), DT_RUN(InvalidRatesAndNulls))

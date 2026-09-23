// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestTsTrp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The transparent packets a DtPcie card receives ASI into
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Packets are built here field by field, in the layout a DTA-2178 was seen to write.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"     // Test framework.
#include "Ts/DtTsTrp.h" // Module under test.
#include "cdtapi.h"     // Receive modes and results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Packets +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Build -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A transparent packet: time Seconds.Nanoseconds, payload bytes Seed, Seed + 1 and so on,
// Valid of them valid, with the packet-sync bit when Synced, and sequence number Seq.
//
static void Build(uint8_t* P, uint32_t Seconds, uint32_t Nanoseconds, int Seed, int Valid,
                  bool Synced, int Seq)
{
    for (int i = 0; i < 4; i++)
    {
        P[i] = (uint8_t)(Seconds >> (8 * i));
        P[4 + i] = (uint8_t)(Nanoseconds >> (8 * i));
    }
    for (int i = 0; i < 204; i++)
        P[8 + i] = (uint8_t)(Seed + i);
    P[8] = 0x47;
    P[212] = Synced ? 0x58 : 0x50;
    P[213] = (uint8_t)Valid;
    P[214] = (uint8_t)Seq;
    P[215] = (uint8_t)(Seq >> 8);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The modes an input channel's SetRxMode takes.
DT_TEST(Modes)
{
    DT_ASSERT_OK(DtTsTrp_CheckMode(DTAPI_RXMODE_ST188));
    DT_ASSERT_OK(DtTsTrp_CheckMode(DTAPI_RXMODE_ST204 | DTAPI_RXMODE_TIMESTAMP32));
    DT_ASSERT_OK(DtTsTrp_CheckMode(DTAPI_RXMODE_STMP2 | DTAPI_RXMODE_TIMESTAMP_TOD));
    DT_ASSERT_OK(DtTsTrp_CheckMode(DTAPI_RXMODE_STRAW));
    DT_ASSERT_OK(DtTsTrp_CheckMode(DTAPI_RXMODE_STTRP));
    DT_ASSERT_EQ(DtTsTrp_CheckMode(DTAPI_RXMODE_STL3), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtTsTrp_CheckMode(DTAPI_RXMODE_RAWASI), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtTsTrp_CheckMode(DTAPI_RXMODE_SDI_FULL), DTAPI_E_INVALID_MODE);
    DT_ASSERT_EQ(DtTsTrp_CheckMode(DTAPI_RXMODE_ST188 | DTAPI_RXMODE_TIMESTAMP64),
                 DTAPI_E_INVALID_MODE);
}

// What each mode gives of a 188- and a 204-byte packet.
DT_TEST(OutputPerMode)
{
    uint8_t P188[DT_TRP_SIZE], P204[DT_TRP_SIZE], Out[DT_TRP_MAX_OUTPUT];
    Build(P188, 100, 500, 0x40, 188, true, 1);
    Build(P204, 100, 500, 0x40, 204, true, 2);
    DtTsTrp Trp;
    memset(&Trp, 0, sizeof(Trp));

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST188);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), 188);
    DT_ASSERT_MEM(Out, P188 + 8, 188);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P204, Out), 188);
    DT_ASSERT_MEM(Out, P204 + 8, 188);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST204);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), 204);
    DT_ASSERT_MEM(Out, P188 + 8, 188);
    for (int i = 188; i < 204; i++)
        DT_ASSERT_EQ(Out[i], 0);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P204, Out), 204);
    DT_ASSERT_MEM(Out, P204 + 8, 204);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STMP2);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), 188);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P204, Out), 204);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STTRP);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), 208);
    DT_ASSERT_MEM(Out, P188 + 8, 208);

    // Time stamps: 32-bit ticks of 54 MHz, or the card's time of day itself.
    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST188 | DTAPI_RXMODE_TIMESTAMP32);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), 192);
    uint32_t Ticks = (uint32_t)Out[0] | (uint32_t)Out[1] << 8 | (uint32_t)Out[2] << 16 |
                     (uint32_t)Out[3] << 24;
    const uint64_t Want = 100ull * 54000000u + 500u * 54u / 1000u;
    DT_ASSERT_EQ(Ticks, (uint32_t)(Want & 0xFFFFFFFFu));
    DT_ASSERT_MEM(Out + 4, P188 + 8, 188);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST204 | DTAPI_RXMODE_TIMESTAMP_TOD);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), 8 + 188 + 16);
    DT_ASSERT_MEM(Out, P188, 8 + 188);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STTRP | DTAPI_RXMODE_TIMESTAMP_TOD);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P188, Out), DT_TRP_MAX_OUTPUT);
    DT_ASSERT_MEM(Out, P188, DT_TRP_MAX_OUTPUT);

    int Flags, Latched;
    DtTsTrp_GetFlags(&Trp, &Flags, &Latched);
    DT_ASSERT_EQ(Latched, 0);
}

// A packet without its packet-sync bit sets the synchronisation error and is dropped,
// but in the raw and transparent modes; the raw mode does not set the error.
DT_TEST(PacketsWithoutSync)
{
    uint8_t P[DT_TRP_SIZE], Out[DT_TRP_MAX_OUTPUT];
    Build(P, 1, 2, 0x10, 204, false, 7);
    DtTsTrp Trp;
    memset(&Trp, 0, sizeof(Trp));
    int Flags, Latched;

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST188);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), 0);
    DtTsTrp_GetFlags(&Trp, &Flags, &Latched);
    DT_ASSERT_EQ(Flags, DTAPI_RX_SYNC_ERR);
    DT_ASSERT_EQ(Latched, DTAPI_RX_SYNC_ERR);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STTRP);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), 208);

    uint8_t Good[DT_TRP_SIZE];
    Build(Good, 1, 2, 0x10, 188, true, 8);
    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST188);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, Good, Out), 188);
    DtTsTrp_GetFlags(&Trp, &Flags, &Latched);
    DT_ASSERT_EQ(Flags, 0);
    DT_ASSERT_EQ(Latched, DTAPI_RX_SYNC_ERR);
    DtTsTrp_ClearFlags(&Trp, DTAPI_RX_SYNC_ERR);
    DtTsTrp_GetFlags(&Trp, &Flags, &Latched);
    DT_ASSERT_EQ(Latched, 0);

    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STRAW);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), 204);
    DtTsTrp_GetFlags(&Trp, &Flags, &Latched);
    DT_ASSERT_EQ(Latched, 0);
}

// A wrong sync nibble, or a valid count the mode does not accept, is no packet in sync.
DT_TEST(BytesThatAreNoPacket)
{
    uint8_t P[DT_TRP_SIZE], Out[DT_TRP_MAX_OUTPUT];
    DtTsTrp Trp;
    memset(&Trp, 0, sizeof(Trp));

    Build(P, 1, 2, 0, 188, true, 0);
    P[212] = 0x48;
    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST188);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), -1);

    Build(P, 1, 2, 0, 100, true, 0);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), -1);
    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STRAW);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), 100);
    Build(P, 1, 2, 0, 205, true, 0);
    DT_ASSERT_EQ(DtTsTrp_Convert(&Trp, P, Out), -1);
}

// The search finds three packets in a row at any offset and gives where a whole packet
// starts; a gap in the sequence numbers is no stream.
DT_TEST(FindingTheStream)
{
    uint8_t Buf[8 * DT_TRP_SIZE];
    DtTsTrp Trp;
    memset(&Trp, 0, sizeof(Trp));
    DtTsTrp_Start(&Trp, DTAPI_RXMODE_ST188);

    for (size_t Skew = 0; Skew < DT_TRP_SIZE; Skew += 37)
    {
        memset(Buf, 0xAA, sizeof(Buf));
        for (int n = 0; n < 6; n++)
            Build(Buf + Skew + (size_t)n * DT_TRP_SIZE, 0, 0, n, 188, true, 65534 + n);
        size_t Offset = 9999;
        DT_ASSERT(DtTsTrp_FindSync(&Trp, Buf, sizeof(Buf), &Offset));
        DT_ASSERT_EQ(Offset % DT_TRP_SIZE, Skew);
        DT_ASSERT(Offset == Skew || Offset == Skew + DT_TRP_SIZE);
    }

    // Too few bytes, and a sequence that jumps.
    size_t Offset = 0;
    DT_ASSERT(!DtTsTrp_FindSync(&Trp, Buf, 3 * DT_TRP_SIZE - 1, &Offset));
    memset(Buf, 0xAA, sizeof(Buf));
    for (int n = 0; n < 6; n++)
        Build(Buf + (size_t)n * DT_TRP_SIZE, 0, 0, n, 188, true, 2 * n);
    DT_ASSERT(!DtTsTrp_FindSync(&Trp, Buf, sizeof(Buf), &Offset));

    // A valid count only the raw modes take counts there.
    for (int n = 0; n < 6; n++)
        Build(Buf + (size_t)n * DT_TRP_SIZE, 0, 0, n, 150, true, n);
    DT_ASSERT(!DtTsTrp_FindSync(&Trp, Buf, sizeof(Buf), &Offset));
    DtTsTrp_Start(&Trp, DTAPI_RXMODE_STRAW);
    DT_ASSERT(DtTsTrp_FindSync(&Trp, Buf, sizeof(Buf), &Offset));
    DT_ASSERT_EQ(Offset % DT_TRP_SIZE, 0u);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("TsTrp", DT_RUN(Modes), DT_RUN(OutputPerMode), DT_RUN(PacketsWithoutSync),
             DT_RUN(BytesThatAreNoPacket), DT_RUN(FindingTheStream))

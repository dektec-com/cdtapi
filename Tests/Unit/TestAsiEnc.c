// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestAsiEnc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - A transport stream coded as ASI symbols
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The 8b/10b table is checked against the code as IEEE 802.3 clause 36 and the ASI
// standard (EN 50083-9) define it, built here from its 5b/6b and 3b/4b sub-blocks. What
// the encoder writes is decoded again with that code, and the rate the symbols carry is
// measured from them.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// CDTAPI includes
#include "DtTest.h"      // Test framework.
#include "Ts/DtAsiEnc.h" // Module under test.
#include "cdtapi.h"      // Transmit modes and results.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The 8b/10b code +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The 5b/6b sub-block abcdei of EDCBA for a negative running disparity; the one for a
// positive running disparity is its complement where it is not balanced, D.07 being the
// balanced exception that has one for each.
static const char* g_6b[32] = {
    "100111", "011101", "101101", "110001", "110101", "101001", "011001", "111000",
    "111001", "100101", "010101", "110100", "001101", "101100", "011100", "010111",
    "011011", "100011", "010011", "110010", "001011", "101010", "011010", "111010",
    "110011", "100110", "010110", "110110", "001110", "101110", "011110", "101011",
};

// The 3b/4b sub-block fghj of HGF for a negative running disparity, and the alternative
// A7 for 7.
static const char* g_4b[8] = {"1011", "1001", "0101", "1100",
                              "1101", "1010", "0110", "1110"};
static const char* g_A7 = "0111";

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ones -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int Ones(const char* Bits)
{
    int n = 0;
    for (; *Bits != '\0'; Bits++)
        n += *Bits == '1';
    return n;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SubBlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Appends Bits to *Code from bit *At on, the first bit in the least significant place,
// complemented when Invert; returns the number of ones appended.
//
static int SubBlock(const char* Bits, bool Invert, uint16_t* Code, int* At)
{
    int OnesOut = 0;
    for (; *Bits != '\0'; Bits++, (*At)++)
    {
        bool One = (*Bits == '1') != Invert;
        if (One)
        {
            *Code |= (uint16_t)(1u << *At);
            OnesOut++;
        }
    }
    return OnesOut;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Encode8b10b -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The code of Byte for running disparity Rd, 0 negative and 1 positive, built from the
// sub-blocks; *NextRd receives the running disparity after it.
//
static uint16_t Encode8b10b(uint8_t Byte, int Rd, int* NextRd)
{
    const int X = Byte & 0x1F, Y = Byte >> 5;
    uint16_t Code = 0;
    int At = 0;

    // The 6b sub-block: complemented at a positive disparity where not balanced, and
    // D.07's two forms by disparity.
    const char* Six = g_6b[X];
    bool Balanced6 = Ones(Six) == 3;
    bool Invert6 = Rd == 1 && (!Balanced6 || X == 7);
    int Ones6 = SubBlock(Six, Invert6, &Code, &At);
    if (Ones6 != 3)
        Rd = Ones6 > 3 ? 1 : 0;

    // The 4b sub-block, with A7 where P7 would make five equal bits in a row.
    const char* Four = g_4b[Y];
    if (Y == 7 && ((Rd == 0 && (X == 17 || X == 18 || X == 20)) ||
                   (Rd == 1 && (X == 11 || X == 13 || X == 14))))
    {
        Four = g_A7;
    }
    bool Balanced4 = Ones(Four) == 2;
    bool Invert4 = Rd == 1 && (!Balanced4 || Y == 3);
    int Ones4 = SubBlock(Four, Invert4, &Code, &At);
    if (Ones4 != 2)
        Rd = Ones4 > 2 ? 1 : 0;

    *NextRd = Rd;
    return Code;
}

// The decoder: for every 10-bit code, the byte it stands for, or -1.
static int g_Decode[1024];

static void BuildDecoder(void)
{
    for (int i = 0; i < 1024; i++)
        g_Decode[i] = -1;
    for (int Byte = 0; Byte < 256; Byte++)
    {
        for (int Rd = 0; Rd < 2; Rd++)
        {
            int Next;
            g_Decode[Encode8b10b((uint8_t)Byte, Rd, &Next)] = Byte;
        }
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Decoding +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// What a stream of symbols decodes to: the data bytes, the K28.5 among them, and whether
// every symbol was right for the running disparity it came at.
typedef struct Decoded
{
    uint8_t* Bytes;
    size_t NumBytes;
    size_t NumK28;
    size_t NumSyms;
    bool Valid;
    int Rd;
} Decoded;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Decode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Decodes Count symbols on from D's running disparity. Positions, the symbol at which
// each data byte came, may be NULL.
//
static void Decode(Decoded* D, const uint16_t* Syms, size_t Count, size_t* Positions)
{
    for (size_t i = 0; i < Count; i++)
    {
        uint16_t S = Syms[i];
        if (S == (D->Rd == 0 ? DT_ASI_K28_5_RDNEG : DT_ASI_K28_5_RDPOS))
        {
            D->NumK28++;
            D->Rd ^= 1;
        }
        else
        {
            int Byte = S < 1024 ? g_Decode[S] : -1;
            int Next = 0;
            if (Byte < 0 || Encode8b10b((uint8_t)Byte, D->Rd, &Next) != S)
                D->Valid = false;
            else
            {
                if (Positions != NULL)
                    Positions[D->NumBytes] = D->NumSyms + i;
                D->Bytes[D->NumBytes++] = (uint8_t)Byte;
                D->Rd = Next;
            }
        }
    }
    D->NumSyms += Count;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NewDecoded -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static Decoded NewDecoded(size_t MaxBytes)
{
    Decoded D = {(uint8_t*)malloc(MaxBytes), 0, 0, 0, true, 1};
    return D;
}

// A transport stream of Count numbered packets of Size bytes.
static uint8_t* Packets(int Count, int Size)
{
    uint8_t* Ts = (uint8_t*)malloc((size_t)Count * (size_t)Size);
    for (int n = 0; n < Count; n++)
    {
        uint8_t* P = Ts + (size_t)n * (size_t)Size;
        P[0] = 0x47;
        P[1] = (uint8_t)(n >> 8);
        P[2] = (uint8_t)n;
        P[3] = 0x10;
        for (int i = 4; i < Size; i++)
            P[i] = (uint8_t)(n * 31 + i);
    }
    return Ts;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EncodeAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Encodes all of Ts into a new buffer of symbols, in pieces of InStep bytes and room
// for OutStep symbols at a time; *NumSyms receives the symbols written.
//
static uint16_t* EncodeAll(DtAsiEnc* Enc, const uint8_t* Ts, size_t Size, size_t InStep,
                           size_t OutStep, size_t* NumSyms)
{
    size_t Room = Size * 2 + 65536;
    while ((double)Room <
           (double)Size * 1.1 * (double)Enc->Available / (double)Enc->Needed + 64)
        Room *= 2;
    uint16_t* Syms = (uint16_t*)malloc(Room * sizeof(uint16_t));
    size_t In = 0, Out = 0;
    while (In < Size && Out < Room)
    {
        size_t InNow = Size - In < InStep ? Size - In : InStep;
        size_t OutNow = Room - Out < OutStep ? Room - Out : OutStep;
        size_t Taken = 0, Written = 0;
        DtAsiEnc_Convert(Enc, Ts + In, InNow, Syms + Out, OutNow, &Taken, &Written);
        In += Taken;
        Out += Written;
        if (Taken == 0 && Written == 0)
            break;
    }
    *NumSyms = Out;
    return Syms;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// DTAPI's table is the 8b/10b code, bit for bit, for both running disparities, and so is
// K28.5.
DT_TEST(TableIsThe8b10bCode)
{
    int Differences = 0;
    for (int Byte = 0; Byte < 256; Byte++)
    {
        for (int Rd = 0; Rd < 2; Rd++)
        {
            int WantRd = -1, GotRd = -1;
            uint16_t Want = Encode8b10b((uint8_t)Byte, Rd, &WantRd);
            uint16_t Got = DtAsiEnc_Code((uint8_t)Byte, Rd, &GotRd);
            if (Want != Got || WantRd != GotRd)
            {
                if (Differences++ < 5)
                    printf("    byte %02X rd %d: table %03X/%d, code %03X/%d\n", Byte, Rd,
                           Got, GotRd, Want, WantRd);
            }

            // Every code has four to six ones and keeps the disparity bounded.
            int One = 0;
            for (int b = 0; b < 10; b++)
                One += (Got >> b) & 1;
            DT_ASSERT(One >= 4 && One <= 6);
            DT_ASSERT(Got < 1024);
            DT_ASSERT(Rd == 0 ? One >= 5 : One <= 5);
        }
    }
    DT_ASSERT_EQ(Differences, 0);

    uint16_t K28 = 0;
    int At = 0;
    SubBlock("001111", false, &K28, &At);
    SubBlock("1010", false, &K28, &At);
    DT_ASSERT_EQ(K28, DT_ASI_K28_5_RDNEG);
    DT_ASSERT_EQ((uint16_t)(~K28 & 0x3FF), DT_ASI_K28_5_RDPOS);
}

// The modes and rates DTAPI accepts, and what it refuses without changing anything.
DT_TEST(ModesAndRates)
{
    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_EQ(Enc.Rate, 10000000);
    DT_ASSERT_EQ(Enc.OutSize, 188);
    DT_ASSERT(Enc.Burst);
    DT_ASSERT_EQ(Enc.K28BeforePacket, 2);

    DT_ASSERT_EQ(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_RAWASI), DTAPI_E_NOT_IMPLEMENTED);
    DT_ASSERT_EQ(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_192), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_SDI_FULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT(Enc.Burst);

    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_ADD16));
    DT_ASSERT_EQ(Enc.InSize, 188);
    DT_ASSERT_EQ(Enc.OutSize, 204);
    DT_ASSERT(!Enc.Burst);
    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_MIN16 | DTAPI_TXMODE_BURST));
    DT_ASSERT_EQ(Enc.InSize, 204);
    DT_ASSERT_EQ(Enc.InUsed, 188);
    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_RAW));
    DT_ASSERT(Enc.Raw);
    DT_ASSERT_EQ(Enc.K28BeforePacket, 0);

    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_188 | DTAPI_TXMODE_BURST));
    DT_ASSERT_EQ(DtAsiEnc_SetRate(&Enc, 0), DTAPI_E_INVALID_RATE);
    DT_ASSERT_EQ(DtAsiEnc_SetRate(&Enc, -5), DTAPI_E_INVALID_RATE);
    DT_ASSERT_EQ(DtAsiEnc_SetRate(&Enc, 216000001), DTAPI_E_INVALID_RATE);
    DT_ASSERT_EQ(Enc.Rate, 10000000);

    // The line holds 216 Mbit/s of 188-byte packets and nothing else; two K28.5 before
    // each packet fit up to 213.7 Mbit/s, one up to 214.9.
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 216000000));
    DT_ASSERT_EQ(Enc.K28BeforePacket, 0);
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 214000000));
    DT_ASSERT_EQ(Enc.K28BeforePacket, 1);
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 213000000));
    DT_ASSERT_EQ(Enc.K28BeforePacket, 2);

    // 204-byte packets at a rate counted in 188-byte packets: the mode is taken, and a
    // rate that does not fit it is refused when the stream starts, as in DTAPI.
    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_204));
    DT_ASSERT_EQ(DtAsiEnc_Start(&Enc), DTAPI_E_INVALID_RATE);
    DT_ASSERT_EQ(DtAsiEnc_SetRate(&Enc, 200000000), DTAPI_E_INVALID_RATE);
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 190000000));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
}

// Every packet comes out as it went in, whole and in order, in burst and normal mode, in
// pieces of every size, with the rate asked for.
DT_TEST(PacketsAndRate)
{
    BuildDecoder();
    const int Count = 40;
    uint8_t* Ts = Packets(Count, 188);
    const int64_t Rates[] = {1000000, 10000000, 38000000, 100000000, 213000000};
    const size_t Steps[][2] = {
        {(size_t)188 * (size_t)Count, 1u << 24}, {1, 7}, {187, 1000}, {5, 3}};

    for (int Mode = 0; Mode < 2; Mode++)
    {
        for (size_t r = 0; r < sizeof(Rates) / sizeof(Rates[0]); r++)
        {
            for (size_t s = 0; s < sizeof(Steps) / sizeof(Steps[0]); s++)
            {
                DtAsiEnc Enc;
                DtAsiEnc_Init(&Enc);
                DT_ASSERT_OK(DtAsiEnc_SetTxMode(
                    &Enc, DTAPI_TXMODE_188 | (Mode == 0 ? DTAPI_TXMODE_BURST : 0)));
                DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, Rates[r]));
                DT_ASSERT_OK(DtAsiEnc_Start(&Enc));

                size_t NumSyms = 0;
                uint16_t* Syms = EncodeAll(&Enc, Ts, (size_t)Count * 188, Steps[s][0],
                                           Steps[s][1], &NumSyms);
                Decoded D = NewDecoded((size_t)Count * 188);
                size_t* Positions = (size_t*)malloc((size_t)Count * 188 * sizeof(size_t));
                Decode(&D, Syms, NumSyms, Positions);

                DT_ASSERT(D.Valid);
                DT_ASSERT_EQ(D.NumBytes, (size_t)Count * 188);
                DT_ASSERT_MEM(D.Bytes, Ts, (size_t)Count * 188);

                // The rate from the symbols between the first bytes of the second and the
                // last packet.
                double Symbols =
                    (double)(Positions[(size_t)(Count - 1) * 188] - Positions[188]);
                double Rate =
                    (double)(Count - 2) * 188 * 8 * DT_ASI_SYMBOL_RATE / Symbols;
                double Error = (Rate - (double)Rates[r]) / (double)Rates[r];
                if (Error < -0.001 || Error > 0.001)
                {
                    printf("    mode %d rate %lld step %zu: measured %.0f\n", Mode,
                           (long long)Rates[r], s, Rate);
                    (*DtFailures)++;
                }

                // In burst mode a packet's bytes are back to back.
                if (Mode == 0)
                {
                    for (int n = 0; n < Count; n++)
                    {
                        size_t First = Positions[(size_t)n * 188];
                        DT_ASSERT_EQ(Positions[(size_t)n * 188 + 187], First + 187);
                    }
                }
                free(Positions);
                free(D.Bytes);
                free(Syms);
            }
        }
    }
    free(Ts);
}

// Before each packet go two K28.5, and at the highest rate none; RAW sends no K28.5
// before anything and checks no sync byte.
DT_TEST(CommasBeforePackets)
{
    BuildDecoder();
    uint8_t* Ts = Packets(4, 188);
    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 200000000));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    size_t NumSyms;
    uint16_t* Syms = EncodeAll(&Enc, Ts, 4 * 188, 4 * 188, 1u << 20, &NumSyms);
    Decoded D = NewDecoded(4 * 188);
    size_t Positions[4 * 188];
    Decode(&D, Syms, NumSyms, Positions);
    DT_ASSERT(D.Valid);
    for (int n = 1; n < 4; n++)
    {
        size_t First = Positions[n * 188];
        DT_ASSERT(Syms[First - 1] == DT_ASI_K28_5_RDNEG ||
                  Syms[First - 1] == DT_ASI_K28_5_RDPOS);
        DT_ASSERT(Syms[First - 2] == DT_ASI_K28_5_RDNEG ||
                  Syms[First - 2] == DT_ASI_K28_5_RDPOS);
    }
    free(Syms);

    // RAW: bytes without a sync byte go out as they are.
    uint8_t Raw[376];
    for (int i = 0; i < 376; i++)
        Raw[i] = (uint8_t)(i * 7 + 1);
    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_RAW | DTAPI_TXMODE_BURST));
    DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 216000000));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    Syms = EncodeAll(&Enc, Raw, sizeof(Raw), sizeof(Raw), 1u << 20, &NumSyms);
    D.NumBytes = D.NumK28 = D.NumSyms = 0;
    D.Rd = 1;
    Decode(&D, Syms, NumSyms, NULL);
    DT_ASSERT(D.Valid);
    DT_ASSERT_EQ(D.NumBytes, sizeof(Raw));
    DT_ASSERT_MEM(D.Bytes, Raw, sizeof(Raw));
    DT_ASSERT_EQ(D.NumK28, 0u);
    int Flags, Latched;
    DtAsiEnc_GetFlags(&Enc, &Flags, &Latched);
    DT_ASSERT_EQ(Latched, 0);
    free(Syms);
    free(D.Bytes);
    free(Ts);
}

// ADD16 adds 16 zeros to each 188-byte packet, in burst and in normal mode, also when the
// room for symbols runs out among the zeros; the zeros of the last packet wait for more
// input, as in DTAPI. MIN16 drops the last 16 bytes of each 204.
DT_TEST(SixteenBytesMoreOrLess)
{
    BuildDecoder();
    uint8_t* Ts188 = Packets(4, 188);
    uint8_t* Ts204 = Packets(3, 204);
    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    size_t NumSyms;
    Decoded D = NewDecoded(4 * 204);
    uint16_t* Syms;

    const size_t OutSteps[] = {50, 7, 1};
    for (int Mode = 0; Mode < 2; Mode++)
    {
        for (size_t s = 0; s < sizeof(OutSteps) / sizeof(OutSteps[0]); s++)
        {
            DT_ASSERT_OK(DtAsiEnc_SetTxMode(
                &Enc, DTAPI_TXMODE_ADD16 | (Mode == 0 ? DTAPI_TXMODE_BURST : 0)));
            DT_ASSERT_OK(DtAsiEnc_SetRate(&Enc, 2000000));
            DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
            Syms = EncodeAll(&Enc, Ts188, 4 * 188, 100, OutSteps[s], &NumSyms);
            D.NumBytes = D.NumK28 = D.NumSyms = 0;
            D.Rd = 1;
            D.Valid = true;
            Decode(&D, Syms, NumSyms, NULL);
            DT_ASSERT(D.Valid);
            DT_ASSERT(D.NumBytes >= 3u * 204 + 188);
            for (int n = 0; n < 3; n++)
            {
                DT_ASSERT_MEM(D.Bytes + n * 204, Ts188 + n * 188, 188);
                for (int i = 188; i < 204; i++)
                    DT_ASSERT_EQ(D.Bytes[n * 204 + i], 0);
            }
            DT_ASSERT_MEM(D.Bytes + 3 * 204, Ts188 + 3 * 188, 188);
            free(Syms);
        }
    }

    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_MIN16));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    Syms = EncodeAll(&Enc, Ts204, 3 * 204, 70, 1u << 20, &NumSyms);
    D.NumBytes = D.NumK28 = D.NumSyms = 0;
    D.Rd = 1;
    Decode(&D, Syms, NumSyms, NULL);
    DT_ASSERT(D.Valid);
    DT_ASSERT_EQ(D.NumBytes, 3u * 188);
    for (int n = 0; n < 3; n++)
        DT_ASSERT_MEM(D.Bytes + n * 188, Ts204 + n * 204, 188);
    free(Syms);
    free(D.Bytes);
    free(Ts188);
    free(Ts204);
}

// A packet that does not start with 0x47 sets the synchronisation error, and the bytes
// up to the next 0x47 are dropped, also at the end of what was given.
DT_TEST(SyncErrorsSkipToTheNextPacket)
{
    BuildDecoder();
    uint8_t* Ts = Packets(3, 188);
    uint8_t In[3 + 3 * 188];
    In[0] = 1, In[1] = 2, In[2] = 3;
    memcpy(In + 3, Ts, 3 * 188);

    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    size_t NumSyms;
    uint16_t* Syms = EncodeAll(&Enc, In, sizeof(In), sizeof(In), 1u << 20, &NumSyms);
    Decoded D = NewDecoded(3 * 188);
    Decode(&D, Syms, NumSyms, NULL);
    DT_ASSERT(D.Valid);
    DT_ASSERT_EQ(D.NumBytes, 3u * 188);
    DT_ASSERT_MEM(D.Bytes, Ts, 3 * 188);
    int Flags, Latched;
    DtAsiEnc_GetFlags(&Enc, &Flags, &Latched);
    DT_ASSERT_EQ(Flags, 0);
    DT_ASSERT_EQ(Latched, DTAPI_TX_SYNC_ERR);
    DtAsiEnc_ClearFlags(&Enc, DTAPI_TX_SYNC_ERR);
    DtAsiEnc_GetFlags(&Enc, &Flags, &Latched);
    DT_ASSERT_EQ(Latched, 0);
    free(Syms);

    // Only garbage: everything taken, nothing written, and no byte read past the input.
    uint8_t Garbage[5] = {1, 2, 3, 4, 5};
    uint16_t Out[64];
    size_t Taken, Written;
    DtAsiEnc_Convert(&Enc, Garbage, sizeof(Garbage), Out, 64, &Taken, &Written);
    DT_ASSERT_EQ(Taken, sizeof(Garbage));
    DtAsiEnc_GetFlags(&Enc, &Flags, &Latched);
    DT_ASSERT_EQ(Flags, DTAPI_TX_SYNC_ERR);
    free(D.Bytes);
    free(Ts);
}

// With TXONTIME a packet goes out when its time comes, two ticks of 54 MHz a symbol,
// counted from the first packet's time.
DT_TEST(PacketsGoOutOnTime)
{
    BuildDecoder();
    uint8_t* Ts = Packets(3, 188);
    const uint32_t Times[3] = {1000000, 1000000 + 2 * 5000, 1000000 + 2 * 20000};
    uint8_t In[3 * 192];
    for (int n = 0; n < 3; n++)
    {
        memcpy(In + n * 192, &Times[n], 4);
        memcpy(In + n * 192 + 4, Ts + n * 188, 188);
    }

    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_OK(DtAsiEnc_SetTxMode(&Enc, DTAPI_TXMODE_188 | DTAPI_TXMODE_TXONTIME));
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    size_t NumSyms;
    uint16_t* Syms = EncodeAll(&Enc, In, sizeof(In), 50, 33, &NumSyms);
    Decoded D = NewDecoded(3 * 188);
    size_t Positions[3 * 188];
    Decode(&D, Syms, NumSyms, Positions);
    DT_ASSERT(D.Valid);
    DT_ASSERT_EQ(D.NumBytes, 3u * 188);
    DT_ASSERT_MEM(D.Bytes, Ts, 3 * 188);
    DT_ASSERT_EQ(Positions[0], 0u);
    DT_ASSERT_EQ(Positions[188], 5000u);
    DT_ASSERT_EQ(Positions[2 * 188], 20000u);
    free(Syms);
    free(D.Bytes);
    free(Ts);
}

// Padding is K28.5 that keeps the disparity, and the symbols of a rate carry its bytes.
DT_TEST(PaddingAndLoad)
{
    DtAsiEnc Enc;
    DtAsiEnc_Init(&Enc);
    DT_ASSERT_OK(DtAsiEnc_Start(&Enc));
    uint16_t Pad[4];
    DtAsiEnc_Pad(&Enc, Pad, 4);
    DT_ASSERT_EQ(Pad[0], DT_ASI_K28_5_RDPOS);
    DT_ASSERT_EQ(Pad[1], DT_ASI_K28_5_RDNEG);
    DT_ASSERT_EQ(Pad[2], DT_ASI_K28_5_RDPOS);
    DT_ASSERT_EQ(Enc.Rd, 1);

    // A second of symbols at 10 Mbit/s carries 1.25 MB. DTAPI's estimate is a little
    // more, about 1,250,616 bytes, since it divides by the symbols left after the K28.5
    // before each packet.
    int64_t Bytes = DtAsiEnc_BytesOf(&Enc, DT_ASI_SYMBOL_RATE);
    DT_ASSERT(Bytes >= 1250614 && Bytes <= 1250617);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Main +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST_MAIN("AsiEnc", DT_RUN(TableIsThe8b10bCode), DT_RUN(ModesAndRates),
             DT_RUN(PacketsAndRate), DT_RUN(CommasBeforePackets),
             DT_RUN(SixteenBytesMoreOrLess), DT_RUN(SyncErrorsSkipToTheNextPacket),
             DT_RUN(PacketsGoOutOnTime), DT_RUN(PaddingAndLoad))

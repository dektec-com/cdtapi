// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAsiEnc.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - A transport stream coded as the ASI symbols a DtPcie card sends -
// Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtAsiEnc.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= 8b/10b +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct Code8b10b
{
    uint16_t Code;
    uint16_t NextRd;
} Code8b10b;

// For each byte, its code and the running disparity after it, first for a negative
// running disparity, then for a positive one. The tests check the table against the
// code's rules rather than trusting it.
static const Code8b10b g_Codes[256][2] = {
    {{0x0B9, 0}, {0x346, 1}}, {{0x0AE, 0}, {0x351, 1}}, {{0x0AD, 0}, {0x352, 1}},
    {{0x363, 1}, {0x0A3, 0}}, {{0x0AB, 0}, {0x354, 1}}, {{0x365, 1}, {0x0A5, 0}},
    {{0x366, 1}, {0x0A6, 0}}, {{0x347, 1}, {0x0B8, 0}}, {{0x0A7, 0}, {0x358, 1}},
    {{0x369, 1}, {0x0A9, 0}}, {{0x36A, 1}, {0x0AA, 0}}, {{0x34B, 1}, {0x08B, 0}},
    {{0x36C, 1}, {0x0AC, 0}}, {{0x34D, 1}, {0x08D, 0}}, {{0x34E, 1}, {0x08E, 0}},
    {{0x0BA, 0}, {0x345, 1}}, {{0x0B6, 0}, {0x349, 1}}, {{0x371, 1}, {0x0B1, 0}},
    {{0x372, 1}, {0x0B2, 0}}, {{0x353, 1}, {0x093, 0}}, {{0x374, 1}, {0x0B4, 0}},
    {{0x355, 1}, {0x095, 0}}, {{0x356, 1}, {0x096, 0}}, {{0x097, 0}, {0x368, 1}},
    {{0x0B3, 0}, {0x34C, 1}}, {{0x359, 1}, {0x099, 0}}, {{0x35A, 1}, {0x09A, 0}},
    {{0x09B, 0}, {0x364, 1}}, {{0x35C, 1}, {0x09C, 0}}, {{0x09D, 0}, {0x362, 1}},
    {{0x09E, 0}, {0x361, 1}}, {{0x0B5, 0}, {0x34A, 1}}, {{0x279, 1}, {0x246, 0}},
    {{0x26E, 1}, {0x251, 0}}, {{0x26D, 1}, {0x252, 0}}, {{0x263, 0}, {0x263, 1}},
    {{0x26B, 1}, {0x254, 0}}, {{0x265, 0}, {0x265, 1}}, {{0x266, 0}, {0x266, 1}},
    {{0x247, 0}, {0x278, 1}}, {{0x267, 1}, {0x258, 0}}, {{0x269, 0}, {0x269, 1}},
    {{0x26A, 0}, {0x26A, 1}}, {{0x24B, 0}, {0x24B, 1}}, {{0x26C, 0}, {0x26C, 1}},
    {{0x24D, 0}, {0x24D, 1}}, {{0x24E, 0}, {0x24E, 1}}, {{0x27A, 1}, {0x245, 0}},
    {{0x276, 1}, {0x249, 0}}, {{0x271, 0}, {0x271, 1}}, {{0x272, 0}, {0x272, 1}},
    {{0x253, 0}, {0x253, 1}}, {{0x274, 0}, {0x274, 1}}, {{0x255, 0}, {0x255, 1}},
    {{0x256, 0}, {0x256, 1}}, {{0x257, 1}, {0x268, 0}}, {{0x273, 1}, {0x24C, 0}},
    {{0x259, 0}, {0x259, 1}}, {{0x25A, 0}, {0x25A, 1}}, {{0x25B, 1}, {0x264, 0}},
    {{0x25C, 0}, {0x25C, 1}}, {{0x25D, 1}, {0x262, 0}}, {{0x25E, 1}, {0x261, 0}},
    {{0x275, 1}, {0x24A, 0}}, {{0x2B9, 1}, {0x286, 0}}, {{0x2AE, 1}, {0x291, 0}},
    {{0x2AD, 1}, {0x292, 0}}, {{0x2A3, 0}, {0x2A3, 1}}, {{0x2AB, 1}, {0x294, 0}},
    {{0x2A5, 0}, {0x2A5, 1}}, {{0x2A6, 0}, {0x2A6, 1}}, {{0x287, 0}, {0x2B8, 1}},
    {{0x2A7, 1}, {0x298, 0}}, {{0x2A9, 0}, {0x2A9, 1}}, {{0x2AA, 0}, {0x2AA, 1}},
    {{0x28B, 0}, {0x28B, 1}}, {{0x2AC, 0}, {0x2AC, 1}}, {{0x28D, 0}, {0x28D, 1}},
    {{0x28E, 0}, {0x28E, 1}}, {{0x2BA, 1}, {0x285, 0}}, {{0x2B6, 1}, {0x289, 0}},
    {{0x2B1, 0}, {0x2B1, 1}}, {{0x2B2, 0}, {0x2B2, 1}}, {{0x293, 0}, {0x293, 1}},
    {{0x2B4, 0}, {0x2B4, 1}}, {{0x295, 0}, {0x295, 1}}, {{0x296, 0}, {0x296, 1}},
    {{0x297, 1}, {0x2A8, 0}}, {{0x2B3, 1}, {0x28C, 0}}, {{0x299, 0}, {0x299, 1}},
    {{0x29A, 0}, {0x29A, 1}}, {{0x29B, 1}, {0x2A4, 0}}, {{0x29C, 0}, {0x29C, 1}},
    {{0x29D, 1}, {0x2A2, 0}}, {{0x29E, 1}, {0x2A1, 0}}, {{0x2B5, 1}, {0x28A, 0}},
    {{0x339, 1}, {0x0C6, 0}}, {{0x32E, 1}, {0x0D1, 0}}, {{0x32D, 1}, {0x0D2, 0}},
    {{0x0E3, 0}, {0x323, 1}}, {{0x32B, 1}, {0x0D4, 0}}, {{0x0E5, 0}, {0x325, 1}},
    {{0x0E6, 0}, {0x326, 1}}, {{0x0C7, 0}, {0x338, 1}}, {{0x327, 1}, {0x0D8, 0}},
    {{0x0E9, 0}, {0x329, 1}}, {{0x0EA, 0}, {0x32A, 1}}, {{0x0CB, 0}, {0x30B, 1}},
    {{0x0EC, 0}, {0x32C, 1}}, {{0x0CD, 0}, {0x30D, 1}}, {{0x0CE, 0}, {0x30E, 1}},
    {{0x33A, 1}, {0x0C5, 0}}, {{0x336, 1}, {0x0C9, 0}}, {{0x0F1, 0}, {0x331, 1}},
    {{0x0F2, 0}, {0x332, 1}}, {{0x0D3, 0}, {0x313, 1}}, {{0x0F4, 0}, {0x334, 1}},
    {{0x0D5, 0}, {0x315, 1}}, {{0x0D6, 0}, {0x316, 1}}, {{0x317, 1}, {0x0E8, 0}},
    {{0x333, 1}, {0x0CC, 0}}, {{0x0D9, 0}, {0x319, 1}}, {{0x0DA, 0}, {0x31A, 1}},
    {{0x31B, 1}, {0x0E4, 0}}, {{0x0DC, 0}, {0x31C, 1}}, {{0x31D, 1}, {0x0E2, 0}},
    {{0x31E, 1}, {0x0E1, 0}}, {{0x335, 1}, {0x0CA, 0}}, {{0x139, 0}, {0x2C6, 1}},
    {{0x12E, 0}, {0x2D1, 1}}, {{0x12D, 0}, {0x2D2, 1}}, {{0x2E3, 1}, {0x123, 0}},
    {{0x12B, 0}, {0x2D4, 1}}, {{0x2E5, 1}, {0x125, 0}}, {{0x2E6, 1}, {0x126, 0}},
    {{0x2C7, 1}, {0x138, 0}}, {{0x127, 0}, {0x2D8, 1}}, {{0x2E9, 1}, {0x129, 0}},
    {{0x2EA, 1}, {0x12A, 0}}, {{0x2CB, 1}, {0x10B, 0}}, {{0x2EC, 1}, {0x12C, 0}},
    {{0x2CD, 1}, {0x10D, 0}}, {{0x2CE, 1}, {0x10E, 0}}, {{0x13A, 0}, {0x2C5, 1}},
    {{0x136, 0}, {0x2C9, 1}}, {{0x2F1, 1}, {0x131, 0}}, {{0x2F2, 1}, {0x132, 0}},
    {{0x2D3, 1}, {0x113, 0}}, {{0x2F4, 1}, {0x134, 0}}, {{0x2D5, 1}, {0x115, 0}},
    {{0x2D6, 1}, {0x116, 0}}, {{0x117, 0}, {0x2E8, 1}}, {{0x133, 0}, {0x2CC, 1}},
    {{0x2D9, 1}, {0x119, 0}}, {{0x2DA, 1}, {0x11A, 0}}, {{0x11B, 0}, {0x2E4, 1}},
    {{0x2DC, 1}, {0x11C, 0}}, {{0x11D, 0}, {0x2E2, 1}}, {{0x11E, 0}, {0x2E1, 1}},
    {{0x135, 0}, {0x2CA, 1}}, {{0x179, 1}, {0x146, 0}}, {{0x16E, 1}, {0x151, 0}},
    {{0x16D, 1}, {0x152, 0}}, {{0x163, 0}, {0x163, 1}}, {{0x16B, 1}, {0x154, 0}},
    {{0x165, 0}, {0x165, 1}}, {{0x166, 0}, {0x166, 1}}, {{0x147, 0}, {0x178, 1}},
    {{0x167, 1}, {0x158, 0}}, {{0x169, 0}, {0x169, 1}}, {{0x16A, 0}, {0x16A, 1}},
    {{0x14B, 0}, {0x14B, 1}}, {{0x16C, 0}, {0x16C, 1}}, {{0x14D, 0}, {0x14D, 1}},
    {{0x14E, 0}, {0x14E, 1}}, {{0x17A, 1}, {0x145, 0}}, {{0x176, 1}, {0x149, 0}},
    {{0x171, 0}, {0x171, 1}}, {{0x172, 0}, {0x172, 1}}, {{0x153, 0}, {0x153, 1}},
    {{0x174, 0}, {0x174, 1}}, {{0x155, 0}, {0x155, 1}}, {{0x156, 0}, {0x156, 1}},
    {{0x157, 1}, {0x168, 0}}, {{0x173, 1}, {0x14C, 0}}, {{0x159, 0}, {0x159, 1}},
    {{0x15A, 0}, {0x15A, 1}}, {{0x15B, 1}, {0x164, 0}}, {{0x15C, 0}, {0x15C, 1}},
    {{0x15D, 1}, {0x162, 0}}, {{0x15E, 1}, {0x161, 0}}, {{0x175, 1}, {0x14A, 0}},
    {{0x1B9, 1}, {0x186, 0}}, {{0x1AE, 1}, {0x191, 0}}, {{0x1AD, 1}, {0x192, 0}},
    {{0x1A3, 0}, {0x1A3, 1}}, {{0x1AB, 1}, {0x194, 0}}, {{0x1A5, 0}, {0x1A5, 1}},
    {{0x1A6, 0}, {0x1A6, 1}}, {{0x187, 0}, {0x1B8, 1}}, {{0x1A7, 1}, {0x198, 0}},
    {{0x1A9, 0}, {0x1A9, 1}}, {{0x1AA, 0}, {0x1AA, 1}}, {{0x18B, 0}, {0x18B, 1}},
    {{0x1AC, 0}, {0x1AC, 1}}, {{0x18D, 0}, {0x18D, 1}}, {{0x18E, 0}, {0x18E, 1}},
    {{0x1BA, 1}, {0x185, 0}}, {{0x1B6, 1}, {0x189, 0}}, {{0x1B1, 0}, {0x1B1, 1}},
    {{0x1B2, 0}, {0x1B2, 1}}, {{0x193, 0}, {0x193, 1}}, {{0x1B4, 0}, {0x1B4, 1}},
    {{0x195, 0}, {0x195, 1}}, {{0x196, 0}, {0x196, 1}}, {{0x197, 1}, {0x1A8, 0}},
    {{0x1B3, 1}, {0x18C, 0}}, {{0x199, 0}, {0x199, 1}}, {{0x19A, 0}, {0x19A, 1}},
    {{0x19B, 1}, {0x1A4, 0}}, {{0x19C, 0}, {0x19C, 1}}, {{0x19D, 1}, {0x1A2, 0}},
    {{0x19E, 1}, {0x1A1, 0}}, {{0x1B5, 1}, {0x18A, 0}}, {{0x239, 0}, {0x1C6, 1}},
    {{0x22E, 0}, {0x1D1, 1}}, {{0x22D, 0}, {0x1D2, 1}}, {{0x1E3, 1}, {0x223, 0}},
    {{0x22B, 0}, {0x1D4, 1}}, {{0x1E5, 1}, {0x225, 0}}, {{0x1E6, 1}, {0x226, 0}},
    {{0x1C7, 1}, {0x238, 0}}, {{0x227, 0}, {0x1D8, 1}}, {{0x1E9, 1}, {0x229, 0}},
    {{0x1EA, 1}, {0x22A, 0}}, {{0x1CB, 1}, {0x04B, 0}}, {{0x1EC, 1}, {0x22C, 0}},
    {{0x1CD, 1}, {0x04D, 0}}, {{0x1CE, 1}, {0x04E, 0}}, {{0x23A, 0}, {0x1C5, 1}},
    {{0x236, 0}, {0x1C9, 1}}, {{0x3B1, 1}, {0x231, 0}}, {{0x3B2, 1}, {0x232, 0}},
    {{0x1D3, 1}, {0x213, 0}}, {{0x3B4, 1}, {0x234, 0}}, {{0x1D5, 1}, {0x215, 0}},
    {{0x1D6, 1}, {0x216, 0}}, {{0x217, 0}, {0x1E8, 1}}, {{0x233, 0}, {0x1CC, 1}},
    {{0x1D9, 1}, {0x219, 0}}, {{0x1DA, 1}, {0x21A, 0}}, {{0x21B, 0}, {0x1E4, 1}},
    {{0x1DC, 1}, {0x21C, 0}}, {{0x21D, 0}, {0x1E2, 1}}, {{0x21E, 0}, {0x1E1, 1}},
    {{0x235, 0}, {0x1CA, 1}},
};

static const Code8b10b g_K28_5[2] = {{DT_ASI_K28_5_RDNEG, 1}, {DT_ASI_K28_5_RDPOS, 0}};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_Code -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
uint16_t DtAsiEnc_Code(uint8_t Byte, int Rd, int* NextRd)
{
    const Code8b10b* C = &g_Codes[Byte][Rd != 0 ? 1 : 0];
    if (NextRd != NULL)
        *NextRd = C->NextRd;
    return C->Code;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Put(DtAsiEnc* Enc, uint16_t** Out, const Code8b10b* C)
{
    *(*Out)++ = C->Code;
    Enc->Rd = C->NextRd;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutK28 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void PutK28(DtAsiEnc* Enc, uint16_t** Out)
{
    Put(Enc, Out, &g_K28_5[Enc->Rd]);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- PutByte -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void PutByte(DtAsiEnc* Enc, uint16_t** Out, uint8_t Byte)
{
    Put(Enc, Out, &g_Codes[Byte][Enc->Rd]);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Settings +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The interval the rate is kept over, in symbol times: 188 x 8, so that a rate in whole
// bits a second has no fraction.
#define INTERVAL (188 * 8)

// The states of a DTAPI_TXMODE_TXONTIME conversion.
enum
{
    ONTIME_INIT,
    ONTIME_TIME,
    ONTIME_SYNC,
    ONTIME_BYTES,
    ONTIME_SKIP
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ApplyRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// When the line has no room for even one K28.5 before each packet, none is sent, but the
// room of one is still reserved.
//
static DtapiResult ApplyRate(DtAsiEnc* Enc, int64_t Rate, int OutSize, bool Raw)
{
    const int64_t Line = (int64_t)DT_ASI_SYMBOL_RATE * INTERVAL;
    const int64_t Needed = Rate * INTERVAL * OutSize / (188 * 8);
    if (Rate <= 0 || Needed <= 0 || Needed > Line)
        return DTAPI_E_INVALID_RATE;

    int K28 = 0;
    int64_t Overhead = 0;
    if (!Raw)
    {
        K28 = 2;
        Overhead = Rate * K28 * INTERVAL / (188 * 8);
        if (Needed > Line - Overhead)
        {
            K28 = 1;
            Overhead = Rate * K28 * INTERVAL / (188 * 8);
            if (Needed > Line - Overhead)
                K28 = 0;
        }
    }
    Enc->Rate = Rate;
    Enc->Needed = Needed;
    Enc->Available = Line - Overhead;
    Enc->K28BeforePacket = K28;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_Init -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAsiEnc_Init(DtAsiEnc* Enc)
{
    memset(Enc, 0, sizeof(*Enc));
    Enc->InSize = Enc->InUsed = Enc->OutSize = 188;
    Enc->Burst = true;
    ApplyRate(Enc, 10000000, Enc->OutSize, false);
    DtAsiEnc_Start(Enc);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiEnc_SetTxMode(DtAsiEnc* Enc, int TxMode)
{
    int In, Used, Out;
    bool Raw = false;
    switch (TxMode & DTAPI_TXMODE_TS_MASK)
    {
    case DTAPI_TXMODE_188:
        In = Used = Out = 188;
        break;
    case DTAPI_TXMODE_204:
        In = Used = Out = 204;
        break;
    case DTAPI_TXMODE_ADD16:
        In = Used = 188;
        Out = 204;
        break;
    case DTAPI_TXMODE_MIN16:
        In = 204;
        Used = Out = 188;
        break;
    case DTAPI_TXMODE_RAW:
        In = Used = Out = 188;
        Raw = true;
        break;
    case DTAPI_TXMODE_RAWASI:
        return DTAPI_E_NOT_IMPLEMENTED;
    default:
        return DTAPI_E_INVALID_ARG;
    }

    Enc->InSize = In;
    Enc->InUsed = Used;
    Enc->OutSize = Out;
    Enc->Raw = Raw;
    Enc->Burst = (TxMode & DTAPI_TXMODE_BURST) != 0;
    Enc->TxOnTime = (TxMode & DTAPI_TXMODE_TXONTIME) != 0;

    // The rate counts in the new packet size; one the new size does not fit is refused
    // when the stream starts, not here.
    ApplyRate(Enc, Enc->Rate, Out, Raw);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_SetRate -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiEnc_SetRate(DtAsiEnc* Enc, int64_t Rate)
{
    return ApplyRate(Enc, Rate, Enc->OutSize, Enc->Raw);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtAsiEnc_Start(DtAsiEnc* Enc)
{
    if (!Enc->TxOnTime)
    {
        DtapiResult Result = ApplyRate(Enc, Enc->Rate, Enc->OutSize, Enc->Raw);
        if (Result != DTAPI_OK)
            return Result;
    }
    Enc->Rd = 1;
    Enc->Acc = 0;
    Enc->ByteIndex = 0;
    Enc->K28Sent = 0;
    Enc->ToSkip = 0;
    Enc->SyncErr = Enc->SyncErrLatched = false;
    Enc->OnTimeState = ONTIME_INIT;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Conversion +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Where a conversion stands: what is left of the input and of the room for symbols.
typedef struct Cursor
{
    const uint8_t* In;
    size_t InLeft;
    uint16_t* Out;
    size_t OutLeft;
} Cursor;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- StartPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// What comes before a packet's first byte, in normal and burst mode: dropping the bytes
// MIN16 leaves out of the packet before, finding the sync byte, and the K28.5 before the
// packet. Returns false when the input or the room ran out first. No byte past the end
// of the input is read while a sync byte is searched for.
//
static bool StartPacket(DtAsiEnc* Enc, Cursor* C)
{
    if (Enc->Raw || Enc->ByteIndex != 0)
        return true;

    if (Enc->ToSkip > 0)
    {
        size_t Skip = (size_t)Enc->ToSkip < C->InLeft ? (size_t)Enc->ToSkip : C->InLeft;
        C->In += Skip;
        C->InLeft -= Skip;
        Enc->ToSkip -= (int)Skip;
        if (Enc->ToSkip > 0 || C->InLeft == 0)
            return false;
    }

    if (*C->In != 0x47)
    {
        Enc->SyncErr = Enc->SyncErrLatched = true;
        while (C->InLeft > 0 && *C->In != 0x47)
        {
            C->In++;
            C->InLeft--;
        }
        if (C->InLeft == 0)
            return false;
    }
    else
        Enc->SyncErr = false;

    while (Enc->K28Sent < Enc->K28BeforePacket && C->OutLeft > 0)
    {
        PutK28(Enc, &C->Out);
        C->OutLeft--;
        Enc->K28Sent++;
    }
    return C->OutLeft > 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- EndPacket -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void EndPacket(DtAsiEnc* Enc)
{
    Enc->ByteIndex = 0;
    Enc->K28Sent = 0;
    Enc->ToSkip = Enc->InSize - Enc->InUsed;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConvertNormal -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// In normal mode the rate decides symbol by symbol whether a byte or a K28.5 goes out,
// so the fill is spread between the bytes.
//
static void ConvertNormal(DtAsiEnc* Enc, Cursor* C)
{
    while (C->InLeft > 0 && C->OutLeft > 0)
    {
        if (!StartPacket(Enc, C))
            break;

        // The packet's bytes, then the zeros that make it OutSize. Among the zeros of
        // ADD16 no byte of the packet is left.
        for (int Zeros = 0; Zeros < 2; Zeros++)
        {
            int Want = Enc->ByteIndex < Enc->InUsed
                           ? (Zeros == 0 ? Enc->InUsed - Enc->ByteIndex : 0)
                           : (Zeros == 0 ? 0 : Enc->OutSize - Enc->ByteIndex);
            if (Zeros == 0 && (size_t)Want > C->InLeft)
                Want = (int)C->InLeft;
            while (C->OutLeft > 0 && Want > 0)
            {
                Enc->Acc += Enc->Needed;
                if (Enc->Acc >= 0)
                {
                    Enc->Acc -= Enc->Available;
                    PutByte(Enc, &C->Out, Zeros == 0 ? *C->In : 0);
                    if (Zeros == 0)
                    {
                        C->In++;
                        C->InLeft--;
                    }
                    Enc->ByteIndex++;
                    Want--;
                }
                else
                    PutK28(Enc, &C->Out);
                C->OutLeft--;
            }
        }
        if (Enc->ByteIndex >= Enc->OutSize)
            EndPacket(Enc);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConvertBurst -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// In burst mode the K28.5 the rate needs go before the packet's bytes, which then go out
// back to back, and the accumulator is settled per packet.
//
static void ConvertBurst(DtAsiEnc* Enc, Cursor* C)
{
    while (C->InLeft > 0 && C->OutLeft > 0)
    {
        if (!StartPacket(Enc, C))
            break;

        if (Enc->Acc < 0)
        {
            int64_t K28 = ((Enc->Needed - 1) - Enc->Acc) / Enc->Needed;
            if ((size_t)K28 > C->OutLeft)
                K28 = (int64_t)C->OutLeft;
            Enc->Acc += Enc->Needed * K28;
            C->OutLeft -= (size_t)K28;
            for (; K28 > 0; K28--)
                PutK28(Enc, &C->Out);
        }

        // None once the packet's bytes are out and only the zeros of ADD16 are left.
        size_t Bytes =
            Enc->ByteIndex < Enc->InUsed ? (size_t)(Enc->InUsed - Enc->ByteIndex) : 0;
        if (Bytes > C->InLeft)
            Bytes = C->InLeft;
        if (Bytes > C->OutLeft)
            Bytes = C->OutLeft;
        Enc->ByteIndex += (int)Bytes;
        Enc->Acc += Enc->Needed * (int64_t)Bytes;
        C->InLeft -= Bytes;
        C->OutLeft -= Bytes;
        for (; Bytes > 0; Bytes--)
            PutByte(Enc, &C->Out, *C->In++);

        size_t Zeros =
            Enc->ByteIndex < Enc->InUsed ? 0 : (size_t)(Enc->OutSize - Enc->ByteIndex);
        if (Zeros > C->OutLeft)
            Zeros = C->OutLeft;
        Enc->ByteIndex += (int)Zeros;
        Enc->Acc += Enc->Needed * (int64_t)Zeros;
        C->OutLeft -= Zeros;
        for (; Zeros > 0; Zeros--)
            PutByte(Enc, &C->Out, 0);

        if (Enc->ByteIndex >= Enc->OutSize)
        {
            EndPacket(Enc);
            Enc->Acc -= Enc->Available * Enc->OutSize;
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ConvertOnTime -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A symbol is two ticks of 54 MHz; K28.5 go out until a packet's time has come, then the
// packet. No byte past the end of the input is read while a sync byte is searched for.
//
static void ConvertOnTime(DtAsiEnc* Enc, Cursor* C)
{
    while (C->InLeft > 0 && C->OutLeft > 0)
    {
        switch (Enc->OnTimeState)
        {
        case ONTIME_INIT:
            Enc->Now = Enc->Next = 0;
            Enc->First = true;
            Enc->ByteIndex = 0;
            Enc->OnTimeState = ONTIME_TIME;
            break;

        case ONTIME_TIME:
            while (Enc->ByteIndex < 4 && C->InLeft > 0)
            {
                Enc->TimeBytes[Enc->ByteIndex++] = *C->In++;
                C->InLeft--;
            }
            if (Enc->ByteIndex == 4)
            {
                Enc->ByteIndex = 0;
                Enc->OnTimeState = ONTIME_SYNC;
            }
            break;

        case ONTIME_SYNC:
            if (*C->In != 0x47)
            {
                Enc->SyncErr = Enc->SyncErrLatched = true;
                while (C->InLeft > 0 && *C->In != 0x47)
                {
                    memmove(Enc->TimeBytes, Enc->TimeBytes + 1, 3);
                    Enc->TimeBytes[3] = *C->In++;
                    C->InLeft--;
                }
            }
            if (C->InLeft > 0)
            {
                Enc->SyncErr = false;
                Enc->Next =
                    (uint32_t)Enc->TimeBytes[0] | (uint32_t)Enc->TimeBytes[1] << 8 |
                    (uint32_t)Enc->TimeBytes[2] << 16 | (uint32_t)Enc->TimeBytes[3] << 24;
                if (Enc->First)
                {
                    Enc->First = false;
                    Enc->Now = Enc->Next;
                }
                Enc->OnTimeState = ONTIME_BYTES;
            }
            break;

        case ONTIME_BYTES:
            while (C->OutLeft > 0 && (int32_t)(Enc->Next - Enc->Now) > 0)
            {
                PutK28(Enc, &C->Out);
                C->OutLeft--;
                Enc->Now += 2;
            }
            if (C->OutLeft == 0)
                break;
            while (C->OutLeft > 0 && C->InLeft > 0 && Enc->ByteIndex < Enc->InUsed)
            {
                PutByte(Enc, &C->Out, *C->In++);
                C->InLeft--;
                C->OutLeft--;
                Enc->ByteIndex++;
                Enc->Now += 2;
            }
            if (C->OutLeft == 0 || Enc->ByteIndex < Enc->InUsed)
                break;
            while (C->OutLeft > 0 && Enc->ByteIndex < Enc->OutSize)
            {
                PutByte(Enc, &C->Out, 0);
                C->OutLeft--;
                Enc->ByteIndex++;
                Enc->Now += 2;
            }
            if (Enc->ByteIndex >= Enc->OutSize)
            {
                Enc->ByteIndex = 0;
                Enc->ToSkip = Enc->InSize - Enc->InUsed;
                Enc->OnTimeState = Enc->ToSkip > 0 ? ONTIME_SKIP : ONTIME_TIME;
            }
            break;

        default: // ONTIME_SKIP
        {
            size_t Skip =
                (size_t)Enc->ToSkip < C->InLeft ? (size_t)Enc->ToSkip : C->InLeft;
            C->In += Skip;
            C->InLeft -= Skip;
            Enc->ToSkip -= (int)Skip;
            if (Enc->ToSkip == 0)
                Enc->OnTimeState = ONTIME_TIME;
            break;
        }
        }
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_Convert -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAsiEnc_Convert(DtAsiEnc* Enc, const uint8_t* In, size_t InSize, uint16_t* Out,
                      size_t OutSyms, size_t* Taken, size_t* Written)
{
    Cursor C = {In, InSize, Out, OutSyms};
    if (Enc->TxOnTime)
        ConvertOnTime(Enc, &C);
    else if (Enc->Burst)
        ConvertBurst(Enc, &C);
    else
        ConvertNormal(Enc, &C);
    *Taken = InSize - C.InLeft;
    *Written = OutSyms - C.OutLeft;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_Pad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtAsiEnc_Pad(DtAsiEnc* Enc, uint16_t* Out, size_t Syms)
{
    for (; Syms > 0; Syms--)
        PutK28(Enc, &Out);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_BytesOf -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int64_t DtAsiEnc_BytesOf(const DtAsiEnc* Enc, int64_t Syms)
{
    return (int64_t)((double)Enc->Needed * (double)Syms / (double)Enc->Available);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAsiEnc_GetFlags(const DtAsiEnc* Enc, int* Flags, int* Latched)
{
    *Flags = Enc->SyncErr ? DTAPI_TX_SYNC_ERR : 0;
    *Latched = Enc->SyncErrLatched ? DTAPI_TX_SYNC_ERR : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAsiEnc_ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtAsiEnc_ClearFlags(DtAsiEnc* Enc, int Flags)
{
    if ((Flags & DTAPI_TX_SYNC_ERR) != 0)
        Enc->SyncErr = Enc->SyncErrLatched = false;
}

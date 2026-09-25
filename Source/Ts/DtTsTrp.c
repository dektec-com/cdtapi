// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtTsTrp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The transparent packets a DtPcie card receives ASI into - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "DtTsTrp.h" // Interface being implemented.

// Where the fields of a transparent packet are.
#define AT_SECONDS 0
#define AT_NANOSECONDS 4
#define AT_PAYLOAD 8
#define AT_SYNC 212
#define AT_VALID 213
#define AT_SEQUENCE 214

// What DTAPI_RXMODE_STTRP delivers of a packet without its time stamp: the 204 payload
// bytes and the trailer.
#define TRP_DATA_SIZE 208

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ValidCountFits -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A valid count the mode accepts: in the raw and transparent modes any up to 204, else
// 188 or 204.
//
static bool ValidCountFits(int TsMode, int Valid)
{
    if (TsMode == DTAPI_RXMODE_STTRP || TsMode == DTAPI_RXMODE_STRAW)
        return Valid <= 204;
    return Valid == 188 || Valid == 204;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTsTrp_CheckMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtTsTrp_CheckMode(int RxMode)
{
    int TsMode = RxMode & DTAPI_RXMODE_TS_MASK;
    if (TsMode != DTAPI_RXMODE_ST188 && TsMode != DTAPI_RXMODE_ST204 &&
        TsMode != DTAPI_RXMODE_STMP2 && TsMode != DTAPI_RXMODE_STRAW &&
        TsMode != DTAPI_RXMODE_STTRP)
    {
        return DTAPI_E_INVALID_MODE;
    }
    if ((RxMode & DTAPI_RXMODE_TIMESTAMP64) != 0)
        return DTAPI_E_INVALID_MODE;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTsTrp_Start -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtTsTrp_Start(DtTsTrp* Trp, int RxMode)
{
    Trp->RxMode = RxMode;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTsTrp_Decode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// One packet decoded on its own; the FIFO it goes into is the channel's.
//
int DtTsTrp_Decode(DtTsTrp* Trp, const uint8_t* Packet, uint8_t* Out)
{
    if ((Packet[AT_SYNC] & 0xF0) != 0x50)
        return -1;

    const int TsMode = Trp->RxMode & DTAPI_RXMODE_TS_MASK;
    const bool LacksPacketSync = (Packet[AT_SYNC] & 0x08) == 0;
    if (TsMode != DTAPI_RXMODE_STRAW)
    {
        Trp->SyncErr = LacksPacketSync;
        if (LacksPacketSync)
            Trp->SyncErrLatched = true;
    }

    const int Valid = Packet[AT_VALID];
    if (!ValidCountFits(TsMode, Valid))
        return -1;
    if (LacksPacketSync && TsMode != DTAPI_RXMODE_STRAW && TsMode != DTAPI_RXMODE_STTRP)
        return 0;

    int PayloadBytes, ZeroBytes = 0;
    switch (TsMode)
    {
    case DTAPI_RXMODE_ST188:
        PayloadBytes = 188;
        break;
    case DTAPI_RXMODE_ST204:
        PayloadBytes = Valid;
        ZeroBytes = Valid == 188 ? 16 : 0;
        break;
    case DTAPI_RXMODE_STTRP:
        PayloadBytes = TRP_DATA_SIZE;
        break;
    default: // DTAPI_RXMODE_STMP2, DTAPI_RXMODE_STRAW
        PayloadBytes = Valid;
        break;
    }

    int OutBytes = 0, PayloadStart = AT_PAYLOAD;
    if ((Trp->RxMode & DTAPI_RXMODE_TIMESTAMP32) != 0 && Out == NULL)
        OutBytes = 4;
    else if ((Trp->RxMode & DTAPI_RXMODE_TIMESTAMP32) != 0)
    {
        // Ticks of a 54 MHz clock, counted from the time of day.
        uint32_t Seconds = (uint32_t)Packet[0] | (uint32_t)Packet[1] << 8 |
                           (uint32_t)Packet[2] << 16 | (uint32_t)Packet[3] << 24;
        uint32_t Nanoseconds = (uint32_t)Packet[4] | (uint32_t)Packet[5] << 8 |
                               (uint32_t)Packet[6] << 16 | (uint32_t)Packet[7] << 24;
        uint32_t Ticks = (uint32_t)((uint64_t)Seconds * 54000000u +
                                    (uint64_t)Nanoseconds * 54u / 1000u);
        for (int i = 0; i < 4; i++)
            Out[OutBytes++] = (uint8_t)(Ticks >> (8 * i));
    }
    else if ((Trp->RxMode & DTAPI_RXMODE_TIMESTAMP_TOD) != 0)
    {
        // The time of day as the card wrote it, before the payload.
        PayloadStart = AT_SECONDS;
        PayloadBytes += AT_PAYLOAD;
    }
    if (Out != NULL)
    {
        memcpy(Out + OutBytes, Packet + PayloadStart, (size_t)PayloadBytes);
        memset(Out + OutBytes + PayloadBytes, 0, (size_t)ZeroBytes);
    }
    return OutBytes + PayloadBytes + ZeroBytes;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTsTrp_FindSync -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The search runs over the trailers: Pos is where a trailer would start, and the first
// packet's valid count is taken as any up to 204.
//
bool DtTsTrp_FindSync(const DtTsTrp* Trp, const uint8_t* Buf, size_t Size, size_t* Offset)
{
    const int TsMode = Trp->RxMode & DTAPI_RXMODE_TS_MASK;
    if (Size < (size_t)DT_TRP_SIZE * DT_TRP_NUM_SYNC)
        return false;

    for (size_t Pos = 0; Pos + (size_t)DT_TRP_SIZE * DT_TRP_NUM_SYNC <= Size; Pos++)
    {
        if ((Buf[Pos] & 0xF0) != 0x50 || Buf[Pos + 1] > 204)
            continue;

        unsigned ExpectedSeq = (unsigned)(Buf[Pos + 2] | Buf[Pos + 3] << 8);
        bool Found = true;
        for (int i = 1; i < DT_TRP_NUM_SYNC && Found; i++)
        {
            const uint8_t* Trailer = Buf + Pos + (size_t)i * DT_TRP_SIZE;
            ExpectedSeq = (ExpectedSeq + 1) & 0xFFFF;
            Found = (Trailer[0] & 0xF0) == 0x50 && ValidCountFits(TsMode, Trailer[1]) &&
                    (unsigned)(Trailer[2] | Trailer[3] << 8) == ExpectedSeq;
        }
        if (Found)
        {
            *Offset = Pos >= AT_SYNC ? Pos - AT_SYNC : Pos + (DT_TRP_SIZE - AT_SYNC);
            return true;
        }
    }
    return false;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTsTrp_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtTsTrp_GetFlags(const DtTsTrp* Trp, int* Flags, int* Latched)
{
    *Flags = Trp->SyncErr ? DTAPI_RX_SYNC_ERR : 0;
    *Latched = Trp->SyncErrLatched ? DTAPI_RX_SYNC_ERR : 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTsTrp_ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtTsTrp_ClearFlags(DtTsTrp* Trp, int Flags)
{
    if ((Flags & DTAPI_RX_SYNC_ERR) != 0)
        Trp->SyncErr = Trp->SyncErrLatched = false;
}

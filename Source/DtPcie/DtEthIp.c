// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtEthIp.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - The header in front of every Ethernet frame in a pipe's shared buffer
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "DtEthIp.h" // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The header without its alignment bytes, which is where a time stamp packet's frame
// starts.
#define DT_ETHIP_TIMESTAMP_HEADER_SIZE 16

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Get64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The little-endian word at Bytes.
//
static uint64_t Get64(const uint8_t* Bytes)
{
    uint64_t Word = 0;

    for (int i = 7; i >= 0; i--)
        Word = Word << 8 | Bytes[i];
    return Word;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Put64 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Put64(uint64_t Word, uint8_t* Bytes)
{
    for (int i = 0; i < 8; i++)
        Bytes[i] = (uint8_t)(Word >> (8 * i));
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Field -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Bits First to First + Width - 1 of Word.
//
static uint64_t Field(uint64_t Word, int First, int Width)
{
    return Word >> First & ((1ull << Width) - 1);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Place -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Value, cut to Width bits, at bit First.
//
static uint64_t Place(uint64_t Value, int First, int Width)
{
    return (Value & ((1ull << Width) - 1)) << First;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= DtEthIp +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtEthIp_NumWords -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtEthIp_NumWords(int FrameSize, int Alignment)
{
    int Bytes = DT_ETHIP_HEADER_SIZE + FrameSize;
    int Unit = Alignment > DT_ETHIP_WORD_SIZE ? Alignment : DT_ETHIP_WORD_SIZE;

    return (Bytes + Unit - 1) / Unit * Unit / DT_ETHIP_WORD_SIZE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtEthIp_HeaderSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int DtEthIp_HeaderSize(int PacketType)
{
    return PacketType == DT_ETHIP_TYPE_TIMESTAMP ? DT_ETHIP_TIMESTAMP_HEADER_SIZE
                                                 : DT_ETHIP_HEADER_SIZE;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtEthIp_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtEthIp_Write(const DtEthIpFields* Header, uint8_t* Bytes)
{
    uint64_t Word = 0;

    if (Header->Jumbo)
    {
        int Padding = Header->NumWords * DT_ETHIP_WORD_SIZE -
                      DtEthIp_HeaderSize(Header->PacketType) - Header->FrameSize;

        Word |= Place(DT_ETHIP_SYNC_V2, 0, 16);
        Word |= Place((uint64_t)Header->NumWords, 16, 11);
        Word |= Place((uint64_t)Padding, 27, 8);
    }
    else
    {
        Word |= Place(DT_ETHIP_SYNC_V1, 0, 16);
        Word |= Place((uint64_t)Header->NumWords, 16, 8);
        Word |= Place((uint64_t)Header->FrameSize, 24, 11);
    }
    Word |= Place((uint64_t)(Header->IpAddressOffset / 4), 35, 5);
    Word |= Place((uint64_t)(Header->PortOffset / 4), 40, 8);
    Word |= Place((uint64_t)Header->Protocol, 48, 1);
    Word |= Place((uint64_t)Header->PacketType, 49, 2);
    Word |= Place((uint64_t)Header->SubStream, 51, 2);
    Word |= Place(Header->IpV4ChecksumError ? 1 : 0, 53, 1);
    Word |= Place(Header->UdpChecksumError ? 1 : 0, 54, 1);
    Word |= Place(Header->TcpChecksumError ? 1 : 0, 55, 1);
    Word |= Place(Header->TimestampRequest ? 1 : 0, 56, 1);
    Word |= Place(Header->TimestampValid ? 1 : 0, 57, 1);
    Word |= Place((uint64_t)Header->Fingerprint, 58, 6);
    Put64(Word, Bytes);

    uint64_t Tod = Place(Header->Nanoseconds, 0, 30) | Place(Header->Seconds, 32, 32);
    Put64(Tod, Bytes + 8);
    Bytes[16] = 0;
    Bytes[17] = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtEthIp_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool DtEthIp_Read(const uint8_t* Bytes, DtEthIpFields* Header)
{
    uint64_t Word = Get64(Bytes);
    uint64_t Tod = Get64(Bytes + 8);
    uint64_t Sync = Field(Word, 0, 16);

    memset(Header, 0, sizeof(*Header));
    Header->IpAddressOffset = (int)Field(Word, 35, 5) * 4;
    Header->PortOffset = (int)Field(Word, 40, 8) * 4;
    Header->Protocol = (int)Field(Word, 48, 1);
    Header->PacketType = (int)Field(Word, 49, 2);
    Header->SubStream = (int)Field(Word, 51, 2);
    Header->IpV4ChecksumError = Field(Word, 53, 1) != 0;
    Header->UdpChecksumError = Field(Word, 54, 1) != 0;
    Header->TcpChecksumError = Field(Word, 55, 1) != 0;
    Header->TimestampRequest = Field(Word, 56, 1) != 0;
    Header->TimestampValid = Field(Word, 57, 1) != 0;
    Header->Fingerprint = (int)Field(Word, 58, 6);
    Header->Nanoseconds = (uint32_t)Field(Tod, 0, 30);
    Header->Seconds = (uint32_t)Field(Tod, 32, 32);

    int HeaderSize = DtEthIp_HeaderSize(Header->PacketType);
    if (Sync == DT_ETHIP_SYNC_V1)
    {
        Header->NumWords = (int)Field(Word, 16, 8);
        Header->FrameSize = (int)Field(Word, 24, 11);
    }
    else if (Sync == DT_ETHIP_SYNC_V2)
    {
        Header->Jumbo = true;
        Header->NumWords = (int)Field(Word, 16, 11);
        Header->FrameSize =
            Header->NumWords * DT_ETHIP_WORD_SIZE - HeaderSize - (int)Field(Word, 27, 8);
    }
    else
        return false;

    return Header->FrameSize >= 0 &&
           HeaderSize + Header->FrameSize <= Header->NumWords * DT_ETHIP_WORD_SIZE;
}

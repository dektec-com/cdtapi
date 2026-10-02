// #*#*#*#*#*#*#*#*#*#*#*#*#*#* DtNmosAddr.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - IP addresses as an SDP writes them, read into bytes and written from them
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The parser and the writer are the bridge's own rather than the platform's inet_pton
// and inet_ntop, so that they read and write alike on every platform and need no socket
// library.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>
#include <string.h>

// CDTAPI includes
#include "DtNmosAddr.h"       // Interface being implemented.
#include "cdtapi_constants.h" // Result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HexValue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The value of a hexadecimal digit, or -1 for another character.
//
static int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsDigit -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IsNameChar -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A character of a domain name: a letter, a digit, a hyphen or a dot.
//
static bool IsNameChar(char c)
{
    return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
           c == '.';
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseIpV4 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the text from Text up to End, which must be a dotted IPv4 address and nothing
// more, into Ip[0..3].
//
static bool ParseIpV4(const char* Text, const char* End, uint8_t* Ip)
{
    const char* p = Text;
    for (int Part = 0; Part < 4; Part++)
    {
        if (Part > 0)
        {
            if (p >= End || *p != '.')
                return false;
            p++;
        }
        const char* Start = p;
        int Value = 0;
        while (p < End && IsDigit(*p) && p - Start < 3)
        {
            Value = Value * 10 + (*p - '0');
            p++;
        }
        if (p == Start || (p < End && IsDigit(*p)) || Value > 255 ||
            (p - Start > 1 && *Start == '0'))
        {
            return false;
        }
        Ip[Part] = (uint8_t)Value;
    }
    return p == End;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ParseIpV6 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Reads the text from Text up to End, which must be an IPv6 address and nothing more,
// into Ip[0..15]: up to eight groups, one "::" standing for the zero groups that are left
// out, and an IPv4 address as the last two groups.
//
static bool ParseIpV6(const char* Text, const char* End, uint8_t* Ip)
{
    const char* p = Text;
    uint8_t Bytes[16];
    int Count = 0; // Bytes read, two a group
    int Gap = -1;  // Where "::" stands, in bytes, or -1

    if (p < End && *p == ':')
    {
        if (p + 1 == End || p[1] != ':')
            return false;
        Gap = 0;
        p += 2;
    }
    while (p < End)
    {
        const char* q = p;
        while (q < End && HexValue(*q) >= 0)
            q++;

        // The last two groups as an IPv4 address.
        if (q < End && *q == '.')
        {
            if (Count > 12 || !ParseIpV4(p, End, Bytes + Count))
                return false;
            Count += 4;
            break;
        }
        if (q - p < 1 || q - p > 4 || Count == 16)
            return false;
        unsigned Value = 0;
        for (; p < q; p++)
            Value = Value * 16 + (unsigned)HexValue(*p);
        Bytes[Count++] = (uint8_t)(Value >> 8);
        Bytes[Count++] = (uint8_t)Value;
        if (p == End)
            break;
        if (*p != ':' || p + 1 == End)
            return false;
        p++;
        if (*p == ':')
        {
            if (Gap >= 0)
                return false;
            Gap = Count;
            p++;
        }
    }
    if ((Gap < 0 && Count != 16) || (Gap >= 0 && Count > 14))
        return false;

    // The groups before the gap, zeros for it, and the groups after it.
    memset(Ip, 0, 16);
    if (Gap < 0)
        Gap = Count;
    memcpy(Ip, Bytes, (size_t)Gap);
    memcpy(Ip + 16 - (Count - Gap), Bytes + Gap, (size_t)(Count - Gap));
    return true;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Addresses +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAddr_Format -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// RFC 5952: hexadecimal in lower case without leading zeros, the longest run of two or
// more zero groups as "::", the first of runs as long, and an IPv4-mapped address with
// its IPv4 address dotted.
//
DtapiResult DtNmosAddr_Format(bool IpV6, const uint8_t Ip[16], char* Text, size_t Size)
{
    char Written[DT_NMOS_ADDR_SIZE];
    static const uint8_t Mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    if (!IpV6)
    {
        snprintf(Written, sizeof(Written), "%u.%u.%u.%u", Ip[0], Ip[1], Ip[2], Ip[3]);
    }
    else if (memcmp(Ip, Mapped, sizeof(Mapped)) == 0)
    {
        snprintf(Written, sizeof(Written), "::ffff:%u.%u.%u.%u", Ip[12], Ip[13], Ip[14],
                 Ip[15]);
    }
    else
    {
        unsigned Groups[8];
        for (int i = 0; i < 8; i++)
            Groups[i] = (unsigned)Ip[2 * i] << 8 | Ip[2 * i + 1];

        // The longest run of zero groups, if it is two or more.
        int RunStart = -1;
        int RunLength = 1;
        for (int i = 0; i < 8; i++)
        {
            int Length = 0;
            while (i + Length < 8 && Groups[i + Length] == 0)
                Length++;
            if (Length > RunLength)
            {
                RunStart = i;
                RunLength = Length;
            }
        }

        size_t Out = 0;
        for (int i = 0; i < 8; i++)
        {
            if (i == RunStart)
            {
                Out += (size_t)snprintf(Written + Out, sizeof(Written) - Out, "::");
                i += RunLength - 1;
                continue;
            }
            const char* Separator = i > 0 && i != RunStart + RunLength ? ":" : "";
            Out += (size_t)snprintf(Written + Out, sizeof(Written) - Out, "%s%x",
                                    Separator, Groups[i]);
        }
    }
    if (strlen(Written) + 1 > Size)
        return DTAPI_E_BUF_TOO_SMALL;
    memcpy(Text, Written, strlen(Written) + 1);
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtNmosAddr_Parse -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtNmosAddr_Parse(const char* Text, uint8_t Ip[16], bool* IpV6)
{
    memset(Ip, 0, 16);
    *IpV6 = false;
    if (Text == NULL || Text[0] == '\0')
        return DTAPI_E_INVALID_ARG;
    if (strchr(Text, ':') != NULL)
    {
        *IpV6 = true;
        return ParseIpV6(Text, Text + strlen(Text), Ip) ? DTAPI_OK : DTAPI_E_INVALID_ARG;
    }

    // Digits and dots are an IPv4 address or nothing; with a letter or a hyphen among
    // them they are a domain name.
    bool Dotted = true;
    for (const char* p = Text; *p != '\0'; p++)
    {
        if (!IsNameChar(*p))
            return DTAPI_E_INVALID_ARG;
        if (!IsDigit(*p) && *p != '.')
            Dotted = false;
    }
    if (!Dotted)
        return DTAPI_E_NOT_SUPPORTED;
    return ParseIpV4(Text, Text + strlen(Text), Ip) ? DTAPI_OK : DTAPI_E_INVALID_ARG;
}

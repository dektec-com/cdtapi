// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# Sim4k.h #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - How the emulated card lays 2160p over one link out, in symbols
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= The 4K layout +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// A 2160p signal over one 6G or 12G link carries four 1080p links. For each line of
// those links, the card's ring holds two coded lines:
//
//   first       the HANC sections of links 1 and 2, then a video section
//   second      the HANC sections of links 3 and 4, then a video section
//
// The raw line the channels carry holds the eight streams of the four links, interleaved
// word by word, in this order: the chrominance of links 4, 2, 3 and 1, then their
// luminance. In a video section, the links take turns by whole pixel pairs on a picture
// line. On a blanking line, each link has a half of its own.
//
// The emulator works this layout out itself, from plan 0014, rather than use the
// library's conversion. The tests then check one against the other.
//
// HancSyms is the number of symbols in one HANC section. ActiveSyms is the number in one
// link's active part, which is half a video section. A raw line holds
// 4*HancSyms + 4*ActiveSyms symbols, and each coded line 2*HancSyms + 2*ActiveSyms.

// Returns where symbol Index of link Link's line lies in the raw line. Both count from 0.
size_t Sim4k_RawAt(int Link, int Index);

// Splits a raw line into the ring's two coded lines. Blanking is true for a blanking
// line.
void Sim4k_Split(int HancSyms, int ActiveSyms, bool Blanking, const uint16_t* Raw,
                 uint16_t* CodedA, uint16_t* CodedB);

// Joins the ring's two coded lines into a raw line: the reverse of Sim4k_Split.
void Sim4k_Merge(int HancSyms, int ActiveSyms, bool Blanking, const uint16_t* CodedA,
                 const uint16_t* CodedB, uint16_t* Raw);

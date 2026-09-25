// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtVidStdList.inc *#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Every video standard with its properties, as an X-macro list
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Each entry is X(Name, FpsNum, FpsDen, NumLines, Scan, LineNumSymHanc, IsLevelB, IoStd,
// OneLinkVidStd):
//
//   Name           the DTAPI_VIDSTD_ macro name without its prefix
//   FpsNum         the frame rate as a reduced fraction; an interlaced standard is named
//   FpsDen         after its field rate, so 1080i50 has 25 frames per second
//   NumLines       the lines of a frame: 525, 625, 750 or 1125
//   Scan           P for progressive, I for interlaced, PSF for progressive segmented
//                  frames, as DT_SCAN_ values
//   LineNumSymHanc the symbols per line in the horizontal blanking, EAV and SAV not
//                  included: the samples of a whole line times two, less the active
//                  symbols, EAV and SAV
//   IsLevelB       true for 3G level B, and for the 2160p standards made of level-B links
//   IoStd          the I/O standard that carries it, a DTAPI_IOCONFIG_ name without its
//                  prefix
//   OneLinkVidStd  for a 2160p standard the 1080p standard of one of its links, whose
//                  frame the standard's frame properties describe; UNKNOWN for the others
//
// Including this file with X defined expands the list:
//
//     #define X(Name, FpsNum, FpsDen, NumLines, Scan, LineNumSymHanc, IsLevelB, IoStd, \
//               OneLinkVidStd) {Name},
//     #include "Tables/DtVidStdList.inc"
//     #undef X
//
// The order is the order in which DtFrameProps_Deduce tries the standards. It decides
// results: the first match wins, so a 1080p standard is found before the 2160p standard
// with the same geometry, and a level-A 2160p before its level-B form.

X(525I59_94, 30000, 1001, 525, I, 268, false, SDI, UNKNOWN)
X(625I50, 25, 1, 625, I, 280, false, SDI, UNKNOWN)
X(720P23_98, 24000, 1001, 750, P, 5666, false, HDSDI, UNKNOWN)
X(720P24, 24, 1, 750, P, 5666, false, HDSDI, UNKNOWN)
X(720P25, 25, 1, 750, P, 5336, false, HDSDI, UNKNOWN)
X(720P29_97, 30000, 1001, 750, P, 4016, false, HDSDI, UNKNOWN)
X(720P30, 30, 1, 750, P, 4016, false, HDSDI, UNKNOWN)
X(720P50, 50, 1, 750, P, 1376, false, HDSDI, UNKNOWN)
X(720P59_94, 60000, 1001, 750, P, 716, false, HDSDI, UNKNOWN)
X(720P60, 60, 1, 750, P, 716, false, HDSDI, UNKNOWN)
X(1080P23_98, 24000, 1001, 1125, P, 1636, false, HDSDI, UNKNOWN)
X(1080P24, 24, 1, 1125, P, 1636, false, HDSDI, UNKNOWN)
X(1080P25, 25, 1, 1125, P, 1416, false, HDSDI, UNKNOWN)
X(1080P29_97, 30000, 1001, 1125, P, 536, false, HDSDI, UNKNOWN)
X(1080P30, 30, 1, 1125, P, 536, false, HDSDI, UNKNOWN)
X(1080PSF23_98, 24000, 1001, 1125, PSF, 1636, false, HDSDI, UNKNOWN)
X(1080PSF24, 24, 1, 1125, PSF, 1636, false, HDSDI, UNKNOWN)
X(1080PSF25, 25, 1, 1125, PSF, 1416, false, HDSDI, UNKNOWN)
X(1080PSF29_97, 30000, 1001, 1125, PSF, 536, false, HDSDI, UNKNOWN)
X(1080PSF30, 30, 1, 1125, PSF, 536, false, HDSDI, UNKNOWN)
X(1080I50, 25, 1, 1125, I, 1416, false, HDSDI, UNKNOWN)
X(1080I59_94, 30000, 1001, 1125, I, 536, false, HDSDI, UNKNOWN)
X(1080I60, 30, 1, 1125, I, 536, false, HDSDI, UNKNOWN)
X(1080P50, 50, 1, 1125, P, 1416, false, 3GSDI, UNKNOWN)
X(1080P50B, 50, 1, 1125, P, 1416, true, 3GSDI, UNKNOWN)
X(1080P59_94, 60000, 1001, 1125, P, 536, false, 3GSDI, UNKNOWN)
X(1080P59_94B, 60000, 1001, 1125, P, 536, true, 3GSDI, UNKNOWN)
X(1080P60, 60, 1, 1125, P, 536, false, 3GSDI, UNKNOWN)
X(1080P60B, 60, 1, 1125, P, 536, true, 3GSDI, UNKNOWN)
X(2160P50, 50, 1, 1125, P, 1416, false, 12GSDI, 1080P50)
X(2160P50B, 50, 1, 1125, P, 1416, true, 12GSDI, 1080P50B)
X(2160P59_94, 60000, 1001, 1125, P, 536, false, 12GSDI, 1080P59_94)
X(2160P59_94B, 60000, 1001, 1125, P, 536, true, 12GSDI, 1080P59_94B)
X(2160P60, 60, 1, 1125, P, 536, false, 12GSDI, 1080P60)
X(2160P60B, 60, 1, 1125, P, 536, true, 12GSDI, 1080P60B)
X(2160P23_98, 24000, 1001, 1125, P, 1636, false, 6GSDI, 1080P23_98)
X(2160P24, 24, 1, 1125, P, 1636, false, 6GSDI, 1080P24)
X(2160P25, 25, 1, 1125, P, 1416, false, 6GSDI, 1080P25)
X(2160P29_97, 30000, 1001, 1125, P, 536, false, 6GSDI, 1080P29_97)
X(2160P30, 30, 1, 1125, P, 536, false, 6GSDI, 1080P30)

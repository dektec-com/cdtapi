// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* SdiFormats.inc *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Every SDI video standard with its line timing, as an X-macro list for tests
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Each entry is F(Name, Lines, LinesF1, ActiveLines, Samples, Active, FpsNum, FpsDen,
// Scan, Payload, Rate, NoVpid):
//
//   Lines, LinesF1  Lines in the frame and in its first field. SMPTE ST 125 and BT.656
//                   give 262 + 263 and 312 + 313, SMPTE ST 274 gives 563 + 562; a
//                   progressive frame is one field.
//   ActiveLines     Lines with picture: 487 for 525, 576 for 625, 720 and 1080.
//   Samples, Active Luma samples in a whole line and in its active part, from the line
//                   timing of SMPTE ST 125 (858), BT.656 (864), ST 296 (1650, 1980, 3300,
//                   3960, 4125) and ST 274 (2200, 2640, 2750). The receiver counts luma
//                   and chroma, so a line has 2 * (Samples - Active) symbols of horizontal
//                   blanking, EAV and SAV included, and 2 * Active of picture.
//   FpsNum, FpsDen  Frames per second; an interlaced standard is named after its field
//                   rate.
//   Scan            P progressive, I interlaced, S PsF (progressive picture, interlaced
//                   transport).
//   Payload         The SMPTE ST 352 payload identifier, byte 1, for the way the test
//                   carries the standard: the 2160p standards are on 6G (ST 2081-10) or
//                   12G (ST 2082-10), and the level-B ones on four 3G links (ST 425-5).
//   Rate            The SDI rate that carries it; a 2160p link carries a 1080p frame.
//   NoVpid          The standard DTAPI finds from the counters alone: it cannot see PsF
//                   at 25 frames and up, and does not see four links.
//
// No include guard: the file is meant to be included more than once.

F(525I59_94, 525, 262, 487, 858, 720, 30000, 1001, I, 0x81, SD, 525I59_94)
F(625I50, 625, 312, 576, 864, 720, 25, 1, I, 0x81, SD, 625I50)
F(720P23_98, 750, 750, 720, 4125, 1280, 24000, 1001, P, 0x84, HD, 720P23_98)
F(720P24, 750, 750, 720, 4125, 1280, 24, 1, P, 0x84, HD, 720P24)
F(720P25, 750, 750, 720, 3960, 1280, 25, 1, P, 0x84, HD, 720P25)
F(720P29_97, 750, 750, 720, 3300, 1280, 30000, 1001, P, 0x84, HD, 720P29_97)
F(720P30, 750, 750, 720, 3300, 1280, 30, 1, P, 0x84, HD, 720P30)
F(720P50, 750, 750, 720, 1980, 1280, 50, 1, P, 0x84, HD, 720P50)
F(720P59_94, 750, 750, 720, 1650, 1280, 60000, 1001, P, 0x84, HD, 720P59_94)
F(720P60, 750, 750, 720, 1650, 1280, 60, 1, P, 0x84, HD, 720P60)
F(1080P23_98, 1125, 1125, 1080, 2750, 1920, 24000, 1001, P, 0x85, HD, 1080P23_98)
F(1080P24, 1125, 1125, 1080, 2750, 1920, 24, 1, P, 0x85, HD, 1080P24)
F(1080P25, 1125, 1125, 1080, 2640, 1920, 25, 1, P, 0x85, HD, 1080P25)
F(1080P29_97, 1125, 1125, 1080, 2200, 1920, 30000, 1001, P, 0x85, HD, 1080P29_97)
F(1080P30, 1125, 1125, 1080, 2200, 1920, 30, 1, P, 0x85, HD, 1080P30)
F(1080PSF23_98, 1125, 563, 1080, 2750, 1920, 24000, 1001, S, 0x85, HD, 1080PSF23_98)
F(1080PSF24, 1125, 563, 1080, 2750, 1920, 24, 1, S, 0x85, HD, 1080PSF24)
F(1080PSF25, 1125, 563, 1080, 2640, 1920, 25, 1, S, 0x85, HD, 1080I50)
F(1080PSF29_97, 1125, 563, 1080, 2200, 1920, 30000, 1001, S, 0x85, HD, 1080I59_94)
F(1080PSF30, 1125, 563, 1080, 2200, 1920, 30, 1, S, 0x85, HD, 1080I60)
F(1080I50, 1125, 563, 1080, 2640, 1920, 25, 1, I, 0x85, HD, 1080I50)
F(1080I59_94, 1125, 563, 1080, 2200, 1920, 30000, 1001, I, 0x85, HD, 1080I59_94)
F(1080I60, 1125, 563, 1080, 2200, 1920, 30, 1, I, 0x85, HD, 1080I60)
F(1080P50, 1125, 1125, 1080, 2640, 1920, 50, 1, P, 0x89, 3G, 1080P50)
F(1080P50B, 1125, 1125, 1080, 2640, 1920, 50, 1, P, 0x8A, 3G, 1080P50B)
F(1080P59_94, 1125, 1125, 1080, 2200, 1920, 60000, 1001, P, 0x89, 3G, 1080P59_94)
F(1080P59_94B, 1125, 1125, 1080, 2200, 1920, 60000, 1001, P, 0x8A, 3G, 1080P59_94B)
F(1080P60, 1125, 1125, 1080, 2200, 1920, 60, 1, P, 0x89, 3G, 1080P60)
F(1080P60B, 1125, 1125, 1080, 2200, 1920, 60, 1, P, 0x8A, 3G, 1080P60B)
F(2160P23_98, 1125, 1125, 1080, 2750, 1920, 24000, 1001, P, 0xC0, 6G, 2160P23_98)
F(2160P24, 1125, 1125, 1080, 2750, 1920, 24, 1, P, 0xC0, 6G, 2160P24)
F(2160P25, 1125, 1125, 1080, 2640, 1920, 25, 1, P, 0xC0, 6G, 2160P25)
F(2160P29_97, 1125, 1125, 1080, 2200, 1920, 30000, 1001, P, 0xC0, 6G, 2160P29_97)
F(2160P30, 1125, 1125, 1080, 2200, 1920, 30, 1, P, 0xC0, 6G, 2160P30)
F(2160P50, 1125, 1125, 1080, 2640, 1920, 50, 1, P, 0xCE, 12G, 2160P50)
F(2160P50B, 1125, 1125, 1080, 2640, 1920, 50, 1, P, 0x98, 3G, 1080P50B)
F(2160P59_94, 1125, 1125, 1080, 2200, 1920, 60000, 1001, P, 0xCE, 12G, 2160P59_94)
F(2160P59_94B, 1125, 1125, 1080, 2200, 1920, 60000, 1001, P, 0x98, 3G, 1080P59_94B)
F(2160P60, 1125, 1125, 1080, 2200, 1920, 60, 1, P, 0xCE, 12G, 2160P60)
F(2160P60B, 1125, 1125, 1080, 2200, 1920, 60, 1, P, 0x98, 3G, 1080P60B)

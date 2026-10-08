// #*#*#*#*#*#*#*#*#*#*#*#*#* ExamplePattern.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Examples: the test pattern and test tone that the sending examples send
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The pattern is an image in planar 4:2:2 with 10-bit samples, of any size:
//   - the upper two thirds: a grey scale of seven bars from black to white, with a white
//     bar a sixteenth of the width wide that moves 16 pixels to the right each frame;
//   - the lower third: the 75% colour bars of SMPTE RP 219, in the colours of ITU-R
//     BT.709, or of BT.601 for an image of 576 lines or fewer;
//   - near the top left corner, in a black box: the frame number as a code of blocks that
//     a program can read back, and below it as eight digits.
//
// ExamplePattern_Init draws the whole image once. ExamplePattern_Draw then draws only
// what changes from one frame to the next: the moving bar and the box. So a frame costs
// about the same in SD as in 2160p, little enough for any example to keep up.
//
// The code holds the frame number's lower 32 bits as a row of 36 blocks, each a light
// block for a 1 and a dark block for a 0: first the two blocks 1 and 0, then the 32 bits,
// most significant first, then the two blocks 0 and 1. The blocks are several pixels wide
// and several lines high, so that the code survives interlace, 8-bit samples and a
// scaler. The marks at both ends tell a code from an image that has none.

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// The test pattern of one image size.
typedef struct ExamplePattern
{
    int Width;       // Pixels per line, even
    int Height;      // Lines per image
    uint16_t* Y;     // Width luma samples per line, line after line
    uint16_t* Cb;    // Width / 2 blue colour difference samples per line
    uint16_t* Cr;    // Width / 2 red colour difference samples per line
    uint16_t* GreyY; // One line of the grey scale's luma, to restore where the bar was
    int BarX;        // Where the moving bar is; -1 before the first frame
} ExamplePattern;

// A rectangle of the image.
typedef struct ExamplePatternArea
{
    int X;     // The left edge, in pixels
    int Y;     // The top line
    int Width; // Pixels
    int Lines; // Lines
} ExamplePatternArea;

// Where the test tone is. A zeroed ExampleTone starts at its beginning.
typedef struct ExampleTone
{
    int64_t Sample; // The next sample
} ExampleTone;

// Sets up *Pattern for images of Width by Height pixels and draws the image without the
// moving bar and the box. Width must be even, and the image at least 320 by 240 pixels,
// so that the box fits in the grey scale. Returns false when the size is not supported
// or there is not enough memory; *Pattern then holds nothing to free.
bool ExamplePattern_Init(ExamplePattern* Pattern, int Width, int Height);

// Frees what ExamplePattern_Init allocated. A zeroed *Pattern is freed too.
void ExamplePattern_Free(ExamplePattern* Pattern);

// Makes the image that of frame Number: moves the bar to its place in that frame and
// draws the box with the number.
void ExamplePattern_Draw(ExamplePattern* Pattern, int64_t Number);

// Fills Areas with the parts where the image differs from the one ExamplePattern_Init
// drew: the moving bar and the box. Returns their number: 0 before the first
// ExamplePattern_Draw, else 2. A program that keeps that first image in its own format
// converts only these parts again for each frame.
int ExamplePattern_Changes(const ExamplePattern* Pattern, ExamplePatternArea Areas[2]);

// Returns the image line, from 0, that runs through the middle of the code in an image of
// Height lines. A receiver reads this line and gives it to ExamplePattern_ReadNumber.
int ExamplePattern_CodeLine(int Height);

// Reads the frame number from Luma, the luma samples of the code's line of an image Width
// pixels wide, as 10-bit values. An 8-bit sample is shifted up by two first. Returns
// false when the line holds no code; *Number is then 0.
bool ExamplePattern_ReadNumber(const uint16_t* Luma, int Width, uint32_t* Number);

// Writes the next Count samples of the test tone to Samples: a 1 kHz sine at -20 dBFS,
// at 48 kHz, continuing where the last call with Tone stopped. Each sample is an
// int32_t with its 24 bits at the top.
void ExampleTone_Next(ExampleTone* Tone, int32_t* Samples, int Count);

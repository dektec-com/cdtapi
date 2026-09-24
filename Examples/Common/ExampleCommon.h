// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleCommon.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the example programs share: the API header, arguments, ports, names
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// The API.
#include "cdtapi.h"

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Exit codes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

#define EXAMPLE_OK 0      // Did what was asked
#define EXAMPLE_FAILED 1  // An API call failed, or the command line was wrong
#define EXAMPLE_NOTHING 2 // Ran, but found nothing, such as no signal

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Arguments +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Options are "--name value" or a lone "--name". A program lists the options it knows;
// anything else on the command line is an error.
//

typedef struct ExampleOption
{
    const char* Name; // Such as "--port"
    bool TakesValue;  // Followed by a value
    const char* Help; // One line for the usage text
} ExampleOption;

// Checks that every argument is a known option, with a value where it takes one. Returns
// false after printing what is wrong when the command line is not valid, and after
// printing Usage and the options when it asks for --help, which every program knows.
bool Example_CheckArguments(int Argc, char** Argv, const char* Usage,
                            const ExampleOption* Options, int NumOptions);

// True when the lone option Name is on the command line.
bool Example_HasFlag(int Argc, char** Argv, const char* Name);

// The value of option Name, or NULL when it is not given.
const char* Example_Value(int Argc, char** Argv, const char* Name);

// Reads option Name as a decimal integer into *Value, which is left alone when the option
// is not given. Prints the problem and returns false for a value that is no integer.
bool Example_Int64(int Argc, char** Argv, const char* Name, int64_t* Value);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Ports +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// What a program needs of a port it picks.
typedef bool (*ExampleSuits)(const DtHwFuncDesc* Port);

// Scans the hardware functions and returns in *Found the first port that is on the
// device with this serial number, 0 for any; that has this number, 0 for any; and that
// suits, NULL for any port. Returns DTAPI_OK, DTAPI_E_NOT_FOUND when no port matches,
// DTAPI_E_OUT_OF_MEM, or the scan's failure.
unsigned int Example_FindPort(int64_t Serial, int Port, ExampleSuits Suits,
                              DtHwFuncDesc* Found);

// Prints "What: RESULT_NAME" for a failed call and returns EXAMPLE_FAILED.
int Example_Failed(const char* What, unsigned int Result);

// True for DTAPI_OK and the DTAPI_OK_ results that carry a warning.
bool Example_Succeeded(unsigned int Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Names +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The name of a video standard, its DTAPI_VIDSTD_ macro without the prefix, such as
// "1080I50", or "UNKNOWN"; NULL for a number that is no video standard.
const char* Example_VidStdName(int VidStd);

// The video standard with Name, compared without regard to case. False when there is
// none.
bool Example_VidStdFromName(const char* Name, int* VidStd);

// The name of an I/O standard value, such as "3GSDI" or "ASI"; "?" for any other.
const char* Example_IoStdName(int Value);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Sleeps for about Ms milliseconds.
void Example_SleepMs(int Ms);

// Milliseconds on a clock that only goes forward, from an arbitrary start.
int64_t Example_NowMs(void);

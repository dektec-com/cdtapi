// #*#*#*#*#*#*#*#*#*#*#*#*#*# ExampleCommon.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - What the example programs share: the API header, arguments, ports, names
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The API.
#include "cdtapi.h"
#include "cdtapi_service.h"

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

// Returns whether the option Name, which takes no value, is on the command line.
bool Example_HasFlag(int Argc, char** Argv, const char* Name);

// Returns the value of option Name, or NULL when it is not given.
const char* Example_Value(int Argc, char** Argv, const char* Name);

// Reads option Name as a decimal integer into *Value, which is left alone when the option
// is not given. Prints the problem and returns false for a value that is not an integer.
bool Example_Int64(int Argc, char** Argv, const char* Name, int64_t* Value);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Ports +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// A program's test of whether a port can do what it needs, e.g. be an SDI input.
typedef bool (*ExampleSuits)(const DtHwFuncDesc* Port);

// Finds the port a program will use: the first port, in the order of DtapiHwFuncScan(),
// on the card with serial number Serial (0: any card), with number Port (0: any port),
// for which Suits returns true (NULL: any port). Fills *Found with its description.
//
// Returns DTAPI_OK, DTAPI_E_NOT_FOUND when no port matches, DTAPI_E_OUT_OF_MEM, or the
// error of the scan.
unsigned int Example_FindPort(int64_t Serial, int Port, ExampleSuits Suits,
                              DtHwFuncDesc* Found);

// Prints "What: RESULT_NAME" for a failed call and returns EXAMPLE_FAILED.
int Example_Failed(const char* What, unsigned int Result);

// Returns whether Result is a success: DTAPI_OK, or a DTAPI_OK_ result with a warning.
bool Example_Succeeded(unsigned int Result);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Names +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// Returns the name of a video standard: its DTAPI_VIDSTD_ macro without the prefix, e.g.
// "1080I50", or "UNKNOWN". Returns NULL for a number that is not a video standard.
const char* Example_VidStdName(int VidStd);

// Sets *VidStd to the video standard called Name, ignoring case, e.g. "1080i50". Returns
// false when there is no such standard.
bool Example_VidStdFromName(const char* Name, int* VidStd);

// Returns the name of an I/O standard value, e.g. "3GSDI" or "ASI", or "?" for another.
const char* Example_IoStdName(int Value);

// Returns the name of a PTP slave's lock, its DT_PTP_LOCK_ macro without the prefix,
// e.g. "LOCKED", or "?" for another value.
const char* Example_PtpLockName(int Lock);

// Returns the name of a PTP slave's state, its DT_PTP_SLAVE_ macro without the
// prefix, e.g. "SLAVE", or "?" for another value.
const char* Example_PtpStateName(int State);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Sending +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// How a program starts sending SDI, and starts again after an underflow, in every
// standard. It holds, puts a few frames into the buffer, and then sends. When the card
// runs out of data anyway, the program clears the FIFO, holds, and starts again in the
// same way. The card goes on on a grid of its own after an underflow, and in 3G level B
// it might not put the next picture in the field it was built for; after a new start the
// first frame is field 2 again.
//
// Which flags mean that the card ran out depends on how the program writes. A program
// that writes its frames gets a black frame from the channel when it is late, and
// DTAPI_TX_FIFO_UFL with it, but the card goes on; only DTAPI_TX_DMA_UFL is an underflow
// then. A program that lends its frames gets no black frames, so DTAPI_TX_FIFO_UFL means
// the card ran out as well.
//

// The state of a program that sends.
typedef struct ExampleSender
{
    DtOutpChannel* Channel;
    const char* Name;        // The port's name, for the lines printed
    int FramesBeforeSending; // The frames put in before the card starts
    int Held;                // The frames put in since the channel held
    bool Sending;            // The card has been told to send
    int Restarts;            // The new starts after an underflow
    int Underflows;          // The latched flags that mean the card ran out
} ExampleSender;

// Holds Channel, whose port is called Name, to put FramesBeforeSending frames into the
// buffer before it sends. Lends says that the program lends its frames, which makes
// DTAPI_TX_FIFO_UFL an underflow as well as DTAPI_TX_DMA_UFL. Returns what
// DtOutpChannel_SetTxControl() returns.
unsigned int ExampleSender_Start(ExampleSender* Sender, DtOutpChannel* Channel,
                                 const char* Name, int FramesBeforeSending, bool Lends);

// Counts a frame written or committed. Starts sending once enough frames are in; while
// sending, starts again after an underflow, with a line that says so. Returns
// DTAPI_OK or the error of the call that failed.
unsigned int ExampleSender_Wrote(ExampleSender* Sender);

// Starts sending what the buffer holds, when the program wrote fewer frames since it last
// held than ExampleSender_Start() asked for. Returns DTAPI_OK or the error of the call.
unsigned int ExampleSender_Finish(ExampleSender* Sender);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Time +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Sleeps for about Ms milliseconds.
void Example_SleepMs(int Ms);

// Returns the time in milliseconds on a clock that never goes back, from an arbitrary
// start; for measuring how long something takes.
int64_t Example_NowMs(void);

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Hashes +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// Returns a 64-bit hash of Size bytes of Data, to compare frames by. The data's 64-bit
// words, each read least significant byte first, go round four FNV-1a hashes in turn:
// word 0 to the first, word 1 to the second, and so on. The bytes after the last whole
// word go to the first. The four are then hashed together, as four words. Every program
// that prints a frame's hash uses this one, so that the lines of a sending and a
// receiving program can be compared.
uint64_t Example_Hash(const void* Data, size_t Size);

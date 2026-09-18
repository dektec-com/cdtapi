// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtAvError.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The text of a thread's last failure in the AV FIFO
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>

// CDTAPI includes
#include "DtAvError.h"     // Interface being implemented.
#include "cdtapi_avfifo.h" // GetLastException.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Failures +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// MSVC's C compiler takes the thread storage of its own spelling; the others C11's.
#if defined(_MSC_VER)
    #define DT_THREAD_LOCAL __declspec(thread)
#else
    #define DT_THREAD_LOCAL _Thread_local
#endif

static DT_THREAD_LOCAL char g_LastFailure[DT_AV_ERROR_SIZE];

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtAvError_Set -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtAvError_Set(DtapiResult Result, const char* Where, const char* What)
{
    snprintf(g_LastFailure, sizeof(g_LastFailure), "%s: %s (%s)", Where, What,
             DtapiResult2Str(Result));
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetLastException -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const char* GetLastException(void)
{
    return g_LastFailure;
}

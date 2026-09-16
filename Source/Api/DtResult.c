// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtResult.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Names of result codes
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>

// CDtapiLite includes
#include "CDtapiLite.h" // Public API and the result codes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Names +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct ResultName
{
    unsigned int Code;
    const char* Name;
} ResultName;

static const ResultName g_ResultNames[] = {
#define X(Name) {Name, #Name},
#include "Tables/DtResultList.inc"
#undef X
};

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtapiResult2Str -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A linear search over some three hundred entries, as a name is looked up for a message
// and not in a loop.
//
const char* DtapiResult2Str(unsigned int Result)
{
    size_t i;

    for (i = 0; i < sizeof(g_ResultNames) / sizeof(g_ResultNames[0]); i++)
    {
        if (g_ResultNames[i].Code == Result)
            return g_ResultNames[i].Name;
    }
    return "???";
}

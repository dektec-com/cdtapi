// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtTest.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Minimal assertion and test-runner header used by every test
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
    #include <crtdbg.h>
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Test framework +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// CDtapiLite has no dependencies, and a unit-test framework is not worth making an
// exception for: gtest and Catch2 would each pull in C++ and a package manager. This
// header is the whole framework.
//
// A test file declares cases with DT_TEST and lists them in DT_TEST_MAIN. The list is
// written out rather than collected automatically, because the usual trick for that is
// a constructor attribute, which MSVC does not have. Listing the cases costs one line
// each and works on every compiler.
//
// A failing assertion reports its location, counts the failure and returns from the
// case, so one broken case does not hide the ones after it. The process exit code is
// zero only when every case passed, which is what CTest reads.
//
// Usage:
//
//     DT_TEST(VersionIsNotEmpty)
//     {
//         DT_ASSERT(DtapiLiteGetVersion()[0] != '\0');
//     }
//
//     DT_TEST_MAIN("Api", DT_RUN(VersionIsNotEmpty))
//

// Declares one test case. The body follows this macro.
#define DT_TEST(Name) static void Name(int* DtFailures)

#define DT_FAIL(...)                                                                     \
    do                                                                                   \
    {                                                                                    \
        printf("    FAIL %s:%d: ", __FILE__, __LINE__);                                  \
        printf(__VA_ARGS__);                                                             \
        printf("\n");                                                                    \
        (*DtFailures)++;                                                                 \
        return;                                                                          \
    } while (0)

#define DT_ASSERT(Cond)                                                                  \
    do                                                                                   \
    {                                                                                    \
        if (!(Cond))                                                                     \
            DT_FAIL("expected %s", #Cond);                                               \
    } while (0)

#define DT_ASSERT_EQ(Actual, Expected)                                                   \
    do                                                                                   \
    {                                                                                    \
        int64_t DtA = (int64_t)(Actual);                                                 \
        int64_t DtE = (int64_t)(Expected);                                               \
        if (DtA != DtE)                                                                  \
            DT_FAIL("%s: expected %" PRId64 ", got %" PRId64, #Actual, DtE, DtA);        \
    } while (0)

#define DT_ASSERT_OK(Result) DT_ASSERT_EQ((Result), 0)

#define DT_ASSERT_STR(Actual, Expected)                                                  \
    do                                                                                   \
    {                                                                                    \
        const char* DtA = (Actual);                                                      \
        const char* DtE = (Expected);                                                    \
        if (DtA == NULL || strcmp(DtA, DtE) != 0)                                        \
            DT_FAIL("%s: expected \"%s\", got \"%s\"", #Actual, DtE,                     \
                    DtA == NULL ? "(null)" : DtA);                                       \
    } while (0)

#define DT_ASSERT_MEM(Actual, Expected, Size)                                            \
    do                                                                                   \
    {                                                                                    \
        size_t DtN = (size_t)(Size);                                                     \
        if (memcmp((Actual), (Expected), DtN) != 0)                                      \
            DT_FAIL("%s: %zu bytes differ from %s", #Actual, DtN, #Expected);            \
    } while (0)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Test runner -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct DtTestCase
{
    const char* Name;
    void (*Func)(int* Failures);
} DtTestCase;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtTestSilenceDialogs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Sends failed CRT assertions and abort() to stderr instead of to a message box.
//
// A test process must never wait for someone to click OK. On Windows the debug CRT pops
// a dialog for a failed assertion, a detected heap corruption or an abort, and a run
// started by CTest or by CI has nobody to dismiss it: the run hangs until it times out,
// and the report says nothing about what actually went wrong.
//
static void DtTestSilenceDialogs(void)
{
#if defined(_MSC_VER)
    int Report;

    // _CRT_WARN, _CRT_ERROR and _CRT_ASSERT.
    for (Report = 0; Report < 3; Report++)
    {
        _CrtSetReportMode(Report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(Report, _CRTDBG_FILE_STDERR);
    }

    // Still report the fault to an attached debugger, but show no dialog.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

#define DT_RUN(Name) {#Name, Name}

#define DT_TEST_MAIN(SuiteName, ...)                                                     \
    int main(void)                                                                       \
    {                                                                                    \
        static const DtTestCase Cases[] = {__VA_ARGS__};                                 \
        const int NumCases = (int)(sizeof(Cases) / sizeof(Cases[0]));                    \
        int TotalFailures = 0;                                                           \
        DtTestSilenceDialogs();                                                          \
        printf("== %s: %d case(s)\n", SuiteName, NumCases);                              \
        for (int i = 0; i < NumCases; i++)                                               \
        {                                                                                \
            int Failures = 0;                                                            \
            Cases[i].Func(&Failures);                                                    \
            printf("  %-4s %s\n", Failures == 0 ? "ok" : "FAIL", Cases[i].Name);         \
            TotalFailures += Failures;                                                   \
        }                                                                                \
        printf("== %s: %s\n", SuiteName, TotalFailures == 0 ? "PASSED" : "FAILED");      \
        return TotalFailures == 0 ? 0 : 1;                                               \
    }

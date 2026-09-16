// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtlTest.h *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Minimal assertion and test-runner header used by every test
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef CDTAPILITE_DTL_TEST_H
#define CDTAPILITE_DTL_TEST_H

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Test framework +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// CDtapiLite has no dependencies, and a unit-test framework is not worth making an
// exception for: gtest and Catch2 would each pull in C++ and a package manager. This
// header is the whole framework.
//
// A test file declares cases with DTL_TEST and lists them in DTL_TEST_MAIN. The list is
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
//     DTL_TEST(VersionIsNotEmpty)
//     {
//         DTL_ASSERT(DtapiLiteGetVersion()[0] != '\0');
//     }
//
//     DTL_TEST_MAIN("Api", DTL_RUN(VersionIsNotEmpty))
//

// Declares one test case. The body follows this macro.
#define DTL_TEST(Name) static void Name(int* DtlFailures)

#define DTL_FAIL(...)                                                                    \
    do                                                                                   \
    {                                                                                    \
        printf("    FAIL %s:%d: ", __FILE__, __LINE__);                                  \
        printf(__VA_ARGS__);                                                             \
        printf("\n");                                                                    \
        (*DtlFailures)++;                                                                \
        return;                                                                          \
    } while (0)

#define DTL_ASSERT(Cond)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if (!(Cond))                                                                     \
            DTL_FAIL("expected %s", #Cond);                                              \
    } while (0)

#define DTL_ASSERT_EQ(Actual, Expected)                                                  \
    do                                                                                   \
    {                                                                                    \
        long long DtlA = (long long)(Actual);                                            \
        long long DtlE = (long long)(Expected);                                          \
        if (DtlA != DtlE)                                                                \
            DTL_FAIL("%s: expected %lld, got %lld", #Actual, DtlE, DtlA);                \
    } while (0)

#define DTL_ASSERT_OK(Result) DTL_ASSERT_EQ((Result), 0)

#define DTL_ASSERT_STR(Actual, Expected)                                                 \
    do                                                                                   \
    {                                                                                    \
        const char* DtlA = (Actual);                                                     \
        const char* DtlE = (Expected);                                                   \
        if (DtlA == NULL || strcmp(DtlA, DtlE) != 0)                                     \
            DTL_FAIL("%s: expected \"%s\", got \"%s\"", #Actual, DtlE,                   \
                     DtlA == NULL ? "(null)" : DtlA);                                    \
    } while (0)

#define DTL_ASSERT_MEM(Actual, Expected, Size)                                           \
    do                                                                                   \
    {                                                                                    \
        size_t DtlN = (size_t)(Size);                                                    \
        if (memcmp((Actual), (Expected), DtlN) != 0)                                     \
            DTL_FAIL("%s: %zu bytes differ from %s", #Actual, DtlN, #Expected);          \
    } while (0)

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Test runner -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

typedef struct DtlTestCase
{
    const char* Name;
    void (*Func)(int* Failures);
} DtlTestCase;

#define DTL_RUN(Name) {#Name, Name}

#define DTL_TEST_MAIN(SuiteName, ...)                                                    \
    int main(void)                                                                       \
    {                                                                                    \
        static const DtlTestCase Cases[] = {__VA_ARGS__};                                \
        const int NumCases = (int)(sizeof(Cases) / sizeof(Cases[0]));                    \
        int TotalFailures = 0;                                                           \
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

#endif // CDTAPILITE_DTL_TEST_H

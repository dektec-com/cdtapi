// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestStr.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the growable string
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtlAlloc.h" // Allocation fault injection.
#include "Core/DtlStr.h"   // Interface under test.
#include "DtlTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(StartsEmptyAndSmall)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_EQ(DtlStrLength(&Str), 0);
    DTL_ASSERT_STR(DtlStrCStr(&Str), "");
    DTL_ASSERT(DtlStrIsSmall(&Str));

    DtlStrFree(&Str);
}

DTL_TEST(ShortTextStaysInTheStructure)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppend(&Str, "IODIR"));
    DTL_ASSERT_OK(DtlStrAppend(&Str, "/INPUT"));

    DTL_ASSERT_STR(DtlStrCStr(&Str), "IODIR/INPUT");
    DTL_ASSERT_EQ(DtlStrLength(&Str), 11);
    DTL_ASSERT(DtlStrIsSmall(&Str));

    DtlStrFree(&Str);
}

// The boundary is where a small-buffer optimisation goes wrong, so it is checked from
// both sides: one character below the capacity and one above it.
DTL_TEST(TransitionToHeapIsExact)
{
    DtlStr Str;
    size_t i;

    DtlStrInit(&Str);

    for (i = 0; i < DTL_STR_SMALL_CAPACITY - 1; i++)
        DTL_ASSERT_OK(DtlStrAppendChar(&Str, 'a'));

    DTL_ASSERT_EQ(DtlStrLength(&Str), DTL_STR_SMALL_CAPACITY - 1);
    DTL_ASSERT(DtlStrIsSmall(&Str));

    DTL_ASSERT_OK(DtlStrAppendChar(&Str, 'b'));
    DTL_ASSERT_EQ(DtlStrLength(&Str), DTL_STR_SMALL_CAPACITY);
    DTL_ASSERT(!DtlStrIsSmall(&Str));

    DTL_ASSERT_EQ(DtlStrCStr(&Str)[0], 'a');
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[DTL_STR_SMALL_CAPACITY - 1], 'b');
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[DTL_STR_SMALL_CAPACITY], '\0');

    DtlStrFree(&Str);
}

DTL_TEST(ContentsSurviveTheMoveToTheHeap)
{
    DtlStr Str;
    int i;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppend(&Str, "prefix:"));

    for (i = 0; i < 500; i++)
        DTL_ASSERT_OK(DtlStrAppendChar(&Str, 'z'));

    DTL_ASSERT_EQ(DtlStrLength(&Str), 7 + 500);
    DTL_ASSERT(!DtlStrIsSmall(&Str));
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[0], 'p');
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[6], ':');
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[7], 'z');

    DtlStrFree(&Str);
}

DTL_TEST(AppendLenHandlesEmbeddedNull)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppendLen(&Str, "ab\0cd", 5));

    DTL_ASSERT_EQ(DtlStrLength(&Str), 5);
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[2], '\0');
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[3], 'c');
    // The terminator is still written after the contents.
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[5], '\0');

    DtlStrFree(&Str);
}

DTL_TEST(AppendingNothingIsAllowed)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppend(&Str, ""));
    DTL_ASSERT_OK(DtlStrAppendLen(&Str, "ignored", 0));
    DTL_ASSERT_EQ(DtlStrLength(&Str), 0);

    DtlStrFree(&Str);
}

DTL_TEST(FormatAppendsAndReplaces)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppendFormat(&Str, "%lld:%d", 2175000123LL, 2));
    DTL_ASSERT_STR(DtlStrCStr(&Str), "2175000123:2");

    DTL_ASSERT_OK(DtlStrAppendFormat(&Str, " port %d", 7));
    DTL_ASSERT_STR(DtlStrCStr(&Str), "2175000123:2 port 7");

    DTL_ASSERT_OK(DtlStrSetFormat(&Str, "DTA-%d", 2178));
    DTL_ASSERT_STR(DtlStrCStr(&Str), "DTA-2178");

    DtlStrFree(&Str);
}

// vsnprintf is called twice, once to measure and once to write. If the second call were
// given an already-consumed argument list the result would be garbage, so a format long
// enough to force the heap is worth checking on its own.
DTL_TEST(FormatLongerThanTheSmallBuffer)
{
    DtlStr Str;
    size_t i;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrSetFormat(&Str, "%0*d", 200, 7));

    DTL_ASSERT_EQ(DtlStrLength(&Str), 200);
    DTL_ASSERT(!DtlStrIsSmall(&Str));
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[199], '7');
    for (i = 0; i < 199; i++)
        DTL_ASSERT_EQ(DtlStrCStr(&Str)[i], '0');

    DtlStrFree(&Str);
}

DTL_TEST(ClearKeepsTheStorage)
{
    DtlStr Str;
    int i;

    DtlStrInit(&Str);
    for (i = 0; i < 200; i++)
        DTL_ASSERT_OK(DtlStrAppendChar(&Str, 'q'));

    DTL_ASSERT(!DtlStrIsSmall(&Str));
    DtlStrClear(&Str);

    DTL_ASSERT_EQ(DtlStrLength(&Str), 0);
    DTL_ASSERT_STR(DtlStrCStr(&Str), "");
    // Still on the heap: clearing empties the contents, it does not undo the move.
    DTL_ASSERT(!DtlStrIsSmall(&Str));

    DtlStrFree(&Str);
}

DTL_TEST(FreeReturnsItToTheSmallBuffer)
{
    DtlStr Str;
    int i;

    DtlStrInit(&Str);
    for (i = 0; i < 300; i++)
        DTL_ASSERT_OK(DtlStrAppendChar(&Str, 'w'));

    DtlStrFree(&Str);
    DTL_ASSERT(DtlStrIsSmall(&Str));
    DTL_ASSERT_EQ(DtlStrLength(&Str), 0);

    // Usable again without re-initialising, and safe to free twice.
    DTL_ASSERT_OK(DtlStrAppend(&Str, "again"));
    DTL_ASSERT_STR(DtlStrCStr(&Str), "again");
    DtlStrFree(&Str);
    DtlStrFree(&Str);
}

DTL_TEST(ReserveMovesToTheHeapUpFront)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrReserve(&Str, 1000));
    DTL_ASSERT(!DtlStrIsSmall(&Str));
    DTL_ASSERT_EQ(DtlStrLength(&Str), 0);
    DTL_ASSERT_STR(DtlStrCStr(&Str), "");

    DtlStrFree(&Str);
}

DTL_TEST(BadArgumentsAreRejected)
{
    DtlStr Str;

    DtlStrInit(&Str);

    DTL_ASSERT_EQ(DtlStrAppend(&Str, NULL), -1);
    DTL_ASSERT_EQ(DtlStrAppendLen(&Str, NULL, 4), -1);
    DTL_ASSERT_EQ(DtlStrAppend(NULL, "x"), -1);
    DTL_ASSERT_EQ(DtlStrAppendFormat(NULL, "x"), -1);
    DTL_ASSERT_EQ(DtlStrAppendFormat(&Str, NULL), -1);
    DTL_ASSERT_EQ(DtlStrSetFormat(NULL, "x"), -1);
    DTL_ASSERT_EQ(DtlStrSetFormat(&Str, NULL), -1);
    DTL_ASSERT_EQ(DtlStrReserve(NULL, 10), -1);
    DTL_ASSERT(DtlStrCStr(NULL) == NULL);
    DTL_ASSERT_EQ(DtlStrLength(NULL), 0);
    DTL_ASSERT(!DtlStrIsSmall(NULL));

    DtlStrInit(NULL);
    DtlStrFree(NULL);
    DtlStrClear(NULL);

    DtlStrFree(&Str);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The move out of the embedded buffer is the one allocation a short string ever makes.
// If it fails, the contents that were already there have to survive untouched.
DTL_TEST(FailedMoveToHeapKeepsTheContents)
{
    DtlStr Str;
    int i;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppend(&Str, "keep me"));

    DtlAllocResetCount();
    DtlAllocFailAfter(0);

    for (i = 0; i < 200; i++)
    {
        if (DtlStrAppendChar(&Str, 'x') != 0)
            break;
    }

    DTL_ASSERT(i < 200);
    DTL_ASSERT(DtlStrIsSmall(&Str));
    DTL_ASSERT_EQ(DtlStrCStr(&Str)[0], 'k');

    DtlAllocResetCount();
    DtlStrFree(&Str);
}

DTL_TEST(FailedGrowthOnTheHeapIsReported)
{
    DtlStr Str;
    int i;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrReserve(&Str, 200));
    DTL_ASSERT(!DtlStrIsSmall(&Str));

    for (i = 0; i < 200; i++)
        DTL_ASSERT_OK(DtlStrAppendChar(&Str, 'y'));

    DtlAllocResetCount();
    DtlAllocFailAfter(0);
    DTL_ASSERT_EQ(DtlStrReserve(&Str, 100000), -1);
    DTL_ASSERT_EQ(DtlStrLength(&Str), 200);

    DtlAllocResetCount();
    DtlStrFree(&Str);
}

DTL_TEST(FormatReportsAllocationFailure)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DtlAllocResetCount();
    DtlAllocFailAfter(0);

    DTL_ASSERT_EQ(DtlStrSetFormat(&Str, "%0*d", 500, 1), -1);
    DTL_ASSERT_EQ(DtlStrLength(&Str), 0);

    DtlAllocResetCount();
    DtlStrFree(&Str);
}

// A caller that computes a length wrongly can ask for one that leaves no room for the
// terminator. The addition would wrap, so it is checked before the allocation, not after.
DTL_TEST(ImpossibleLengthIsRefused)
{
    DtlStr Str;

    DtlStrInit(&Str);
    DTL_ASSERT_OK(DtlStrAppend(&Str, "intact"));

    DTL_ASSERT_EQ(DtlStrAppendLen(&Str, "x", (size_t)-1), -1);
    DTL_ASSERT_STR(DtlStrCStr(&Str), "intact");

    // Reserve takes the length directly, so it overflows one step later.
    DTL_ASSERT_EQ(DtlStrReserve(&Str, (size_t)-1), -1);
    DTL_ASSERT_STR(DtlStrCStr(&Str), "intact");

    DtlStrFree(&Str);
}

DTL_TEST_MAIN("Str", DTL_RUN(StartsEmptyAndSmall), DTL_RUN(ShortTextStaysInTheStructure),
              DTL_RUN(TransitionToHeapIsExact), DTL_RUN(ContentsSurviveTheMoveToTheHeap),
              DTL_RUN(AppendLenHandlesEmbeddedNull), DTL_RUN(AppendingNothingIsAllowed),
              DTL_RUN(FormatAppendsAndReplaces), DTL_RUN(FormatLongerThanTheSmallBuffer),
              DTL_RUN(ClearKeepsTheStorage), DTL_RUN(FreeReturnsItToTheSmallBuffer),
              DTL_RUN(ReserveMovesToTheHeapUpFront), DTL_RUN(BadArgumentsAreRejected),
              DTL_RUN(FailedMoveToHeapKeepsTheContents),
              DTL_RUN(FailedGrowthOnTheHeapIsReported),
              DTL_RUN(FormatReportsAllocationFailure), DTL_RUN(ImpossibleLengthIsRefused))

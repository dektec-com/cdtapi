// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestStr.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the growable string
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "Core/DtAlloc.h" // Allocation fault injection.
#include "Core/DtStr.h"   // Interface under test.
#include "DtTest.h"       // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(StartsEmptyAndSmall)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_EQ(DtStrLength(&Str), 0);
    DT_ASSERT_STR(DtStrCStr(&Str), "");
    DT_ASSERT(DtStrIsSmall(&Str));

    DtStrFree(&Str);
}

DT_TEST(ShortTextStaysInTheStructure)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppend(&Str, "IODIR"));
    DT_ASSERT_OK(DtStrAppend(&Str, "/INPUT"));

    DT_ASSERT_STR(DtStrCStr(&Str), "IODIR/INPUT");
    DT_ASSERT_EQ(DtStrLength(&Str), 11);
    DT_ASSERT(DtStrIsSmall(&Str));

    DtStrFree(&Str);
}

// The boundary is where a small-buffer optimisation goes wrong, so it is checked from
// both sides: one character below the capacity and one above it.
DT_TEST(TransitionToHeapIsExact)
{
    DtStr Str;
    size_t i;

    DtStrInit(&Str);

    for (i = 0; i < DT_STR_SMALL_CAPACITY - 1; i++)
        DT_ASSERT_OK(DtStrAppendChar(&Str, 'a'));

    DT_ASSERT_EQ(DtStrLength(&Str), DT_STR_SMALL_CAPACITY - 1);
    DT_ASSERT(DtStrIsSmall(&Str));

    DT_ASSERT_OK(DtStrAppendChar(&Str, 'b'));
    DT_ASSERT_EQ(DtStrLength(&Str), DT_STR_SMALL_CAPACITY);
    DT_ASSERT(!DtStrIsSmall(&Str));

    DT_ASSERT_EQ(DtStrCStr(&Str)[0], 'a');
    DT_ASSERT_EQ(DtStrCStr(&Str)[DT_STR_SMALL_CAPACITY - 1], 'b');
    DT_ASSERT_EQ(DtStrCStr(&Str)[DT_STR_SMALL_CAPACITY], '\0');

    DtStrFree(&Str);
}

DT_TEST(ContentsSurviveTheMoveToTheHeap)
{
    DtStr Str;
    int i;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppend(&Str, "prefix:"));

    for (i = 0; i < 500; i++)
        DT_ASSERT_OK(DtStrAppendChar(&Str, 'z'));

    DT_ASSERT_EQ(DtStrLength(&Str), 7 + 500);
    DT_ASSERT(!DtStrIsSmall(&Str));
    DT_ASSERT_EQ(DtStrCStr(&Str)[0], 'p');
    DT_ASSERT_EQ(DtStrCStr(&Str)[6], ':');
    DT_ASSERT_EQ(DtStrCStr(&Str)[7], 'z');

    DtStrFree(&Str);
}

DT_TEST(AppendLenHandlesEmbeddedNull)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppendLen(&Str, "ab\0cd", 5));

    DT_ASSERT_EQ(DtStrLength(&Str), 5);
    DT_ASSERT_EQ(DtStrCStr(&Str)[2], '\0');
    DT_ASSERT_EQ(DtStrCStr(&Str)[3], 'c');
    // The terminator is still written after the contents.
    DT_ASSERT_EQ(DtStrCStr(&Str)[5], '\0');

    DtStrFree(&Str);
}

DT_TEST(AppendingNothingIsAllowed)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppend(&Str, ""));
    DT_ASSERT_OK(DtStrAppendLen(&Str, "ignored", 0));
    DT_ASSERT_EQ(DtStrLength(&Str), 0);

    DtStrFree(&Str);
}

DT_TEST(FormatAppendsAndReplaces)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppendFormat(&Str, "%lld:%d", 2175000123LL, 2));
    DT_ASSERT_STR(DtStrCStr(&Str), "2175000123:2");

    DT_ASSERT_OK(DtStrAppendFormat(&Str, " port %d", 7));
    DT_ASSERT_STR(DtStrCStr(&Str), "2175000123:2 port 7");

    DT_ASSERT_OK(DtStrSetFormat(&Str, "DTA-%d", 2178));
    DT_ASSERT_STR(DtStrCStr(&Str), "DTA-2178");

    DtStrFree(&Str);
}

// vsnprintf is called twice, once to measure and once to write. If the second call were
// given an already-consumed argument list the result would be garbage, so a format long
// enough to force the heap is worth checking on its own.
DT_TEST(FormatLongerThanTheSmallBuffer)
{
    DtStr Str;
    size_t i;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrSetFormat(&Str, "%0*d", 200, 7));

    DT_ASSERT_EQ(DtStrLength(&Str), 200);
    DT_ASSERT(!DtStrIsSmall(&Str));
    DT_ASSERT_EQ(DtStrCStr(&Str)[199], '7');
    for (i = 0; i < 199; i++)
        DT_ASSERT_EQ(DtStrCStr(&Str)[i], '0');

    DtStrFree(&Str);
}

DT_TEST(ClearKeepsTheStorage)
{
    DtStr Str;
    int i;

    DtStrInit(&Str);
    for (i = 0; i < 200; i++)
        DT_ASSERT_OK(DtStrAppendChar(&Str, 'q'));

    DT_ASSERT(!DtStrIsSmall(&Str));
    DtStrClear(&Str);

    DT_ASSERT_EQ(DtStrLength(&Str), 0);
    DT_ASSERT_STR(DtStrCStr(&Str), "");
    // Still on the heap: clearing empties the contents, it does not undo the move.
    DT_ASSERT(!DtStrIsSmall(&Str));

    DtStrFree(&Str);
}

DT_TEST(FreeReturnsItToTheSmallBuffer)
{
    DtStr Str;
    int i;

    DtStrInit(&Str);
    for (i = 0; i < 300; i++)
        DT_ASSERT_OK(DtStrAppendChar(&Str, 'w'));

    DtStrFree(&Str);
    DT_ASSERT(DtStrIsSmall(&Str));
    DT_ASSERT_EQ(DtStrLength(&Str), 0);

    // Usable again without re-initialising, and safe to free twice.
    DT_ASSERT_OK(DtStrAppend(&Str, "again"));
    DT_ASSERT_STR(DtStrCStr(&Str), "again");
    DtStrFree(&Str);
    DtStrFree(&Str);
}

DT_TEST(ReserveMovesToTheHeapUpFront)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrReserve(&Str, 1000));
    DT_ASSERT(!DtStrIsSmall(&Str));
    DT_ASSERT_EQ(DtStrLength(&Str), 0);
    DT_ASSERT_STR(DtStrCStr(&Str), "");

    DtStrFree(&Str);
}

DT_TEST(BadArgumentsAreRejected)
{
    DtStr Str;

    DtStrInit(&Str);

    DT_ASSERT_EQ(DtStrAppend(&Str, NULL), -1);
    DT_ASSERT_EQ(DtStrAppendLen(&Str, NULL, 4), -1);
    DT_ASSERT_EQ(DtStrAppend(NULL, "x"), -1);
    DT_ASSERT_EQ(DtStrAppendFormat(NULL, "x"), -1);
    DT_ASSERT_EQ(DtStrAppendFormat(&Str, NULL), -1);
    DT_ASSERT_EQ(DtStrSetFormat(NULL, "x"), -1);
    DT_ASSERT_EQ(DtStrSetFormat(&Str, NULL), -1);
    DT_ASSERT_EQ(DtStrReserve(NULL, 10), -1);
    DT_ASSERT(DtStrCStr(NULL) == NULL);
    DT_ASSERT_EQ(DtStrLength(NULL), 0);
    DT_ASSERT(!DtStrIsSmall(NULL));

    DtStrInit(NULL);
    DtStrFree(NULL);
    DtStrClear(NULL);

    DtStrFree(&Str);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Out of memory +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The move out of the embedded buffer is the one allocation a short string ever makes.
// If it fails, the contents that were already there have to survive untouched.
DT_TEST(FailedMoveToHeapKeepsTheContents)
{
    DtStr Str;
    int i;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppend(&Str, "keep me"));

    DtAllocResetCount();
    DtAllocFailAfter(0);

    for (i = 0; i < 200; i++)
    {
        if (DtStrAppendChar(&Str, 'x') != 0)
            break;
    }

    DT_ASSERT(i < 200);
    DT_ASSERT(DtStrIsSmall(&Str));
    DT_ASSERT_EQ(DtStrCStr(&Str)[0], 'k');

    DtAllocResetCount();
    DtStrFree(&Str);
}

DT_TEST(FailedGrowthOnTheHeapIsReported)
{
    DtStr Str;
    int i;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrReserve(&Str, 200));
    DT_ASSERT(!DtStrIsSmall(&Str));

    for (i = 0; i < 200; i++)
        DT_ASSERT_OK(DtStrAppendChar(&Str, 'y'));

    DtAllocResetCount();
    DtAllocFailAfter(0);
    DT_ASSERT_EQ(DtStrReserve(&Str, 100000), -1);
    DT_ASSERT_EQ(DtStrLength(&Str), 200);

    DtAllocResetCount();
    DtStrFree(&Str);
}

DT_TEST(FormatReportsAllocationFailure)
{
    DtStr Str;

    DtStrInit(&Str);
    DtAllocResetCount();
    DtAllocFailAfter(0);

    DT_ASSERT_EQ(DtStrSetFormat(&Str, "%0*d", 500, 1), -1);
    DT_ASSERT_EQ(DtStrLength(&Str), 0);

    DtAllocResetCount();
    DtStrFree(&Str);
}

// A caller that computes a length wrongly can ask for one that leaves no room for the
// terminator. The addition would wrap, so it is checked before the allocation, not after.
DT_TEST(ImpossibleLengthIsRefused)
{
    DtStr Str;

    DtStrInit(&Str);
    DT_ASSERT_OK(DtStrAppend(&Str, "intact"));

    DT_ASSERT_EQ(DtStrAppendLen(&Str, "x", (size_t)-1), -1);
    DT_ASSERT_STR(DtStrCStr(&Str), "intact");

    // Reserve takes the length directly, so it overflows one step later.
    DT_ASSERT_EQ(DtStrReserve(&Str, (size_t)-1), -1);
    DT_ASSERT_STR(DtStrCStr(&Str), "intact");

    DtStrFree(&Str);
}

DT_TEST_MAIN("Str", DT_RUN(StartsEmptyAndSmall), DT_RUN(ShortTextStaysInTheStructure),
             DT_RUN(TransitionToHeapIsExact), DT_RUN(ContentsSurviveTheMoveToTheHeap),
             DT_RUN(AppendLenHandlesEmbeddedNull), DT_RUN(AppendingNothingIsAllowed),
             DT_RUN(FormatAppendsAndReplaces), DT_RUN(FormatLongerThanTheSmallBuffer),
             DT_RUN(ClearKeepsTheStorage), DT_RUN(FreeReturnsItToTheSmallBuffer),
             DT_RUN(ReserveMovesToTheHeapUpFront), DT_RUN(BadArgumentsAreRejected),
             DT_RUN(FailedMoveToHeapKeepsTheContents),
             DT_RUN(FailedGrowthOnTheHeapIsReported),
             DT_RUN(FormatReportsAllocationFailure), DT_RUN(ImpossibleLengthIsRefused))

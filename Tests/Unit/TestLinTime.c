// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestLinTime.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the Linux timed-wait deadline arithmetic
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtTest.h"            // Test framework.
#include "OAL/Linux/LinTime.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(ZeroAddsNothing)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 250000000L, 0, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 100);
    DT_ASSERT_EQ(Nsec, 250000000L);
}

DT_TEST(MillisecondsWithoutCarry)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 100000000L, 10, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 100);
    DT_ASSERT_EQ(Nsec, 110000000L);
}

// The case that matters: a deadline whose nanoseconds pass one billion must carry into
// the seconds. Left uncarried, pthread_cond_timedwait refuses it with EINVAL and the wait
// returns at once, so a 10 ms poll loop would spin at full CPU.
DT_TEST(NanosecondsCarryIntoSeconds)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 995000000L, 10, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 101);
    DT_ASSERT_EQ(Nsec, 5000000L);
}

DT_TEST(ExactlyOneBillionCarries)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 999000000L, 1, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 101);
    DT_ASSERT_EQ(Nsec, 0);
}

DT_TEST(WholeSecondsAreSplitOff)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 900000000L, 2500, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 103);
    DT_ASSERT_EQ(Nsec, 400000000L);
}

// The largest timeout an int can hold, from the largest normalised nanosecond value.
// Every intermediate must stay in range and the result must still be normalised.
DT_TEST(LargestTimeoutStaysNormalised)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(0, LIN_NSEC_PER_SEC - 1, 2147483647, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 2147484);
    DT_ASSERT_EQ(Nsec, 646999999L);
    DT_ASSERT(Nsec >= 0 && Nsec < LIN_NSEC_PER_SEC);
}

DT_TEST(NegativeTimeoutCountsAsZero)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 5L, -50, &Sec, &Nsec);
    DT_ASSERT_EQ(Sec, 100);
    DT_ASSERT_EQ(Nsec, 5L);
}

DT_TEST_MAIN("LinTime", DT_RUN(ZeroAddsNothing), DT_RUN(MillisecondsWithoutCarry),
             DT_RUN(NanosecondsCarryIntoSeconds), DT_RUN(ExactlyOneBillionCarries),
             DT_RUN(WholeSecondsAreSplitOff), DT_RUN(LargestTimeoutStaysNormalised),
             DT_RUN(NegativeTimeoutCountsAsZero))

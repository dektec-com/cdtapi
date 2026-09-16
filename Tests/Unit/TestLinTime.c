// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestLinTime.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the Linux timed-wait deadline arithmetic
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "DtlTest.h"           // Test framework.
#include "OAL/Linux/LinTime.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DTL_TEST(ZeroAddsNothing)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 250000000L, 0, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 100);
    DTL_ASSERT_EQ(Nsec, 250000000L);
}

DTL_TEST(MillisecondsWithoutCarry)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 100000000L, 10, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 100);
    DTL_ASSERT_EQ(Nsec, 110000000L);
}

// The case that matters: a deadline whose nanoseconds pass one billion must carry into
// the seconds. Left uncarried, pthread_cond_timedwait refuses it with EINVAL and the wait
// returns at once, so a 10 ms poll loop would spin at full CPU.
DTL_TEST(NanosecondsCarryIntoSeconds)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 995000000L, 10, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 101);
    DTL_ASSERT_EQ(Nsec, 5000000L);
}

DTL_TEST(ExactlyOneBillionCarries)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 999000000L, 1, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 101);
    DTL_ASSERT_EQ(Nsec, 0);
}

DTL_TEST(WholeSecondsAreSplitOff)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 900000000L, 2500, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 103);
    DTL_ASSERT_EQ(Nsec, 400000000L);
}

// The largest timeout an int can hold, from the largest normalised nanosecond value.
// Every intermediate must stay in range and the result must still be normalised.
DTL_TEST(LargestTimeoutStaysNormalised)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(0, LIN_NSEC_PER_SEC - 1, 2147483647, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 2147484);
    DTL_ASSERT_EQ(Nsec, 646999999L);
    DTL_ASSERT(Nsec >= 0 && Nsec < LIN_NSEC_PER_SEC);
}

DTL_TEST(NegativeTimeoutCountsAsZero)
{
    int64_t Sec;
    long Nsec;

    LinTimeAddMs(100, 5L, -50, &Sec, &Nsec);
    DTL_ASSERT_EQ(Sec, 100);
    DTL_ASSERT_EQ(Nsec, 5L);
}

DTL_TEST_MAIN("LinTime", DTL_RUN(ZeroAddsNothing), DTL_RUN(MillisecondsWithoutCarry),
              DTL_RUN(NanosecondsCarryIntoSeconds), DTL_RUN(ExactlyOneBillionCarries),
              DTL_RUN(WholeSecondsAreSplitOff), DTL_RUN(LargestTimeoutStaysNormalised),
              DTL_RUN(NegativeTimeoutCountsAsZero))

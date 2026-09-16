// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestRing.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the DMA ring buffer view
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "Core/DtlRing.h" // Interface under test.
#include "DtlTest.h"      // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define RING_SIZE 16

// Fills the backing store with a recognisable pattern, so that a copy landing at the
// wrong offset shows up as a wrong value rather than as a zero.
static void FillPattern(uint8_t* Base, size_t Size)
{
    size_t i;

    for (i = 0; i < Size; i++)
        Base[i] = (uint8_t)(0x10 + i);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(InitRejectsUnusableBuffers)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];

    DTL_ASSERT_EQ(DtlRingInit(NULL, Base, RING_SIZE), -1);
    DTL_ASSERT_EQ(DtlRingInit(&Ring, NULL, RING_SIZE), -1);

    // A single byte cannot hold anything, because one byte is always kept free to tell
    // a full ring from an empty one.
    DTL_ASSERT_EQ(DtlRingInit(&Ring, Base, 1), -1);
    DTL_ASSERT_EQ(DtlRingInit(&Ring, Base, 0), -1);

    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, 2));
}

DTL_TEST(StartsEmpty)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];

    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 0);
    DTL_ASSERT_EQ(DtlRingFree(&Ring), RING_SIZE - 1);
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 0);
}

// An offset outside the buffer means the library and the driver disagree about the ring.
// Accepting it would read arbitrary memory.
DTL_TEST(WriteOffsetOutsideTheBufferIsRefused)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];

    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));

    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, RING_SIZE - 1));
    DTL_ASSERT_EQ(DtlRingSetWriteOffset(&Ring, RING_SIZE), -1);
    DTL_ASSERT_EQ(DtlRingSetWriteOffset(&Ring, RING_SIZE + 100), -1);
    DTL_ASSERT_EQ(DtlRingSetWriteOffset(NULL, 0), -1);

    // The refused values must not have moved anything.
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), RING_SIZE - 1);
}

DTL_TEST(ReadWithoutWrapping)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[4];

    FillPattern(Base, RING_SIZE);
    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 8));
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 8);

    DTL_ASSERT_OK(DtlRingRead(&Ring, Out, 4));
    DTL_ASSERT_EQ(Out[0], 0x10);
    DTL_ASSERT_EQ(Out[3], 0x13);
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 4);
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 4);
}

// The wrap is the part that gets written wrong, so it is checked on its own: a read that
// crosses the end of the buffer has to come back as one contiguous run.
DTL_TEST(ReadAcrossTheEndOfTheBuffer)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[8];

    FillPattern(Base, RING_SIZE);
    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));

    // Move the read offset near the end, then let the producer wrap past it.
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 14));
    DTL_ASSERT_OK(DtlRingSkip(&Ring, 12));
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 12);

    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 4));
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 8);

    DTL_ASSERT_OK(DtlRingRead(&Ring, Out, 8));

    // Four bytes from the tail, then four from the start.
    DTL_ASSERT_EQ(Out[0], 0x1C);
    DTL_ASSERT_EQ(Out[3], 0x1F);
    DTL_ASSERT_EQ(Out[4], 0x10);
    DTL_ASSERT_EQ(Out[7], 0x13);
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 4);
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 0);
}

DTL_TEST(PeekDoesNotConsume)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t First[4];
    uint8_t Second[4];

    FillPattern(Base, RING_SIZE);
    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 8));

    DTL_ASSERT_OK(DtlRingPeek(&Ring, First, 4));
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 8);
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 0);

    DTL_ASSERT_OK(DtlRingPeek(&Ring, Second, 4));
    DTL_ASSERT_MEM(First, Second, 4);
}

DTL_TEST(ReadingMoreThanIsAvailableFails)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 4));

    DTL_ASSERT_EQ(DtlRingRead(&Ring, Out, 5), -1);
    DTL_ASSERT_EQ(DtlRingPeek(&Ring, Out, 5), -1);
    DTL_ASSERT_EQ(DtlRingSkip(&Ring, 5), -1);

    // A failed read consumes nothing.
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 4);
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 0);
}

DTL_TEST(ZeroLengthIsAllowed)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[1];

    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingPeek(&Ring, Out, 0));
    DTL_ASSERT_OK(DtlRingSkip(&Ring, 0));
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 0);
}

DTL_TEST(FullRingHoldsSizeMinusOne)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, RING_SIZE - 1));

    DTL_ASSERT_EQ(DtlRingLoad(&Ring), RING_SIZE - 1);
    DTL_ASSERT_EQ(DtlRingFree(&Ring), 0);
}

DTL_TEST(ClearDropsEverythingAvailable)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];

    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 10));
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 10);

    DtlRingClear(&Ring);
    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 0);
    DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), 10);

    DtlRingClear(NULL);
}

// Many laps, with a chunk size that is not a divisor of the ring size, so that the wrap
// lands on a different offset each time round. This is the defect that a short test
// misses and a soak run finds hours later.
DTL_TEST(ManyLapsStayConsistent)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[5];
    size_t Write = 0;
    int Lap;

    FillPattern(Base, RING_SIZE);
    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));

    for (Lap = 0; Lap < 1000; Lap++)
    {
        size_t Expected;

        Write = (Write + 5) % RING_SIZE;
        DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, Write));
        DTL_ASSERT_EQ(DtlRingLoad(&Ring), 5);

        // What the read must return is fixed by where the read offset currently is.
        Expected = DtlRingReadOffset(&Ring);
        DTL_ASSERT_OK(DtlRingRead(&Ring, Out, 5));
        DTL_ASSERT_EQ(Out[0], Base[Expected]);
        DTL_ASSERT_EQ(Out[4], Base[(Expected + 4) % RING_SIZE]);

        DTL_ASSERT_EQ(DtlRingLoad(&Ring), 0);
        DTL_ASSERT_EQ(DtlRingReadOffset(&Ring), Write);
    }
}

DTL_TEST(NullIsAcceptedEverywhere)
{
    uint8_t Out[4];

    DTL_ASSERT_EQ(DtlRingLoad(NULL), 0);
    DTL_ASSERT_EQ(DtlRingFree(NULL), 0);
    DTL_ASSERT_EQ(DtlRingReadOffset(NULL), 0);
    DTL_ASSERT_EQ(DtlRingPeek(NULL, Out, 4), -1);
    DTL_ASSERT_EQ(DtlRingSkip(NULL, 4), -1);
    DTL_ASSERT_EQ(DtlRingRead(NULL, Out, 4), -1);
}

DTL_TEST(PeekRejectsNullDestination)
{
    DtlRing Ring;
    uint8_t Base[RING_SIZE];

    DTL_ASSERT_OK(DtlRingInit(&Ring, Base, RING_SIZE));
    DTL_ASSERT_OK(DtlRingSetWriteOffset(&Ring, 4));
    DTL_ASSERT_EQ(DtlRingPeek(&Ring, NULL, 4), -1);
}

// A caller that zeroes the structure and skips DtlRingInit has a ring with no backing
// store. Every entry point has to notice rather than dereference the null base.
DTL_TEST(ZeroedStructIsTreatedAsEmpty)
{
    DtlRing Ring;
    uint8_t Out[4];

    memset(&Ring, 0, sizeof(Ring));

    DTL_ASSERT_EQ(DtlRingLoad(&Ring), 0);
    DTL_ASSERT_EQ(DtlRingFree(&Ring), 0);
    DTL_ASSERT_EQ(DtlRingSetWriteOffset(&Ring, 0), -1);
    DTL_ASSERT_EQ(DtlRingPeek(&Ring, Out, 4), -1);
    DTL_ASSERT_EQ(DtlRingSkip(&Ring, 4), -1);
    DTL_ASSERT_EQ(DtlRingRead(&Ring, Out, 4), -1);
}

DTL_TEST_MAIN("Ring", DTL_RUN(InitRejectsUnusableBuffers), DTL_RUN(StartsEmpty),
              DTL_RUN(WriteOffsetOutsideTheBufferIsRefused), DTL_RUN(ReadWithoutWrapping),
              DTL_RUN(ReadAcrossTheEndOfTheBuffer), DTL_RUN(PeekDoesNotConsume),
              DTL_RUN(ReadingMoreThanIsAvailableFails), DTL_RUN(ZeroLengthIsAllowed),
              DTL_RUN(FullRingHoldsSizeMinusOne), DTL_RUN(ClearDropsEverythingAvailable),
              DTL_RUN(ManyLapsStayConsistent), DTL_RUN(NullIsAcceptedEverywhere),
              DTL_RUN(PeekRejectsNullDestination), DTL_RUN(ZeroedStructIsTreatedAsEmpty))

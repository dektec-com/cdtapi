// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestRing.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the DMA ring buffer view
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "Core/DtRing.h" // Interface under test.
#include "DtTest.h"      // Test framework.

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(InitRejectsUnusableBuffers)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_EQ(DtRingInit(NULL, Base, RING_SIZE, 1), -1);
    DT_ASSERT_EQ(DtRingInit(&Ring, NULL, RING_SIZE, 1), -1);

    // A single byte cannot hold anything, because one byte is always kept free to tell
    // a full ring from an empty one.
    DT_ASSERT_EQ(DtRingInit(&Ring, Base, 1, 1), -1);
    DT_ASSERT_EQ(DtRingInit(&Ring, Base, 0, 1), -1);

    DT_ASSERT_OK(DtRingInit(&Ring, Base, 2, 1));

    // A reserve of zero cannot tell full from empty, and a reserve of the whole buffer
    // leaves a ring that holds nothing.
    DT_ASSERT_EQ(DtRingInit(&Ring, Base, RING_SIZE, 0), -1);
    DT_ASSERT_EQ(DtRingInit(&Ring, Base, RING_SIZE, RING_SIZE), -1);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, RING_SIZE - 1));
}

DT_TEST(StartsEmpty)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_EQ(DtRingLoad(&Ring), 0);
    DT_ASSERT_EQ(DtRingFree(&Ring), RING_SIZE - 1);
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 0);
}

// An offset outside the buffer means the library and the driver disagree about the ring.
// Accepting it would read arbitrary memory.
DT_TEST(WriteOffsetOutsideTheBufferIsRefused)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));

    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, RING_SIZE - 1));
    DT_ASSERT_EQ(DtRingSetWriteOffset(&Ring, RING_SIZE), -1);
    DT_ASSERT_EQ(DtRingSetWriteOffset(&Ring, RING_SIZE + 100), -1);
    DT_ASSERT_EQ(DtRingSetWriteOffset(NULL, 0), -1);

    // The refused values must not have moved anything.
    DT_ASSERT_EQ(DtRingLoad(&Ring), RING_SIZE - 1);
}

DT_TEST(ReadWithoutWrapping)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[4];

    FillPattern(Base, RING_SIZE);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 8));
    DT_ASSERT_EQ(DtRingLoad(&Ring), 8);

    DT_ASSERT_OK(DtRingRead(&Ring, Out, 4));
    DT_ASSERT_EQ(Out[0], 0x10);
    DT_ASSERT_EQ(Out[3], 0x13);
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 4);
    DT_ASSERT_EQ(DtRingLoad(&Ring), 4);
}

// The wrap is the part that gets written wrong, so it is checked on its own: a read that
// crosses the end of the buffer has to come back as one contiguous run.
DT_TEST(ReadAcrossTheEndOfTheBuffer)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[8];

    FillPattern(Base, RING_SIZE);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));

    // Move the read offset near the end, then let the producer wrap past it.
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 14));
    DT_ASSERT_OK(DtRingSkip(&Ring, 12));
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 12);

    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 4));
    DT_ASSERT_EQ(DtRingLoad(&Ring), 8);

    DT_ASSERT_OK(DtRingRead(&Ring, Out, 8));

    // Four bytes from the tail, then four from the start.
    DT_ASSERT_EQ(Out[0], 0x1C);
    DT_ASSERT_EQ(Out[3], 0x1F);
    DT_ASSERT_EQ(Out[4], 0x10);
    DT_ASSERT_EQ(Out[7], 0x13);
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 4);
    DT_ASSERT_EQ(DtRingLoad(&Ring), 0);
}

DT_TEST(PeekDoesNotConsume)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t First[4];
    uint8_t Second[4];

    FillPattern(Base, RING_SIZE);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 8));

    DT_ASSERT_OK(DtRingPeek(&Ring, First, 4));
    DT_ASSERT_EQ(DtRingLoad(&Ring), 8);
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 0);

    DT_ASSERT_OK(DtRingPeek(&Ring, Second, 4));
    DT_ASSERT_MEM(First, Second, 4);
}

DT_TEST(ReadingMoreThanIsAvailableFails)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 4));

    DT_ASSERT_EQ(DtRingRead(&Ring, Out, 5), -1);
    DT_ASSERT_EQ(DtRingPeek(&Ring, Out, 5), -1);
    DT_ASSERT_EQ(DtRingSkip(&Ring, 5), -1);

    // A failed read consumes nothing.
    DT_ASSERT_EQ(DtRingLoad(&Ring), 4);
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 0);
}

DT_TEST(ZeroLengthIsAllowed)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[1];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingPeek(&Ring, Out, 0));
    DT_ASSERT_OK(DtRingSkip(&Ring, 0));
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 0);
}

DT_TEST(FullRingHoldsSizeMinusOne)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, RING_SIZE - 1));

    DT_ASSERT_EQ(DtRingLoad(&Ring), RING_SIZE - 1);
    DT_ASSERT_EQ(DtRingFree(&Ring), 0);
}

DT_TEST(ClearDropsEverythingAvailable)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 10));
    DT_ASSERT_EQ(DtRingLoad(&Ring), 10);

    DtRingClear(&Ring);
    DT_ASSERT_EQ(DtRingLoad(&Ring), 0);
    DT_ASSERT_EQ(DtRingReadOffset(&Ring), 10);

    DtRingClear(NULL);
}

// Many laps, with a chunk size that is not a divisor of the ring size, so that the wrap
// lands on a different offset each time round. This is the defect that a short test
// misses and a soak run finds hours later.
DT_TEST(ManyLapsStayConsistent)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];
    uint8_t Out[5];
    size_t Write = 0;
    int Lap;

    FillPattern(Base, RING_SIZE);
    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));

    for (Lap = 0; Lap < 1000; Lap++)
    {
        size_t Expected;

        Write = (Write + 5) % RING_SIZE;
        DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, Write));
        DT_ASSERT_EQ(DtRingLoad(&Ring), 5);

        // What the read must return is fixed by where the read offset currently is.
        Expected = DtRingReadOffset(&Ring);
        DT_ASSERT_OK(DtRingRead(&Ring, Out, 5));
        DT_ASSERT_EQ(Out[0], Base[Expected]);
        DT_ASSERT_EQ(Out[4], Base[(Expected + 4) % RING_SIZE]);

        DT_ASSERT_EQ(DtRingLoad(&Ring), 0);
        DT_ASSERT_EQ(DtRingReadOffset(&Ring), Write);
    }
}

DT_TEST(NullIsAcceptedEverywhere)
{
    uint8_t Out[4];

    DT_ASSERT_EQ(DtRingLoad(NULL), 0);
    DT_ASSERT_EQ(DtRingFree(NULL), 0);
    DT_ASSERT_EQ(DtRingReadOffset(NULL), 0);
    DT_ASSERT_EQ(DtRingPeek(NULL, Out, 4), -1);
    DT_ASSERT_EQ(DtRingSkip(NULL, 4), -1);
    DT_ASSERT_EQ(DtRingRead(NULL, Out, 4), -1);
}

DT_TEST(PeekRejectsNullDestination)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 4));
    DT_ASSERT_EQ(DtRingPeek(&Ring, NULL, 4), -1);
}

// A caller that zeroes the structure and skips DtRingInit has a ring with no backing
// store. Every entry point has to notice rather than dereference the null base.
DT_TEST(ZeroedStructIsTreatedAsEmpty)
{
    DtRing Ring;
    uint8_t Out[4];

    memset(&Ring, 0, sizeof(Ring));

    DT_ASSERT_EQ(DtRingLoad(&Ring), 0);
    DT_ASSERT_EQ(DtRingFree(&Ring), 0);
    DT_ASSERT_EQ(DtRingSetWriteOffset(&Ring, 0), -1);
    DT_ASSERT_EQ(DtRingPeek(&Ring, Out, 4), -1);
    DT_ASSERT_EQ(DtRingSkip(&Ring, 4), -1);
    DT_ASSERT_EQ(DtRingRead(&Ring, Out, 4), -1);
}

// The hardware keeps a whole data word free, not one byte. With an eight-byte word the
// ring holds eight bytes less than its size, and reports free space accordingly. A
// one-byte reserve here would let the transmit side write seven bytes too many and leave
// a full ring looking empty.
DT_TEST(ReserveOfOneDataWord)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 8));
    DT_ASSERT_EQ(DtRingFree(&Ring), RING_SIZE - 8);

    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 5));
    DT_ASSERT_EQ(DtRingLoad(&Ring), 5);
    DT_ASSERT_EQ(DtRingFree(&Ring), RING_SIZE - 8 - 5);

    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, RING_SIZE - 8));
    DT_ASSERT_EQ(DtRingFree(&Ring), 0);
}

// A write offset that would fill the reserve cannot come from a consistent driver.
DT_TEST(LoadBeyondTheReserveIsRefused)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRingInit(&Ring, Base, RING_SIZE, 8));

    DT_ASSERT_EQ(DtRingSetWriteOffset(&Ring, RING_SIZE - 7), -1);
    DT_ASSERT_EQ(DtRingLoad(&Ring), 0);

    // Also when the offending offset has wrapped past the read offset.
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 6));
    DT_ASSERT_OK(DtRingSkip(&Ring, 6));
    DT_ASSERT_EQ(DtRingSetWriteOffset(&Ring, 5), -1);
    DT_ASSERT_OK(DtRingSetWriteOffset(&Ring, 14));
}

DT_TEST_MAIN("Ring", DT_RUN(InitRejectsUnusableBuffers), DT_RUN(StartsEmpty),
             DT_RUN(WriteOffsetOutsideTheBufferIsRefused), DT_RUN(ReadWithoutWrapping),
             DT_RUN(ReadAcrossTheEndOfTheBuffer), DT_RUN(PeekDoesNotConsume),
             DT_RUN(ReadingMoreThanIsAvailableFails), DT_RUN(ZeroLengthIsAllowed),
             DT_RUN(FullRingHoldsSizeMinusOne), DT_RUN(ClearDropsEverythingAvailable),
             DT_RUN(ManyLapsStayConsistent), DT_RUN(NullIsAcceptedEverywhere),
             DT_RUN(PeekRejectsNullDestination), DT_RUN(ZeroedStructIsTreatedAsEmpty),
             DT_RUN(ReserveOfOneDataWord), DT_RUN(LoadBeyondTheReserveIsRefused))

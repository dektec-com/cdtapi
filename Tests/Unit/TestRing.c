// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# TestRing.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the DMA ring buffer view
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtRing.h" // Interface under test.
#include "DtTest.h"      // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

#define RING_SIZE 16

// Fills the backing store with a recognisable pattern, so that a copy landing at the
// wrong offset shows up as a wrong value rather than as a zero.
static void FillPattern(uint8_t* Base, size_t Size)
{
    for (size_t i = 0; i < Size; i++)
        Base[i] = (uint8_t)(0x10 + i);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(InitRejectsUnusableBuffers)
{
    uint8_t Base[RING_SIZE];

    DT_ASSERT_EQ(DtRing_Init(NULL, Base, RING_SIZE, 1), -1);
    DtRing Ring;
    DT_ASSERT_EQ(DtRing_Init(&Ring, NULL, RING_SIZE, 1), -1);

    // A single byte cannot hold anything, because one byte is always kept free to tell
    // a full ring from an empty one.
    DT_ASSERT_EQ(DtRing_Init(&Ring, Base, 1, 1), -1);
    DT_ASSERT_EQ(DtRing_Init(&Ring, Base, 0, 1), -1);

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, 2, 1));

    // A reserve of zero cannot tell full from empty, and a reserve of the whole buffer
    // leaves a ring that holds nothing.
    DT_ASSERT_EQ(DtRing_Init(&Ring, Base, RING_SIZE, 0), -1);
    DT_ASSERT_EQ(DtRing_Init(&Ring, Base, RING_SIZE, RING_SIZE), -1);
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, RING_SIZE - 1));
}

DT_TEST(StartsEmpty)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 0);
    DT_ASSERT_EQ(DtRing_Room(&Ring), RING_SIZE - 1);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 0);
}

// An offset outside the buffer means the library and the driver disagree about the ring.
// Accepting it would read arbitrary memory.
DT_TEST(WriteOffsetOutsideTheBufferIsRefused)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));

    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, RING_SIZE - 1));
    DT_ASSERT_EQ(DtRing_SetWriteOffset(&Ring, RING_SIZE), -1);
    DT_ASSERT_EQ(DtRing_SetWriteOffset(&Ring, RING_SIZE + 100), -1);
    DT_ASSERT_EQ(DtRing_SetWriteOffset(NULL, 0), -1);

    // The refused values must not have moved anything.
    DT_ASSERT_EQ(DtRing_Load(&Ring), RING_SIZE - 1);
}

DT_TEST(ReadWithoutWrapping)
{
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 8));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 8);

    uint8_t Out[4];
    DT_ASSERT_OK(DtRing_Read(&Ring, Out, 4));
    DT_ASSERT_EQ(Out[0], 0x10);
    DT_ASSERT_EQ(Out[3], 0x13);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 4);
    DT_ASSERT_EQ(DtRing_Load(&Ring), 4);
}

// The wrap is the part that gets written wrong, so it is checked on its own: a read that
// crosses the end of the buffer has to come back as one contiguous run.
DT_TEST(ReadAcrossTheEndOfTheBuffer)
{
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));

    // Move the read offset near the end, then let the producer wrap past it.
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 14));
    DT_ASSERT_OK(DtRing_Skip(&Ring, 12));
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 12);

    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 4));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 8);

    uint8_t Out[8];
    DT_ASSERT_OK(DtRing_Read(&Ring, Out, 8));

    // Four bytes from the tail, then four from the start.
    DT_ASSERT_EQ(Out[0], 0x1C);
    DT_ASSERT_EQ(Out[3], 0x1F);
    DT_ASSERT_EQ(Out[4], 0x10);
    DT_ASSERT_EQ(Out[7], 0x13);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 4);
    DT_ASSERT_EQ(DtRing_Load(&Ring), 0);
}

DT_TEST(PeekDoesNotConsume)
{
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 8));

    uint8_t First[4];
    DT_ASSERT_OK(DtRing_Peek(&Ring, First, 4));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 8);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 0);

    uint8_t Second[4];
    DT_ASSERT_OK(DtRing_Peek(&Ring, Second, 4));
    DT_ASSERT_MEM(First, Second, 4);
}

DT_TEST(ReadingMoreThanIsAvailableFails)
{
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 4));

    uint8_t Out[RING_SIZE];
    DT_ASSERT_EQ(DtRing_Read(&Ring, Out, 5), -1);
    DT_ASSERT_EQ(DtRing_Peek(&Ring, Out, 5), -1);
    DT_ASSERT_EQ(DtRing_Skip(&Ring, 5), -1);

    // A failed read consumes nothing.
    DT_ASSERT_EQ(DtRing_Load(&Ring), 4);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 0);
}

DT_TEST(ZeroLengthIsAllowed)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    uint8_t Out[1];
    DT_ASSERT_OK(DtRing_Peek(&Ring, Out, 0));
    DT_ASSERT_OK(DtRing_Skip(&Ring, 0));
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 0);
}

DT_TEST(FullRingHoldsSizeMinusOne)
{
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, RING_SIZE - 1));

    DT_ASSERT_EQ(DtRing_Load(&Ring), RING_SIZE - 1);
    DT_ASSERT_EQ(DtRing_Room(&Ring), 0);
}

DT_TEST(ClearDropsEverythingAvailable)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 10));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 10);

    DtRing_Clear(&Ring);
    DT_ASSERT_EQ(DtRing_Load(&Ring), 0);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 10);

    DtRing_Clear(NULL);
}

// Many laps, with a chunk size that is not a divisor of the ring size, so that the wrap
// lands on a different offset each time round. This is the defect that a short test
// misses and a soak run finds hours later.
DT_TEST(ManyLapsStayConsistent)
{
    uint8_t Base[RING_SIZE];
    size_t Write = 0;

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));

    uint8_t Out[5];
    for (int Lap = 0; Lap < 1000; Lap++)
    {
        Write = (Write + 5) % RING_SIZE;
        DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, Write));
        DT_ASSERT_EQ(DtRing_Load(&Ring), 5);

        // What the read must return is fixed by where the read offset currently is.
        size_t Expected = DtRing_ReadOffset(&Ring);
        DT_ASSERT_OK(DtRing_Read(&Ring, Out, 5));
        DT_ASSERT_EQ(Out[0], Base[Expected]);
        DT_ASSERT_EQ(Out[4], Base[(Expected + 4) % RING_SIZE]);

        DT_ASSERT_EQ(DtRing_Load(&Ring), 0);
        DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), Write);
    }
}

DT_TEST(NullIsAcceptedEverywhere)
{
    DT_ASSERT_EQ(DtRing_Load(NULL), 0);
    DT_ASSERT_EQ(DtRing_Room(NULL), 0);
    DT_ASSERT_EQ(DtRing_ReadOffset(NULL), 0);
    uint8_t Out[4];
    DT_ASSERT_EQ(DtRing_Peek(NULL, Out, 4), -1);
    DT_ASSERT_EQ(DtRing_Skip(NULL, 4), -1);
    DT_ASSERT_EQ(DtRing_Read(NULL, Out, 4), -1);
}

DT_TEST(PeekRejectsNullDestination)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 4));
    DT_ASSERT_EQ(DtRing_Peek(&Ring, NULL, 4), -1);
}

// A caller that zeroes the structure and skips DtRing_Init has a ring with no backing
// store. Every entry point has to notice rather than dereference the null base.
DT_TEST(ZeroedStructIsTreatedAsEmpty)
{
    DtRing Ring;

    memset(&Ring, 0, sizeof(Ring));

    DT_ASSERT_EQ(DtRing_Load(&Ring), 0);
    DT_ASSERT_EQ(DtRing_Room(&Ring), 0);
    DT_ASSERT_EQ(DtRing_SetWriteOffset(&Ring, 0), -1);
    uint8_t Out[4];
    DT_ASSERT_EQ(DtRing_Peek(&Ring, Out, 4), -1);
    DT_ASSERT_EQ(DtRing_Skip(&Ring, 4), -1);
    DT_ASSERT_EQ(DtRing_Read(&Ring, Out, 4), -1);
}

// The hardware keeps a whole data word free, not one byte. With an eight-byte word the
// ring holds eight bytes less than its size, and reports free space accordingly. A
// one-byte reserve here would let the transmit side write seven bytes too many and leave
// a full ring looking empty.
DT_TEST(ReserveOfOneDataWord)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 8));
    DT_ASSERT_EQ(DtRing_Room(&Ring), RING_SIZE - 8);

    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 5));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 5);
    DT_ASSERT_EQ(DtRing_Room(&Ring), RING_SIZE - 8 - 5);

    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, RING_SIZE - 8));
    DT_ASSERT_EQ(DtRing_Room(&Ring), 0);
}

// A write offset that would fill the reserve cannot come from a consistent driver.
DT_TEST(LoadBeyondTheReserveIsRefused)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 8));

    DT_ASSERT_EQ(DtRing_SetWriteOffset(&Ring, RING_SIZE - 7), -1);
    DT_ASSERT_EQ(DtRing_Load(&Ring), 0);

    // Also when the offending offset has wrapped past the read offset.
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 6));
    DT_ASSERT_OK(DtRing_Skip(&Ring, 6));
    DT_ASSERT_EQ(DtRing_SetWriteOffset(&Ring, 5), -1);
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 14));
}

// Peeking further on: the offset counts from the read offset, the wrap is handled, and
// nothing is consumed.
DT_TEST(PeekAtAnOffset)
{
    uint8_t Base[RING_SIZE];

    FillPattern(Base, RING_SIZE);
    DtRing Ring;
    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 14));
    DT_ASSERT_OK(DtRing_Skip(&Ring, 10));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 6));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 12);

    uint8_t Out[4];
    DT_ASSERT_OK(DtRing_PeekAt(&Ring, 2, Out, 4)); // Offsets 12 to 15
    DT_ASSERT_EQ(Out[0], 0x1C);
    DT_ASSERT_EQ(Out[3], 0x1F);
    DT_ASSERT_OK(DtRing_PeekAt(&Ring, 4, Out, 4)); // Offsets 14, 15, 0 and 1
    DT_ASSERT_EQ(Out[0], 0x1E);
    DT_ASSERT_EQ(Out[2], 0x10);
    DT_ASSERT_EQ(Out[3], 0x11);
    DT_ASSERT_OK(DtRing_PeekAt(&Ring, 8, Out, 4)); // Offsets 2 to 5, the last available
    DT_ASSERT_EQ(Out[3], 0x15);

    DT_ASSERT_EQ(DtRing_PeekAt(&Ring, 9, Out, 4), -1);
    DT_ASSERT_EQ(DtRing_PeekAt(&Ring, 13, Out, 0), 0);
    DT_ASSERT_EQ(DtRing_PeekAt(&Ring, (size_t)-1, Out, 2), -1);
    DT_ASSERT_EQ(DtRing_PeekAt(&Ring, 2, Out, (size_t)-1), -1);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 10);
    DT_ASSERT_EQ(DtRing_Load(&Ring), 12);
}

// A span is the buffer's own bytes when they lie in one piece, and NULL across the end or
// beyond what is available.
DT_TEST(SpanInOnePiece)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 14));
    DT_ASSERT_OK(DtRing_Skip(&Ring, 10));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 6));

    DT_ASSERT(DtRing_Span(&Ring, 0, 6) == Base + 10);
    DT_ASSERT(DtRing_Span(&Ring, 2, 4) == Base + 12);
    DT_ASSERT(DtRing_Span(&Ring, 4, 4) == NULL);
    DT_ASSERT(DtRing_Span(&Ring, 6, 6) == Base);
    DT_ASSERT(DtRing_Span(&Ring, 6, 7) == NULL);
    DT_ASSERT(DtRing_Span(NULL, 0, 1) == NULL);
}

// A restart empties the ring where reading and writing continue, inside the buffer only.
DT_TEST(RestartEmptiesAtAnOffset)
{
    DtRing Ring;
    uint8_t Base[RING_SIZE];

    DT_ASSERT_OK(DtRing_Init(&Ring, Base, RING_SIZE, 1));
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 9));

    DT_ASSERT_OK(DtRing_Restart(&Ring, 12));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 0);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 12);
    DT_ASSERT_OK(DtRing_SetWriteOffset(&Ring, 2));
    DT_ASSERT_EQ(DtRing_Load(&Ring), 6);

    DT_ASSERT_EQ(DtRing_Restart(&Ring, RING_SIZE), -1);
    DT_ASSERT_EQ(DtRing_ReadOffset(&Ring), 12);
    DT_ASSERT_EQ(DtRing_Restart(NULL, 0), -1);
}

DT_TEST_MAIN("Ring", DT_RUN(InitRejectsUnusableBuffers), DT_RUN(StartsEmpty),
             DT_RUN(WriteOffsetOutsideTheBufferIsRefused), DT_RUN(ReadWithoutWrapping),
             DT_RUN(ReadAcrossTheEndOfTheBuffer), DT_RUN(PeekDoesNotConsume),
             DT_RUN(ReadingMoreThanIsAvailableFails), DT_RUN(ZeroLengthIsAllowed),
             DT_RUN(FullRingHoldsSizeMinusOne), DT_RUN(ClearDropsEverythingAvailable),
             DT_RUN(ManyLapsStayConsistent), DT_RUN(NullIsAcceptedEverywhere),
             DT_RUN(PeekRejectsNullDestination), DT_RUN(ZeroedStructIsTreatedAsEmpty),
             DT_RUN(ReserveOfOneDataWord), DT_RUN(LoadBeyondTheReserveIsRefused),
             DT_RUN(PeekAtAnOffset), DT_RUN(SpanInOnePiece),
             DT_RUN(RestartEmptiesAtAnOffset))

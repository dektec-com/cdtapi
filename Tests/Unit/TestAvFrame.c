// #*#*#*#*#*#*#*#*#*#*#*#*#*#* TestAvFrame.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Unit tests for the frame pool, the frame FIFO and the frame properties
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "AvFifo/DtAvFrame.h" // Functions under test.
#include "Core/DtAlloc.h"     // Live allocations and failures.
#include "DtTest.h"           // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Pool +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(PoolGivesAlignedClearedFrames)
{
    int Live = DtAlloc_NumLive();
    DtAvFramePool Pool;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));

    DtAvFrame* A = DtAvFramePool_Get(&Pool, 1000);
    DtAvFrame* B = DtAvFramePool_Get(&Pool, 0);
    DT_ASSERT(A != NULL && B != NULL && A != B);
    DT_ASSERT((AvFifo_Frame*)A == &A->Frame);
    DT_ASSERT_EQ((uintptr_t)A->Frame.Data % DT_AV_FRAME_ALIGNMENT, 0);
    DT_ASSERT_EQ(A->Frame.Size, 1000);
    DT_ASSERT_EQ(A->Frame.NumRows, -1);
    DT_ASSERT_EQ(A->Frame.NumValidBytes, 0);
    memset(A->Frame.Data, 0x5A, 1000);
    DT_ASSERT_EQ(DtAvFramePool_NumFrames(&Pool), 2);
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&Pool), 0);

    // A returned frame comes back, cleared, with the same data when it is large enough.
    A->Frame.NumValidBytes = 1000;
    A->Frame.RtpTime = 7;
    A->Frame.Field = 1;
    uint8_t* Data = A->Frame.Data;
    DT_ASSERT(DtAvFramePool_Return(&Pool, &A->Frame));
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&Pool), 1);
    DtAvFrame* Again = DtAvFramePool_Get(&Pool, 800);
    DT_ASSERT(Again == A);
    DT_ASSERT(Again->Frame.Data == Data);
    DT_ASSERT_EQ(Again->Frame.Size, 800);
    DT_ASSERT_EQ(Again->Frame.NumValidBytes, 0);
    DT_ASSERT_EQ(Again->Frame.RtpTime, 0);
    DT_ASSERT_EQ(Again->Frame.Field, 0);

    // A larger size gets new data, still aligned.
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Again->Frame));
    Again = DtAvFramePool_Get(&Pool, 100000);
    DT_ASSERT(Again == A);
    DT_ASSERT_EQ((uintptr_t)Again->Frame.Data % DT_AV_FRAME_ALIGNMENT, 0);
    memset(Again->Frame.Data, 0, 100000);
    DT_ASSERT_EQ(DtAvFramePool_NumFrames(&Pool), 2);

    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

DT_TEST(PoolRefusesWrongReturns)
{
    int Live = DtAlloc_NumLive();
    DtAvFramePool Pool;
    DtAvFramePool Other;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtAvFramePool_Init(&Other));

    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, 10);
    AvFifo_Frame Stranger;
    DT_ASSERT(Frame != NULL);
    DT_ASSERT(!DtAvFramePool_Return(&Pool, NULL));
    DT_ASSERT(!DtAvFramePool_Return(&Pool, &Stranger));
    DT_ASSERT(!DtAvFramePool_Return(&Other, &Frame->Frame));
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
    DT_ASSERT(!DtAvFramePool_Return(&Pool, &Frame->Frame));
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&Pool), 1);

    DtAvFramePool_Destroy(&Other);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

DT_TEST(PoolSurvivesFailedAllocations)
{
    int Live = DtAlloc_NumLive();
    DtAvFramePool Pool;
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));

    // The frame, then its data.
    DtAlloc_FailAfter(0);
    DT_ASSERT(DtAvFramePool_Get(&Pool, 10) == NULL);
    DtAlloc_FailAfter(1);
    DT_ASSERT(DtAvFramePool_Get(&Pool, 10) == NULL);
    DtAlloc_FailAfter(-1);
    DT_ASSERT_EQ(DtAvFramePool_NumFrames(&Pool), 0);

    // A free frame that cannot grow stays free.
    DtAvFrame* Frame = DtAvFramePool_Get(&Pool, 10);
    DT_ASSERT(Frame != NULL);
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frame->Frame));
    DtAlloc_FailAfter(0);
    DT_ASSERT(DtAvFramePool_Get(&Pool, 5000) == NULL);
    DtAlloc_FailAfter(-1);
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&Pool), 1);
    DT_ASSERT(DtAvFramePool_Get(&Pool, 5000) == Frame);

    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= FIFO +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(FifoKeepsOrderAndOverflows)
{
    int Live = DtAlloc_NumLive();
    DtAvFramePool Pool;
    DtAvFrameFifo Fifo;
    DtAvFrame* Frames[10];
    DT_ASSERT_OK(DtAvFramePool_Init(&Pool));
    DT_ASSERT_OK(DtAvFrameFifo_Init(&Fifo));
    for (int i = 0; i < 10; i++)
    {
        Frames[i] = DtAvFramePool_Get(&Pool, 4);
        DT_ASSERT(Frames[i] != NULL);
    }

    DT_ASSERT_EQ(DtAvFrameFifo_GetMaxSize(&Fifo), 4);
    DT_ASSERT(DtAvFrameFifo_Pop(&Fifo) == NULL);
    for (int i = 0; i < 4; i++)
        DT_ASSERT(DtAvFrameFifo_Push(&Fifo, Frames[i]));
    DT_ASSERT(!DtAvFrameFifo_TakeOverflow(&Fifo));
    DT_ASSERT(!DtAvFrameFifo_Push(&Fifo, Frames[4]));
    DT_ASSERT_EQ(DtAvFrameFifo_Load(&Fifo), 4);
    DT_ASSERT(DtAvFrameFifo_TakeOverflow(&Fifo));
    DT_ASSERT(!DtAvFrameFifo_TakeOverflow(&Fifo));

    // Wrap the ring, then grow it: the order stays.
    DT_ASSERT(DtAvFrameFifo_Pop(&Fifo) == Frames[0]);
    DT_ASSERT(DtAvFrameFifo_Pop(&Fifo) == Frames[1]);
    DT_ASSERT(DtAvFrameFifo_Push(&Fifo, Frames[4]));
    DT_ASSERT(DtAvFrameFifo_Push(&Fifo, Frames[5]));
    DT_ASSERT_EQ(DtAvFrameFifo_SetMaxSize(&Fifo, 0), DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtAvFrameFifo_SetMaxSize(&Fifo, 8));
    for (int i = 6; i < 10; i++)
        DT_ASSERT(DtAvFrameFifo_Push(&Fifo, Frames[i]));
    for (int i = 2; i < 10; i++)
        DT_ASSERT(DtAvFrameFifo_Pop(&Fifo) == Frames[i]);

    // A smaller maximum keeps what is there, and refuses more.
    for (int i = 0; i < 5; i++)
        DT_ASSERT(DtAvFrameFifo_Push(&Fifo, Frames[i]));
    DT_ASSERT_OK(DtAvFrameFifo_SetMaxSize(&Fifo, 2));
    DT_ASSERT_EQ(DtAvFrameFifo_Load(&Fifo), 5);
    DT_ASSERT(!DtAvFrameFifo_Push(&Fifo, Frames[5]));
    DT_ASSERT(DtAvFrameFifo_Pop(&Fifo) == Frames[0]);

    // Growing fails without memory and changes nothing.
    DtAlloc_FailAfter(0);
    DT_ASSERT_EQ(DtAvFrameFifo_SetMaxSize(&Fifo, 100), DTAPI_E_OUT_OF_MEM);
    DtAlloc_FailAfter(-1);
    DT_ASSERT_EQ(DtAvFrameFifo_GetMaxSize(&Fifo), 2);

    // Clearing returns the frames to the pool.
    DT_ASSERT(DtAvFramePool_Return(&Pool, &Frames[0]->Frame));
    DtAvFrameFifo_Clear(&Fifo, &Pool);
    DT_ASSERT_EQ(DtAvFrameFifo_Load(&Fifo), 0);
    DT_ASSERT(!DtAvFrameFifo_TakeOverflow(&Fifo));
    DT_ASSERT_EQ(DtAvFramePool_NumFree(&Pool), 5);

    DtAvFrameFifo_Destroy(&Fifo);
    DtAvFramePool_Destroy(&Pool);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Frame properties +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(FramePropertiesByBytesAndRows)
{
    AvFifo_Frame Frame;
    FrameProperties Props;

    memset(&Frame, 0, sizeof(Frame));
    Frame.NumValidBytes = 1920 * 5 / 2 * 1080;
    Frame.NumRows = 1080;
    DT_ASSERT_OK(GetFrameProperties(&Frame, &Props));
    DT_ASSERT_EQ(Props.Width, 1920);
    DT_ASSERT_EQ(Props.Height, 1080);
    DT_ASSERT_EQ(Props.BitDepth, 10);
    DT_ASSERT_EQ(Props.IsInterlaced, 0);
    DT_ASSERT_EQ(Props.BytesPerLine, 4800);
    DT_ASSERT_EQ(Props.NLines, 1080);
    DT_ASSERT_EQ(Props.Subsampling, ChromaSubsampling_422);

    // A field of 1080i in 8 bits, and of 576i in 16.
    Frame.NumValidBytes = 1920 * 2 * 540;
    Frame.NumRows = 540;
    DT_ASSERT_OK(GetFrameProperties(&Frame, &Props));
    DT_ASSERT_EQ(Props.IsInterlaced, 1);
    DT_ASSERT_EQ(Props.Height, 1080);
    DT_ASSERT_EQ(Props.BitDepth, 8);
    Frame.NumValidBytes = 720 * 4 * 288;
    Frame.NumRows = 288;
    DT_ASSERT_OK(GetFrameProperties(&Frame, &Props));
    DT_ASSERT_EQ(Props.Height, 576);
    DT_ASSERT_EQ(Props.BitDepth, 16);

    // 4:2:0, an unknown size, and no frame.
    Frame.Is420 = 1;
    DT_ASSERT_EQ(GetFrameProperties(&Frame, &Props), DTAPI_E_UNSUP_FORMAT);
    Frame.Is420 = 0;
    Frame.NumRows = 287;
    DT_ASSERT_EQ(GetFrameProperties(&Frame, &Props), DTAPI_E_UNSUP_FORMAT);
    DT_ASSERT_EQ(GetFrameProperties(NULL, &Props), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(GetFrameProperties(&Frame, NULL), DTAPI_E_INVALID_ARG);
}

DT_TEST_MAIN("AvFrame", DT_RUN(PoolGivesAlignedClearedFrames),
             DT_RUN(PoolRefusesWrongReturns), DT_RUN(PoolSurvivesFailedAllocations),
             DT_RUN(FifoKeepsOrderAndOverflows), DT_RUN(FramePropertiesByBytesAndRows))

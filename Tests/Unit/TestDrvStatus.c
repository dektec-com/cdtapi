// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestDrvStatus.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Unit tests for the translation of driver statuses to DTAPI results
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The expected results are written out again here, from DTAPI's DtResultToDtapiResult,
// rather than taken from the implementation, so that a wrong row fails.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h"             // DTAPI result codes.
#include "DtDrvAbi.h"               // The DT_STATUS_ codes.
#include "DtDrvStatus.h"            // Interface under test.
#include "DtTest.h"                 // Test framework.
#include "OAL/OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Status +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct StatusCase
{
    const char* Name;
    uint32_t Status;
    unsigned int Result;
} StatusCase;

#define CASE(Status, Result) {#Status, (uint32_t)DT_STATUS_##Status, DTAPI_E_##Result}

static const StatusCase MappedCases[] = {
    CASE(OUT_OF_MEMORY, OUT_OF_MEM),
    CASE(BUFFER_OVERFLOW, TOO_LONG),
    CASE(INVALID_PARAMETER, INVALID_ARG),
    CASE(NOT_SUPPORTED, NOT_SUPPORTED),
    CASE(NOT_FOUND, NOT_FOUND),
    CASE(NOT_FOUND_INCOMP_FW, NOT_FOUND),
    CASE(TIMEOUT, TIMEOUT),
    CASE(NOT_INITIALISED, NOT_INITIALIZED),
    CASE(CONFIG_ERROR, CONFIG),
    CASE(IN_USE, IN_USE),
    CASE(OUT_OF_RESOURCES, OUT_OF_RESOURCES),
    CASE(POWERDOWN, EVENT_POWER),
    CASE(BUSY, BUSY),
    CASE(UNKNOWN_PHY, NOT_SUPPORTED),
    CASE(NOT_STARTED, NOT_STARTED),
    CASE(MULTICASTLIST_FULL, OUT_OF_RESOURCES),
    CASE(VERSION_MISMATCH, DRIVER_INCOMP),
    CASE(BUF_TOO_SMALL, BUF_TOO_SMALL),
    CASE(BUF_TOO_LARGE, BUF_TOO_LARGE),
    CASE(NO_POWER, NO_POWER),
    CASE(EXCL_ACCESS_REQD, EXCL_ACCESS_REQD),
    CASE(LOCKED, LOCKED),
    CASE(NO_IOSTUB, NOT_IMPLEMENTED),
    CASE(NOT_IMPLEMENTED, NOT_IMPLEMENTED),
    CASE(NOT_ENABLED, INVALID_MODE),
    CASE(INVALID_IN_OPMODE, INVALID_MODE),
    CASE(KEYWORD_ERROR, KEYWORD),
    CASE(TOO_LONG, TOO_LONG),
    CASE(EEPROM_FULL, EEPROM_FULL),
    CASE(ALREADY_OPEN_EXCL, ALREADY_EXCL_ACCESS),

    // DTAPI names these four explicitly, and reports each as a driver failure.
    CASE(IO_PENDING, DEV_DRIVER),
    CASE(CANCELLED, DEV_DRIVER),
    CASE(REQUEUE, DEV_DRIVER),
    CASE(FAIL, DEV_DRIVER),

    // Not named by DTAPI, and so reported through its default.
    CASE(EOF, DEV_DRIVER),
    CASE(NOT_ENOUGH_RIGHTS, DEV_DRIVER),
    CASE(READ_WRITE_ERROR, DEV_DRIVER),
};

#undef CASE

DT_TEST(EveryStatusMapsAsDtapiDoes)
{
    size_t i;

    for (i = 0; i < sizeof(MappedCases) / sizeof(MappedCases[0]); i++)
    {
        const StatusCase* Case = &MappedCases[i];
        unsigned int Result = DtDrvStatusToResult(Case->Status);

        if (Result != Case->Result)
            DT_FAIL("DT_STATUS_%s: expected 0x%X, got 0x%X", Case->Name, Case->Result,
                    Result);
    }
}

// The encoding the other tests rely on, and that the backends' classification expects.
DT_TEST(StatusEncodingMatchesThePlatform)
{
    DT_ASSERT_EQ(DT_STATUS_OK, 0);
#if defined(_WIN32)
    DT_ASSERT_EQ(DT_STATUS_IN_USE, 0xE000000AUL);
#else
    DT_ASSERT_EQ(DT_STATUS_IN_USE, 0x0001000AUL);
#endif
}

DT_TEST(SuccessIsOk)
{
    DT_ASSERT_EQ(DtDrvStatusToResult((uint32_t)DT_STATUS_OK), DTAPI_OK);
}

// A value that is not a DtStatus at all, such as a raw operating system error, is still
// reported as a driver failure rather than passed through as if it were a result.
DT_TEST(UnknownStatusIsDriverFailure)
{
    DT_ASSERT_EQ(DtDrvStatusToResult(0x12345678U), DTAPI_E_DEV_DRIVER);
    DT_ASSERT_EQ(DtDrvStatusToResult(0xFFFFFFFFU), DTAPI_E_DEV_DRIVER);
    DT_ASSERT_EQ(DtDrvStatusToResult((uint32_t)DT_STATUS_ERROR(0xFFFF)),
                 DTAPI_E_DEV_DRIVER);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Outcome +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

DT_TEST(OutcomeOkIsOk)
{
    DT_ASSERT_EQ(DtDrvOutcomeToResult(OS_IOCTL_OK, 0), DTAPI_OK);
}

DT_TEST(OutcomeDriverStatusUsesTheStatus)
{
    DT_ASSERT_EQ(DtDrvOutcomeToResult(OS_IOCTL_DRIVER_STATUS, (uint32_t)DT_STATUS_IN_USE),
                 DTAPI_E_IN_USE);
    DT_ASSERT_EQ(
        DtDrvOutcomeToResult(OS_IOCTL_DRIVER_STATUS, (uint32_t)DT_STATUS_NOT_SUPPORTED),
        DTAPI_E_NOT_SUPPORTED);
}

// Only a driver status is looked up. A status that comes with another outcome is ignored.
DT_TEST(OutcomeOsFailuresIgnoreTheStatus)
{
    DT_ASSERT_EQ(DtDrvOutcomeToResult(OS_IOCTL_NO_RESOURCES, (uint32_t)DT_STATUS_IN_USE),
                 DTAPI_E_OUT_OF_RESOURCES);
    DT_ASSERT_EQ(DtDrvOutcomeToResult(OS_IOCTL_COMMUNICATION, (uint32_t)DT_STATUS_IN_USE),
                 DTAPI_E_COMMUNICATION);
}

DT_TEST(UnknownOutcomeIsCommunication)
{
    DT_ASSERT_EQ(DtDrvOutcomeToResult(-99, 0), DTAPI_E_COMMUNICATION);
    DT_ASSERT_EQ(DtDrvOutcomeToResult(1, 0), DTAPI_E_COMMUNICATION);
}

DT_TEST_MAIN("DrvStatus", DT_RUN(EveryStatusMapsAsDtapiDoes),
             DT_RUN(StatusEncodingMatchesThePlatform), DT_RUN(SuccessIsOk),
             DT_RUN(UnknownStatusIsDriverFailure), DT_RUN(OutcomeOkIsOk),
             DT_RUN(OutcomeDriverStatusUsesTheStatus),
             DT_RUN(OutcomeOsFailuresIgnoreTheStatus),
             DT_RUN(UnknownOutcomeIsCommunication))

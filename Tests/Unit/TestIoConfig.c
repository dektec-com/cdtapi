// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestIoConfig.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDtapiLite includes
#include "CDtapiLite.h"  // DTAPI_IOCONFIG_ codes and result codes.
#include "DtlDrvAbi.h"   // IOCONFIG_NAME_MAX_SIZE, the driver's field size.
#include "DtlIoConfig.h" // Interface under test.
#include "DtlTest.h"     // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Completeness +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The highest code in CDTAPI.h is TODREF_STEADYCLOCK. A constant added to the public
// header without an entry in the X-macro list changes this relation and fails here.
DTL_TEST(TableCoversEveryCode)
{
    DTL_ASSERT_EQ(DtlIoConfigCount(), DTAPI_IOCONFIG_TODREF_STEADYCLOCK + 1);
}

// Every code has a name, and that name leads back to the same code. A table out of
// numeric order, a duplicated name or a missing entry all break the round trip for some
// code.
DTL_TEST(EveryCodeRoundTrips)
{
    char Name[IOCONFIG_NAME_MAX_SIZE];
    int Code;
    int Back;

    for (Code = 0; Code < DtlIoConfigCount(); Code++)
    {
        if (DtlIoConfigGetName(Code, Name, sizeof(Name)) != DTAPI_OK)
            DTL_FAIL("code %d has no name", Code);
        if (Name[0] == '\0')
            DTL_FAIL("code %d has an empty name", Code);
        if (DtlIoConfigGetCode(Name, &Back) != DTAPI_OK || Back != Code)
            DTL_FAIL("code %d -> \"%s\" -> %d", Code, Name, Back);
    }
}

// Every name has to fit the driver's fixed-size field, terminator included.
DTL_TEST(EveryNameFitsTheDriverField)
{
    char Name[256];
    int Code;

    for (Code = 0; Code < DtlIoConfigCount(); Code++)
    {
        DTL_ASSERT_OK(DtlIoConfigGetName(Code, Name, sizeof(Name)));
        if (strlen(Name) + 1 > IOCONFIG_NAME_MAX_SIZE)
            DTL_FAIL("\"%s\" does not fit %d bytes", Name, IOCONFIG_NAME_MAX_SIZE);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Known values +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Spot checks against CDTAPI.h, chosen where the numbering is least obvious: the
// pseudo-values, the first value after the groups, and SDIRX before SDI, which only file
// order explains.
//

static int CodeOf(const char* Name)
{
    int Code;

    return DtlIoConfigGetCode(Name, &Code) == DTAPI_OK ? Code : -100;
}

DTL_TEST(KnownNamesHaveTheirCodes)
{
    DTL_ASSERT_EQ(CodeOf("IODIR"), 0);
    DTL_ASSERT_EQ(CodeOf("IOSTD"), 1);
    DTL_ASSERT_EQ(CodeOf("TODREFSEL"), 9);
    DTL_ASSERT_EQ(CodeOf("TRUE"), 18);
    DTL_ASSERT_EQ(CodeOf("FALSE"), 19);
    DTL_ASSERT_EQ(CodeOf("DISABLED"), 20);
    DTL_ASSERT_EQ(CodeOf("INPUT"), 21);
    DTL_ASSERT_EQ(CodeOf("OUTPUT"), 25);
    DTL_ASSERT_EQ(CodeOf("SDIRX"), 46);
    DTL_ASSERT_EQ(CodeOf("SDI"), 47);
    DTL_ASSERT_EQ(CodeOf("2160P50"), 50);
    DTL_ASSERT_EQ(CodeOf("1080I50"), 67);
    DTL_ASSERT_EQ(CodeOf("TODREF_STEADYCLOCK"), 112);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Edge cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DTL_TEST(EmptyNameAndMinusOneCorrespond)
{
    char Name[8] = "junk";
    int Code = 99;

    DTL_ASSERT_OK(DtlIoConfigGetCode("", &Code));
    DTL_ASSERT_EQ(Code, -1);

    DTL_ASSERT_OK(DtlIoConfigGetName(-1, Name, sizeof(Name)));
    DTL_ASSERT_STR(Name, "");
}

DTL_TEST(UnknownNameIsRefused)
{
    int Code = 99;

    DTL_ASSERT_EQ(DtlIoConfigGetCode("NOSUCHCODE", &Code), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(Code, -1);

    // The match is exact, as in the driver.
    DTL_ASSERT_EQ(DtlIoConfigGetCode("iodir", &Code), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigGetCode("IODIR ", &Code), DTAPI_E_INVALID_ARG);
}

DTL_TEST(CodeOutOfRangeIsRefused)
{
    char Name[16] = "junk";

    DTL_ASSERT_EQ(DtlIoConfigGetName(-2, Name, sizeof(Name)), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_STR(Name, "");
    DTL_ASSERT_EQ(DtlIoConfigGetName(DtlIoConfigCount(), Name, sizeof(Name)),
                  DTAPI_E_INVALID_ARG);
}

// "TODREF_STEADYCLOCK" is eighteen characters: nineteen bytes with its terminator.
DTL_TEST(NameMustFitTheBuffer)
{
    char Name[32];

    DTL_ASSERT_EQ(DtlIoConfigGetName(DTAPI_IOCONFIG_TODREF_STEADYCLOCK, Name, 18),
                  DTAPI_E_BUF_TOO_SMALL);
    DTL_ASSERT_STR(Name, "");

    DTL_ASSERT_OK(DtlIoConfigGetName(DTAPI_IOCONFIG_TODREF_STEADYCLOCK, Name, 19));
    DTL_ASSERT_STR(Name, "TODREF_STEADYCLOCK");
}

DTL_TEST(NullArgumentsAreRefused)
{
    char Name[8];
    int Code;

    DTL_ASSERT_EQ(DtlIoConfigGetCode(NULL, &Code), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(Code, -1);
    DTL_ASSERT_EQ(DtlIoConfigGetCode("IODIR", NULL), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigGetName(0, NULL, 8), DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigGetName(0, Name, 0), DTAPI_E_INVALID_ARG);
}

DTL_TEST_MAIN("IoConfig", DTL_RUN(TableCoversEveryCode), DTL_RUN(EveryCodeRoundTrips),
              DTL_RUN(EveryNameFitsTheDriverField), DTL_RUN(KnownNamesHaveTheirCodes),
              DTL_RUN(EmptyNameAndMinusOneCorrespond), DTL_RUN(UnknownNameIsRefused),
              DTL_RUN(CodeOutOfRangeIsRefused), DTL_RUN(NameMustFitTheBuffer),
              DTL_RUN(NullArgumentsAreRefused))

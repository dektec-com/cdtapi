// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestIoConfig.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

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

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Validation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

typedef struct ValidConfig
{
    int Group;
    int Value;
    int SubValue;
} ValidConfig;

static const ValidConfig ValidConfigs[] = {
#define V(Group, Value, SubValue) {Group, Value, SubValue},
#include "IoConfigValidList.inc"
#undef V
};

#define VALID_CONFIG_COUNT (sizeof(ValidConfigs) / sizeof(ValidConfigs[0]))

static bool IsListedValid(int Group, int Value, int SubValue)
{
    size_t i;

    for (i = 0; i < VALID_CONFIG_COUNT; i++)
    {
        if (ValidConfigs[i].Group == Group && ValidConfigs[i].Value == Value &&
            ValidConfigs[i].SubValue == SubValue)
        {
            return true;
        }
    }
    return false;
}

// Every combination of codes, including -1 and one past the last code in each position,
// against the list worked out the way DTAPI does. This is the check on the parent
// encoding: any wrong kind or parent makes some combination disagree.
DTL_TEST(EveryCombinationMatchesDtapi)
{
    int Count = DtlIoConfigCount();
    int Group, Value, SubValue;
    size_t Valid = 0;

    for (Group = -1; Group <= Count; Group++)
    {
        for (Value = -1; Value <= Count; Value++)
        {
            for (SubValue = -2; SubValue <= Count; SubValue++)
            {
                bool Expected = IsListedValid(Group, Value, SubValue);
                bool Actual = DtlIoConfigIsValid(Group, Value, SubValue) == DTAPI_OK;

                if (Expected != Actual)
                {
                    DTL_FAIL("(%d, %d, %d): expected %s", Group, Value, SubValue,
                             Expected ? "valid" : "invalid");
                    return;
                }
                if (Actual)
                    Valid++;
            }
        }
    }
    DTL_ASSERT_EQ(Valid, VALID_CONFIG_COUNT);
}

// The two configurations CDTAPI itself sends, and the near misses around them.
DTL_TEST(DirectionNeedsItsSubValue)
{
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                     DTAPI_IOCONFIG_INPUT));
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                                     DTAPI_IOCONFIG_OUTPUT));
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, -1),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                     DTAPI_IOCONFIG_OUTPUT),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_DISABLED, -1));
}

// The sub-values with two parents are valid under both.
DTL_TEST(SharedSubValuesBelongToBothOutputs)
{
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                                     DTAPI_IOCONFIG_DBLBUF));
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INTOUTPUT,
                                     DTAPI_IOCONFIG_LOOPTHR));
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                     DTAPI_IOCONFIG_DBLBUF),
                  DTAPI_E_INVALID_ARG);
}

// A boolean I/O capability is set to TRUE or FALSE, and TRUE is not itself a group.
DTL_TEST(BooleanCapabilitiesTakeTrueOrFalse)
{
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_GENLOCKED, DTAPI_IOCONFIG_TRUE, -1));
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_GENREF, DTAPI_IOCONFIG_FALSE, -1));
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_GENLOCKED, DTAPI_IOCONFIG_TRUE,
                                     DTAPI_IOCONFIG_TRUE),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_TRUE, DTAPI_IOCONFIG_TRUE, -1),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_TRUE, -1),
                  DTAPI_E_INVALID_ARG);
}

DTL_TEST(VideoStandardsBelongToTheirRate)
{
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_HDSDI,
                                     DTAPI_IOCONFIG_1080I50));
    DTL_ASSERT_OK(DtlIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1));
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_3GSDI,
                                     DTAPI_IOCONFIG_1080I50),
                  DTAPI_E_INVALID_ARG);
    DTL_ASSERT_EQ(DtlIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_HDSDI, -1),
                  DTAPI_E_INVALID_ARG);
}

DTL_TEST_MAIN("IoConfig", DTL_RUN(TableCoversEveryCode), DTL_RUN(EveryCodeRoundTrips),
              DTL_RUN(EveryNameFitsTheDriverField), DTL_RUN(KnownNamesHaveTheirCodes),
              DTL_RUN(EmptyNameAndMinusOneCorrespond), DTL_RUN(UnknownNameIsRefused),
              DTL_RUN(CodeOutOfRangeIsRefused), DTL_RUN(NameMustFitTheBuffer),
              DTL_RUN(NullArgumentsAreRefused), DTL_RUN(EveryCombinationMatchesDtapi),
              DTL_RUN(DirectionNeedsItsSubValue),
              DTL_RUN(SharedSubValuesBelongToBothOutputs),
              DTL_RUN(BooleanCapabilitiesTakeTrueOrFalse),
              DTL_RUN(VideoStandardsBelongToTheirRate))

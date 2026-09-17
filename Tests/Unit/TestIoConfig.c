// #*#*#*#*#*#*#*#*#*#*#*#*#*# TestIoConfig.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Unit tests for I/O configuration codes and driver names
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>

// CDtapiLite includes
#include "CDtapiLite.h" // DTAPI_IOCONFIG_ codes and result codes.
#include "DtIoConfig.h" // Interface under test.
#include "DtPcieAbi.h"  // IOCONFIG_NAME_MAX_SIZE, the driver's field size.
#include "DtTest.h"     // Test framework.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Completeness +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The highest code in CDTAPI.h is TODREF_STEADYCLOCK. A constant added to the public
// header without an entry in the X-macro list changes this relation and fails here.
DT_TEST(TableCoversEveryCode)
{
    DT_ASSERT_EQ(DtIoConfigCount(), DTAPI_IOCONFIG_TODREF_STEADYCLOCK + 1);
}

// Every code has a name, and that name leads back to the same code. A table out of
// numeric order, a duplicated name or a missing entry all break the round trip for some
// code.
DT_TEST(EveryCodeRoundTrips)
{
    char Name[IOCONFIG_NAME_MAX_SIZE];
    int Code;
    int Back;

    for (Code = 0; Code < DtIoConfigCount(); Code++)
    {
        if (DtIoConfigGetName(Code, Name, sizeof(Name)) != DTAPI_OK)
            DT_FAIL("code %d has no name", Code);
        if (Name[0] == '\0')
            DT_FAIL("code %d has an empty name", Code);
        if (DtIoConfigGetCode(Name, &Back) != DTAPI_OK || Back != Code)
            DT_FAIL("code %d -> \"%s\" -> %d", Code, Name, Back);
    }
}

// Every name has to fit the driver's fixed-size field, terminator included.
DT_TEST(EveryNameFitsTheDriverField)
{
    char Name[256];
    int Code;

    for (Code = 0; Code < DtIoConfigCount(); Code++)
    {
        DT_ASSERT_OK(DtIoConfigGetName(Code, Name, sizeof(Name)));
        if (strlen(Name) + 1 > IOCONFIG_NAME_MAX_SIZE)
            DT_FAIL("\"%s\" does not fit %d bytes", Name, IOCONFIG_NAME_MAX_SIZE);
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

    return DtIoConfigGetCode(Name, &Code) == DTAPI_OK ? Code : -100;
}

DT_TEST(KnownNamesHaveTheirCodes)
{
    DT_ASSERT_EQ(CodeOf("IODIR"), 0);
    DT_ASSERT_EQ(CodeOf("IOSTD"), 1);
    DT_ASSERT_EQ(CodeOf("TODREFSEL"), 9);
    DT_ASSERT_EQ(CodeOf("TRUE"), 18);
    DT_ASSERT_EQ(CodeOf("FALSE"), 19);
    DT_ASSERT_EQ(CodeOf("DISABLED"), 20);
    DT_ASSERT_EQ(CodeOf("INPUT"), 21);
    DT_ASSERT_EQ(CodeOf("OUTPUT"), 25);
    DT_ASSERT_EQ(CodeOf("SDIRX"), 46);
    DT_ASSERT_EQ(CodeOf("SDI"), 47);
    DT_ASSERT_EQ(CodeOf("2160P50"), 50);
    DT_ASSERT_EQ(CodeOf("1080I50"), 67);
    DT_ASSERT_EQ(CodeOf("TODREF_STEADYCLOCK"), 112);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Edge cases +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(EmptyNameAndMinusOneCorrespond)
{
    char Name[8] = "junk";
    int Code = 99;

    DT_ASSERT_OK(DtIoConfigGetCode("", &Code));
    DT_ASSERT_EQ(Code, -1);

    DT_ASSERT_OK(DtIoConfigGetName(-1, Name, sizeof(Name)));
    DT_ASSERT_STR(Name, "");
}

DT_TEST(UnknownNameIsRefused)
{
    int Code = 99;

    DT_ASSERT_EQ(DtIoConfigGetCode("NOSUCHCODE", &Code), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Code, -1);

    // The match is exact, as in the driver.
    DT_ASSERT_EQ(DtIoConfigGetCode("iodir", &Code), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigGetCode("IODIR ", &Code), DTAPI_E_INVALID_ARG);
}

DT_TEST(CodeOutOfRangeIsRefused)
{
    char Name[16] = "junk";

    DT_ASSERT_EQ(DtIoConfigGetName(-2, Name, sizeof(Name)), DTAPI_E_INVALID_ARG);
    DT_ASSERT_STR(Name, "");
    DT_ASSERT_EQ(DtIoConfigGetName(DtIoConfigCount(), Name, sizeof(Name)),
                 DTAPI_E_INVALID_ARG);
}

// "TODREF_STEADYCLOCK" is eighteen characters: nineteen bytes with its terminator.
DT_TEST(NameMustFitTheBuffer)
{
    char Name[32];

    DT_ASSERT_EQ(DtIoConfigGetName(DTAPI_IOCONFIG_TODREF_STEADYCLOCK, Name, 18),
                 DTAPI_E_BUF_TOO_SMALL);
    DT_ASSERT_STR(Name, "");

    DT_ASSERT_OK(DtIoConfigGetName(DTAPI_IOCONFIG_TODREF_STEADYCLOCK, Name, 19));
    DT_ASSERT_STR(Name, "TODREF_STEADYCLOCK");
}

DT_TEST(NullArgumentsAreRefused)
{
    char Name[8];
    int Code;

    DT_ASSERT_EQ(DtIoConfigGetCode(NULL, &Code), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(Code, -1);
    DT_ASSERT_EQ(DtIoConfigGetCode("IODIR", NULL), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigGetName(0, NULL, 8), DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigGetName(0, Name, 0), DTAPI_E_INVALID_ARG);
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
DT_TEST(EveryCombinationMatchesDtapi)
{
    int Count = DtIoConfigCount();
    int Group, Value, SubValue;
    size_t Valid = 0;

    for (Group = -1; Group <= Count; Group++)
    {
        for (Value = -1; Value <= Count; Value++)
        {
            for (SubValue = -2; SubValue <= Count; SubValue++)
            {
                bool Expected = IsListedValid(Group, Value, SubValue);
                bool Actual = DtIoConfigIsValid(Group, Value, SubValue) == DTAPI_OK;

                if (Expected != Actual)
                {
                    DT_FAIL("(%d, %d, %d): expected %s", Group, Value, SubValue,
                            Expected ? "valid" : "invalid");
                    return;
                }
                if (Actual)
                    Valid++;
            }
        }
    }
    DT_ASSERT_EQ(Valid, VALID_CONFIG_COUNT);
}

// The two configurations CDTAPI itself sends, and the near misses around them.
DT_TEST(DirectionNeedsItsSubValue)
{
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                   DTAPI_IOCONFIG_INPUT));
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                                   DTAPI_IOCONFIG_OUTPUT));
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                   DTAPI_IOCONFIG_OUTPUT),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_DISABLED, -1));
}

// The sub-values with two parents are valid under both.
DT_TEST(SharedSubValuesBelongToBothOutputs)
{
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_OUTPUT,
                                   DTAPI_IOCONFIG_DBLBUF));
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INTOUTPUT,
                                   DTAPI_IOCONFIG_LOOPTHR));
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_INPUT,
                                   DTAPI_IOCONFIG_DBLBUF),
                 DTAPI_E_INVALID_ARG);
}

// A boolean I/O capability is set to TRUE or FALSE, and TRUE is not itself a group.
DT_TEST(BooleanCapabilitiesTakeTrueOrFalse)
{
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_GENLOCKED, DTAPI_IOCONFIG_TRUE, -1));
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_GENREF, DTAPI_IOCONFIG_FALSE, -1));
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_GENLOCKED, DTAPI_IOCONFIG_TRUE,
                                   DTAPI_IOCONFIG_TRUE),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_TRUE, DTAPI_IOCONFIG_TRUE, -1),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_IODIR, DTAPI_IOCONFIG_TRUE, -1),
                 DTAPI_E_INVALID_ARG);
}

DT_TEST(VideoStandardsBelongToTheirRate)
{
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_HDSDI,
                                   DTAPI_IOCONFIG_1080I50));
    DT_ASSERT_OK(DtIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_ASI, -1));
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_3GSDI,
                                   DTAPI_IOCONFIG_1080I50),
                 DTAPI_E_INVALID_ARG);
    DT_ASSERT_EQ(DtIoConfigIsValid(DTAPI_IOCONFIG_IOSTD, DTAPI_IOCONFIG_HDSDI, -1),
                 DTAPI_E_INVALID_ARG);
}

DT_TEST_MAIN("IoConfig", DT_RUN(TableCoversEveryCode), DT_RUN(EveryCodeRoundTrips),
             DT_RUN(EveryNameFitsTheDriverField), DT_RUN(KnownNamesHaveTheirCodes),
             DT_RUN(EmptyNameAndMinusOneCorrespond), DT_RUN(UnknownNameIsRefused),
             DT_RUN(CodeOutOfRangeIsRefused), DT_RUN(NameMustFitTheBuffer),
             DT_RUN(NullArgumentsAreRefused), DT_RUN(EveryCombinationMatchesDtapi),
             DT_RUN(DirectionNeedsItsSubValue),
             DT_RUN(SharedSubValuesBelongToBothOutputs),
             DT_RUN(BooleanCapabilitiesTakeTrueOrFalse),
             DT_RUN(VideoStandardsBelongToTheirRate))

// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* TestXml.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - Tests for the XML of DtapiService's messages
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The documents are those DtapiService 5.2.8 answered on Linux, as the bytes came.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"  // Allocation seam.
#include "DtTest.h"        // Test framework.
#include "Service/DtXml.h" // Interface under test.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reader +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// The answer to GET_PARVALS, with the list of masters as XML in an attribute and a new
// line inside it.
static const char ParVals[] =
    "<ParIdVals Cnt=\"3\">\n"
    "<ParIdVal ParId=\"19\" VT=\"2\" VV=\"0\"/>\n"
    "<ParIdVal ParId=\"7\" VT=\"5\" VV=\"&lt;V Cnt=&quot;0&quot;/&gt;\n"
    "\"/>\n"
    "<ParIdVal ParId=\"5\" VT=\"4\" VV=\"false\"/>\n"
    "</ParIdVals>\n";

DT_TEST(ServiceAnswerIsRead)
{
    DtXmlElem Root;
    DT_ASSERT_OK(DtXml_Parse(ParVals, &Root));
    DT_ASSERT_STR(Root.Name, "ParIdVals");
    DT_ASSERT_STR(DtXml_Attr(&Root, "Cnt"), "3");
    DT_ASSERT_EQ(Root.NumChildren, 3);
    DT_ASSERT_STR(Root.Children[0].Name, "ParIdVal");
    DT_ASSERT_STR(DtXml_Attr(&Root.Children[1], "VV"), "<V Cnt=\"0\"/>\n");
    bool Enable = true;
    DT_ASSERT(DtXml_AttrBool(&Root.Children[2], "VV", &Enable));
    DT_ASSERT(!Enable);
    DT_ASSERT(DtXml_Child(&Root, "ParIdVal") == &Root.Children[0]);
    DT_ASSERT(DtXml_Child(&Root, "Other") == NULL);
    DT_ASSERT(DtXml_Attr(&Root, "Missing") == NULL);

    // The value is a document of its own.
    DtXmlElem Inner;
    DT_ASSERT_OK(DtXml_Parse(DtXml_Attr(&Root.Children[1], "VV"), &Inner));
    DT_ASSERT_STR(Inner.Name, "V");
    DT_ASSERT_EQ(Inner.NumChildren, 0);
    DtXml_Free(&Inner);
    DtXml_Free(&Root);
    DT_ASSERT(Root.Name == NULL);
}

DT_TEST(EntitiesAndReferencesAreDecoded)
{
    DtXmlElem Root;
    DT_ASSERT_OK(DtXml_Parse("<P Desc='&lt;meanPathDelay&gt; &amp; &apos;x&apos;' "
                             "Num=\"&#65;&#x20AC;&#x1F600;\"/>",
                             &Root));
    DT_ASSERT_STR(DtXml_Attr(&Root, "Desc"), "<meanPathDelay> & 'x'");
    DT_ASSERT_STR(DtXml_Attr(&Root, "Num"), "A\xE2\x82\xAC\xF0\x9F\x98\x80");
    DtXml_Free(&Root);
}

DT_TEST(DeclarationCommentsAndTextAreSkipped)
{
    DtXmlElem Root;
    DT_ASSERT_OK(
        DtXml_Parse("<?xml version=\"1.0\"?>\r\n<!-- a comment -->\n"
                    "<A x = \"1\" >text<!-- c --><B/> more <C y=\"\"></C ></A>\n",
                    &Root));
    DT_ASSERT_STR(Root.Name, "A");
    DT_ASSERT_STR(DtXml_Attr(&Root, "x"), "1");
    DT_ASSERT_EQ(Root.NumChildren, 2);
    DT_ASSERT_STR(Root.Children[0].Name, "B");
    DT_ASSERT_STR(DtXml_Attr(&Root.Children[1], "y"), "");
    DtXml_Free(&Root);
}

DT_TEST(BrokenDocumentsAreRefused)
{
    static const char* const Broken[] = {
        "",
        "   ",
        "text",
        "<A",
        "<A x=\"1\"",
        "<A x=1/>",
        "<A x=\"1/>",
        "<A x/>",
        "<A></B>",
        "<A><B></A>",
        "<A x=\"&unknown;\"/>",
        "<A x=\"&amp\"/>",
        "<A x=\"&#0;\"/>",
        "<A x=\"&#xD800;\"/>",
        "<A x=\"&#x110000;\"/>",
        "<A x=\"&#;\"/>",
        "<A x=\"&#X41;\"/>",
        "<A x=\"a<b\"/>",
        "<A/><B/>",
        "<A/>trailing",
        "<!-- unterminated <A/>",
        "<? unterminated <A/>",
        "</A>",
    };
    for (size_t i = 0; i < sizeof(Broken) / sizeof(Broken[0]); i++)
    {
        DtXmlElem Root;
        if (DtXml_Parse(Broken[i], &Root) != DTAPI_E_COMMUNICATION)
            DT_FAIL("\"%s\" was not refused", Broken[i]);
        DT_ASSERT(Root.Name == NULL && Root.NumChildren == 0);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- NestedDoc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Writes a document of Depth elements, each in the one before, into Doc.
//
static void NestedDoc(char* Doc, size_t Size, int Depth)
{
    size_t Length = 0;
    for (int i = 1; i < Depth; i++)
        Length += (size_t)snprintf(Doc + Length, Size - Length, "<a>");
    Length += (size_t)snprintf(Doc + Length, Size - Length, "<a/>");
    for (int i = 1; i < Depth; i++)
        Length += (size_t)snprintf(Doc + Length, Size - Length, "</a>");
}

DT_TEST(NestingIsLimited)
{
    char Doc[40 * 8];
    DtXmlElem Root;
    NestedDoc(Doc, sizeof(Doc), 32);
    DT_ASSERT_OK(DtXml_Parse(Doc, &Root));
    DtXml_Free(&Root);
    NestedDoc(Doc, sizeof(Doc), 33);
    DT_ASSERT_EQ(DtXml_Parse(Doc, &Root), DTAPI_E_COMMUNICATION);
}

DT_TEST(IntegersAreReadWithTheirRange)
{
    DtXmlElem Root;
    DT_ASSERT_OK(
        DtXml_Parse("<A Min=\"-9223372036854775808\" Max=\"9223372036854775807\" "
                    "Over=\"9223372036854775808\" Neg=\"-12\" Bad=\"12a\" "
                    "Sign=\"-\" Empty=\"\" UMax=\"18446744073709551615\" "
                    "UOver=\"18446744073709551616\" Plus=\"+1\"/>",
                    &Root));
    int64_t Value = 0;
    DT_ASSERT(DtXml_AttrInt(&Root, "Min", &Value));
    DT_ASSERT(Value == INT64_MIN);
    DT_ASSERT(DtXml_AttrInt(&Root, "Max", &Value));
    DT_ASSERT(Value == INT64_MAX);
    DT_ASSERT(DtXml_AttrInt(&Root, "Neg", &Value));
    DT_ASSERT_EQ(Value, -12);
    DT_ASSERT(!DtXml_AttrInt(&Root, "Over", &Value));
    DT_ASSERT(!DtXml_AttrInt(&Root, "Bad", &Value));
    DT_ASSERT(!DtXml_AttrInt(&Root, "Sign", &Value));
    DT_ASSERT(!DtXml_AttrInt(&Root, "Empty", &Value));
    DT_ASSERT(!DtXml_AttrInt(&Root, "Plus", &Value));
    DT_ASSERT(!DtXml_AttrInt(&Root, "Missing", &Value));

    uint64_t Unsigned = 0;
    DT_ASSERT(DtXml_AttrUInt(&Root, "UMax", &Unsigned));
    DT_ASSERT(Unsigned == UINT64_MAX);
    DT_ASSERT(!DtXml_AttrUInt(&Root, "UOver", &Unsigned));
    DT_ASSERT(!DtXml_AttrUInt(&Root, "Neg", &Unsigned));
    DT_ASSERT(!DtXml_AttrUInt(&Root, "Empty", &Unsigned));
    DtXml_Free(&Root);
}

DT_TEST(BoolsAreTrueOrFalse)
{
    DtXmlElem Root;
    DT_ASSERT_OK(DtXml_Parse("<A t=\"true\" T=\"TRUE\" f=\"false\" o=\"1\"/>", &Root));
    bool Value = false;
    DT_ASSERT(DtXml_AttrBool(&Root, "t", &Value) && Value);
    DT_ASSERT(DtXml_AttrBool(&Root, "T", &Value) && Value);
    DT_ASSERT(DtXml_AttrBool(&Root, "f", &Value) && !Value);
    DT_ASSERT(!DtXml_AttrBool(&Root, "o", &Value));
    DtXml_Free(&Root);
}

DT_TEST(ParseFailsCleanlyWithoutMemory)
{
    // Each allocation the parse makes fails in turn, and nothing is left behind.
    DtAlloc_ResetCount();
    DtXmlElem Root;
    DT_ASSERT_OK(DtXml_Parse(ParVals, &Root));
    DtXml_Free(&Root);
    int NumAllocations = DtAlloc_NumAllocations();
    DT_ASSERT(NumAllocations > 0);
    for (int i = 0; i < NumAllocations; i++)
    {
        int Live = DtAlloc_NumLive();
        DtAlloc_ResetCount();
        DtAlloc_FailAfter(i);
        DtapiResult Result = DtXml_Parse(ParVals, &Root);
        DtAlloc_ResetCount();
        DT_ASSERT_EQ(Result, DTAPI_E_OUT_OF_MEM);
        DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
    }
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writer +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

DT_TEST(CommandIsWritten)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "ParIds");
    DtXmlOut_AttrInt(&Out, "Cnt", 2);
    DtXmlOut_Open(&Out, "I");
    DtXmlOut_AttrInt(&Out, "Val", 19);
    DtXmlOut_Close(&Out, "I");
    DtXmlOut_Open(&Out, "I");
    DtXmlOut_AttrInt(&Out, "Val", -7);
    DtXmlOut_Close(&Out, "I");
    DtXmlOut_Close(&Out, "ParIds");
    char* Text = NULL;
    DT_ASSERT_OK(DtXmlOut_Finish(&Out, &Text));
    DT_ASSERT_STR(Text, "<ParIds Cnt=\"2\"><I Val=\"19\"/><I Val=\"-7\"/></ParIds>");
    DtAlloc_Free(Text);
    DT_ASSERT(Out.Text == NULL);
}

DT_TEST(AttributeValuesAreEscapedAndReadBack)
{
    static const char Value[] = "<V Cnt=\"1\"> & 'x' \xE2\x82\xAC";
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtXmlOut_Open(&Out, "Attach");
    DtXmlOut_Attr(&Out, "S", Value);
    DtXmlOut_AttrBool(&Out, "Exclusive", false);
    DtXmlOut_AttrInt(&Out, "Serial", 2110000076);
    DtXmlOut_Close(&Out, "Attach");
    char* Text = NULL;
    DT_ASSERT_OK(DtXmlOut_Finish(&Out, &Text));
    DT_ASSERT_STR(Text,
                  "<Attach S=\"&lt;V Cnt=&quot;1&quot;&gt; &amp; 'x' \xE2\x82\xAC\" "
                  "Exclusive=\"false\" Serial=\"2110000076\"/>");

    DtXmlElem Root;
    DtapiResult Result = DtXml_Parse(Text, &Root);
    DtAlloc_Free(Text);
    DT_ASSERT_OK(Result);
    DT_ASSERT_STR(DtXml_Attr(&Root, "S"), Value);
    DtXml_Free(&Root);
}

DT_TEST(EmptyDocumentIsEmptyText)
{
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    char* Text = NULL;
    DT_ASSERT_OK(DtXmlOut_Finish(&Out, &Text));
    DT_ASSERT_STR(Text, "");
    DtAlloc_Free(Text);
}

DT_TEST(WriterReportsMemoryThatRanOut)
{
    int Live = DtAlloc_NumLive();
    DtXmlOut Out;
    DtXmlOut_Init(&Out);
    DtAlloc_ResetCount();
    DtAlloc_FailAfter(0);
    DtXmlOut_Open(&Out, "A");
    DtXmlOut_Close(&Out, "A");
    DtAlloc_ResetCount();
    char* Text = NULL;
    DT_ASSERT_EQ(DtXmlOut_Finish(&Out, &Text), DTAPI_E_OUT_OF_MEM);
    DT_ASSERT(Text == NULL);
    DT_ASSERT_EQ(DtAlloc_NumLive(), Live);
}

DT_TEST_MAIN("Xml", DT_RUN(ServiceAnswerIsRead), DT_RUN(EntitiesAndReferencesAreDecoded),
             DT_RUN(DeclarationCommentsAndTextAreSkipped),
             DT_RUN(BrokenDocumentsAreRefused), DT_RUN(NestingIsLimited),
             DT_RUN(IntegersAreReadWithTheirRange), DT_RUN(BoolsAreTrueOrFalse),
             DT_RUN(ParseFailsCleanlyWithoutMemory), DT_RUN(CommandIsWritten),
             DT_RUN(AttributeValuesAreEscapedAndReadBack),
             DT_RUN(EmptyDocumentIsEmptyText), DT_RUN(WriterReportsMemoryThatRanOut))

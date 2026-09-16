# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Test suites
#
# SPDX-License-Identifier: BSD-3-Clause

# Adds one test executable and registers it with CTest, so that both "ctest" and the
# Visual Studio Test Explorer list it.
function(cdtapilite_add_test SuiteName SourceFile)
    set(TargetName "test_${SuiteName}")
    get_filename_component(SourcePath "${SourceFile}" ABSOLUTE)
    set(FrameworkHeader "${CMAKE_CURRENT_SOURCE_DIR}/DtTest.h")
    add_executable(${TargetName} "${SourcePath}" "${FrameworkHeader}")

    # The same directory-based filters as the library. A generated source lives in the
    # build directory, outside the tree, so it gets a filter of its own.
    string(FIND "${SourcePath}" "${CMAKE_CURRENT_SOURCE_DIR}/" InTree)
    if(InTree EQUAL 0)
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Tests"
                     FILES "${SourcePath}" "${FrameworkHeader}")
    else()
        source_group("Generated" FILES "${SourcePath}")
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Tests"
                     FILES "${FrameworkHeader}")
    endif()

    set_target_properties(${TargetName} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        FOLDER "Tests")
    target_include_directories(${TargetName} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/Source/Device
        ${CMAKE_SOURCE_DIR}/Source/Drv)
    target_link_libraries(${TargetName} PRIVATE cdtapilite)
    target_compile_options(${TargetName} PRIVATE ${CDTAPILITE_WARN_FLAGS})

    # Tests are held to the same standard as the library. A warning here is usually a
    # test that does not assert what it looks like it asserts.
    if(CDTAPILITE_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${TargetName} PRIVATE /WX)
        else()
            target_compile_options(${TargetName} PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME ${SuiteName} COMMAND ${TargetName})

    # An MSVC AddressSanitizer build links the ASan runtime dynamically, and that DLL
    # sits in the compiler's own bin directory rather than anywhere on PATH. Without
    # this the test binaries start and immediately exit with 0xc0000135, DLL not found,
    # which looks like a crash rather than a missing dependency.
    if(MSVC AND CMAKE_C_FLAGS MATCHES "fsanitize=address")
        get_filename_component(CDTAPILITE_MSVC_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
        set_tests_properties(${SuiteName} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CDTAPILITE_MSVC_BIN}")
    endif()
endfunction()

cdtapilite_add_test(Alloc Unit/TestAlloc.c)
cdtapilite_add_test(Api Unit/TestApi.c)
cdtapilite_add_test(Buf Unit/TestBuf.c)
cdtapilite_add_test(Device Unit/TestDevice.c)
cdtapilite_add_test(DmaBuffer Unit/TestDmaBuffer.c)
cdtapilite_add_test(DrvStatus Unit/TestDrvStatus.c)
cdtapilite_add_test(FrameProps Unit/TestFrameProps.c)
cdtapilite_add_test(IoConfig Unit/TestIoConfig.c)
cdtapilite_add_test(IoctlOutcome Unit/TestIoctlOutcome.c)
cdtapilite_add_test(LinIoctlBuffer Unit/TestLinIoctlBuffer.c)
cdtapilite_add_test(LinTime Unit/TestLinTime.c)
cdtapilite_add_test(Ring Unit/TestRing.c)
cdtapilite_add_test(Smpte352 Unit/TestSmpte352.c)
cdtapilite_add_test(Str Unit/TestStr.c)
cdtapilite_add_test(Thread Unit/TestThread.c)
cdtapilite_add_test(Vec Unit/TestVec.c)
cdtapilite_add_test(VidStd Unit/TestVidStd.c)

# The names of result codes are checked against every result code the public header
# defines. The list of codes is read from the header at configure time, so a code added
# there without a name fails the test.
set(CDTAPILITE_CONSTANTS_HEADER "${CMAKE_SOURCE_DIR}/Include/CDtapiLite_Constants.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CDTAPILITE_CONSTANTS_HEADER}")
file(STRINGS "${CDTAPILITE_CONSTANTS_HEADER}" ResultDefines
    REGEX "^#define (DTAPI_OK[A-Z0-9_]*|DTAPI_E_[A-Z0-9_]+) ")
set(ResultCodes "")
foreach(Line IN LISTS ResultDefines)
    string(REGEX MATCH "^#define ([A-Z0-9_]+)" Unused "${Line}")
    string(APPEND ResultCodes "R(${CMAKE_MATCH_1})\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Generated/ResultCodes.inc" "${ResultCodes}")
cdtapilite_add_test(Result Unit/TestResult.c)
target_include_directories(test_Result PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

# The names the example programs give video standards are checked against every video
# standard the public header defines, read from the header at configure time.
if(CDTAPILITE_BUILD_EXAMPLES)
    file(STRINGS "${CDTAPILITE_CONSTANTS_HEADER}" VidStdDefines
         REGEX "^#define DTAPI_VIDSTD_[A-Z0-9_]+ ")
    set(VidStdCodes "")
    foreach(Line IN LISTS VidStdDefines)
        string(REGEX MATCH "^#define (DTAPI_VIDSTD_[A-Z0-9_]+) " Unused "${Line}")
        string(APPEND VidStdCodes "V(${CMAKE_MATCH_1})\n")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Generated/VidStdCodes.inc" "${VidStdCodes}")

    cdtapilite_add_test(ExampleNames Unit/TestExampleNames.c)
    target_sources(test_ExampleNames PRIVATE
        "${CMAKE_SOURCE_DIR}/Examples/Common/ExampleCommon.c")
    target_include_directories(test_ExampleNames PRIVATE
        "${CMAKE_SOURCE_DIR}/Examples" "${CMAKE_CURRENT_BINARY_DIR}/Generated")
endif()

# Including the vendored driver ABI is the test: DtCommon.h carries an ASSERT_SIZE on
# every structure, so a layout change fails this build.
cdtapilite_add_test(Abi Abi/AbiSizes.c)

# Run the OS abstraction, driver and device layers against the emulated device. The
# variable is appended rather than set, so that the AddressSanitizer PATH change is kept.
foreach(SimSuite SimDevice SimCommands SimSdiRx SimFunc SimApi SimDetect)
    cdtapilite_add_test(${SimSuite} Sim/Test${SimSuite}.c)
    set_property(TEST ${SimSuite} APPEND PROPERTY
        ENVIRONMENT_MODIFICATION "CDTAPILITE_SIM=set:1")
endforeach()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ CDTAPI compatibility +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# Checks CDtapiLite against the original CDTAPI.h, which lives outside this repository.
# Nothing derived from it is committed: the checks below are written into the build
# directory at configure time. Two tests:
#
#   CdtapiCompat  compiles three checks into one program.
#     Names       every macro CDTAPI.h defines is defined by CDtapiLite.h, or an #error
#                 generated per name fails the build.
#     Prototypes  every function both headers declare, declared again as CDTAPI.h has
#                 it after CDtapiLite.h, so that a differing signature fails the build.
#     Values      CDtapiLite_Constants.h and CDTAPI.h in one translation unit; a macro
#                 with a different definition fails as a redefinition, warnings being
#                 errors. The full headers cannot share a unit, as both define types.
#     Layout      the size of each shared structure and the offset of each of its
#                 fields, taken once with CDTAPI.h and once with CDtapiLite.h, and
#                 compared when the program runs.
#   CdtapiDropIn  Compat/TestDropIn.c, which includes only CDTAPI.h, linked against
#                 CDtapiLite and run against the emulator.
#
# Where the original header is not found, both are registered as disabled, so that
# CTest reports them as not run instead of the checks silently not existing.

set(CDTAPILITE_CDTAPI_HEADER
    "${CMAKE_SOURCE_DIR}/../CDTAPI/CDTAPI/CDTAPI.h"
    CACHE FILEPATH "Original CDTAPI.h to check the public header against")

# The structures both headers define, and their fields.
set(CompatLayouts
    "DtTimeOfDay:Seconds,Nanoseconds"
    "DtHwFuncDesc:DeviceName,Description,SerialNumber,Port,IsSdi,IsAvFifo,IsInput,IsOutput"
    "DtDetVidStd:VidStd,LinkStd,LinkNr,Vpid,Vpid2,AspectRatio,OriginalVidStd,OriginalLinkStd")

# The enumerators both headers define. They are no macros, so the name and value checks
# do not see them; their values are compared with the layout.
set(CompatEnumerators DT_AR_UNKNOWN DT_AR_4_3 DT_AR_16_9 DT_AR_14_9)

if(EXISTS "${CDTAPILITE_CDTAPI_HEADER}")
    get_filename_component(CompatHeader "${CDTAPILITE_CDTAPI_HEADER}" ABSOLUTE)
    get_filename_component(CompatHeaderDir "${CompatHeader}" DIRECTORY)
    set(CompatDir "${CMAKE_CURRENT_BINARY_DIR}/CdtapiCompat")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CompatHeader}")

    # Names: every #define except the header's own guard.
    file(STRINGS "${CompatHeader}" CompatDefines REGEX "^#define [A-Za-z0-9_]+")
    set(CompatChecks "")
    set(CompatCount 0)
    foreach(Line IN LISTS CompatDefines)
        string(REGEX MATCH "^#define ([A-Za-z0-9_]+)" Unused "${Line}")
        if(CMAKE_MATCH_1 STREQUAL "CDTAPI_H")
            continue()
        endif()
        string(APPEND CompatChecks
            "#ifndef ${CMAKE_MATCH_1}\n"
            "#error \"CDtapiLite.h does not define ${CMAKE_MATCH_1} from CDTAPI.h\"\n"
            "#endif\n")
        math(EXPR CompatCount "${CompatCount} + 1")
    endforeach()
    file(WRITE "${CompatDir}/CdtapiNames.inc" "${CompatChecks}")

    # CDTAPI.h includes its generated version header; any content will do here.
    file(WRITE "${CompatDir}/CDTAPI_Version.h" "#define CDTAPI_VERSION \"compat\"\n")

    file(WRITE "${CompatDir}/CdtapiValues.c"
        "#include \"CDtapiLite_Constants.h\"\n"
        "#include \"${CompatHeader}\"\n"
        "int CdtapiValuesChecked;\n")

    # Prototypes: every function of CDTAPI.h that CDtapiLite.h declares, declared again
    # exactly as CDTAPI.h has it, after CDtapiLite.h. A parameter or return type that
    # differs fails the build as conflicting declarations. The header is read whole and
    # its semicolons masked, because a CMake list would split the lines on them.
    file(READ "${CompatHeader}" CompatText)
    file(READ "${CMAKE_SOURCE_DIR}/Include/CDtapiLite.h" LiteText)
    string(REPLACE ";" "@SEMICOLON@" CompatText "${CompatText}")
    string(REPLACE "\n" ";" CompatLines "${CompatText}")
    set(ProtoLines "")
    set(ProtoCount 0)
    foreach(Line IN LISTS CompatLines)
        if(Line MATCHES "^[A-Za-z].*[ *](Dtapi[A-Za-z0-9]*|Dt[A-Za-z]+_[A-Za-z0-9]+)\\(")
            string(FIND "${LiteText}" " ${CMAKE_MATCH_1}(" LiteDeclares)
            if(LiteDeclares GREATER -1)
                string(REPLACE "@SEMICOLON@" ";" Line "${Line}")
                string(APPEND ProtoLines "${Line}\n")
                math(EXPR ProtoCount "${ProtoCount} + 1")
            endif()
        endif()
    endforeach()
    file(WRITE "${CompatDir}/CdtapiPrototypes.c"
        "#include \"CDtapiLite.h\"\n"
        "${ProtoLines}"
        "const int CdtapiPrototypesChecked = ${ProtoCount};\n")

    # Layout: one table of sizes and offsets per header, in the same order.
    set(LayoutRows "")
    set(LayoutNames "")
    foreach(Layout IN LISTS CompatLayouts)
        string(REPLACE ":" ";" LayoutParts "${Layout}")
        list(GET LayoutParts 0 Type)
        list(GET LayoutParts 1 Fields)
        string(REPLACE "," ";" Fields "${Fields}")
        string(APPEND LayoutRows "    sizeof(${Type}),\n")
        string(APPEND LayoutNames "    \"sizeof(${Type})\",\n")
        foreach(Field IN LISTS Fields)
            string(APPEND LayoutRows "    offsetof(${Type}, ${Field}),\n")
            string(APPEND LayoutNames "    \"${Type}.${Field}\",\n")
        endforeach()
    endforeach()
    foreach(Enumerator IN LISTS CompatEnumerators)
        string(APPEND LayoutRows "    (size_t)${Enumerator},\n")
        string(APPEND LayoutNames "    \"${Enumerator}\",\n")
    endforeach()
    foreach(Side Orig Lite)
        if(Side STREQUAL "Orig")
            set(SideHeader "${CompatHeader}")
        else()
            set(SideHeader "CDtapiLite.h")
        endif()
        file(WRITE "${CompatDir}/CdtapiLayout${Side}.c"
            "#include <stddef.h>\n"
            "#include \"${SideHeader}\"\n"
            "const size_t CdtapiLayout${Side}[] = {\n${LayoutRows}};\n")
    endforeach()

    file(WRITE "${CompatDir}/CdtapiCompat.c"
        "#include \"CDtapiLite.h\"\n"
        "#include \"CdtapiNames.inc\"\n"
        "#include <stdio.h>\n"
        "extern const int CdtapiPrototypesChecked;\n"
        "extern const size_t CdtapiLayoutOrig[];\n"
        "extern const size_t CdtapiLayoutLite[];\n"
        "static const char* const LayoutNames[] = {\n${LayoutNames}};\n"
        "int main(void)\n"
        "{\n"
        "    int Failures = 0;\n"
        "    size_t i;\n"
        "    for (i = 0; i < sizeof(LayoutNames) / sizeof(LayoutNames[0]); i++)\n"
        "    {\n"
        "        if (CdtapiLayoutOrig[i] != CdtapiLayoutLite[i])\n"
        "        {\n"
        "            printf(\"FAIL %s: CDTAPI.h %u, CDtapiLite.h %u\\n\", LayoutNames[i],\n"
        "                   (unsigned)CdtapiLayoutOrig[i], (unsigned)CdtapiLayoutLite[i]);\n"
        "            Failures++;\n"
        "        }\n"
        "    }\n"
        "    printf(\"CDTAPI.h: ${CompatCount} macros, %d prototypes and %u layout \"\n"
        "           \"items checked\\n\", CdtapiPrototypesChecked, (unsigned)i);\n"
        "    return Failures == 0 ? 0 : 1;\n"
        "}\n")

    cdtapilite_add_test(CdtapiCompat "${CompatDir}/CdtapiCompat.c")
    target_sources(test_CdtapiCompat PRIVATE
        "${CompatDir}/CdtapiValues.c"
        "${CompatDir}/CdtapiPrototypes.c"
        "${CompatDir}/CdtapiLayoutOrig.c"
        "${CompatDir}/CdtapiLayoutLite.c")
    source_group("Generated" FILES
        "${CompatDir}/CdtapiValues.c"
        "${CompatDir}/CdtapiPrototypes.c"
        "${CompatDir}/CdtapiLayoutOrig.c"
        "${CompatDir}/CdtapiLayoutLite.c")
    target_include_directories(test_CdtapiCompat PRIVATE "${CompatDir}")

    # The drop-in program sees the original header's directory for CDTAPI.h and the
    # generated version header, and nothing of CDtapiLite's own headers but the tests'.
    cdtapilite_add_test(CdtapiDropIn Compat/TestDropIn.c)
    target_include_directories(test_CdtapiDropIn BEFORE PRIVATE
        "${CompatDir}" "${CompatHeaderDir}")
    set_property(TEST CdtapiDropIn APPEND PROPERTY
        ENVIRONMENT_MODIFICATION "CDTAPILITE_SIM=set:1")

    message(STATUS "CDTAPI compatibility tests: ${CompatCount} macros from ${CompatHeader}")
else()
    foreach(CompatTest CdtapiCompat CdtapiDropIn)
        add_test(NAME ${CompatTest} COMMAND "${CMAKE_COMMAND}" -E echo "skipped")
        set_tests_properties(${CompatTest} PROPERTIES DISABLED TRUE)
    endforeach()
    message(STATUS "CDTAPI compatibility tests disabled: "
                   "${CDTAPILITE_CDTAPI_HEADER} not found")
endif()

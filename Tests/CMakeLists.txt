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
    set(FrameworkHeader "${CMAKE_CURRENT_SOURCE_DIR}/DtlTest.h")
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
cdtapilite_add_test(DmaBuffer Unit/TestDmaBuffer.c)
cdtapilite_add_test(IoConfig Unit/TestIoConfig.c)
cdtapilite_add_test(LinIoctlBuffer Unit/TestLinIoctlBuffer.c)
cdtapilite_add_test(LinTime Unit/TestLinTime.c)
cdtapilite_add_test(Ring Unit/TestRing.c)
cdtapilite_add_test(Str Unit/TestStr.c)
cdtapilite_add_test(Thread Unit/TestThread.c)
cdtapilite_add_test(Vec Unit/TestVec.c)
cdtapilite_add_test(VidStd Unit/TestVidStd.c)

# Including the vendored driver ABI is the test: DtCommon.h carries an ASSERT_SIZE on
# every structure, so a layout change fails this build.
cdtapilite_add_test(Abi Abi/AbiSizes.c)

# Runs the OS abstraction and driver layers against the emulated device. The variable is
# appended rather than set, so that the AddressSanitizer PATH change is kept.
cdtapilite_add_test(SimDevice Sim/TestSimDevice.c)
set_property(TEST SimDevice APPEND PROPERTY
    ENVIRONMENT_MODIFICATION "CDTAPILITE_SIM=set:1")

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ CDTAPI compatibility +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# Checks CDtapiLite.h against the original CDTAPI.h, which lives outside this repository.
# Nothing derived from it is committed: everything below is written into the build
# directory at configure time.
#
#   A macro missing from CDtapiLite.h fails on an #error generated per CDTAPI.h name.
#   A macro with a different definition fails as a redefinition, because both headers
#   are included in one translation unit and warnings are errors.
#
# Where the original header is not found, the test is registered as disabled, so that
# CTest reports it as not run instead of the check silently not existing.

set(CDTAPILITE_CDTAPI_HEADER
    "${CMAKE_SOURCE_DIR}/../CDTAPI/CDTAPI/CDTAPI.h"
    CACHE FILEPATH "Original CDTAPI.h to check the public header against")

if(EXISTS "${CDTAPILITE_CDTAPI_HEADER}")
    get_filename_component(CompatHeader "${CDTAPILITE_CDTAPI_HEADER}" ABSOLUTE)
    set(CompatDir "${CMAKE_CURRENT_BINARY_DIR}/CdtapiCompat")

    file(STRINGS "${CompatHeader}" CompatDefines REGEX "^#define DTAPI_[A-Za-z0-9_]+")
    set(CompatChecks "")
    set(CompatCount 0)
    foreach(Line IN LISTS CompatDefines)
        string(REGEX MATCH "^#define (DTAPI_[A-Za-z0-9_]+)" Unused "${Line}")
        string(APPEND CompatChecks
            "#ifndef ${CMAKE_MATCH_1}\n"
            "#error \"CDtapiLite.h does not define ${CMAKE_MATCH_1} from CDTAPI.h\"\n"
            "#endif\n")
        math(EXPR CompatCount "${CompatCount} + 1")
    endforeach()

    file(WRITE "${CompatDir}/CdtapiNames.inc" "${CompatChecks}")

    # CDTAPI.h includes its generated version header; any content will do here.
    file(WRITE "${CompatDir}/CDTAPI_Version.h" "#define CDTAPI_VERSION \"compat\"\n")

    file(WRITE "${CompatDir}/CdtapiCompat.c"
        "#include \"CDtapiLite.h\"\n"
        "#include \"CdtapiNames.inc\"\n"
        "#include \"${CompatHeader}\"\n"
        "#include <stdio.h>\n"
        "int main(void)\n"
        "{\n"
        "    puts(\"CDTAPI.h: ${CompatCount} macros checked\");\n"
        "    return 0;\n"
        "}\n")

    cdtapilite_add_test(CdtapiCompat "${CompatDir}/CdtapiCompat.c")
    target_include_directories(test_CdtapiCompat PRIVATE "${CompatDir}")
    message(STATUS "CDTAPI compatibility test: ${CompatCount} macros from ${CompatHeader}")
else()
    add_test(NAME CdtapiCompat COMMAND "${CMAKE_COMMAND}" -E echo "skipped")
    set_tests_properties(CdtapiCompat PROPERTIES DISABLED TRUE)
    message(STATUS "CDTAPI compatibility test disabled: "
                   "${CDTAPILITE_CDTAPI_HEADER} not found")
endif()

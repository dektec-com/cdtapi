# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Test suites
#
# SPDX-License-Identifier: BSD-3-Clause

# Adds one test executable and registers it with CTest, so that both "ctest" and the
# Visual Studio Test Explorer list it.
function(cdtapilite_add_test SuiteName SourceFile)
    set(TargetName "test_${SuiteName}")
    add_executable(${TargetName} ${SourceFile})
    set_target_properties(${TargetName} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF)
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
cdtapilite_add_test(Ring Unit/TestRing.c)
cdtapilite_add_test(Str Unit/TestStr.c)
cdtapilite_add_test(Vec Unit/TestVec.c)

# Including the vendored driver ABI is the test: DtCommon.h carries an ASSERT_SIZE on
# every structure, so a layout change fails this build.
cdtapilite_add_test(Abi Abi/AbiSizes.c)

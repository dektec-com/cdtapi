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
    add_test(NAME ${SuiteName} COMMAND ${TargetName})
endfunction()

cdtapilite_add_test(Api Unit/TestApi.c)

# Including the vendored driver ABI is the test: DtCommon.h carries an ASSERT_SIZE on
# every structure, so a layout change fails this build.
cdtapilite_add_test(Abi Abi/AbiSizes.c)

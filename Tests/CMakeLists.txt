# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Test suites
#
# SPDX-License-Identifier: BSD-3-Clause

# Adds one test executable and registers it with CTest, so that both "ctest" and the
# Visual Studio Test Explorer list it.
function(cdtapi_add_test SuiteName SourceFile)
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
        ${CMAKE_SOURCE_DIR}/Source/DtPcie)
    target_link_libraries(${TargetName} PRIVATE cdtapi)
    target_compile_options(${TargetName} PRIVATE ${CDTAPI_WARN_FLAGS})

    # Tests are held to the same standard as the library. A warning here is usually a
    # test that does not assert what it looks like it asserts.
    if(CDTAPI_WARNINGS_AS_ERRORS)
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
        get_filename_component(CDTAPI_MSVC_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
        set_tests_properties(${SuiteName} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CDTAPI_MSVC_BIN}")
    endif()
endfunction()

cdtapi_add_test(Alloc Unit/TestAlloc.c)
cdtapi_add_test(Api Unit/TestApi.c)
cdtapi_add_test(AsiEnc Unit/TestAsiEnc.c)
cdtapi_add_test(AvFrame Unit/TestAvFrame.c)
cdtapi_add_test(AvPacket Unit/TestAvPacket.c)
cdtapi_add_test(AvPixConv Unit/TestAvPixConv.c)
cdtapi_add_test(AvTime Unit/TestAvTime.c)
cdtapi_add_test(Buf Unit/TestBuf.c)
cdtapi_add_test(Device Unit/TestDevice.c)
cdtapi_add_test(DmaBuffer Unit/TestDmaBuffer.c)
cdtapi_add_test(DtPcieStatus Unit/TestDtPcieStatus.c)
cdtapi_add_test(DtPcieVidStd Unit/TestDtPcieVidStd.c)
cdtapi_add_test(TsTrp Unit/TestTsTrp.c)
cdtapi_add_test(EthIp Unit/TestEthIp.c)
cdtapi_add_test(FrameProps Unit/TestFrameProps.c)
cdtapi_add_test(IoConfig Unit/TestIoConfig.c)
cdtapi_add_test(IoctlOutcome Unit/TestIoctlOutcome.c)
cdtapi_add_test(LinIoctlBuffer Unit/TestLinIoctlBuffer.c)
cdtapi_add_test(LinTime Unit/TestLinTime.c)
cdtapi_add_test(Net Unit/TestNet.c)
cdtapi_add_test(NetAddr Unit/TestNetAddr.c)
cdtapi_add_test(Ring Unit/TestRing.c)
cdtapi_add_test(SdiFrame Unit/TestSdiFrame.c)
cdtapi_add_test(Smpte352 Unit/TestSmpte352.c)
cdtapi_add_test(St2110Audio Unit/TestSt2110Audio.c)
cdtapi_add_test(St2110Video Unit/TestSt2110Video.c)
cdtapi_add_test(Thread Unit/TestThread.c)
cdtapi_add_test(Vec Unit/TestVec.c)
cdtapi_add_test(VidStd Unit/TestVidStd.c)
cdtapi_add_test(WorkerPool Unit/TestWorkerPool.c)

# The throughput of the pixel conversions. A program to run by hand, not a test: timing
# depends on the machine, so nothing about it can pass or fail.
add_executable(BenchAvPixConv Bench/BenchAvPixConv.c)
set_target_properties(BenchAvPixConv PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "Tests")
target_include_directories(BenchAvPixConv PRIVATE ${CMAKE_SOURCE_DIR}/Source
    "${CMAKE_CURRENT_BINARY_DIR}/BenchBuild/$<CONFIG>")
target_link_libraries(BenchAvPixConv PRIVATE cdtapi)
target_compile_options(BenchAvPixConv PRIVATE ${CDTAPI_WARN_FLAGS})

# The configuration and the compiler flags the library was built with, which the benchmark
# prints: the speed of the portable conversions depends on how far the compiler vectorises
# them.
set(BenchFlags "${CMAKE_C_FLAGS}")
foreach(Config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${Config}" ConfigUpper)
    string(APPEND BenchFlags "$<$<CONFIG:${Config}>: ${CMAKE_C_FLAGS_${ConfigUpper}}>")
endforeach()
string(REPLACE "\"" "\\\"" BenchFlags "${BenchFlags}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BenchBuild/$<CONFIG>/BenchBuild.h"
    CONTENT "#define BENCH_CONFIG \"$<CONFIG>\"\n#define BENCH_FLAGS \"${BenchFlags}\"\n")

# The names of result codes are checked against every result code the public header
# defines. The list of codes is read from the header at configure time, so a code added
# there without a name fails the test.
set(CDTAPI_CONSTANTS_HEADER "${CMAKE_SOURCE_DIR}/Include/cdtapi_constants.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CDTAPI_CONSTANTS_HEADER}")
file(STRINGS "${CDTAPI_CONSTANTS_HEADER}" ResultDefines
    REGEX "^#define (DTAPI_OK[A-Z0-9_]*|DTAPI_E_[A-Z0-9_]+) ")
set(ResultCodes "")
foreach(Line IN LISTS ResultDefines)
    string(REGEX MATCH "^#define ([A-Z0-9_]+)" Unused "${Line}")
    string(APPEND ResultCodes "R(${CMAKE_MATCH_1})\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Generated/ResultCodes.inc" "${ResultCodes}")
cdtapi_add_test(Result Unit/TestResult.c)
target_include_directories(test_Result PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

# Every function the public headers declare, as an X-macro list for the conformance
# suite. That suite takes the address of each, so a function that is declared and not
# defined fails the link. It also calls each: a function its source does not call fails
# the configuration here, so that a function added to a header cannot go untested there.
# A function dropped from a header leaves its call without a declaration, which fails the
# build where warnings are errors.
set(PublicHeaders
    "${CMAKE_SOURCE_DIR}/Include/cdtapi.h"
    "${CMAKE_SOURCE_DIR}/Include/cdtapi_avfifo.h")
set(ConformanceSource "${CMAKE_CURRENT_SOURCE_DIR}/Conformance/TestConformance.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PublicHeaders}
                                                                ${ConformanceSource})
file(READ "${ConformanceSource}" ConformanceText)
set(PublicFunctions "")
set(NotCalled "")
foreach(PublicHeader IN LISTS PublicHeaders)
    file(READ "${PublicHeader}" HeaderText)
    string(REGEX MATCHALL "CDTAPI_API[^;(]+\\(" Declarations "${HeaderText}")
    foreach(Declaration IN LISTS Declarations)
        if(NOT Declaration MATCHES "define|__declspec|__attribute__")
            string(REGEX MATCH "([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*\\($" Unused
                   "${Declaration}")
            string(APPEND PublicFunctions "F(${CMAKE_MATCH_1})\n")
            string(FIND "${ConformanceText}" "${CMAKE_MATCH_1}(" Found)
            if(Found EQUAL -1)
                list(APPEND NotCalled "${CMAKE_MATCH_1}")
            endif()
        endif()
    endforeach()
endforeach()
if(NotCalled)
    list(JOIN NotCalled ", " NotCalled)
    message(FATAL_ERROR "Tests/Conformance/TestConformance.c calls none of: ${NotCalled}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Generated/PublicFunctions.inc"
     "${PublicFunctions}")

# The whole interface called once against the emulator, which needs the emulated DTA-2110
# for the AV FIFO.
cdtapi_add_test(Conformance Conformance/TestConformance.c)
target_include_directories(test_Conformance PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/Generated")
set_property(TEST Conformance APPEND PROPERTY
    ENVIRONMENT_MODIFICATION "CDTAPI_SIM=set:1")
set_property(TEST Conformance APPEND PROPERTY
    ENVIRONMENT_MODIFICATION "CDTAPI_SIM_DTA2110=set:1")

# The names the example programs give video standards are checked against every video
# standard the public header defines, read from the header at configure time.
if(CDTAPI_BUILD_EXAMPLES)
    file(STRINGS "${CDTAPI_CONSTANTS_HEADER}" VidStdDefines
         REGEX "^#define DTAPI_VIDSTD_[A-Z0-9_]+ ")
    set(VidStdCodes "")
    foreach(Line IN LISTS VidStdDefines)
        string(REGEX MATCH "^#define (DTAPI_VIDSTD_[A-Z0-9_]+) " Unused "${Line}")
        string(APPEND VidStdCodes "V(${CMAKE_MATCH_1})\n")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Generated/VidStdCodes.inc" "${VidStdCodes}")

    cdtapi_add_test(ExampleNames Unit/TestExampleNames.c)
    target_sources(test_ExampleNames PRIVATE
        "${CMAKE_SOURCE_DIR}/Examples/Common/ExampleCommon.c")
    target_include_directories(test_ExampleNames PRIVATE
        "${CMAKE_SOURCE_DIR}/Examples" "${CMAKE_CURRENT_BINARY_DIR}/Generated")
endif()

# Including the vendored driver ABI is the test: DtCommon.h carries an ASSERT_SIZE on
# every structure, so a layout change fails this build.
cdtapi_add_test(Abi Abi/AbiSizes.c)

# The bytes of the packet header in a pipe's buffer are checked against the bit fields of
# the SDK's EthPrtcls.h, which lives outside this repository and is not vendored: its
# layout is the compiler's, so it is compiled here and nowhere in the library. Where the
# header is not found, the test is registered as disabled.
set(CDTAPI_ETHPRTCLS_HEADER
    "${CMAKE_SOURCE_DIR}/../../Common/Source/EthPrtcls.h"
    CACHE FILEPATH "The SDK's EthPrtcls.h to check the packet header against")
if(EXISTS "${CDTAPI_ETHPRTCLS_HEADER}")
    get_filename_component(EthPrtclsDir "${CDTAPI_ETHPRTCLS_HEADER}" DIRECTORY)
    cdtapi_add_test(EthIpLayout Compat/TestEthIpLayout.c)
    target_include_directories(test_EthIpLayout PRIVATE "${EthPrtclsDir}")

    # The SDK's header has 64-bit and nameless bit fields, which the warnings would refuse.
    if(MSVC)
        target_compile_options(test_EthIpLayout PRIVATE /wd4201 /wd4214)
    else()
        target_compile_options(test_EthIpLayout PRIVATE -Wno-pedantic -Wno-conversion)
    endif()
else()
    add_test(NAME EthIpLayout COMMAND "${CMAKE_COMMAND}" -E echo "skipped")
    set_tests_properties(EthIpLayout PROPERTIES DISABLED TRUE)
endif()

# Run the OS abstraction, driver and device layers against the emulated device. The
# variable is appended rather than set, so that the AddressSanitizer PATH change is kept.
foreach(SimSuite SimDevice SimCommands SimSdiRx SimFunc SimApi SimDetect SimDeviceScan
                 SimChSdiRx SimInpChannel SimSdiTx SimOutpChannel SimAsi SimAsiRx SimAsiTx SimNw SimNet
                 SimAvPipe SimAvFifo SimSdiFiles SimActivate SimClocks)
    cdtapi_add_test(${SimSuite} Sim/Test${SimSuite}.c)
    set_property(TEST ${SimSuite} APPEND PROPERTY
        ENVIRONMENT_MODIFICATION "CDTAPI_SIM=set:1")
endforeach()

# SimSdiFiles leaves a file of frames for an example's run with CDTAPI_SIM_SDI_SOURCE.
set_tests_properties(SimSdiFiles PROPERTIES FIXTURES_SETUP SimSdiSourceFile)

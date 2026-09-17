# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Example programs, and their smoke tests against the emulator
#
# SPDX-License-Identifier: BSD-3-Clause

set(ExamplePrograms DtListDevices DtConfigPort DtDetectVidStd DtReceiveFrames
    DtTransmitFrames)

# Programs that use what CDtapiLite adds to CDTAPI.h, and so build against CDtapiLite.h
# only.
set(ExampleLitePrograms DtListDeviceDescs)

# The original CDTAPI.h, which the Tests directory also looks for. Each program is built
# a second time against it, as a drop-in check, where it is found.
set(CDTAPILITE_CDTAPI_HEADER
    "${CMAKE_SOURCE_DIR}/../CDTAPI/CDTAPI/CDTAPI.h"
    CACHE FILEPATH "Original CDTAPI.h to check the public header against")

if(EXISTS "${CDTAPILITE_CDTAPI_HEADER}")
    get_filename_component(ExampleCdtapiDir "${CDTAPILITE_CDTAPI_HEADER}" DIRECTORY)
    set(ExampleShimDir "${CMAKE_CURRENT_BINARY_DIR}/CdtapiShim")

    # CDTAPI.h includes its generated version header; any content will do here.
    file(WRITE "${ExampleShimDir}/CDTAPI_Version.h" "#define CDTAPI_VERSION \"example\"\n")
    set(ExampleVariants Lite Cdtapi)
else()
    set(ExampleVariants Lite)
endif()

# The variants are named by a word rather than by their suffix, because a CMake list
# cannot hold an empty string on its own: Lite builds against CDtapiLite.h under the
# program's name, Cdtapi against CDTAPI.h under <Program>_Cdtapi.
function(cdtapilite_example_suffix Variant Suffix)
    if(Variant STREQUAL "Cdtapi")
        set(${Suffix} "_Cdtapi" PARENT_SCOPE)
    else()
        set(${Suffix} "" PARENT_SCOPE)
    endif()
endfunction()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Programs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

# Adds one program in one variant.
function(cdtapilite_add_example Program Variant)
    cdtapilite_example_suffix(${Variant} Suffix)
    set(Target "${Program}${Suffix}")
    set(Sources
        "${CMAKE_CURRENT_SOURCE_DIR}/${Program}.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.h")
    add_executable(${Target} ${Sources})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Examples" FILES ${Sources})

    set_target_properties(${Target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        FOLDER "Examples")
    target_include_directories(${Target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${Target} PRIVATE cdtapilite)
    target_compile_options(${Target} PRIVATE ${CDTAPILITE_WARN_FLAGS})
    if(CDTAPILITE_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${Target} PRIVATE /WX)
        else()
            target_compile_options(${Target} PRIVATE -Werror)
        endif()
    endif()

    if(Variant STREQUAL "Cdtapi")
        target_compile_definitions(${Target} PRIVATE EXAMPLE_WITH_CDTAPI)
        target_include_directories(${Target} BEFORE PRIVATE
            "${ExampleShimDir}" "${ExampleCdtapiDir}")
    endif()
endfunction()

foreach(Program IN LISTS ExamplePrograms)
    foreach(Variant IN LISTS ExampleVariants)
        cdtapilite_add_example(${Program} ${Variant})
    endforeach()
endforeach()
foreach(Program IN LISTS ExampleLitePrograms)
    cdtapilite_add_example(${Program} Lite)
endforeach()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Smoke tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# Each program runs against the emulated DTA-2178, and passes when its output matches.
# The regular expression spans lines where a test checks more than one.
#

if(NOT CDTAPILITE_BUILD_TESTS)
    return()
endif()

# Registers a run of Program, in every variant, with the arguments after Expected. A
# program of ExampleLitePrograms runs in its one variant.
function(cdtapilite_example_test Name Program Expected)
    set(Variants ${ExampleVariants})
    if(Program IN_LIST ExampleLitePrograms)
        set(Variants Lite)
    endif()
    foreach(Variant IN LISTS Variants)
        cdtapilite_example_suffix(${Variant} Suffix)
        set(Test "Example${Suffix}_${Name}")
        add_test(NAME ${Test} COMMAND ${Program}${Suffix} ${ARGN})
        set_tests_properties(${Test} PROPERTIES
            PASS_REGULAR_EXPRESSION "${Expected}"
            ENVIRONMENT_MODIFICATION "CDTAPILITE_SIM=set:1")

        # As for the test suites: an MSVC AddressSanitizer build finds its runtime next
        # to the compiler.
        if(MSVC AND CMAKE_C_FLAGS MATCHES "fsanitize=address")
            get_filename_component(MsvcBin "${CMAKE_C_COMPILER}" DIRECTORY)
            set_property(TEST ${Test} APPEND PROPERTY
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${MsvcBin}")
        endif()
    endforeach()
endfunction()

cdtapilite_example_test(ListDevices DtListDevices
    "9217800001:1  DTA-2178 port 1  SDI,INPUT,OUTPUT.*9217800001:9  DTA-2178 port 9  SDI\n.*10 ports")
cdtapilite_example_test(ListDevicesOfNoDevice DtListDevices "^0 ports" --serial 1)
cdtapilite_example_test(ListDevicesHelp DtListDevices "^Usage: .*--serial" --help)

cdtapilite_example_test(ListDeviceDescs DtListDeviceDescs
    "^Device 1\n  Category: 0\n  Serial: 9217800001\n.*  PcieMaxSlotPower: 25000\n1 devices")
cdtapilite_example_test(ListDeviceDescsOfNoDevice DtListDeviceDescs "^0 devices" --serial 1)

cdtapilite_example_test(ConfigPort DtConfigPort
    "9217800001:2  IODIR INPUT  DTAPI_OK\n9217800001:2  IOSTD 3GSDI 1080P50  DTAPI_OK"
    --port 2 --input --vidstd 1080p50)
cdtapilite_example_test(ConfigPortFourLinks DtConfigPort
    "^9217800001:3  IOSTD 3GSDI 1080P60  DTAPI_OK" --port 3 --vidstd 2160P60 --linkstd 0)
cdtapilite_example_test(ConfigPortWithoutLink DtConfigPort
    "DtapiVidStd2IoStd: DTAPI_E_INVALID_LINKSTD" --vidstd 2160P60)
cdtapilite_example_test(ConfigPortUnknownStandard DtConfigPort
    "Unknown video standard: 1080X" --vidstd 1080X)
cdtapilite_example_test(ConfigPortNothingToDo DtConfigPort "^Nothing to do")
cdtapilite_example_test(ConfigPortBadOption DtConfigPort "^Unknown option: --bogus" --bogus)

cdtapilite_example_test(DetectVidStd DtDetectVidStd "^9217800001:1  no video standard")
cdtapilite_example_test(DetectVidStdWaiting DtDetectVidStd
    "^9217800001:1  no video standard" --timeout 50)
cdtapilite_example_test(DetectVidStdOnOutput DtDetectVidStd
    "^9217800001:2  DtDevice_DetectVidStd: DTAPI_E_INVALID_MODE" --port 2)
cdtapilite_example_test(DetectVidStdNoInput DtDetectVidStd
    "^No SDI input that suits" --port 9)
cdtapilite_example_test(DetectVidStdBadNumber DtDetectVidStd
    "^Option --timeout needs a number" --timeout soon)

cdtapilite_example_test(ReceiveFramesWithoutSignal DtReceiveFrames
    "^9217800001:1  no frame within 100 ms" --timeout 100)
cdtapilite_example_test(ReceiveFramesOnOutput DtReceiveFrames
    "^9217800001:2  DtInpChannel_AttachToPort: DTAPI_E_NO_DT_INPUT" --port 2)
cdtapilite_example_test(ReceiveFramesBadMode DtReceiveFrames
    "^Unknown receive mode: 12B" --rxmode 12B)
cdtapilite_example_test(ReceiveFramesDetect DtReceiveFrames
    "^9217800001:1  DtInpChannel_DetectIoStd: DTAPI_E_INVALID_VIDSTD" --detect --timeout 20)

cdtapilite_example_test(TransmitFramesPattern DtTransmitFrames
    "^9217800001:2  frame 0  7425000 bytes  hash [0-9A-F]+\n9217800001:2  frame 1  7425000 bytes  hash [0-9A-F]+\n9217800001:2  frame 2 "
    --port 2 --vidstd 1080I50 --count 3)
cdtapilite_example_test(TransmitFramesSd16Bits DtTransmitFrames
    "^9217800001:2  frame 0  1801800 bytes  hash [0-9A-F]+\n9217800001:2  latched"
    --port 2 --vidstd 525I59_94 --txmode 16B --flags)
cdtapilite_example_test(TransmitFramesOnInput DtTransmitFrames
    "^9217800001:1  DtOutpChannel_AttachToPort: DTAPI_E_NO_DT_OUTPUT"
    --port 1 --vidstd 1080I50)
cdtapilite_example_test(TransmitFrames8Bits DtTransmitFrames
    "^9217800001:2  frame 0  5940000 bytes  hash [0-9A-F]+\n9217800001:2  DtOutpChannel_SetTxControl: DTAPI_E_CONFIG_RAW_SDI"
    --port 2 --vidstd 1080I50 --txmode 8B)
cdtapilite_example_test(TransmitFramesBadMode DtTransmitFrames
    "^Unknown transmit mode: 12B" --txmode 12B)
cdtapilite_example_test(TransmitFramesNothingToSend DtTransmitFrames
    "^Give --in, or --vidstd for the test pattern")
cdtapilite_example_test(TransmitFramesMissingFile DtTransmitFrames
    "^Cannot read no-such-frame0.raw" --in no-such-frame)
cdtapilite_example_test(TransmitFramesNoPattern DtTransmitFrames
    "^No test pattern for video standard 2160P50" --vidstd 2160P50)

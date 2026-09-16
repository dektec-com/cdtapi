# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Example programs, and their smoke tests against the emulator
#
# SPDX-License-Identifier: BSD-3-Clause

set(ExamplePrograms DtListDevices DtConfigPort DtDetectVidStd)

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
    set(ExampleVariants "" "_Cdtapi")
else()
    set(ExampleVariants "")
endif()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Programs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

# Adds one program, built against CDtapiLite.h, or with Variant "_Cdtapi" against
# CDTAPI.h under the name <Program>_Cdtapi.
function(cdtapilite_add_example Program Variant)
    set(Target "${Program}${Variant}")
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

    if(Variant STREQUAL "_Cdtapi")
        target_compile_definitions(${Target} PRIVATE EXAMPLE_WITH_CDTAPI)
        target_include_directories(${Target} BEFORE PRIVATE
            "${ExampleShimDir}" "${ExampleCdtapiDir}")
    endif()
endfunction()

foreach(Program IN LISTS ExamplePrograms)
    foreach(Variant IN LISTS ExampleVariants)
        cdtapilite_add_example(${Program} "${Variant}")
    endforeach()
endforeach()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Smoke tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# Each program runs against the emulated DTA-2178, and passes when its output matches.
# The regular expression spans lines where a test checks more than one.
#

if(NOT CDTAPILITE_BUILD_TESTS)
    return()
endif()

# Registers a run of Program, in every variant, with the arguments after Expected.
function(cdtapilite_example_test Name Program Expected)
    foreach(Variant IN LISTS ExampleVariants)
        set(Test "Example${Variant}_${Name}")
        add_test(NAME ${Test} COMMAND ${Program}${Variant} ${ARGN})
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

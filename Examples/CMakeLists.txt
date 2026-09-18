# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Example programs, and their smoke tests against the emulator
#
# SPDX-License-Identifier: BSD-3-Clause

set(ExamplePrograms DtListDevices DtConfigPort DtDetectVidStd DtReceiveFrames
    DtTransmitFrames DtReceive2110 DtTransmit2110 DtListDeviceDescs)

# Programs of SMPTE ST 2110, which also take ExampleAvFifo.c and the AV FIFO header.
set(ExampleAvPrograms DtReceive2110 DtTransmit2110)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Programs -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

# Adds one program.
function(cdtapi_add_example Program)
    set(Target "${Program}")
    set(Sources
        "${CMAKE_CURRENT_SOURCE_DIR}/${Program}.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleCommon.h")
    if(Program IN_LIST ExampleAvPrograms)
        list(APPEND Sources
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleAvFifo.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/Common/ExampleAvFifo.h")
    endif()
    add_executable(${Target} ${Sources})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Examples" FILES ${Sources})

    set_target_properties(${Target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        FOLDER "Examples")
    target_include_directories(${Target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${Target} PRIVATE cdtapi)
    target_compile_options(${Target} PRIVATE ${CDTAPI_WARN_FLAGS})
    if(CDTAPI_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${Target} PRIVATE /WX)
        else()
            target_compile_options(${Target} PRIVATE -Werror)
        endif()
    endif()

endfunction()

foreach(Program IN LISTS ExamplePrograms)
    cdtapi_add_example(${Program})
endforeach()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Smoke tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
#
# Each program runs against the emulated DTA-2178, and passes when its output matches.
# The regular expression spans lines where a test checks more than one.
#

if(NOT CDTAPI_BUILD_TESTS)
    return()
endif()

# Registers a run of Program with the arguments after Expected. Every entry of
# ExampleExtraEnv, such as "CDTAPI_SIM_DTA2110=set:1", is added to the environment of
# the runs registered while it is set.
function(cdtapi_example_test Name Program Expected)
    set(Test "Example_${Name}")
    add_test(NAME ${Test} COMMAND ${Program} ${ARGN})
    set_tests_properties(${Test} PROPERTIES
        PASS_REGULAR_EXPRESSION "${Expected}"
        ENVIRONMENT_MODIFICATION "CDTAPI_SIM=set:1")
    foreach(Variable IN LISTS ExampleExtraEnv)
        set_property(TEST ${Test} APPEND PROPERTY ENVIRONMENT_MODIFICATION "${Variable}")
    endforeach()

    # As for the test suites: an MSVC AddressSanitizer build finds its runtime next to
    # the compiler.
    if(MSVC AND CMAKE_C_FLAGS MATCHES "fsanitize=address")
        get_filename_component(MsvcBin "${CMAKE_C_COMPILER}" DIRECTORY)
        set_property(TEST ${Test} APPEND PROPERTY
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${MsvcBin}")
    endif()
endfunction()

cdtapi_example_test(ListDevices DtListDevices
    "9217800001:1  DTA-2178 port 1  SDI,ASI,INPUT,OUTPUT.*9217800001:9  DTA-2178 port 9  SDI\n.*10 ports")
cdtapi_example_test(ListDevicesOfNoDevice DtListDevices "^0 ports" --serial 1)
cdtapi_example_test(ListDevicesHelp DtListDevices "^Usage: .*--serial" --help)

cdtapi_example_test(ListDeviceDescs DtListDeviceDescs
    "^Device 1\n  Category: 0\n  Serial: 9217800001\n.*  PcieMaxSlotPower: 25000\n1 devices")
cdtapi_example_test(ListDeviceDescsOfNoDevice DtListDeviceDescs "^0 devices" --serial 1)

cdtapi_example_test(ConfigPort DtConfigPort
    "9217800001:2  IODIR INPUT  DTAPI_OK\n9217800001:2  IOSTD 3GSDI 1080P50  DTAPI_OK"
    --port 2 --input --vidstd 1080p50)
cdtapi_example_test(ConfigPortFourLinks DtConfigPort
    "^9217800001:3  IOSTD 3GSDI 1080P60  DTAPI_OK" --port 3 --vidstd 2160P60 --linkstd 0)
cdtapi_example_test(ConfigPortWithoutLink DtConfigPort
    "DtapiVidStd2IoStd: DTAPI_E_INVALID_LINKSTD" --vidstd 2160P60)
cdtapi_example_test(ConfigPortUnknownStandard DtConfigPort
    "Unknown video standard: 1080X" --vidstd 1080X)
cdtapi_example_test(ConfigPortNothingToDo DtConfigPort "^Nothing to do")
cdtapi_example_test(ConfigPortBadOption DtConfigPort "^Unknown option: --bogus" --bogus)

cdtapi_example_test(DetectVidStd DtDetectVidStd "^9217800001:1  no video standard")
cdtapi_example_test(DetectVidStdWaiting DtDetectVidStd
    "^9217800001:1  no video standard" --timeout 50)
cdtapi_example_test(DetectVidStdOnOutput DtDetectVidStd
    "^9217800001:2  DtDevice_DetectVidStd: DTAPI_E_INVALID_MODE" --port 2)
cdtapi_example_test(DetectVidStdNoInput DtDetectVidStd
    "^No SDI input that suits" --port 9)
cdtapi_example_test(DetectVidStdBadNumber DtDetectVidStd
    "^Option --timeout needs a number" --timeout soon)

cdtapi_example_test(ReceiveFramesWithoutSignal DtReceiveFrames
    "^9217800001:1  no frame within 100 ms" --timeout 100)
cdtapi_example_test(ReceiveFramesOnOutput DtReceiveFrames
    "^9217800001:2  DtInpChannel_AttachToPort: DTAPI_E_NO_DT_INPUT" --port 2)
cdtapi_example_test(ReceiveFramesBadMode DtReceiveFrames
    "^Unknown receive mode: 12B" --rxmode 12B)
cdtapi_example_test(ReceiveFramesDetect DtReceiveFrames
    "^9217800001:1  DtInpChannel_DetectIoStd: DTAPI_E_INVALID_VIDSTD" --detect --timeout 20)

cdtapi_example_test(TransmitFramesPattern DtTransmitFrames
    "^9217800001:2  frame 0  7425000 bytes  hash [0-9A-F]+\n9217800001:2  frame 1  7425000 bytes  hash [0-9A-F]+\n9217800001:2  frame 2 "
    --port 2 --vidstd 1080I50 --count 3)
cdtapi_example_test(TransmitFramesSd16Bits DtTransmitFrames
    "^9217800001:2  frame 0  1801800 bytes  hash [0-9A-F]+\n9217800001:2  latched"
    --port 2 --vidstd 525I59_94 --txmode 16B --flags)
cdtapi_example_test(TransmitFramesOnInput DtTransmitFrames
    "^9217800001:1  DtOutpChannel_AttachToPort: DTAPI_E_NO_DT_OUTPUT"
    --port 1 --vidstd 1080I50)
cdtapi_example_test(TransmitFrames8Bits DtTransmitFrames
    "^9217800001:2  frame 0  5940000 bytes  hash [0-9A-F]+\n9217800001:2  DtOutpChannel_SetTxControl: DTAPI_E_CONFIG_RAW_SDI"
    --port 2 --vidstd 1080I50 --txmode 8B)
cdtapi_example_test(TransmitFramesBadMode DtTransmitFrames
    "^Unknown transmit mode: 12B" --txmode 12B)
cdtapi_example_test(TransmitFramesNothingToSend DtTransmitFrames
    "^Give --in, or --vidstd for the test pattern")
cdtapi_example_test(TransmitFramesMissingFile DtTransmitFrames
    "^Cannot read no-such-frame0.raw" --in no-such-frame)
cdtapi_example_test(TransmitFramesNoPattern DtTransmitFrames
    "^No test pattern for video standard 2160P50" --vidstd 2160P50)

# The ST 2110 programs need the emulated DTA-2110, which the environment adds; the
# loopback makes the packets a program sends arrive at its own receive side, although
# each program has an emulated card of its own and so cannot receive what another sends.
set(ExampleExtraEnv "CDTAPI_SIM_DTA2110=set:1" "CDTAPI_SIM_LOOPBACK=set:1")

cdtapi_example_test(Transmit2110Video DtTransmit2110
    "9211000001:1  hardware pipe  239.1.2.3:5004  320x240 25Hz 10-bit\n9211000001:1  frame 0  192000 bytes  tod .*  rtp .*\n.*sent 2 frames"
    --count 2 --width 320 --height 240 --rate 25)
cdtapi_example_test(Transmit2110EightBit DtTransmit2110
    "320x240 25Hz 8-bit\n9211000001:1  frame 0  153600 bytes"
    --count 1 --width 320 --height 240 --rate 25 --8bit --8bit)
cdtapi_example_test(Transmit2110Audio DtTransmit2110
    "software pipe  239.1.2.3:5004  2 channels L24 48000 Hz\n9211000001:1  frame 0  2880 bytes"
    --audio 2 --count 2)
cdtapi_example_test(Transmit2110ToAddress DtTransmit2110
    "239.9.9.9:5008  320x240 25Hz 10-bit" --ip 239.9.9.9 --udp 5008 --count 1
    --width 320 --height 240 --rate 25)
cdtapi_example_test(Transmit2110BadAddress DtTransmit2110
    "^Not an IPv4 address: 239.1.2" --ip 239.1.2)
cdtapi_example_test(Transmit2110BadPipe DtTransmit2110
    "^Unknown pipe: fast" --pipe fast)
cdtapi_example_test(Transmit2110Help DtTransmit2110 "^Usage: .*--audio" --help)

cdtapi_example_test(Receive2110WithoutStream DtReceive2110
    "9211000001:1  hardware pipe  239.1.2.3:5004  1920x1080 50Hz 10-bit\n9211000001:1  no frame within 100 ms\n9211000001:1  ok 0  incomplete 0  gaps 0  packet errors 0  dropped 0  sync 0"
    --count 1 --timeout 100)
cdtapi_example_test(Receive2110SoftwarePipe DtReceive2110
    "^9211000001:1  software pipe" --count 1 --timeout 20 --pipe sw)
cdtapi_example_test(Receive2110Audio DtReceive2110
    "2 channels L24 48000 Hz" --audio 2 --count 1 --timeout 20)
cdtapi_example_test(Receive2110BadFormat DtReceive2110
    "^Unknown receive format: 12b" --format 12b)

set(ExampleExtraEnv "")

# Without the emulated DTA-2110 there is no IP port at all.
cdtapi_example_test(Transmit2110WithoutPort DtTransmit2110 "^No IP port that suits")
cdtapi_example_test(Receive2110WithoutPort DtReceive2110 "^No IP port that suits")

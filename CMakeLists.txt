# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDTAPI - Top-level build description
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.21)

# The major and minor number are those of the DTAPI whose behaviour CDTAPI reproduces;
# the patch number is CDTAPI's own. The driver is told the major and minor number in
# every property request, see Source/DtPcie/DtPcieCmd.c.
project(CDTAPI
    VERSION 6.13.0
    DESCRIPTION "Native open-source C API for DekTec SDI and SMPTE ST 2110 interfaces"
    HOMEPAGE_URL "https://www.dektec.com"
    LANGUAGES C)

# Where CDTAPI sits beside DTAPI, in the SDK tree, the two version numbers must agree;
# where it stands alone this does nothing. See the script for what it compares.
include("${CMAKE_CURRENT_SOURCE_DIR}/Scripts/check_dtapi_version.cmake")

# Groups the test projects into a Tests folder in the Visual Studio solution.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Options -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

option(CDTAPI_BUILD_TESTS "Build the test suites" ON)
option(CDTAPI_BUILD_EXAMPLES "Build the example programs" ON)
option(CDTAPI_BUILD_SHARED "Build a shared library instead of a static one" OFF)

# The SDK ships one set of files per platform, named as DekTec names them and installed
# in Include, Lib and Bin. A build of the library on its own installs as libraries on the
# platform usually do, which is what a distribution, vcpkg or a developer expects.
option(CDTAPI_SDK_LAYOUT "Install with the SDK's file names and directories" OFF)

# Leaves out the platform driver backends so that the library builds and runs on a
# machine with no DekTec driver installed. The emulated device is always compiled in.
option(CDTAPI_SIM_ONLY "Build without the real driver backends" OFF)

option(CDTAPI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Generated files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/cdtapi_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/cdtapi_version.h"
    @ONLY)

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Library +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

set(CDTAPI_SOURCES
    Source/Api/DtApi.c
    Source/Api/DtResult.c
    Source/AvFifo/DtAvError.c
    Source/AvFifo/DtAvFrame.c
    Source/AvFifo/DtAvFrameProps.c
    Source/AvFifo/DtAvPacket.c
    Source/AvFifo/DtAvPipe.c
    Source/AvFifo/DtAvPort.c
    Source/AvFifo/DtAvRxFifo.c
    Source/AvFifo/DtAvPixConv.c
    Source/AvFifo/DtAvTime.c
    Source/AvFifo/DtAvTxFifo.c
    Source/AvFifo/DtSt2110Audio.c
    Source/AvFifo/DtSt2110Video.c
    Source/Channel/DtAsiRx.c
    Source/Channel/DtInpChannel.c
    Source/Channel/DtSdiRx.c
    Source/Channel/DtOutpChannel.c
    Source/Channel/DtSdiTx.c
    Source/Core/DtAlloc.c
    Source/Core/DtBuf.c
    Source/Core/DtRing.c
    Source/Core/DtVec.c
    Source/Device/DtAvInput.c
    Source/Device/DtDevice.c
    Source/Device/DtFunc.c
    Source/Net/DtNet.c
    Source/DtPcie/DtEthIp.c
    Source/DtPcie/DtPcieCmd.c
    Source/DtPcie/DtPcieCmdAsi.c
    Source/DtPcie/DtPcieCmdPipe.c
    Source/DtPcie/DtPcieCmdSdiTx.c
    Source/DtPcie/DtPcieStatus.c
    Source/DtPcie/DtIoConfig.c
    Source/OAL/OsDispatch.c
    Source/OAL/OsDmaBuffer.c
    Source/OAL/OsIoctlOutcome.c
    Source/OAL/OsNet.c
    Source/OAL/Sim/SimAsi.c
    Source/OAL/Sim/SimChSdiRx.c
    Source/OAL/Sim/SimDta2110.c
    Source/OAL/Sim/SimDta2178.c
    Source/OAL/Sim/SimDtPcie.c
    Source/OAL/Sim/SimNet.c
    Source/OAL/Sim/SimNw.c
    Source/OAL/Sim/SimSdiTx.c
    Source/Ts/DtAsiEnc.c
    Source/Ts/DtTsTrp.c
    Source/Video/DtFrameProps.c
    Source/Video/DtSdiFrame.c
    Source/Video/DtSmpte352.c
    Source/Video/DtVidStd.c)

# The Linux buffer layout contains no Linux-specific code and is compiled everywhere, so
# that it is unit-tested on every platform even though its only user is the Linux backend.
list(APPEND CDTAPI_SOURCES
    Source/OAL/Linux/LinIoctlBuffer.c
    Source/OAL/Linux/LinTime.c)

# The SSSE3 and AVX2 pixel conversions exist for x86 processors only. The library calls
# them when CPUID reports SSSE3 or AVX2, so only their own files are compiled for it.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x86|i[3-6]86)$")
    list(APPEND CDTAPI_SOURCES
        Source/AvFifo/DtAvPixConvSsse3.c
        Source/AvFifo/DtAvPixConvAvx2.c)
    set(CDTAPI_HAVE_SSSE3 ON)
else()
    set(CDTAPI_HAVE_SSSE3 OFF)
endif()

# Threads, events, mutexes, DMA memory and the network belong to the operating system
# rather than to a device, so they are built for the host platform whether or not the
# driver backend is.
if(WIN32)
    list(APPEND CDTAPI_SOURCES
        Source/OAL/Windows/WinMemory.c
        Source/OAL/Windows/WinNet.c
        Source/OAL/Windows/WinThread.c)
else()
    list(APPEND CDTAPI_SOURCES
        Source/OAL/Linux/LinMemory.c
        Source/OAL/Linux/LinNet.c
        Source/OAL/Linux/LinThread.c)
endif()

# The real driver backend for this platform, or a stand-in that finds no hardware.
if(CDTAPI_SIM_ONLY)
    list(APPEND CDTAPI_SOURCES Source/OAL/OsNoPlatform.c)
elseif(WIN32)
    list(APPEND CDTAPI_SOURCES Source/OAL/Windows/WinDtPcie.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CDTAPI_SOURCES Source/OAL/Linux/LinDtPcie.c)
else()
    message(FATAL_ERROR "CDTAPI supports Windows and Linux; "
                        "use CDTAPI_SIM_ONLY=ON to build for emulation elsewhere.")
endif()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IDE layout -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
#
# Everything under Include and Source is added to the target, so that an IDE shows the
# whole tree and not only what this platform compiles: headers, the X-macro tables, and
# the backends for the other platform. Files outside CDTAPI_SOURCES are marked
# HEADER_FILE_ONLY, which lists them without building them. source_group then turns the
# directory structure into the Visual Studio filters.
#
# The glob only decides what is shown. What is compiled stays the explicit list above.

file(GLOB_RECURSE CDTAPI_TREE CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.inc")

list(TRANSFORM CDTAPI_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
     OUTPUT_VARIABLE CDTAPI_BUILT)
set(CDTAPI_SHOWN_ONLY ${CDTAPI_TREE})
list(REMOVE_ITEM CDTAPI_SHOWN_ONLY ${CDTAPI_BUILT})
set_source_files_properties(${CDTAPI_SHOWN_ONLY} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${CDTAPI_TREE})

if(CDTAPI_BUILD_SHARED)
    add_library(cdtapi SHARED ${CDTAPI_BUILT} ${CDTAPI_SHOWN_ONLY})
    target_compile_definitions(cdtapi PRIVATE CDTAPI_EXPORTS)

    # A program that links the DLL through CMake calls straight through the import table.
    # One that links it by hand does not have to define anything; see cdtapi.h.
    target_compile_definitions(cdtapi INTERFACE CDTAPI_DLL)
else()
    add_library(cdtapi STATIC ${CDTAPI_BUILT} ${CDTAPI_SHOWN_ONLY})
endif()

add_library(cdtapi::cdtapi ALIAS cdtapi)

# Where the installed files land. The SDK's layout is Include, Lib and Bin, as its other
# libraries have it and as an application that builds against the SDK already looks for
# them; a build of the library on its own installs as libraries on the platform usually
# do, which is what a distribution, vcpkg or a developer expects. The target's install
# interface names the same directory, so this stands before the target is described.
include(GNUInstallDirs)

if(CDTAPI_SDK_LAYOUT)
    set(CDTAPI_INCLUDE_DIR "Include")
    set(CDTAPI_LIB_DIR "Lib")
    set(CDTAPI_BIN_DIR "Bin")
    set(CDTAPI_PKGCONFIG_DIR "Lib/pkgconfig")
    set(CDTAPI_CMAKE_DIR "Lib/cmake/cdtapi")
else()
    set(CDTAPI_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}")
    set(CDTAPI_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
    set(CDTAPI_BIN_DIR "${CMAKE_INSTALL_BINDIR}")
    set(CDTAPI_PKGCONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
    set(CDTAPI_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/cdtapi")
endif()

set_target_properties(cdtapi PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(cdtapi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Generated>
        $<INSTALL_INTERFACE:${CDTAPI_INCLUDE_DIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Device
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/DtPcie)

if(CDTAPI_HAVE_SSSE3)
    target_compile_definitions(cdtapi PRIVATE
        CDTAPI_HAVE_SSSE3=1 CDTAPI_HAVE_AVX2=1)
    # MSVC compiles the SSSE3 intrinsics without a flag and needs /arch:AVX2 for AVX2;
    # GCC and Clang, clang-cl too, need a flag for each.
    set(PixConvDir "${CMAKE_CURRENT_SOURCE_DIR}/Source/AvFifo")
    if(NOT MSVC OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set_source_files_properties("${PixConvDir}/DtAvPixConvSsse3.c"
            PROPERTIES COMPILE_OPTIONS -mssse3)
        set_source_files_properties("${PixConvDir}/DtAvPixConvAvx2.c"
            PROPERTIES COMPILE_OPTIONS -mavx2)
    else()
        set_source_files_properties("${PixConvDir}/DtAvPixConvAvx2.c"
            PROPERTIES COMPILE_OPTIONS /arch:AVX2)
    endif()
endif()

if(CDTAPI_SIM_ONLY)
    target_compile_definitions(cdtapi PRIVATE CDTAPI_SIM_ONLY=1)
elseif(WIN32)
    # SetupAPI enumerates the DtPcie device interfaces.
    target_link_libraries(cdtapi PRIVATE setupapi)
endif()

if(WIN32)
    # Winsock and IP Helper give the network.
    target_link_libraries(cdtapi PRIVATE ws2_32 iphlpapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(cdtapi PUBLIC Threads::Threads)
endif()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- Warnings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

if(MSVC)
    set(CDTAPI_WARN_FLAGS /W4 /permissive-)
else()
    set(CDTAPI_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

target_compile_options(cdtapi PRIVATE ${CDTAPI_WARN_FLAGS})

if(CDTAPI_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(cdtapi PRIVATE /WX)
    else()
        target_compile_options(cdtapi PRIVATE -Werror)
    endif()
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

if(CDTAPI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Examples +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

if(CDTAPI_BUILD_EXAMPLES)
    add_subdirectory(Examples)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Install +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

# The directories are set where the target is described, because its install interface
# names one of them.

# What the file is called. On Windows a static library fixes which C runtime it calls, so
# the SDK ships one per runtime and configuration, named as DTAPI's own are: cdtapi64mt,
# cdtapi64mtd, cdtapi64md and cdtapi64mdd. The DLL and its import library are cdtapi64,
# built against the dynamic runtime, and serve an application of any runtime. Elsewhere
# the name is plain cdtapi, which is what -lcdtapi asks for.
if(CDTAPI_SDK_LAYOUT AND WIN32)
    if(CDTAPI_BUILD_SHARED)
        set_target_properties(cdtapi PROPERTIES OUTPUT_NAME "cdtapi64")
    else()
        # The dynamic runtime is MSVC's default, and every spelling of the static one
        # leaves out DLL: MultiThreaded, and MultiThreaded$<$<CONFIG:Debug>:Debug>.
        set(CDTAPI_RUNTIME_SUFFIX "md")
        if(CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
            set(CDTAPI_RUNTIME_SUFFIX "mt")
        endif()
        set_target_properties(cdtapi PROPERTIES
            OUTPUT_NAME "cdtapi64${CDTAPI_RUNTIME_SUFFIX}"
            DEBUG_POSTFIX "d")
    endif()
endif()

install(TARGETS cdtapi
    EXPORT cdtapiTargets
    ARCHIVE DESTINATION ${CDTAPI_LIB_DIR}
    LIBRARY DESTINATION ${CDTAPI_LIB_DIR}
    RUNTIME DESTINATION ${CDTAPI_BIN_DIR})

# The DLL's symbols, for a customer who debugs into it. A static library's symbols are in
# the archive itself.
if(CDTAPI_SDK_LAYOUT AND WIN32 AND CDTAPI_BUILD_SHARED)
    install(FILES "$<TARGET_PDB_FILE:cdtapi>" DESTINATION ${CDTAPI_BIN_DIR} OPTIONAL)
endif()

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/cdtapi.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/cdtapi_constants.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/cdtapi_avfifo.h"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/cdtapi_version.h"
    DESTINATION ${CDTAPI_INCLUDE_DIR})

# A pkg-config file, for a consumer that builds outside CMake. The SDK's Windows tree
# leaves it out: a Windows application is not built through pkg-config, and one file
# cannot describe the four static archives, which differ in the runtime they call.
if(NOT (CDTAPI_SDK_LAYOUT AND WIN32))
    # What a static consumer has to link beside the library itself. A shared build carries
    # them, so the .pc names them as private either way.
    if(WIN32)
        set(CDTAPI_SYSTEM_LIBS "-lsetupapi -lws2_32 -liphlpapi")
    else()
        set(CDTAPI_SYSTEM_LIBS "-lpthread")
    endif()

    set(CDTAPI_LINK_NAME "cdtapi")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cdtapi.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cdtapi.pc"
        @ONLY)

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cdtapi.pc"
        DESTINATION ${CDTAPI_PKGCONFIG_DIR})
endif()

# The package find_package(cdtapi CONFIG) loads, which is how a CMake project and vcpkg
# ask for the library. The SDK's Windows tree leaves it out for the reason the .pc is
# left out: its four static archives install into one prefix, and each would write the
# same target file over the one before it.
if(NOT (CDTAPI_SDK_LAYOUT AND WIN32))
    include(CMakePackageConfigHelpers)

    install(EXPORT cdtapiTargets
        FILE cdtapi-targets.cmake
        NAMESPACE cdtapi::
        DESTINATION ${CDTAPI_CMAKE_DIR})

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cdtapi-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cdtapi-config.cmake"
        INSTALL_DESTINATION ${CDTAPI_CMAKE_DIR})

    # A later minor number answers a request for an earlier one: an application that asks
    # for 6.13 is served by the 6.14 that is installed, as it is by any 6.x.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/cdtapi-config-version.cmake"
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/cdtapi-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cdtapi-config-version.cmake"
        DESTINATION ${CDTAPI_CMAKE_DIR})
endif()

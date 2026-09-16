# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
#
# CDtapiLite - Top-level build description
#
# SPDX-License-Identifier: BSD-3-Clause

# 3.21 rather than the SDK's usual 3.15: CMakePresets schema v6 and the Visual Studio 18
# generator both need it. This is the one deliberate deviation from the SDK convention.
cmake_minimum_required(VERSION 3.21)

project(CDtapiLite
    VERSION 1.0.0
    DESCRIPTION "Native open-source C API for DekTec SDI interfaces"
    HOMEPAGE_URL "https://www.dektec.com"
    LANGUAGES C)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Options -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

option(CDTAPILITE_BUILD_TESTS "Build the test suites" ON)
option(CDTAPILITE_BUILD_SHARED "Build a shared library instead of a static one" OFF)

# Leaves out the platform driver backends so that the library builds and runs on a
# machine with no DekTec driver installed. The emulated device is always compiled in.
option(CDTAPILITE_SIM_ONLY "Build without the real driver backends" OFF)

option(CDTAPILITE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Generated files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/CDtapiLite_Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/CDtapiLite_Version.h"
    @ONLY)

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Library +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

set(CDTAPILITE_SOURCES
    Source/Api/DtlApi.c
    Source/Core/DtlAlloc.c
    Source/Core/DtlBuf.c
    Source/Core/DtlRing.c
    Source/Core/DtlStr.c
    Source/Core/DtlVec.c
    Source/Drv/DtlDrv.c
    Source/OAL/OsDispatch.c
    Source/OAL/Sim/SimDtPcie.c)

# The Linux buffer layout contains no Linux-specific code and is compiled everywhere, so
# that it is unit-tested on every platform even though its only user is the Linux backend.
list(APPEND CDTAPILITE_SOURCES Source/OAL/Linux/LinIoctlBuffer.c)

# The real driver backend for this platform, or a stand-in that finds no hardware.
if(CDTAPILITE_SIM_ONLY)
    list(APPEND CDTAPILITE_SOURCES Source/OAL/OsNoPlatform.c)
elseif(WIN32)
    list(APPEND CDTAPILITE_SOURCES Source/OAL/Windows/WinDtPcie.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CDTAPILITE_SOURCES Source/OAL/Linux/LinDtPcie.c)
else()
    message(FATAL_ERROR "CDtapiLite supports Windows and Linux; "
                        "use CDTAPILITE_SIM_ONLY=ON to build for emulation elsewhere.")
endif()

if(CDTAPILITE_BUILD_SHARED)
    add_library(cdtapilite SHARED ${CDTAPILITE_SOURCES})
    target_compile_definitions(cdtapilite PRIVATE CDTAPILITE_EXPORTS)
else()
    add_library(cdtapilite STATIC ${CDTAPILITE_SOURCES})
    target_compile_definitions(cdtapilite PUBLIC CDTAPILITE_STATIC)
endif()

add_library(CDtapiLite::cdtapilite ALIAS cdtapilite)

set_target_properties(cdtapilite PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(cdtapilite
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Generated>
        $<INSTALL_INTERFACE:Include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Drv)

if(CDTAPILITE_SIM_ONLY)
    target_compile_definitions(cdtapilite PRIVATE CDTAPILITE_SIM_ONLY=1)
elseif(WIN32)
    # SetupAPI enumerates the DtPcie device interfaces.
    target_link_libraries(cdtapilite PRIVATE setupapi)
endif()

# .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-- Warnings -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-

if(MSVC)
    set(CDTAPILITE_WARN_FLAGS /W4 /permissive-)
else()
    set(CDTAPILITE_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

target_compile_options(cdtapilite PRIVATE ${CDTAPILITE_WARN_FLAGS})

if(CDTAPILITE_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(cdtapilite PRIVATE /WX)
    else()
        target_compile_options(cdtapilite PRIVATE -Werror)
    endif()
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Tests +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

if(CDTAPILITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Install +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

include(GNUInstallDirs)

install(TARGETS cdtapilite
    EXPORT CDtapiLiteTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/CDtapiLite.h"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/CDtapiLite_Version.h"
    DESTINATION Include)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cdtapilite.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cdtapilite.pc"
    @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cdtapilite.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

## *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2024 DekTec
##
cmake_minimum_required (VERSION 3.20)

## add vcpkg path to toolchain
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain")
endif()

## Set the installation prefix if it’s not set in the command line
if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/export" CACHE PATH "Installation Directory" FORCE)
endif()

## Options
option(VCPKG_BUILD "Set if building within vcpkg" OFF)

project("cdtapi" VERSION 1.1.0 LANGUAGES CXX)

add_subdirectory(src)

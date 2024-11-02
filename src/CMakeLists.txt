## *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* CMakeLists.txt *#*#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2024 DekTec
##

cmake_minimum_required (VERSION 3.20)

## Includes
include(CMakePackageConfigHelpers)

# Find dtapi package
find_package(dtapi CONFIG REQUIRED)

configure_file("cdtapi_version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/cdtapi_version.h" @ONLY)
configure_file("cdtapi.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/cdtapi.pc" @ONLY)

# Create a library project
add_library(cdtapi)
# Set target sources
target_sources(cdtapi
    PRIVATE
        "cdtapi.cpp"
        "cdtapi_avfifo.cpp"       
        "${CMAKE_CURRENT_BINARY_DIR}/generated/cdtapi_version.h"
        "cdtapi.h"
        "cdtapi_avfifo.h"
)

# Set target include directories
target_include_directories(cdtapi
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(cdtapi
  INTERFACE
    $<INSTALL_INTERFACE:"include">
)

# Link to DTAPI
target_link_libraries(cdtapi PRIVATE DTAPI::DTAPI)

# Enable c++17
target_compile_features(cdtapi PRIVATE cxx_std_17)

# Include the GNUInstallDirs module to define standard install directories
include(GNUInstallDirs)

# Generate a version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cdtapiConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Generate configuration/targets file
configure_file(../cmake/cdtapiConfig.cmake.in cdtapiConfig.cmake @ONLY)

message(STATUS "CMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}")
message(STATUS "CMAKE_INSTALL_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}")
message(STATUS "CMAKE_INSTALL_DATAROOTDIR=${CMAKE_INSTALL_DATAROOTDIR}")

# Export the library target so it can be used by other projects with find_package
install(TARGETS cdtapi
        EXPORT cdtapiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

# Export the targets to a CMake file
install(EXPORT cdtapiTargets
        NAMESPACE cdtapi::       # Prefix to identify the target in find_package
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME})

# Install include files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cdtapi_version.h"
    "cdtapi.h"
    "cdtapi_avfifo.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
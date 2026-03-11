cmake_minimum_required(VERSION 3.14)

project(mist
    VERSION      0.1.0
    DESCRIPTION  "MIST — Make It Simple, Toolkit"
    LANGUAGES    CXX
)

# ------------------------------------------------------------------
# Library target
# ------------------------------------------------------------------

add_library(mist STATIC
    src/logger/logger.cxx
    src/logger/logger_types.cxx
    src/logger/progress_bar.cxx
    src/logger/multi_progress_bar.cxx 
    src/ring_finding/hough_transform.cxx
)

target_include_directories(mist
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(mist PUBLIC cxx_std_17)

# Namespaced alias — lets consumers use mist::mist whether they
# find the library via find_package() or add_subdirectory().
add_library(mist::mist ALIAS mist)

# ------------------------------------------------------------------
# Install rules
# ------------------------------------------------------------------

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 1. Install the compiled library
install(TARGETS mist
    EXPORT       mistTargets
    ARCHIVE      DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY      DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME      DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 2. Install the public headers (preserving subdirectory structure)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 3. Install the export set — lets find_package() reconstruct the target
install(EXPORT mistTargets
    FILE        mistTargets.cmake
    NAMESPACE   mist::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mist
)

# 4. Generate and install the version file
#    SameMajorVersion policy: compatible if major version matches.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mistConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# 5. Generate and install the config file (the entry point for find_package)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mistConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mistConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mist
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mistConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mistConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mist
)

# ------------------------------------------------------------------
# Tests  (off by default so downstream projects skip them)
#
# MIST_BUILD_TESTS is an *option* — a CMake boolean cache variable.
# It defaults to OFF, so:
#   • `add_subdirectory(mist)` in another project builds nothing extra.
#   • Developers working on mist itself enable it explicitly:
#       cmake -B build -DMIST_BUILD_TESTS=ON
# ------------------------------------------------------------------

option(MIST_BUILD_TESTS "Build mist test executables" OFF)

if(MIST_BUILD_TESTS)
    # Place all test binaries in <build>/bin/ for easy discovery.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(test_logger test/logger.cxx)

    target_link_libraries(test_logger PRIVATE mist::mist)

    # Inherit the same C++17 requirement as the library.
    target_compile_features(test_logger PRIVATE cxx_std_17)

    message(STATUS "[mist] Tests enabled — binaries will land in ${CMAKE_BINARY_DIR}/bin")
endif()
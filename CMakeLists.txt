cmake_minimum_required(VERSION 3.14)

project(mist
    VERSION      0.1.0
    DESCRIPTION  "MIST — Make It Simple, Toolkit"
    LANGUAGES    CXX
)

# ------------------------------------------------------------------
# Library target
# ------------------------------------------------------------------

add_library(mist
    src/logger/logger_types.cpp
    src/logger/logger.cpp
    src/logger/progress_bar.cpp
    src/logger/progress_bar_registry.cxx
    src/ring_finding/hough_transform.cpp
    # rnd.h is header-only — no .cpp needed
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
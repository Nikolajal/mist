#!/bin/bash
set -e

# Get project root (one directory above scripts/)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# ------------------------------------------------------------------
# Flags
#   --tests / -t   enable MIST_BUILD_TESTS (off by default, mirrors
#                  the CMake option so downstream builds stay clean)
#   --run  / -r    run the test binaries after building (implies -t)
# ------------------------------------------------------------------

BUILD_TESTS=OFF
RUN_TESTS=0

for arg in "$@"; do
    case "$arg" in
        --tests|-t) BUILD_TESTS=ON ;;
        --run|-r)   BUILD_TESTS=ON; RUN_TESTS=1 ;;
        *)
            echo "Usage: $(basename "$0") [--tests|-t] [--run|-r]"
            echo "  -t / --tests   build test executables"
            echo "  -r / --run     build AND run test executables"
            exit 1 ;;
    esac
done

# Clean build directory
[ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"    \
    -DCMAKE_BUILD_TYPE=Release              \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local"   \
    -DMIST_BUILD_TESTS="$BUILD_TESTS"
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

# Optionally run every binary found in build/bin/
if [ "$RUN_TESTS" -eq 1 ]; then
    BIN_DIR="$BUILD_DIR/bin"
    if [ ! -d "$BIN_DIR" ] || [ -z "$(ls "$BIN_DIR" 2>/dev/null)" ]; then
        echo "No test binaries found in $BIN_DIR"
        exit 1
    fi

    FAILED=0
    for test_bin in "$BIN_DIR"/*; do
        [ -x "$test_bin" ] || continue
        echo "--- running $(basename "$test_bin") ---"
        "$test_bin" || { echo "FAILED: $(basename "$test_bin")"; FAILED=1; }
    done

    [ "$FAILED" -eq 0 ] || exit 1
fi
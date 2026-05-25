#!/bin/bash
set -e

# Get project root (one directory above scripts/)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Override install prefix with MIST_INSTALL_PREFIX env var; fall back to ~/.local
INSTALL_PREFIX="${MIST_INSTALL_PREFIX:-$HOME/.local}"

# clean build directory
[ -d "$PROJECT_ROOT/build" ] && rm -rf "$PROJECT_ROOT/build"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"    \
    -DCMAKE_BUILD_TYPE=Release              \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"
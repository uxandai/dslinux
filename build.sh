#!/usr/bin/env bash
#
# Build libdualsense (dev workflow, no install)
#
#   ./build.sh           # release build
#   ./build.sh debug     # debug build with sanitizers
#   ./build.sh clean     # remove build dir
#
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$REPO_DIR/build"

case "${1:-release}" in
    debug)
        cmake -B "$BUILD_DIR" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DDUALSENSE_SANITIZERS=ON \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            "$REPO_DIR"
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        echo "Cleaned."
        exit 0
        ;;
    release|*)
        cmake -B "$BUILD_DIR" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            "$REPO_DIR"
        ;;
esac

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "Built: build/dsctl  build/dualsensed  build/libdualsense.so"
echo ""
echo "Test:  ./build/dsctl info"
echo "       ./build/dsctl trigger right weapon 2 7 8"

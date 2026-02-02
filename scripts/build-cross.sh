#!/usr/bin/env bash
set -euo pipefail

# Simple helper to configure and build using the aarch64 sysroot toolchain
# Usage: ./scripts/build-cross.sh /path/to/pi-sysroot

SYSROOT=${1:-/home/anthony/pi-sysroot/pi-sysroot}
BUILD_DIR=${2:-build-aarch64}
TOOLCHAIN_FILE=cmake/aarch64-pi-toolchain.cmake

mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE \
  -DCMAKE_SYSROOT="$SYSROOT" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -- -j$(nproc)

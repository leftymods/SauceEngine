#!/usr/bin/env bash
# Builds SauceEngine natively on Linux.
#
# Dependencies (Debian/Ubuntu):
#   sudo apt install build-essential cmake ninja-build libglfw3-dev \
#                    libgl1-mesa-dev libpulse-dev
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-linux}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo
echo "Built: $(pwd)/SauceEngine"
echo "Run with: ./SauceEngine -game /path/to/Half-Life\\ 2/hl2"

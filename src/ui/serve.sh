#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# find the most recently configured Emscripten build
BUILD_DIR=$(ls -td cmake-build-emscripten-*/ 2>/dev/null | head -1)
if [ -z "$BUILD_DIR" ]; then
    echo "Error: no Emscripten build directory found." >&2
    echo "Configure first:  cmake --preset emscripten-debug" >&2
    exit 1
fi

echo "=== Building index in ${BUILD_DIR} ==="
cmake --build "$BUILD_DIR" --target index -j 6

exec python3 "${BUILD_DIR}serve_wasm.py"

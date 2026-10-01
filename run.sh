#!/usr/bin/env sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ ! -f build-debug/CMakeCache.txt ]; then
    cmake --preset debug
fi
cmake --build build-debug --parallel 4
exec ./build-debug/vulkan-starter-app "$@"

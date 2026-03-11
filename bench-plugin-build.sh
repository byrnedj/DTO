#!/bin/bash
# bench-plugin-build.sh - Build the GCC plugin, DTO, and benchmark binaries
#
# Usage: ./bench-plugin-build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Building DTO library ==="
make libdto

echo "=== Creating symlinks ==="
ln -sf ./libdto.so.1.0 ./libdto.so.1
ln -sf ./libdto.so.1.0 ./libdto.so

echo "=== Building GCC plugin ==="
PLUGIN_INC=$(gcc -print-file-name=plugin)/include
g++ -shared -fPIC -fno-rtti \
    -o dto_inline_memcpy.so dto_inline_memcpy.cc \
    -I"$PLUGIN_INC"

echo "=== Building benchmarks ==="

# 1. Baseline: no DTO, no plugin (glibc memcpy/memset)
gcc -O2 -fno-builtin -o bench-plugin bench-plugin.c

# 2. With DTO linked (no plugin - DTO intercepts all memcpy/memset)
gcc -O2 -fno-builtin -o bench-plugin-dto bench-plugin.c \
    -L. -ldto -Wl,-rpath,.

# 3. With DTO linked + plugin (plugin inlines bounded ops, DTO only sees large ones)
gcc -O2 -fno-builtin -fplugin=./dto_inline_memcpy.so \
    -o bench-plugin-dto-opt bench-plugin.c \
    -L. -ldto -Wl,-rpath,.

echo "=== Build complete ==="
echo "  bench-plugin          - baseline (glibc)"
echo "  bench-plugin-dto      - DTO linked (all ops intercepted)"
echo "  bench-plugin-dto-opt  - DTO + plugin (small ops inlined)"

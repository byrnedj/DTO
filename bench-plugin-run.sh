#!/bin/bash
# bench-plugin-run.sh - Run benchmarks: baseline vs DTO vs DTO+plugin
#
# Usage: ./bench-plugin-run.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

for bin in bench-plugin bench-plugin-dto bench-plugin-dto-opt; do
    if [ ! -x "$bin" ]; then
        echo "Error: $bin not found. Run ./bench-plugin-build.sh first."
        exit 1
    fi
done

echo "========================================"
echo "  DTO GCC Plugin Benchmark"
echo "========================================"

echo ""
echo "--- Baseline (glibc, no DTO) ---"
./bench-plugin

echo ""
echo "--- DTO linked (no plugin) ---"
DTO_COLLECT_STATS=0 ./bench-plugin-dto

echo ""
echo "--- DTO + Plugin (small ops inlined) ---"
DTO_COLLECT_STATS=0 ./bench-plugin-dto-opt

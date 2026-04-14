#!/bin/bash
# Build the baseline dto_baseline.o from the baseline branch.
# Compiles dto.c to an object file and renames the 4 public symbols
# (memcpy/memset/memcmp/memmove → bl_*) so it can be statically linked
# alongside the current version in the same binary.
#
# Usage: build_baseline.sh <source_dir> <output_dir>
#   source_dir - root of the DTO repo (for git operations)
#   output_dir - where to put dto_baseline.o

set -e

SOURCE_DIR="$1"
OUTPUT_DIR="$2"
BASELINE_OBJ="${OUTPUT_DIR}/dto_baseline.o"
WORKTREE_DIR="${OUTPUT_DIR}/baseline_worktree"
REMOTE="origin"
BRANCH="new_baseline"

mkdir -p "${OUTPUT_DIR}"

# Skip if baseline already built and up-to-date
if [ -f "${BASELINE_OBJ}" ]; then
    cd "${SOURCE_DIR}"
    timeout 20 git fetch "${REMOTE}" "${BRANCH}" --quiet 2>/dev/null || true
    REMOTE_SHA=$(git rev-parse "${REMOTE}/${BRANCH}" 2>/dev/null || echo "unknown")
    BUILT_SHA=""
    if [ -f "${OUTPUT_DIR}/baseline_sha" ]; then
        BUILT_SHA=$(cat "${OUTPUT_DIR}/baseline_sha")
    fi
    if [ "${REMOTE_SHA}" = "${BUILT_SHA}" ]; then
        echo "Baseline dto_baseline.o is up-to-date (${REMOTE}/${BRANCH} = ${REMOTE_SHA:0:12})"
        exit 0
    fi
    echo "Baseline outdated, rebuilding..."
fi

# Clean up any previous worktree
if [ -d "${WORKTREE_DIR}" ]; then
    cd "${SOURCE_DIR}"
    git worktree remove --force "${WORKTREE_DIR}" 2>/dev/null || rm -rf "${WORKTREE_DIR}"
fi

# Create a worktree at the baseline branch
cd "${SOURCE_DIR}"
git fetch "${REMOTE}" "${BRANCH}" --quiet
echo "Building baseline from ${REMOTE}/${BRANCH}..."
git worktree add --detach "${WORKTREE_DIR}" "${REMOTE}/${BRANCH}"

# Compile to object file with same flags as CMake Release
gcc -c -O3 -DNDEBUG -march=native -mwaitpkg -fPIC \
    -D_GNU_SOURCE -DDTO_STATS_SUPPORT \
    "${WORKTREE_DIR}/dto.c" -o "${OUTPUT_DIR}/dto_baseline_raw.o" \
    >>"${OUTPUT_DIR}/build.log" 2>&1

# Rename public symbols: memcpy→bl_memcpy, etc.
objcopy --redefine-sym memcpy=bl_memcpy \
        --redefine-sym memset=bl_memset \
        --redefine-sym memcmp=bl_memcmp \
        --redefine-sym memmove=bl_memmove \
        "${OUTPUT_DIR}/dto_baseline_raw.o" "${BASELINE_OBJ}"

rm -f "${OUTPUT_DIR}/dto_baseline_raw.o"

# Record the SHA we built
BUILT_SHA=$(git rev-parse "${REMOTE}/${BRANCH}")
echo "${BUILT_SHA}" > "${OUTPUT_DIR}/baseline_sha"

# Clean up worktree
cd "${SOURCE_DIR}"
git worktree remove --force "${WORKTREE_DIR}" 2>/dev/null || true

echo "Baseline built: ${BASELINE_OBJ} (${REMOTE}/${BRANCH} = ${BUILT_SHA:0:12})"

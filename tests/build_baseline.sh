#!/bin/bash
# Build the baseline dto_baseline.o from the baseline branch.
# Compiles dto.c to an object file and renames the 4 public symbols
# (memcpy/memset/memcmp/memmove → bl_*) so it can be statically linked
# alongside the current version in the same binary.
#
# Usage: build_baseline.sh <source_dir> <output_dir> [c_compiler] [cflags...]
#   source_dir  - root of the DTO repo (for git operations)
#   output_dir  - where to put dto_baseline.o
#   c_compiler  - C compiler to use (defaults to gcc); pass CMAKE_C_COMPILER so
#                 the baseline object is built with the same compiler as the
#                 current object it is A/B-compared against.
#   cflags...   - compile flags (defines, -march, -mwaitpkg, ...); pass the same
#                 flags used for the current object so the A/B comparison is
#                 apples-to-apples. Defaults to a sensible set if omitted.

set -e

SOURCE_DIR="$1"
OUTPUT_DIR="$2"
CC="${3:-gcc}"
CFLAGS_EXTRA=("${@:4}")
if [ ${#CFLAGS_EXTRA[@]} -eq 0 ]; then
    CFLAGS_EXTRA=(-O3 -DNDEBUG -march=native -fPIC
                 -D_GNU_SOURCE -DDTO_STATS_SUPPORT
                 -DDTO_ACCEL_CONFIG_SUPPORT -DDTO_NUMA_SUPPORT)
fi
BASELINE_OBJ="${OUTPUT_DIR}/dto_baseline.o"
WORKTREE_DIR="${OUTPUT_DIR}/baseline_worktree"
UPSTREAM_URL="https://github.com/intel/DTO.git"
BRANCH="perf_baseline"

mkdir -p "${OUTPUT_DIR}"

# Skip if baseline already built and up-to-date
if [ -f "${BASELINE_OBJ}" ]; then
    cd "${SOURCE_DIR}"
    timeout 20 git fetch "${UPSTREAM_URL}" "${BRANCH}" --quiet 2>/dev/null || true
    REMOTE_SHA=$(git rev-parse FETCH_HEAD 2>/dev/null || echo "unknown")
    BUILT_SHA=""
    if [ -f "${OUTPUT_DIR}/baseline_sha" ]; then
        BUILT_SHA=$(cat "${OUTPUT_DIR}/baseline_sha")
    fi
    if [ "${REMOTE_SHA}" = "${BUILT_SHA}" ]; then
        echo "Baseline dto_baseline.o is up-to-date (${UPSTREAM_URL} ${BRANCH} = ${REMOTE_SHA:0:12})"
        exit 0
    fi
    if [ "${REMOTE_SHA}" = "unknown" ]; then
        # Offline / upstream unreachable but a cached baseline exists: reuse it
        # rather than aborting the whole build on the fetch below.
        echo "WARNING: cannot reach ${UPSTREAM_URL}; reusing cached ${BASELINE_OBJ}" >&2
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
if ! git fetch "${UPSTREAM_URL}" "${BRANCH}" --quiet; then
    echo "ERROR: failed to fetch ${BRANCH} from ${UPSTREAM_URL}" >&2
    if [ -f "${BASELINE_OBJ}" ]; then
        echo "Reusing existing ${BASELINE_OBJ}" >&2
        exit 0
    fi
    exit 1
fi
echo "Building baseline from ${UPSTREAM_URL} ${BRANCH}..."
git worktree add --detach "${WORKTREE_DIR}" FETCH_HEAD

# Compile to object file with the same flags as the current object
"${CC}" -c "${CFLAGS_EXTRA[@]}" \
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
BUILT_SHA=$(git rev-parse FETCH_HEAD)
echo "${BUILT_SHA}" > "${OUTPUT_DIR}/baseline_sha"

# Clean up worktree
cd "${SOURCE_DIR}"
git worktree remove --force "${WORKTREE_DIR}" 2>/dev/null || true

echo "Baseline built: ${BASELINE_OBJ} (${UPSTREAM_URL} ${BRANCH} = ${BUILT_SHA:0:12})"

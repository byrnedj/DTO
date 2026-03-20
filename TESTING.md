# DTO Test Suite

## Prerequisites

DSA hardware tests require configured work queues and hugepage allocation:

```bash
# Configure DSA (run your accel-config setup script)
sudo ./accelConfig.sh 0 yes 0   # Example: 1 work queue, DSA0 enabled

# Allocate hugepages for 2MB page tests
sudo sh -c 'echo 128 > /proc/sys/vm/nr_hugepages'
```

## Build

```bash
cmake -B build -DDTO_BUILD_TESTS=ON
cmake --build build
cd build
```

To disable tests: `-DDTO_BUILD_TESTS=OFF`

## Running Tests

```bash
# Run all tests (functional + perf)
ctest

# Functional tests only (no DSA hardware needed)
ctest --label-regex functional

# Performance tests only (requires DSA hardware)
ctest --label-regex perf

# Verbose output (shows stdout for all tests, not just failures)
ctest -V

# With output on failure only
ctest --output-on-failure
```

## Test Labels

| Label        | Description                            |
|--------------|----------------------------------------|
| `functional` | Correctness tests, no DSA needed       |
| `perf`       | Performance benchmarks, requires DSA   |

## Functional Tests

Correctness tests for `memset`, `memcpy`, `memmove`, `memcmp` across buffer
sizes spanning below and above the DSA offload threshold. Includes
unaligned access and multithreaded tests. These pass with or without DSA
hardware -- DTO falls back to CPU transparently.

```bash
ctest -R functional
```

## Performance Tests

The performance test (`perf_combined`) detects regressions in libdto by
comparing a **baseline** build (from the `perf_baseline` branch on
`github.com/intel/DTO`) against the **current** working tree, both
statically linked into a single
test binary. The test fails if any non-reference benchmark shows a trimmed
mean latency increase exceeding the minimum effect threshold (default 2%).

```bash
ctest -R perf_combined --output-on-failure
```

### How It Works

1. **Build phase** — CMake compiles two object files:
   - `dto_current.o` from the working tree (`cur_memcpy`, `cur_memset`, ...)
   - `dto_baseline.o` from the `perf_baseline` branch on `github.com/intel/DTO`
     via `tests/build_baseline.sh` (`bl_memcpy`, `bl_memset`, ...)

   Both use `objcopy --redefine-sym` so the two versions coexist in the same
   binary with no symbol conflicts. The baseline is cached by git SHA and
   only rebuilt when the upstream branch changes.

2. **Measurement** — For each (benchmark × DTO config × page size) cell:
   - Each cell runs `PERF_AB_ROUNDS` (default 3) forked child processes,
     each executing 10,000 iterations (configurable via `DEFAULT_ITERS`).
   - Each child runs with **randomized interleaving**: a xorshift32 PRNG
     decides whether baseline or current runs first on each iteration,
     eliminating first-mover bias.

3. **Analysis** — Samples from all rounds are pooled, sorted, then:
   - **Trimmed mean** (10th–90th percentile) computes the central tendency,
     excluding outlier tails.
   - **Kolmogorov–Smirnov test** on the trimmed distributions reports the
     D statistic (max ECDF distance) and p-value as a distribution shape
     diagnostic. Note: KS D is sensitive to distribution width, not just
     shift—tight distributions can show large D for tiny absolute changes.
   - **IQR-based outlier count** uses a shared threshold (Q3 + 1.5×IQR from
     the combined baseline+current quartiles) to count outliers per version.
   - **Pass/fail** is based solely on the trimmed mean change exceeding the
     `PERF_MIN_EFFECT` threshold (default 2%).

4. **Output** — Two detail tables (one per page size) show per-cell results,
   followed by a speedup summary comparing each DTO config against raw CPU.
   A `distributions.csv` file is written to `RESULTS_DIR` for offline
   plotting with `tests/plot-distributions.R`.

### A/B Fork Architecture

The following diagram shows the full lifecycle of one cell (one benchmark ×
DTO config × page size combination). The parent process orchestrates
everything; each measurement round runs in a forked child that communicates
results back through shared memory (`MAP_SHARED|MAP_ANONYMOUS`).

```
run_cell(benchmark, dto_config, page_config)
│
│   ┌─────────────────────────────────────────────────────────┐
│   │  MEASUREMENT PHASE — ab_rounds forked children          │
│   └─────────────────────────────────────────────────────────┘
│
├── iters = DEFAULT_ITERS (10,000)
├── alloc pooled arrays: all_bl[rounds×iters], all_cur[rounds×iters]
│
├── for round = 0 .. ab_rounds-1:
│   │
│   ├── fork_ab(iters)
│   │   │
│   │   ├── [parent] setenv(DTO config vars: CSF, AAK, MIN_BYTES, etc.)
│   │   ├── [parent] fork() ─────────────────────────────────────┐
│   │   │                                                        │
│   │   │   ┌────────────────────────────────────────────────────▼───┐
│   │   │   │  CHILD PROCESS (pid == 0)                              │
│   │   │   │                                                        │
│   │   │   │  1. pthread_atfork child handler fires:                │
│   │   │   │     └── dto.c:child() resets dto_initialized = 0       │
│   │   │   │         and calls init_dto() which re-reads env vars,  │
│   │   │   │         reopens DSA work queues, resets auto-tune state│
│   │   │   │                                                        │
│   │   │   │  2. Resolve function pointers:                         │
│   │   │   │     ├── cpu config:  dlopen("libc.so.6") → dlsym       │
│   │   │   │     │   a_memcpy = b_memcpy = libc memcpy (A==B)       │
│   │   │   │     └── dto configs: a_* = bl_* (baseline symbols)     │
│   │   │   │                      b_* = cur_* (current symbols)     │
│   │   │   │                                                        │
│   │   │   │  3. Initialize src[i] = i & 0xFF, dst = src            │
│   │   │   │                                                        │
│   │   │   │  4. Warmup (100 iters of both a and b)                 │
│   │   │   │     └── primes IOTLB entries for DSA                   │
│   │   │   │                                                        │
│   │   │   │  5. Measurement loop (10,000 iters):                   │
│   │   │   │     see "Measurement Loop Detail" below                │
│   │   │   │                                                        │
│   │   │   │  6. qsort(bl_samples), qsort(cur_samples)              │
│   │   │   │  7. slot->nsamples, slot->ok = 1                       │
│   │   │   │  8. exit(0)                                            │
│   │   │   └────────────────────────────────────────────────────────┘
│   │   │
│   │   ├── [parent] waitpid()
│   │   └── [parent] unsetenv(DTO config vars)
│   │
│   └── [parent] memcpy child samples into pooled all_bl[], all_cur[]
│
│   ┌─────────────────────────────────────────────────────────┐
│   │  ANALYSIS PHASE                                         │
│   └─────────────────────────────────────────────────────────┘
│
├── qsort all_bl[], all_cur[]
├── trimmed mean (10th–90th percentile) → bl_ns, cur_ns
├── change = (cur_ns - bl_ns) / bl_ns × 100
├── KS test on trimmed distributions → D statistic, p-value
├── IQR outlier count (shared Q1/Q3 threshold)
├── regression = change > min_effect (default 2%)
└── append raw samples to distributions.csv
```

#### Measurement Loop Detail

Each iteration measures both baseline and current with randomized ordering
to eliminate first-mover bias (e.g., DSA engine idle advantage).

```
seed rng = rdtsc() | 1          ← nonzero seed for xorshift32

for i = 0 .. iters-1:
│
├── xorshift32(rng) → bl_first = (rng & 1)
│
│   ┌── Measure FIRST  ──────────────────────────────┐
│   │ if cold_cache: clflushopt(src), clflushopt(dst)│
│   │ lfence                                         │
│   │ start = rdtsc                                  │
│   │ execute (bl_first ? BASELINE : CURRENT)        │
│   │ COMPILER_BARRIER                               │
│   │ end = rdtscp                                   │
│   └────────────────────────────────────────────────┘
│
│   (memcmp: reset dst = src)
│
│   ┌── Measure SECOND  ─────────────────────────────┐
│   │ if cold_cache: clflushopt(src), clflushopt(dst)│
│   │ lfence                                         │
│   │ start = rdtsc                                  │
│   │ execute (bl_first ? CURRENT : BASELINE)        │
│   │ COMPILER_BARRIER                               │
│   │ end = rdtscp                                   │
│   └────────────────────────────────────────────────┘
│
├── bl_samples[i] = t_bl
└── cur_samples[i] = t_cur
```

#### Shared Memory Layout

The parent allocates one shared memory region (`MAP_SHARED|MAP_ANONYMOUS`)
used by all children sequentially. The child writes directly to this
region; the parent reads it after `waitpid()` returns.

```
shm ── ┌──────────────────────────────────────┐
       │ struct shared_result                 │
       │   .ok              (child sets to 1) │
       │   .nsamples        (actual count)    │
       │   .requested_iters (parent sets)     │
       ├──────────────────────────────────────┤
       │ bl_samples[0..DEFAULT_ITERS-1]       │
       │   (baseline cycle counts, uint64_t)  │
       ├──────────────────────────────────────┤
       │ cur_samples[0..DEFAULT_ITERS-1]      │
       │   (current cycle counts, uint64_t)   │
       └──────────────────────────────────────┘

src ── ┌──────────────────────────────────────┐
       │ Source buffer (MAP_SHARED)           │
       │ 4KB pages or 2MB hugepages           │
       └──────────────────────────────────────┘

dst ── ┌──────────────────────────────────────┐
       │ Destination buffer (MAP_SHARED)      │
       │ 4KB pages or 2MB hugepages           │
       └──────────────────────────────────────┘
```

All buffers use `MAP_SHARED` so the child (a separate process after `fork()`)
operates on the same physical pages as the parent. This avoids copy-on-write
faults during measurement that would add noise.

### Noise Reduction

The test applies several techniques for stable, reproducible measurements:

- **CPU pinning** (`sched_setaffinity`) to a single core (default core 1)
- **Core and uncore frequency pinning** via sysfs (requires root)
- **Cold-cache** benchmarking with `clflushopt` before each iteration (default)
- **Process isolation** via `fork()` — each A/B round runs in a child process,
  triggering DTO's `pthread_atfork` handler to reinitialize DSA state
- **Static linking** — both DTO versions share the same code layout, eliminating
  noise from separate shared library mappings
- **Randomized A/B order** — eliminates systematic first-mover advantage
  (e.g., idle DSA engine bias)

### Test Modes

Four DTO configurations isolate different code paths:

| Config     | What it Measures                                         |
|------------|----------------------------------------------------------|
| `cpu`      | Raw libc (via `dlsym`), no DTO — reference baseline      |
| `stdc`     | DTO linked, forced CPU path (`DTO_USESTDC_CALLS=1`)      |
| `dsa`      | Pure DSA through DTO (`CSF=0`, no auto-tuning)           |
| `dsa_auto` | DSA with CPU+DSA split (`CSF=0.33`, auto-tuning enabled) |

Each config is tested with both **4KB pages** and **2MB hugepages**.

DSA-enabled modes set `DTO_MIN_BYTES=4096` so DSA is exercised for all buffer
sizes in the benchmark.

### Benchmarked Operations

| Benchmark     | Size    | Pass/Fail |
|---------------|---------|-----------|
| `memcpy_4k`   | 4 KB    | Reference only (high CV at small sizes) |
| `memcpy_8k`   | 8 KB    | Reference only |
| `memcpy_16k`  | 16 KB   | Yes |
| `memcpy_32k`  | 32 KB   | Yes |
| `memcpy_64k`  | 64 KB   | Yes |
| `memcpy_128k` | 128 KB  | Yes |
| `memcpy_256k` | 256 KB  | Yes |
| `memcpy_512k` | 512 KB  | Yes |
| `memcpy_1m`   | 1 MB    | Yes |
| `memset_64k`  | 64 KB   | Yes |
| `memset_128k` | 128 KB  | Yes |
| `memset_256k` | 256 KB  | Yes |
| `memset_1m`   | 1 MB    | Yes |
| `memcmp_64k`  | 64 KB   | Yes |
| `memcmp_128k` | 128 KB  | Yes |
| `memcmp_1m`   | 1 MB    | Yes |

### Example Output

Note: Your numbers will differ based on hardware, kernel, and DSA
configuration.

```
DTO Combined A/B Performance Test (cold cache)
================================================
Pinned CPU:    1
TSC freq:      2.000 GHz
Core freq:     2000 MHz (min=2000 max=2000) [pinned]
Uncore freq:   min=2000 max=2000 MHz [pinned]
A/B rounds:    3 (interleaved, randomized order)
Min effect:    2.0%
Page sizes:    4KB, 2MB hugepages
Linking:       static (bl_* / cur_* in same binary)

  ===============================================
  A/B Results — 4KB pages
  ===============================================
  Test          Config    Base mean   Cur mean Change    KS D    Result    Outliers
  ------------- --------- ---------   --------- -------- -----   -------   --------
  memcpy_16k    cpu        4033 ns     4036 ns   +0.1%   0.026   PASS      bl=5 cur=3
                stdc       4035 ns     4034 ns   -0.0%   0.021   PASS      bl=4 cur=6
                dsa        1520 ns     1518 ns   -0.1%   0.018   PASS      bl=2 cur=3
                dsa_auto   1680 ns     1675 ns   -0.3%   0.031   PASS      bl=8 cur=5
  ...

  ===============================================
  Speedup vs CPU (current library)
  ===============================================

  4KB pages:
  Test                stdc     dsa       dsa_auto
  --------------  ---------- ---------- ----------
  memcpy_16k          1.00x     2.66x     2.41x
  ...
```

### Plotting Distributions

The test writes raw sample data to `distributions.csv`. Use the R script to
generate density, violin, and ECDF plots for each operation:

```bash
cd build
Rscript ../tests/plot-distributions.R perf_results/distributions.csv [4k|2m] [memcpy|memset|memcmp]
```

### Baseline Management

The baseline is built automatically from the `perf_baseline` branch on
`github.com/intel/DTO` during `cmake --build`. The build script
(`tests/build_baseline.sh`):

- Fetches the upstream branch and checks the SHA
- Skips the build if the cached `dto_baseline.o` matches the current SHA
- Creates a temporary git worktree to compile the baseline source
- Compiles with the same flags as the current build (`-O3 -DNDEBUG -march=native`)
- Renames symbols via `objcopy` (`memcpy` → `bl_memcpy`, etc.)
- Cleans up the worktree after building

To force a baseline rebuild, delete `build/perf_baseline/dto_baseline.o` and
rebuild.

### Environment Variables

| Variable            | Default                                | Effect                                             |
|---------------------|----------------------------------------|----------------------------------------------------|
| `PERF_CPU`          | `1`                                    | CPU core to pin the benchmark thread to            |
| `PERF_FREQ_MHZ`     | `2000`                                 | Pin core and uncore frequency (MHz); requires root |
| `PERF_MIN_EFFECT`   | `2`                                    | Minimum trimmed mean change % to flag a FAIL       |
| `PERF_AB_ROUNDS`    | `3`                                    | Number of interleaved A/B fork rounds              |
| `PERF_COLD_CACHE`   | `1`                                    | 1 = `clflushopt` per iteration, 0 = flush once     |
| `RESULTS_DIR`       |  ${CMAKE_BINARY_DIR}/perf_results      | Directory for `distributions.csv` output           |

CMake sets `RESULTS_DIR` and `PERF_FREQ_MHZ` automatically when running via
CTest. Override `PERF_FREQ_MHZ` at configure time:

```bash
cmake -B build -DDTO_BUILD_TESTS=ON -DPERF_FREQ_MHZ=2400
```

### Pin to a Different CPU Core

```bash
PERF_CPU=3 ctest -R perf_combined
```

### Adjust Regression Threshold

Default is 2%. To relax (e.g., on noisy systems):

```bash
PERF_MIN_EFFECT=5 ctest -R perf_combined
```

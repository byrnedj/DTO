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
# Run all tests (functional + perf + summary)
ctest

# CI-safe tests only (no DSA hardware needed)
ctest --label-regex ci

# Hardware perf tests only (requires DSA)
ctest --label-regex perf

# Single test by name
ctest -R perf_dsa_2m

# Verbose output (shows stdout for all tests, not just failures)
ctest -V

# With output on failure only
ctest --output-on-failure
```

## Test Labels

| Label      | Description                            |
|------------|----------------------------------------|
| `ci`       | Safe for GitHub Actions, no DSA needed |
| `hardware` | Requires configured DSA work queues    |
| `perf`     | Performance benchmarks                 |

## Functional Tests

Correctness tests for `memset`, `memcpy`, `memmove`, `memcmp` across buffer
sizes spanning below and above the DSA offload threshold. Includes
unaligned access and multithreaded tests. These pass with or without DSA
hardware -- DTO falls back to CPU transparently.

```bash
ctest -R functional
```

## Performance Tests

The performance tests detect throughput regressions introduced by DTO library
changes. Each test mode captures a baseline on the current code, and
subsequent runs compare against that baseline. A test fails if latency
drops below the baseline.

Tests use cold-cache methodology (`clflushopt` before each operation) and
pin the benchmark thread to a single CPU core (default: core 1) for stable
results.

### Test Modes

Four modes isolate different DTO code paths:

| CTest Name            | Binary                  | What it Measures                                        |
|-----------------------|-------------------------|---------------------------------------------------------|
| `perf_cpu_4k/2m`      | `dto-test-perf-cpu`     | Raw libc, no DTO linked                                 |
| `perf_stdc_4k/2m`     | `dto-test-perf`         | DTO linked, forced CPU path (`DTO_USESTDC_CALLS=1`)     |
| `perf_dsa_4k/2m`      | `dto-test-perf`         | Pure DSA through DTO (`CSF=0`, no auto-tuning)          |
| `perf_dsa_auto_4k/2m` | `dto-test-perf`         | DSA with CPU+DSA split (`CSF=0.33`, auto-tuning on)     |
| `perf_summary`        | `dto-test-perf-summary` | Side-by-side table of all modes (runs last)             |

The `4k` variants use standard 4KB pages. The `2m` variants use 2MB
hugepages (requires: `echo 128 > /proc/sys/vm/nr_hugepages`).

DSA-enabled modes set `DTO_MIN_BYTES=4096` so DSA is used for all buffer
sizes in the benchmark.

### Regression Testing Workflow

Baseline files are local to your machine (gitignored, not committed) since
performance numbers are hardware-specific. The workflow compares a feature
branch against the main branch baselines on the same machine.

1. **On main, build and capture baselines:**

   ```bash
   git checkout main
   cmake -B build -DDTO_BUILD_TESTS=ON && cmake --build build
   cd build
   UPDATE_BASELINES=1 ctest --label-regex perf -E summary
   ```

   This writes baseline `.dat` files to `tests/baselines/` in the source
   tree.

2. **Checkout the feature branch and rebuild:**

   ```bash
   git checkout feature-branch
   cmake --build build
   ```

3. **Run the perf tests** -- they compare against the main branch baselines
   (without `UPDATE_BASELINES`, the baseline files are only read, never
   overwritten):

   ```bash
   ctest --label-regex perf --output-on-failure
   ```

4. **Review the summary** for a side-by-side view across all modes:

   ```bash
   ctest -R perf_summary -V
   ```

### Summary Output

After all perf tests complete, `perf_summary` prints a combined table
comparing all modes. The speedup column shows the ratio of CPU (no DTO)
latency to DSA (without autotuning) latency.

Note: Your numbers will differ based on hardware, kernel, and DSA
configuration.

```
==============================================================================
  DTO Performance Summary (2MB Hugepages)
==============================================================================

  Test           | CPU (no DTO)      ns/op |     DTO+STDC      ns/op |      DTO+DSA      ns/op | DTO+DSA+Auto      ns/op | Speedup
  ---------------|-------------------------|-------------------------|-------------------------|-------------------------|--------
  memcpy_64k     |       3.90   16818.0 ns |       3.91   16776.0 ns |      16.36    4005.0 ns |      15.50    4228.0 ns |  4.20x
  memcpy_128k    |       5.18   25282.0 ns |       5.19   25233.0 ns |      23.98    5466.0 ns |      22.10    5928.0 ns |  4.63x
  memcpy_1m      |       5.45  192498.0 ns |       5.46  191940.0 ns |      40.84   25675.0 ns |      38.50   27222.0 ns |  7.50x
```

To view the summary directly:

```bash
RESULTS_DIR=perf_results ./dto-test-perf-summary
```

### Baseline Files

Baselines are stored in `tests/baselines/` and are gitignored (not
committed) since performance numbers are hardware-specific. Each file
captures the latency for a specific mode and page size:

```
cpu_4k.dat       cpu_2m.dat          libc CPU baselines
stdc_4k.dat      stdc_2m.dat         DTO + CPU path baselines
dsa_4k.dat       dsa_2m.dat          DTO + DSA (pure) baselines
dsa_auto_4k.dat  dsa_auto_2m.dat     DTO + DSA (auto-tuned) baselines
```

Regenerate baselines when:
- Moving to new hardware or kernel
- Changing DSA configuration (engines, work queues)
- Intentionally accepting a performance change

```bash
# Regenerate all baselines
UPDATE_BASELINES=1 ctest --label-regex perf -E summary

# Regenerate a specific mode
UPDATE_BASELINES=1 ctest -R perf_dsa_2m
```

Baselines are also auto-generated on the first run if the files don't
exist, so you don't need to run `UPDATE_BASELINES` explicitly on a fresh
checkout.

### Adjust Tolerance

Default tolerance is 2% -- a drop below baseline fails the
test. If measurement noise on your system causes false failures, you can
relax the threshold:

```bash
PERF_TOLERANCE=20 ctest --label-regex perf
```

### Pin to a Different CPU Core

Default is core 1. Override with:

```bash
PERF_CPU=3 ctest --label-regex perf
```

### Run Manually (Outside CTest)

```bash
# Raw CPU (no DTO)
PERF_LABEL=cpu BASELINE_DIR=../tests/baselines \
  ./dto-test-perf-cpu

# DTO forced to CPU path
PERF_LABEL=stdc DTO_USESTDC_CALLS=1 \
  BASELINE_DIR=../tests/baselines ./dto-test-perf

# DTO + DSA (pure, no CPU split)
PERF_LABEL=dsa DTO_CPU_SIZE_FRACTION=0 DTO_AUTO_ADJUST_KNOBS=0 \
  DTO_MIN_BYTES=4096 BASELINE_DIR=../tests/baselines ./dto-test-perf

# DTO + DSA (auto-tuned, 33% initial CPU fraction)
PERF_LABEL=dsa_auto DTO_CPU_SIZE_FRACTION=0.33 DTO_AUTO_ADJUST_KNOBS=1 \
  DTO_MIN_BYTES=4096 BASELINE_DIR=../tests/baselines ./dto-test-perf

# Add DTO_PERF_HUGE=1 to any of the above for 2MB hugepages
```

## Environment Variables

| Variable                  | Effect                                              |
|---------------------------|-----------------------------------------------------|
| `BASELINE_DIR`            | Directory containing `.dat` baseline files          |
| `RESULTS_DIR`             | Directory for result files consumed by the summary  |
| `UPDATE_BASELINES`        | If set, write measured results as new baselines     |
| `PERF_TOLERANCE`          | Allowed % drop below baseline (default: 0)          |
| `PERF_LABEL`              | File prefix: `cpu`, `stdc`, `dsa`, or `dsa_auto`   |
| `PERF_CPU`                | CPU core to pin benchmark thread to (default: 1)    |
| `DTO_PERF_HUGE`           | If set, allocate with 2MB hugepages                 |
| `DTO_MIN_BYTES`           | DSA offload threshold in bytes (default: 65536)     |
| `DTO_CPU_SIZE_FRACTION`   | CPU fraction of each operation (0 = all DSA)        |
| `DTO_AUTO_ADJUST_KNOBS`   | Set to `1` to enable auto-tuning, `0` to disable   |
| `DTO_USESTDC_CALLS`       | Set to `1` to force CPU path (no DSA)               |

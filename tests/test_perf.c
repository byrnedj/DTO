/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * test_perf.c - Performance regression tests for DTO.
 *
 * Measures throughput (GB/s) and per-operation latency (ns) for memset,
 * memcpy, memcmp using cold-cache methodology (clflushopt before each
 * operation). Results are compared against checked-in baselines to detect
 * performance regressions from incoming DTO library changes.
 *
 * The benchmark thread is pinned to a single CPU core (PERF_CPU env var,
 * default: core 1) and elevated to SCHED_FIFO priority to reduce run-to-run
 * variance.  Per-iteration timings are collected and the median is reported
 * rather than the mean, making results robust against outlier spikes from
 * interrupts or transient system activity.
 *
 * Four modes of operation (separate CTest entries):
 *
 *   1. Raw CPU:      Built without libdto (dto-test-perf-cpu).
 *                    Measures baseline libc performance.
 *
 *   2. DTO + stdc:   Built with libdto, DTO_USESTDC_CALLS=1.
 *                    Measures DTO interception overhead without DSA.
 *
 *   3. DTO + DSA:    Built with libdto, DTO_CPU_SIZE_FRACTION=0,
 *                    DTO_AUTO_ADJUST_KNOBS=0.
 *                    Measures pure DSA performance through DTO.
 *
 *   4. DTO + DSA     Built with libdto, DTO_CPU_SIZE_FRACTION=0.33,
 *      (auto):       DTO_AUTO_ADJUST_KNOBS=1.
 *                    Measures DSA with CPU+DSA auto-tuning.
 *
 * Environment variables:
 *   BASELINE_DIR     - Directory containing baseline .dat files
 *   RESULTS_DIR      - Directory to write result files for summary
 *   UPDATE_BASELINES - If set, overwrite baseline file with measured results
 *   PERF_TOLERANCE   - Allowed %% drop below baseline before failing (default: 0)
 *   PERF_CPU         - CPU core to pin benchmark thread to (default: 1)
 *   PERF_PASSES      - Full passes per benchmark; median-of-medians when >1 (default: 1)
 *   PERF_COLD_CACHE  - Flush caches every iteration (1, default) or once per size (0)
 *   PERF_FREQ_MHZ    - Pin core and uncore frequency to this value in MHz (e.g., 2000)
 *   DTO_PERF_HUGE    - If set, allocate with 2MB hugepages
 *   PERF_LABEL       - Label for file prefix (cpu/stdc/dsa/dsa_auto)
 *
 * Labels: hardware, perf (DSA mode requires configured DSA work queues)
 ******************************************************************************/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "dto_test_utils.h"

#include <stdint.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <math.h>
#include <x86intrin.h>

/* Warmup iterations before timing */
#define WARMUP_ITERS 10000

/* Default tolerance: fail on any regression */
#define DEFAULT_TOLERANCE 2.0

#define DEFAULT_CPU 1

/* Default number of full passes per benchmark (median-of-medians when > 1) */
#define DEFAULT_PASSES 11

/* Sink for memcmp results to prevent dead code elimination */
static volatile int benchmark_sink;

static int use_hugepages;
static int cold_cache;

/* TSC frequency in ticks per nanosecond (GHz), calibrated at startup */
static double tsc_ghz;

/* ---- rdtsc helpers ---- */

static inline uint64_t rdtsc_start(void)
{
	__asm__ volatile("lfence");
	return __rdtsc();
}

static inline uint64_t rdtsc_end(void)
{
	unsigned int aux;

	return __rdtscp(&aux);
}

/*
 * Calibrate TSC frequency by measuring ticks over a known wall-clock
 * interval.  Busy-waits for ~50ms to get a stable ratio.
 */
static double calibrate_tsc(void)
{
	struct timespec t0, t1;
	uint64_t tsc0, tsc1;
	uint64_t elapsed_ns;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	tsc0 = rdtsc_end();

	/* Busy-wait ~50ms */
	do {
		clock_gettime(CLOCK_MONOTONIC, &t1);
		elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1000000000ULL +
			     (t1.tv_nsec - t0.tv_nsec);
	} while (elapsed_ns < 50000000ULL);

	tsc1 = rdtsc_end();

	return (double)(tsc1 - tsc0) / (double)elapsed_ns;
}

/* ---- CPU pinning ---- */

static int pin_to_cpu(int cpu)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
		perror("sched_setaffinity");
		return -1;
	}
	return 0;
}

/* ---- Frequency pinning ---- */

static struct {
	int active;
	int cpu;
	unsigned long orig_core_min_khz;
	unsigned long orig_core_max_khz;
	char uncore_dir[256];
	unsigned long orig_uncore_min_khz;
	unsigned long orig_uncore_max_khz;
} freq_state;

static int read_sysfs_ulong(const char *path, unsigned long *val)
{
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (fscanf(f, "%lu", val) != 1) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int write_sysfs_ulong(const char *path, unsigned long val)
{
	FILE *f = fopen(path, "w");

	if (!f)
		return -1;
	fprintf(f, "%lu", val);
	fclose(f);
	return 0;
}

/*
 * Set core frequency scaling_min and scaling_max for a given CPU.
 * Handles ordering: drops min first so max can be lowered, then sets both.
 */
static int set_core_freq(int cpu, unsigned long khz)
{
	char path[256];
	unsigned long abs_min;

	/* Read hw minimum so we can drop scaling_min safely */
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq",
		 cpu);
	if (read_sysfs_ulong(path, &abs_min) < 0)
		return -1;

	/* Drop min to absolute minimum to allow max to go anywhere */
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq",
		 cpu);
	if (write_sysfs_ulong(path, abs_min) < 0)
		return -1;

	/* Set max */
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq",
		 cpu);
	if (write_sysfs_ulong(path, khz) < 0)
		return -1;

	/* Set min to match */
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq",
		 cpu);
	return write_sysfs_ulong(path, khz);
}

/*
 * Find the uncore frequency sysfs directory for the package that owns
 * the given CPU.  Returns 0 on success with dir filled in.
 */
static int find_uncore_dir(int cpu, char *dir, size_t len)
{
	char path[512];
	unsigned long pkg, die;

	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/topology/physical_package_id",
		 cpu);
	if (read_sysfs_ulong(path, &pkg) < 0)
		return -1;

	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/topology/die_id", cpu);
	if (read_sysfs_ulong(path, &die) < 0)
		die = 0;

	snprintf(dir, len,
		 "/sys/devices/system/cpu/intel_uncore_frequency/"
		 "package_%02lu_die_%02lu", pkg, die);

	/* Verify it exists */
	snprintf(path, sizeof(path), "%s/min_freq_khz", dir);
	unsigned long tmp;

	return read_sysfs_ulong(path, &tmp);
}

static int set_uncore_freq(const char *dir, unsigned long khz)
{
	char path[512];
	unsigned long abs_min;

	snprintf(path, sizeof(path), "%s/initial_min_freq_khz", dir);
	if (read_sysfs_ulong(path, &abs_min) < 0) {
		/* Fallback: read current min */
		snprintf(path, sizeof(path), "%s/min_freq_khz", dir);
		if (read_sysfs_ulong(path, &abs_min) < 0)
			return -1;
	}

	/* Drop min first */
	snprintf(path, sizeof(path), "%s/min_freq_khz", dir);
	write_sysfs_ulong(path, abs_min);

	snprintf(path, sizeof(path), "%s/max_freq_khz", dir);
	if (write_sysfs_ulong(path, khz) < 0)
		return -1;

	snprintf(path, sizeof(path), "%s/min_freq_khz", dir);
	return write_sysfs_ulong(path, khz);
}

static void restore_freq(void)
{
	if (!freq_state.active)
		return;

	set_core_freq(freq_state.cpu, freq_state.orig_core_max_khz);
	/* Restore original min (may be lower than max) */
	{
		char path[512];

		snprintf(path, sizeof(path),
			 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq",
			 freq_state.cpu);
		write_sysfs_ulong(path, freq_state.orig_core_min_khz);
	}

	if (freq_state.uncore_dir[0]) {
		set_uncore_freq(freq_state.uncore_dir,
				freq_state.orig_uncore_max_khz);
		char path[512];

		snprintf(path, sizeof(path), "%s/min_freq_khz",
			 freq_state.uncore_dir);
		write_sysfs_ulong(path, freq_state.orig_uncore_min_khz);
	}
}

static int pin_freq(int cpu, unsigned long mhz)
{
	unsigned long khz = mhz * 1000;
	char path[512];

	/* Save and set core frequency */
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq",
		 cpu);
	if (read_sysfs_ulong(path, &freq_state.orig_core_min_khz) < 0) {
		fprintf(stderr, "WARNING: cannot read core freq for CPU %d "
			"(run as root, check cpufreq driver)\n", cpu);
		return -1;
	}
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq",
		 cpu);
	read_sysfs_ulong(path, &freq_state.orig_core_max_khz);

	if (set_core_freq(cpu, khz) < 0) {
		fprintf(stderr, "WARNING: could not pin core freq to %lu MHz "
			"(run as root)\n", mhz);
		return -1;
	}

	freq_state.cpu = cpu;
	freq_state.active = 1;
	atexit(restore_freq);

	/* Save and set uncore frequency */
	if (find_uncore_dir(cpu, freq_state.uncore_dir,
			    sizeof(freq_state.uncore_dir)) == 0) {
		snprintf(path, sizeof(path), "%s/min_freq_khz",
			 freq_state.uncore_dir);
		read_sysfs_ulong(path, &freq_state.orig_uncore_min_khz);
		snprintf(path, sizeof(path), "%s/max_freq_khz",
			 freq_state.uncore_dir);
		read_sysfs_ulong(path, &freq_state.orig_uncore_max_khz);

		if (set_uncore_freq(freq_state.uncore_dir, khz) < 0)
			fprintf(stderr, "WARNING: could not pin uncore freq "
				"to %lu MHz\n", mhz);
	} else {
		fprintf(stderr, "WARNING: intel_uncore_frequency sysfs not "
			"found, uncore freq not pinned\n");
		freq_state.uncore_dir[0] = '\0';
	}

	return 0;
}

/* ---- Cache flush helpers (matches bench_batch_vs_single) ---- */

static inline void clflushopt(volatile void *p)
{
	asm volatile("clflushopt (%0)" :: "r"(p) : "memory");
}

static void flush_buffer(void *buf, size_t size)
{
	char *p = buf;

	for (size_t i = 0; i < size; i += 64)
		clflushopt(p + i);
	_mm_sfence();
}

enum perf_op {
	OP_MEMSET,
	OP_MEMCPY,
	OP_MEMCMP,
};

struct perf_test {
	const char *name;
	enum perf_op op;
	size_t size;
	int iterations;
	int batch;	/* ops per timed sample (amortizes rdtsc for small sizes) */
	int ref_only;	/* if 1, report results but skip baseline pass/fail check */
};

/*
 * Benchmark configurations.
 *
 * Sizes range from 4KB to 1MB covering below and above the DSA offload
 * threshold. Iterations scale inversely with buffer size: small buffers
 * (4-8KB) use 100K iterations to compensate for their short per-op time,
 * while large buffers (256KB+) use 10K.
 *
 * Small sizes (4-8KB) use batch=10: each timed sample covers 10
 * back-to-back operations, amortizing rdtsc and per-call overhead.
 * The recorded sample is divided by batch to get per-op ticks.
 */

#define ITERATIONS 20000
#define SMALL_ITERATIONS 50000
static struct perf_test benchmarks[] = {
	/*           name             op        size     iters           batch ref */
	{"memcpy_4k",     OP_MEMCPY,  4096,     SMALL_ITERATIONS, 1,   1},
	{"memcpy_8k",     OP_MEMCPY,  8192,     SMALL_ITERATIONS, 1,   1},
	{"memcpy_16k",    OP_MEMCPY,  16384,    ITERATIONS,  1,        0},
	{"memcpy_32k",    OP_MEMCPY,  32768,    ITERATIONS,  1,        0},
	{"memcpy_64k",    OP_MEMCPY,  65536,    ITERATIONS,  1,        0},
	{"memcpy_128k",   OP_MEMCPY,  131072,   ITERATIONS,  1,        0},
	{"memcpy_256k",   OP_MEMCPY,  262144,   ITERATIONS,  1,        0},
	{"memcpy_512k",   OP_MEMCPY,  524288,   ITERATIONS,  1,        0},
	{"memcpy_1m",     OP_MEMCPY,  1048576,  ITERATIONS,  1,        0},

	{"memset_64k",    OP_MEMSET,  65536,    ITERATIONS,  1,        0},
	{"memset_128k",   OP_MEMSET,  131072,   ITERATIONS,  1,        0},
	{"memset_256k",   OP_MEMSET,  262144,   ITERATIONS,  1,        0},
	{"memset_1m",     OP_MEMSET,  1048576,  ITERATIONS,  1,        0},

	{"memcmp_64k",    OP_MEMCMP,  65536,    ITERATIONS,  1,        0},
	{"memcmp_128k",   OP_MEMCMP,  131072,   ITERATIONS,  1,        0},
	{"memcmp_1m",     OP_MEMCMP,  1048576,  ITERATIONS,  1,        0},

	{NULL, 0, 0, 0, 0, 0}
};

static void *alloc_buffer(size_t size)
{
	void *p;

	if (use_hugepages) {
		size_t alloc = (size + (2 << 20) - 1) & ~((2 << 20) - 1UL);

		p = mmap(NULL, alloc,
			 PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB |
			 MAP_POPULATE | (21 << MAP_HUGE_SHIFT),
			 -1, 0);
		if (p == MAP_FAILED) {
			fprintf(stderr, "mmap hugepage failed (need: "
				"echo 128 > /proc/sys/vm/nr_hugepages)\n");
			return NULL;
		}
	} else {
		p = mmap(NULL, size,
			 PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
			 -1, 0);
		if (p == MAP_FAILED) {
			perror("mmap");
			return NULL;
		}
	}
	return p;
}

static void free_buffer(void *p, size_t size)
{
	if (use_hugepages) {
		size_t alloc = (size + (2 << 20) - 1) & ~((2 << 20) - 1UL);

		munmap(p, alloc);
	} else {
		munmap(p, size);
	}
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t va = *(const uint64_t *)a;
	uint64_t vb = *(const uint64_t *)b;

	return (va > vb) - (va < vb);
}

static int cmp_double(const void *a, const void *b)
{
	double va = *(const double *)a;
	double vb = *(const double *)b;

	return (va > vb) - (va < vb);
}

/*
 * Sample distribution file format (binary):
 *   uint32_t  count      - number of samples
 *   double    tsc_ghz    - TSC ticks-per-ns for conversion
 *   uint64_t  samples[]  - raw TSC ticks, sorted ascending
 */
static int save_samples(const char *dir, const char *label,
			const char *pagesize, const char *testname,
			uint64_t *samples, int count)
{
	char path[4096];
	FILE *f;
	uint32_t n = count;

	snprintf(path, sizeof(path), "%s/%s_%s_%s.samples",
		 dir, label, pagesize, testname);
	f = fopen(path, "wb");
	if (!f)
		return -1;

	fwrite(&n, sizeof(n), 1, f);
	fwrite(&tsc_ghz, sizeof(tsc_ghz), 1, f);
	fwrite(samples, sizeof(uint64_t), count, f);
	fclose(f);
	return 0;
}

/*
 * Run a single pass of the benchmark. Returns GB/s (or -1 on error).
 * If out_samples / out_nsamples are non-NULL, the caller takes ownership
 * of the samples array (ticks, not ns) and must free() it.
 */
static double run_benchmark(struct perf_test *test, double *out_avg_ns,
			    uint64_t **out_samples, int *out_nsamples)
{
	uint8_t *src, *dst;
	uint64_t *samples;
	uint64_t start, end;

	if (out_samples)
		*out_samples = NULL;
	if (out_nsamples)
		*out_nsamples = 0;

	src = alloc_buffer(test->size);
	dst = alloc_buffer(test->size);
	if (!src || !dst) {
		if (src) free_buffer(src, test->size);
		if (dst) free_buffer(dst, test->size);
		return -1.0;
	}

	samples = malloc(test->iterations * sizeof(uint64_t));
	if (!samples) {
		free_buffer(src, test->size);
		free_buffer(dst, test->size);
		return -1.0;
	}

	/* Initialize source data */
	for (size_t i = 0; i < test->size; i++)
		src[i] = (uint8_t)(i & 0xFF);
	/* For memcmp, dst must match src */
	for (size_t i = 0; i < test->size; i++)
		dst[i] = src[i];

	/* Warmup: prime IOTLB and DSA work queues */
	for (int i = 0; i < WARMUP_ITERS; i++) {
		switch (test->op) {
		case OP_MEMSET:
			memset(dst, 0xAA, test->size);
			break;
		case OP_MEMCPY:
			memcpy(dst, src, test->size);
			break;
		case OP_MEMCMP:
			benchmark_sink = memcmp(dst, src, test->size);
			break;
		}
	}

	/* Re-init dst for memcmp (warmup may have overwritten via memset) */
	if (test->op == OP_MEMCMP) {
		for (size_t i = 0; i < test->size; i++)
			dst[i] = src[i];
	}

	/*
	 * Timed run.
	 *
	 * Cold-cache mode (default): flush both buffers before each
	 * iteration so every operation measures DRAM latency.
	 * Warm-cache mode (PERF_COLD_CACHE=0): flush once before the
	 * loop; subsequent iterations hit L1/L2 — more representative
	 * for small buffers that stay cache-resident in real workloads.
	 */
	if (!cold_cache) {
		flush_buffer(src, test->size);
		flush_buffer(dst, test->size);
	}

	int batch = test->batch > 0 ? test->batch : 1;

	for (int i = 0; i < test->iterations; i++) {
		if (cold_cache) {
		    flush_buffer(src, test->size);
		    flush_buffer(dst, test->size);
		}
		
                start = rdtsc_start();

		for (int b = 0; b < batch; b++) {

			switch (test->op) {
			case OP_MEMSET:
				memset(dst, 0xAA, test->size);
				break;
			case OP_MEMCPY:
				memcpy(dst, src, test->size);
				break;
			case OP_MEMCMP:
				benchmark_sink = memcmp(dst, src, test->size);
				break;
			}
			COMPILER_BARRIER();
		}

		end = rdtsc_end();
		samples[i] = (end - start) / batch;
	}

	qsort(samples, test->iterations, sizeof(uint64_t), cmp_u64);
	double median_ns = (double)samples[test->iterations / 2] / tsc_ghz;
	double gbps = (double)test->size / median_ns;

	*out_avg_ns = median_ns;

	if (out_samples) {
		*out_samples = samples;  /* caller owns it now */
		if (out_nsamples)
			*out_nsamples = test->iterations;
	} else {
		free(samples);
	}

	/*
	 * Post-benchmark verification: prove the full buffer was operated on.
	 * Poison dst, run one more operation, then check first/mid/last bytes.
	 */
	if (test->op == OP_MEMCPY && test->size > 0) {
		/* Poison entire dst with 0xDE */
		for (size_t i = 0; i < test->size; i++)
			dst[i] = 0xDE;

		flush_buffer(src, test->size);
		flush_buffer(dst, test->size);
		memcpy(dst, src, test->size);
		COMPILER_BARRIER();

		/* Check first, middle, and last bytes */
		size_t checks[] = {0, test->size / 2, test->size - 1};
		for (int c = 0; c < 3; c++) {
			size_t idx = checks[c];

			if (dst[idx] != src[idx]) {
				fprintf(stderr,
					"\n    VERIFY FAIL: %s byte[%zu] "
					"dst=0x%02x src=0x%02x\n",
					test->name, idx, dst[idx], src[idx]);
				free_buffer(src, test->size);
				free_buffer(dst, test->size);
				return -1.0;
			}
		}
		/* Spot-check that poison was fully overwritten */
		if (dst[test->size - 1] == 0xDE) {
			fprintf(stderr,
				"\n    VERIFY FAIL: %s last byte still "
				"poisoned (0xDE) — copy may be incomplete\n",
				test->name);
			free_buffer(src, test->size);
			free_buffer(dst, test->size);
			return -1.0;
		}
	}

	if (test->op == OP_MEMSET && test->size > 0) {
		/* Clear dst, run one memset, verify last byte */
		for (size_t i = 0; i < test->size; i++)
			dst[i] = 0x00;

		flush_buffer(dst, test->size);
		memset(dst, 0xAA, test->size);
		COMPILER_BARRIER();

		if (dst[test->size - 1] != 0xAA) {
			fprintf(stderr,
				"\n    VERIFY FAIL: %s last byte = 0x%02x, "
				"expected 0xAA\n",
				test->name, dst[test->size - 1]);
			free_buffer(src, test->size);
			free_buffer(dst, test->size);
			return -1.0;
		}
	}

	free_buffer(src, test->size);
	free_buffer(dst, test->size);
	return gbps;
}

int main(void)
{
	const char *results_dir = getenv("RESULTS_DIR");
	const char *label = getenv("PERF_LABEL");
	const char *cpu_env = getenv("PERF_CPU");
	const char *passes_env = getenv("PERF_PASSES");
	int cpu = cpu_env ? atoi(cpu_env) : DEFAULT_CPU;
	int passes = passes_env ? atoi(passes_env) : DEFAULT_PASSES;
	struct perf_baseline results[MAX_BASELINES];
	int num_results = 0;
	int errors = 0;

	use_hugepages = (getenv("DTO_PERF_HUGE") != NULL);
	cold_cache = getenv("PERF_COLD_CACHE") ? atoi(getenv("PERF_COLD_CACHE")) : 1;

	if (!label)
		label = "dsa";
	if (passes < 1)
		passes = 1;

	/* Real-time priority to minimize scheduler preemption */
	{
		struct sched_param sp = { .sched_priority = 1 };

		if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
			fprintf(stderr, "WARNING: could not set SCHED_FIFO "
				"(run as root for lower variance)\n");
	}

	/* Pin benchmark thread to a single core for stable results */
	if (pin_to_cpu(cpu) < 0)
		fprintf(stderr, "WARNING: could not pin to CPU %d, "
			"results may have higher variance\n", cpu);

	/* Pin core + uncore frequency if requested (before TSC calibration) */
	{
		const char *freq_env = getenv("PERF_FREQ_MHZ");

		if (freq_env)
			pin_freq(cpu, strtoul(freq_env, NULL, 10));
	}

	/* Calibrate TSC after pinning so we measure the pinned core's freq */
	tsc_ghz = calibrate_tsc();

	printf("DTO Performance Tests (%s cache) [%s]\n",
	       cold_cache ? "cold" : "warm", label);
	printf("==========================================\n");
	printf("Page size:              %s\n",
	       use_hugepages ? "2MB hugepages" : "4KB");
	printf("Pinned to CPU:          %d\n", cpu);
	printf("TSC frequency:          %.3f GHz\n", tsc_ghz);
	if (freq_state.active) {
		const char *mhz = getenv("PERF_FREQ_MHZ");

		printf("Core frequency:         %s MHz (pinned)\n", mhz);
		if (freq_state.uncore_dir[0])
			printf("Uncore frequency:       %s MHz (pinned)\n",
			       mhz);
	}
	printf("Cache mode:             %s\n",
	       cold_cache ? "cold (flush every iteration)" :
			    "warm (flush once per size)");
	printf("Passes per benchmark:   %d%s\n", passes,
	       passes > 1 ? " (median-of-medians)" : "");
	printf("DTO_CPU_SIZE_FRACTION:  %s\n",
	       getenv("DTO_CPU_SIZE_FRACTION") ? : "(default)");
	printf("DTO_AUTO_ADJUST_KNOBS:  %s\n",
	       getenv("DTO_AUTO_ADJUST_KNOBS") ? : "(default)");
	printf("DTO_USESTDC_CALLS:      %s\n\n",
	       getenv("DTO_USESTDC_CALLS") ? : "(default)");

	/* Table header */
	printf("  %-20s %12s %10s", "Test", "med ns/op", "GB/s");
	if (passes > 1)
		printf(" %8s", "CV%%");
	printf("\n");
	printf("  %-20s %12s %10s", "----", "-----", "----");
	if (passes > 1)
		printf(" %8s", "----");
	printf("\n");

	const char *pagesize = use_hugepages ? "2m" : "4k";

	/* Run benchmarks */
	for (int i = 0; benchmarks[i].name != NULL; i++) {
		double avg_ns;
		double gbps;
		double cv_pct = -1.0;
		int iters = benchmarks[i].iterations;

		/* Accumulate all raw samples across passes for distribution saving */
		uint64_t *all_samples = NULL;
		int total_samples = 0;

		if (results_dir) {
			all_samples = malloc((size_t)passes * iters *
					     sizeof(uint64_t));
		}

		if (passes == 1) {
			uint64_t *pass_samples = NULL;
			int pass_n = 0;

			gbps = run_benchmark(&benchmarks[i], &avg_ns,
					     all_samples ? &pass_samples : NULL,
					     &pass_n);
			if (gbps >= 0 && pass_samples) {
				memcpy(all_samples, pass_samples,
				       pass_n * sizeof(uint64_t));
				total_samples = pass_n;
				free(pass_samples);
			}
		} else {
			double *pass_medians;
			int ok = 1;

			pass_medians = malloc(passes * sizeof(double));
			if (!pass_medians) {
				printf("  %-20s %12s\n",
				       benchmarks[i].name, "ERROR");
				errors++;
				free(all_samples);
				continue;
			}

			for (int p = 0; p < passes; p++) {
				double ns;
				uint64_t *pass_samples = NULL;
				int pass_n = 0;

				gbps = run_benchmark(&benchmarks[i], &ns,
						     all_samples ? &pass_samples : NULL,
						     &pass_n);
				if (gbps < 0) {
					ok = 0;
					free(pass_samples);
					break;
				}
				pass_medians[p] = ns;

				if (pass_samples) {
					memcpy(all_samples + total_samples,
					       pass_samples,
					       pass_n * sizeof(uint64_t));
					total_samples += pass_n;
					free(pass_samples);
				}
			}
			if (!ok) {
				free(pass_medians);
				free(all_samples);
				printf("  %-20s %12s\n",
				       benchmarks[i].name, "ERROR");
				errors++;
				continue;
			}

			qsort(pass_medians, passes, sizeof(double),
			      cmp_double);
			avg_ns = pass_medians[passes / 2];
			gbps = (double)benchmarks[i].size / avg_ns;

			/* Coefficient of variation of pass medians */
			{
				double sum = 0.0, sum_sq = 0.0, mean, var;

				for (int p = 0; p < passes; p++)
					sum += pass_medians[p];
				mean = sum / passes;
				for (int p = 0; p < passes; p++) {
					double d = pass_medians[p] - mean;

					sum_sq += d * d;
				}
				var = sum_sq / passes;
				cv_pct = (sqrt(var) / mean) * 100.0;
			}
			free(pass_medians);
		}

		/* Save sample distribution */
		if (all_samples && total_samples > 0 && gbps >= 0) {
			qsort(all_samples, total_samples, sizeof(uint64_t),
			      cmp_u64);
			save_samples(results_dir, label, pagesize,
				     benchmarks[i].name, all_samples,
				     total_samples);
		}
		free(all_samples);

		if (gbps < 0) {
			printf("  %-20s %12s\n", benchmarks[i].name, "ERROR");
			errors++;
			continue;
		}

		/* Store latency as the baseline metric */
		snprintf(results[num_results].name, BASELINE_NAME_LEN,
			 "%s", benchmarks[i].name);
		results[num_results].latency_ns = avg_ns;
		num_results++;

		printf("  %-20s %10.1f ns %10.4f", benchmarks[i].name,
		       avg_ns, gbps);
		if (cv_pct >= 0)
			printf(" %7.2f%%", cv_pct);
		if (benchmarks[i].ref_only)
			printf("  (ref)");
		printf("\n");
	}

	/* Write results for summary/ratio comparison */
	if (results_dir) {
		char results_path[4096];

		mkdir(results_dir, 0755);
		snprintf(results_path, sizeof(results_path), "%s/%s_%s.dat",
			 results_dir, label,
			 use_hugepages ? "2m" : "4k");
		save_baselines(results_path, results, num_results);
		printf("\n  Results saved to %s\n", results_path);
	}

	printf("\n");
	if (errors > 0) {
		printf("  %d measurement error(s)\n", errors);
		return 1;
	}

	printf("  Measurement complete\n");
	return 0;
}

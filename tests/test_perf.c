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
#define WARMUP_ITERS 1000

/* Default tolerance: fail on any regression */
#define DEFAULT_TOLERANCE 2.0

#define DEFAULT_CPU 1

/* Default number of full passes per benchmark (median-of-medians when > 1) */
#define DEFAULT_PASSES 7

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
};

/*
 * Benchmark configurations.
 *
 * Sizes range from 4KB to 1MB covering below and above the DSA offload
 * threshold. Iterations scale inversely with buffer size: small buffers
 * (4-8KB) use 100K iterations to compensate for their short per-op time,
 * while large buffers (256KB+) use 10K.
 */
static struct perf_test benchmarks[] = {
	{"memcpy_4k",     OP_MEMCPY,  4096,     100000},
	{"memcpy_8k",     OP_MEMCPY,  8192,     100000},
	{"memcpy_16k",    OP_MEMCPY,  16384,    50000},
	{"memcpy_32k",    OP_MEMCPY,  32768,    50000},
	{"memcpy_64k",    OP_MEMCPY,  65536,    20000},
	{"memcpy_128k",   OP_MEMCPY,  131072,   20000},
	{"memcpy_256k",   OP_MEMCPY,  262144,   10000},
	{"memcpy_512k",   OP_MEMCPY,  524288,   10000},
	{"memcpy_1m",     OP_MEMCPY,  1048576,  10000},

	{"memset_64k",    OP_MEMSET,  65536,    20000},
	{"memset_128k",   OP_MEMSET,  131072,   20000},
	{"memset_256k",   OP_MEMSET,  262144,   10000},
	{"memset_1m",     OP_MEMSET,  1048576,  10000},

	{"memcmp_64k",    OP_MEMCMP,  65536,    20000},
	{"memcmp_128k",   OP_MEMCMP,  131072,   20000},
	{"memcmp_1m",     OP_MEMCMP,  1048576,  10000},

	{NULL, 0, 0, 0}
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

static double run_benchmark(struct perf_test *test, double *out_avg_ns)
{
	uint8_t *src, *dst;
	uint64_t *samples;
	uint64_t start, end;

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

	for (int i = 0; i < test->iterations; i++) {
		if (cold_cache) {
			flush_buffer(src, test->size);
			flush_buffer(dst, test->size);
		}

		start = rdtsc_start();

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

		end = rdtsc_end();
		samples[i] = end - start;
	}

	qsort(samples, test->iterations, sizeof(uint64_t), cmp_u64);
	double median_ns = (double)samples[test->iterations / 2] / tsc_ghz;
	double gbps = (double)test->size / median_ns;

	*out_avg_ns = median_ns;
	free(samples);

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
	int update_mode = (getenv("UPDATE_BASELINES") != NULL);
	double tolerance = DEFAULT_TOLERANCE;
	const char *tol_env = getenv("PERF_TOLERANCE");
	const char *baseline_dir = getenv("BASELINE_DIR");
	const char *results_dir = getenv("RESULTS_DIR");
	const char *label = getenv("PERF_LABEL");
	const char *cpu_env = getenv("PERF_CPU");
	const char *passes_env = getenv("PERF_PASSES");
	int cpu = cpu_env ? atoi(cpu_env) : DEFAULT_CPU;
	int passes = passes_env ? atoi(passes_env) : DEFAULT_PASSES;
	char baseline_path[4096];
	struct perf_baseline baselines[MAX_BASELINES];
	struct perf_baseline results[MAX_BASELINES];
	int num_baselines = 0;
	int num_results = 0;
	int failures = 0;

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

	/* Calibrate TSC after pinning so we measure the pinned core's freq */
	tsc_ghz = calibrate_tsc();

	printf("DTO Performance Tests (%s cache) [%s]\n",
	       cold_cache ? "cold" : "warm", label);
	printf("==========================================\n");
	printf("Page size:              %s\n",
	       use_hugepages ? "2MB hugepages" : "4KB");
	printf("Pinned to CPU:          %d\n", cpu);
	printf("TSC frequency:          %.3f GHz\n", tsc_ghz);
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

	if (tol_env)
		tolerance = atof(tol_env);

	/* Determine baseline file path using label and page size */
	if (!baseline_dir)
		baseline_dir = "tests/baselines";

	snprintf(baseline_path, sizeof(baseline_path), "%s/%s_%s.dat",
		 baseline_dir, label,
		 use_hugepages ? "2m" : "4k");

	/* Load existing baselines, or auto-generate if missing */
	if (!update_mode) {
		num_baselines = load_baselines(baseline_path, baselines,
					       MAX_BASELINES);
		if (num_baselines == 0) {
			printf("  No baselines found at %s\n"
			       "  Auto-generating baselines on this run\n\n",
			       baseline_path);
			update_mode = 1;
		} else {
			printf("  Loaded %d baselines from %s (tolerance: %.0f%%)\n\n",
			       num_baselines, baseline_path, tolerance);
		}
	} else {
		printf("  BASELINE UPDATE MODE - generating new baselines\n\n");
	}

	/* Table header */
	printf("  %-20s %12s %10s", "Test", "med ns/op", "GB/s");
	if (passes > 1)
		printf(" %8s", "CV%%");
	if (!update_mode)
		printf(" %12s %10s %s", "Baseline", "Delta", "Result");
	printf("\n");
	printf("  %-20s %12s %10s", "----", "-----", "----");
	if (passes > 1)
		printf(" %8s", "----");
	if (!update_mode)
		printf(" %12s %10s %s", "--------", "-----", "------");
	printf("\n");

	/* Run benchmarks */
	for (int i = 0; benchmarks[i].name != NULL; i++) {
		double avg_ns;
		double gbps;
		double cv_pct = -1.0; /* coefficient of variation, -1 = N/A */

		if (passes == 1) {
			gbps = run_benchmark(&benchmarks[i], &avg_ns);
		} else {
			double *pass_medians;
			int ok = 1;

			pass_medians = malloc(passes * sizeof(double));
			if (!pass_medians) {
				printf("  %-20s %12s\n",
				       benchmarks[i].name, "ERROR");
				failures++;
				continue;
			}

			for (int p = 0; p < passes; p++) {
				double ns;

				gbps = run_benchmark(&benchmarks[i], &ns);
				if (gbps < 0) {
					ok = 0;
					break;
				}
				pass_medians[p] = ns;
			}
			if (!ok) {
				free(pass_medians);
				printf("  %-20s %12s\n",
				       benchmarks[i].name, "ERROR");
				failures++;
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

		if (gbps < 0) {
			printf("  %-20s %12s\n", benchmarks[i].name, "ERROR");
			failures++;
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

		if (update_mode) {
			printf("\n");
		} else {
			double baseline_ns = find_baseline(baselines,
							   num_baselines,
							   benchmarks[i].name);
			if (baseline_ns < 0) {
				printf(" %12s %10s %s\n", "N/A", "N/A", "SKIP");
			} else {
				/* Positive delta = slower = regression */
				double delta_pct = ((avg_ns - baseline_ns) /
						    baseline_ns) * 100.0;
				double threshold = baseline_ns *
						   (1.0 + tolerance / 100.0);
				int pass = (avg_ns <= threshold);

				printf(" %10.1f ns %+8.1f%% %s\n",
				       baseline_ns, delta_pct,
				       pass ? "PASS" : "FAIL");

				if (!pass) {
					failures++;
					fprintf(stderr,
						"    %s: %.1f ns is %.1f%%"
						" slower than baseline %.1f ns"
						" (threshold: +%.0f%%)\n",
						benchmarks[i].name, avg_ns,
						delta_pct, baseline_ns,
						tolerance);
				}

				if (avg_ns < baseline_ns * 0.7)
					printf("    NOTE: %.1f%% faster"
					       " than baseline -- consider"
					       " updating\n", -delta_pct);
			}
		}
	}

	/* Save baselines if in update mode */
	if (update_mode) {
		if (save_baselines(baseline_path, results, num_results) == 0)
			printf("\n  Baselines saved to %s\n", baseline_path);
		else
			failures++;
	}

	/* Write results for summary aggregation */
	if (results_dir) {
		char results_path[4096];

		mkdir(results_dir, 0755);
		snprintf(results_path, sizeof(results_path), "%s/%s_%s.dat",
			 results_dir, label,
			 use_hugepages ? "2m" : "4k");
		save_baselines(results_path, results, num_results);
	}

	printf("\n");
	if (failures > 0) {
		printf("  %d test(s) FAILED\n", failures);
		return 1;
	}

	printf("  All tests passed\n");
	return 0;
}

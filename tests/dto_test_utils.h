/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * dto_test_utils.h - Lightweight test utilities for DTO test suite.
 *
 * Provides:
 *   - Assertion macros (return 1 from calling function on failure)
 *   - Test runner with pass/fail reporting
 *   - Timing helpers for performance benchmarks
 *   - Baseline load/save for performance regression testing
 ******************************************************************************/

#ifndef DTO_TEST_UTILS_H
#define DTO_TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

/* ---- Assertion macros ---- */

#define ASSERT_TRUE(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "    FAIL: %s (at %s:%d)\n", \
			#cond, __FILE__, __LINE__); \
		return 1; \
	} \
} while (0)

#define ASSERT_EQ(a, b) do { \
	long long _a = (long long)(a), _b = (long long)(b); \
	if (_a != _b) { \
		fprintf(stderr, "    FAIL: %s == %s (%lld != %lld) at %s:%d\n", \
			#a, #b, _a, _b, __FILE__, __LINE__); \
		return 1; \
	} \
} while (0)

/* ---- Test runner ---- */

typedef int (*test_fn)(void);

struct dto_test {
	const char *name;
	test_fn fn;
};

#define TEST_ENTRY(fn) { #fn, fn }

static inline int run_tests(struct dto_test *tests)
{
	int total = 0, passed = 0, failed = 0;

	for (int i = 0; tests[i].name != NULL; i++) {
		total++;
		printf("  %-50s ", tests[i].name);
		fflush(stdout);
		if (tests[i].fn() == 0) {
			printf("PASS\n");
			passed++;
		} else {
			printf("FAIL\n");
			failed++;
		}
	}

	printf("\nResults: %d/%d passed", passed, total);
	if (failed > 0)
		printf(" (%d FAILED)", failed);
	printf("\n");

	return failed > 0 ? 1 : 0;
}

/* ---- Timing helpers ---- */

static inline uint64_t time_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Prevent dead code elimination of benchmark operations */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/* ---- Aligned allocation ---- */

static inline void *alloc_aligned(size_t size)
{
	void *ptr = NULL;

	if (posix_memalign(&ptr, 4096, size) != 0)
		return NULL;
	return ptr;
}

/* ---- Performance baseline support ---- */

#define MAX_BASELINES 64
#define BASELINE_NAME_LEN 64

struct perf_baseline {
	char name[BASELINE_NAME_LEN];
	double latency_ns;
};

static inline int load_baselines(const char *path,
				 struct perf_baseline *baselines, int max)
{
	FILE *f = fopen(path, "r");
	int count = 0;
	char line[256];

	if (!f)
		return 0;

	while (fgets(line, sizeof(line), f) && count < max) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (sscanf(line, "%63s %lf", baselines[count].name,
			   &baselines[count].latency_ns) == 2)
			count++;
	}
	fclose(f);
	return count;
}

static inline int save_baselines(const char *path,
				 struct perf_baseline *baselines, int count)
{
	FILE *f = fopen(path, "w");
	time_t now;

	if (!f) {
		fprintf(stderr, "Failed to open %s for writing\n", path);
		return 1;
	}

	now = time(NULL);
	fprintf(f, "# DTO Performance Baselines (latency in ns)\n");
	fprintf(f, "# Re-generate with: UPDATE_BASELINES=1 ctest --label-regex perf\n");
	fprintf(f, "# Generated: %s", ctime(&now));
	for (int i = 0; i < count; i++)
		fprintf(f, "%-24s %.1f\n", baselines[i].name,
			baselines[i].latency_ns);

	fclose(f);
	return 0;
}

static inline double find_baseline(struct perf_baseline *baselines, int count,
				   const char *name)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(baselines[i].name, name) == 0)
			return baselines[i].latency_ns;
	}
	return -1.0;
}

#endif /* DTO_TEST_UTILS_H */

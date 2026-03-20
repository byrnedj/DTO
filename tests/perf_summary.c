/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * perf_summary.c - Aggregate side-by-side summary of all DTO perf modes.
 *
 * Reads result files from RESULTS_DIR (written by test_perf.c) and prints
 * a combined table showing CPU, STDC, DSA, and DSA+Auto results with
 * speedup ratios.
 *
 * Expected files in RESULTS_DIR:
 *   cpu_4k.dat, stdc_4k.dat, dsa_4k.dat, dsa_auto_4k.dat
 *   cpu_2m.dat, stdc_2m.dat, dsa_2m.dat, dsa_auto_2m.dat
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TESTS 32
#define NAME_LEN 64
#define MAX_MODES 8

struct result {
	char name[NAME_LEN];
	double latency_ns;
};

struct mode {
	const char *label;
	const char *header;
	struct result tests[MAX_TESTS];
	int count;
};

static int load_results(const char *path, struct result *results, int max)
{
	FILE *f = fopen(path, "r");
	int count = 0;
	char line[256];

	if (!f)
		return 0;

	while (fgets(line, sizeof(line), f) && count < max) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (sscanf(line, "%63s %lf", results[count].name,
			   &results[count].latency_ns) == 2)
			count++;
	}
	fclose(f);
	return count;
}

static double find_latency(struct mode *m, const char *name)
{
	for (int i = 0; i < m->count; i++) {
		if (strcmp(m->tests[i].name, name) == 0)
			return m->tests[i].latency_ns;
	}
	return -1.0;
}

/* Parse size from test name suffix: memcpy_128k -> 131072 */
static size_t parse_size(const char *name)
{
	const char *s = strrchr(name, '_');
	char *end;
	long val;

	if (!s)
		return 0;
	s++;

	val = strtol(s, &end, 10);
	if (*end == 'k' || *end == 'K')
		return (size_t)val * 1024;
	if (*end == 'm' || *end == 'M')
		return (size_t)val * 1048576;
	return (size_t)val;
}

static void print_table(const char *results_dir, const char *pagesize,
			const char *pagesize_label)
{
	struct {
		const char *suffix;
		const char *header;
	} mode_defs[] = {
		{"cpu",      "CPU (no DTO)"},
		{"stdc",     "DTO+STDC"},
		{"dsa",      "DTO+DSA"},
		{"dsa_auto", "DTO+DSA+Auto"},
	};
	int num_defs = sizeof(mode_defs) / sizeof(mode_defs[0]);
	struct mode modes[MAX_MODES];
	int num_modes = 0;
	int ref;

	/* Load available result files for this page size */
	for (int i = 0; i < num_defs; i++) {
		char path[4096];

		snprintf(path, sizeof(path), "%s/%s_%s.dat",
			 results_dir, mode_defs[i].suffix, pagesize);

		modes[num_modes].count = load_results(path,
						      modes[num_modes].tests,
						      MAX_TESTS);
		if (modes[num_modes].count > 0) {
			modes[num_modes].label = mode_defs[i].suffix;
			modes[num_modes].header = mode_defs[i].header;
			num_modes++;
		}
	}

	if (num_modes == 0)
		return;

	/* Use mode with most tests as reference for row names */
	ref = 0;
	for (int m = 1; m < num_modes; m++) {
		if (modes[m].count > modes[ref].count)
			ref = m;
	}

	/* Header */
	printf("==========================================================");
	printf("============================================================\n");
	printf("  DTO Performance Summary (%s)\n", pagesize_label);
	printf("==========================================================");
	printf("============================================================\n\n");

	/* Column headers */
	printf("  %-14s", "Test");
	for (int m = 0; m < num_modes; m++)
		printf(" | %10s %12s", modes[m].header, "GB/s");
	printf(" | Speedup\n");

	printf("  %-14s", "--------------");
	for (int m = 0; m < num_modes; m++)
		printf("-|-%10s-%12s", "----------", "------------");
	printf("-|--------\n");

	/* Data rows */
	for (int t = 0; t < modes[ref].count; t++) {
		const char *name = modes[ref].tests[t].name;
		size_t size = parse_size(name);
		double cpu_ns = -1, dsa_ns = -1;

		printf("  %-14s", name);

		for (int m = 0; m < num_modes; m++) {
			double ns = find_latency(&modes[m], name);

			if (ns > 0 && size > 0) {
				double gbps = (double)size / ns;

				printf(" | %8.1f ns %10.4f", ns, gbps);

				if (strcmp(modes[m].label, "cpu") == 0)
					cpu_ns = ns;
				if (strcmp(modes[m].label, "dsa") == 0)
					dsa_ns = ns;
			} else {
				printf(" | %10s %12s", "N/A", "N/A");
			}
		}

		if (cpu_ns > 0 && dsa_ns > 0)
			printf(" | %5.2fx", cpu_ns / dsa_ns);
		else
			printf(" |   N/A");

		printf("\n");
	}

	printf("\n");
}

int main(void)
{
	const char *results_dir = getenv("RESULTS_DIR");

	if (!results_dir) {
		fprintf(stderr, "RESULTS_DIR not set\n");
		return 1;
	}

	printf("\n");
	print_table(results_dir, "4k", "4KB Pages");
	print_table(results_dir, "2m", "2MB Hugepages");

	return 0;
}

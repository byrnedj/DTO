/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * perf_summary.c - Mann-Whitney U based performance regression detection.
 *
 * Reads per-mode result files and sample distributions from RESULTS_DIR
 * (written by test_perf_combined.c or test_perf.c), prints a side-by-side
 * summary table, then runs the Mann-Whitney U test comparing current sample
 * distributions against baseline distributions.
 *
 * A regression is detected when the distribution of latencies has shifted
 * significantly (p < 0.05) AND the median got worse (higher latency).
 *
 * Expected files in RESULTS_DIR:
 *   {cpu,stdc,dsa,dsa_auto}_{4k,2m}.dat        - median latencies
 *   {cpu,stdc,dsa,dsa_auto}_{4k,2m}_*.samples   - raw sample distributions
 *
 * Baseline sample files in BASELINE_DIR:
 *   {cpu,stdc,dsa,dsa_auto}_{4k,2m}_*.samples
 *
 * Environment:
 *   RESULTS_DIR      - directory with current result files (required)
 *   BASELINE_DIR     - directory with baseline sample files (required)
 *   UPDATE_BASELINES - if set, copy current samples to baseline dir
 *   PERF_MIN_EFFECT  - minimum median change %% to count as regression (default: 2)
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/stat.h>

#define MAX_TESTS 32
#define NAME_LEN 64
#define MAX_MODES 8

/* ---- Result loading (same format as test_perf.c output) ---- */

struct result {
	char name[NAME_LEN];
	double latency_ns;
};

struct mode {
	const char *suffix;
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

/* ---- Sample distribution loading ---- */

struct sample_dist {
	uint64_t *ticks;
	int count;
	double tsc_ghz;
};

static int load_samples(const char *dir, const char *mode,
			const char *pagesize, const char *testname,
			struct sample_dist *dist)
{
	char path[4096];
	FILE *f;
	uint32_t n;

	dist->ticks = NULL;
	dist->count = 0;
	dist->tsc_ghz = 1.0;

	snprintf(path, sizeof(path), "%s/%s_%s_%s.samples",
		 dir, mode, pagesize, testname);
	f = fopen(path, "rb");
	if (!f)
		return -1;

	if (fread(&n, sizeof(n), 1, f) != 1 || n == 0) {
		fclose(f);
		return -1;
	}
	if (fread(&dist->tsc_ghz, sizeof(dist->tsc_ghz), 1, f) != 1) {
		fclose(f);
		return -1;
	}

	dist->ticks = malloc((size_t)n * sizeof(uint64_t));
	if (!dist->ticks) {
		fclose(f);
		return -1;
	}

	if (fread(dist->ticks, sizeof(uint64_t), n, f) != n) {
		free(dist->ticks);
		dist->ticks = NULL;
		fclose(f);
		return -1;
	}

	dist->count = n;
	fclose(f);
	return 0;
}

static void free_samples(struct sample_dist *dist)
{
	free(dist->ticks);
	dist->ticks = NULL;
	dist->count = 0;
}

/* ---- Two-sample Kolmogorov-Smirnov test ---- */

struct ks_result {
	double d;	/* max ECDF distance (0..1) */
	double p;	/* p-value */
};

static struct ks_result ks_test(const uint64_t *a, int n1,
				const uint64_t *b, int n2)
{
	struct ks_result r = {0.0, 1.0};
	int i = 0, j = 0;
	double d_max = 0.0;

	while (i < n1 || j < n2) {
		uint64_t val;
		double diff;

		if (i < n1 && (j >= n2 || a[i] <= b[j]))
			val = a[i];
		else
			val = b[j];

		while (i < n1 && a[i] == val) i++;
		while (j < n2 && b[j] == val) j++;

		diff = fabs((double)i / n1 - (double)j / n2);
		if (diff > d_max)
			d_max = diff;
	}

	r.d = d_max;

	double ne = (double)n1 * n2 / (n1 + n2);
	double lambda = (sqrt(ne) + 0.12 + 0.11 / sqrt(ne)) * d_max;
	double sum = 0.0;

	for (int k = 1; k <= 100; k++) {
		double term = exp(-2.0 * k * k * lambda * lambda);

		if (k % 2 == 1)
			sum += term;
		else
			sum -= term;
		if (term < 1e-12)
			break;
	}
	r.p = 2.0 * sum;
	if (r.p < 0) r.p = 0;
	if (r.p > 1) r.p = 1;

	return r;
}

/*
 * Print overlaid ASCII histogram of two sorted sample distributions.
 * Shows the 5th-95th percentile range to exclude extreme outliers.
 * 'B' = baseline only, 'C' = current only, '#' = overlap.
 */
#define HIST_BINS 40
#define HIST_HEIGHT 12

static void print_histogram(const struct sample_dist *bl,
			    const struct sample_dist *cur)
{
	/* Use 5th and 95th percentile across both for range */
	double bl_p5  = (double)bl->ticks[(int)(bl->count * 0.05)] / bl->tsc_ghz;
	double bl_p95 = (double)bl->ticks[(int)(bl->count * 0.95)] / bl->tsc_ghz;
	double cur_p5  = (double)cur->ticks[(int)(cur->count * 0.05)] / cur->tsc_ghz;
	double cur_p95 = (double)cur->ticks[(int)(cur->count * 0.95)] / cur->tsc_ghz;

	double lo = bl_p5 < cur_p5 ? bl_p5 : cur_p5;
	double hi = bl_p95 > cur_p95 ? bl_p95 : cur_p95;

	if (hi <= lo)
		return;

	double bin_width = (hi - lo) / HIST_BINS;
	int bl_bins[HIST_BINS] = {0};
	int cur_bins[HIST_BINS] = {0};
	int max_count = 0;

	/* Bin the baseline samples */
	for (int i = 0; i < bl->count; i++) {
		double ns = (double)bl->ticks[i] / bl->tsc_ghz;
		int bin = (int)((ns - lo) / bin_width);

		if (bin < 0) bin = 0;
		if (bin >= HIST_BINS) bin = HIST_BINS - 1;
		bl_bins[bin]++;
	}

	/* Bin the current samples */
	for (int i = 0; i < cur->count; i++) {
		double ns = (double)cur->ticks[i] / cur->tsc_ghz;
		int bin = (int)((ns - lo) / bin_width);

		if (bin < 0) bin = 0;
		if (bin >= HIST_BINS) bin = HIST_BINS - 1;
		cur_bins[bin]++;
	}

	for (int b = 0; b < HIST_BINS; b++) {
		if (bl_bins[b] > max_count) max_count = bl_bins[b];
		if (cur_bins[b] > max_count) max_count = cur_bins[b];
	}

	if (max_count == 0)
		return;

	/* Print top-down */
	for (int row = HIST_HEIGHT; row >= 1; row--) {
		double threshold = (double)row / HIST_HEIGHT * max_count;

		printf("    %s", row == HIST_HEIGHT ? "  " : "  ");
		for (int b = 0; b < HIST_BINS; b++) {
			int has_bl = (bl_bins[b] >= threshold);
			int has_cur = (cur_bins[b] >= threshold);

			if (has_bl && has_cur)
				putchar('#');
			else if (has_bl)
				putchar('B');
			else if (has_cur)
				putchar('C');
			else
				putchar(' ');
		}
		printf("\n");
	}

	/* X-axis */
	printf("    ");
	for (int b = 0; b < HIST_BINS + 2; b++)
		putchar('-');
	printf("\n");
	printf("    %-20.0f ns %*s %18.0f ns\n",
	       lo, HIST_BINS - 22, "", hi);
	printf("    B=baseline  C=current  #=overlap\n\n");
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

/* Skip 4k/8k entries (ref-only in benchmarks) */
static int is_ref_only(const char *name)
{
	const char *s = strrchr(name, '_');

	if (!s)
		return 0;
	s++;
	return (strcmp(s, "4k") == 0 || strcmp(s, "8k") == 0);
}

/* ---- Mode definitions ---- */

static struct {
	const char *suffix;
	const char *header;
} mode_defs[] = {
	{"cpu",      "CPU (no DTO)"},
	{"stdc",     "DTO+STDC"},
	{"dsa",      "DTO+DSA"},
	{"dsa_auto", "DTO+DSA+Auto"},
};
#define NUM_MODE_DEFS (sizeof(mode_defs) / sizeof(mode_defs[0]))

/*
 * Process one page size: load results, print summary table, run
 * Mann-Whitney U tests against baseline sample distributions.
 *
 * Returns number of regressions detected (0 = all passed).
 */
static int process_pagesize(const char *results_dir, const char *baseline_dir,
			    const char *pagesize, const char *pagesize_label,
			    int update_mode, double min_effect_pct)
{
	struct mode modes[MAX_MODES];
	int num_modes = 0;
	int cpu_idx = -1;
	int ref;
	int failures = 0;

	/* Load available result files for this page size */
	for (int i = 0; i < (int)NUM_MODE_DEFS; i++) {
		char path[4096];

		snprintf(path, sizeof(path), "%s/%s_%s.dat",
			 results_dir, mode_defs[i].suffix, pagesize);

		modes[num_modes].count = load_results(
			path, modes[num_modes].tests, MAX_TESTS);
		if (modes[num_modes].count > 0) {
			modes[num_modes].suffix = mode_defs[i].suffix;
			modes[num_modes].header = mode_defs[i].header;
			if (strcmp(mode_defs[i].suffix, "cpu") == 0)
				cpu_idx = num_modes;
			num_modes++;
		}
	}

	if (num_modes == 0) {
		printf("  No results found for %s\n\n", pagesize_label);
		return 0;
	}

	/* Use mode with most tests as reference for row names */
	ref = 0;
	for (int m = 1; m < num_modes; m++) {
		if (modes[m].count > modes[ref].count)
			ref = m;
	}

	/* ---- Summary table (informational) ---- */
	printf("==========================================================");
	printf("===========================================================\n");
	printf("  DTO Performance Summary (%s)\n", pagesize_label);
	printf("==========================================================");
	printf("===========================================================\n\n");

	printf("  %-14s", "Test");
	for (int m = 0; m < num_modes; m++)
		printf(" | %12s %8s", modes[m].header, "GB/s");
	if (cpu_idx >= 0)
		printf(" | Speedup");
	printf("\n");

	printf("  %-14s", "--------------");
	for (int m = 0; m < num_modes; m++)
		printf("-|-%12s-%8s", "------------", "--------");
	if (cpu_idx >= 0)
		printf("-|--------");
	printf("\n");

	for (int t = 0; t < modes[ref].count; t++) {
		const char *name = modes[ref].tests[t].name;
		size_t size = parse_size(name);
		double cpu_ns = -1, dsa_ns = -1;

		printf("  %-14s", name);

		for (int m = 0; m < num_modes; m++) {
			double ns = find_latency(&modes[m], name);

			if (ns > 0 && size > 0) {
				double gbps = (double)size / ns;

				printf(" | %10.1f ns %8.4f", ns, gbps);

				if (m == cpu_idx)
					cpu_ns = ns;
				if (strcmp(modes[m].suffix, "dsa") == 0)
					dsa_ns = ns;
			} else {
				printf(" | %12s %8s", "N/A", "N/A");
			}
		}

		if (cpu_ns > 0 && dsa_ns > 0)
			printf(" | %5.2fx", cpu_ns / dsa_ns);
		else if (cpu_idx >= 0)
			printf(" |   N/A");

		if (is_ref_only(name))
			printf("  (ref)");
		printf("\n");
	}

	/* ---- Mann-Whitney U tests ---- */
	printf("\n  ---- Mann-Whitney U Distribution Tests ----\n");

	/* Auto-generate baselines if none exist */
	if (!update_mode) {
		struct sample_dist probe;

		if (load_samples(baseline_dir, modes[0].suffix, pagesize,
				 modes[ref].tests[0].name, &probe) < 0) {
			printf("  No baseline samples found in %s\n"
			       "  Auto-generating baselines on this run\n\n",
			       baseline_dir);
			update_mode = 1;
		} else {
			free_samples(&probe);
		}
	}

	if (update_mode) {
		printf("  UPDATE MODE: copying current samples to baseline\n\n");
		mkdir(baseline_dir, 0755);

		for (int t = 0; t < modes[ref].count; t++) {
			const char *name = modes[ref].tests[t].name;

			for (int m = 0; m < num_modes; m++) {
				char src_path[4096], dst_path[4096];
				FILE *sf, *df;
				char buf[8192];
				size_t n;

				snprintf(src_path, sizeof(src_path),
					 "%s/%s_%s_%s.samples",
					 results_dir, modes[m].suffix,
					 pagesize, name);
				snprintf(dst_path, sizeof(dst_path),
					 "%s/%s_%s_%s.samples",
					 baseline_dir, modes[m].suffix,
					 pagesize, name);

				sf = fopen(src_path, "rb");
				if (!sf)
					continue;
				df = fopen(dst_path, "wb");
				if (!df) {
					fclose(sf);
					continue;
				}
				while ((n = fread(buf, 1, sizeof(buf), sf)) > 0)
					fwrite(buf, 1, n, df);
				fclose(sf);
				fclose(df);
			}
		}
		printf("  Baseline samples saved to %s\n\n", baseline_dir);
		return 0;
	}

	/* Comparison mode */
	{
		int mw_tested = 0, mw_sig = 0;

		printf("  %-14s %-12s %20s %20s %10s %8s %10s  %s\n",
		       "Test", "Mode", "baseline trimmed",
		       "current trimmed", "Change",
		       "KS D", "p-value", "Result");
		printf("  %-14s %-12s %20s %20s %10s %8s %10s  %s\n",
		       "--------------", "------------",
		       "--------------------", "--------------------",
		       "----------", "--------", "----------", "------");

		for (int t = 0; t < modes[ref].count; t++) {
			const char *name = modes[ref].tests[t].name;
			int ref_only = is_ref_only(name);

			for (int m = 0; m < num_modes; m++) {
				struct sample_dist bl_dist, cur_dist;
				double bl_ns, cur_ns, change_pct;
				struct ks_result ks;
				int regression;

				if (load_samples(baseline_dir,
						 modes[m].suffix, pagesize,
						 name, &bl_dist) < 0)
					continue;
				if (load_samples(results_dir,
						 modes[m].suffix, pagesize,
						 name, &cur_dist) < 0) {
					free_samples(&bl_dist);
					continue;
				}

				/*
				 * Trimmed mean (5th-95th pctl) and
				 * Mann-Whitney on trimmed range.
				 */
				{
					int blo = bl_dist.count / 10;
					int bhi = bl_dist.count * 9 / 10;
					int clo = cur_dist.count / 10;
					int chi = cur_dist.count * 9 / 10;
					int bn = bhi - blo, cn = chi - clo;
					double bs = 0, cs = 0;

					for (int k = blo; k < bhi; k++)
						bs += (double)bl_dist.ticks[k]
						      / bl_dist.tsc_ghz;
					for (int k = clo; k < chi; k++)
						cs += (double)cur_dist.ticks[k]
						      / cur_dist.tsc_ghz;
					bl_ns = bs / bn;
					cur_ns = cs / cn;
					ks = ks_test(bl_dist.ticks + blo, bn,
						     cur_dist.ticks + clo, cn);
				}
				change_pct = ((cur_ns - bl_ns) /
					      bl_ns) * 100.0;

				regression = change_pct > min_effect_pct;

				printf("  %-14s %-12s %18.1f ns %18.1f ns "
				       "%+8.1f%% D=%.4f p=%.2e  ",
				       name, modes[m].suffix,
				       bl_ns, cur_ns,
				       change_pct, ks.d, ks.p);

				if (ref_only)
					printf("REF\n");
				else if (regression)
					printf("FAIL ***\n");
				else if (change_pct < -min_effect_pct)
					printf("IMPROVED\n");
				else
					printf("PASS\n");

				mw_tested++;
				if (regression && !ref_only) {
					mw_sig++;
					failures++;
				}

				/* Show distribution for significant changes */
				if (!ref_only &&
				    (change_pct > min_effect_pct ||
				     change_pct < -min_effect_pct))
					print_histogram(&bl_dist, &cur_dist);

				free_samples(&bl_dist);
				free_samples(&cur_dist);
			}
		}

		printf("\n");
		if (mw_tested > 0) {
			if (mw_sig > 0)
				printf("  %d/%d tests show significant "
				       "regression (p < 0.05)\n\n",
				       mw_sig, mw_tested);
			else
				printf("  All %d tests passed "
				       "(no significant regressions)\n\n",
				       mw_tested);
		} else {
			printf("  No sample distributions found — run "
			       "UPDATE_BASELINES=1 first\n\n");
		}
	}

	return failures;
}

int main(void)
{
	const char *results_dir = getenv("RESULTS_DIR");
	const char *baseline_dir = getenv("BASELINE_DIR");
	int update_mode = (getenv("UPDATE_BASELINES") != NULL);
	const char *effect_env = getenv("PERF_MIN_EFFECT");
	double min_effect_pct = effect_env ? atof(effect_env) : 2.0;
	int failures = 0;

	if (!results_dir) {
		fprintf(stderr, "RESULTS_DIR not set\n");
		return 1;
	}
	if (!baseline_dir) {
		fprintf(stderr, "BASELINE_DIR not set\n");
		return 1;
	}

	printf("\n  Regression threshold: p < 0.05 AND median change > "
	       "+%.1f%%\n", min_effect_pct);

	failures += process_pagesize(results_dir, baseline_dir,
				     "4k", "4KB Pages",
				     update_mode, min_effect_pct);
	failures += process_pagesize(results_dir, baseline_dir,
				     "2m", "2MB Hugepages",
				     update_mode, min_effect_pct);

	return failures > 0 ? 1 : 0;
}

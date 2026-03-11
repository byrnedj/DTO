/*
 * bench-plugin.c - Benchmark: baseline vs DTO vs DTO+plugin for small memcpy
 *
 * The memcpy is inside a function with a VRP-visible bound (n < 512).
 * With -fno-builtin, ALL memcpy calls go through PLT (DTO intercepts).
 * The plugin detects the bound via VRP and inlines the memcpy/memset,
 * bypassing DTO entirely.
 *
 * Build & run:
 *   ./bench-plugin-build.sh
 *   ./bench-plugin-run.sh
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ITERATIONS  10000000
#define WARMUP      1000000

/*
 * Bounded memcpy: VRP can prove n < 512 inside the if-body.
 * Without plugin: gcc emits call memcpy@PLT (due to -fno-builtin).
 * With plugin: gcc emits inline AVX2 + rep movsb (no PLT call).
 */
static void bounded_copy(char *dst, const char *src, size_t n)
{
	if (n < 512)
		memcpy(dst, src, n);
}

static void bounded_set(char *dst, int c, size_t n)
{
	if (n < 512)
		memset(dst, c, n);
}

static double bench_memcpy(size_t size)
{
	char *src = aligned_alloc(64, 512);
	char *dst = aligned_alloc(64, 512);
	struct timespec st, et;

	memset(src, 0xAA, 512);
	memset(dst, 0x00, 512);

	for (int i = 0; i < WARMUP; i++) {
		bounded_copy(dst, src, size);
		__asm__ volatile("" : "+r"(dst), "+r"(src) :: "memory");
	}

	clock_gettime(CLOCK_MONOTONIC, &st);
	for (int i = 0; i < ITERATIONS; i++) {
		bounded_copy(dst, src, size);
		__asm__ volatile("" : "+r"(dst), "+r"(src) :: "memory");
	}
	clock_gettime(CLOCK_MONOTONIC, &et);

	double ns = (et.tv_sec - st.tv_sec) * 1e9 + (et.tv_nsec - st.tv_nsec);
	free(src);
	free(dst);
	return ns / ITERATIONS;
}

static double bench_memset(size_t size)
{
	char *dst = aligned_alloc(64, 512);
	struct timespec st, et;

	memset(dst, 0x00, 512);

	for (int i = 0; i < WARMUP; i++) {
		bounded_set(dst, 0x55, size);
		__asm__ volatile("" : "+r"(dst) :: "memory");
	}

	clock_gettime(CLOCK_MONOTONIC, &st);
	for (int i = 0; i < ITERATIONS; i++) {
		bounded_set(dst, 0x55, size);
		__asm__ volatile("" : "+r"(dst) :: "memory");
	}
	clock_gettime(CLOCK_MONOTONIC, &et);

	double ns = (et.tv_sec - st.tv_sec) * 1e9 + (et.tv_nsec - st.tv_nsec);
	free(dst);
	return ns / ITERATIONS;
}

int main(void)
{
	size_t sizes[] = { 1, 8, 32, 64, 128, 256, 512 };
	int nsizes = sizeof(sizes) / sizeof(sizes[0]);

	printf("=== memcpy (%d iterations) ===\n", ITERATIONS);
	printf("%8s  %10s\n", "size", "ns/call");
	printf("---------------------\n");
	for (int i = 0; i < nsizes; i++)
		printf("%8zu  %10.1f\n", sizes[i], bench_memcpy(sizes[i]));

	printf("\n=== memset (%d iterations) ===\n", ITERATIONS);
	printf("%8s  %10s\n", "size", "ns/call");
	printf("---------------------\n");
	for (int i = 0; i < nsizes; i++)
		printf("%8zu  %10.1f\n", sizes[i], bench_memset(sizes[i]));

	return 0;
}

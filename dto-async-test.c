// dto-async-test.c -- exercises the async compare / memfill / translation
// fetch API and measures what a fresh process pays the first time DSA touches
// a mapping it has never accessed (the offload-daemon case).
//
//   dto-async-test [--pages 4k|2m|anon] [--size-mib N] [--prefault none|populate|tf]
//                  [--depth D] [--chunk-kib K] [--tf-mib M]
//
// Correctness checks run first (compare equal/differ, memfill, CRC vs zlib,
// translation fetch status). Then a memfd of --size-mib (4 KiB or 2 MiB
// hugetlb pages) is filled through pwrite from a buffer, mapped afresh so this
// process has no page-table entries for it, optionally prefaulted
// (MADV_POPULATE_READ, or DSA Translation Fetch in --tf-mib descriptors), and
// then classified against zero with --depth async COMPAREs of --chunk-kib
// each; a second pass over the now-warm mapping follows.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <linux/memfd.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include "dto.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void *zero_buf(size_t n) {
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (p == MAP_FAILED) { perror("mmap zero"); exit(1); }
	madvise(p, n, MADV_HUGEPAGE);
	return p;
}
static int wait_op(dto_async_op *op) { int r; while ((r = dto_async_poll(op)) == DTO_ASYNC_PENDING) __builtin_ia32_pause(); return r; }

static uint32_t crc32c_raw(const unsigned char *p, size_t n, uint32_t seed)
{
	uint32_t c = seed;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0x82f63b78u & (0u - (c & 1)));
	}
	return c;
}

static int check(const char *what, int ok) { printf("  %-44s %s\n", what, ok ? "ok" : "FAIL"); return !ok; }

static int correctness(void)
{
	int fails = 0;
	size_t n = 1 << 20;
	unsigned char *a = zero_buf(n), *b = zero_buf(n), *z = zero_buf(n);
	dto_async_op op;
	int rc;

	printf("correctness:\n");
	rc = dto_submit_compare(&op, a, z, n);
	if (rc != DTO_ASYNC_SUBMITTED) { printf("  compare: FALLBACK (no DSA?)\n"); return 1; }
	fails += check("compare zero vs zero -> equal", wait_op(&op) == DTO_ASYNC_DONE && dto_async_result(&op) == 0);

	a[777777] = 0x5a;
	rc = dto_submit_compare(&op, a, z, n);
	fails += check("compare differs -> result 1", rc == DTO_ASYNC_SUBMITTED && wait_op(&op) == DTO_ASYNC_DONE && dto_async_result(&op) == 1);
	fails += check("  first difference offset reported", dto_async_bytes_completed(&op) == 777777);

	memset(b, 0xa5, n);
	rc = dto_submit_memfill(&op, b, 0, n, 1);
	fails += check("memfill zero -> done", rc == DTO_ASYNC_SUBMITTED && wait_op(&op) == DTO_ASYNC_DONE);
	{ size_t i; int allz = 1; for (i = 0; i < n; i++) if (b[i]) { allz = 0; break; } fails += check("  buffer reads back zero", allz); }
	rc = dto_submit_memfill(&op, b, 0x0102030405060708ull, n, 0);
	fails += check("memfill pattern -> matches", rc == DTO_ASYNC_SUBMITTED && wait_op(&op) == DTO_ASYNC_DONE && ((uint64_t *)b)[12345] == 0x0102030405060708ull);

	for (size_t i = 0; i < n; i++) a[i] = (unsigned char)(i * 2654435761u >> 13);
	rc = dto_submit_crc(&op, a, n);
	if (rc == DTO_ASYNC_SUBMITTED && wait_op(&op) == DTO_ASYNC_DONE) {
		uint32_t dsa = (uint32_t)dto_async_crc_val(&op);
		uint32_t r0 = crc32c_raw(a, n, 0), rf = crc32c_raw(a, n, 0xffffffffu);
		const char *conv = dsa == r0 ? "crc32c seed 0, no final xor (raw)" : dsa == (uint32_t)~r0 ? "~crc32c(seed 0)"
			: dsa == rf ? "crc32c seed ~0, no final xor" : dsa == (uint32_t)~rf ? "standard CRC-32C (seed ~0, final xor)" : "UNKNOWN";
		printf("  crc: dsa=%08x | raw0=%08x ~raw0=%08x rawF=%08x ~rawF=%08x zlib-crc32=%08x -> %s\n",
		       dsa, r0, ~r0, rf, ~rf, crc32(0, a, n), conv);
		fails += !strcmp(conv, "UNKNOWN");
	} else {
		printf("  crc: not submitted/failed\n"); fails++;
	}

	void *big = zero_buf(64 << 20);
	rc = dto_submit_transl_fetch(&op, big, 64 << 20);
	if (rc != DTO_ASYNC_SUBMITTED) printf("  transl fetch: FALLBACK\n");
	else { int r = wait_op(&op); printf("  transl fetch 64 MiB: %s status=%#x\n", r == DTO_ASYNC_DONE ? "done" : "FAILED", dto_async_status(&op)); }
	munmap(big, 64 << 20);
	return fails;
}

struct run { double secs; unsigned long submitted, fallback, failed, nonzero; };

static struct run classify(unsigned char *base, size_t size, size_t chunk, int depth, unsigned char *z)
{
	struct run r = {0};
	dto_async_op *ops = aligned_alloc(64, sizeof(dto_async_op) * depth);
	size_t *off = calloc(depth, sizeof(size_t));
	int *live = calloc(depth, sizeof(int));
	size_t next = 0; int inflight = 0;
	double t0 = now();
	while (next < size || inflight) {
		for (int s = 0; s < depth; s++) {
			if (live[s]) {
				int st = dto_async_poll(&ops[s]);
				if (st == DTO_ASYNC_PENDING) continue;
				if (st == DTO_ASYNC_DONE) { if (dto_async_result(&ops[s])) r.nonzero++; }
				else r.failed++;
				live[s] = 0; inflight--;
			}
			if (next < size) {
				size_t len = size - next < chunk ? size - next : chunk;
				int rc = dto_submit_compare(&ops[s], base + next, z, len);
				if (rc == DTO_ASYNC_SUBMITTED) { live[s] = 1; off[s] = next; inflight++; r.submitted++; }
				else { r.fallback++; }
				next += len;
			}
		}
	}
	r.secs = now() - t0;
	free(ops); free(off); free(live);
	return r;
}

int main(int argc, char **argv)
{
	const char *pages = "2m", *prefault = "none";
	size_t size_mib = 1024, chunk_kib = 1024, tf_mib = 64; int depth = 32;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--pages")) pages = argv[++i];
		else if (!strcmp(argv[i], "--prefault")) prefault = argv[++i];
		else if (!strcmp(argv[i], "--size-mib")) size_mib = atol(argv[++i]);
		else if (!strcmp(argv[i], "--chunk-kib")) chunk_kib = atol(argv[++i]);
		else if (!strcmp(argv[i], "--tf-mib")) tf_mib = atol(argv[++i]);
		else if (!strcmp(argv[i], "--depth")) depth = atoi(argv[++i]);
		else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
	}
	if (correctness()) printf("correctness: FAILURES\n");

	size_t size = size_mib << 20, chunk = chunk_kib << 10;
	unsigned char *z = zero_buf(chunk);
	unsigned char *base;
	if (!strcmp(pages, "anon")) {
		base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	} else {
		int huge = !strcmp(pages, "2m");
		int fd = memfd_create("dto-async-test", huge ? MFD_HUGETLB | MFD_HUGE_2MB : 0);
		if (fd < 0) { perror("memfd_create"); return 1; }
		if (ftruncate(fd, size)) { perror("ftruncate"); return 1; }
		/* populate the file's pages from this process WITHOUT mapping it, so
		 * the later mmap starts with no PTEs (like a guest memfd handed to
		 * the daemon). pwrite into hugetlb memfd is not supported; map once,
		 * touch, unmap instead. */
		unsigned char *tmp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (tmp == MAP_FAILED) { perror("mmap fill"); return 1; }
		for (size_t o = 0; o < size; o += 4096) tmp[o] = 0; /* allocate every page, all zero */
		munmap(tmp, size);
		base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	}
	if (base == MAP_FAILED) { perror("mmap base"); return 1; }

	printf("\nfresh mapping: pages=%s size=%zu MiB chunk=%zu KiB depth=%d prefault=%s\n", pages, size_mib, chunk_kib, depth, prefault);
	double tp0 = now(); const char *pf_note = "";
	if (!strcmp(prefault, "populate")) {
		if (madvise(base, size, MADV_POPULATE_READ)) { perror("MADV_POPULATE_READ"); }
	} else if (!strcmp(prefault, "tf")) {
		size_t tf = tf_mib << 20; int nd = (size + tf - 1) / tf;
		dto_async_op *ops = aligned_alloc(64, sizeof(dto_async_op) * nd);
		int sub = 0, done = 0, failed = 0; int st0 = 0;
		for (int i = 0; i < nd; i++) {
			size_t o = (size_t)i * tf, len = size - o < tf ? size - o : tf;
			if (dto_submit_transl_fetch(&ops[i], base + o, len) == DTO_ASYNC_SUBMITTED) sub++; else failed++;
		}
		for (int i = 0; i < nd; i++) { int r = wait_op(&ops[i]); if (r == DTO_ASYNC_DONE) done++; else { failed++; if (!st0) st0 = dto_async_status(&ops[i]); } }
		static char note[96]; snprintf(note, sizeof note, " (tf descs: %d submitted, %d done, %d failed, first status %#x)", sub, done, failed, st0);
		pf_note = note; free(ops);
	}
	double pf = now() - tp0;
	printf("  prefault: %.1f ms%s\n", pf * 1e3, pf_note);
	struct run r1 = classify(base, size, chunk, depth, z);
	printf("  pass 1 (cold):  %.1f ms  %.1f GiB/s  submitted %lu fallback %lu failed %lu nonzero %lu\n", r1.secs * 1e3, size / r1.secs / (1 << 30), r1.submitted, r1.fallback, r1.failed, r1.nonzero);
	struct run r2 = classify(base, size, chunk, depth, z);
	printf("  pass 2 (warm):  %.1f ms  %.1f GiB/s  submitted %lu fallback %lu failed %lu nonzero %lu\n", r2.secs * 1e3, size / r2.secs / (1 << 30), r2.submitted, r2.fallback, r2.failed, r2.nonzero);
	printf("  total prefault+pass1: %.1f ms\n", (pf + r1.secs) * 1e3);
	return 0;
}

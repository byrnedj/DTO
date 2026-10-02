// dto-batch-test.c -- correctness and cost of batched DSA operations on 4 KiB
// pages: COMPARE (equal / differing), DUALCAST, CRC; and time per page for
// memcmp on the CPU, one async op per page, and batches of 16..1024.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "dto.h"

#define P 4096
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static unsigned char *buf(size_t n) {
	unsigned char *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	return p;
}
static void wait_batch(dto_batch *b) {
	double t0 = now();
	while (dto_batch_poll(b) == DTO_ASYNC_PENDING) {
		__builtin_ia32_pause();
		if (now() - t0 > 2.0) {
			printf("  batch of %d did not complete in 2 s; op0 status %d\n", dto_batch_count(b), dto_batch_status(b, 0));
			exit(3);
		}
	}
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	size_t npages = argc > 1 ? atol(argv[1]) : 65536;	/* 256 MiB */
	unsigned char *a = buf(npages * P), *s = buf(npages * P), *g = buf(npages * P), *g2 = buf(npages * P);
	int fails = 0;
	for (size_t i = 0; i < npages * P; i += 8) *(uint64_t *)(a + i) = i * 0x9e3779b97f4a7c15ull;
	memcpy(s, a, npages * P);
	/* every 4th page differs */
	for (size_t p = 0; p < npages; p += 4) s[p * P + 100] ^= 1;

	/* correctness, one batch of 1024 */
	dto_batch *b = dto_batch_create(1024);
	for (int i = 0; i < 1024; i++) dto_batch_add_compare(b, a + (size_t)i * P, s + (size_t)i * P, P);
	if (dto_batch_submit(b) != DTO_ASYNC_SUBMITTED) { printf("batch submit FALLBACK\n"); return 1; }
	wait_batch(b);
	int wrong = 0;
	for (int i = 0; i < 1024; i++) {
		int want = (i % 4) == 0;
		if (dto_batch_status(b, i) != 1 || dto_batch_result(b, i) != want) wrong++;
		if (want && dto_batch_bytes_completed(b, i) != 100) wrong++;
	}
	printf("batch compare x1024: %s\n", wrong ? "FAIL" : "ok"); fails += !!wrong;
	dto_batch_reset(b);
	for (int i = 0; i < 1024; i++) dto_batch_add_dualcast(b, g + (size_t)i * P, g2 + (size_t)i * P, a + (size_t)i * P, P, 0);
	dto_batch_submit(b); wait_batch(b);
	wrong = memcmp(g, a, 1024 * P) || memcmp(g2, a, 1024 * P);
	for (int i = 0; i < 1024; i++) wrong |= dto_batch_status(b, i) != 1;
	printf("batch dualcast x1024: %s\n", wrong ? "FAIL" : "ok"); fails += !!wrong;
	dto_batch_reset(b);
	if (dto_batch_add_dualcast(b, g, g2 + 64, a, P, 0) != -1) { printf("dualcast misaligned not refused: FAIL\n"); fails++; }
	dto_batch_reset(b);
	dto_batch_add_crc(b, a, P); dto_batch_add_crc(b, a + P, P);
	dto_batch_submit(b); wait_batch(b);
	printf("batch crc x2: status %d %d crc %08x %08x\n", dto_batch_status(b, 0), dto_batch_status(b, 1), dto_batch_crc(b, 0), dto_batch_crc(b, 1));
	dto_batch_reset(b);
	dto_batch_add_compare(b, a, s + P, P);	/* single-op batch -> plain descriptor */
	dto_batch_submit(b); wait_batch(b);
	printf("single-op batch: status %d result %d\n", dto_batch_status(b, 0), dto_batch_result(b, 0));

	/* cost per 4 KiB compare */
	double t0 = now(); size_t diff = 0;
	for (size_t p = 0; p < npages; p++) diff += memcmp(a + p * P, s + p * P, P) != 0;
	double cpu = now() - t0;
	printf("\ncompare %zu pages (%zu MiB), %zu differ\n", npages, npages * P >> 20, diff);
	printf("  cpu memcmp                 %8.1f ms  %6.0f ns/page\n", cpu * 1e3, cpu * 1e9 / npages);
	dto_async_op *ops = aligned_alloc(64, sizeof(dto_async_op) * 32); size_t fb1 = 0;
	t0 = now();
	for (size_t p = 0; p < npages; p += 32) {
		int sub[32];
		for (int k = 0; k < 32; k++) {
			sub[k] = dto_submit_compare(&ops[k], a + (p + k) * P, s + (p + k) * P, P) == DTO_ASYNC_SUBMITTED;
			if (!sub[k]) { fb1++; (void)memcmp(a + (p + k) * P, s + (p + k) * P, P); }
		}
		for (int k = 0; k < 32; k++) while (sub[k] && dto_async_poll(&ops[k]) == DTO_ASYNC_PENDING) __builtin_ia32_pause();
	}
	double one = now() - t0;
	printf("  dsa one op/page, 32 deep   %8.1f ms  %6.0f ns/page  (%zu fell back to cpu)\n", one * 1e3, one * 1e9 / npages, fb1);
	int sizes[] = {16, 64, 256, 1024};
	for (int si = 0; si < 4; si++) {
		int bs = sizes[si], depth = 8; dto_batch *bb[8];
		for (int k = 0; k < depth; k++) bb[k] = dto_batch_create(bs);
		t0 = now(); size_t next = 0, d2 = 0; int live[8] = {0};
		while (next < npages || live[0] || live[1] || live[2] || live[3] || live[4] || live[5] || live[6] || live[7]) {
			for (int k = 0; k < depth; k++) {
				if (live[k]) {
					if (dto_batch_poll(bb[k]) == DTO_ASYNC_PENDING) continue;
					for (int i = 0; i < dto_batch_count(bb[k]); i++) d2 += dto_batch_result(bb[k], i);
					live[k] = 0;
				}
				if (next < npages) {
					dto_batch_reset(bb[k]);
					for (int i = 0; i < bs && next < npages; i++, next++) dto_batch_add_compare(bb[k], a + next * P, s + next * P, P);
					if (dto_batch_submit(bb[k]) == DTO_ASYNC_SUBMITTED) live[k] = 1;
				}
			}
		}
		double t = now() - t0;
		printf("  dsa batch %4d x 8 deep     %8.1f ms  %6.0f ns/page  (%zu differ)\n", bs, t * 1e3, t * 1e9 / npages, d2);
		for (int k = 0; k < depth; k++) dto_batch_destroy(bb[k]);
	}
	return fails ? 1 : 0;
}

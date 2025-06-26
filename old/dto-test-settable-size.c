/*******************************************************************************
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>
#include <stdatomic.h>
#include <unistd.h>

//1024*1024*1024*256   A bit over the total memory in both NUMAs
//#define NUM_BUFS  (4*1024UL)
#define BUF_SIZE_BASE  1024UL // (128*1024UL) 
//#define ALLOC_SIZE (1024UL*1024UL*1024UL*128)  //(1024UL*128*1024UL)  //(NUM_BUFS * BUF_SIZE)
#define ALLOC_SIZE (1024UL*1024UL*512)  //(1024UL*1024UL*1024UL*48)
#define MEMSET_PATTERN 'a'

#define MAX_THREADS 50 
#define LOG_COUNT 100000
//#define PRINT_OUTPUT 1

enum memop {
	MEMSET = 0x1,  
	MEMCOPY = 0x2,       
	MEMMOVE = 0x4,       
	MEMCMP = 0x8,        
};

struct parms {
    int buf_size;
	unsigned long long max_iters;
	int num_threads;
	uint32_t mem_ops;
	uint32_t sleep_time_us;
};

#ifdef PRINT_OUTPUT
atomic_int no_ops = 0;
#endif

int thread_func(void *thr_data)
{

	struct parms *p = (struct parms *)thr_data;
	uint64_t buf_size = BUF_SIZE_BASE * p->buf_size;
	uint64_t num_bufs = ALLOC_SIZE / buf_size;
	unsigned long long max_iters = p->max_iters;
	uint32_t mem_ops = p->mem_ops;
	uint32_t sleep_time_us = p->sleep_time_us;


	//printf("buf_size %lu num_bufs %lu max_iter %lu\n", buf_size, num_bufs,max_iters);

	// allocate memory
	void *src_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));
	void *dest_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));

	printf("alloc size %lu\n",ALLOC_SIZE);

	//uint8_t *s = src_addr;
	//uint8_t *d = dest_addr;

	for (unsigned long long i=0; i < p->max_iters; ++i) {
		int j = i % num_bufs;

		uint8_t *s = src_addr + j * buf_size;
		uint8_t *d = dest_addr + j * buf_size;

		// issue the memory transactions
		if (mem_ops & MEMSET) {
			//printf("dto-test memset %d %x\n",buf_size,s);
			memset(s, MEMSET_PATTERN, buf_size);
		}

		if (mem_ops & MEMCOPY) {
			//printf("dto-test memcpy %d %x %x\n",buf_size,s,d);
			memcpy(d, s, buf_size);
		}

		if (mem_ops & MEMMOVE) {
			memmove(d, s, buf_size);
			//printf("memmove %d\n",buf_size);
		}

		if (mem_ops & MEMCMP) {
			memcmp(d, s, buf_size);
			//printf("memcmp %d\n",buf_size);
		}
        

#ifdef PRINT_OUTPUT
		++no_ops;
		if (no_ops % LOG_COUNT == 0)
			printf("completed %d ops\n", no_ops);
#endif

		if (sleep_time_us > 0)
			usleep(sleep_time_us);

	}

	free(src_addr);
	free(dest_addr);

	return 0;
}

int main(int argc, char **argv)
{
	struct parms p[MAX_THREADS];
 	thrd_t threads[MAX_THREADS];

	if (argc < 6) {
		printf("Usage: dto-test-settable-size num_threads sleep_time_us mem_op buf_size max_iters [buf_size, max_iters for remaining threads]\n");
		printf("buf_size in increments of 1024, num_threads <= 10\n");
		return 1;
	}

	else{
		p[0].num_threads = atoi(argv[1]);
		p[0].sleep_time_us = atoi(argv[2]);
		p[0].mem_ops = atoi(argv[3]);
		p[0].buf_size = atoi(argv[4]);
		sscanf(argv[5], "%llu", &p[0].max_iters);
		//printf("num_threads %d buf_size %d iterations %llu\n",p[0].num_threads,p[0].buf_size, p[0].max_iters);
		
		if(p[0].num_threads > MAX_THREADS) {
			printf("number of threads must be <= %d\n",MAX_THREADS);
			return 1;
		}

		if(p[0].num_threads>1 && argc>6) {
			if(argc != 4+(p[0].num_threads*2)) {
				printf("Need to either provide one buf_size and num_iters, or a buf_size and num_iters for each thread");
				return 1;
			}

			for(int i = 1; i<p[0].num_threads;++i){
				p[i].num_threads = p[0].num_threads;
				p[i].mem_ops = p[0].mem_ops;
				p[i].sleep_time_us = p[0].sleep_time_us;
				p[i].buf_size = atoi(argv[6+(i-1)*2]);
				p[i].max_iters = atoi(argv[7+(i-1)*2]);
			}
		}

		else {
			for(int i = 1; i<p[0].num_threads;++i){
				p[i].max_iters = p[0].max_iters;
				p[i].mem_ops = p[0].mem_ops;
				p[i].sleep_time_us = p[0].sleep_time_us;
				p[i].num_threads = p[0].num_threads;
				p[i].buf_size = p[0].buf_size;

			}
		}	
	}

	//printf("buf_size %lu iters %lu threads %u\n",p.buf_size, p.max_iters, p.num_threads);

	for(int t = 0; t < p[0].num_threads; ++t)
		thrd_create(&threads[t], thread_func, &p[t]);

	for(int t = 0; t < p[0].num_threads; ++t)
		thrd_join(threads[t], NULL);
		
#ifdef PRINT_OUTPUT	
	printf("all threads completed execution\n");
#endif

	return 0;
}

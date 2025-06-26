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
#include <numa.h>
#include <sched.h>

//1024*1024*1024*256   A bit over the total memory in both NUMAs
#define NUM_BUFS  1024UL*48  //24*1024UL
#define BUF_SIZE_BASE  1024UL // (128*1024UL) 
#define BUF_SIZE 1024UL*BUF_SIZE_BASE  // 2048UL*BUF_SIZE_BASE
//#define ALLOC_SIZE (1024UL*48*1024UL)  //(1024UL*128*1024UL)  //(NUM_BUFS * BUF_SIZE)
//#define ALLOC_SIZE (2*1024UL*1024UL)
#define MEMSET_PATTERN 'a'

#define MAX_THREADS 50 
#define LOG_COUNT 100000
#define PRINT_OUTPUT 1

#define RAND_SEED 121919193

enum memop {
	MEMSET = 0x1,  
	MEMCOPY = 0x2,       
	MEMMOVE = 0x4,       
	MEMCMP = 0x8,        
};

enum numa_policy {
	NUMA_UNAWARE = 0x0,
	NUMA_SAME,
	NUMA_DIFF
};

struct parms {
    int buf_size;
	unsigned long long max_iters;
	int num_threads;
	uint32_t mem_ops;
	uint32_t sleep_time_us;
	uint32_t mem_buf_size;
	uint32_t num_mem_bufs;
	uint16_t use_random_index;
	uint32_t *index;
	uint8_t src_numa_policy;
	uint8_t dst_numa_policy;

};

#ifdef PRINT_OUTPUT
atomic_int no_ops = 0;
#endif

// get random number between 0 and 1
static __always_inline uint32_t getRand(uint32_t max)  
{

	uint32_t x = rand() / (RAND_MAX / (max + 1) + 1);

	return x;
}

static __always_inline void swap(uint32_t *a, uint32_t *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int thread_func(void *thr_data)
{

	//printf("starting thread\n");
	struct parms *p = (struct parms *)thr_data;
	uint64_t buf_size = BUF_SIZE_BASE * p->buf_size;
	uint32_t num_mem_bufs = p->num_mem_bufs;
	uint64_t mem_buf_size = p->mem_buf_size * BUF_SIZE_BASE;
	uint64_t num_bufs = mem_buf_size / buf_size;
	unsigned long long max_iters = p->max_iters;
	uint32_t mem_ops = p->mem_ops;
	uint32_t sleep_time_us = p->sleep_time_us;
	uint16_t use_random_index = p->use_random_index;
	uint32_t *index = p->index;
	uint8_t src_numa_policy = p->src_numa_policy;
	uint8_t dst_numa_policy = p->dst_numa_policy;


	uint8_t *s;
	uint8_t *d;

	printf("alloc size %lu num_mem_bufs %d allocated %lu, num_bufs %d\n",mem_buf_size,num_mem_bufs,mem_buf_size*num_mem_bufs,num_bufs);
	
	//uint8_t* src_buffs[num_mem_bufs];
	//uint8_t* dst_buffs[num_mem_bufs];


	if (src_numa_policy != NUMA_UNAWARE) {
		const int cpu = sched_getcpu();
		int node = numa_node_of_cpu(cpu);

		if(src_numa_policy == NUMA_SAME){
			//printf("src: allocating on same node %d\n",node);
			numa_set_preferred(node);
		}
		else {
			node = (node+1)%2;
			//printf("src: allocating on different node %d\n",node);
			numa_set_preferred(node);
		}
	}

	uint8_t** src_buffs = calloc(num_mem_bufs, sizeof(uint8_t*));
	for(uint32_t i=0;i<num_mem_bufs;++i) {
		src_buffs[i] = calloc(mem_buf_size, sizeof(uint8_t));
	}

	if (dst_numa_policy != NUMA_UNAWARE) {
		const int cpu = sched_getcpu();
		int node = numa_node_of_cpu(cpu);

		if(dst_numa_policy == NUMA_SAME){
			//printf("dst: allocating on same node %d\n",node);
			numa_set_preferred(node);
		}
		else {
			node = (node+1)%2;
			//printf("dst: allocating on different node %d\n",node);
			numa_set_preferred(node);
		}
	}

	uint8_t** dst_buffs = calloc(num_mem_bufs, sizeof(uint8_t*));
	for(uint32_t i=0;i<num_mem_bufs;++i) {
		dst_buffs[i] = calloc(mem_buf_size, sizeof(uint8_t));
	}
	
	//printf("allocated buffers\n");

	int j;
	int k;
	for (unsigned long long i=0; i < p->max_iters; ++i) {
		
		if (num_mem_bufs > 1) {
			j = i % num_mem_bufs;

			if (use_random_index)
				j=index[j];
			else
				k=index[j];

			//printf("num_bufs > 1 i %d j %d\n",i,j);
			s = src_buffs[j];
			d = dst_buffs[j];
		}

		else{
			j = i % num_bufs;
							
			//printf("num_bufs not > 1 i %d j %d\n",i,j);
			s = src_buffs[0] + j * buf_size;
			d = dst_buffs[0] + j * buf_size;
		}

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

	for(int i=0;i<num_mem_bufs;++i) {
		free(src_buffs[i]);
		free(dst_buffs[i]);
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct parms p[MAX_THREADS];
 	thrd_t threads[MAX_THREADS];

	if (argc != 11) {
		printf("Usage: dto-test-settable-size num_threads sleep_time_us mem_op buf_size max_iters mem_buf_size num_mem_bufs use_random_index src_numa_policy dst_numa_policy\n");
		printf("buf_size in increments of 1024, num_threads <= 10\n");
		return 1;
	}

	else{
		p[0].num_threads = atoi(argv[1]);
		p[0].sleep_time_us = atoi(argv[2]);
		p[0].mem_ops = atoi(argv[3]);
		p[0].buf_size = atoi(argv[4]);
		sscanf(argv[5], "%llu", &p[0].max_iters);
		p[0].mem_buf_size = atoi(argv[6]);
		p[0].num_mem_bufs = atoi(argv[7]);
		p[0].use_random_index = atoi(argv[8]);
		p[0].src_numa_policy = atoi(argv[9]);
		p[0].dst_numa_policy = atoi(argv[10]);

		printf("num_threads %d buf_size %d iterations %llu mem_buf_size %u num_bufs %u random %d\n",p[0].num_threads,p[0].buf_size, p[0].max_iters, p[0].mem_buf_size, p[0].num_mem_bufs,p[0].use_random_index);
		
		if(p[0].num_threads > MAX_THREADS) {
			printf("number of threads must be <= %d\n",MAX_THREADS);
			return 1;
		}

		if(p[0].buf_size > p[0].mem_buf_size) {
			printf("buf_size %d must be  <= mem_buf_size %d\n",p[0].buf_size,p[0].mem_buf_size);
			return 1;
		}

		
        for(int i = 1; i<p[0].num_threads;++i){
            p[i].mem_ops = p[0].mem_ops;
            p[i].sleep_time_us = p[0].sleep_time_us;
            p[i].num_threads = p[0].num_threads;
            p[i].buf_size = p[0].buf_size;
            p[i].mem_buf_size = p[0].mem_buf_size;
            p[i].num_mem_bufs = p[0].num_mem_bufs;
            p[i].use_random_index = p[0].use_random_index;
            p[i].src_numa_policy = p[0].src_numa_policy;
            p[i].dst_numa_policy = p[0].dst_numa_policy;

			
		}	
	}

	//printf("buf_size %lu iters %lu threads %u\n",p.buf_size, p.max_iters, p.num_threads);

	// create random index. If there is only 1 buffer, assume that buf_size is mem_buf_size and
	// create the index even though it won't be used (to include this overhead to allow for fair comparisons)
	uint32_t ind_size;
	if (p[0].num_mem_bufs>0){
		ind_size = p[0].num_mem_bufs;
	}
	else {
		ind_size = p[0].mem_buf_size/p[0].buf_size;
	}
	uint32_t *index = calloc(ind_size, sizeof(uint32_t));
	for (uint32_t i=0;i<ind_size;++i) {
		index[i] = i;
	}

	//printf("created index array\n");

	for (uint32_t i=ind_size-1;i>0;--i) {
		uint32_t j = getRand(i);
		if (j>ind_size)
			printf("random index too large %u\n",j);
		if (j<0)
			printf("random index too small %u\n",j);
		
		swap(&index[i],&index[j]);
	}

	//printf("finished randomizing index\n");
    unsigned long long num_iters_per_thread = p[0].max_iters / p[0].num_threads;
    unsigned long long remainder = p[0].max_iters - (num_iters_per_thread * p[0].num_threads);

    printf("num inters per thread %d remainder %d\n",num_iters_per_thread, remainder);

	for(int t = 0; t < p[0].num_threads; ++t) {
		p[t].index = index;
        p[t].max_iters = num_iters_per_thread;

        if (t==p[0].num_threads-1)
            p[t].max_iters += remainder;

		thrd_create(&threads[t], thread_func, &p[t]);
		//printf("created thread %d\n",t);
	}

	for(int t = 0; t < p[0].num_threads; ++t) {
		thrd_join(threads[t], NULL);
        printf("thread %d done\n", t);
    }
		
#ifdef PRINT_OUTPUT	
	printf("all threads completed execution\n");
#endif

	return 0;
}

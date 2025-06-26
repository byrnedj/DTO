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
//#define PRINT_OUTPUT 1

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
    int transaction_size;
	unsigned long long max_iters;
	uint32_t mem_ops;
	uint32_t sleep_time_us;
    uint8_t** src_buffs;
    uint8_t** dst_buffs;
    uint32_t num_mem_bufs;
    uint32_t thread_id;
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

//static __always_inline void swap(uint32_t *a, uint32_t *b) {
//    int temp = *a;
//    *a = *b;
//    *b = temp;
//}

static __always_inline void swap(uint8_t **a, uint8_t **b) {
    uint8_t *temp = *a;
    *a = *b;
    *b = temp;
}

int thread_func(void *thr_data)
{

	//printf("starting thread\n");
	struct parms *p = (struct parms *)thr_data;
	uint64_t transaction_size = BUF_SIZE_BASE * p->transaction_size;
	unsigned long long max_iters = p->max_iters;
	uint32_t mem_ops = p->mem_ops;
	uint32_t sleep_time_us = p->sleep_time_us;
    uint8_t** src_buffs = p->src_buffs;
    uint8_t** dst_buffs = p->dst_buffs;
    uint32_t num_mem_bufs = p->num_mem_bufs;
    uint32_t thread_id = p->thread_id;

    uint8_t *s;
	uint8_t *d;

    //for (uint32_t i=0;i<10;++i) {
    //    printf("%d: src addr %d: %x\n",thread_id, i,src_buffs[i]);
    //}

	for (unsigned long long i=0; i < p->max_iters; ++i) {
        s = src_buffs[i%num_mem_bufs];
        d = dst_buffs[i%num_mem_bufs];
		
		// issue the memory transactions
		if (mem_ops & MEMSET) {
			//printf("dto-test memset %d %x\n",buf_size,s);
			memset(s, MEMSET_PATTERN, transaction_size);
		}

		if (mem_ops & MEMCOPY) {
			//printf("dto-test memcpy %d %x %x\n",transaction_size,s,d);
			memcpy(d, s, transaction_size);
		}

		if (mem_ops & MEMMOVE) {
			memmove(d, s, transaction_size);
			//printf("memmove %d\n",buf_size);
		}

		if (mem_ops & MEMCMP) {
			memcmp(d, s, transaction_size);
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

	return 0;
}

int main(int argc, char **argv)
{
	struct parms p[MAX_THREADS];
 	thrd_t threads[MAX_THREADS];

    uint8_t src_numa_policy;
	uint8_t dst_numa_policy;
    uint8_t mem_ops;
    uint32_t num_threads;
    uint32_t sleep_time_us;
    uint32_t transaction_size;
    uint64_t total_num_iters;
    uint32_t num_mem_bufs;
    uint32_t mem_buf_size;
    uint32_t random_access;

	if (argc != 11) {
		printf("Usage: dto-test-settable-size num_threads sleep_time_us mem_op transaction_size total_num_iters mem_buf_size num_mem_bufs random_access src_numa_policy dst_numa_policy\n");
		printf("buf_size in increments of 1024, num_threads <= 10\n");
		return 1;
	}

	else{
		num_threads = atoi(argv[1]);
		sleep_time_us = atoi(argv[2]);
		mem_ops = atoi(argv[3]);
		transaction_size = atoi(argv[4]);
		sscanf(argv[5], "%llu", &total_num_iters);
		mem_buf_size = atoi(argv[6]);
		num_mem_bufs = atoi(argv[7]);
		random_access = atoi(argv[8]);
		src_numa_policy = atoi(argv[9]);
		dst_numa_policy = atoi(argv[10]);

		//printf("num_threads %d transaction_size %d total_iterations %llu mem_buf_size %u num_bufs %u random %d\n",num_threads,transaction_size, total_num_iters, mem_buf_size, num_mem_bufs,random_access);
		
		if(num_threads > MAX_THREADS) {
			printf("number of threads must be <= %d\n",MAX_THREADS);
			return 1;
		}

        if(num_mem_bufs < 1) {
            printf("number_mem_bufs must be > 0 number entered was %d\n", num_mem_bufs);
            return 1;
        } 

		if(transaction_size > mem_buf_size) {
			printf("transaction_size %d must be  <= mem_buf_size %d\n",transaction_size,mem_buf_size);
			return 1;
		}
	}

	//printf("buf_size %lu iters %lu threads %u\n",p.buf_size, p.max_iters, p.num_threads);

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
		src_buffs[i] = calloc(mem_buf_size*BUF_SIZE_BASE, sizeof(uint8_t));
	}

    //printf("first src buff %x\n",src_buffs[0]);

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
		dst_buffs[i] = calloc(mem_buf_size*BUF_SIZE_BASE, sizeof(uint8_t));
	}

	// ind_size is the effective number of buffers either allocated explicitly or partitioned from a single buffer
	uint32_t ind_size;
	if (num_mem_bufs>1){
		ind_size = num_mem_bufs;
	}
	else {
		ind_size = mem_buf_size/transaction_size;
	}


    //printf("number of virtual buffers %d\n", ind_size);
	//uint32_t *index = calloc(ind_size, sizeof(uint32_t));
	//for (uint32_t i=0;i<ind_size;++i) {
	//	index[i] = i;
	//}

    // the src_addrs and dst_addrs are addressed to the start of buffers. We either have allocated them individually, or we partition the one buffer.
    uint8_t** src_addrs = calloc(ind_size, sizeof(uint8_t*));
    uint8_t** dst_addrs = calloc(ind_size, sizeof(uint8_t*));
    if (num_mem_bufs>1) {
		for (uint32_t i=0;i<ind_size;++i) {
            src_addrs[i] = src_buffs[i];
            dst_addrs[i] = dst_buffs[i];
        }
	}
	else {
		for (uint32_t i=0;i<ind_size;++i) {
            src_addrs[i] = src_buffs[0] + transaction_size*BUF_SIZE_BASE*i;
            dst_addrs[i] = dst_buffs[0] + transaction_size*BUF_SIZE_BASE*i;
        }
	}

	// Randomize the order of the buffers is so desired.
	//printf("created index array\n");
    if (random_access) {
        for (uint32_t i=ind_size-1;i>0;--i) {
            uint32_t j = getRand(i);
            if (j>ind_size)
                printf("random index too large %u\n",j);
            if (j<0)
                printf("random index too small %u\n",j);
            
            swap(&src_addrs[i],&src_addrs[j]);
            swap(&dst_addrs[i],&dst_addrs[j]);
        }
    }

    //for (uint32_t i=0;i<10;++i) {
    //    printf("src addr %d: %x\n",i,src_addrs[i]);
    //}

	//printf("finished randomizing index\n");
    unsigned long long num_iters_per_thread = total_num_iters / num_threads;
    unsigned long long remainder = total_num_iters - (num_iters_per_thread * num_threads);

    uint32_t bufs_per_thread = ind_size / num_threads;

    //printf("num inters per thread %d remainder %d\n",num_iters_per_thread, remainder);
    //printf("num bufs per thread %d\n",bufs_per_thread);

	for(int t = 0; t < num_threads; ++t) {
        p[t].max_iters = num_iters_per_thread;
        p[t].mem_ops = mem_ops;
        p[t].sleep_time_us = sleep_time_us;
        p[t].transaction_size = transaction_size;
        p[t].src_buffs = &src_addrs[t*bufs_per_thread];
        p[t].dst_buffs = &dst_addrs[t*bufs_per_thread];
        p[t].num_mem_bufs = bufs_per_thread;
        p[t].thread_id = t;

        if (t==num_threads-1)
            p[t].max_iters += remainder;

		thrd_create(&threads[t], thread_func, &p[t]);
		//printf("created thread %d\n",t);
	}

	for(int t = 0; t < num_threads; ++t) {
		thrd_join(threads[t], NULL);
        //printf("thread %d done\n", t);
    }

    for(int i=0;i<num_mem_bufs;++i) {
		free(src_buffs[i]);
		free(dst_buffs[i]);
	}

    free(src_buffs);
    free(dst_buffs);
    free(src_addrs);
    free(dst_addrs);
		
#ifdef PRINT_OUTPUT	
	printf("all threads completed execution\n");
#endif

	return 0;
}

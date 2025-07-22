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
#include <x86intrin.h>
#include <sys/time.h>

//1024*1024*1024*256   A bit over the total memory in both NUMAs
//#define NUM_BUFS  1024UL*48  //24*1024UL
#define BUF_SIZE_BASE  1024UL // (128*1024UL) 
//#define BUF_SIZE 1024UL*BUF_SIZE_BASE  // 2048UL*BUF_SIZE_BASE
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
    int transaction_size;
	unsigned long long max_iters;
	uint32_t mem_ops;
	uint32_t sleep_time_us;
    uint8_t** src_buffs;
    uint8_t** dst_buffs;
    uint32_t num_mem_bufs;
    uint32_t thread_id;
    uint32_t num_threads;
    uint32_t num_burst_threads;
    uint64_t burst_size;
    uint32_t time_between_ms;
    uint32_t warmup_time_s;
	uint64_t cycles;
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

static inline uint64_t
get_ms(void)
{
	struct timeval tp;

	gettimeofday(&tp, NULL);

	return tp.tv_sec*1000+tp.tv_usec/1000;
}

static __always_inline uint64_t
rdtsc(void)
{
	uint64_t tsc;
	unsigned int dummy;

	/*
	 * https://www.felixcloutier.com/x86/rdtscp
	 * The RDTSCP instruction is not a serializing instruction, but it
	 * does wait until all previous instructions have executed and all
	 * previous loads are globally visible
	 *
	 * If software requires RDTSCP to be executed prior to execution of
	 * any subsequent instruction (including any memory accesses), it can
	 * execute LFENCE immediately after RDTSCP
	 */
	tsc = __rdtscp(&dummy);
	__builtin_ia32_lfence();

	return tsc;
}

static void
calibrate(uint64_t *cycles_per_sec)
{
	uint64_t  start;
	uint64_t  end;
	uint64_t starttick, endtick;
	uint64_t ms_diff, cycle_diff;

	endtick = get_ms();

	while (endtick == (starttick = get_ms()))
		;

	/* Measure cycle diff for 500 ms */
	start = rdtsc();
	while ((endtick = get_ms())  < (starttick + 500))
		;
	end = rdtsc();

	cycle_diff = end - start;
	ms_diff = endtick - starttick;

	/* ms * cycles_per_sec = cycle_diff * 1000 */

	*cycles_per_sec = (cycle_diff * (uint64_t)1000)/ms_diff;
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
    uint32_t num_threads = p->num_threads;

    uint32_t num_burst_threads = p->num_burst_threads;
    uint64_t burst_size = p->burst_size;
    uint32_t time_between_ms = p->time_between_ms;
    uint32_t warmup_time_s = p->warmup_time_s;

	p->cycles = 0;

    uint8_t *s;
	uint8_t *d;

	uint64_t start;
	
    //for (uint32_t i=0;i<10;++i) {
    //    printf("%d: src addr %d: %x\n",thread_id, i,src_buffs[i]);
    //}

	//printf("thread func num_mem_bufs %d\n", num_mem_bufs);

    char bursting = thread_id > num_threads-num_burst_threads-1;

	if(warmup_time_s > 0 && bursting) {
		sleep(warmup_time_s);
	}

    //uint64_t incr = max_iters/sleep_time_us;


	for (unsigned long long i=0; i < max_iters; ++i) {
        s = src_buffs[i%num_mem_bufs];
        d = dst_buffs[i%num_mem_bufs];


		start = rdtsc();
	
		//printf("next buffers: index %d, src %x, dst %x\n",i%num_mem_bufs,s,d);
		
		// issue the memory transactions
		if (mem_ops & MEMSET) {
			//printf("dto-test memset %d %x\n",transaction_size,s);
			memset(s, MEMSET_PATTERN, transaction_size);
		}

		if (mem_ops & MEMCOPY) {
			//printf("dto-test memcpy %d %x %x\n",transaction_size,s,d);
			memcpy(d, s, transaction_size);
		}

		if (mem_ops & MEMMOVE) {
			//printf("%d: memmove %x %x %d\n",i, s,d,transaction_size);
			memmove(d, s, transaction_size);
		}

		if (mem_ops & MEMCMP) {
			//printf("memcmp %d\n",transaction_size);
			memcmp(d, s, transaction_size);
		}

		p->cycles += (rdtsc() - start);
        

#ifdef PRINT_OUTPUT
		++no_ops;
		//if (no_ops % LOG_COUNT == 0)
		//	printf("completed %d ops\n", no_ops);
#endif
        /*
        if (sleep_time_us > 0 && i%incr == 0){
            --sleep_time_us;
            //printf("sleep time %d\n",sleep_time_us);
        }
        */

        if(bursting && i%burst_size==0) {
			//printf("sleeping for burst\n");
            usleep(time_between_ms*1000);
            //sleep(1);
        }
		

		if (sleep_time_us > 0) {
			//printf("sleeping\n");
			usleep(sleep_time_us);
		}

        if (no_ops > max_iters)
            break;

	}

	//printf("thread %d finished %d\n", thread_id,no_ops);

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
    uint64_t mem_buf_size;
	uint64_t allocated_src_buf_size; 
    uint32_t random_access;
    uint32_t num_burst_threads;
    uint64_t burst_size;
    uint32_t time_between_ms;
    uint32_t warmup_time_s;
	uint32_t percent_src_dst_overlap;
	uint8_t overlap_probability;
	uint64_t cycles=0;
	float latency;
	float bw;
	uint64_t cycles_per_sec;

	if (argc != 17) {
		printf("Usage: dto-test-settable-size num_threads sleep_time_us mem_op transaction_size total_num_iters mem_buf_size num_mem_bufs random_access src_numa_policy dst_numa_policy num_burst_threads burst_size time_between_ms warmup_time_s percent_src_dst_overlap overlap_probability\n");
		printf("buf_size in increments of 1024, num_threads <= 10\n");
		return 1;
	}

	else{
		num_threads = atoi(argv[1]);
		sleep_time_us = atoi(argv[2]);
		mem_ops = atoi(argv[3]);
		transaction_size = atoi(argv[4]);
		sscanf(argv[5], "%llu", &total_num_iters);
		//mem_buf_size = atoi(argv[6]);
		sscanf(argv[6], "%llu", &mem_buf_size);
		num_mem_bufs = atoi(argv[7]);
		random_access = atoi(argv[8]);
		src_numa_policy = atoi(argv[9]);
		dst_numa_policy = atoi(argv[10]);
        num_burst_threads = atoi(argv[11]);
        sscanf(argv[12], "%llu", &burst_size);
        time_between_ms = atoi(argv[13]);
        warmup_time_s = atoi(argv[14]);
		percent_src_dst_overlap = atoi(argv[15]);
		overlap_probability = atoi(argv[16]);

		calibrate(&cycles_per_sec);

		//printf("num_threads %d transaction_size %d total_iterations %llu mem_buf_size %llu num_bufs %u random %d\n",num_threads,transaction_size, total_num_iters, mem_buf_size, num_mem_bufs,random_access);
		
		if(num_threads > MAX_THREADS) {
			printf("number of threads must be <= %d\n",MAX_THREADS);
			return 1;
		}

        if(num_mem_bufs < 1) {
            printf("number_mem_bufs must be > 0 number entered was %d\n", num_mem_bufs);
            return 1;
        } 

		// overlapping buffers supported only for MEMMOVE and not for runs with multiple operations
		if (mem_ops != MEMMOVE && overlap_probability != 0) {
			printf("overlaping buffers supported only for MEMMOVE alone. overlap_probability modified to be 0.\n");
			return 1;
		}

		if(overlap_probability<0 || overlap_probability>100) {
			printf("overlap probablity must be a number between 0 and 100. %d was entered",overlap_probability);
		}

		if(overlap_probability>0) {
			if(percent_src_dst_overlap<1 || percent_src_dst_overlap>100) {
				printf("percent_src_dst_overlap must be a number between 1 and 100 if overlap_probability is > 0. %d was entered",percent_src_dst_overlap);
			}
		}

		if (overlap_probability > 0 && num_mem_bufs != 1) {
			printf("if overlap_probability > 0 num_mem_bufs must be 1");
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

	size_t page_size = getpagesize();

	uint8_t** src_buffs = calloc(num_mem_bufs, sizeof(uint8_t*));
	for(uint32_t i=0;i<num_mem_bufs;++i) {
		if (overlap_probability > 0) {
			float buf_size_mult = (float)2-(float)percent_src_dst_overlap/100;
			allocated_src_buf_size = (mem_buf_size*buf_size_mult);
			//printf("allocated src buffer %u: %llu %f bytes %x\n",i,allocated_src_buf_size*BUF_SIZE_BASE, buf_size_mult, src_buffs[i]);
	
		}
		else {
			allocated_src_buf_size = mem_buf_size;
			//printf("allocated src buffer %d: %llu bytes %x\n",i,allocated_src_buf_size*BUF_SIZE_BASE,src_buffs[i]);
		}

		src_buffs[i] = calloc(allocated_src_buf_size*BUF_SIZE_BASE, sizeof(uint8_t));

		for (size_t ii = 0; ii < allocated_src_buf_size*BUF_SIZE_BASE; ii += page_size) {
			src_buffs[i][ii] = 0;
		}
	}

    //printf("first src buff %x\n",src_buffs[0]);
	uint8_t** dst_buffs;
	if (overlap_probability < 100) {
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

		dst_buffs = calloc(num_mem_bufs, sizeof(uint8_t*));
		for(uint32_t i=0;i<num_mem_bufs;++i) {
			dst_buffs[i] = calloc(mem_buf_size*BUF_SIZE_BASE, sizeof(uint8_t));
			//printf("allocated dst buffer %d: %x\n",i,dst_buffs[i]);

			for (size_t ii = 0; ii < mem_buf_size*BUF_SIZE_BASE; ii += page_size) {
				dst_buffs[i][ii] = 0;
			}
		}

	}

	// ind_size is the effective number of buffers either allocated explicitly or partitioned from a single buffer
	uint32_t ind_size;
	uint32_t t_size;
	uint32_t ind_size_overlap;
	uint32_t t_size_overlap;
	uint32_t non_overlap_size;
	uint32_t overall_ind_size;
	if (num_mem_bufs>1){
		overall_ind_size = num_mem_bufs;
	}
	else {
		if (overlap_probability < 100) {
			ind_size = mem_buf_size/transaction_size;
			t_size = transaction_size;
			overall_ind_size = ind_size;
		}
		if (overlap_probability > 0) {
			non_overlap_size = transaction_size*(100-percent_src_dst_overlap)/100;
			t_size_overlap = transaction_size + non_overlap_size;
			ind_size_overlap = allocated_src_buf_size/t_size_overlap;
			ind_size_overlap -= 1;
			overall_ind_size = ind_size_overlap;
		}

	}

	if (overlap_probability > 0 && overlap_probability < 100) {
		if (ind_size < ind_size_overlap)
			overall_ind_size = ind_size;
		else
			overall_ind_size = ind_size_overlap;
	}

	if (overall_ind_size > 1)
		overall_ind_size--;


    //printf("number of virtual buffers nonoverlap %d number overlap %d \n", overall_ind_size, ind_size_overlap);
	//uint64_t max_allocated_bytes = (t_size_overlap*BUF_SIZE_BASE)*overall_ind_size;
	//printf("src buffer size %llu max allocated bytes %llu\n",allocated_src_buf_size*BUF_SIZE_BASE,max_allocated_bytes);
	//uint32_t *index = calloc(ind_size, sizeof(uint32_t));
	//for (uint32_t i=0;i<ind_size;++i) {
	//	index[i] = i;
	//}

    // the src_addrs and dst_addrs are addressed to the start of buffers. We either have allocated them individually, or we partition the one buffer.
    uint8_t** src_addrs = calloc(overall_ind_size, sizeof(uint8_t*));
	uint8_t** dst_addrs = calloc(overall_ind_size, sizeof(uint8_t*));
    
	if (num_mem_bufs>1) {
		for (uint32_t i=0;i<overall_ind_size;++i) {
            src_addrs[i] = src_buffs[i];
            dst_addrs[i] = dst_buffs[i];
        }
	}
	else {
		if (overlap_probability == 0) {
			for (uint32_t i=0;i<overall_ind_size;++i) {
				src_addrs[i] = src_buffs[0] + transaction_size*BUF_SIZE_BASE*i;
				dst_addrs[i] = dst_buffs[0] + transaction_size*BUF_SIZE_BASE*i;
			}
		}
		else if (overlap_probability == 100) {
			for (uint32_t i=0;i<overall_ind_size;++i) {
				src_addrs[i] = src_buffs[0] + t_size_overlap*BUF_SIZE_BASE*i;
				dst_addrs[i] = src_addrs[i] + non_overlap_size*BUF_SIZE_BASE;
			}
		}
		else {
			float prob = overlap_probability;
			prob = prob / 100;
			uint32_t j;
			uint8_t* src_buf_pos = src_buffs[0];
			uint8_t* dst_buf_pos = dst_buffs[0];

			//printf("transaction size %d nonoverlap size %d t_size_overlap %d\n", transaction_size,non_overlap_size,t_size_overlap);
			
			for (uint32_t i=0;i<overall_ind_size;++i) {
				j = getRand(1);
				// no overlap
				if (j>prob) {
					src_addrs[i] = src_buf_pos;
					dst_addrs[i] = dst_buf_pos;
					src_buf_pos += transaction_size*BUF_SIZE_BASE;
					dst_buf_pos += transaction_size*BUF_SIZE_BASE;
				}
				else {
					src_addrs[i] = src_buf_pos;
					dst_addrs[i] = src_buf_pos + non_overlap_size*BUF_SIZE_BASE;

					src_buf_pos += t_size_overlap*BUF_SIZE_BASE;
				}
			}
		}
	}

	// Randomize the order of the buffers is so desired.
	//printf("created index array\n");
    if (random_access) {
        for (uint32_t i=overall_ind_size-1;i>0;--i) {
            uint32_t j = getRand(i);
            if (j>overall_ind_size)
                printf("random index too large %u\n",j);
            if (j<0)
                printf("random index too small %u\n",j);
            
            swap(&src_addrs[i],&src_addrs[j]);
            swap(&dst_addrs[i],&dst_addrs[j]);
        }
    }

    //for (uint32_t i=0;i<overall_ind_size;++i) {
    //    printf("src addr %d: %x dst addr %x\n",i,src_addrs[i],dst_addrs[i]);
    //}

	//printf("finished randomizing index\n");
    unsigned long long num_iters_per_thread = total_num_iters / num_threads;
    //unsigned long long remainder = total_num_iters - (num_iters_per_thread * num_threads);

    uint32_t bufs_per_thread = overall_ind_size / num_threads;

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
        p[t].num_threads = num_threads;
        p[t].num_burst_threads = num_burst_threads;
        p[t].burst_size = burst_size;
        p[t].time_between_ms = time_between_ms;
        p[t].warmup_time_s = warmup_time_s;

        //if (t==num_threads-1)
        //    p[t].max_iters += remainder;

		thrd_create(&threads[t], thread_func, &p[t]);
		//printf("created thread %d\n",t);
	}

	for(int t = 0; t < num_threads; ++t) {
		thrd_join(threads[t], NULL);
        //printf("thread %d done\n", t);
		cycles += p[t].cycles;
    }

    for(int i=0;i<num_mem_bufs;++i) {
		free(src_buffs[i]);
		if (overlap_probability < 100)
			free(dst_buffs[i]);
	}

    free(src_buffs);
	if (overlap_probability < 100) 
	    free(dst_buffs);
    free(src_addrs);
    free(dst_addrs);

	float secs;
	latency = 1.0 * cycles / (num_threads*num_iters_per_thread);

	secs = (float)cycles/cycles_per_sec;
	bw = (num_threads*num_iters_per_thread) * (transaction_size*BUF_SIZE_BASE/secs)/1000000000;
	float latency_ns = (latency * 1E9)/cycles_per_sec;

	printf("'''\n");
	printf("# BW %f GB/s Latency %f ns cycles per second %llu\n",bw,latency, cycles_per_sec);
	printf("micro_stats={'BW':%f, 'latency':%f, 'cycles_per_sec':%llu}\n",bw,latency_ns,cycles_per_sec);
		
#ifdef PRINT_OUTPUT	
	//printf("all threads completed execution\n");
#endif

	return 0;
}

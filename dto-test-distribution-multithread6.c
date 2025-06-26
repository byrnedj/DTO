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

//#define NUM_BUFS  (16*4*1024UL) // (8*4*1024UL) // (4*1024UL)  
//#define BUF_SIZE  (16*1024UL) // (128*1024UL) 
#define BUF_SIZE_BASE  1024UL 
//#define ALLOC_SIZE (NUM_BUFS * BUF_SIZE)
#define MEMSET_PATTERN 'a'

#define MAX_INDEX_LENGTH 100000

#define MAX_THREADS 32 
#define LOG_COUNT 10000
#define PRINT_OUTPUT 1
#define MAX_ROWS 64

#define RAND_SEED 121919193

#define TS_NS(s, e) (((e.tv_sec*1000000000) + e.tv_nsec) - ((s.tv_sec*1000000000) + s.tv_nsec))
#define unlikely(x)     __builtin_expect((x), 0)


enum memop {
	MEMSET = 0x0,
	MEMCOPY,
	MEMMOVE,
	MEMCMP,
	MAX_MEMOP,
};

enum numa_policy {
	NUMA_UNAWARE = 0x0,
	NUMA_SAME,
	NUMA_DIFF
};

enum ramp_types {
    RAMP_TYPE_NONE = 0x0,
    RAMP_TYPE_UP_LINEAR,
    RAMP_TYPE_DOWN_LINEAR,
};

enum ramp_modes {
    RAMP_MODE_ALL = 0x0,
    RAMP_MODE_CONSTANT,
    RAMP_MODE_BURSTING
};

struct mem_op_entry {   // description of memory transaction
    int op;
    int size;
    //int size_bucket;
};

struct parms {
    double* cum_probs;   // list of cumulative probabilities for associated memory transaction entries
    struct mem_op_entry* entries;  // list of memory transaction entries
	unsigned long long max_iters;
    unsigned long long per_thread_iters;
    uint32_t sleep_time_us;
    uint32_t ramp_type;
    uint32_t ramp_mode;
    uint8_t** src_buffs;
    uint8_t** dst_buffs;
    uint32_t num_mem_bufs;
    uint32_t thread_id;
    uint32_t num_threads;

    //unsigned long long max_ops;
    //uint32_t max_buf_size;
    uint8_t *indicees;
    uint32_t start_ind;
    uint32_t num_indicees;

    uint32_t num_burst_threads;
    uint64_t burst_size;
    uint32_t time_between_ms;
    uint32_t warmup_time_s;

};

#ifdef PRINT_OUTPUT
atomic_int no_ops = 0;
#endif

// get random number between 0 and 1
double getRand()  
{
	double x = rand()/((double)RAND_MAX+1);
	return x;
};

static __always_inline uint32_t getRand2(uint32_t max)  
{

	uint32_t x = rand() / (RAND_MAX / (max + 1) + 1);

	return x;
}

static __always_inline void swap(uint8_t **a, uint8_t **b) {
    uint8_t *temp = *a;
    *a = *b;
    *b = temp;
}

int thread_func(void *thr_data)
{

	struct parms *p = (struct parms *)thr_data;
	unsigned long long max_iters = p->max_iters;
    unsigned long long per_thread_iters = p->per_thread_iters;
    uint32_t start_ind = p->start_ind;
    uint32_t num_indicees = p->num_indicees;
    uint8_t *indicees = p->indicees;
    double* cum_probs = p->cum_probs;
    struct mem_op_entry* entries = p->entries;

    int32_t sleep_time_us = p->sleep_time_us;
    uint32_t ramp_type = p->ramp_type;
    uint32_t ramp_mode = p->ramp_mode;
    uint8_t** src_buffs = p->src_buffs;
    uint8_t** dst_buffs = p->dst_buffs;
    uint32_t num_mem_bufs = p->num_mem_bufs;
    uint32_t thread_id = p->thread_id;
    uint32_t num_threads = p->num_threads;
    //uint32_t initial_sleep_time = 0;

    uint32_t num_burst_threads = p->num_burst_threads;
    uint64_t burst_size = p->burst_size;
    uint32_t time_between_ms = p->time_between_ms;
    uint32_t warmup_time_s = p->warmup_time_s;
    
    int size;
    int mem_op;
    int ind;

    //printf("%d: num_iters %llu start_ind %d num_inds %d num_mem_bufs %d\n", thread_id, max_iters, start_ind, num_indicees, num_mem_bufs);

    //for (uint32_t i=0;i<10;++i) {
    //    printf("%d: index %d: %x\n",thread_id, i,indicees[start_ind+i]);
    //}

    uint8_t *s;
	uint8_t *d;
    //uint64_t delay;
    //uint64_t sleep_time_cycles = sleep_time_us*1000;

    //if (sleep_time_us > 0) {
    //    float init_sleep_time = sleep_time_us;
    //    uint32_t initial_sleep_time = init_sleep_time/num_threads*thread_id;
        //printf("%d: num_threads %d sleep_time_us %d init_sleep_time %f initial sleep time %d\n",thread_id,num_threads, sleep_time_us, init_sleep_time, initial_sleep_time);
    //    usleep(initial_sleep_time);
    //}
    
    char bursting = thread_id > num_threads-num_burst_threads-1;
    char ramping = 0;
    char done_with_warmup;
    uint64_t incr;
    struct timespec st, ct;
    uint32_t sleep_increment;
    uint32_t start_delay;

    if ((sleep_time_us > 0) && (ramp_type != RAMP_TYPE_NONE)) {
        if (ramp_mode == RAMP_MODE_ALL) {
            ramping = 1;
        }
        else {
            if ((bursting && ramp_mode == RAMP_MODE_BURSTING) || (!bursting && ramp_mode == RAMP_MODE_CONSTANT))
                ramping = 1;
        }
    }

    if (ramping && ((ramp_type == RAMP_TYPE_DOWN_LINEAR) || (ramp_type == RAMP_TYPE_UP_LINEAR))) {
        incr = per_thread_iters/sleep_time_us;
        start_delay = sleep_time_us/num_threads;
        if (ramp_type == RAMP_TYPE_DOWN_LINEAR) {
            sleep_increment = 1;
            sleep_time_us = 0;
        }
        else    
            sleep_increment = -1;
    }
        
    clock_gettime(CLOCK_BOOTTIME, &st);				

    if(warmup_time_s > 0 && bursting) {
		sleep(warmup_time_s);
        done_with_warmup = 1;
	}

    if (ramping && ((ramp_type == RAMP_TYPE_DOWN_LINEAR) || (ramp_type == RAMP_TYPE_UP_LINEAR)) && start_delay > 0) {
         sleep(delay_time);
    }

    for (unsigned long long i=0; i < max_iters; ++i) {
        s = src_buffs[i%num_mem_bufs];
        d = dst_buffs[i%num_mem_bufs];
        
        ind = indicees[(start_ind+i)%num_indicees];

        size = entries[ind].size;
        mem_op = entries[ind].op;
        //bucket_size = entries[ind].size_bucket;

        // issue the memory transaction
        switch (mem_op) {
            case MEMSET: {
                memset(s, MEMSET_PATTERN, size);
                //printf("memset %d\n",size);
                break;
            }

            case MEMCOPY: {
                memcpy(d, s, size);
                //printf("memscpy %d\n",size);
                break;
            }

            case MEMMOVE: {
                memmove(d, s, size);
                //printf("memmove %d\n",size);
                break;
            }
        }

#ifdef PRINT_OUTPUT
		++no_ops;
		//if (no_ops % LOG_COUNT == 0)
		//	printf("completed %d ops\n", no_ops);
#endif

        if (ramping) {
            if(i%incr == 0 && i != 0){
                if(warmup_time_s > 0 && !done_with_warmup) {
                    clock_gettime(CLOCK_BOOTTIME, &ct);	
                    if (ct.tv_sec - st.tv_sec >= warmup_time_s) {
                        done_with_warmup = 1;
                        sleep_time_us += sleep_increment;
                        //printf("%d done with warmup\n",thread_id);
                    }			

                }
                else {
                    sleep_time_us += sleep_increment;
                    //printf("%d sleep time %d\n",thread_id, sleep_time_us);
                }
            }
        }
        
        if (ramping && sleep_time_us > 0) {
            //printf("%d: sleeping %d\n",thread_id,sleep_time_us);
			usleep(sleep_time_us);
        }


        if(bursting && i%burst_size==0) {
            //printf("%d sleeping between bursts\n",thread_id);
            usleep(time_between_ms*1000);
            //sleep(1);
        }

        if (no_ops >= max_iters) {
            //if(ramping)
            //    printf("%d: i %d incr %d\n",thread_id,i,incr);
            break;
        }
 
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct parms p[MAX_THREADS];
 	thrd_t threads[MAX_THREADS];

    struct mem_op_entry entries[120];
    double cum_probs[120];

    char line[100];
    char *sp;
    //int size_buckets[MAX_ROWS]; // array of size bucket starts
    int sizes[MAX_ROWS];   //array of average transaction sizes
    double cpy_probs[MAX_ROWS];  // array of probablilities for copy 
    double set_probs[MAX_ROWS];  // probabilities for set
    double mov_probs[MAX_ROWS];  // probabilities for move
    int count = 0;
    double cur_prob = 0.0;  // running total of cumulative probabilities
    int cur_entry = 0;

    int num_threads;
    uint32_t sleep_time_us;
    uint32_t ramp_type;
    uint32_t ramp_mode;
    unsigned long long num_iter;
    unsigned long long per_thread_iters;
    uint32_t mem_buf_size;
	uint32_t num_mem_bufs;
    uint16_t random_access;
	uint8_t src_numa_policy;
	uint8_t dst_numa_policy;
    uint32_t max_transaction_size=0;
    uint32_t num_burst_threads;
    uint64_t burst_size;
    uint32_t time_between_ms;
    uint32_t warmup_time_s;


    FILE *fp;

	if (argc != 16) {
		printf("Usage: dto-test-distribution num_threads sleep_time sleep_ramp_type sleep_ramp_mode num_iters mem_buf_size num_mem_bufs use_random_index src_numa_policy dst_numa_policy num_burst_threads burst_size time_between_ms warmup_time_s input_file_path \n");
		exit(0);
	}

    num_threads = atoi(argv[1]);
    sleep_time_us = atoi(argv[2]);
    ramp_type = atoi(argv[3]);
    ramp_mode = atoi(argv[4]);
    sscanf(argv[5], "%llu", &num_iter);
    mem_buf_size = atoi(argv[6]);
	num_mem_bufs = atoi(argv[7]);
    random_access = atoi(argv[8]);
	src_numa_policy = atoi(argv[9]);
	dst_numa_policy = atoi(argv[10]);
    num_burst_threads = atoi(argv[11]);
    sscanf(argv[12], "%llu", &burst_size);
    time_between_ms = atoi(argv[13]);
    warmup_time_s = atoi(argv[14]);

    if(num_threads > MAX_THREADS) {
        printf("number of threads must be <= %d\n",MAX_THREADS);
        exit(0);
    }

    if(num_mem_bufs < 1) {
        printf("number_mem_bufs must be > 0 number entered was %d\n", num_mem_bufs);
        exit(0);
    } 

    //printf("num_threads %d iterations %llu mem_buf_size %u num_bufs %u random %d\n",num_threads, num_iter, mem_buf_size, num_mem_bufs,random_access);

    fp = fopen(argv[15],"r");
    if (fp == NULL)
    {
        printf("Can not open file %s\n",argv[15]);
        exit(0);
    }

    // read in the data from the csv file
    while(fgets(line, 100, fp )!= NULL)
    {
        //printf("%s\n",line);

        //sp = strtok(line, ",");
        //size_buckets[count] = atoi(sp);

        sp = strtok(line, ",");
        sizes[count] = atoi(sp);
        if (sizes[count] > max_transaction_size)
            max_transaction_size = sizes[count];

        sp = strtok(NULL, ",");
        set_probs[count] = atof(sp);

        sp = strtok(NULL, ",");
        cpy_probs[count] = atof(sp);

        sp = strtok(NULL, ",");
        mov_probs[count] = atof(sp);

        count++;

        // The current data has 39 entries. This number could be higher in other data
        //TODO: have the user pass in the size of the arrays
        if (count > MAX_ROWS) {
            printf("File contains too many lines. We assume that there are no more than %d rows\n", MAX_ROWS);
            exit(0);
        }
    }

    fclose(fp);

    max_transaction_size = max_transaction_size/BUF_SIZE_BASE;

    if(max_transaction_size > mem_buf_size) {
        printf("transaction size %d must be  <= mem_buf_size %d\n",max_transaction_size,mem_buf_size);
        return 1;
    }

    //for (int i=0; i<count; ++i){
    //    printf("%d: size %d set %f cpy %f cmp %f\n",i,sizes[i],set_probs[i],cpy_probs[i],mov_probs[i]);
    //}

    // create one array of all memory transactions. build up an array of cumulative probability, so we can select from the array using a random number between 0 and 1
    for (int i=0; i<count; ++i){
        if (set_probs[i] > 0){
            entries[cur_entry].op = MEMSET;
            entries[cur_entry].size = sizes[i];

            //if (size_buckets[i]+4096 > entries[cur_entry].size)
            //    entries[cur_entry].size_bucket = size_buckets[i] + 4096;
            //else
            //    entries[cur_entry].size_bucket = entries[cur_entry].size;

            cur_prob += set_probs[i];
            cum_probs[cur_entry] = cur_prob;

            //printf("%d: memset size %d cum_prob %f\n", cur_entry, entries[cur_entry].size, cum_probs[cur_entry]);

            ++cur_entry;
        }

        if (cpy_probs[i] > 0){
            entries[cur_entry].op = MEMCOPY;
            entries[cur_entry].size =sizes[i];

            //if (size_buckets[i]+4096 > entries[cur_entry].size)
            //    entries[cur_entry].size_bucket = size_buckets[i] + 4096;
            //else
            //    entries[cur_entry].size_bucket = entries[cur_entry].size;
            
            cur_prob += cpy_probs[i];
            cum_probs[cur_entry] = cur_prob;

            //printf("%d: memcpy size %d cum_prob %f\n", cur_entry, entries[cur_entry].size, cum_probs[cur_entry]);

            ++cur_entry;
        }

        if (mov_probs[i] > 0){
            entries[cur_entry].op = MEMMOVE;
            entries[cur_entry].size = sizes[i];
            
            //if (size_buckets[i]+4096 > entries[cur_entry].size)
            //    entries[cur_entry].size_bucket = size_buckets[i] + 4096;
            //else
            //    entries[cur_entry].size_bucket = entries[cur_entry].size;
            
            cur_prob += mov_probs[i];
            cum_probs[cur_entry] = cur_prob;

            //printf("%d: memmov size %d cum_prob %f\n", cur_entry, entries[cur_entry].size, cum_probs[cur_entry]);

            ++cur_entry;
        }
    }

    uint32_t num_entries = cur_entry;

    uint32_t index_length = MAX_INDEX_LENGTH;
    if (index_length > num_iter)
        index_length = num_iter;

    uint8_t *indicees = calloc(index_length, sizeof(uint8_t));
    double rn;
    int ind;
    
    srand(RAND_SEED);

	for (uint32_t i=0; i < index_length; ++i) {

        // draw a random number and select a transaction to perform
        rn = getRand();
        for (ind=0;ind<num_entries;++ind) {
            if (rn < cum_probs[ind])
                break;
        }

        indicees[i] = ind;
    }

    //for (uint32_t i=0;i<10;++i) {
    //    printf("index %d: %x\n",i,indicees[i]);
    //}

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
		ind_size = mem_buf_size/max_transaction_size;
	}


    //printf("number of virtual buffers %d\n", ind_size);

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
            src_addrs[i] = src_buffs[0] + max_transaction_size*BUF_SIZE_BASE*i;
            dst_addrs[i] = dst_buffs[0] + max_transaction_size*BUF_SIZE_BASE*i;
        }
	}

	// Randomize the order of the buffers is so desired.
	//printf("created index array\n");
    if (random_access) {
        for (uint32_t i=ind_size-1;i>0;--i) {
            uint32_t j = getRand2(i);
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

    //unsigned long long num_iters_per_thread = num_iter / num_threads;
    //unsigned long long remainder = num_iter - (num_iters_per_thread * num_threads);

    per_thread_iters = num_iter / num_threads;

    uint32_t bufs_per_thread = ind_size / num_threads;
    uint32_t indicees_per_thread = index_length / num_threads;

    //printf("num inters per thread %d remainder %d\n",num_iters_per_thread, remainder);
    //printf("num bufs per thread %d\n",bufs_per_thread);

	for(int t = 0; t < num_threads; ++t) {
        p[t].cum_probs = cum_probs;
        p[t].entries = entries;
        p[t].max_iters = num_iter;  //num_iters_per_thread;
        p[t].per_thread_iters = per_thread_iters;
        p[t].indicees = indicees;
        p[t].start_ind = t*indicees_per_thread;
        p[t].num_indicees = index_length;
        p[t].src_buffs = &src_addrs[t*bufs_per_thread];
        p[t].dst_buffs = &dst_addrs[t*bufs_per_thread];
        p[t].num_mem_bufs = bufs_per_thread;
        p[t].thread_id = t;
        p[t].num_threads = num_threads;
        p[t].sleep_time_us = sleep_time_us;
        p[t].ramp_type = ramp_type;
        p[t].ramp_mode = ramp_mode;
        p[t].num_burst_threads = num_burst_threads;
        p[t].burst_size = burst_size;
        p[t].time_between_ms = time_between_ms;
        p[t].warmup_time_s = warmup_time_s;
        
        
        //if (t==num_threads-1)
        //    p[t].max_iters += remainder;

        //printf("thread %d start index %lu end index %lu\n",t, p[t].start_ind, p[t].start_ind+num_iters_per_thread);

		thrd_create(&threads[t], thread_func, &p[t]);
    }

	for(int t = 0; t < num_threads; ++t)
		thrd_join(threads[t], NULL);
	
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

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

#define NUM_BUFS  (16*4*1024UL) // (8*4*1024UL) // (4*1024UL)  
#define BUF_SIZE  (16*1024UL) // (128*1024UL) 
#define BUF_SIZE_BASE  1024UL 
#define ALLOC_SIZE (NUM_BUFS * BUF_SIZE)
#define MEMSET_PATTERN 'a'

#define MAX_THREADS 32 
#define LOG_COUNT 1000000
//#define PRINT_OUTPUT 1
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

struct mem_op_entry {   // description of memory transaction
    int op;
    int size;
    //int size_bucket;
};

struct parms {
    double* cum_probs;   // list of cumulative probabilities for associated memory transaction entries
    struct mem_op_entry* entries;  // list of memory transaction entries
	unsigned long long max_iters;
    unsigned long long max_ops;
    unsigned long long start_ind;
    uint32_t max_buf_size;
    uint8_t *indicees;
    uint32_t mem_buf_size;
	uint32_t num_mem_bufs;
	uint16_t use_random_index;
	uint32_t *buf_index;
	uint8_t src_numa_policy;
	uint8_t dst_numa_policy;
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

static __always_inline void swap(uint32_t *a, uint32_t *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

int thread_func(void *thr_data)
{

	struct parms *p = (struct parms *)thr_data;
	unsigned long long max_iters = p->max_iters;
    unsigned long long start_ind = p->start_ind;
    uint8_t *indicees = p->indicees;
    double* cum_probs = p->cum_probs;
    struct mem_op_entry* entries = p->entries;
    uint16_t use_random_index = p->use_random_index;
	uint32_t *buf_index = p->buf_index;
	uint8_t src_numa_policy = p->src_numa_policy;
	uint8_t dst_numa_policy = p->dst_numa_policy;
    uint32_t num_mem_bufs = p->num_mem_bufs;
	uint64_t mem_buf_size = p->mem_buf_size * BUF_SIZE_BASE;
    uint64_t buf_size = BUF_SIZE_BASE * p->max_buf_size;
    uint64_t num_bufs = mem_buf_size / buf_size;

    
    int size;
    //int bucket_size;
    int mem_op;
    int ind;

    printf("alloc size %lu num_mem_bufs %d allocated %lu, num_bufs %d\n",mem_buf_size,num_mem_bufs,mem_buf_size*num_mem_bufs,num_bufs);

    //int cur_offset = 0;

    //for (ind=0;ind<num_entries;++ind) 
    //    printf("prob %f op %d size %d bucket %d\n",cum_probs[ind],entries[ind].op,entries[ind].size,entries[ind].size_bucket);

	//printf("buf_size %lu num_bufs %lu max_iter %lu\n", buf_size, num_bufs,max_iters);

    uint8_t *s;
	uint8_t *d;

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


	// allocate memory
	//void *src_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));
	//void *dest_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));

    int j;
	int k;

    for (unsigned long long i=start_ind; i < start_ind + max_iters; ++i) {
        
        if (num_mem_bufs > 1) {
			j = i % num_mem_bufs;

			if (use_random_index)
				j=buf_index[j];
			else
				k=buf_index[j];

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

        ind = indicees[i];

        size = entries[ind].size;
        mem_op = entries[ind].op;
        //bucket_size = entries[ind].size_bucket;

        //if (cur_offset + size > ALLOC_SIZE)
        //    cur_offset = 0;

		//uint8_t *s = src_addr + cur_offset;
		//uint8_t *d = dest_addr + cur_offset;

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

        // move the start pointer in the src/dst arrays, so we are always working on new memory locations
        //cur_offset += bucket_size;
        //cur_offset += size;

#ifdef PRINT_OUTPUT
		++no_ops;
		if (no_ops % LOG_COUNT == 0)
			printf("completed %d ops\n", no_ops);
#endif

 
	}

	//free(src_addr);
	//free(dest_addr);

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
    unsigned long long num_iter;
    uint32_t mem_buf_size;
	uint32_t num_mem_bufs;
    uint16_t use_random_index;
	uint8_t src_numa_policy;
	uint8_t dst_numa_policy;
    uint32_t max_transaction_size=0;


    FILE *fp;

	if (argc != 9) {
		printf("Usage: dto-test-distribution num_threads num_iters mem_buf_size num_mem_bufs use_random_index src_numa_policy dst_numa_policy input_file_path\n");
		exit(0);
	}


    num_threads = atoi(argv[1]);
    //printf("max iters %llu num threads %d\n",p.max_iters,p.num_threads);
    if(num_threads > MAX_THREADS) {
        printf("number of threads must be <= %d\n",MAX_THREADS);
        exit(0);
    }

    sscanf(argv[2], "%llu", &num_iter);

    mem_buf_size = atoi(argv[3]);
	num_mem_bufs = atoi(argv[4]);
    use_random_index = atoi(argv[5]);
	src_numa_policy = atoi(argv[6]);
	dst_numa_policy = atoi(argv[7]);

    printf("num_threads %d iterations %llu mem_buf_size %u num_bufs %u random %d\n",num_threads, num_iter, mem_buf_size, num_mem_bufs,use_random_index);
		

    fp = fopen(argv[8],"r");
    if (fp == NULL)
    {
        printf("Can not open file %s\n",argv[8]);
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

    int num_entries = cur_entry;
    uint8_t *indicees = calloc(num_iter, sizeof(uint8_t));
    double rn;
    int ind;
    
    srand(RAND_SEED);

	for (unsigned long long i=0; i < num_iter; ++i) {

        // draw a random number and select a transaction to perform
        rn = getRand();
        for (ind=0;ind<num_entries;++ind) {
            if (rn < cum_probs[ind])
                break;
        }

        indicees[i] = ind;
    }

	//printf("buf_size %lu iters %lu threads %u\n",p.buf_size, p.max_iters, p.num_threads);

    unsigned long long num_iters_per_thread = num_iter / num_threads;
    unsigned long long remainder = num_iter - (num_iters_per_thread * num_threads);

    //printf("total iters %lu threads %u iters per thread %lu remainder %lu\n",num_iter, num_threads, num_iters_per_thread, remainder);

    // create random index. If there is only 1 buffer, assume that buf_size is mem_buf_size and
	// create the index even though it won't be used (to include this overhead to allow for fair comparisons)
	uint32_t ind_size;
	if (num_mem_bufs>0){
		ind_size = num_mem_bufs;
	}
	else {
		ind_size = mem_buf_size/max_transaction_size;
	}
	uint32_t *index = calloc(ind_size, sizeof(uint32_t));
	for (uint32_t i=0;i<ind_size;++i) {
		index[i] = i;
	}

	printf("created index array size %d\n",ind_size);

	for (uint32_t i=ind_size-1;i>0;--i) {
		uint32_t j = getRand2(i);
		if (j>ind_size)
			printf("random index too large %u\n",j);
		if (j<0)
			printf("random index too small %u\n",j);
		
		swap(&index[i],&index[j]);
	}

    printf("randomized index array\n");

	for(int t = 0; t < num_threads; ++t) {
        p[t].cum_probs = cum_probs;
        p[t].entries = entries;
        p[t].max_iters = num_iters_per_thread;
        p[t].indicees = indicees;
        p[t].start_ind = t*num_iters_per_thread;
        p[t].mem_buf_size = mem_buf_size;
        p[t].num_mem_bufs = num_mem_bufs;
        p[t].use_random_index = use_random_index;
        p[t].src_numa_policy = src_numa_policy;
        p[t].dst_numa_policy = dst_numa_policy;
        p[t].buf_index = index;
        p[t].max_buf_size = max_transaction_size;
        
        if (t==num_threads-1)
            p[t].max_iters += remainder;

        //printf("thread %d start index %lu end index %lu\n",t, p[t].start_ind, p[t].start_ind+num_iters_per_thread);

		thrd_create(&threads[t], thread_func, &p);
    }

	for(int t = 0; t < num_threads; ++t)
		thrd_join(threads[t], NULL);
	
#ifdef PRINT_OUTPUT	
	printf("all threads completed execution\n");
#endif

	return 0;
}

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

#define NUM_BUFS  (16*4*1024UL) // (8*4*1024UL) // (4*1024UL)  
#define BUF_SIZE  (16*1024UL) // (128*1024UL) 
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
    uint8_t *indicees;
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


int thread_func(void *thr_data)
{

	struct parms *p = (struct parms *)thr_data;
	unsigned long long max_iters = p->max_iters;
    unsigned long long start_ind = p->start_ind;
    uint8_t *indicees = p->indicees;
    double* cum_probs = p->cum_probs;
    struct mem_op_entry* entries = p->entries;
    
    int size;
    //int bucket_size;
    int mem_op;
    int ind;

    int cur_offset = 0;

    //for (ind=0;ind<num_entries;++ind) 
    //    printf("prob %f op %d size %d bucket %d\n",cum_probs[ind],entries[ind].op,entries[ind].size,entries[ind].size_bucket);

	//printf("buf_size %lu num_bufs %lu max_iter %lu\n", buf_size, num_bufs,max_iters);

	// allocate memory
	void *src_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));
	void *dest_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));

    for (unsigned long long i=start_ind; i < start_ind + max_iters; ++i) {
        
        ind = indicees[i];

        size = entries[ind].size;
        mem_op = entries[ind].op;
        //bucket_size = entries[ind].size_bucket;

        if (cur_offset + size > ALLOC_SIZE)
            cur_offset = 0;

		uint8_t *s = src_addr + cur_offset;
		uint8_t *d = dest_addr + cur_offset;

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
        cur_offset += size;

#ifdef PRINT_OUTPUT
		++no_ops;
		if (no_ops % LOG_COUNT == 0)
			printf("completed %d ops\n", no_ops);
#endif

 
	}

	free(src_addr);
	free(dest_addr);

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
    

    FILE *fp;

	if (argc != 4) {
		printf("Usage: dto-test-distribution num_iters num_threads input_file_path\n");
		exit(0);
	}


    num_threads = atoi(argv[2]);
    //printf("max iters %llu num threads %d\n",p.max_iters,p.num_threads);
    if(num_threads > MAX_THREADS) {
        printf("number of threads must be <= %d\n",MAX_THREADS);
        exit(0);
    }

    sscanf(argv[1], "%llu", &num_iter);

    fp = fopen(argv[3],"r");
    if (fp == NULL)
    {
        printf("Can not open file %s\n",argv[2]);
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

	for(int t = 0; t < num_threads; ++t) {
        p[t].cum_probs = cum_probs;
        p[t].entries = entries;
        p[t].max_iters = num_iters_per_thread;
        p[t].indicees = indicees;
        p[t].start_ind = t*num_iters_per_thread;
        
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

/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
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

#define NUM_BUFS  (8*4*1024UL) // (4*1024UL) // 1 //750 // 
#define BUF_SIZE  (16*1024UL) // (128*1024UL) //  (128*1024UL) // (2*8192UL)  //
#define ALLOC_SIZE (NUM_BUFS * BUF_SIZE)
#define MEMSET_PATTERN 'a'

//#define MAX_ITERS 8000000  //1000000
#define MAX_THREADS 10 
#define LOG_COUNT 10000
#define INTERVAL_MARGIN 60000  // We subtract a margin from the interval between transactions to account for processing time of the code and other slop...

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
};

struct parms {
    double* cum_probs;   // list of cumulative probabilities for associated memory transaction entries
    struct mem_op_entry* entries;  // list of memory transaction entries
    int num_entries;  // number of entries in the lis
	int max_iters;
	int num_threads;
    int interval_ns;
};

atomic_int no_ops = 0;

// get random number between 0 and 1
double getRand()  
{
	double x = rand()/((double)RAND_MAX+1);
	return x;
};


int thread_func(void *thr_data)
{

	struct parms *p = (struct parms *)thr_data;
	uint32_t max_iters = p->max_iters;
    double* cum_probs = p->cum_probs;
    int num_entries = p->num_entries;
    struct mem_op_entry* entries = p->entries;
    struct timespec start_time;
    struct timespec end_time;

    struct timespec overall_start_time;
    struct timespec overall_end_time;

    int ind;
    double rn;
    int size;
    int mem_op;

    int cur_offset = 0;

    //for (ind=0;ind<num_entries;++ind) 
    //    printf("prob %f op %d size %d\n",cum_probs[ind],entries[ind].op,entries[ind].size);

	//printf("buf_size %lu num_bufs %lu max_iter %lu\n", buf_size, num_bufs,max_iters);

	// allocate memory
	void *src_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));
	void *dest_addr = calloc(ALLOC_SIZE, sizeof(uint8_t));



	uint8_t updating = 0;
	long elapsed;
	long window[100];  //We make sure that the running average of the last 100 transactions does not exceed the interval we want to maintain between transactions
    int cur_pos=0;   // current position in the window
    long cur_total=0.0;  // current total of entries in the window
    long window_interval_ns=p->interval_ns*100;  // multipy by 10 because we are comparing against the sum of the last 10
    long interval_ns = p->interval_ns - INTERVAL_MARGIN;

    for (int i=0;i<100;++i)
        window[i] = 0;

    // capture time at the start to check at the end what average rate we maintained
    clock_gettime(CLOCK_BOOTTIME, &overall_start_time);

	for (int i=0; i < p->max_iters; ++i) {

        clock_gettime(CLOCK_BOOTTIME, &start_time);

        // draw a random number and select a transaction to perform
        rn = getRand();
        for (ind=0;ind<num_entries;++ind) {
            if (rn < cum_probs[ind])
                break;
        }

        size = entries[ind].size;
        mem_op = entries[ind].op;

        if (cur_offset + size > ALLOC_SIZE)
            cur_offset = 0;

		uint8_t *s = src_addr + cur_offset;
		uint8_t *d = dest_addr + cur_offset;

        //printf("%f %d %d %d\n",rn,mem_op,size, cur_offset);

        // issue the memory transaction
        switch (mem_op) {
            case MEMSET: {
                memset(s, MEMSET_PATTERN, size);
                break;
            }

            case MEMCOPY: {
                memcpy(d, s, size);
                break;
            }

            case MEMMOVE: {
                memmove(d, s, size);
                break;
            }
        }

        // move the start pointer in the src/dst arrays, so we are always working on new memory locations
        cur_offset += size;

        ++no_ops;
		if (no_ops % LOG_COUNT == 0)
			printf("completed %d ops\n", no_ops);

        clock_gettime(CLOCK_BOOTTIME, &end_time);   

        // calculate how much time has elapsed in selecting and performing the transaction
		elapsed = TS_NS(start_time, end_time);
        cur_total -= window[cur_pos];
        cur_total += elapsed;
        window[cur_pos] = elapsed;
        cur_pos = (cur_pos+1)%100;

        //if (cur_total > 40008730)  //(no_ops % LOG_COUNT == 0)
        //    printf(" %d %d %d %d %d\n",mem_op, size, cur_total, interval_ns, elapsed);

        // Check to make sure we are able to keep up. Because there are outliers, the check is performed over a 100 transaction window. window_interval_ns is the desired interval*100 
        // and cur_total is the sum of the last 100 times
        if (unlikely(cur_total > window_interval_ns)) {
            printf("Not keeping up!! %d %d %d %d %d\n",mem_op, size, cur_total, window_interval_ns, elapsed);
            exit(0);
        }

        // if our elapsed time is less than the interval, sleep 
        if (elapsed < interval_ns) {
            //printf("sleep %d\n",(p->interval_ns-elapsed)/1000);
            usleep((interval_ns-elapsed)/1000);
        }
	}

    clock_gettime(CLOCK_BOOTTIME, &overall_end_time);
    elapsed = TS_NS(overall_start_time, overall_end_time);
    printf("elapsed usec %d usec/iteration %d\n", elapsed/1000, elapsed/1000/p->max_iters);

	free(src_addr);
	free(dest_addr);

	return 0;
}

int main(int argc, char **argv)
{
	struct parms p;
 	thrd_t threads[MAX_THREADS];

    struct mem_op_entry entries[120];
    double cum_probs[120];

    p.cum_probs = cum_probs;
    p.entries = entries;

    char line[100];
    char *sp;
    int sizes[40];   //array of transaction sizes
    double cpy_probs[40];  // array of probablilities for copy 
    double set_probs[40];  // probabilities for set
    double mov_probs[40];  // probabilities for move
    int count = 0;
    double cur_prob = 0.0;  // running total of cumulative probabilities
    int cur_entry = 0;

    FILE *fp;

	if (argc != 5) {
		printf("Usage: dto-test-distribution num_threads max_iters interval_us input_file_path\n");
		exit(0);
	}

	else{
		p.num_threads = atoi(argv[1]);
		p.max_iters = atoi(argv[2]);
        p.interval_ns = atoi(argv[3])*1000;
		if(p.num_threads > MAX_THREADS) {
			printf("number of threads must be <= %d\n",MAX_THREADS);
			exit(0);
		}

 
        fp = fopen(argv[4],"r");
    
        if (fp == NULL)
        {
            printf("Can not open file");
            exit(0);
        }
    
        // read in the data from the csv file
        while(fgets(line, 100, fp )!= NULL)
        {
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
            if (count > 40) {
                printf("File contains too many lines. We assume that there are no more than 40 rows\n");
                exit(0);
            }
        }

        fclose(fp);

        // create one array of all memory transactions. build up an array of cumulative probability, so we can select from the array using a random number between 0 and 1
        for (int i=0; i<count; ++i){
            if (set_probs[i] > 0){
                entries[cur_entry].op = MEMSET;
                entries[cur_entry].size = sizes[i] + 2048;
                
                cur_prob += set_probs[i];
                cum_probs[cur_entry] = cur_prob;

                ++cur_entry;
            }

            if (cpy_probs[i] > 0){
                entries[cur_entry].op = MEMCOPY;
                entries[cur_entry].size = sizes[i] + 2048;
                
                cur_prob += cpy_probs[i];
                cum_probs[cur_entry] = cur_prob;

                ++cur_entry;
            }

            if (mov_probs[i] > 0){
                entries[cur_entry].op = MEMMOVE;
                entries[cur_entry].size = sizes[i] + 2048;
                
                cur_prob += mov_probs[i];
                cum_probs[cur_entry] = cur_prob;

                ++cur_entry;
            }
        }

        p.num_entries = cur_entry;
		
	}

	//printf("buf_size %lu iters %lu threads %u\n",p.buf_size, p.max_iters, p.num_threads);

	for(int t = 0; t < p.num_threads; ++t)
		thrd_create(&threads[t], thread_func, &p);

	for(int t = 0; t < p.num_threads; ++t)
		thrd_join(threads[t], NULL);
	
	//printf("all threads completed execution\n");
	return 0;
}

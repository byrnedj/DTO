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


static atomic_ullong cycles = 0;


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
    float latency;
	float bw;
	uint64_t cycles_per_sec;
    uint32_t i;
    uint64_t start;
    struct timespec st, et;
    uint64_t st2,ed2;

	for (i=0;i<1000000;++i) {

        start = rdtsc();

        clock_gettime(CLOCK_BOOTTIME, &st);
        uint64_t t;							
        clock_gettime(CLOCK_BOOTTIME, &et);				
        t = (((et.tv_sec*1000000000) + et.tv_nsec) -			
                ((st.tv_sec*1000000000) + st.tv_nsec));	

        //st2=rdtsc(); 
        //ed2=rdtsc();
        //t=ed2-st2;


		cycles += (rdtsc() - start);


    }

    return 0;
}
        

int main(int argc, char **argv)
{
    thrd_t thread;
    thrd_create(&thread, thread_func, NULL);

    thrd_join(thread, NULL);


	float secs;
	float latency = 1.0 * cycles / 1000000;
    
	uint64_t cycles_per_sec;
	calibrate(&cycles_per_sec);

    float latency_ns = (latency * 1E9)/cycles_per_sec;
	secs = (float)cycles/cycles_per_sec;
	
	printf("Latency %f ns  cycles per second %llu \n",latency_ns, cycles_per_sec);
	
	return 0;
}

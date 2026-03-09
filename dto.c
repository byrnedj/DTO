/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cpuid.h>
#include <linux/idxd.h>
#include <x86intrin.h>
#include <sched.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <dlfcn.h>
#include <accel-config/libaccel_config.h>
#include <numaif.h>
#include <numa.h>
#include <signal.h>

#define likely(x)       __builtin_expect((x), 1)
#define unlikely(x)     __builtin_expect((x), 0)

// DSA capabilities
#define GENCAP_CC_MEMORY  0x4

#define UMWAIT_DELAY_DEFAULT 100000 //cycles until umwait timeout

#define C01_STATE 1
#define C02_STATE 0
#define TPAUSE_DELAY 1000

#define USE_ORIG_FUNC(n, use_dsa) (use_std_lib_calls == 1 || !use_dsa || n < dsa_min_size)
#define TS_NS(s, e) (((e.tv_sec*1000000000) + e.tv_nsec) - ((s.tv_sec*1000000000) + s.tv_nsec))

/* Maximum WQs that DTO will use. It is rather an arbitrary limit
 * to keep things simple and avoid having to dynamically allocate memory.
 * Allocating memory dynamically may create cyclic dependency and may cause
 * a hang (e.g., memset --> malloc --> alloc library calls memset --> memset)
 */
#define MAX_WQS 32
#define MAX_NUMA_NODES 32
#define DTO_DEFAULT_MIN_SIZE 65536
#define DTO_INITIALIZED 0
#define DTO_INITIALIZING 1


#define NSEC_PER_SEC (1000000000)
#define MSEC_PER_SEC (1000)
#define NSEC_PER_MSEC (NSEC_PER_SEC/MSEC_PER_SEC)

// thread specific variables
static __thread struct dsa_hw_desc thr_desc;
static __thread struct dsa_completion_record thr_comp __attribute__((aligned(32)));
static __thread uint64_t thr_bytes_completed;
static __thread size_t thr_cpu_fraction_bytes;
static __thread uint64_t thr_cpu_fraction_ns;
static __thread uint64_t thr_submit_ns;
static __thread uint64_t thr_poll_ns;
static __thread uint64_t tl_num_descs;
static __thread uint64_t tl_next_sample;

// original std memory functions
static void * (*orig_memset)(void *s, int c, size_t n);
static void * (*orig_memcpy)(void *dest, const void *src, size_t n);
static void * (*orig_memmove)(void *dest, const void *src, size_t n);
static int (*orig_memcmp)(const void *s1, const void *s2, size_t n);

struct dto_wq {
	struct accfg_wq *acc_wq;
	char wq_path[PATH_MAX];
	uint64_t dsa_gencap;
	int wq_size;
	uint32_t max_transfer_size;
	int wq_fd;
	void *wq_portal;
	bool wq_mmapped;
};

struct dto_device {
	struct dto_wq* wqs[MAX_WQS];
	uint8_t num_wqs;
	atomic_uchar next_wq;
};

enum wait_options {
	WAIT_BUSYPOLL = 0,
	WAIT_UMWAIT,
	WAIT_YIELD,
	WAIT_TPAUSE
};

enum numa_aware {
	NA_NONE = 0,
	NA_BUFFER_CENTRIC,
	NA_CPU_CENTRIC,
	NA_LAST_ENTRY
};

enum overlapping_memmove_actions {
	OVERLAPPING_CPU = 0,
	OVERLAPPING_DSA,
	OVERLAPPING_LAST_ENTRY
};

static const char * const numa_aware_names[] = {
	[NA_NONE] = "none",
	[NA_BUFFER_CENTRIC] = "buffer-centric",
	[NA_CPU_CENTRIC] = "cpu-centric"
};

// global workqueue variables
static struct dto_wq wqs[MAX_WQS];
static struct dto_device* devices[MAX_NUMA_NODES];
static uint8_t num_wqs;
static atomic_uchar next_wq;
static atomic_uchar dto_initialized;
static atomic_uchar dto_initializing;
static uint8_t use_std_lib_calls;
static enum numa_aware is_numa_aware;
static size_t dsa_min_size = DTO_DEFAULT_MIN_SIZE;
static int wait_method = WAIT_BUSYPOLL;
static size_t cpu_size_fraction;   // range of values is 0 to 99

static uint8_t dto_dsa_memcpy = 1;
static uint8_t dto_dsa_memmove = 1;
static uint8_t dto_dsa_memset = 1;
static uint8_t dto_dsa_memcmp = 1;

static uint8_t dto_dsa_cc = 1;
static bool dto_use_c02 = true; //C02 state is default -
                            //C02 avg exit latency is ~500 ns
                            //and C01 is about ~240 ns on SPR

#define TPAUSE_C02_DELAY_NS 6000 //in this case we are offloading so delay can
                                 //be ~6 us as this is around the time a > 64KB 
                                 //copy takes to complete

#define TPAUSE_C01_DELAY_NS 1000 //keep smaller because we want to wake up
                                 //with lower latency

static uint64_t tpause_wait_time = TPAUSE_C02_DELAY_NS;

static unsigned long dto_umwait_delay = UMWAIT_DELAY_DEFAULT;

static uint8_t dto_overlapping_memmove_action = OVERLAPPING_CPU;

static uint8_t fork_handler_registered;

static uint8_t dto_profiling;
#define PROFILING_SAMPLE_INTERVAL_DEFAULT 100
static unsigned int profiling_sample_interval = PROFILING_SAMPLE_INTERVAL_DEFAULT;

/* Minimum samples needed per bucket before profiling-driven knobs are applied */
#define PROFILING_MIN_SAMPLES 10
/* How often (in profiling samples) to recompute knobs */
#define PROFILING_KNOB_RECOMPUTE_INTERVAL 200
static __thread uint64_t tl_profiling_knob_counter;

enum memop {
	MEMSET = 0x0,
	MEMCOPY,
	MEMMOVE,
	MEMCMP,
	MAX_MEMOP,
};

static const char * const memop_names[] = {
	[MEMSET] = "set",
	[MEMCOPY] = "cpy",
	[MEMMOVE] = "mov",
	[MEMCMP] = "cmp"
};

// memory stats
#define HIST_BUCKET_SIZE 4096
#define HIST_NO_BUCKETS 512
enum stat_group {
	STDC_CALL_MIN_SIZE = 0x0,
	STDC_CALL_SAMPLED,
	STDC_CALL_DSA_FAILED,
	STDC_CALL_CPU_FRACTION,
	DSA_CALL_SUCCESS,
	DSA_CALL_FAILED,
	DSA_FAIL_CODES,
	MAX_STAT_GROUP
};

static const char * const stat_group_names[] = {
	[STDC_CALL_MIN_SIZE] = "cpu (min_sz)",
	[STDC_CALL_SAMPLED] = "cpu (sampled)",
	[STDC_CALL_DSA_FAILED] = "cpu (dsa_fail)",
	[STDC_CALL_CPU_FRACTION] = "cpu (fraction)",
	[DSA_CALL_SUCCESS] = "dsa (success)",
	[DSA_CALL_FAILED] = "dsa (failed)",
	[DSA_FAIL_CODES] = "failure reason"
};

enum return_code {
	SUCCESS = 0x0,
	RETRY,
	PAGE_FAULT,
	FAIL_OTHERS,
	MAX_FAILURES,
};

static const char * const failure_names[] = {
	[SUCCESS] = "Success",
	[RETRY] = "Retry",
	[PAGE_FAULT] = "PFs",
	[FAIL_OTHERS] = "Others",
};

static const char * const wait_names[] = {
	[WAIT_BUSYPOLL] = "busypoll",
	[WAIT_UMWAIT] = "umwait",
	[WAIT_YIELD] = "yield",
        [WAIT_TPAUSE] = "tpause"
};

static int collect_stats;
static char dto_log_path[PATH_MAX];
static int log_fd = -1;
static int stats_fd = -1;

#ifdef DTO_STATS_SUPPORT
static struct timespec dto_start_time;

#define DTO_COLLECT_STATS_START(cs, st)				\
	do {							\
		if (unlikely(cs)) {				\
			clock_gettime(CLOCK_BOOTTIME, &st);	\
		}						\
	} while (0)						\


#define DTO_COLLECT_STATS_DSA_END(cs, st, et, op, n, overlap, tbc, r)				\
	do {										\
		if (unlikely(cs)) {							\
			uint64_t t;							\
			clock_gettime(CLOCK_BOOTTIME, &et);				\
			t = (((et.tv_sec*1000000000) + et.tv_nsec) -			\
					((st.tv_sec*1000000000) + st.tv_nsec));		\
			if (unlikely(r != SUCCESS))					\
				update_stats(op, n, overlap, tbc, t, DSA_CALL_FAILED, r);	\
			else								\
				update_stats(op, n, overlap, tbc, t, DSA_CALL_SUCCESS, 0);	\
		}									\
	} while (0)									\

#define DTO_COLLECT_STATS_CPU_END(cs, st, et, op, n, orig_n, grp)		\
	do {									\
		if (unlikely(cs)) {						\
			uint64_t t;						\
			clock_gettime(CLOCK_BOOTTIME, &et);			\
			t = (((et.tv_sec*1000000000) + et.tv_nsec) -		\
				((st.tv_sec*1000000000) + st.tv_nsec));		\
			update_stats(op, orig_n, false, n, t, grp, 0);			\
		}								\
	} while (0)								\

#define DTO_COLLECT_STATS_SAMPLED_CPU_END(cs, st, et, op, n)			\
	do {									\
		if (unlikely(cs)) {						\
			uint64_t t;						\
			clock_gettime(CLOCK_BOOTTIME, &et);			\
			t = (((et.tv_sec*1000000000) + et.tv_nsec) -		\
				((st.tv_sec*1000000000) + st.tv_nsec));		\
			update_stats(op, n, false, n, t, STDC_CALL_SAMPLED, 0);	\
		}								\
	} while (0)								\

static atomic_int op_counter[HIST_NO_BUCKETS][MAX_STAT_GROUP][MAX_MEMOP];
static atomic_ullong bytes_counter[HIST_NO_BUCKETS][MAX_STAT_GROUP];
static atomic_ullong lat_counter[HIST_NO_BUCKETS][MAX_STAT_GROUP][MAX_MEMOP];
static atomic_ullong submit_lat_counter[HIST_NO_BUCKETS][MAX_MEMOP];
static atomic_ullong poll_lat_counter[HIST_NO_BUCKETS][MAX_MEMOP];
static atomic_int fail_counter[HIST_NO_BUCKETS][MAX_FAILURES];
#endif

/* call initialize/cleanup functions when library is loaded/unloaded */
static int init_dto(void) __attribute__((constructor));
static void cleanup_dto(void) __attribute__((destructor));

static int waitpkg_support;

static enum {
	LOG_LEVEL_FATAL,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_TRACE
} log_levels;

#define LOG_FATAL(...) dto_log(LOG_LEVEL_FATAL, __VA_ARGS__)
#define LOG_ERROR(...) dto_log(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_TRACE(...) dto_log(LOG_LEVEL_TRACE, __VA_ARGS__)

static unsigned int log_level = LOG_LEVEL_FATAL;

static void dto_stats_log(const char *fmt, ...)
{
        char buf[512];
        va_list args;

        if (stats_fd == -1 && LOG_LEVEL_TRACE > log_level)
                return;

        va_start(args, fmt);
        if (stats_fd != -1) {
                if (vsnprintf(buf, sizeof(buf), fmt, args) > 0)
                        write(stats_fd, buf, strlen(buf));
        } else if (log_fd == -1) {
                vprintf(fmt, args);
        } else {
                vsnprintf(buf, sizeof(buf), fmt, args);
                write(log_fd, buf, strlen(buf));
        }
        va_end(args);
}

#ifdef DTO_STATS_SUPPORT
static void print_stats(void);
static struct sigaction previous_sigint_action;
static volatile sig_atomic_t sigint_handler_installed;
static int previous_sigint_action_valid;

static void restore_sigint_handler(void)
{
        if (!sigint_handler_installed)
                return;

        if (previous_sigint_action_valid)
                sigaction(SIGINT, &previous_sigint_action, NULL);
        else {
                struct sigaction default_action = {0};

                default_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &default_action, NULL);
        }

        sigint_handler_installed = 0;
}

static void dto_handle_sigint(int signo)
{
        if (collect_stats)
                print_stats();

        restore_sigint_handler();

        if (previous_sigint_action_valid && previous_sigint_action.sa_handler == SIG_IGN)
                return;

        raise(signo);
}

static void install_sigint_handler(void)
{
        struct sigaction action = {0};

        if (sigint_handler_installed)
                return;

        action.sa_handler = dto_handle_sigint;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;

        if (sigaction(SIGINT, NULL, &previous_sigint_action) == 0)
                previous_sigint_action_valid = 1;
        else
                previous_sigint_action_valid = 0;

        if (sigaction(SIGINT, &action, NULL) == 0)
                sigint_handler_installed = 1;
}
#endif

/* Auto tune heuristics magic numbers */
#define DESCS_PER_RUN 0xF0
#define NUM_DESCS 16
#define MIN_AVG_YIELD_WAITS 1.0
#define MAX_AVG_YIELD_WAITS 2.0
#define MIN_AVG_POLL_WAITS 5.0
#define MAX_AVG_POLL_WAITS 20.0
#define MAX_CPU_SIZE_FRACTION 90  // specified in percent (e.g., 90 is 0.90)
#define CSF_STEP_INCREMENT 1
#define CSF_STEP_DECREMENT 1
#define MAX_DSA_MIN_SIZE 65536
#define MIN_DSA_MIN_SIZE 6144
#define DMS_STEP_INCREMENT 1024
#define DMS_STEP_DECREMENT 1024

/* Auto tuning variables */
static atomic_ullong num_descs;
static atomic_ullong adjust_num_descs;
static atomic_ullong adjust_num_waits;
/* default waits are for yield because yield is default waiting method */
static double min_avg_waits = MIN_AVG_YIELD_WAITS;
static double max_avg_waits = MAX_AVG_YIELD_WAITS;
static uint8_t auto_adjust_knobs = 1;

extern char *__progname;

static void dto_log(int req_log_level, const char *fmt, ...)
{
	char buf[512];
	va_list args;

	if (req_log_level > log_level)
		return;

	va_start(args, fmt);
	if (log_fd == -1)
		vprintf(fmt, args);
	else {
		vsnprintf(buf, sizeof(buf), fmt, args);
		write(log_fd, buf, strlen(buf));
	}
	va_end(args);
}

/* Reinitialize DTO in the child process. */
static void child (void)
{
#ifdef DTO_STATS_SUPPORT
	int i, j, k;

	/* Reset the counters */
	for (i = 0; i < HIST_NO_BUCKETS; i++) {
		for (j = 0; j < MAX_STAT_GROUP; j++) {
			for (k = 0; k < MAX_MEMOP; k++) {
				op_counter[i][j][k] = 0;
				lat_counter[i][j][k] = 0;
			}
			bytes_counter[i][j] = 0;
		}
		for (j = 0; j < MAX_MEMOP; j++) {
			submit_lat_counter[i][j] = 0;
			poll_lat_counter[i][j] = 0;
		}
		for (j = 0; j < MAX_FAILURES; j++)
			fail_counter[i][j] = 0;
	}
#endif
	dto_initializing = 0;
	dto_initialized = 0;
	log_fd = -1;

	init_dto();
}

static __always_inline unsigned char enqcmd(struct dsa_hw_desc *desc, volatile void *reg)
{
	unsigned char retry;

	asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
			"setz %0\t\n"
			: "=r"(retry) : "a" (reg), "d" (desc));
	return retry;
}

static __always_inline void movdir64b(struct dsa_hw_desc *desc, volatile void *reg)
{
	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02\t\n"
		: : "a" (reg), "d" (desc));
}

static __always_inline void dsa_wait_yield(const volatile uint8_t *comp)
{
	while (*comp == 0) {
	    sched_yield();
	}
}

static __always_inline void dsa_wait_busy_poll(const volatile uint8_t *comp)
{
	while (*comp == 0) {
	    _mm_pause();
	}
}

static __always_inline void dsa_wait_tpause(const volatile uint8_t *comp)
{
	while (*comp == 0) {
            uint64_t delay = _rdtsc() + tpause_wait_time;
            _tpause(C02_STATE, delay);
        }
}

static __always_inline void __dsa_wait_umwait(const volatile uint8_t *comp)
{
	_umonitor((void*)comp);

        uint64_t delay = _rdtsc() + UMWAIT_DELAY_DEFAULT;
	_umwait(C02_STATE, delay);
}

static __always_inline void dsa_wait_umwait(const volatile uint8_t *comp)
{
	while (*comp == 0) {
	    __dsa_wait_umwait(comp);
        }
}

static __always_inline void __dsa_wait(const volatile uint8_t *comp)
{
        switch(wait_method) {
            case WAIT_YIELD:
		sched_yield();
                break;
            case WAIT_UMWAIT:
                __dsa_wait_umwait(comp);
                break;
            case WAIT_TPAUSE:
                _tpause( C01_STATE, _rdtsc() + TPAUSE_C01_DELAY_NS); 
                break;
            default:
                 _mm_pause();
        }
}

static __always_inline void dsa_wait_no_adjust(const volatile uint8_t *comp)
{
    switch (wait_method) {
        case WAIT_YIELD:
            dsa_wait_yield(comp);
            break;
        case WAIT_UMWAIT:
            dsa_wait_umwait(comp);
            break;
        case WAIT_BUSYPOLL:
            dsa_wait_busy_poll(comp);
            break;
        case WAIT_TPAUSE:
            dsa_wait_tpause(comp);
            break;
        default:
            dsa_wait_busy_poll(comp);
    }
}

/* A simple auto-tuning heuristic.
 * Goal of the Heuristic:
 *   - CPU and DSA should complete their fraction of the job roughly simultaneously.
 *     This minimizes the thread's wait time for DSA while maximizing DSA utilization
 *  Approximating the goal:
 *   - Threads waiting for DSA completion (e.g., by yielding), keep avg. no. of
 *     waits (i.e., yields) between min_avg_waits and max_avg_waits
 *     (see local_num_waits variable below)
 * Heuristic
 *   1) Sample number of waits (local_num_waits) for NUM_DESCS descriptors
 *   every DESCS_PER_RUN descriptors
 *   2) If the avg. number of waits per descriptor > max_avg_waits, decrease load on DSA
 *      - If cpu_size_fraction not too high, increase it by CSF_STEP_INCREMENT
 *      - else if dsa_min_size not too high, increase it by DMS_STEP_INCREMENT
 *   3) If the avg. number of waits per descriptor < min_avg_waits, increase load on DSA
 *      - If cpu_size_fraction not too low, decrease it by CSF_STEP_DECREMENT
 *      - else if dsa_min_size not too low, decrease it by DMS_STEP_DECREMENT
 */
static __always_inline void dsa_wait_and_adjust(const volatile uint8_t *comp)
{
	uint64_t local_num_waits = 0;

	if ((++num_descs & DESCS_PER_RUN) != DESCS_PER_RUN) {
		while (*comp == 0) {
			__dsa_wait(comp);
                }

		return;
	}

	/* Run the heuristics as well as wait for DSA */
	while (*comp == 0) {
		__dsa_wait(comp);
		local_num_waits++;
	}

	// operations that have failed (mostly due to page fault) return very quickly and cause the algorithm
	// to think that the DSA operation was faster than it really was. We exclude them from the calculation.
	if (*comp != DSA_COMP_SUCCESS) {
		return;
	}

	adjust_num_descs++;
	adjust_num_waits += local_num_waits;

	if (adjust_num_descs >= NUM_DESCS) {
		unsigned long long temp = adjust_num_descs;

		if (temp && atomic_compare_exchange_strong(&adjust_num_descs, &temp, 0)) {
			double avg_num_waits = (double)adjust_num_waits / temp;

			adjust_num_waits = 0;
			if (avg_num_waits > max_avg_waits) {
				if (cpu_size_fraction < MAX_CPU_SIZE_FRACTION)
					cpu_size_fraction += CSF_STEP_INCREMENT;
				else if (dsa_min_size < MAX_DSA_MIN_SIZE)
					dsa_min_size += DMS_STEP_INCREMENT;
			} else if (avg_num_waits < min_avg_waits) {
				if (cpu_size_fraction >= CSF_STEP_DECREMENT)
					cpu_size_fraction -= CSF_STEP_DECREMENT;
				else if (dsa_min_size > MIN_DSA_MIN_SIZE)
					dsa_min_size -= DMS_STEP_DECREMENT;
			}
		}
	}
}

static __always_inline int dsa_wait(struct dto_wq *wq,
	struct dsa_hw_desc *hw, volatile uint8_t *comp)
{
#ifdef DTO_STATS_SUPPORT
	struct timespec _pst, _pet;
	if (unlikely(collect_stats))
		clock_gettime(CLOCK_BOOTTIME, &_pst);
#endif

	if (auto_adjust_knobs)
		dsa_wait_and_adjust(comp);
	else
		dsa_wait_no_adjust(comp);

#ifdef DTO_STATS_SUPPORT
	if (unlikely(collect_stats)) {
		clock_gettime(CLOCK_BOOTTIME, &_pet);
		thr_poll_ns += TS_NS(_pst, _pet);
	}
#endif

	if (likely(*comp == DSA_COMP_SUCCESS)) {
		thr_bytes_completed += hw->xfer_size;
		return SUCCESS;
	} else if ((*comp & DSA_COMP_STATUS_MASK) == DSA_COMP_PAGE_FAULT_NOBOF) {
		thr_bytes_completed += thr_comp.bytes_completed;
		return PAGE_FAULT;
	}
	LOG_ERROR("failed status %x xfersz %x\n", *comp, hw->xfer_size);
	return FAIL_OTHERS;
}

static __always_inline int dsa_submit(struct dto_wq *wq,
	struct dsa_hw_desc *hw)
{
	int ret;
	//LOG_TRACE("desc flags: 0x%x, opcode: 0x%x\n", hw->flags, hw->opcode);
	__builtin_ia32_sfence();

#ifdef DTO_STATS_SUPPORT
	struct timespec _sst, _set;
	if (unlikely(collect_stats))
		clock_gettime(CLOCK_BOOTTIME, &_sst);
#endif

	if (wq->wq_mmapped) {
		ret = enqcmd(hw, wq->wq_portal);
		if (!ret) {
#ifdef DTO_STATS_SUPPORT
			if (unlikely(collect_stats)) {
				clock_gettime(CLOCK_BOOTTIME, &_set);
				thr_submit_ns += TS_NS(_sst, _set);
			}
#endif
			return SUCCESS;
		}
	} else {
		ret = write(wq->wq_fd, hw, sizeof(*hw));
#ifdef DTO_STATS_SUPPORT
		if (unlikely(collect_stats)) {
			clock_gettime(CLOCK_BOOTTIME, &_set);
			thr_submit_ns += TS_NS(_sst, _set);
		}
#endif
		if (ret == sizeof(*hw))
			return SUCCESS;
		else
			return FAIL_OTHERS;
	}
	return RETRY;
}

static __always_inline int dsa_execute(struct dto_wq *wq,
	struct dsa_hw_desc *hw, volatile uint8_t *comp)
{
	int ret;
	*comp = 0;
	//LOG_TRACE("desc flags: 0x%x, opcode: 0x%x\n", hw->flags, hw->opcode);
	__builtin_ia32_sfence();

#ifdef DTO_STATS_SUPPORT
	struct timespec _sst, _set;
	if (unlikely(collect_stats))
		clock_gettime(CLOCK_BOOTTIME, &_sst);
#endif

	if (wq->wq_mmapped)
		ret = enqcmd(hw, wq->wq_portal);

	else {
		ret = write(wq->wq_fd, hw, sizeof(*hw));
		if (ret != sizeof(*hw))
			return FAIL_OTHERS;
		else
			ret = 0;
	}
	if (!ret) {
#ifdef DTO_STATS_SUPPORT
		struct timespec _pst, _pet;
		if (unlikely(collect_stats)) {
			clock_gettime(CLOCK_BOOTTIME, &_set);
			thr_submit_ns += TS_NS(_sst, _set);
			clock_gettime(CLOCK_BOOTTIME, &_pst);
		}
#endif
		dsa_wait_no_adjust(comp);

#ifdef DTO_STATS_SUPPORT
		if (unlikely(collect_stats)) {
			clock_gettime(CLOCK_BOOTTIME, &_pet);
			thr_poll_ns += TS_NS(_pst, _pet);
		}
#endif

		if (*comp == DSA_COMP_SUCCESS) {
			thr_bytes_completed += hw->xfer_size;
			return SUCCESS;
		} else if ((*comp & DSA_COMP_STATUS_MASK) == DSA_COMP_PAGE_FAULT_NOBOF) {
			thr_bytes_completed += thr_comp.bytes_completed;
			return PAGE_FAULT;
		}
		LOG_ERROR("failed status %x xfersz %x\n", *comp, hw->xfer_size);
		return FAIL_OTHERS;
	}
	return RETRY;
}

#ifdef DTO_STATS_SUPPORT
static void update_stats(int op, size_t n, bool overlapping, size_t bytes_completed,
		uint64_t elapsed_ns, int group, int error_code)
{
	// dto_memcpymove didn't actually submit the request to DSA, so there is nothing to log. This will be captured by a second call
	if (op == MEMMOVE && overlapping && dto_overlapping_memmove_action == OVERLAPPING_CPU && group == DSA_CALL_SUCCESS) {
		return;
	}

	int bucket = (n / HIST_BUCKET_SIZE);

	if (bucket >= HIST_NO_BUCKETS)  /* last bucket includes remaining sizes */
		bucket = HIST_NO_BUCKETS-1;
	++op_counter[bucket][group][op];
	bytes_counter[bucket][group] += bytes_completed;
	lat_counter[bucket][group][op] += elapsed_ns;
	if (group == DSA_CALL_FAILED)
		++fail_counter[bucket][error_code];

}

static void update_submit_poll_stats(int op, size_t n, uint64_t submit_ns, uint64_t poll_ns)
{
	int bucket = (n / HIST_BUCKET_SIZE);

	if (bucket >= HIST_NO_BUCKETS)
		bucket = HIST_NO_BUCKETS-1;
	submit_lat_counter[bucket][op] += submit_ns;
	poll_lat_counter[bucket][op] += poll_ns;
}

static void print_stats(void)
{
	struct timespec dto_end_time;

	if (likely(!collect_stats))
		return;

	clock_gettime(CLOCK_BOOTTIME, &dto_end_time);

	dto_stats_log("DTO Run Time: %ld ms\n", TS_NS(dto_start_time, dto_end_time)/1000000);
	dto_stats_log("DTO CPU Size Fraction: %.2f\n", cpu_size_fraction / 100.0);

	// display stats
	for (int t = 0; t < 2; ++t) {
		if (t == 0)
			dto_stats_log("\n******** Number of Memory Operations ********\n");
		else
			dto_stats_log("\n******** Average Memory Operation Latency (us)  ********\n");

		dto_stats_log("%17s    ", "");
		for (int g = 0; g < MAX_STAT_GROUP; ++g) {
			if (t == 0) {
				if (g == DSA_FAIL_CODES)
					dto_stats_log("<***** %-13s *****> ", stat_group_names[g]);
				else
					dto_stats_log("<*************** %-13s ***************> ", stat_group_names[g]);
			} else {
				if (g != DSA_FAIL_CODES)
					dto_stats_log("<******** %-13s ********> ", stat_group_names[g]);

			}
		}
		dto_stats_log("\n");

		dto_stats_log("%-17s -- ", "Byte Range");
		for (int g = 0; g < MAX_STAT_GROUP - 1; ++g) {
			for (int o = 0; o < MAX_MEMOP; ++o)
				dto_stats_log("%-8s ", memop_names[o]);
			if (t == 0)
				dto_stats_log("%-12s ", "bytes");
		}
		if (t == 0)
			for (int o = 1; o < MAX_FAILURES; ++o)
				dto_stats_log("%-6s ", failure_names[o]);
		dto_stats_log("\n");

		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			bool empty = true;

			for (int g = 0; g < MAX_STAT_GROUP; ++g) {
				for (int o = 0; o < MAX_MEMOP; ++o) {
					if (op_counter[b][g][o] != 0) {
						empty = false;
						break;
					}
				}
				if (!empty)
					break;
			}
			if (empty)
				continue;

			if (b < (HIST_NO_BUCKETS-1))
				dto_stats_log("% 8d-%-8d -- ", b*4096, ((b+1)*4096)-1);
			else
				dto_stats_log("   >=%-12d -- ", b*4096);

			for (int g = 0; g < MAX_STAT_GROUP - 1; ++g) {
				for (int o = 0; o < MAX_MEMOP; ++o) {
					if (t == 0) {
						dto_stats_log("%-8d ", op_counter[b][g][o]);
						continue;
					}
					if (op_counter[b][g][o] != 0) {
						double avg_us = ((double) lat_counter[b][g][o])/(((double) op_counter[b][g][o]) * 1000.0);

						dto_stats_log("%-8.2f ", avg_us);
					} else {
						dto_stats_log("%-8d ", 0);
					}
				}
				if (t == 0)
					dto_stats_log("%-12lld ", bytes_counter[b][g]);
			}
			if (t == 0)
				for (int o = 1; o < MAX_FAILURES; ++o)
					dto_stats_log("%-6d ", fail_counter[b][o]);
			dto_stats_log("\n");
		}
	}

	/* Submit and Poll latency breakdown for DSA operations */
	LOG_TRACE("\n******** Average DSA Submit / Poll Latency (us)  ********\n");
	LOG_TRACE("%-17s -- ", "Byte Range");
	for (int o = 0; o < MAX_MEMOP; ++o)
		LOG_TRACE("%-10s ", memop_names[o]);
	LOG_TRACE("   ");
	for (int o = 0; o < MAX_MEMOP; ++o)
		LOG_TRACE("%-10s ", memop_names[o]);
	LOG_TRACE("\n");

	LOG_TRACE("%17s    ", "");
	for (int o = 0; o < MAX_MEMOP; ++o)
		LOG_TRACE("%-10s ", "submit");
	LOG_TRACE("   ");
	for (int o = 0; o < MAX_MEMOP; ++o)
		LOG_TRACE("%-10s ", "poll");
	LOG_TRACE("\n");

	for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
		bool empty = true;

		for (int o = 0; o < MAX_MEMOP; ++o) {
			if (op_counter[b][DSA_CALL_SUCCESS][o] != 0) {
				empty = false;
				break;
			}
		}
		if (empty)
			continue;

		if (b < (HIST_NO_BUCKETS-1))
			LOG_TRACE("% 8d-%-8d -- ", b*4096, ((b+1)*4096)-1);
		else
			LOG_TRACE("   >=%-12d -- ", b*4096);

		for (int o = 0; o < MAX_MEMOP; ++o) {
			int count = op_counter[b][DSA_CALL_SUCCESS][o];
			if (count > 0) {
				double avg_us = ((double)submit_lat_counter[b][o]) / ((double)count * 1000.0);
				LOG_TRACE("%-10.2f ", avg_us);
			} else {
				LOG_TRACE("%-10d ", 0);
			}
		}
		LOG_TRACE("   ");
		for (int o = 0; o < MAX_MEMOP; ++o) {
			int count = op_counter[b][DSA_CALL_SUCCESS][o];
			if (count > 0) {
				double avg_us = ((double)poll_lat_counter[b][o]) / ((double)count * 1000.0);
				LOG_TRACE("%-10.2f ", avg_us);
			} else {
				LOG_TRACE("%-10d ", 0);
			}
		}
		LOG_TRACE("\n");
	}
}

static void analyze_profiling_stats(void)
{
	if (!dto_profiling)
		return;

	double total_cpu_time_ns = 0;
	double total_dsa_time_ns = 0;
	double total_ops_analyzed = 0;

	LOG_TRACE("\n======== Profiling Analysis ========\n");

	for (int o = 0; o < MAX_MEMOP; ++o) {
		bool has_data = false;
		int crossover_bucket = -1;
		double op_cpu_time_ns = 0;
		double op_dsa_time_ns = 0;
		double op_total_ops = 0;

		/* Check if there is any sampled or DSA data for this op */
		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			if (op_counter[b][STDC_CALL_SAMPLED][o] > 0 ||
			    op_counter[b][DSA_CALL_SUCCESS][o] > 0) {
				has_data = true;
				break;
			}
		}

		if (!has_data)
			continue;

		LOG_TRACE("\n--- %s ---\n", memop_names[o]);
		LOG_TRACE("%-17s    %-12s %-12s %-10s %-8s\n",
			"Byte Range", "CPU (us)", "DSA (us)", "Winner", "Ops");

		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			int sampled_count = op_counter[b][STDC_CALL_SAMPLED][o];
			int dsa_count = op_counter[b][DSA_CALL_SUCCESS][o];

			if (sampled_count == 0 && dsa_count == 0)
				continue;

			double cpu_avg_us = 0;
			double dsa_avg_us = 0;
			bool have_cpu = sampled_count > 0;
			bool have_dsa = dsa_count > 0;

			if (have_cpu)
				cpu_avg_us = ((double)lat_counter[b][STDC_CALL_SAMPLED][o]) /
					     ((double)sampled_count * 1000.0);
			if (have_dsa)
				dsa_avg_us = ((double)lat_counter[b][DSA_CALL_SUCCESS][o]) /
					     ((double)dsa_count * 1000.0);

			/* Total DSA-eligible ops in this bucket for this op:
			 * sampled + DSA success + DSA failed
			 */
			int total_ops = sampled_count + dsa_count +
					op_counter[b][DSA_CALL_FAILED][o];

			const char *winner;
			if (have_cpu && have_dsa) {
				winner = (dsa_avg_us < cpu_avg_us) ? "DSA" : "CPU";
				if (crossover_bucket < 0 && dsa_avg_us < cpu_avg_us)
					crossover_bucket = b;
			} else {
				winner = "N/A";
			}

			if (b < (HIST_NO_BUCKETS - 1))
				LOG_TRACE("% 8d-%-8d    ", b * HIST_BUCKET_SIZE,
					((b + 1) * HIST_BUCKET_SIZE) - 1);
			else
				LOG_TRACE("   >=%-12d    ", b * HIST_BUCKET_SIZE);

			if (have_cpu)
				LOG_TRACE("%-12.2f ", cpu_avg_us);
			else
				LOG_TRACE("%-12s ", "---");

			if (have_dsa)
				LOG_TRACE("%-12.2f ", dsa_avg_us);
			else
				LOG_TRACE("%-12s ", "---");

			LOG_TRACE("%-10s %-8d\n", winner, total_ops);

			/* Accumulate time estimates where we have both measurements */
			if (have_cpu && have_dsa) {
				double cpu_avg_ns = cpu_avg_us * 1000.0;
				double dsa_avg_ns = dsa_avg_us * 1000.0;
				op_cpu_time_ns += (double)total_ops * cpu_avg_ns;
				op_dsa_time_ns += (double)total_ops * dsa_avg_ns;
				op_total_ops += total_ops;
			}
		}

		if (crossover_bucket >= 0)
			LOG_TRACE("\nDSA crossover: %d bytes (DSA starts outperforming CPU)\n",
				crossover_bucket * HIST_BUCKET_SIZE);
		else if (op_total_ops > 0)
			LOG_TRACE("\nNo DSA crossover found (CPU faster at all measured sizes)\n");

		if (op_total_ops > 0) {
			double savings_ns = op_cpu_time_ns - op_dsa_time_ns;
			double savings_pct = (op_cpu_time_ns > 0) ?
				(savings_ns / op_cpu_time_ns) * 100.0 : 0;

			LOG_TRACE("Total estimated CPU time:  %10.2f ms\n",
				op_cpu_time_ns / 1000000.0);
			LOG_TRACE("Total estimated DSA time:  %10.2f ms\n",
				op_dsa_time_ns / 1000000.0);
			if (savings_ns > 0)
				LOG_TRACE("Estimated DSA savings:    %10.2f ms (%.1f%%)\n",
					savings_ns / 1000000.0, savings_pct);
			else
				LOG_TRACE("Estimated DSA overhead:   %10.2f ms (%.1f%% slower)\n",
					-savings_ns / 1000000.0, -savings_pct);
		}

		total_cpu_time_ns += op_cpu_time_ns;
		total_dsa_time_ns += op_dsa_time_ns;
		total_ops_analyzed += op_total_ops;
	}

	if (total_ops_analyzed > 0) {
		double total_savings_ns = total_cpu_time_ns - total_dsa_time_ns;
		double total_savings_pct = (total_cpu_time_ns > 0) ?
			(total_savings_ns / total_cpu_time_ns) * 100.0 : 0;

		LOG_TRACE("\n======== Summary ========\n");
		LOG_TRACE("Combined CPU time:  %10.2f ms\n",
			total_cpu_time_ns / 1000000.0);
		LOG_TRACE("Combined DSA time:  %10.2f ms\n",
			total_dsa_time_ns / 1000000.0);
		if (total_savings_ns > 0)
			LOG_TRACE("Combined savings:   %10.2f ms (%.1f%%)\n",
				total_savings_ns / 1000000.0, total_savings_pct);
		else
			LOG_TRACE("Combined overhead:  %10.2f ms (%.1f%% slower)\n",
				-total_savings_ns / 1000000.0, -total_savings_pct);
	}

	/*
	 * Total expected speedup across ALL memory operations.
	 *
	 * cpu_only_time: estimated total time if every operation ran on CPU.
	 *   - min_size ops: use their actual measured CPU time
	 *   - DSA-eligible ops: use sampled CPU latency (extrapolated to all ops in bucket)
	 *
	 * dsa_projected_time: estimated total time with DSA for eligible ops.
	 *   - min_size ops: still on CPU (same as cpu_only)
	 *   - DSA-eligible ops where DSA wins: use DSA latency
	 *   - DSA-eligible ops where CPU wins: use CPU latency (wouldn't offload these)
	 */
	double cpu_only_time_ns = 0;
	double dsa_projected_time_ns = 0;
	double total_all_ops = 0;
	double total_min_size_ops = 0;

	for (int o = 0; o < MAX_MEMOP; ++o) {
		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			int min_size_count = op_counter[b][STDC_CALL_MIN_SIZE][o];
			int sampled_count = op_counter[b][STDC_CALL_SAMPLED][o];
			int dsa_count = op_counter[b][DSA_CALL_SUCCESS][o];
			int dsa_failed_count = op_counter[b][DSA_CALL_FAILED][o];

			/* min_size ops: always CPU, count towards both baselines */
			if (min_size_count > 0) {
				double min_size_time = (double)lat_counter[b][STDC_CALL_MIN_SIZE][o];
				cpu_only_time_ns += min_size_time;
				dsa_projected_time_ns += min_size_time;
				total_min_size_ops += min_size_count;
				total_all_ops += min_size_count;
			}

			/* DSA-eligible ops: sampled + dsa_success + dsa_failed */
			int eligible_count = sampled_count + dsa_count + dsa_failed_count;
			if (eligible_count == 0)
				continue;

			total_all_ops += eligible_count;

			double cpu_avg_ns = 0;
			double dsa_avg_ns = 0;
			bool have_cpu = sampled_count > 0;
			bool have_dsa = dsa_count > 0;

			if (have_cpu)
				cpu_avg_ns = ((double)lat_counter[b][STDC_CALL_SAMPLED][o]) /
					     (double)sampled_count;
			if (have_dsa)
				dsa_avg_ns = ((double)lat_counter[b][DSA_CALL_SUCCESS][o]) /
					     (double)dsa_count;

			if (have_cpu) {
				/* CPU baseline: all eligible ops at CPU speed */
				cpu_only_time_ns += (double)eligible_count * cpu_avg_ns;
			} else if (have_dsa) {
				/* No CPU sample; use DSA time as conservative estimate */
				cpu_only_time_ns += (double)eligible_count * dsa_avg_ns;
			}

			if (have_cpu && have_dsa) {
				/* Use whichever is faster (optimal offload decision) */
				double best_ns = (dsa_avg_ns < cpu_avg_ns) ? dsa_avg_ns : cpu_avg_ns;
				dsa_projected_time_ns += (double)eligible_count * best_ns;
			} else if (have_dsa) {
				dsa_projected_time_ns += (double)eligible_count * dsa_avg_ns;
			} else if (have_cpu) {
				/* No DSA measurement, assume stays on CPU */
				dsa_projected_time_ns += (double)eligible_count * cpu_avg_ns;
			}
		}
	}

	if (total_all_ops > 0 && cpu_only_time_ns > 0) {
		double speedup = cpu_only_time_ns / dsa_projected_time_ns;

		LOG_TRACE("\n======== Total Expected Speedup ========\n");
		LOG_TRACE("Total memory ops:       %10.0f\n", total_all_ops);
		LOG_TRACE("  min_size (CPU-only):  %10.0f\n", total_min_size_ops);
		LOG_TRACE("  DSA-eligible:         %10.0f\n", total_all_ops - total_min_size_ops);
		LOG_TRACE("CPU-only total time:    %10.2f ms\n",
			cpu_only_time_ns / 1000000.0);
		LOG_TRACE("DSA-projected time:     %10.2f ms\n",
			dsa_projected_time_ns / 1000000.0);
		LOG_TRACE("Expected speedup:       %10.2fx\n", speedup);

		double pct_time_min_size = 0;
		double min_size_total = 0;
		for (int o = 0; o < MAX_MEMOP; ++o)
			for (int b = 0; b < HIST_NO_BUCKETS; ++b)
				if (op_counter[b][STDC_CALL_MIN_SIZE][o] > 0)
					min_size_total += (double)lat_counter[b][STDC_CALL_MIN_SIZE][o];
		if (cpu_only_time_ns > 0)
			pct_time_min_size = (min_size_total / cpu_only_time_ns) * 100.0;

		LOG_TRACE("Time in min_size ops:   %10.2f ms (%.1f%% of CPU-only)\n",
			min_size_total / 1000000.0, pct_time_min_size);
	}

	LOG_TRACE("\n");
}

/*
 * Compute optimal dsa_min_size and cpu_size_fraction from profiling data.
 *
 * dsa_min_size: set to the crossover point — lowest byte range where DSA
 *   outperforms CPU across the dominant mem ops (memcpy/memset).
 *
 * cpu_size_fraction: for sizes above the crossover, compute the ratio that
 *   keeps CPU and DSA finishing at roughly the same time. If DSA takes D us
 *   and CPU takes C us for a given size, the CPU should handle C/(C+D) of the
 *   work so both finish simultaneously.
 */
static void apply_profiling_knobs(void)
{
	if (!dto_profiling || auto_adjust_knobs)
		return;

	/*
	 * Find the global crossover: lowest bucket where DSA wins for any op.
	 * Use the most conservative (highest) crossover across all ops so we
	 * don't send small ops to DSA where it's slower.
	 */
	int global_crossover = -1;

	for (int o = 0; o < MAX_MEMOP; ++o) {
		int op_crossover = -1;

		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			int sampled_count = op_counter[b][STDC_CALL_SAMPLED][o];
			int dsa_count = op_counter[b][DSA_CALL_SUCCESS][o];

			if (sampled_count < PROFILING_MIN_SAMPLES ||
			    dsa_count < PROFILING_MIN_SAMPLES)
				continue;

			double cpu_avg_ns = ((double)lat_counter[b][STDC_CALL_SAMPLED][o]) /
					    (double)sampled_count;
			double dsa_avg_ns = ((double)lat_counter[b][DSA_CALL_SUCCESS][o]) /
					    (double)dsa_count;

			if (dsa_avg_ns < cpu_avg_ns) {
				op_crossover = b;
				break;
			}
		}

		if (op_crossover >= 0) {
			if (global_crossover < 0 || op_crossover > global_crossover)
				global_crossover = op_crossover;
		}
	}

	/* Apply dsa_min_size from crossover point */
	if (global_crossover >= 0) {
		size_t new_min_size = (size_t)global_crossover * HIST_BUCKET_SIZE;
		if (new_min_size < HIST_BUCKET_SIZE)
			new_min_size = HIST_BUCKET_SIZE;
		dsa_min_size = new_min_size;
	}

	/*
	 * Compute optimal cpu_size_fraction from buckets above the crossover.
	 *
	 * For each bucket where DSA wins, the optimal CPU fraction is the ratio
	 * that makes the CPU work take the same time as the DSA work:
	 *   fraction = D / (C + D)
	 * where C = CPU time per byte, D = DSA time per byte.
	 *
	 * This means: if CPU does fraction*N bytes and DSA does (1-fraction)*N
	 * bytes, the CPU finishes in fraction*N*C_per_byte time and the DSA
	 * finishes in (1-fraction)*N*D_per_byte time. Setting them equal:
	 *   fraction * C = (1 - fraction) * D
	 *   fraction = D / (C + D)
	 *
	 * We weight by the number of ops in each bucket so the most common
	 * sizes dominate the fraction.
	 */
	double weighted_fraction_sum = 0;
	double total_weight = 0;

	for (int o = 0; o < MAX_MEMOP; ++o) {
		for (int b = (global_crossover >= 0 ? global_crossover : 0);
		     b < HIST_NO_BUCKETS; ++b) {
			int sampled_count = op_counter[b][STDC_CALL_SAMPLED][o];
			int dsa_count = op_counter[b][DSA_CALL_SUCCESS][o];

			if (sampled_count < PROFILING_MIN_SAMPLES ||
			    dsa_count < PROFILING_MIN_SAMPLES)
				continue;

			double cpu_avg_ns = ((double)lat_counter[b][STDC_CALL_SAMPLED][o]) /
					    (double)sampled_count;
			double dsa_avg_ns = ((double)lat_counter[b][DSA_CALL_SUCCESS][o]) /
					    (double)dsa_count;

			/* Only compute fraction where DSA wins */
			if (dsa_avg_ns >= cpu_avg_ns)
				continue;

			double fraction = dsa_avg_ns / (cpu_avg_ns + dsa_avg_ns);
			int total_ops = sampled_count + dsa_count +
					op_counter[b][DSA_CALL_FAILED][o];

			weighted_fraction_sum += fraction * (double)total_ops;
			total_weight += (double)total_ops;
		}
	}

	if (total_weight > 0) {
		double optimal_fraction = weighted_fraction_sum / total_weight;
		/* Clamp to valid range: 0 to MAX_CPU_SIZE_FRACTION */
		size_t new_csf = (size_t)(optimal_fraction * 100.0);
		if (new_csf > MAX_CPU_SIZE_FRACTION)
			new_csf = MAX_CPU_SIZE_FRACTION;
		cpu_size_fraction = new_csf;
	}

	LOG_TRACE("Profiling knobs applied: dsa_min_size=%lu, "
		"cpu_size_fraction=%.2f\n",
		dsa_min_size, cpu_size_fraction / 100.0);
}
#endif

#define DTO_MAX_PARAM_LEN 16
static void dto_get_param_string(int dir_fd, char *path, char *out)
{
	int fd;
	char buffer[DTO_MAX_PARAM_LEN];
	int bytes;

	out[0] = '\0';
	fd = openat(dir_fd, path, O_RDONLY);

	if (fd < 0)
		return;

	bytes = read(fd, buffer, DTO_MAX_PARAM_LEN - 1);

	if (bytes <= 0) {
		close(fd);
		return;
	}

	if (buffer[bytes - 1] == '\n')
		buffer[bytes - 1] = '\0';
	else
		buffer[bytes] = '\0';

	strncpy(out, buffer, DTO_MAX_PARAM_LEN);
	close(fd);
}

static unsigned long long dto_get_param_ullong(int dir_fd, char *path, int *err)
{
	int fd;
	char buffer[DTO_MAX_PARAM_LEN];
	int bytes;
	unsigned long long val;

	fd = openat(dir_fd, path, O_RDONLY);

	if (fd < 0) {
		*err = -errno;
		return -errno;
	}

	bytes = read(fd, buffer, DTO_MAX_PARAM_LEN - 1);

	if (bytes <= 0) {
		*err = -errno;
		close(fd);
		return -errno;
	}

	errno = 0;
	val = strtoull(buffer, NULL, 0);

	*err = -errno;
	close(fd);

	return val;
}

static struct dto_device* get_dto_device(int dev_numa_node) {
	struct dto_device* dev = NULL;

	if (devices[dev_numa_node] == NULL) {
		dev = calloc(1, sizeof(struct dto_device));
		devices[dev_numa_node] = dev;
	} else {
		dev = devices[dev_numa_node];
	}

	return dev;
}

static void correct_devices_list() {
	struct dto_device* dev = NULL;
	for (uint8_t i = 0; i < MAX_NUMA_NODES; i++) {
		if (devices[i] != NULL) {
			dev = devices[i];
		} else {
			devices[i] = dev;
		}
	}
}

static __always_inline  int get_numa_node(void* buf) {
	int numa_node = -1;

	switch (is_numa_aware) {
        case NA_BUFFER_CENTRIC: {
			if (buf != NULL) {
				int status[1] = {-1};

				// get numa node of memory pointed by buf
				if (move_pages(0, 1, &buf, NULL, status, 0) == 0) {
					numa_node = status[0];
				} else {
					LOG_ERROR("move_pages call error: %d - %s", errno, strerror(errno));
				}

				// alternatively get_mempolicy can be used
				// if (get_mempolicy(&numa_node, NULL, 0, (void *)buf, MPOL_F_NODE | MPOL_F_ADDR) != 0) {
				// 	LOG_ERROR("get_mempolicy call error: %d - %s", errno, strerror(errno));
				// }
			} else {
				LOG_ERROR("NULL buffer delivered. Unable to detect numa node");
			}
		}
		break;
		case NA_CPU_CENTRIC: {
			const int cpu = sched_getcpu();
			if (cpu != -1) {
				numa_node = numa_node_of_cpu(sched_getcpu());
			}
			else {
				LOG_ERROR("sched_getcpu call error: %d - %s", errno, strerror(errno));
			}
		}
		break;
        default:
		break;
        }

        return numa_node;
}

static void cleanup_devices() {
	struct dto_device* dev = NULL;
	for (uint i = 0; i < MAX_NUMA_NODES; i++) {
		if (devices[i] != dev) {
			dev = devices[i];
			free(devices[i]);
		}
		devices[i] = NULL;
	}
}

static bool test_write_syscall(struct dto_wq *wq)
{
	struct dsa_hw_desc desc = {0};
	struct dsa_completion_record comp __attribute__((aligned(32)));
	int retry = 0;

	desc.opcode = DSA_OPCODE_NOOP;
	desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	comp.status = 0;

	desc.completion_addr = (unsigned long)&comp;

	if (dsa_submit(wq, &desc) == SUCCESS) {
		while (comp.status == 0 && retry++ < 10000)
			_mm_pause();

		if (comp.status == DSA_COMP_SUCCESS)
			return true;
	}

	return false;
}

static int dsa_init_from_wq_list(char *wq_list)
{
	char *wq;
	char file_path[PATH_MAX];
	int dsa_id, wq_id;
	int dir_fd;
	int rc;

	num_wqs = 0;

	wq = strtok(wq_list, ";");
	while (wq != NULL) {
		char wq_mode[DTO_MAX_PARAM_LEN];

		if (sscanf(wq, "wq%d.%d", &dsa_id, &wq_id) != 2) {
			LOG_ERROR("Invalid WQ format %s\n", wq);
			rc = -EINVAL;
			goto fail_wq;
		}

		snprintf(file_path, PATH_MAX, "/sys/bus/dsa/devices/dsa%d", dsa_id);

		dir_fd = open(file_path, O_PATH);
		if (dir_fd == -1) {
			LOG_ERROR("dir %s open failed: %s\n", file_path, strerror(errno));
			rc = -errno;
			goto fail_wq;
		}

		wqs[num_wqs].dsa_gencap = dto_get_param_ullong(dir_fd, "gen_cap", &rc);
		if (rc) {
			close(dir_fd);
			goto fail_wq;
		}

		const int dev_numa_node = (int)dto_get_param_ullong(dir_fd, "numa_node", &rc);
		if (rc) {
			close(dir_fd);
			goto fail_wq;
		}

		close(dir_fd);

		snprintf(file_path, PATH_MAX, "/sys/bus/dsa/devices/%s", wq);

		dir_fd = open(file_path, O_PATH);
		if (dir_fd == -1) {
			LOG_ERROR("dir %s open failed: %s\n", file_path, strerror(errno));
			rc = -errno;
			goto fail_wq;
		}

		wqs[num_wqs].max_transfer_size = dto_get_param_ullong(dir_fd, "max_transfer_size", &rc);
		if (rc) {
			close(dir_fd);
			goto fail_wq;
		}

		dto_get_param_string(dir_fd, "mode", wq_mode);

		if (wq_mode[0] == '\0') {
			close(dir_fd);
			rc = -ENOTSUP;
			goto fail_wq;
		}

		if (strcmp(wq_mode, "shared") != 0) {
			continue;
		}

		wqs[num_wqs].wq_size = dto_get_param_ullong(dir_fd, "size", &rc);
		close(dir_fd);

		if (rc)
			goto fail_wq;

		snprintf(wqs[num_wqs].wq_path, PATH_MAX, "/dev/dsa/%s", wq);

		// open DSA WQ
		wqs[num_wqs].wq_fd = open(wqs[num_wqs].wq_path, O_RDWR);
		if (wqs[num_wqs].wq_fd < 0) {
			LOG_ERROR("DSA WQ %s open error: %s\n", wqs[num_wqs].wq_path, strerror(errno));
			rc = -errno;
			goto fail_wq;
		}

		// map DSA WQ portal
		wqs[num_wqs].wq_portal = mmap(NULL, 0x1000, PROT_WRITE, MAP_SHARED | MAP_POPULATE,
				wqs[num_wqs].wq_fd, 0);

		if (wqs[num_wqs].wq_portal == MAP_FAILED) {
			/* In case the driver doesn't support mmap, test if it
			 * supports write system call for work submission, and
			 * if yes, fallback to using write syscall.
			 */

			rc = -errno;
			if (test_write_syscall(&wqs[num_wqs]))
				wqs[num_wqs].wq_mmapped = false;
			else {
				LOG_ERROR("mmap error for DSA wq: %s, error: %s\n", wqs[num_wqs].wq_path, strerror(errno));
				goto fail_wq;
			}
		} else {
			wqs[num_wqs].wq_mmapped = true;
			close(wqs[num_wqs].wq_fd);
		}

		if (is_numa_aware) {
			struct dto_device* dev = get_dto_device(dev_numa_node);
			if (dev != NULL &&
				dev->num_wqs < MAX_WQS) {
				dev->wqs[dev->num_wqs++] = &wqs[num_wqs];
			}
		}

		++num_wqs;
		if (num_wqs == MAX_WQS)
			break;

		wq = strtok(NULL, ";");
	}

	if (num_wqs == 0) {
		rc = -EINVAL;
		goto fail;
	}

	if (is_numa_aware) {
		correct_devices_list();
	}

	return 0;

fail_wq:
	for (int j = 0; j < num_wqs; j++)
		munmap(wqs[j].wq_portal, 0x1000);
	num_wqs = 0;

	cleanup_devices();

fail:
	return rc;
}

static int dsa_init_from_accfg(void)
{
	int used_devids[MAX_WQS];
	struct accfg_device *device;
	struct accfg_wq *wq;
	struct accfg_ctx *dto_ctx = NULL;
	int rc;
	int i;

	for (i = 0; i < MAX_WQS; i++) {
		wqs[i].acc_wq = NULL;
		used_devids[i] = -1;
	}

	rc = accfg_new(&dto_ctx);
	if (rc < 0)
		return rc;
	num_wqs = 0;

	accfg_device_foreach(dto_ctx, device) {
		enum accfg_device_state dstate;

		/* use dsa devices only*/
		if (strncmp(accfg_device_get_devname(device), "dsa", 3)!= 0)
			continue;

		/* Make sure that the device is enabled */
		dstate = accfg_device_get_state(device);
		if (dstate != ACCFG_DEVICE_ENABLED)
			continue;

		/* Check if we have already used a wq on this device */
		for (i = 0; i < num_wqs; i++)
			if (accfg_device_get_id(device) == used_devids[i])
				break;
		if (i != num_wqs)
			continue;

		struct dto_device* dev = NULL;

		if (is_numa_aware) {
			const int dev_numa_node = accfg_device_get_numa_node(device);
			dev = get_dto_device(dev_numa_node);
		}

		accfg_wq_foreach(device, wq) {
			enum accfg_wq_state wstate;
			enum accfg_wq_mode mode;
			enum accfg_wq_type type;

			/* Get a workqueue that's enabled */
			wstate = accfg_wq_get_state(wq);
			if (wstate != ACCFG_WQ_ENABLED)
				continue;

			/* The wq type should be user */
			type = accfg_wq_get_type(wq);
			if (type != ACCFG_WQT_USER)
				continue;

			/* the wq mode should be shared work queue */
			mode = accfg_wq_get_mode(wq);
			if (mode != ACCFG_WQ_SHARED)
				continue;

			wqs[num_wqs].wq_size = accfg_wq_get_size(wq);
			wqs[num_wqs].max_transfer_size = accfg_wq_get_max_transfer_size(wq);

			wqs[num_wqs].acc_wq = wq;
			wqs[num_wqs].dsa_gencap = accfg_device_get_gen_cap(device);

			used_devids[num_wqs] = accfg_device_get_id(device);

			if (is_numa_aware &&
				dev != NULL &&
				dev->num_wqs < MAX_WQS) {
				dev->wqs[dev->num_wqs++] = &wqs[num_wqs];
			}

			num_wqs++;
		}

		if (num_wqs == MAX_WQS)
			break;
	}

	if (num_wqs == 0) {
		rc = -EINVAL;
		goto fail;
	}

	for (i = 0; i < num_wqs; i++) {
		struct accfg_wq *acc_wq = wqs[i].acc_wq;

		rc = accfg_wq_get_user_dev_path(acc_wq, wqs[i].wq_path, PATH_MAX);
		if (rc) {
			LOG_ERROR("Error getting device path\n");
			goto fail_wq;
		}

		// open DSA WQ
		wqs[i].wq_fd = open(wqs[i].wq_path, O_RDWR);
		if (wqs[i].wq_fd < 0) {
			LOG_ERROR("DSA WQ %s open error: %s\n", wqs[i].wq_path, strerror(errno));
			rc = -errno;
			goto fail_wq;
		}

		// map DSA WQ portal
		wqs[i].wq_portal = mmap(NULL, 0x1000, PROT_WRITE, MAP_SHARED | MAP_POPULATE, wqs[i].wq_fd, 0);

		if (wqs[i].wq_portal == MAP_FAILED) {
			/* In case the driver doesn't support mmap, test if it
			 * supports write system call for work submission, and
			 * if yes, fallback to using write syscall.
			 */
			rc = -errno;
			if (test_write_syscall(&wqs[i]))
				wqs[num_wqs].wq_mmapped = false;
			else {
				LOG_ERROR("mmap error for DSA wq: %s, error: %s\n", wqs[i].wq_path, strerror(errno));
				goto fail_wq;
			}
		} else {
			wqs[i].wq_mmapped = true;
			close(wqs[i].wq_fd);
		}
	}

	if (is_numa_aware) {
		correct_devices_list();
	}

	accfg_unref(dto_ctx);
	return 0;

fail_wq:
	for (int j = 0; j < i; j++)
		munmap(wqs[j].wq_portal, 0x1000);
	num_wqs = 0;

	cleanup_devices();
fail:
	accfg_unref(dto_ctx);
	return rc;
}

static int dsa_init(void)
{
	unsigned int unused[2];
	unsigned int leaf, waitpkg;
	const char *env_str;
	char wq_list[256];

	/* detect waitpkg support */
	leaf = 7;
	waitpkg = 0;
	if (__get_cpuid(0, &leaf, unused, &waitpkg, unused + 1)) {
		if (waitpkg & 0x20) {
			LOG_TRACE("waitpkg supported\n");
			waitpkg_support = 1;
		}
	}

	env_str = getenv("DTO_WAIT_METHOD");
	if (env_str != NULL) {
		if (!strncmp(env_str, wait_names[WAIT_BUSYPOLL], strlen(wait_names[WAIT_BUSYPOLL]))) {
			wait_method = WAIT_BUSYPOLL;
			min_avg_waits = MIN_AVG_POLL_WAITS;
			max_avg_waits = MAX_AVG_POLL_WAITS;
		} else if (!strncmp(env_str, wait_names[WAIT_UMWAIT], strlen(wait_names[WAIT_UMWAIT]))) {
			if (waitpkg_support) {
				wait_method = WAIT_UMWAIT;
				/* Use the same waits as busypoll for now */
				min_avg_waits = MIN_AVG_POLL_WAITS;
				max_avg_waits = MAX_AVG_POLL_WAITS;
			} else
				LOG_ERROR("umwait not supported. Falling back to default wait method\n");
		} else if (!strncmp(env_str, wait_names[WAIT_TPAUSE], strlen(wait_names[WAIT_TPAUSE]))) {
		    if (waitpkg_support) {
			wait_method = WAIT_TPAUSE;
                    } else {
			LOG_ERROR("tpause not supported. Falling back to busypoll\n");
                        wait_method = WAIT_BUSYPOLL;
                    }
                }
	}

	env_str = getenv("DTO_WQ_LIST");
	if (env_str == NULL)
		return dsa_init_from_accfg();

	strncpy(wq_list, env_str, sizeof(wq_list) - 1);
	/* ensure wq_list is null terminated */
	wq_list[sizeof(wq_list) - 1] = '\0';

	return dsa_init_from_wq_list(wq_list);
}

static int init_dto(void)
{
	uint8_t init_notcomplete = 0;

	if (atomic_compare_exchange_strong(&dto_initializing, &init_notcomplete, 1)) {
		char *env_str;

		env_str = getenv("DTO_LOG_FILE");
		if (env_str != NULL) {
			char temp[PATH_MAX];
			struct stat st;

			strncpy(dto_log_path, env_str, PATH_MAX - 1);
			/* ensure dto_log_path is null terminated */
			dto_log_path[PATH_MAX - 1] = '\0';

			snprintf(temp, sizeof(temp), ".%s.%d", __progname, getpid());
			strncat(dto_log_path, temp, PATH_MAX - strlen(dto_log_path) - 1);

			/* Open the log file only if it doesn't exist or if it is a regular file */
			if (lstat(dto_log_path, &st) == -1 ||
					(st.st_mode & S_IFMT) == S_IFREG)
				log_fd = open(dto_log_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
			/* No need to handle the open() error. It will automatically fallback
			 * to using standard output if log file open failed
			 */
		}

		env_str = getenv("DTO_LOG_LEVEL");
		if (env_str != NULL) {
			errno = 0;
			log_level = strtoul(env_str, NULL, 10);
			if (errno)
				log_level = LOG_LEVEL_FATAL;

			if (log_level > LOG_LEVEL_TRACE)
				log_level = LOG_LEVEL_TRACE;
		}

		// save std c lib function pointers
		orig_memset = dlsym(RTLD_NEXT, "memset");
		orig_memcpy = dlsym(RTLD_NEXT, "memcpy");
		orig_memmove = dlsym(RTLD_NEXT, "memmove");
		orig_memcmp = dlsym(RTLD_NEXT, "memcmp");

		env_str = getenv("DTO_USESTDC_CALLS");
		if (env_str != NULL) {
			errno = 0;
			use_std_lib_calls = strtoul(env_str, NULL, 10);
			if (errno)
				use_std_lib_calls = 0;

			use_std_lib_calls = !!use_std_lib_calls;
		}

		env_str = getenv("DTO_DSA_MEMCPY");
		if (env_str != NULL) {
			errno = 0;
			dto_dsa_memcpy = strtoul(env_str, NULL, 10);
			if (errno)
				dto_dsa_memcpy = 0;

			dto_dsa_memcpy = !!dto_dsa_memcpy;
		}

		env_str = getenv("DTO_DSA_CC");
		if (env_str != NULL) {
			errno = 0;
			dto_dsa_cc = strtoul(env_str, NULL, 10);
			if (errno)
				dto_dsa_cc = 0;

			dto_dsa_cc = !!dto_dsa_cc;
		}

		env_str = getenv("DTO_DSA_MEMMOVE");
		if (env_str != NULL) {
			errno = 0;
			dto_dsa_memmove = strtoul(env_str, NULL, 10);
			if (errno)
				dto_dsa_memmove = 0;

			dto_dsa_memmove = !!dto_dsa_memmove;
		}

		env_str = getenv("DTO_DSA_MEMSET");
		if (env_str != NULL) {
			errno = 0;
			dto_dsa_memset = strtoul(env_str, NULL, 10);
			if (errno)
				dto_dsa_memset = 0;

			dto_dsa_memset = !!dto_dsa_memset;
		}

		env_str = getenv("DTO_DSA_MEMCMP");
		if (env_str != NULL) {
			errno = 0;
			dto_dsa_memcmp = strtoul(env_str, NULL, 10);
			if (errno)
				dto_dsa_memcmp = 0;

			dto_dsa_memcmp = !!dto_dsa_memcmp;
		}

		env_str = getenv("DTO_OVERLAPPING_MEMMOVE_ACTION");
		if (env_str != NULL) {
			errno = 0;
			dto_overlapping_memmove_action = strtoul(env_str, NULL, 10);
			if (errno)
				dto_overlapping_memmove_action = OVERLAPPING_CPU;
		}

#ifdef DTO_STATS_SUPPORT
		env_str = getenv("DTO_COLLECT_STATS");
		if (env_str != NULL) {
			errno = 0;
			collect_stats = strtoul(env_str, NULL, 10);
			if (errno)
				collect_stats = 0;

			collect_stats = !!collect_stats;
		}

		if (collect_stats) {
			env_str = getenv("DTO_STATS_FILE");
                        if (env_str != NULL) {
                                struct stat st;

                                if (lstat(env_str, &st) == -1 ||
                                        (st.st_mode & S_IFMT) == S_IFREG) {
                                        stats_fd = open(env_str, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                                        if (stats_fd == -1)
                                                LOG_ERROR("Failed to open DTO_STATS_FILE '%s': %s\n", env_str, strerror(errno));
                                } else {
                                        LOG_ERROR("DTO_STATS_FILE '%s' must reference a regular file\n", env_str);
                                }
                        }

                        clock_gettime(CLOCK_BOOTTIME, &dto_start_time);
                        /* Change the log level to 'trace' so that the
                         * stats can be logged
                         */
                        log_level = LOG_LEVEL_TRACE;
                        dto_stats_log("Stats collection enabled, some latencies may be higher due to collection overhead.\n");
                        install_sigint_handler();
                }
#endif

		/* Register fork handler for the child process */
		if (!fork_handler_registered) {
			/* If pthread_atfork fails, and process calls fork,
			 * the child may crash when using DSA offload. Dont
			 * take that risk and disable DSA offload for parent
			 * as well.
			 */
			if (!pthread_atfork(NULL, NULL, child))
				fork_handler_registered = 1;
			else {
				LOG_ERROR("Setting fork() handler failed. "
					"Falling back to using CPUs.\n");
				use_std_lib_calls = 1;
			}
		}

		// initialize DSA
		if (!use_std_lib_calls) {
			// check environment variables
			env_str = getenv("DTO_MIN_BYTES");

			if (env_str != NULL) {
				errno = 0;
				dsa_min_size = strtoul(env_str, NULL, 10);
				if (errno)
					dsa_min_size = DTO_DEFAULT_MIN_SIZE;
			}

			double cpu_size_fraction_float = 0.0;
			env_str = getenv("DTO_CPU_SIZE_FRACTION");

			if (env_str != NULL) {
				errno = 0;
				cpu_size_fraction_float = strtod(env_str, NULL);

				if (errno || cpu_size_fraction_float < 0 || cpu_size_fraction_float >= 1) {
					LOG_ERROR("Invalid DTO_CPU_SIZE_FRACTION %s, "
						"Must be >= 0 and < 1. "
						"Falling back to default 0.0\n", env_str);
					cpu_size_fraction_float = 0.0;
				}
				/* Use only 2 digits after decimal point */
				cpu_size_fraction = cpu_size_fraction_float * 100;
			}

			env_str = getenv("DTO_AUTO_ADJUST_KNOBS");

			if (env_str != NULL) {
				errno = 0;
				auto_adjust_knobs = strtoul(env_str, NULL, 10);
				if (errno)
					auto_adjust_knobs = 1;

				auto_adjust_knobs = !!auto_adjust_knobs;
			}

			env_str = getenv("DTO_PROFILING");
			if (env_str != NULL) {
				errno = 0;
				dto_profiling = strtoul(env_str, NULL, 10);
				if (errno)
					dto_profiling = 0;
				dto_profiling = !!dto_profiling;
			}

			env_str = getenv("DTO_PROFILING_SAMPLE_INTERVAL");
			if (env_str != NULL) {
				errno = 0;
				profiling_sample_interval = strtoul(env_str, NULL, 10);
				if (errno || profiling_sample_interval == 0)
					profiling_sample_interval = PROFILING_SAMPLE_INTERVAL_DEFAULT;
			}

			if (numa_available() != -1) {
				env_str = getenv("DTO_IS_NUMA_AWARE");
				if (env_str != NULL) {
					errno = 0;
					is_numa_aware = strtoul(env_str, NULL, 10);
					if (errno || is_numa_aware >= NA_LAST_ENTRY) {
						is_numa_aware = NA_NONE;
					}
				}
			}

			env_str = getenv("DTO_UMWAIT_DELAY");

			if (env_str != NULL) {
				errno = 0;
				dto_umwait_delay = strtoul(env_str, NULL, 10);
				if (errno || dto_umwait_delay == 0)
					dto_umwait_delay = UMWAIT_DELAY_DEFAULT;
			}

			if (dsa_init()) {
				LOG_ERROR("Didn't find any usable DSAs. Falling back to using CPUs.\n");
				use_std_lib_calls = 1;
			}

                        // calculate the wait time for TPAUSE
                        if (wait_method == WAIT_TPAUSE) {
    			        unsigned int num, den, freq;
    			        unsigned int empty;
    			        unsigned long long tmp;
    			        __get_cpuid( 0x15, &den, &num, &freq, &empty );
    			        freq /= 1000;
    			        LOG_TRACE( "Core Freq = %u kHz\n", freq );
    			        LOG_TRACE( "TSC Mult  = %u\n", num );
    			        LOG_TRACE( "TSC Den   = %u\n", den );
    			        freq *= num;
    			        freq /= den;
    			        LOG_TRACE( "CPU freq = %u kHz\n", freq );

    			        LOG_TRACE( "Requested wait: %llu nsec\n", tpause_wait_time );
    			        tmp = tpause_wait_time;
    			        tmp *= freq;
    			        tpause_wait_time = tmp / NSEC_PER_MSEC;
    			        LOG_TRACE( "Requested wait duration: %llu cycles\n", tpause_wait_time );
                        }
    
			// display configuration
			LOG_TRACE("log_level: %d, collect_stats: %d, use_std_lib_calls: %d, dsa_min_size: %lu, "
				"cpu_size_fraction: %.2f, wait_method: %s, auto_adjust_knobs: %d, numa_awareness: %s, dto_dsa_cc: %d, "
				"profiling: %d, profiling_sample_interval: %u\n",
				log_level, collect_stats, use_std_lib_calls, dsa_min_size,
				cpu_size_fraction_float, wait_names[wait_method], auto_adjust_knobs, numa_aware_names[is_numa_aware], dto_dsa_cc,
				dto_profiling, profiling_sample_interval);
			for (int i = 0; i < num_wqs; i++)
				LOG_TRACE("[%d] wq_path: %s, wq_size: %d, dsa_cap: %lx\n", i,
					wqs[i].wq_path, wqs[i].wq_size, wqs[i].dsa_gencap);
		}
		dto_initialized = 1;

		return DTO_INITIALIZED;
	}

	return DTO_INITIALIZING;
}

static void cleanup_dto(void)
{
	// unmap and close wq portal
	for (int i = 0; i < num_wqs; i++) {
		if (wqs[i].wq_mmapped) {
			munmap(wqs[i].wq_portal, 0x1000);
			wqs[i].wq_mmapped = false;
		}
		close(wqs[i].wq_fd);
	}
#ifdef DTO_STATS_SUPPORT
	print_stats();
	analyze_profiling_stats();
	apply_profiling_knobs();
	restore_sigint_handler();
#endif
        if (log_fd != -1)
                close(log_fd);
	if (stats_fd != -1) {
		close(stats_fd);
		stats_fd = -1;
	}

	cleanup_devices();
}

static __always_inline  struct dto_wq *get_wq(void* buf)
{
	struct dto_wq* wq = NULL;

	if (is_numa_aware) {
		int status[1] = {-1};

		// get the numa node for the target DSA device
		const int numa_node = get_numa_node(buf);
		if (numa_node >= 0 && numa_node < MAX_NUMA_NODES) {
			struct dto_device* dev = devices[numa_node];
			if (dev != NULL &&
				dev->num_wqs > 0) {
				wq = dev->wqs[dev->next_wq++ % dev->num_wqs];
			}
		}
	}

	if (wq == NULL) {
		wq = &wqs[next_wq++ % num_wqs];
	}

	return wq;
}

static void dto_memset(void *s, int c, size_t n, int *result)
{
	uint64_t memset_pattern;
	size_t cpu_size, dsa_size;
	struct dto_wq *wq = get_wq(s);

	for (int i = 0; i < 8; ++i)
		((uint8_t *) &memset_pattern)[i] = (uint8_t) c;

	thr_desc.opcode = DSA_OPCODE_MEMFILL;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
	thr_desc.completion_addr = (uint64_t)&thr_comp;
	thr_desc.pattern = memset_pattern;

	/* cpu_size_fraction guaranteed to be >= 0 and < 100 */
	cpu_size = n * cpu_size_fraction / 100;
	dsa_size = n - cpu_size;

	thr_bytes_completed = 0;
	thr_cpu_fraction_bytes = 0;
	thr_cpu_fraction_ns = 0;
	thr_submit_ns = 0;
	thr_poll_ns = 0;
	if (dsa_size <= wq->max_transfer_size) {
		thr_desc.dst_addr = (uint64_t) s + cpu_size;
		thr_desc.xfer_size = (uint32_t) dsa_size;
		thr_comp.status = 0;
		*result = dsa_submit(wq, &thr_desc);
		if (likely(*result == SUCCESS)) {
			if (cpu_size) {
#ifdef DTO_STATS_SUPPORT
				struct timespec _fst, _fet;
				if (unlikely(collect_stats))
					clock_gettime(CLOCK_BOOTTIME, &_fst);
#endif
				orig_memset(s, c, cpu_size);
#ifdef DTO_STATS_SUPPORT
				if (unlikely(collect_stats)) {
					clock_gettime(CLOCK_BOOTTIME, &_fet);
					thr_cpu_fraction_ns = ((_fet.tv_sec*1000000000) + _fet.tv_nsec) -
						((_fst.tv_sec*1000000000) + _fst.tv_nsec);
				}
#endif
				thr_bytes_completed = cpu_size;
				thr_cpu_fraction_bytes = cpu_size;
			}
			*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
		}
	} else {
		uint32_t threshold;
		size_t current_cpu_size_fraction = cpu_size_fraction;  // the cpu_size_fraction might be changed by the auto tune algorithm
		threshold = wq->max_transfer_size * 100 / (100 - current_cpu_size_fraction);
#ifdef DTO_STATS_SUPPORT
		struct timespec _fst, _fet;
#endif

		do {
			size_t len;

			len = n <= threshold ? n : threshold;

			cpu_size = len * current_cpu_size_fraction / 100;
			dsa_size = len - cpu_size;

			thr_desc.dst_addr = (uint64_t) s + cpu_size + thr_bytes_completed;
			thr_desc.xfer_size = (uint32_t) dsa_size;
			thr_comp.status = 0;
			*result = dsa_submit(wq, &thr_desc);
			if (*result == SUCCESS) {
				if (cpu_size) {
					void *s1 = s + thr_bytes_completed;

#ifdef DTO_STATS_SUPPORT
					if (unlikely(collect_stats))
						clock_gettime(CLOCK_BOOTTIME, &_fst);
#endif
					orig_memset(s1, c, cpu_size);
#ifdef DTO_STATS_SUPPORT
					if (unlikely(collect_stats)) {
						clock_gettime(CLOCK_BOOTTIME, &_fet);
						thr_cpu_fraction_ns += ((_fet.tv_sec*1000000000) + _fet.tv_nsec) -
							((_fst.tv_sec*1000000000) + _fst.tv_nsec);
					}
#endif
					thr_bytes_completed += cpu_size;
					thr_cpu_fraction_bytes += cpu_size;
				}
				*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
			}

			if (*result != SUCCESS)
				break;
			n -= len;
			/* If remaining bytes are less than dsa_min_size,
			 * dont submit to DSA. Instead, complete remaining
			 * bytes on CPU
			 */
		} while (n >= dsa_min_size);
	}
}

/* For overlapping src & dest buffers in memmove API, we can't split the memmove
 * job. Otherwise, it may lead to incorrect copy operation.
 */
static __always_inline bool dto_profiling_is_sample(void)
{
	if (unlikely(tl_next_sample == 0))
		tl_next_sample = rand() % (profiling_sample_interval * 2) + 1;
	return ++tl_num_descs == tl_next_sample;
}

static __always_inline void dto_profiling_schedule_next(void)
{
	tl_next_sample = tl_num_descs + rand() % (profiling_sample_interval * 2) + 1;

#ifdef DTO_STATS_SUPPORT
	/* Periodically recompute knobs from profiling data */
	if (unlikely(++tl_profiling_knob_counter >= PROFILING_KNOB_RECOMPUTE_INTERVAL)) {
		tl_profiling_knob_counter = 0;
		apply_profiling_knobs();
	}
#endif
}

static bool is_overlapping_buffers (void *dest, const void *src, size_t n)
{
	if ((dest + n) < src || (src + n) < dest)
		return false;

	return true;
}

static bool dto_memcpymove(void *dest, const void *src, size_t n, bool is_memcpy, int *result)
{
	struct dto_wq *wq;
	size_t cpu_size, dsa_size;
	bool is_overlapping;

	thr_bytes_completed = 0;
	thr_cpu_fraction_bytes = 0;
	thr_cpu_fraction_ns = 0;
	thr_submit_ns = 0;
	thr_poll_ns = 0;

	if (!is_memcpy && is_overlapping_buffers(dest, src, n)) {
		cpu_size = 0;
		is_overlapping = true;
	} else {
		/* cpu_size_fraction guaranteed to be >= 0 and < 1 */
		cpu_size = n * cpu_size_fraction / 100;
		is_overlapping = false;
	}

	// If this is an overlapping memmove and the action is to perform on CPU, return having done nothing and
	// memmove will perform the copy and correctly attribute statistics to stdlib call group
	if (is_overlapping && dto_overlapping_memmove_action == OVERLAPPING_CPU) {
		*result = SUCCESS;
		return true;
	}

	dsa_size = n - cpu_size;
	wq = get_wq(dest);

	thr_desc.opcode = DSA_OPCODE_MEMMOVE;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
	thr_desc.completion_addr = (uint64_t)&thr_comp;

	if (dsa_size <= wq->max_transfer_size) {
		thr_desc.src_addr = (uint64_t) src + cpu_size;
		thr_desc.dst_addr = (uint64_t) dest + cpu_size;
		thr_desc.xfer_size = (uint32_t) dsa_size;
		thr_comp.status = 0;
		if (is_overlapping) {
			*result = dsa_execute(wq, &thr_desc, &thr_comp.status);
		} else {
			*result = dsa_submit(wq, &thr_desc);
			if (*result == SUCCESS) {
				if (cpu_size) {
#ifdef DTO_STATS_SUPPORT
					struct timespec _fst, _fet;
					if (unlikely(collect_stats))
						clock_gettime(CLOCK_BOOTTIME, &_fst);
#endif
					if (is_memcpy)
						orig_memcpy(dest, src, cpu_size);
					else
						orig_memmove(dest, src, cpu_size);
#ifdef DTO_STATS_SUPPORT
					if (unlikely(collect_stats)) {
						clock_gettime(CLOCK_BOOTTIME, &_fet);
						thr_cpu_fraction_ns = ((_fet.tv_sec*1000000000) + _fet.tv_nsec) -
							((_fst.tv_sec*1000000000) + _fst.tv_nsec);
					}
#endif
					thr_bytes_completed += cpu_size;
					thr_cpu_fraction_bytes += cpu_size;
				}
				*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
			}
		}
	} else {
		uint32_t threshold;
		size_t current_cpu_size_fraction = cpu_size_fraction;  // the cpu_size_fraction might be changed by the auto tune algorithm
		if (is_overlapping) {
			threshold = wq->max_transfer_size;
		} else {
			threshold = wq->max_transfer_size * 100 / (100 - current_cpu_size_fraction);
		}
#ifdef DTO_STATS_SUPPORT
		struct timespec _fst, _fet;
#endif

		do {
			size_t len;

			len = n <= threshold ? n : threshold;

			if (!is_overlapping)
				cpu_size = len * current_cpu_size_fraction / 100;

			dsa_size = len - cpu_size;

			thr_desc.src_addr = (uint64_t) src + cpu_size + thr_bytes_completed;
			thr_desc.dst_addr = (uint64_t) dest + cpu_size + thr_bytes_completed;
			thr_desc.xfer_size = (uint32_t) dsa_size;
			thr_comp.status = 0;
			if (is_overlapping){
				*result = dsa_execute(wq, &thr_desc, &thr_comp.status);
			} else {
				*result = dsa_submit(wq, &thr_desc);
				if (*result == SUCCESS) {
					if (cpu_size) {
						const void *src1 = src + thr_bytes_completed;
						void *dest1 = dest + thr_bytes_completed;

#ifdef DTO_STATS_SUPPORT
						if (unlikely(collect_stats))
							clock_gettime(CLOCK_BOOTTIME, &_fst);
#endif
						if (is_memcpy)
							orig_memcpy(dest1, src1, cpu_size);
						else
							orig_memmove(dest1, src1, cpu_size);
#ifdef DTO_STATS_SUPPORT
						if (unlikely(collect_stats)) {
							clock_gettime(CLOCK_BOOTTIME, &_fet);
							thr_cpu_fraction_ns += ((_fet.tv_sec*1000000000) + _fet.tv_nsec) -
								((_fst.tv_sec*1000000000) + _fst.tv_nsec);
						}
#endif
						thr_bytes_completed += cpu_size;
						thr_cpu_fraction_bytes += cpu_size;
					}
					*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
				}
			}

			if (*result != SUCCESS)
				break;
			n -= len;
			/* If remaining bytes are less than dsa_min_size,
			* dont submit to DSA. Instead, complete remaining
			* bytes on CPU
			*/
		} while (n >= dsa_min_size);
	}

	return is_overlapping;
}

static int dto_memcmp(const void *s1, const void *s2, size_t n, int *result)
{
	struct dto_wq *wq = get_wq((void*)s2);
	int cmp_result = 0;
	size_t orig_n = n;

	thr_desc.opcode = DSA_OPCODE_COMPARE;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	thr_desc.completion_addr = (uint64_t)&thr_comp;
	thr_comp.result = 0;

	thr_bytes_completed = 0;
	thr_submit_ns = 0;
	thr_poll_ns = 0;

	if (n <= wq->max_transfer_size) {
		thr_desc.src_addr = (uint64_t) s1;
		thr_desc.src2_addr = (uint64_t) s2;
		thr_desc.xfer_size = (uint32_t) n;
		*result = dsa_execute(wq, &thr_desc, &thr_comp.status);
	} else {
		do {
			size_t len;

			len = n <= wq->max_transfer_size ? n : wq->max_transfer_size;

			thr_desc.src_addr = (uint64_t) s1 + thr_bytes_completed;
			thr_desc.src2_addr = (uint64_t) s2 + thr_bytes_completed;
			thr_desc.xfer_size = (uint32_t) len;
			*result = dsa_execute(wq, &thr_desc, &thr_comp.status);

			if (*result != SUCCESS || thr_comp.result)
				break;

			n -= len;
			/* If remaining bytes are less than dsa_min_size,
			 * dont submit to DSA. Instead, complete remaining
			 * bytes on CPU
			 */
		} while (n >= dsa_min_size);
	}

	if (thr_comp.result) {
		/* cmp returned mismatch. determine the return value */
		uint8_t *t1 = (uint8_t *)s1 + thr_bytes_completed;
		uint8_t *t2 = (uint8_t *)s2 + thr_bytes_completed;

		cmp_result = *t1 - *t2;
		/* Inform the caller than the job is done even though
		 * we didn't process all the bytes
		 */
		thr_bytes_completed = orig_n;
	}
	return cmp_result;
}

/* The dto_internal_mem* APIs are used only when mem* APIs are
 * called before DTO is properly initialized. So these
 * implementations dont have to be performant
 */
static void *dto_internal_memset(void *s1, int c, size_t n)
{
	char *dest = s1;
	size_t i;

	for (i = 0; i < n; i++)
		dest[i] = (char)c;

	return s1;
}

static void *dto_internal_memcpymove(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = (const char *)src;
	ssize_t i;

	if (s >= d) {
		/* go from beginning to end */
		for (i = 0; i < n; i++)
			d[i] = s[i];
	} else {
		/* go from end to beginning */
		for (i = n - 1; i >= 0; i--)
			d[i] = s[i];
	}

	return dest;
}

static int dto_internal_memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *src1 = (const unsigned char *)s1;
	const unsigned char *src2 = (const unsigned char *)s2;
	size_t i;

	for (i = 0; i < n; i++) {
		if (src1[i] != src2[i])
			return src1[i] - src2[i];
	}
	return 0;
}

void *memset(void *s1, int c, size_t n)
{
	int result = 0;
	void *ret = s1;
	int use_orig_func = USE_ORIG_FUNC(n, dto_dsa_memset);
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	int cpu_fallback_group = STDC_CALL_MIN_SIZE;
#endif

	if (unlikely(dto_initialized == 0)) {
		/* If there are other constructors in the same binary,
		 * they may run before DTO's constructor. Just use
		 * internal CPU-based implementation if DTO is not
		 * initialized yet.
		 */
		return dto_internal_memset(s1, c, n);
	}

	if (!use_orig_func) {
		if (unlikely(dto_profiling) && dto_profiling_is_sample()) {
			dto_profiling_schedule_next();
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_START(collect_stats, st);
#endif
			orig_memset(s1, c, n);
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_SAMPLED_CPU_END(collect_stats, st, et, MEMSET, n);
#endif
			return ret;
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		dto_memset(s1, c, n, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMSET, n, false, thr_bytes_completed, result);
		if (unlikely(collect_stats))
			update_submit_poll_stats(MEMSET, orig_n, thr_submit_ns, thr_poll_ns);
		if (unlikely(collect_stats) && thr_cpu_fraction_bytes > 0)
			update_stats(MEMSET, orig_n, false, thr_cpu_fraction_bytes, thr_cpu_fraction_ns, STDC_CALL_CPU_FRACTION, 0);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			s1 = (void *)((uint64_t)s1 + thr_bytes_completed);
#ifdef DTO_STATS_SUPPORT
			cpu_fallback_group = STDC_CALL_DSA_FAILED;
#endif
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memset(s1, c, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMSET, n, orig_n, cpu_fallback_group);
#endif
	}
	return ret;
}

void *memcpy(void *dest, const void *src, size_t n)
{
	int result = 0;
	void *ret = dest;
	int use_orig_func = USE_ORIG_FUNC(n, dto_dsa_memcpy);
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	int cpu_fallback_group = STDC_CALL_MIN_SIZE;
#endif

	if (unlikely(dto_initialized == 0)) {
		/* If there are other constructors in the same binary,
		 * they may run before DTO's constructor. Just use
		 * internal CPU-based implementation if DTO is not
		 * initialized yet.
		 */
		return dto_internal_memcpymove(dest, src, n);
	}

	if (!use_orig_func) {
		if (unlikely(dto_profiling) && dto_profiling_is_sample()) {
			dto_profiling_schedule_next();
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_START(collect_stats, st);
#endif
			orig_memcpy(dest, src, n);
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_SAMPLED_CPU_END(collect_stats, st, et, MEMCOPY, n);
#endif
			return ret;
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		dto_memcpymove(dest, src, n, 1, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCOPY, n, false, thr_bytes_completed, result);
		if (unlikely(collect_stats))
			update_submit_poll_stats(MEMCOPY, orig_n, thr_submit_ns, thr_poll_ns);
		if (unlikely(collect_stats) && thr_cpu_fraction_bytes > 0)
			update_stats(MEMCOPY, orig_n, false, thr_cpu_fraction_bytes, thr_cpu_fraction_ns, STDC_CALL_CPU_FRACTION, 0);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			if (thr_comp.result == 0) {
				dest = (void *)((uint64_t)dest + thr_bytes_completed);
				src = (const void *)((uint64_t)src + thr_bytes_completed);
			}
#ifdef DTO_STATS_SUPPORT
			cpu_fallback_group = STDC_CALL_DSA_FAILED;
#endif
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memcpy(dest, src, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMCOPY, n, orig_n, cpu_fallback_group);
#endif
	}
	return ret;
}

void *memmove(void *dest, const void *src, size_t n)
{
	int result = 0;
	void *ret = dest;
	int use_orig_func = USE_ORIG_FUNC(n, dto_dsa_memmove);
	bool is_overlapping;
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	int cpu_fallback_group = STDC_CALL_MIN_SIZE;
#endif

	if (unlikely(dto_initialized == 0)) {
		/* If there are other constructors in the same binary,
		 * they may run before DTO's constructor. Just use
		 * internal CPU-based implementation if DTO is not
		 * initialized yet.
		 */
		return dto_internal_memcpymove(dest, src, n);
	}

	if (!use_orig_func) {
		if (unlikely(dto_profiling) && dto_profiling_is_sample()) {
			dto_profiling_schedule_next();
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_START(collect_stats, st);
#endif
			orig_memmove(dest, src, n);
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_SAMPLED_CPU_END(collect_stats, st, et, MEMMOVE, n);
#endif
			return ret;
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		is_overlapping = dto_memcpymove(dest, src, n, 0, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMMOVE, n, is_overlapping, thr_bytes_completed, result);
		if (unlikely(collect_stats))
			update_submit_poll_stats(MEMMOVE, orig_n, thr_submit_ns, thr_poll_ns);
		if (unlikely(collect_stats) && thr_cpu_fraction_bytes > 0)
			update_stats(MEMMOVE, orig_n, false, thr_cpu_fraction_bytes, thr_cpu_fraction_ns, STDC_CALL_CPU_FRACTION, 0);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			if (thr_comp.result == 0) {
				dest = (void *)((uint64_t)dest + thr_bytes_completed);
				src = (const void *)((uint64_t)src + thr_bytes_completed);
			}
#ifdef DTO_STATS_SUPPORT
			cpu_fallback_group = STDC_CALL_DSA_FAILED;
#endif
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memmove(dest, src, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMMOVE, n, orig_n, cpu_fallback_group);
#endif
	}
	return ret;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
	int result = 0;
	int ret;
	int use_orig_func = USE_ORIG_FUNC(n, dto_dsa_memcmp);
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	int cpu_fallback_group = STDC_CALL_MIN_SIZE;
#endif

	if (unlikely(dto_initialized == 0)) {
		/* If there are other constructors in the same binary,
		 * they may run before DTO's constructor. Just use
		 * internal CPU-based implementation if DTO is not
		 * initialized yet.
		 */
		return dto_internal_memcmp(s1, s2, n);
	}

	if (!use_orig_func) {
		if (unlikely(dto_profiling) && dto_profiling_is_sample()) {
			dto_profiling_schedule_next();
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_START(collect_stats, st);
#endif
			ret = orig_memcmp(s1, s2, n);
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_SAMPLED_CPU_END(collect_stats, st, et, MEMCMP, n);
#endif
			return ret;
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		ret = dto_memcmp(s1, s2, n, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCMP, n, false, thr_bytes_completed, result);
		if (unlikely(collect_stats))
			update_submit_poll_stats(MEMCMP, orig_n, thr_submit_ns, thr_poll_ns);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			s1 = (const void *)((uint64_t)s1 + thr_bytes_completed);
			s2 = (const void *)((uint64_t)s2 + thr_bytes_completed);
#ifdef DTO_STATS_SUPPORT
			cpu_fallback_group = STDC_CALL_DSA_FAILED;
#endif
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		ret = orig_memcmp(s1, s2, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMCMP, n, orig_n, cpu_fallback_group);
#endif
	}
	return ret;
}

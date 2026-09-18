/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
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
#include "dto.h"
#include <nmmintrin.h>  // For _mm_crc32_u32 etc.
#include <sys/syscall.h>
#include <signal.h>
#include <limits.h>

/* Raw futex opcodes. <linux/futex.h> is deliberately not included: its
 * __kernel_timespec definitions collide with glibc's struct timespec on some
 * kernel-header versions, and only these two private-flag operations are
 * needed. Values are FUTEX_WAIT|FUTEX_PRIVATE_FLAG and
 * FUTEX_WAKE|FUTEX_PRIVATE_FLAG. */
#define DTO_FUTEX_WAIT_PRIVATE 128
#define DTO_FUTEX_WAKE_PRIVATE 129

/* rel is a RELATIVE CLOCK_MONOTONIC timeout, which is what plain FUTEX_WAIT
 * takes. Returns -1/errno on failure; EAGAIN means *w != val, i.e. the value
 * we were going to sleep on has already moved on and there is nothing to
 * wait for. That kernel-side compare is what makes the wake un-loseable. */
static inline int agg_futex_wait(_Atomic uint32_t *w, uint32_t val,
	const struct timespec *rel)
{
	return syscall(SYS_futex, (uint32_t *)w, DTO_FUTEX_WAIT_PRIVATE,
		val, rel, NULL, 0);
}

/* n > 1 is unreachable by construction, and re-proposing a batched wake is a
 * dead end that has already been costed. FUTEX_WAKE's count applies to ONE
 * address, and every armed slot has its own futex word (seq) by design, so
 * there is no address that names k waiters. The variant that does -- a shared
 * per-shard generation word that W blocked workers all wait on -- wakes all W
 * to retire k completions and loses unless k/W > ~0.6, and it would need a
 * SECOND futex word inside the validated block loop. The arithmetic that
 * forecloses it: per-completion poller cost is ~1.3us of FUTEX_WAKE against
 * ~1.7us per SWEEP (amortized over every completion in the pass), so
 * retire_rate = k / (1.7 + 1.3k) us^-1 with an asymptote of 1/1.3us = 770K/s
 * -- and the measured 450-750K/s sits on it. The poller is 75-90% syscall, so
 * the only linear lever is the NUMBER OF SYSCALL ISSUERS. That is what
 * DTO_AGG_POLLERS is. */
static inline int agg_futex_wake(_Atomic uint32_t *w, int n)
{
	return syscall(SYS_futex, (uint32_t *)w, DTO_FUTEX_WAKE_PRIVATE,
		n, NULL, NULL, 0);
}

#define likely(x)       __builtin_expect((x), 1)
#define unlikely(x)     __builtin_expect((x), 0)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// DSA capabilities
#define GENCAP_CC_MEMORY  0x4

/* DSA CRC Generation / Copy with CRC use the RFC3720 convention by default:
 * the seed and the output CRC are inverted (and reflected into the device's
 * internal bit order) by the hardware. crc32c_hw() computes the raw
 * (reflected-domain, no inversion) CRC32C, i.e.:
 *
 *     crc_val = ~crc32c_hw_with_seed(data, ~crc_seed)
 *
 * To make the device return exactly crc32c_hw(data) (seed 0), submit with
 * crc_seed = 0xFFFFFFFF and invert the returned crc_val. */
#define DSA_CRC_SEED_FOR_RAW 0xFFFFFFFFu
#define DSA_CRC_VAL_TO_RAW(v) ((uint32_t)~(uint32_t)(v))

#define UMWAIT_DELAY_DEFAULT 100000

#define C01_STATE 1
#define C02_STATE 0

#define USE_ORIG_FUNC(n, use_dsa) (use_std_lib_calls == 1 || !use_dsa || thr_dsa_disabled || (n*(100-cpu_size_fraction)/100) < dsa_min_size)
#define TS_NS(s, e) (((e.tv_sec*1000000000) + e.tv_nsec) - ((s.tv_sec*1000000000) + s.tv_nsec))

/* Maximum WQs that DTO will use. It is rather an arbitrary limit
 * to keep things simple and avoid having to dynamically allocate memory.
 * Allocating memory dynamically may create cyclic dependency and may cause
 * a hang (e.g., memset --> malloc --> alloc library calls memset --> memset)
 */
#define WQ_MMAPPED 1
#define MAX_WQS 32
#define MAX_NUMA_NODES 32
#define MAX_PAGES_PER_CALL 256
#define DTO_DEFAULT_MIN_SIZE 32768
#define DTO_DEFAULT_USLEEP 20
#define DTO_INITIALIZED 0
#define DTO_INITIALIZING 1
#define WQ_PATH 24 // max length of wq path string, e.g., /dev/dsa/wq0.0

#define NSEC_PER_SEC (1000000000)
#define MSEC_PER_SEC (1000)
#define NSEC_PER_MSEC (NSEC_PER_SEC/MSEC_PER_SEC)

// thread specific variables
static __thread struct dsa_hw_desc thr_desc;
/* The completion record for synchronous single-descriptor ops is reached
 * through a TLS pointer rather than being a TLS object itself. Under
 * WAIT_AGGREGATOR the pointer is redirected at a static per-slot record so
 * that the poller thread, which reads other threads' completion records,
 * never dereferences a TLS block or stack frame that glibc can unmap at
 * thread exit. For every other wait method it points at the TLS record and
 * the behavior is bit-for-bit unchanged.
 * thr_comp is a MACRO: &thr_comp, thr_comp.status, ... all still compile.
 * Never declare a local object named thr_comp. */
static __thread struct dsa_completion_record thr_comp_tls __attribute__((aligned(32)));
static __thread struct dsa_completion_record *thr_compp;

static __always_inline struct dsa_completion_record *dto_compp(void)
{
	/* A TLS address is not a constant expression, so this cannot be a
	 * static initializer. */
	if (unlikely(thr_compp == NULL))
		thr_compp = &thr_comp_tls;
	return thr_compp;
}
#define thr_comp (*dto_compp())
static __thread uint64_t thr_bytes_completed;
static __thread int16_t wq_index = -1;
static __thread uint8_t thr_dsa_disabled;  /* 1 = thread exceeded max threads limit */

// batch operation variables (thread-local for concurrent batch operations)
#define MAX_BATCH_DESCS 64
static __thread struct dsa_hw_desc thr_batch_descs[MAX_BATCH_DESCS] __attribute__((aligned(64)));
static __thread struct dsa_completion_record thr_batch_comp __attribute__((aligned(32)));
static __thread struct dsa_completion_record thr_batch_sub_comps[MAX_BATCH_DESCS] __attribute__((aligned(32)));

// original std memory functions
static void * (*orig_memset)(void *s, int c, size_t n);
static void * (*orig_memcpy)(void *dest, const void *src, size_t n);
static void * (*orig_memmove)(void *dest, const void *src, size_t n);
static int (*orig_memcmp)(const void *s1, const void *s2, size_t n);

struct dto_wq {
	int wq_fd;
	bool wq_mmapped;
	void *wq_portal;
	struct accfg_wq *acc_wq;
	uint64_t dsa_gencap;
	int wq_size;
	uint32_t max_transfer_size;
	char wq_path[WQ_PATH];
} __attribute__((aligned(64)));

struct dto_device {
	struct dto_wq* wqs[MAX_WQS];
	uint8_t num_wqs;
	atomic_uchar next_wq;
};

enum wait_options {
	WAIT_BUSYPOLL = 0,
	WAIT_UMWAIT,
	WAIT_YIELD,
	WAIT_TPAUSE,
        WAIT_SLEEP,
	WAIT_SPINYIELD,
	WAIT_AGGREGATOR
};

enum numa_aware {
	NA_NONE = 0,
	NA_BUFFER_CENTRIC,
	NA_CPU_CENTRIC,
	NA_LAST_ENTRY
};

static const char * const numa_aware_names[] = {
	[NA_NONE] = "none",
	[NA_BUFFER_CENTRIC] = "buffer-centric",
	[NA_CPU_CENTRIC] = "cpu-centric"
};

// global workqueue variables
static struct dto_wq wqs[MAX_WQS];
static struct dto_device* devices[MAX_NUMA_NODES];
static atomic_int num_threads;
static uint8_t num_wqs;
static uint8_t max_wqs_supported;
static atomic_uchar next_wq;
static atomic_uchar memset_pages_rr;
static atomic_uchar dto_initialized;
static atomic_uchar dto_initializing;
static uint8_t use_std_lib_calls;
static int dsa_max_threads;  /* 0 = no limit */

/* TinyLFU-style admission for the DSA submitter slots
 * (DTO_DSA_ADMISSION=lfu). The dsa_max_threads slots form a cache of
 * threads: admission compares a challenger's eligible-op frequency (exact
 * table -- thread numbers are dense) against the coldest slot holder's, and
 * periodic halving ages idle holders out. The default (fcfs) keeps the
 * original first-come permanent grant. */
enum dsa_admission_mode { ADMIT_FCFS = 0, ADMIT_LFU };
static int dsa_admission = ADMIT_FCFS;

#define DSA_SLOTS_MAX 1024
#define FREQ_TABLE_SIZE 4096
#define FREQ_MAX 255
#define ADMIT_APPLY_PERIOD 64
#define AGE_WINDOW_OPS (1u << 20)

struct dsa_slot {
	_Atomic int32_t owner; /* thread number, -1 = free */
	_Atomic uint32_t gen;  /* bumped on every ownership change */
	_Atomic uint8_t hinted; /* owner held an activity hint on last submit */
};
static struct dsa_slot dsa_slots[DSA_SLOTS_MAX];

/* Ticket-aware admission (DTO_DSA_ADMISSION=ticket): the application marks
 * threads that are inside a phase producing offload-eligible work via
 * dto_thread_active() -- for mongod, WiredTiger execution-control ticket
 * acquire/release. Hint possession replaces frequency inference for those
 * threads: hinted challengers admit immediately and preferentially evict
 * unhinted holders. Unhinted threads (storage daemons, foreign apps) keep
 * the full TinyLFU path over whatever slots remain, so a process that never
 * calls the hint behaves exactly as =lfu. The hint lingers for a few ops
 * after release so per-transaction ticket cycling doesn't thrash slots. */
static int ticket_mode;
#define HINT_LINGER_OPS 512
static __thread int32_t thr_hint;        /* nesting count of active hints */
static __thread uint32_t thr_hint_linger; /* eligible ops still hinted */
static __thread uint8_t thr_slot_hint_mirror; /* last value stored to slot */

/* One frequency counter per cacheline: thousands of threads bump these, and
 * unpadded adjacent counters turned the table into 64 contended lines under
 * the 16-instance overload. 256KB of bss buys zero false sharing. */
struct freq_slot {
	_Atomic uint8_t v;
	char pad[63];
};
static struct freq_slot thr_freq_tab[FREQ_TABLE_SIZE]
	__attribute__((aligned(64)));
static _Atomic uint32_t admit_eligible_ops;
static _Atomic uint32_t admit_age_epoch;

/* Frequency updates are sampled 1-in-freq_sample per thread: admission is a
 * relative comparison, so uniform sampling preserves ordering while cutting
 * shared-line write traffic by the sampling factor. The aging trigger rides
 * the same sampled branch, so non-sampled ops touch no shared state at all.
 * A global gap between challenges bounds the victim-scan rate process-wide
 * no matter how many slotless threads are hot. */
static int freq_sample = 8;            /* DTO_LFU_FREQ_SAMPLE */

/* Load shed (DTO_SHED=1): a sampled scheduling probe (rdtsc around
 * sched_yield) measures time-to-reschedule -- the quantity that decides
 * whether synchronous offload is profitable at all. When the host is
 * oversubscribed past the threshold, the shed level rises and ops below
 * SHED_BASE_BYTES << level fall back to the CPU (small ops first: worst
 * wait-to-work ratio). The probe is offload-independent, so recovery is
 * automatic: levels step back down as yield latency subsides. */
static int dto_shed;
static _Atomic int shed_level;
#define SHED_LEVEL_MAX 8
#define SHED_BASE_BYTES 16384
#define SHED_PROBE_PERIOD 256          /* per-thread: probe 1 in 256 */
#define SHED_PROBE_WINDOW 64           /* probes per level decision */
static int shed_hi_us = 20;            /* DTO_SHED_HI_US: escalate above */
static int shed_lo_us = 5;             /* DTO_SHED_LO_US: recover below */
static uint64_t shed_hi_cycles = 40000, shed_lo_cycles = 10000;
static unsigned int tsc_khz = 2000000;
static _Atomic uint64_t shed_probe_cycles;
static _Atomic uint32_t shed_probe_count;
static __thread uint16_t thr_probe_ctr;
static uint32_t age_window_samples = (AGE_WINDOW_OPS >> 3);
#define CHALLENGE_GLOBAL_GAP 8         /* sampled ops between challenges */
static _Atomic uint32_t challenge_stamp;
static __thread uint16_t thr_bump_ctr;

/* Dynamic slot-count tuning (DTO_LFU_AUTO_K=1): the same sampled-waits
 * signal the cpu_size_fraction heuristic uses, driving K instead. Long
 * average waits mean the device side is saturated by the current
 * submitters, so the working set shrinks; short waits mean headroom, so it
 * grows. lfu_k is the live bound the victim scan and the holder fast path
 * both honor. */
static int lfu_auto_k;
static _Atomic int lfu_k;
static int lfu_k_min = 8;
static int lfu_k_max = 512;
#define LFU_K_STEP 8
/* Signal window: ENQCMD WQ-full rejections per submit. Unlike sampled wait
 * counts, retry rate is pure device backpressure -- immune to scheduler
 * delay, which under thread oversubscription inflates yield-wait counts and
 * misreads CPU load as device saturation. */
#define LFU_K_WINDOW 4096
static atomic_ullong k_win_submits;
static atomic_ullong k_win_retries;

/* Latency-trend signal (DTO_LFU_AUTO_K=2): sampled rdtsc timing of
 * descriptors, byte-normalized (cycles per KB) and compared window-over-
 * window against an EWMA reference. Rising latency -> shrink (the queueing
 * knee), flat or falling -> probe upward. The relative comparison cancels
 * the constant scheduler tax that poisons absolute wait counts under
 * thread oversubscription. */
#define LAT_WINDOW 256
#define LAT_SAMPLE_PERIOD 64
static int lat_eps_num = 115;          /* rising = > ref * eps / 100 */
static _Atomic uint64_t lat_win_cycles;
static _Atomic uint64_t lat_win_bytes;
static _Atomic uint32_t lat_win_count;
static uint64_t lat_ref_cpkb;          /* EWMA, fixed-point x256; closer-only */
static int lat_freeze;                 /* windows to skip after a shrink */
static __thread uint16_t thr_lat_ctr;
static __thread uint64_t thr_lat_t0;
static __thread uint32_t thr_lat_bytes; /* 0 = not timing this descriptor */

static __thread int32_t thr_num = -1;
static __thread int16_t thr_slot = -1;
static __thread uint32_t thr_slot_gen;
static __thread uint16_t thr_backoff;
static enum numa_aware is_numa_aware;
static size_t dsa_min_size = DTO_DEFAULT_MIN_SIZE;
/* Gate for the explicit CRC API (dto_crc, dto_memcpy_crc_async); when unset
 * it follows dsa_min_size. See DTO_CRC_MIN_BYTES. */
#define CRC_MIN_SIZE_UNSET ((size_t)-1)
static size_t crc_min_size = CRC_MIN_SIZE_UNSET;
static inline size_t crc_dsa_min_size(void)
{
	return crc_min_size == CRC_MIN_SIZE_UNSET ? dsa_min_size : crc_min_size;
}
static int wait_method = WAIT_BUSYPOLL;
static size_t cpu_size_fraction;   // range of values is 0 to 99

static uint8_t dto_dsa_memcpy = 1;
static uint8_t dto_dsa_memmove = 1;
static uint8_t dto_dsa_memset = 1;
static uint8_t dto_dsa_memcmp = 1;

static uint8_t dto_dsa_cc = 1;
static uint8_t dto_dsa_bof = 1;
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

static uint8_t fork_handler_registered;

enum memop {
	MEMSET = 0x0,
	MEMCOPY,
	MEMMOVE_INTERNAL,
	MEMCOPY_ASYNC,
	MEMMOVE,
	MEMCMP,
	BATCH_COPY,
	CRC,
	MAX_MEMOP,
};

static const char * const memop_names[] = {
	[MEMSET] = "set",
	[MEMCOPY] = "cpy",
	[MEMCOPY_ASYNC] = "acpy",
	[MEMMOVE_INTERNAL] = "movi",
	[MEMMOVE] = "mov",
	[MEMCMP] = "cmp",
	[BATCH_COPY] = "batch",
	[CRC] = "crc"
};

// memory stats
#define HIST_BUCKET_SIZE 4096
#define HIST_NO_BUCKETS 512
enum stat_group {
	STDC_CALL = 0x0,
	DSA_CALL_SUCCESS,
	DSA_CALL_FAILED,
	DSA_FAIL_CODES,
	MAX_STAT_GROUP
};

static const char * const stat_group_names[] = {
	[STDC_CALL] = "stdc calls",
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
        [WAIT_TPAUSE] = "tpause",
        [WAIT_SLEEP] = "sleep",
	[WAIT_SPINYIELD] = "spinyield",
	[WAIT_AGGREGATOR] = "aggregator"
};

static int collect_stats;
static char dto_log_path[PATH_MAX];
static int log_fd = -1;

#ifdef DTO_STATS_SUPPORT
static struct timespec dto_start_time;

#define DTO_COLLECT_STATS_START(cs, st)				\
	do {							\
		if (unlikely(cs)) {				\
			clock_gettime(CLOCK_BOOTTIME, &st);	\
		}						\
	} while (0)						\


#define DTO_COLLECT_STATS_DSA_END(cs, st, et, op, n, tbc, r)				\
	do {										\
		if (unlikely(cs)) {							\
			uint64_t t;							\
			clock_gettime(CLOCK_BOOTTIME, &et);				\
			t = (((et.tv_sec*1000000000) + et.tv_nsec) -			\
					((st.tv_sec*1000000000) + st.tv_nsec));		\
			if (unlikely(r != SUCCESS))					\
				update_stats(op, n, tbc, t, DSA_CALL_FAILED, r);	\
			else								\
				update_stats(op, n, tbc, t, DSA_CALL_SUCCESS, 0);	\
		}									\
	} while (0)									\

#define DTO_COLLECT_STATS_CPU_END(cs, st, et, op, n, orig_n)			\
	do {									\
		if (unlikely(cs)) {						\
			uint64_t t;						\
			clock_gettime(CLOCK_BOOTTIME, &et);			\
			t = (((et.tv_sec*1000000000) + et.tv_nsec) -		\
				((st.tv_sec*1000000000) + st.tv_nsec));		\
			update_stats(op, orig_n, n, t, STDC_CALL, 0);		\
		}								\
	} while (0)								\

/* Thread-local stats structure */
struct thread_stats {
	int op_counter[HIST_NO_BUCKETS][MAX_STAT_GROUP][MAX_MEMOP];
	unsigned long long bytes_counter[HIST_NO_BUCKETS][MAX_STAT_GROUP];
	unsigned long long lat_counter[HIST_NO_BUCKETS][MAX_STAT_GROUP][MAX_MEMOP];
	int fail_counter[HIST_NO_BUCKETS][MAX_FAILURES];
};

/* Thread-local pointer to heap-allocated stats */
static __thread struct thread_stats *tl_stats = NULL;

/* Global registry of all thread stats for aggregation */
#define MAX_STAT_THREADS 4096
static struct thread_stats *global_stats_registry[MAX_STAT_THREADS];
static atomic_int global_stats_count = 0;
static pthread_mutex_t stats_registry_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/* call initialize/cleanup functions when library is loaded/unloaded */
static int init_dto(void) __attribute__((constructor));
static void cleanup_dto(void) __attribute__((destructor));

static int umwait_support;

static enum {
	LOG_LEVEL_FATAL,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_TRACE
} log_levels;

#define LOG_FATAL(...) dto_log(LOG_LEVEL_FATAL, __VA_ARGS__)
#define LOG_ERROR(...) dto_log(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_TRACE(...) dto_log(LOG_LEVEL_TRACE, __VA_ARGS__)

static unsigned int log_level = LOG_LEVEL_FATAL;

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

/* Auto tune v2 */
#define KP 0.5
#define KI 0.1
#define SAMPLE_INTERVAL 10000
#define AUTO_ADJUST_KNOBS 1
#define AUTO_ADJUST_KNOBS_V2 2

#define AUTO_TUNE_V2_TARGET 1

static __thread uint64_t tl_num_descs = 0;
static __thread uint64_t tl_next_sample = 0;
static __thread uint64_t tl_integral = 0;
static __thread uint64_t tl_cpu_size_fraction = 0;

/* Auto tuning variables */
static atomic_ullong num_descs;
static atomic_ullong adjust_num_descs;
static atomic_ullong adjust_num_waits;
/* default waits are for yield because yield is default waiting method */
static double min_avg_waits = MIN_AVG_YIELD_WAITS;
static double max_avg_waits = MAX_AVG_YIELD_WAITS;
static uint8_t auto_adjust_knobs = 1;

extern char *__progname;

uint32_t crc32c_hw(const uint8_t* data, size_t len) {
    uint32_t crc = 0;  // Initial value, can be 0 or 0xFFFFFFFF depending on convention

    while (len >= sizeof(uint64_t)) {
        crc = _mm_crc32_u64(crc, *(uint64_t*)data);
        data += sizeof(uint64_t);
        len -= sizeof(uint64_t);
    }

    while (len >= sizeof(uint32_t)) {
        crc = _mm_crc32_u32(crc, *(uint32_t*)data);
        data += sizeof(uint32_t);
        len -= sizeof(uint32_t);
    }

    while (len--) {
        crc = _mm_crc32_u8(crc, *data++);
    }

    return crc;
}


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

/* ---------------------------------------------------------------------------
 * WAIT_AGGREGATOR
 *
 * One dedicated poller thread per process turns N spinning workers into one
 * spinner plus N futex-blocked workers, so the cores the workers were holding
 * during device time become available to other threads (or to another
 * process sharing the machine).
 *
 * A worker blocks immediately for transfers at or above DTO_AGG_BLOCK_KB and
 * spins to completion below it; either way the poller sweeps the armed slots
 * and, when the device has written a status byte, closes the slot with a CAS
 * and issues FUTEX_WAKE.
 *
 * The whole protocol rests on one monotonically increasing 32-bit word per
 * slot, seq, which is BOTH the armed flag (odd = armed) and the futex word:
 *
 *   - seq never decreases, not on release, not on claim by a new owner.
 *     Therefore a stale CAS left over from a previous op, or from a previous
 *     owner of the slot, always fails. That is the ABA answer for the CAS.
 *   - The poller CASes from the exact value it loaded for that slot, strictly
 *     BEFORE it wakes. A worker that reaches FUTEX_WAIT after the fact gets
 *     EAGAIN from the kernel's own compare, so the wakeup cannot be lost.
 *   - FUTEX_WAKE carries no compare value, so a delayed wake CAN land on a
 *     later arming of the same slot by a different owner. That is harmless
 *     but it is not prevented: it is absorbed by the next rule.
 *   - The loop condition is always *comp == 0, never the futex word: every
 *     wake is treated as possibly spurious or stale.
 * ------------------------------------------------------------------------- */

#define AGG_SLOTS_MAX 512

/* CONTROL line. Written by the owner (arm/disarm) and by the poller (CAS).
 * Deliberately not in the same cacheline as the completion record: the owner
 * dirties this line on every arm, and the device must not have to
 * snoop-invalidate a CPU-dirty line to land a completion. */
struct agg_slot {
	_Atomic uint32_t seq;	/* futex word AND armed flag; odd = armed,
				 * even = idle. NEVER reset. */
	_Atomic uint32_t owner;	/* 0 = free, 1 = owned */
	char pad[64 - 8];
} __attribute__((aligned(64)));
_Static_assert(sizeof(struct agg_slot) == 64, "agg_slot must be one line");

/* Owner-private diagnostics, deliberately NOT in struct agg_slot. The poller
 * loads every seq on every sweep and keeps those lines in S state; a counter
 * bump on the worker's fast path sharing that line would invalidate it and
 * turn the sweep into a stream of coherence misses. Nothing reads these until
 * shutdown. */
struct agg_stat {
	uint32_t block, spinhit, timeout, spincap, unknown, early;
	char pad[64 - 24];
} __attribute__((aligned(64)));
_Static_assert(sizeof(struct agg_stat) == 64, "agg_stat must be one line");
static struct agg_stat agg_stats[AGG_SLOTS_MAX];

/* DEVICE-WRITTEN line. The record sits at offset 0 of a 64B-aligned object,
 * which satisfies DSA's 32-byte completion-record alignment requirement. */
struct agg_rec {
	struct dsa_completion_record comp;	/* 32B */
	char pad[32];
} __attribute__((aligned(64)));

static struct agg_slot agg_slots[AGG_SLOTS_MAX];
static struct agg_rec agg_recs[AGG_SLOTS_MAX];

/* SHARDED POLLERS.
 *
 * A single poller retires only ~450-750K completions/s (see agg_futex_wake
 * for where that number comes from and why nothing but more syscall issuers
 * can move it). Below ~256KB the device produces completions faster than
 * that and throughput collapses to exactly the retire rate times the transfer
 * size. Read as a break-even transfer size: at 222 GB/s the device produces
 * 222e9/S completions/s, so one poller at ~700 kops/s breaks even at
 * S ~= 300KB, and P pollers at 300KB/P -- P=2 -> 150KB, P=4 -> 77KB.
 *
 * The table is therefore cut into P contiguous shards, each swept by its own
 * poller. Every piece of per-poller state below is split by WRITE FREQUENCY,
 * which also repairs a pre-existing false-sharing bug: agg_park, agg_hb,
 * agg_hi, agg_stop, agg_started and agg_degraded_until used to be six
 * consecutive unpadded statics in one 64B line, so the eligibility test --
 * which runs on EVERY offloaded op, including the spinning majority that
 * never arms -- shared a line with a counter the poller RMWs 450-750K times a
 * second. */
#define AGG_POLLERS_MAX 16

/* Read-mostly. Loaded on the eligibility test of every offloaded op in this
 * shard. Written only at start, at poller death and at teardown.
 *
 * lo/limit are IMMUTABLE once agg_started is published; the only writes are
 * the init-time orphan fold, which happens before publication. See agg_kick()
 * for why immutability is a correctness requirement and not a simplification. */
struct agg_shard {
	_Atomic uint32_t started;
	uint32_t lo, limit;		/* [lo, limit) */
	int node;			/* NUMA node, or -1 for "don't bind" */
	int created;			/* pthread_create returned 0 */
	_Atomic uint64_t degraded_until;	/* rdtsc deadline; watchdog */
	pthread_t tid;
} __attribute__((aligned(64)));
_Static_assert(sizeof(struct agg_shard) == 64, "agg_shard must be one line");

/* Poller-written every sweep. Its own line for exactly the reason agg_stats
 * is not inside struct agg_slot: a per-sweep store must not invalidate a line
 * the workers load on their fast path. */
struct agg_shard_hot {
	_Atomic uint32_t park;		/* futex word: 1 = this poller parked */
	_Atomic uint32_t hi;		/* highest claimed slot + 1, >= lo */
	_Atomic uint64_t hb;		/* ++ at the end of every sweep */
} __attribute__((aligned(64)));
_Static_assert(sizeof(struct agg_shard_hot) == 64,
	       "agg_shard_hot must be one line");

static struct agg_shard agg_shards[AGG_POLLERS_MAX];
static struct agg_shard_hot agg_hot[AGG_POLLERS_MAX];
static uint32_t agg_npollers = 1;	/* live shard count (P_eff) */
static uint32_t agg_pollers_per_node = 1;
/* Init-only map. Written in full before agg_started is published and never
 * again while a slot can be armed; the poller never reads it at all (it reads
 * its own lo/limit), so both sides compute the same mapping by construction
 * rather than by agreement. */
static uint8_t agg_slot_shard[AGG_SLOTS_MAX];
static cpu_set_t agg_shard_cpus[AGG_POLLERS_MAX];
static uint8_t agg_shard_cpus_valid[AGG_POLLERS_MAX];
static int agg_any_created;		/* at least one poller to join */

/* PROCESS-WIDE, deliberately not sharded. One word, read-mostly, written
 * once, and teardown is by definition all-shards; sharding it would add
 * stores to the one path that must stay simple and would make the bounded
 * join deadline harder to bound. */
static _Atomic uint32_t agg_stop;
/* The claim gate in get_wq(), meaning "the pool is final and the table is
 * open". Set once by init_dto() after the create loop and cleared by
 * cleanup_dto(). No poller writes it: with P pollers, "the last one out" is
 * not a condition any single poller can evaluate, and agg_stop is already the
 * flag every worker tests. Per-shard liveness lives in agg_shards[s].started. */
static _Atomic uint32_t agg_started;
static pthread_key_t agg_key;
static int agg_key_ready;		/* keys survive fork: create once */
static pthread_attr_t agg_attr;		/* built once; fork-child safe */
static int agg_attr_ready;
static uint32_t agg_nodes = 1;		/* NUMA nodes to partition slots over */

static uint32_t agg_nslots = 64;	/* live table size */
static uint64_t agg_postarm_cyc, agg_idle_cyc, agg_idle_min_cyc,
		agg_degrade_cyc, agg_dead_cyc;
static uint32_t agg_to_us = 100, agg_to_max_us = 1000;
static int agg_cpu = -1, agg_rt;

/* Read-mostly after init, loaded on the fast path of every offloaded op.
 * Given their own 64B line: a poller stores its agg_hot[] heartbeat on every
 * sweep at 450-750K/s, and the gate compare must never become a coherence
 * miss. The
 * aligned attribute opens a fresh line, so nothing declared above can share
 * with these. */
static uint32_t agg_block_bytes __attribute__((aligned(64))) = 64u << 10;
static uint64_t agg_spin_cap_cyc;
static _Atomic uint32_t agg_spincap_logged;	/* one log line per process */
static _Atomic uint32_t agg_full_logged;	/* one log line per process */

/* Claim sequencer. Bumped once per successful-or-failed claim attempt, i.e.
 * about once per offloading thread, never on a wait path. It exists because
 * the claimed slot index now SELECTS THE POLLER: with one poller the claim's
 * placement inside the table was a NUMA detail, with P pollers it is the load
 * balance, so the start position has to come from something that actually
 * varies. thr_num does not -- it is assigned only by get_wq_lfu(), which
 * get_wq_inner() reaches only under DTO_DSA_ADMISSION=lfu, so in the default
 * fcfs mode every thread hashed from -1 and linear-probed from the same slot,
 * packing every claim into the lowest shard. */
static _Atomic uint32_t agg_claim_ticket;

/* Call counters for the public wait API. The consumer that motivated it
 * (WiredTiger) resolves these by dlsym and silently keeps its old poll loop
 * when they are absent, so "did the new path engage" is otherwise
 * unfalsifiable from outside the process. */
static _Atomic uint64_t dto_wait_calls_async, dto_wait_calls_batch,
		        dto_wait_entered_async, dto_wait_entered_batch;
/* Latched one-shot, not reported at exit: mongod is terminated by a signal and
 * never runs cleanup_dto, so a shutdown-only report is invisible in exactly the
 * process this API exists for. */
static _Atomic uint32_t dto_wait_logged_async, dto_wait_logged_batch;
static _Atomic uint32_t dto_sub_logged_batch, dto_sub_logged_crc;

static __thread int32_t thr_agg_slot = -1;	/* -1 unclaimed, -2 table full,
						 * -3 thread is exiting */
static __thread uint32_t thr_agg_retry;		/* re-claim backoff counter */
/* Resolved once, at claim time, from agg_slot_shard[]. Never computed on a
 * wait path and never on the spin path. */
static __thread struct agg_shard *thr_agg_sh;
static __thread struct agg_shard_hot *thr_agg_hot;

/* Measured DSA cost model, used at INIT ONLY -- to floor the spin cap against
 * the size gate and to log the effective configuration. It is never consulted
 * on a wait path, so a stale calibration on future silicon misplaces a knob
 * and can never produce a wrong wait decision. Re-measure both constants on
 * new silicon. */
#define AGG_DSA_FIXED_NS	700ULL		/* 0.70us fixed */
#define AGG_DSA_BW_MB_PER_S	56600ULL	/* 56.6 GB/s */

static inline uint64_t agg_model_ns(uint64_t bytes)
{
	return AGG_DSA_FIXED_NS + bytes * 1000ULL / AGG_DSA_BW_MB_PER_S;
}

static int agg_getenv_int(const char *name, int def)
{
	const char *e = getenv(name);
	long v;

	if (e == NULL || *e == '\0')
		return def;
	errno = 0;
	v = strtol(e, NULL, 10);
	if (errno)
		return def;
	return (int)v;
}

static inline uint64_t agg_us_to_cyc(uint64_t us)
{
	return us * tsc_khz / 1000;
}

static inline int agg_clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Reinitialize DTO in the child process. */
static void child (void)
{
#ifdef DTO_STATS_SUPPORT
	/* Reset the thread-local stats pointer and global registry */
	tl_stats = NULL;

	pthread_mutex_lock(&stats_registry_lock);
	/* Free old stats structures */
	int count = atomic_load(&global_stats_count);
	for (int i = 0; i < count && i < MAX_STAT_THREADS; ++i) {
		free(global_stats_registry[i]);
		global_stats_registry[i] = NULL;
	}
	global_stats_count = 0;
	pthread_mutex_unlock(&stats_registry_lock);
#endif
	dto_initializing = 0;
	dto_initialized = 0;
	log_fd = -1;

	/* The poller thread did not survive the fork. Resetting seq to zero is
	 * safe here and only here: the child is single-threaded at this
	 * instant, so no stale CAS and no stale wake can exist to be confused
	 * by the restart. Explicit stores, never memset(), which is
	 * interposed and whose orig_memset may still be NULL. */
	for (uint32_t i = 0; i < AGG_SLOTS_MAX; i++) {
		atomic_store_explicit(&agg_slots[i].owner, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_slots[i].seq, 0,
		    memory_order_relaxed);
		agg_recs[i].comp.status = 0;
		/* agg_stats is the only thing cleanup_dto() dumps and it is
		 * the instrument the bring-up gate reads. Inherited, the
		 * child's dump would be parent+child summed and labelled with
		 * the CHILD's slot-to-shard map, so a parent-side timeout or
		 * spincap would read as a child-side watchdog event that
		 * never happened. */
		agg_stats[i].block = 0;
		agg_stats[i].spinhit = 0;
		agg_stats[i].timeout = 0;
		agg_stats[i].spincap = 0;
		agg_stats[i].unknown = 0;
		agg_stats[i].early = 0;
	}
	atomic_store_explicit(&agg_stop, 0, memory_order_relaxed);
	atomic_store_explicit(&agg_started, 0, memory_order_relaxed);
	/* One-shot log latches, or the child never reports its own first
	 * occurrence of an event the parent happened to hit already. */
	atomic_store_explicit(&agg_spincap_logged, 0, memory_order_relaxed);
	atomic_store_explicit(&agg_full_logged, 0, memory_order_relaxed);
	atomic_store_explicit(&agg_claim_ticket, 0, memory_order_relaxed);
	/* AGG_POLLERS_MAX, not the live agg_npollers: the child re-runs the
	 * whole knob block and DTO_AGG_POLLERS can come back LARGER than the
	 * parent's, so a shard left live here would survive holding a
	 * pthread_t that names a stranger -- the same hazard that forces a
	 * single poller's pthread_t to be cleared here, now applied to
	 * sixteen of them. lo/limit/hi are
	 * re-seeded by the partition computation when init_dto() re-runs;
	 * zeroing them here is a clean slate, not a valid state.
	 * agg_slot_shard[] needs no clearing: it is rewritten in full before
	 * agg_started is published. */
	for (uint32_t s = 0; s < AGG_POLLERS_MAX; s++) {
		atomic_store_explicit(&agg_shards[s].started, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_shards[s].degraded_until, 0,
		    memory_order_relaxed);
		agg_shards[s].tid = (pthread_t)0;
		agg_shards[s].created = 0;
		agg_shards[s].lo = 0;
		agg_shards[s].limit = 0;
		agg_shards[s].node = -1;
		agg_shard_cpus_valid[s] = 0;
		atomic_store_explicit(&agg_hot[s].park, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_hot[s].hb, 0, memory_order_relaxed);
		atomic_store_explicit(&agg_hot[s].hi, 0, memory_order_relaxed);
	}
	agg_npollers = 1;
	agg_pollers_per_node = 1;
	agg_any_created = 0;
	thr_agg_slot = -1;
	thr_agg_sh = NULL;
	thr_agg_hot = NULL;
	thr_compp = &thr_comp_tls;
	/* Clear this thread's key value, or its destructor would later
	 * release a slot that a different child thread owns. The key itself
	 * survives the fork and must not be re-created. */
	if (agg_key_ready)
		pthread_setspecific(agg_key, NULL);

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

static __always_inline void umonitor(const volatile void *addr)
{
	asm volatile(".byte 0xf3, 0x48, 0x0f, 0xae, 0xf0" : : "a"(addr));
}

static __always_inline int umwait(unsigned long timeout, unsigned int state)
{
	uint8_t r;
	uint32_t timeout_low = (uint32_t)timeout;
	uint32_t timeout_high = (uint32_t)(timeout >> 32);

	asm volatile(".byte 0xf2, 0x48, 0x0f, 0xae, 0xf1\t\n"
		"setc %0\t\n"
		: "=r"(r)
		: "c"(state), "a"(timeout_low), "d"(timeout_high));
	return r;
}

static inline void tpause(unsigned long timeout, unsigned int state)
{
    uint32_t timeout_low = (uint32_t)(timeout);
    uint32_t timeout_high = (uint32_t)(timeout >> 32);
    asm volatile(".byte 0x66, 0x0f, 0xae, 0xf1\t\n"
             :
             : "c"(state), "a"(timeout_low), "d"(timeout_high));
}


static __always_inline void dsa_wait_yield(const volatile uint8_t *comp)
{
	while (*comp == 0) {
	    sched_yield();
	}
}

/* Spin briefly (long enough to cover the device time of a typical small
 * operation), then yield. Under an uncontended sprint the completion lands
 * during the spin and the op pays nothing; under contention the thread
 * donates its CPU like plain yield. This closes the gap where a yield's
 * reschedule latency -- a full scheduling quantum when every thread is
 * runnable -- was charged to each offloaded op. */
static uint64_t spinyield_cycles = 16000;   /* ~8us at 2GHz; see init */

static __always_inline void dsa_wait_spinyield(const volatile uint8_t *comp)
{
	uint64_t deadline = __rdtsc() + spinyield_cycles;

	while (*comp == 0) {
		if (__rdtsc() < deadline)
			_mm_pause();
		else
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


/* pthread_key destructor: hand the slot back when the owning thread exits.
 * A thread cannot exit from inside its own wait, so seq is even here.
 * Nothing needs to be quiesced and nothing is freed: the poller only ever
 * touches agg_slots[] and agg_recs[], which live in .bss for the life of the
 * process. */
static void agg_slot_release(void *v)
{
	uint32_t i = (uint32_t)(uintptr_t)v - 1;

	if (i >= AGG_SLOTS_MAX)
		return;
	thr_compp = &thr_comp_tls;
	/* -3, not -1: glibc runs key destructors in up to four rounds, and a
	 * destructor registered by any other library may call an interposed
	 * mem* function. That would reach get_wq() -> agg_claim_slot() and
	 * hand this dying thread a second slot, whose completion record it
	 * would then point thr_compp at -- after this key value has already
	 * been consumed, so nothing would ever release it. */
	thr_agg_slot = -3;
	thr_agg_retry = 0;
	/* Cleared for the same reason the slot index goes to -3: a destructor
	 * registered by another library can reach get_wq() and must not find a
	 * stale shard pointer sitting beside a dead slot index. */
	thr_agg_sh = NULL;
	thr_agg_hot = NULL;
	atomic_store_explicit(&agg_slots[i].owner, 0, memory_order_release);
}

static int agg_claim_slot(void)
{
	uint32_t tk = atomic_fetch_add_explicit(&agg_claim_ticket, 1,
	    memory_order_relaxed);
	uint32_t base = 0, span = agg_nslots, start, k, ppn;
	struct agg_shard_hot *hot;

	/* Prefer the region of the table belonging to this thread's NUMA
	 * node. Slots are 64B and agg_recs is plain .bss, so 64 completion
	 * records share a 4KB page: with an unpartitioned search one page is
	 * first-touched by whichever node claims first and every other node's
	 * threads then take a cross-socket write (device) and read (worker)
	 * on the one byte whose latency this whole path is built around.
	 * Partitioned, a page is only ever claimed by same-node threads, so
	 * first touch places it on their node. */
	if (agg_nodes > 1) {
		int cpu = sched_getcpu();
		int nd = cpu >= 0 ? numa_node_of_cpu(cpu) : -1;

		if (nd >= 0) {
			span = agg_nslots / agg_nodes;
			if (span == 0)
				span = 1;
			base = ((uint32_t)nd % agg_nodes) * span;
		}
	}
	/* Spread SHARD-FIRST inside the node region, not slot-first: the
	 * region is cut into agg_pollers_per_node contiguous shards, so
	 * consecutive claims must step by a whole shard width to land under
	 * distinct pollers. Slot-first (start = base + tk % span) fills shard
	 * 0 completely before touching shard 1, which is what leaves the
	 * other pollers sweeping empty ranges and parked while one of them
	 * carries the whole process at the P == 1 retire rate.
	 * This is a START HINT only -- the probe below still walks the node
	 * region and then the whole table, so no slot becomes unreachable. */
	ppn = agg_pollers_per_node;
	if (ppn == 0 || ppn > span)
		ppn = 1;
	start = base + ((tk % ppn) * (span / ppn) + tk / ppn) % span;

	for (k = 0; k < agg_nslots; k++) {
		/* The node's own region first, then the rest of the table:
		 * a slot on the wrong node still beats no slot at all. */
		uint32_t i = k < span ? base + (start - base + k) % span
				      : (base + k) % agg_nslots;
		uint32_t z = 0;

		if (atomic_compare_exchange_strong_explicit(&agg_slots[i].owner,
				&z, 1u, memory_order_acq_rel,
				memory_order_relaxed)) {
			/* seq is never RESET across owners -- monotonicity is
			 * what makes a stale CAS or wake from the previous
			 * owner inert -- but the even-means-idle parity the
			 * poller filters on is load-bearing, so normalize it
			 * upwards here rather than assume the previous owner
			 * left it even. */
			uint32_t sq = atomic_load_explicit(&agg_slots[i].seq,
			    memory_order_relaxed);

			if (sq & 1u)
				atomic_store_explicit(&agg_slots[i].seq,
				    sq + 1, memory_order_relaxed);
			agg_recs[i].comp.status = 0;
			thr_compp = &agg_recs[i].comp;	/* device lands here */
			thr_agg_slot = (int32_t)i;
			/* One byte load, once per thread. The shard that owns
			 * this slot is a property of the slot, not of the
			 * thread, so no wait path ever recomputes it. */
			hot = &agg_hot[agg_slot_shard[i]];
			thr_agg_sh = &agg_shards[agg_slot_shard[i]];
			thr_agg_hot = hot;
			pthread_setspecific(agg_key,
			    (void *)(uintptr_t)(i + 1));
			/* CAS-max THIS SHARD's hi. seq_cst on success, not
			 * release: the poller's park-time load of hi decides
			 * whether slot i is inside the range it re-checks
			 * before sleeping, so this store has to join the
			 * single total order S that already carries the arm
			 * and the park store. release/acquire on hi orders
			 * NOTHING against the park word, which leaves C11
			 * permitting a poller to load a stale hi that excludes
			 * i while the worker's Dekker load of park still sees
			 * 0 -- a lost wakeup, saved today only by x86's LOCK
			 * XCHG on the arm draining the store buffer. At
			 * seq_cst a poller that missed this CAS has
			 * hi-load <S hi-CAS <S arm <S park-load together with
			 * park-store <S hi-load, so park-store <S park-load
			 * and the worker necessarily observes park == 1 and
			 * kicks. Strengthening only, and free on x86: the CAS
			 * is already a locked RMW.
			 * INVARIANT: this CAS-max must stay ordered BEFORE the
			 * arm in dsa_wait_aggregator(). */
			for (;;) {
				uint32_t h = atomic_load_explicit(&hot->hi,
				    memory_order_relaxed);

				if (h >= i + 1)
					break;
				if (atomic_compare_exchange_weak_explicit(
				    &hot->hi, &h, i + 1, memory_order_seq_cst,
				    memory_order_relaxed))
					break;
			}
			return 0;
		}
	}
	thr_agg_slot = -2;
	thr_agg_retry = 4096;
	/* A full table is indistinguishable from a working aggregator in the
	 * log otherwise: the pollers are up, agg_started is set, and every
	 * unslotted thread silently spinyields forever under the re-claim
	 * backoff -- i.e. the entire CPU saving this wait method exists for
	 * is gone with no diagnostic. Once per process; the backoff makes
	 * this path recur. */
	{
		uint32_t z = 0;

		if (atomic_compare_exchange_strong_explicit(&agg_full_logged,
		    &z, 1u, memory_order_relaxed, memory_order_relaxed))
			LOG_ERROR("aggregator: all %u slots are owned; further threads fall back to spinyield until one is released (raise DTO_AGG_SLOTS, max %d)\n",
			    agg_nslots, AGG_SLOTS_MAX);
	}
	return -1;
}

/* Called by a worker immediately after arming, on ITS OWN shard's park word.
 * Steady state costs one load of a read-mostly line: no RMW, no syscall.
 *
 * Dekker pair with agg_park_wait(): the worker's seq_cst arm (W1) precedes
 * its seq_cst load of hot->park (W2); the shard's poller's seq_cst store
 * hot->park = 1 (A1) precedes its seq_cst loads of every seq in its range
 * (A2). If both missed -- W2 saw 0 and A2 saw even -- the single total order
 * over seq_cst operations would need W2 < A1, A1 < A2, A2 < W1 and W1 < W2, a
 * cycle. So at most one of them can miss, and the poller can never park with
 * a slot armed. S is a GLOBAL total order, so restricting attention to one
 * shard cannot break the argument; it only requires that W1 and A2 name the
 * SAME seq and that W2 and A1 name the SAME park word.
 *
 * The park word must be per shard for the same reason. Under one shared park
 * word a kick from an unrelated shard could consume the 1 -> 0 transition and
 * leave shard s parked with slot i armed: a lost wakeup.
 *
 * SHARDING ADDS TWO OBLIGATIONS, and they are obligations on the PARTITION,
 * not on memory order:
 *
 *   TOTALITY AND DISJOINTNESS -- the shard ranges must partition
 *   [0, agg_nslots) exactly (shard 0's lo == 0, each lo == the previous
 *   limit, the last limit == agg_nslots; verified once at init). A gap is a
 *   permanent hang for every thread that claims into it. An overlap is two
 *   pollers CASing one seq, which the CAS makes safe but which wastes a
 *   sweep.
 *
 *   IMMUTABILITY -- agg_slot_shard[], lo and limit are written once, before
 *   agg_started is published, and never again while a slot can be armed.
 *   Under a dynamic map a slot could move between the worker's W1/W2 and the
 *   new owner's A1/A2: W2 would have loaded the park word of a poller that no
 *   longer owns the slot, while the new owner's A1 preceded its own A2. That
 *   is not a cycle, it is a LOST WAKEUP, and the failing-CAS rule does not
 *   cover it. This is why there is no work stealing, no rebalancing, and no
 *   poller adopting a dead neighbour's range.
 *
 * The third obligation is COVERAGE, not ordering: A2 must include slot i,
 * i.e. i < hot->hi. That is what the seq_cst CAS-max in agg_claim_slot()
 * buys. Sweeping fewer slots than the shard owns loses a wakeup; sweeping
 * more is correct but is the cross-socket sweep the NUMA composition exists
 * to prevent. */
static inline void agg_kick(struct agg_shard_hot *hot)
{
	if (atomic_load_explicit(&hot->park, memory_order_seq_cst) == 1) {
		uint32_t one = 1;

		/* The 1 -> 0 CAS also makes the poller's
		 * FUTEX_WAIT(&hot->park, 1) return EAGAIN if it had not yet
		 * entered the syscall when the wake was issued. */
		if (atomic_compare_exchange_strong_explicit(&hot->park, &one,
		    0u, memory_order_acq_rel, memory_order_relaxed))
			agg_futex_wake(&hot->park, 1);
	}
}

static void dsa_wait_aggregator(const volatile uint8_t *comp, uint32_t xfer)
{
	struct agg_slot *s;
	struct agg_stat *sv;
	struct agg_shard *sh;
	struct agg_shard_hot *hot;
	uint64_t deadline, hb0, dead_since;
	uint32_t myseq, to_us = agg_to_us, it;

	/* Eligibility FIRST, before any spinning. The pointer-identity test is
	 * what keeps the batch, dto_memset_pages and async paths -- whose
	 * completion records are TLS, stack or caller-owned, i.e. not visible
	 * to the poller -- off this path without extra branching at those call
	 * sites. Testing it after the spin, as an ordinary "slow path" guard
	 * would, means every one of those waits burns the full spin budget
	 * here and THEN enters dsa_wait_spinyield, which
	 * starts its own fresh deadline: double the intended time on the core
	 * for every ineligible op in the process.
	 *
	 * Liveness and the watchdog deadline are read from THIS THREAD's
	 * shard, which is a read-mostly line by construction -- nothing the
	 * pollers write every sweep lives in it. One wedged poller therefore
	 * degrades only its own shard's workers instead of disabling the
	 * aggregator process-wide, which is exactly what sharding is supposed
	 * to stop being possible. */
	if (thr_agg_slot < 0 || thr_agg_sh == NULL ||
	    comp != (const volatile uint8_t *)&thr_compp->status ||
	    !atomic_load_explicit(&thr_agg_sh->started, memory_order_relaxed) ||
	    atomic_load_explicit(&agg_stop, memory_order_relaxed) ||
	    __rdtsc() < atomic_load_explicit(&thr_agg_sh->degraded_until,
	    memory_order_relaxed)) {
		dsa_wait_spinyield(comp);
		atomic_thread_fence(memory_order_acquire);
		return;
	}

	sh = thr_agg_sh;
	hot = thr_agg_hot;
	sv = &agg_stats[thr_agg_slot];

	/* SIZE GATE. The decision is the transfer size and nothing else. Below
	 * the threshold the op is shorter than a futex round trip (3-9us of CPU
	 * across two threads), so spinning is strictly cheaper. Above it the op
	 * outlives that round trip and the core is better given away, which is
	 * the entire reason this wait method exists. One compare
	 * against a global that is L1-resident forever; no TLS, no shared
	 * write, no timestamp on the block path at all.
	 *
	 * xfer == 0 means the caller could not report a size, not that the
	 * transfer is tiny. It BLOCKS. The only site that passes 0 is
	 * dsa_wait_no_adjust, reached from the batch, async and
	 * dto_memset_pages paths, whose completion records are TLS, stack or
	 * caller-owned and are therefore already rejected by the
	 * pointer-identity test above -- so this is a "must not misbehave if
	 * that invariant ever changes" rule, not a live policy. Blocking is the
	 * right failure direction three times over: it costs one bounded futex
	 * round trip where guessing "small" costs a core held for an op of
	 * unbounded size; it is the path carrying the whole self-rescue ladder
	 * (timeout escalation, heartbeat, repair kick, dead-poller degrade,
	 * spinyield backstop) rather than the path whose only protection is the
	 * valve below; and it preserves the deleted adaptive model's own
	 * semantics, since agg_class_of(0) already returned the largest
	 * class. */
	if (unlikely(xfer == 0)) {
		sv->unknown++;
		goto arm;
	}
	/* Strictly greater, so DTO_AGG_BLOCK_KB=64 means "more than 64KB
	 * blocks" -- the instruction verbatim. Not a one-byte quibble: an
	 * exactly-64KB copy is the commonest large memcpy there is, and 64K is
	 * the measured WORST case for this wait method (13% of busypoll
	 * throughput, CPU/op moving the wrong way, 9.3 -> 13.0), so the shipped
	 * default must leave it on the spin side. With >= the policy would also
	 * have hinged on an unrelated knob: any nonzero DTO_CPU_SIZE_FRACTION
	 * shrinks the DSA share of a 64KB copy below 64KB and makes it spin,
	 * while the same call at fraction 0 blocked. */
	if (xfer > agg_block_bytes)
		goto arm;			/* no spin at all */

	/* Spin to completion. agg_spin_cap_cyc is a safety valve, not a budget:
	 * it sits two orders of magnitude above any completion that can
	 * legitimately appear below the gate, and exists only so that a page
	 * fault, a wedged descriptor or a saturated device cannot pin this
	 * core. The deadline is sampled every 16th pause rather than every one
	 * -- a pause is ~140 cycles and an rdtsc ~30, so testing each iteration
	 * would stretch the poll period by ~20% and with it the detection
	 * latency of the very completion this loop exists to catch. At 1-in-16
	 * the tax is ~1.3%; widening further buys ~1% and is not worth touching
	 * a validated loop for. */
	deadline = __rdtsc() + agg_spin_cap_cyc;
	it = 0;
	while (*comp == 0) {
		if ((++it & 15u) == 0 && __rdtsc() >= deadline)
			goto spincap;
		_mm_pause();
	}
	sv->spinhit++;
	goto out;

spincap:
	/* The valve fired. Arm and block exactly as an over-threshold op does
	 * -- NOT dsa_wait_spinyield, which would keep the thread on the
	 * runqueue spinning and yielding for the duration of a fault, i.e. the
	 * pathology this wait method exists to prevent. The valve does not feed
	 * back into the gate: reacting to it would rebuild the estimator that
	 * was just deleted, and would adapt on fault samples, which is
	 * precisely what the old agg_outlier_cyc filter existed to stop. */
	sv->spincap++;
	if (atomic_exchange_explicit(&agg_spincap_logged, 1u,
	    memory_order_relaxed) == 0)
		LOG_ERROR("aggregator: spin cap expired on a %u-byte transfer; "
		    "blocking instead. Page fault, device stall, or "
		    "DTO_AGG_BLOCK_KB set too high. Further occurrences are "
		    "counted in the per-slot spincap stat only.\n", xfer);
	/* fall through */

arm:
	/* Already done? Deleting the pre-spin also deleted the free completion
	 * check that spinning performed at iteration 0, and a descriptor can
	 * easily have retired before the wait is even entered: with
	 * DTO_CPU_SIZE_FRACTION the caller memcpys its own share of the buffer
	 * BEFORE calling dsa_wait, which at a 50% split is ~5us of CPU work
	 * against 1.86us of device time. Without this load such an op pays the
	 * whole arm + kick + sweep + wake round trip -- 3-9us of cross-thread
	 * CPU, the exact cost this wait method exists to avoid -- for a wait of
	 * zero length. One load on a path that is about to issue a seq_cst RMW
	 * anyway; it covers the xfer == 0 and spin-cap fall-throughs too, and
	 * `out` is the identical exit the spin hit already takes. */
	if (*comp != 0) {
		sv->early++;
		goto out;
	}

	s = &agg_slots[thr_agg_slot];
	sv->block++;

	/* ARM. A full-barrier RMW, not a release store: it is the publishing
	 * half of the Dekker pair with the poller's park, and a release store
	 * would not order the following load of this shard's park word. */
	myseq = atomic_load_explicit(&s->seq, memory_order_relaxed) + 1; /* odd */
	atomic_exchange_explicit(&s->seq, myseq, memory_order_seq_cst);
	agg_kick(hot);

	/* Post-arm window, default 0. Under the size gate a worker arms at t~0
	 * of a descriptor that by construction needs at least
	 * agg_block_bytes/56.6GB/s of device time -- 1.86us at the default
	 * gate, more under queueing -- so a 1us window catches essentially
	 * nothing and just burns the core the gate exists to give back. It
	 * earned its keep under the old model, which armed only AFTER spinning
	 * out an estimate, i.e. already near the expected completion. Retained
	 * as a sweep axis because it is the correct recovery if the gate is
	 * ever configured far down (at DTO_AGG_BLOCK_KB=8, ops modelling at
	 * 0.85us really can land inside it). Guarded rather than merely
	 * defaulted off: the unguarded loop still executes two rdtsc when the
	 * knob is 0, on what is now the common path for every large op.
	 *
	 * Correctness never depended on it. The block loop re-tests *comp == 0
	 * before every futex_wait, and if the poller has already CASed seq the
	 * kernel's own compare returns EAGAIN immediately. Removing the window
	 * can cost one extra syscall in a race; it cannot lose a wakeup. */
	if (agg_postarm_cyc) {
		uint64_t d2 = __rdtsc() + agg_postarm_cyc;

		while (*comp == 0 && __rdtsc() < d2)
			_mm_pause();
	}

	/* This shard's heartbeat, not a process-wide one. With one counter for
	 * every poller a shard ticking at 600K/s makes hb != hb0 true on every
	 * timeout, so a genuinely dead shard's workers never degrade -- while
	 * one slow shard freezes a counter that is then blamed on three live
	 * ones. */
	hb0 = atomic_load_explicit(&hot->hb, memory_order_acquire);
	dead_since = 0;
	while (*comp == 0) {
		struct timespec ts;
		int r;

		if (unlikely(atomic_load_explicit(&agg_stop,
		    memory_order_relaxed)))
			break;		/* finish by polling below */

		ts.tv_sec = 0;
		ts.tv_nsec = (long)to_us * 1000;
		r = agg_futex_wait(&s->seq, myseq, &ts);
		if (r == 0)
			continue;			/* re-test *comp */
		if (errno == EAGAIN || errno == EINTR)
			continue;			/* seq moved on */
		if (errno == ETIMEDOUT) {
			uint64_t now, hb;

			if (*comp != 0)
				break;
			sv->timeout++;
			hb = atomic_load_explicit(&hot->hb,
			    memory_order_acquire);
			now = __rdtsc();
			if (to_us < agg_to_max_us)
				to_us = agg_to_max_us;
			if (hb != hb0) {		/* poller is alive */
				hb0 = hb;
				dead_since = 0;
				continue;
			}
			/* No sweep has completed since we armed. On the
			 * oversubscribed machine this wait method exists for,
			 * a SCHED_OTHER poller simply waiting for a slice is
			 * by far the likeliest explanation, and a few hundred
			 * microseconds of that is ordinary -- so it must not
			 * be mistaken for poller death, which disables the
			 * aggregator PROCESS-WIDE. Kick once, since a lost
			 * unpark is the one cause we can actually repair, then
			 * require the heartbeat to stay frozen for agg_dead_cyc
			 * (tens of milliseconds, i.e. far longer than any
			 * plausible scheduling delay) before degrading. */
			if (dead_since == 0) {
				dead_since = now;
				agg_kick(hot);
				continue;
			}
			if (now - dead_since < agg_dead_cyc)
				continue;
			atomic_store_explicit(&sh->degraded_until,
			    now + agg_degrade_cyc, memory_order_relaxed);
			break;
		}
		break;					/* self-rescue */
	}

	/* DISARM. Release store of myseq + 1 (even). If the poller already
	 * CASed, this stores the identical value; seq never decreases. */
	atomic_store_explicit(&s->seq, myseq + 1, memory_order_release);

	/* The device still owns the destination buffer until it writes the
	 * completion record, so this function must never return with the
	 * status byte still zero -- not on degrade, not on shutdown, not on
	 * poller death. */
	if (unlikely(*comp == 0))
		dsa_wait_spinyield(comp);

out:
	/* Order the status byte ahead of the caller's reads of
	 * thr_comp.bytes_completed / .result / .crc_val. Free on x86. */
	atomic_thread_fence(memory_order_acquire);
}

/* Returns the number of slots still armed with no completion yet. Sweeps only
 * this poller's own range: a shard is a contiguous sub-range of exactly one
 * NUMA node region, so a sweep never reads another socket's status bytes. */
static uint32_t agg_sweep(struct agg_shard *sh, struct agg_shard_hot *hot)
{
	uint32_t lo = __atomic_load_n(&sh->lo, __ATOMIC_RELAXED);
	uint32_t limit = __atomic_load_n(&sh->limit, __ATOMIC_RELAXED);
	uint32_t hi = atomic_load_explicit(&hot->hi, memory_order_acquire);
	uint32_t live = 0, n = 0, i, k;
	struct {
		uint32_t i, seq;
	} g[AGG_SLOTS_MAX];

	if (limit > AGG_SLOTS_MAX)
		limit = AGG_SLOTS_MAX;
	if (hi > limit)
		hi = limit;

	/* Pass A: collect the armed slots and start the completion-record
	 * line fills up front, so the status loads below are MLP-bound
	 * instead of serialized on LLC latency. An idle slot keeps the
	 * NON-ZERO status of its last completed op, which is why armedness
	 * has to be the filter and the status byte can never be one. */
	for (i = lo; i < hi; i++) {
		uint32_t sq = atomic_load_explicit(&agg_slots[i].seq,
		    memory_order_acquire);

		if ((sq & 1u) == 0)
			continue;
		g[n].i = i;
		g[n].seq = sq;
		n++;
		__builtin_prefetch((const void *)&agg_recs[i].comp.status, 0, 0);
	}

	/* Pass B: read status, close the slot, then wake. The order matters
	 * twice over: seq was loaded BEFORE status (so the CAS can only
	 * succeed for the arming this status belongs to), and the CAS is
	 * issued BEFORE the wake (so a worker arriving late at FUTEX_WAIT is
	 * rejected by the kernel's compare instead of sleeping forever). */
	for (k = 0; k < n; k++) {
		uint32_t sq = g[k].seq;

		i = g[k].i;
		if (((volatile struct dsa_completion_record *)
		    &agg_recs[i].comp)->status == 0) {
			live++;
			continue;
		}
		if (atomic_compare_exchange_strong_explicit(&agg_slots[i].seq,
		    &sq, sq + 1, memory_order_acq_rel, memory_order_relaxed))
			agg_futex_wake(&agg_slots[i].seq, 1);
		else
			live++;		/* the owner self-rescued */
	}
	atomic_fetch_add_explicit(&hot->hb, 1, memory_order_release);
	return live;
}

/* Returns 1 if the park actually entered the futex sleep, which is the input
 * to the rate-adaptive idle window in agg_main(). Poller-private, so it must
 * be a return value and not a static: with P pollers a shared flag would be
 * three other pollers' answer. */
static int agg_park_wait(struct agg_shard *sh, struct agg_shard_hot *hot)
{
	/* Pure liveness backstop, not a wakeup mechanism: agg_kick() is what
	 * unparks the poller, and a worker whose kick was somehow lost
	 * self-rescues through its own futex timeout. Short timeouts here buy
	 * nothing and cost a timer plus a full pre-park spin window in an
	 * otherwise completely idle process. */
	struct timespec ts = { 0, 100 * 1000 * 1000 };	/* 100 ms backstop */
	uint32_t lo = __atomic_load_n(&sh->lo, __ATOMIC_RELAXED);
	uint32_t limit = __atomic_load_n(&sh->limit, __ATOMIC_RELAXED);
	uint32_t hi, i;
	int slept = 0;

	atomic_store_explicit(&hot->park, 1, memory_order_seq_cst);
	/* seq_cst, not acquire: this load decides which slots the re-check
	 * below covers, so it has to join the same total order as the claim's
	 * CAS-max. See agg_claim_slot() for the execution this rules out. */
	hi = atomic_load_explicit(&hot->hi, memory_order_seq_cst);
	if (limit > AGG_SLOTS_MAX)
		limit = AGG_SLOTS_MAX;
	if (hi > limit)
		hi = limit;
	/* Re-check every slot AFTER publishing the park flag; see agg_kick()
	 * for why this pair cannot both miss. */
	for (i = lo; i < hi; i++)
		if (atomic_load_explicit(&agg_slots[i].seq,
		    memory_order_seq_cst) & 1u)
			goto out;
	if (atomic_load_explicit(&agg_stop, memory_order_seq_cst))
		goto out;
	slept = 1;
	agg_futex_wait(&hot->park, 1, &ts);
out:
	atomic_store_explicit(&hot->park, 0, memory_order_seq_cst);
	return slept;
}

static void *agg_main(void *arg)
{
	uint32_t sidx = (uint32_t)(uintptr_t)arg;
	struct agg_shard *sh = &agg_shards[sidx];
	struct agg_shard_hot *hot = &agg_hot[sidx];
	uint64_t idle_deadline = 0, idle_cur = agg_idle_cyc;
	char name[16] = "dto-agg";

	/* This thread must never offload and must never consume an admission
	 * slot; USE_ORIG_FUNC is now unconditionally true for it. */
	thr_dsa_disabled = 1;
	wq_index = -2;
	thr_agg_slot = -2;
	thr_agg_sh = NULL;
	thr_agg_hot = NULL;
	/* Formed by hand rather than with snprintf: this runs before the
	 * thread has done anything else and stdio would drag an interposed
	 * mem* call underneath it for a diagnostic string. */
	name[7] = (char)('0' + (sidx / 10) % 10);
	name[8] = (char)('0' + sidx % 10);
	name[9] = '\0';
	pthread_setname_np(pthread_self(), name);
	/* Placement is decided once, at init, on the initializing thread:
	 * agg_shard_cpus[] is either the operator's DTO_AGG_CPU + shard index
	 * or this shard's NUMA node mask, and is invalid when neither applies
	 * (the agg_nodes == 1 default, where today's code made no affinity
	 * call at all). */
	if (agg_shard_cpus_valid[sidx])
		pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t),
		    &agg_shard_cpus[sidx]);
	if (agg_rt) {
		struct sched_param p = { .sched_priority = 1 };

		pthread_setschedparam(pthread_self(), SCHED_RR, &p);
	}
	atomic_store_explicit(&sh->started, 1, memory_order_release);

	/* No logging, no malloc and no interposed mem* call below this point:
	 * cleanup_dto() closes log_fd while this thread may still be alive. */
	while (!atomic_load_explicit(&agg_stop, memory_order_relaxed)) {
		if (agg_sweep(sh, hot) > 0) {
			idle_deadline = 0;
			_mm_pause();
			continue;
		}
		/* No slot in this shard has ever been claimed, so nothing can
		 * arrive until one is -- and a claim CAS-maxes hi before the
		 * first arm, and that arm kicks. Park with NO spin window at
		 * all. At P == 1 "the table is empty" means "this process
		 * never offloads" and is rare; at P > 1 in a process with a
		 * handful of threads it is the COMMON case for most shards,
		 * and without this each of them burns a full DTO_AGG_IDLE_US
		 * window before every park. */
		if (atomic_load_explicit(&hot->hi, memory_order_relaxed) ==
		    __atomic_load_n(&sh->lo, __ATOMIC_RELAXED)) {
			agg_park_wait(sh, hot);
			idle_deadline = 0;
			continue;
		}
		/* Nothing is armed, so no completion can arrive until some
		 * worker arms -- and arming kicks us. Spinning here is
		 * therefore pure insurance against park/unpark churn, and its
		 * cost is a whole core. Hold the window only as long as it
		 * keeps paying: if the last park actually slept, the process
		 * is slower than the window and the window halves; if it
		 * returned without sleeping, arrivals are dense and it grows
		 * back. A fixed window is the worst of both -- at mongod-like
		 * rates (one offload every few hundred microseconds) a 200us
		 * window never expires and the poller burns a core to serve a
		 * few thousand ops per second. */
		if (idle_deadline == 0) {
			idle_deadline = __rdtsc() + idle_cur;
		} else if (__rdtsc() >= idle_deadline) {
			uint64_t t = __rdtsc();
			int slept = agg_park_wait(sh, hot);

			if (slept && __rdtsc() - t > idle_cur) {
				idle_cur >>= 1;
				if (idle_cur < agg_idle_min_cyc)
					idle_cur = agg_idle_min_cyc;
			} else {
				idle_cur <<= 1;
				if (idle_cur > agg_idle_cyc)
					idle_cur = agg_idle_cyc;
			}
			idle_deadline = 0;
		}
		_mm_pause();
	}

	/* Drain: one last sweep so anyone whose op really completed gets the
	 * real answer, then release every remaining waiter. Each woken worker
	 * re-tests its status byte, sees agg_stop and polls its own descriptor
	 * to completion, so nobody is left blocked and nobody returns with an
	 * unwritten completion record. */
	agg_sweep(sh, hot);
	{
		uint32_t hi = atomic_load_explicit(&hot->hi,
		    memory_order_acquire);
		uint32_t limit = __atomic_load_n(&sh->limit, __ATOMIC_RELAXED);
		uint32_t i;

		if (limit > AGG_SLOTS_MAX)
			limit = AGG_SLOTS_MAX;
		if (hi > limit)
			hi = limit;
		for (i = __atomic_load_n(&sh->lo, __ATOMIC_RELAXED); i < hi;
		    i++) {
			uint32_t sq = atomic_load_explicit(&agg_slots[i].seq,
			    memory_order_acquire);

			if (sq & 1u)
				agg_futex_wake(&agg_slots[i].seq, INT_MAX);
		}
	}
	atomic_store_explicit(&sh->started, 0, memory_order_release);
	return NULL;
}

/* Turns DTO_AGG_CPU / the NUMA composition into one cpu_set_t per shard, once,
 * on the initializing thread. Never pile pollers onto one CPU: that would
 * serialize them and silently reproduce P == 1 throughput through a knob that
 * appears to have been honoured. */
static void agg_build_cpusets(void)
{
	cpu_set_t inherited;
	int have_inherited;
	uint32_t sidx;

	have_inherited = pthread_getaffinity_np(pthread_self(),
	    sizeof(inherited), &inherited) == 0;

	for (sidx = 0; sidx < agg_npollers; sidx++)
		agg_shard_cpus_valid[sidx] = 0;

	if (agg_cpu >= 0) {
		/* DTO_AGG_CPU keeps its type and gains a meaning for P > 1:
		 * the FIRST CPU of a contiguous run, so poller s pins to
		 * agg_cpu + s. At P == 1 that is agg_cpu, bit-for-bit today.
		 * A run rather than a comma list because an operator carving
		 * out cores has a range (isolcpus=84-87), and because
		 * agg_getenv_int is this file's knob idiom -- a list parser
		 * would be new string handling in an LD_PRELOAD initialiser
		 * for a diagnostic knob.
		 * With agg_nodes > 1 the run must therefore be ORDERED to
		 * match the shard-to-node map (shards 0..ppn-1 on node 0,
		 * ppn..2*ppn-1 on node 1, ...); with agg_nodes == 1 the whole
		 * run must lie within the one node. A CPU on the wrong node is
		 * rejected below rather than honoured: it is the single
		 * placement agg_start_pollers' header comment forbids, because
		 * a sweep is unconditional where a claim is once per thread. */
		for (sidx = 0; sidx < agg_npollers; sidx++) {
			int cpu = agg_cpu + (int)sidx;
			int cpu_nd;

			if (cpu >= CPU_SETSIZE ||
			    (have_inherited && !CPU_ISSET(cpu, &inherited))) {
				LOG_ERROR("aggregator: DTO_AGG_CPU run reaches cpu %d for shard %u, which is outside CPU_SETSIZE or this process's affinity mask; that poller is left unpinned\n",
				    cpu, sidx);
				continue;
			}
			/* agg_nodes > 1 is only ever set when numa_available()
			 * succeeded, so numa_node_of_cpu is callable here for
			 * the same reason it is on the claim path. */
			cpu_nd = agg_nodes > 1 && agg_shards[sidx].node >= 0 ?
			    numa_node_of_cpu(cpu) : -1;
			if (cpu_nd >= 0 && cpu_nd != agg_shards[sidx].node) {
				LOG_ERROR("aggregator: DTO_AGG_CPU would pin shard %u (slots [%u,%u) on node %d) to cpu %d, which is on node %d; that poller would take a cross-socket miss on every status byte of every sweep, so it is left unpinned -- order the DTO_AGG_CPU run to match the shard-to-node map or unset it\n",
				    sidx, agg_shards[sidx].lo,
				    agg_shards[sidx].limit,
				    agg_shards[sidx].node, cpu, cpu_nd);
				continue;
			}
			CPU_ZERO(&agg_shard_cpus[sidx]);
			CPU_SET(cpu, &agg_shard_cpus[sidx]);
			agg_shard_cpus_valid[sidx] = 1;
		}
		return;
	}

	/* Default. agg_nodes == 1 (every P == 1 config, and the measured
	 * 86-core single-node machine) makes no affinity call at all, which is
	 * bit-identical to the behaviour this replaces. With more than one
	 * node a poller is bound to the CPU MASK OF ITS NODE -- node level,
	 * never core level. Its agg_recs pages are homed on that node by first
	 * touch and a wrong-socket poller takes a cross-socket miss on every
	 * status byte of every sweep; but pinning to a specific core on the
	 * oversubscribed machine this wait method exists for takes that core
	 * from workers, and a SCHED_OTHER poller sharing a core with a
	 * spinning worker is the exact interaction the heartbeat watchdog was
	 * tuned around. */
	if (numa_available() == -1)
		return;
	{
		/* One allocation for the whole pool. init_dto() is also
		 * reached from the pthread_atfork child handler, so every
		 * allocation on this path is one more thing that has to be
		 * safe there; pthread_create already allocates below, but
		 * there is no reason to add sixteen more. */
		struct bitmask *bm = numa_allocate_cpumask();

		if (bm == NULL)
			return;
		for (sidx = 0; sidx < agg_npollers; sidx++) {
			int i, any = 0;

			if (agg_shards[sidx].node < 0)
				continue;
			if (numa_node_to_cpus(agg_shards[sidx].node, bm) != 0)
				continue;
			CPU_ZERO(&agg_shard_cpus[sidx]);
			for (i = 0; i < CPU_SETSIZE &&
			    (unsigned long)i < bm->size; i++) {
				if (!numa_bitmask_isbitset(bm, i))
					continue;
				if (have_inherited &&
				    !CPU_ISSET(i, &inherited))
					continue;
				CPU_SET(i, &agg_shard_cpus[sidx]);
				any = 1;
			}
			if (any)
				agg_shard_cpus_valid[sidx] = 1;
			else
				LOG_ERROR("aggregator: node %d has no CPU inside this process's affinity mask; shard %u is left unpinned\n",
				    agg_shards[sidx].node, sidx);
		}
		numa_free_cpumask(bm);
	}
}

/* One poller per shard, ~700 kops/s each. */
#define AGG_POLLER_KOPS 700ULL
/* Peak measured device throughput, used only to state the arrival rate the
 * gate admits in the init log. */
#define AGG_PEAK_BYTES_PER_S 222000000000ULL

/* Composes the slot-to-shard partition with the NUMA claim partition, starts
 * the pollers, repairs the table for any poller that could not be created,
 * and only then publishes agg_started. Returns 0 when at least one poller is
 * live and every claimable slot is swept by exactly one of them.
 *
 * A shard MUST be a contiguous sub-range of exactly one node region. The
 * claim's NUMA partition exists so that a 4KB page of agg_recs (64 slots) is
 * first-touched only by same-node threads; a poller sweeping across a node
 * boundary reads those status bytes cross-socket on EVERY pass, which is
 * worse than the per-op traffic the partition removed, because a sweep is
 * unconditional where a claim is once per thread. */
static int agg_start_pollers(void)
{
	/* ONE POLLER PER NODE by default -- the minimum that gives every node
	 * region a sweeper, and the configuration the bring-up gate validated.
	 *
	 * It is tempting to derive this from the gate instead (peak device
	 * bandwidth / DTO_AGG_BLOCK_KB, over AGG_POLLER_KOPS), and that
	 * arithmetic is exactly what the warning below reports. It must not be
	 * the DEFAULT, because it can only assume the gate's WORST-CASE
	 * arrival rate while the pollers a process needs depends on the rate
	 * it actually produces. Measured at 1MB, where the gate blocks
	 * everything but the arrival rate is only ~214 kops/s: P=1 costs
	 * 10.4us/op and 2.18 cores, P=4 14.4us and 3.08, P=8 23.4us and 4.98.
	 * An extra poller costs very nearly one whole core, because a poller
	 * whose shard has work in flight on most sweeps never reaches the park
	 * path at all and the rate-adaptive idle window never gets to shrink.
	 * Over-provisioning from a static worst case would therefore burn
	 * cores in every process that is not at the gate's saturation point,
	 * which is nearly all of them.
	 *
	 * The count is PER NODE: agg_claim_slot partitions the table by NUMA
	 * node, so a process whose threads all run on one node is served only
	 * by that node's pollers. Measured: at P=2 on a 2-node box a node-0
	 * workload took claims in shard 0 alone and matched P=1 exactly. The
	 * node multiply happens below, in the ppn composition. */
	int def = agg_clamp((int)agg_nodes, 1, AGG_POLLERS_MAX);
	int req = agg_clamp(agg_getenv_int("DTO_AGG_POLLERS", def), 1,
	    AGG_POLLERS_MAX);
	uint32_t node_span, ppn, d, j, sidx, i;
	uint32_t created = 0;
	sigset_t all, old_set;

	for (sidx = 0; sidx < AGG_POLLERS_MAX; sidx++) {
		agg_shards[sidx].lo = agg_shards[sidx].limit = 0;
		agg_shards[sidx].node = -1;
		agg_shards[sidx].created = 0;
		agg_shards[sidx].tid = (pthread_t)0;
		agg_shard_cpus_valid[sidx] = 0;
		atomic_store_explicit(&agg_shards[sidx].started, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_shards[sidx].degraded_until, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_hot[sidx].park, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_hot[sidx].hb, 0,
		    memory_order_relaxed);
		atomic_store_explicit(&agg_hot[sidx].hi, 0,
		    memory_order_relaxed);
	}

	node_span = agg_nslots / agg_nodes;	/* the claim's own expression */
	if (node_span == 0)
		node_span = 1;

	if (req == 1) {
		/* P == 1 is special-cased AHEAD of all composition. Without
		 * it, ppn = max(1, 1/agg_nodes) = 1 would yield
		 * P_eff = agg_nodes and break "P == 1 reproduces today's
		 * behaviour exactly" on the commonest multi-socket
		 * configuration. */
		agg_npollers = 1;
		agg_pollers_per_node = 1;
		agg_shards[0].lo = 0;
		agg_shards[0].limit = agg_nslots;
		agg_shards[0].node = -1;
	} else if (agg_nodes > AGG_POLLERS_MAX) {
		/* More configured NUMA nodes than the shard table can hold.
		 * Group WHOLE node regions per shard: such a shard does sweep
		 * across a node boundary, which S3 exists to avoid, but every
		 * node region is still swept by exactly one poller and no
		 * region is split between two -- the correctness requirement.
		 * Logged, because the alternative (silently leaving regions
		 * unswept) is a permanent hang. */
		uint32_t gsz = (agg_nodes + AGG_POLLERS_MAX - 1) /
		    AGG_POLLERS_MAX;
		uint32_t ng = (agg_nodes + gsz - 1) / gsz;

		for (sidx = 0; sidx < ng; sidx++) {
			uint32_t n0 = sidx * gsz, n1 = (sidx + 1) * gsz;

			if (n1 > agg_nodes)
				n1 = agg_nodes;
			agg_shards[sidx].lo = n0 * node_span;
			agg_shards[sidx].limit = (sidx == ng - 1) ?
			    agg_nslots : n1 * node_span;
			agg_shards[sidx].node = (int)n0;
		}
		agg_npollers = ng;
		agg_pollers_per_node = 1;
		LOG_ERROR("aggregator: %u configured NUMA nodes exceeds AGG_POLLERS_MAX %d; %u shards each cover %u whole node regions and will sweep across a node boundary\n",
		    agg_nodes, AGG_POLLERS_MAX, ng, gsz);
	} else {
		/* Round DOWN, then floor at one poller per node. The claim
		 * spreads threads evenly across nodes, so an asymmetric
		 * allocation makes the under-served node's retire rate the
		 * process-wide ceiling and the extra poller buys nothing.
		 * Relaxing the floor is not an option: it would leave a node
		 * region under no poller, and folding that region under a
		 * neighbour is the cross-socket sweep forbidden above. The
		 * consequence is logged rather than hidden -- agg_nodes counts
		 * CONFIGURED nodes (numa_max_node() + 1), empty and CXL ones
		 * included, so P = 4 becomes P_eff = 8 on a machine reporting
		 * eight nodes with two populated. Those extra shards claim
		 * nothing and park immediately. */
		ppn = (uint32_t)req / agg_nodes;
		if (ppn < 1)
			ppn = 1;
		if (ppn > node_span)		/* no shard may be empty */
			ppn = node_span;
		if (ppn * agg_nodes > AGG_POLLERS_MAX)
			ppn = (uint32_t)AGG_POLLERS_MAX / agg_nodes;
		if (ppn < 1)
			ppn = 1;
		for (d = 0; d < agg_nodes; d++) {
			uint32_t nlo = d * node_span;
			/* THE TAIL FOLD IS LOAD-BEARING. When agg_nodes does
			 * not divide agg_nslots, the slots in
			 * [agg_nodes * node_span, agg_nslots) lie outside
			 * every node region but ARE reachable by the claim's
			 * fallback arm. A slot that is claimable and swept by
			 * nobody is a guaranteed hang, so the last node's
			 * upper bound is forced to agg_nslots: the last
			 * shard's swept range is deliberately wider than its
			 * preferred-claim range. */
			uint32_t nhi = (d == agg_nodes - 1) ? agg_nslots :
			    (d + 1) * node_span;

			for (j = 0; j < ppn; j++) {
				sidx = d * ppn + j;
				agg_shards[sidx].lo = nlo + (uint32_t)
				    ((uint64_t)j * (nhi - nlo) / ppn);
				agg_shards[sidx].limit = nlo + (uint32_t)
				    ((uint64_t)(j + 1) * (nhi - nlo) / ppn);
				agg_shards[sidx].node = agg_nodes > 1 ?
				    (int)d : -1;
			}
		}
		agg_npollers = ppn * agg_nodes;
		agg_pollers_per_node = ppn;
	}

	/* Partition verification, before anything can be claimed. One loop
	 * over at most sixteen shards, once per process; a gap is a permanent
	 * hang for every thread that claims into it, so publishing a table
	 * with one is never better than falling back to spinyield. */
	{
		uint32_t prev = 0;
		int bad = agg_npollers == 0 || agg_npollers > AGG_POLLERS_MAX;

		for (sidx = 0; !bad && sidx < agg_npollers; sidx++) {
			if (agg_shards[sidx].lo != prev ||
			    agg_shards[sidx].limit <= agg_shards[sidx].lo)
				bad = 1;
			prev = agg_shards[sidx].limit;
		}
		if (!bad && prev != agg_nslots)
			bad = 1;
		if (bad) {
			LOG_ERROR("aggregator: computed shard partition is not a total, disjoint cover of %u slots; using spinyield\n",
			    agg_nslots);
			return -1;
		}
	}

	if ((uint32_t)req != agg_npollers)
		LOG_ERROR("aggregator: DTO_AGG_POLLERS=%d composed to %u pollers (%u node%s x %u per node); set DTO_AGG_POLLERS explicitly to override\n",
		    req, agg_npollers, agg_nodes, agg_nodes == 1 ? "" : "s",
		    agg_pollers_per_node);

	for (i = 0; i < agg_nslots; i++)
		agg_slot_shard[i] = 0;
	for (sidx = 0; sidx < agg_npollers; sidx++) {
		for (i = agg_shards[sidx].lo; i < agg_shards[sidx].limit; i++)
			agg_slot_shard[i] = (uint8_t)sidx;
		atomic_store_explicit(&agg_hot[sidx].hi, agg_shards[sidx].lo,
		    memory_order_relaxed);
	}

	agg_build_cpusets();

	/* Created one at a time, in shard order, so that a failure at shard s
	 * is observed with s-1 live, s+1 not yet attempted, and -- because
	 * agg_started is still clear -- no concurrent claimer anywhere. */
	sigfillset(&all);		/* the pollers take no app signal */
	pthread_sigmask(SIG_SETMASK, &all, &old_set);
	for (sidx = 0; sidx < agg_npollers; sidx++) {
		if (pthread_create(&agg_shards[sidx].tid, &agg_attr, agg_main,
		    (void *)(uintptr_t)sidx) == 0) {
			/* NOT started, which the poller sets only once it has
			 * been scheduled: teardown has to run for a thread
			 * that exists but has not run yet, or a short-lived
			 * process unmaps the WQ portals and closes the log out
			 * from under it. */
			agg_shards[sidx].created = 1;
			created++;
		} else {
			agg_shards[sidx].tid = (pthread_t)0;
			LOG_ERROR("aggregator: pthread_create failed for shard %u [%u,%u)\n",
			    sidx, agg_shards[sidx].lo, agg_shards[sidx].limit);
		}
	}
	pthread_sigmask(SIG_SETMASK, &old_set, NULL);
	agg_any_created = created > 0;
	if (created == 0) {
		LOG_ERROR("aggregator: no poller could be created, using spinyield\n");
		return -1;
	}

	/* Repair, structurally rather than by a per-claim test. Widening a
	 * live sibling's range is a plain store it picks up on its next
	 * sweep; it can only ADD slots to that sweep, never remove one, so the
	 * sibling cannot miss an arm it was already responsible for. And no
	 * slot can be armed yet in any case, because agg_started is published
	 * below. Never fold across a node boundary: that would trade a clean
	 * spinyield fallback for a permanent cross-socket sweep. */
	for (sidx = 0; sidx < agg_npollers; sidx++) {
		uint32_t t;
		int folded = 0;

		if (agg_shards[sidx].created)
			continue;
		for (t = sidx; t-- > 0; ) {
			if (agg_shards[t].node != agg_shards[sidx].node)
				break;
			if (!agg_shards[t].created)
				continue;
			if (agg_shards[t].limit < agg_shards[sidx].limit)
				__atomic_store_n(&agg_shards[t].limit,
				    agg_shards[sidx].limit, __ATOMIC_RELAXED);
			folded = 1;
			break;
		}
		for (t = sidx + 1; !folded && t < agg_npollers; t++) {
			if (agg_shards[t].node != agg_shards[sidx].node)
				break;
			if (!agg_shards[t].created)
				continue;
			if (agg_shards[t].lo > agg_shards[sidx].lo)
				__atomic_store_n(&agg_shards[t].lo,
				    agg_shards[sidx].lo, __ATOMIC_RELAXED);
			folded = 1;
		}
		if (folded) {
			LOG_ERROR("aggregator: shard %u [%u,%u) has no poller; its slots were folded into a same-node sibling\n",
			    sidx, agg_shards[sidx].lo, agg_shards[sidx].limit);
			continue;
		}
		/* PRE-OWN the orphaned range. The claim's CAS from 0 then
		 * fails forever, so those slots are simply not in the table:
		 * no new test on any path, no cost anywhere, and child()'s
		 * existing owner = 0 loop undoes it for free before init_dto()
		 * re-runs. Shrinking agg_nslots instead is wrong -- it feeds
		 * span and base in the claim, so it would silently re-point
		 * every node's preferred region. A thread that loses its
		 * node's region takes the claim's fallback arm to another
		 * node's slot, or gets thr_agg_slot = -2 and spinyields under
		 * the existing re-claim backoff: the existing table-full
		 * behaviour, not a new failure mode. */
		for (i = agg_shards[sidx].lo; i < agg_shards[sidx].limit; i++)
			atomic_store_explicit(&agg_slots[i].owner, 1u,
			    memory_order_relaxed);
		LOG_ERROR("aggregator: shard %u [%u,%u) has no poller and no same-node sibling; those slots are retired (unclaimable)\n",
		    sidx, agg_shards[sidx].lo, agg_shards[sidx].limit);
	}

	for (sidx = 0; sidx < agg_npollers; sidx++) {
		if (!agg_shards[sidx].created)
			continue;
		for (i = agg_shards[sidx].lo; i < agg_shards[sidx].limit; i++)
			agg_slot_shard[i] = (uint8_t)sidx;
	}

	/* Coverage verification after the repair: every slot is either swept
	 * by exactly one LIVE shard or retired out of the table entirely. */
	for (i = 0; i < agg_nslots; i++) {
		uint32_t cov = 0;

		for (sidx = 0; sidx < agg_npollers; sidx++)
			if (agg_shards[sidx].created &&
			    i >= agg_shards[sidx].lo &&
			    i < agg_shards[sidx].limit)
				cov++;
		if (cov == 1)
			continue;
		if (cov == 0 && atomic_load_explicit(&agg_slots[i].owner,
		    memory_order_relaxed) == 1u)
			continue;
		LOG_ERROR("aggregator: slot %u is swept by %u live shards after repair; using spinyield\n",
		    i, cov);
		atomic_store_explicit(&agg_stop, 1, memory_order_seq_cst);
		for (sidx = 0; sidx < agg_npollers; sidx++) {
			atomic_store_explicit(&agg_hot[sidx].park, 0,
			    memory_order_seq_cst);
			agg_futex_wake(&agg_hot[sidx].park, 1);
		}
		return -1;		/* cleanup_dto still joins them */
	}

	/* The gate and the poller count belong in ONE line. Lowering
	 * DTO_AGG_BLOCK_KB without raising DTO_AGG_POLLERS is how the
	 * throughput collapse this pool exists to fix gets recreated
	 * somewhere new and quietly. */
	{
		/* PER NODE, not the pool total. agg_claim_slot partitions the
		 * table by node, so a process confined to one node is served
		 * only by that node's pollers and the pool figure overstates
		 * its real ceiling by agg_nodes. Measured: at P=2 on a 2-node
		 * box a node-0 workload used shard 0 alone and matched P=1
		 * exactly, while the pool figure claimed twice the budget. */
		uint32_t per_node = agg_npollers / agg_nodes;
		uint64_t cap_kops;
		uint64_t arr_kops = agg_block_bytes ?
		    AGG_PEAK_BYTES_PER_S / 1000ULL / agg_block_bytes : 0;

		if (per_node == 0)
			per_node = 1;
		cap_kops = (uint64_t)per_node * AGG_POLLER_KOPS;

		LOG_TRACE("aggregator: %u poller%s over %u slots and %u NUMA node%s (%u per node), per-node retire budget ~%llu kops/s; gate blocks above %u bytes, which at peak device throughput admits ~%llu kops/s (break-even transfer size ~%llu KB)\n",
		    agg_npollers, agg_npollers == 1 ? "" : "s", agg_nslots,
		    agg_nodes, agg_nodes == 1 ? "" : "s", per_node,
		    (unsigned long long)cap_kops, agg_block_bytes,
		    (unsigned long long)arr_kops,
		    (unsigned long long)(AGG_PEAK_BYTES_PER_S / 1024ULL /
			(cap_kops * 1000ULL)));
		if (agg_block_bytes == 0)
			LOG_ERROR("aggregator: DTO_AGG_BLOCK_KB=0 blocks every offloaded op regardless of size; the per-node retire budget (%u poller%s, ~%llu kops/s) is then the only ceiling\n",
			    per_node, per_node == 1 ? "" : "s",
			    (unsigned long long)cap_kops);
		else if (arr_kops > cap_kops)
			LOG_ERROR("aggregator: the DTO_AGG_BLOCK_KB=%u gate admits up to ~%llu kops/s but %u poller%s per node retire only ~%llu kops/s; raise DTO_AGG_POLLERS or the gate\n",
			    agg_block_bytes >> 10,
			    (unsigned long long)arr_kops, per_node,
			    per_node == 1 ? "" : "s",
			    (unsigned long long)cap_kops);
	}

	/* Only now is the table open for claims. */
	atomic_store_explicit(&agg_started, 1, memory_order_release);
	return 0;
}

/* WAIT_AGGREGATOR never reaches here: the aggregator path is dispatched
 * before the auto_adjust_knobs switch, and the auto-tuning heuristics that
 * call this count wait-loop iterations, a signal that is meaningless for a
 * thread that blocks. */
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
        case WAIT_SPINYIELD:
            dsa_wait_spinyield(comp);
            break;
        case WAIT_YIELD:
            dsa_wait_yield(comp);
            break;
        case WAIT_UMWAIT:
            dsa_wait_umwait(comp);
            break;
        case WAIT_TPAUSE:
            dsa_wait_tpause(comp);
            break;
        case WAIT_BUSYPOLL:
            dsa_wait_busy_poll(comp);
            break;
        case WAIT_AGGREGATOR:
            /* transfer size is not available here; the gate treats 0 as
             * unknown and blocks. Note WAIT_SLEEP below deliberately falls
             * through */
            dsa_wait_aggregator(comp, 0);
            break;
        case WAIT_SLEEP:
            // This method is not typically used in high-performance scenarios but
            // gives a good demonstration of how much CPU time can be reduced
            do {
                usleep(DTO_DEFAULT_USLEEP); // Sleep for 20 microseconds
            } while (*comp == 0);
        default:
            dsa_wait_busy_poll(comp);
    }
}

/* Wait for a CALLER-OWNED completion record (the public async and batch ops).
 *
 * These records are deliberately NOT aggregated, and the reason is an
 * ordering one rather than a CAS one. To aggregate them the poller would have
 * to be handed a pointer into the caller's storage, and its sequence is:
 * load seq (odd) -> load the registered pointer -> DEREFERENCE -> CAS. The
 * CAS is issued AFTER the dereference, so a failing CAS suppresses a spurious
 * WAKE but cannot retroactively make the LOAD legal. The hazardous
 * interleaving is precisely the one where the owner is no longer blocked: it
 * sees its status byte, disarms, returns from its wait, returns from the
 * function whose op was a local array in a stack frame, and the thread exits
 * -- glibc's stack cache munmaps past its 40MB limit. The poller can be
 * descheduled between its seq load and the dereference for an unbounded time,
 * so there is no bound on the window from its side. Heap storage does not
 * rescue it either: dto_batch_op_free can return the page to the OS. The
 * poller dereferencing nothing but agg_slots[] and agg_recs[], both .bss for
 * the life of the process, is exactly why the aggregator has no lifetime
 * problems at all; registering caller pointers spends that.
 *
 * So the size gate is applied HERE, directly, against the same threshold and
 * the same spin cap the interposed path uses: at or below it spin, above it
 * hand the core back through the scheduler. This must never call
 * dsa_wait_aggregator() -- a foreign comp fails its pointer-identity test and
 * falls through to spinyield having already discarded the transfer size,
 * which reaches the right place by accident, with no gate applied and the
 * misleading appearance of using the aggregator. */
static void dto_wait_caller_owned(const volatile uint8_t *comp, uint64_t bytes)
{
	if (wait_method != WAIT_AGGREGATOR) {
		dsa_wait_no_adjust(comp);
	} else if (bytes != 0 && bytes <= agg_block_bytes) {
		/* Sampling the deadline every 16th pause, and why, is lifted
		 * unchanged from dsa_wait_aggregator's spin loop. */
		uint64_t deadline = __rdtsc() + agg_spin_cap_cyc;
		uint32_t it = 0;

		while (*comp == 0) {
			if ((++it & 15u) == 0 && __rdtsc() >= deadline) {
				dsa_wait_spinyield(comp);
				break;
			}
			_mm_pause();
		}
	} else {
		dsa_wait_spinyield(comp);
	}
	/* Order the status byte ahead of the caller's reads of crc_val /
	 * bytes_completed, exactly as dsa_wait_aggregator's out: does. */
	atomic_thread_fence(memory_order_acquire);
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

static __always_inline void dsa_wait_and_adjust_v2(const volatile uint8_t *comp)
{

    if (++tl_num_descs == tl_next_sample) {
	uint64_t local_num_waits = 0;
        while (*comp == 0) {
            __dsa_wait(comp);
            local_num_waits++;
        }
        int64_t error = local_num_waits - AUTO_TUNE_V2_TARGET;
        tl_integral += error;
        uint64_t new_frac = tl_cpu_size_fraction - KP * error + KI * tl_integral;

        // Clamp within valid range
        tl_cpu_size_fraction = MAX(1, MIN(MAX_CPU_SIZE_FRACTION, new_frac));
        tl_next_sample += rand() % SAMPLE_INTERVAL*2 + 1;
    } else {
	while (*comp == 0) {
	    __dsa_wait(comp);
        }
    }
}

/* Grow or shrink the LFU slot count from the retry rate of the closing
 * window. Shrink must dispossess holders of the slots being retired: their
 * generation is bumped (holder fast path fails) and ownership cleared so a
 * later regrow starts from free slots. Thresholds: >2% WQ-full rejections
 * is backpressure, a clean window is headroom. */
/* CAS K downward and retire the dropped slots: generation bump evicts the
 * holders lazily, cleared ownership lets a regrow start from free slots.
 * Returns the new K, or -1 if the CAS lost. */
static int lfu_shrink_to(int k, int nk)
{
	int i;

	if (!atomic_compare_exchange_strong(&lfu_k, &k, nk))
		return -1;
	for (i = nk; i < k; i++) {
		atomic_fetch_add(&dsa_slots[i].gen, 1);
		atomic_store(&dsa_slots[i].owner, -1);
	}
	return nk;
}

static void lfu_adjust_k(void)
{
	uint64_t r = atomic_exchange(&k_win_retries, 0);
	int k = atomic_load(&lfu_k);
	int nk;

	if (r * 50 > LFU_K_WINDOW && k > lfu_k_min) {
		nk = k - LFU_K_STEP < lfu_k_min ? lfu_k_min : k - LFU_K_STEP;
		if (lfu_shrink_to(k, nk) >= 0)
			LOG_TRACE("lfu_auto_k: shrink %d -> %d (%llu retries/%d)\n",
			    k, nk, (unsigned long long)r, LFU_K_WINDOW);
	} else if (r == 0 && k < lfu_k_max) {
		nk = k + LFU_K_STEP > lfu_k_max ? lfu_k_max : k + LFU_K_STEP;
		if (atomic_compare_exchange_strong(&lfu_k, &k, nk))
			LOG_TRACE("lfu_auto_k: grow %d -> %d (clean window)\n",
			    k, nk);
	}
}

/* Latency-trend controller, run by the thread that closes a window. */
static void lat_window_close(void)
{
	uint64_t cycles = atomic_exchange(&lat_win_cycles, 0);
	uint64_t bytes = atomic_exchange(&lat_win_bytes, 0);
	uint64_t kb = bytes >> 10 ? bytes >> 10 : 1;
	uint64_t cpkb = (cycles << 8) / kb;
	int k = atomic_load(&lfu_k);
	int nk;

	atomic_store(&lat_win_count, 0);

	if (lat_ref_cpkb == 0) {		/* bootstrap window */
		lat_ref_cpkb = cpkb;
		return;
	}
	if (lat_freeze) {			/* queue drain after a shrink */
		lat_freeze--;
		lat_ref_cpkb = (3 * lat_ref_cpkb + cpkb) / 4;
		return;
	}

	if (cpkb * 100 > lat_ref_cpkb * (uint64_t)lat_eps_num) {
		if (k > lfu_k_min) {
			nk = k - 2 * LFU_K_STEP;
			if (nk < lfu_k_min)
				nk = lfu_k_min;
			if (lfu_shrink_to(k, nk) >= 0) {
				lat_freeze = 2;
				LOG_TRACE("lfu_auto_k: lat shrink %d -> %d (cpkb %llu ref %llu)\n",
				    k, nk, (unsigned long long)cpkb,
				    (unsigned long long)lat_ref_cpkb);
			}
		}
	} else if (k < lfu_k_max) {
		nk = k + LFU_K_STEP > lfu_k_max ? lfu_k_max : k + LFU_K_STEP;
		if (atomic_compare_exchange_strong(&lfu_k, &k, nk))
			LOG_TRACE("lfu_auto_k: lat grow %d -> %d (cpkb %llu ref %llu)\n",
			    k, nk, (unsigned long long)cpkb,
			    (unsigned long long)lat_ref_cpkb);
	}
	lat_ref_cpkb = (3 * lat_ref_cpkb + cpkb) / 4;
}

static __always_inline void lfu_account_submit(int retried)
{
	uint64_t subs;

	if (lfu_auto_k != 1)
		return;
	if (retried)
		atomic_fetch_add_explicit(&k_win_retries, 1,
		    memory_order_relaxed);
	subs = atomic_fetch_add_explicit(&k_win_submits, 1,
	    memory_order_relaxed) + 1;
	if ((subs & (LFU_K_WINDOW - 1)) == 0)
		lfu_adjust_k();
}

/* Latency-signal submit hook: start timing 1 of every LAT_SAMPLE_PERIOD
 * successful submissions on this thread. Non-sampled cost is one TLS
 * decrement; the descriptor/completion pair is thread-local so the sample
 * needs no tagging. */
static __always_inline void lfu_lat_submit(uint32_t xfer)
{
	if (lfu_auto_k != 2)
		return;
	if (thr_lat_ctr != 0) {
		thr_lat_ctr--;
		return;
	}
	thr_lat_ctr = LAT_SAMPLE_PERIOD;
	thr_lat_bytes = xfer;
	thr_lat_t0 = __rdtsc();
}

static __always_inline void lfu_lat_complete(void)
{
	if (lfu_auto_k != 2 || thr_lat_bytes == 0)
		return;
	atomic_fetch_add_explicit(&lat_win_cycles, __rdtsc() - thr_lat_t0,
	    memory_order_relaxed);
	atomic_fetch_add_explicit(&lat_win_bytes, thr_lat_bytes,
	    memory_order_relaxed);
	thr_lat_bytes = 0;
	if (atomic_fetch_add_explicit(&lat_win_count, 1,
	    memory_order_relaxed) == LAT_WINDOW - 1)
		lat_window_close();
}

static __always_inline int dsa_wait(struct dto_wq *wq,
	struct dsa_hw_desc *hw, volatile uint8_t *comp)
{
	/* Dispatched ahead of the knobs switch so the aggregator gets the
	 * transfer size (it decides spin-vs-block from the transfer size) and
	 * stays selected
	 * even if auto-tuning is forced back on. */
	if (wait_method == WAIT_AGGREGATOR)
		dsa_wait_aggregator(comp, hw->xfer_size);
	else switch (auto_adjust_knobs) {
            case AUTO_ADJUST_KNOBS:
                dsa_wait_and_adjust(comp);
                break;
            case AUTO_ADJUST_KNOBS_V2:
                dsa_wait_and_adjust_v2(comp);
                break;
            default:
                dsa_wait_no_adjust(comp);
        }

	lfu_lat_complete();

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

	if (wq->wq_mmapped) {
		ret = enqcmd(hw, wq->wq_portal);
		if (!ret) {
			lfu_account_submit(0);
			lfu_lat_submit(hw->xfer_size);
			return SUCCESS;
		}
	} else {
		ret = write(wq->wq_fd, hw, sizeof(*hw));
		if (ret == sizeof(*hw))
			return SUCCESS;
		else
			return FAIL_OTHERS;
	}
	lfu_account_submit(1);
	return RETRY;
}

static __always_inline int dsa_execute(struct dto_wq *wq,
	struct dsa_hw_desc *hw, volatile uint8_t *comp)
{
	int ret;
	*comp = 0;
	//LOG_TRACE("desc flags: 0x%x, opcode: 0x%x\n", hw->flags, hw->opcode);
	__builtin_ia32_sfence();

	switch (wq->wq_mmapped) {
            case WQ_MMAPPED:
		ret = enqcmd(hw, wq->wq_portal);
                break;
            default:
		ret = write(wq->wq_fd, hw, sizeof(*hw));
		if (ret != sizeof(*hw)) {
			return FAIL_OTHERS;
                }
		else {
			ret = 0;
                }
                break;
        }

	if (!ret) {
		/* see dsa_wait(): size-gated dispatch, knobs-independent */
		if (wait_method == WAIT_AGGREGATOR)
			dsa_wait_aggregator(comp, hw->xfer_size);
		else switch (auto_adjust_knobs) {
                    case AUTO_ADJUST_KNOBS:
                        dsa_wait_and_adjust(comp);
                        break;
                    case AUTO_ADJUST_KNOBS_V2:
                        dsa_wait_and_adjust_v2(comp);
                        break;
                    default:
                        dsa_wait_no_adjust(comp);
                }

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
static void print_stats(void);

/* Dump aggregated stats to the log every DTO_STATS_DUMP_SEC while the
 * workload runs, so long-lived processes that never run library
 * destructors (or are killed) still produce statistics. */
#define DTO_STATS_DUMP_SEC 30
static _Atomic long stats_last_dump_sec;

static void maybe_dump_stats(void)
{
	struct timespec now;
	long prev;

	clock_gettime(CLOCK_BOOTTIME, &now);
	prev = atomic_load(&stats_last_dump_sec);
	if (now.tv_sec - prev < DTO_STATS_DUMP_SEC)
		return;
	if (atomic_compare_exchange_strong(&stats_last_dump_sec, &prev,
					   now.tv_sec))
		print_stats();
}

static void update_stats(int op, size_t n, size_t bytes_completed,
		uint64_t elapsed_ns, int group, int error_code)
{
	int bucket = (n / HIST_BUCKET_SIZE);

	/* Allocate and register this thread's stats on first use.
	 * Use mmap instead of calloc to avoid re-entering the allocator
	 * (which deadlocks when tcmalloc calls memset while holding its
	 * PageHeap spinlock). mmap returns zeroed memory. */
	if (unlikely(tl_stats == NULL)) {
		tl_stats = mmap(NULL, sizeof(struct thread_stats),
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (tl_stats == MAP_FAILED) {
			tl_stats = NULL;
			return;  /* Out of memory, skip stats */
		}

		pthread_mutex_lock(&stats_registry_lock);
		int idx = atomic_fetch_add(&global_stats_count, 1);
		if (idx < MAX_STAT_THREADS) {
			global_stats_registry[idx] = tl_stats;
		}
		pthread_mutex_unlock(&stats_registry_lock);
	}

	if (bucket >= HIST_NO_BUCKETS)  /* last bucket includes remaining sizes */
		bucket = HIST_NO_BUCKETS-1;

	/* Update thread-local stats (no atomics needed!) */
	++tl_stats->op_counter[bucket][group][op];
	tl_stats->bytes_counter[bucket][group] += bytes_completed;
	tl_stats->lat_counter[bucket][group][op] += elapsed_ns;
	if (group == DSA_CALL_FAILED)
		++tl_stats->fail_counter[bucket][error_code];
	maybe_dump_stats();
}

static void print_stats(void)
{
	struct timespec dto_end_time;
	/* Static: this is ~250KB and print_stats can run on an application
	 * thread via the periodic dump; single writer is ensured by the
	 * dump rate-limit CAS and the destructor ordering. */
	static struct thread_stats aggregated_stats;
	int num_threads;

	if (likely(!collect_stats))
		return;

	clock_gettime(CLOCK_BOOTTIME, &dto_end_time);

	LOG_TRACE("DTO Run Time: %ld ms\n", TS_NS(dto_start_time, dto_end_time)/1000000);
	if (dsa_admission == ADMIT_LFU) {
		int k = atomic_load(&lfu_k);

		if (ticket_mode) {
			int held = 0, hinted_held = 0;

			for (int si = 0; si < k; si++) {
				if (atomic_load_explicit(&dsa_slots[si].owner,
				    memory_order_relaxed) >= 0) {
					held++;
					if (atomic_load_explicit(
					    &dsa_slots[si].hinted,
					    memory_order_relaxed))
						hinted_held++;
				}
			}
			LOG_TRACE("DTO LFU slots (K): %d held: %d hinted: %d\n",
			    k, held, hinted_held);
		} else {
			LOG_TRACE("DTO LFU slots (K): %d\n", k);
		}
	}
	if (dto_shed)
		LOG_TRACE("DTO shed level: %d\n", atomic_load(&shed_level));
	LOG_TRACE("DTO CPU Fraction: %.2f \n", cpu_size_fraction/100.0);

	/* Aggregate all thread-local stats */
	/* Never the interposed memset: this runs on an application thread
	 * from inside a memop's stats epilogue, and the 250KB fill would be
	 * offloaded through that thread's own descriptor and completion
	 * record -- clobbering the result of the operation still being
	 * consumed (observed as CRC 0 / false checksum mismatches). */
	orig_memset(&aggregated_stats, 0, sizeof(aggregated_stats));

	pthread_mutex_lock(&stats_registry_lock);
	num_threads = atomic_load(&global_stats_count);
	for (int tid = 0; tid < num_threads && tid < MAX_STAT_THREADS; ++tid) {
		struct thread_stats *ts = global_stats_registry[tid];
		if (ts == NULL)
			continue;

		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			for (int g = 0; g < MAX_STAT_GROUP; ++g) {
				for (int o = 0; o < MAX_MEMOP; ++o) {
					aggregated_stats.op_counter[b][g][o] += ts->op_counter[b][g][o];
					aggregated_stats.lat_counter[b][g][o] += ts->lat_counter[b][g][o];
				}
				aggregated_stats.bytes_counter[b][g] += ts->bytes_counter[b][g];
			}
			for (int f = 0; f < MAX_FAILURES; ++f) {
				aggregated_stats.fail_counter[b][f] += ts->fail_counter[b][f];
			}
		}
	}
	pthread_mutex_unlock(&stats_registry_lock);

	// display stats
	for (int t = 0; t < 2; ++t) {
		if (t == 0)
			LOG_TRACE("\n******** Number of Memory Operations ********\n");
		else
			LOG_TRACE("\n******** Average Memory Operation Latency (us)  ********\n");

		LOG_TRACE("%17s    ", "");
		for (int g = 0; g < MAX_STAT_GROUP; ++g) {
			if (t == 0) {
				if (g == DSA_FAIL_CODES)
					LOG_TRACE("<***** %-13s *****> ", stat_group_names[g]);
				else
					LOG_TRACE("<*************** %-13s ***************> ", stat_group_names[g]);
			} else {
				if (g != DSA_FAIL_CODES)
					LOG_TRACE("<******** %-13s ********> ", stat_group_names[g]);

			}
		}
		LOG_TRACE("\n");

		LOG_TRACE("%-17s -- ", "Byte Range");
		for (int g = 0; g < MAX_STAT_GROUP - 1; ++g) {
			for (int o = 0; o < MAX_MEMOP; ++o)
				LOG_TRACE("%-8s ", memop_names[o]);
			if (t == 0)
				LOG_TRACE("%-12s ", "bytes");
		}
		if (t == 0)
			for (int o = 1; o < MAX_FAILURES; ++o)
				LOG_TRACE("%-6s ", failure_names[o]);
		LOG_TRACE("\n");

		for (int b = 0; b < HIST_NO_BUCKETS; ++b) {
			bool empty = true;

			for (int g = 0; g < MAX_STAT_GROUP; ++g) {
				for (int o = 0; o < MAX_MEMOP; ++o) {
					if (aggregated_stats.op_counter[b][g][o] != 0) {
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
				LOG_TRACE("% 8d-%-8d -- ", b*4096, ((b+1)*4096)-1);
			else
				LOG_TRACE("   >=%-12d -- ", b*4096);

			for (int g = 0; g < MAX_STAT_GROUP - 1; ++g) {
				for (int o = 0; o < MAX_MEMOP; ++o) {
					if (t == 0) {
						LOG_TRACE("%-8d ", aggregated_stats.op_counter[b][g][o]);
						continue;
					}
					if (aggregated_stats.op_counter[b][g][o] != 0) {
						double avg_us = ((double) aggregated_stats.lat_counter[b][g][o])/(((double) aggregated_stats.op_counter[b][g][o]) * 1000.0);

						LOG_TRACE("%-8.2f ", avg_us);
					} else {
						LOG_TRACE("%-8d ", 0);
					}
				}
				if (t == 0)
					LOG_TRACE("%-12lld ", aggregated_stats.bytes_counter[b][g]);
			}
			if (t == 0)
				for (int o = 1; o < MAX_FAILURES; ++o)
					LOG_TRACE("%-6d ", aggregated_stats.fail_counter[b][o]);
			LOG_TRACE("\n");
		}
	}
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

		snprintf(wqs[num_wqs].wq_path, sizeof(wqs[num_wqs].wq_path), "/dev/dsa/%s", wq);

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

		rc = accfg_wq_get_user_dev_path(acc_wq, wqs[i].wq_path, sizeof(wqs[i].wq_path));
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

        if (num_wqs > max_wqs_supported) {
            num_wqs = max_wqs_supported;
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

	/* detect umwait support */
	leaf = 7;
	waitpkg = 0;
	if (__get_cpuid(0, &leaf, unused, &waitpkg, unused + 1)) {
		if (waitpkg & 0x20) {
			LOG_TRACE("umwait supported\n");
			umwait_support = 1;
		}
	}

	env_str = getenv("DTO_WAIT_METHOD");
	if (env_str != NULL) {
		if (!strncmp(env_str, wait_names[WAIT_BUSYPOLL], strlen(wait_names[WAIT_BUSYPOLL]))) {
			wait_method = WAIT_BUSYPOLL;
			min_avg_waits = MIN_AVG_POLL_WAITS;
			max_avg_waits = MAX_AVG_POLL_WAITS;
		} else if (!strncmp(env_str, wait_names[WAIT_SPINYIELD], strlen(wait_names[WAIT_SPINYIELD]))) {
			wait_method = WAIT_SPINYIELD;
			min_avg_waits = MIN_AVG_YIELD_WAITS;
			max_avg_waits = MAX_AVG_YIELD_WAITS;
		} else if (!strncmp(env_str, wait_names[WAIT_YIELD], strlen(wait_names[WAIT_YIELD]))) {
			wait_method = WAIT_YIELD;
			min_avg_waits = MIN_AVG_YIELD_WAITS;
			max_avg_waits = MAX_AVG_YIELD_WAITS;
		} else if (!strncmp(env_str, wait_names[WAIT_UMWAIT], strlen(wait_names[WAIT_UMWAIT]))) {
			if (umwait_support) {
				wait_method = WAIT_UMWAIT;
				/* Use the same waits as busypoll for now */
				min_avg_waits = MIN_AVG_POLL_WAITS;
				max_avg_waits = MAX_AVG_POLL_WAITS;
			} else
				LOG_ERROR("umwait not supported. Falling back to default wait method\n");
		} else if (!strncmp(env_str, wait_names[WAIT_TPAUSE], strlen(wait_names[WAIT_TPAUSE]))) {
		    if (umwait_support) {
			wait_method = WAIT_TPAUSE;
                    } else {
			LOG_ERROR("tpause not supported. Falling back to busypoll\n");
                        wait_method = WAIT_BUSYPOLL;
                    }
                } else if (!strncmp(env_str, wait_names[WAIT_SLEEP], strlen(wait_names[WAIT_SLEEP]))) {
                    double local_cpu_size_fraction = 0;
		    env_str = getenv("DTO_CPU_SIZE_FRACTION");

                    if (env_str != NULL) {
			    errno = 0;
			    local_cpu_size_fraction = strtod(env_str, NULL);
                    }

                    uint64_t local_auto_adjust_knobs = 0;
	            env_str = getenv("DTO_AUTO_ADJUST_KNOBS");

		    if (env_str != NULL) {
			errno = 0;
			local_auto_adjust_knobs = strtoul(env_str, NULL, 10);
			    if (errno)
				local_auto_adjust_knobs = 1;
		    }
                    if (local_cpu_size_fraction < 0.001 && local_auto_adjust_knobs == 0) {
                        wait_method = WAIT_SLEEP;
                    } else {
                        LOG_ERROR("sleep not supported for partial offloading (fraction > 0 and/or autotuning is selected\n");
                        wait_method = WAIT_BUSYPOLL;
                    }
                } else if (!strncmp(env_str, wait_names[WAIT_AGGREGATOR],
				strlen(wait_names[WAIT_AGGREGATOR]))) {
			wait_method = WAIT_AGGREGATOR;
			min_avg_waits = MIN_AVG_POLL_WAITS;
			max_avg_waits = MAX_AVG_POLL_WAITS;
			/* DTO_AUTO_ADJUST_KNOBS is parsed before dsa_init() is
			 * called, so clearing it here sticks. The heuristic's
			 * input is the number of wait-loop iterations, which
			 * is ~1 whenever the thread blocks, so leaving it on
			 * would feed the tuner a constant. */
			if (auto_adjust_knobs) {
				/* LOG_ERROR, not LOG_TRACE: this silently
				 * changes a second tuning axis, so an A/B of
				 * aggregator against umwait with
				 * DTO_AUTO_ADJUST_KNOBS set would be comparing
				 * two things at once. */
				LOG_ERROR("aggregator: DTO_AUTO_ADJUST_KNOBS=%d ignored; benchmark both arms with it off\n",
					auto_adjust_knobs);
				auto_adjust_knobs = 0;
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
        num_threads = 0;

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
		
                env_str = getenv("DTO_DSA_BOF");
		if (env_str != NULL) {
			errno = 0;
			dto_dsa_bof = strtoul(env_str, NULL, 10);
			if (errno)
				dto_dsa_bof = 0;

			dto_dsa_bof = !!dto_dsa_bof;
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
			clock_gettime(CLOCK_BOOTTIME, &dto_start_time);
			/* Arm the periodic dump so the first one fires a full
			 * interval from now: dumping on the very first
			 * operation puts a large print in the middle of the
			 * host application's early single-threaded init. */
			atomic_store(&stats_last_dump_sec, dto_start_time.tv_sec);
			/* Change the log level to 'trace' so that the
			 * stats can be logged
			 */
			log_level = LOG_LEVEL_TRACE;
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

			/* Separate DSA gate for the explicit CRC API (dto_crc,
			 * dto_memcpy_crc_async). Lets applications offload
			 * explicit CRC/copy+CRC calls while keeping transparent
			 * memcpy/memset interposition on the CPU (e.g.
			 * DTO_MIN_BYTES very large, DTO_CRC_MIN_BYTES small).
			 * Defaults to dsa_min_size when unset. */
			env_str = getenv("DTO_SHED");
			if (env_str != NULL &&
			    strtoul(env_str, NULL, 10) == 1)
				dto_shed = 1;
			env_str = getenv("DTO_SHED_HI_US");
			if (env_str != NULL && atoi(env_str) > 0)
				shed_hi_us = atoi(env_str);
			env_str = getenv("DTO_SHED_LO_US");
			if (env_str != NULL && atoi(env_str) > 0)
				shed_lo_us = atoi(env_str);

			env_str = getenv("DTO_CRC_MIN_BYTES");

			if (env_str != NULL) {
				errno = 0;
				crc_min_size = strtoul(env_str, NULL, 10);
				if (errno)
					crc_min_size = CRC_MIN_SIZE_UNSET;
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
                                tl_cpu_size_fraction = cpu_size_fraction;
			}

			env_str = getenv("DTO_AUTO_ADJUST_KNOBS");

			if (env_str != NULL) {
				errno = 0;
				auto_adjust_knobs = strtoul(env_str, NULL, 10);
				if (errno)
					auto_adjust_knobs = 1;
                                if (auto_adjust_knobs == AUTO_ADJUST_KNOBS_V2) {
                                    tl_next_sample = rand() % (SAMPLE_INTERVAL*2) + 1;
                                }
			}

                        // Only use c02 if we are offloading a significant chunk to
                        // DSA so we amortize the exit latency of C02 state
                        if (cpu_size_fraction <= 20 && auto_adjust_knobs == 0) {
                            dto_use_c02 = true;
                        } else {
                            dto_use_c02 = false;
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
                        max_wqs_supported = 100;
                        env_str = getenv("DTO_MAX_WQS_SUPPORTED");
                        if (env_str != NULL) {
                                errno = 0;
                                max_wqs_supported = strtoul(env_str, NULL, 10);
                                if (errno || max_wqs_supported == 0)
                                        max_wqs_supported = 100;
                        }

                        env_str = getenv("DTO_DSA_MAX_THREADS");
                        if (env_str != NULL) {
                                errno = 0;
                                dsa_max_threads = strtoul(env_str, NULL, 10);
                                if (errno || dsa_max_threads < 0)
                                        dsa_max_threads = 0;
                                LOG_TRACE("dsa_max_threads: %d\n", dsa_max_threads);
			}

			env_str = getenv("DTO_DSA_ADMISSION");
			if (env_str != NULL && (!strcmp(env_str, "lfu") ||
			    !strcmp(env_str, "ticket"))) {
				dsa_admission = ADMIT_LFU;
				ticket_mode = !strcmp(env_str, "ticket");
				if (dsa_max_threads <= 0)
					dsa_max_threads = 64;
				if (dsa_max_threads > DSA_SLOTS_MAX)
					dsa_max_threads = DSA_SLOTS_MAX;
				for (int si = 0; si < DSA_SLOTS_MAX; si++)
					atomic_store(&dsa_slots[si].owner, -1);
				atomic_store(&lfu_k, dsa_max_threads);

				env_str = getenv("DTO_LFU_AUTO_K");
				if (env_str != NULL) {
					lfu_auto_k = strtoul(env_str, NULL, 10);
					if (lfu_auto_k < 0 || lfu_auto_k > 2)
						lfu_auto_k = 0;
				}
				env_str = getenv("DTO_LFU_LAT_EPS");
				if (env_str != NULL && atoi(env_str) > 100)
					lat_eps_num = atoi(env_str);
				env_str = getenv("DTO_LFU_FREQ_SAMPLE");
				if (env_str != NULL && atoi(env_str) >= 1 &&
				    atoi(env_str) <= 256) {
					freq_sample = atoi(env_str);
					age_window_samples =
					    AGE_WINDOW_OPS / freq_sample;
					if (age_window_samples < 1024)
						age_window_samples = 1024;
				}
				env_str = getenv("DTO_LFU_K_MIN");
				if (env_str != NULL)
					lfu_k_min = atoi(env_str);
				env_str = getenv("DTO_LFU_K_MAX");
				if (env_str != NULL)
					lfu_k_max = atoi(env_str);
				if (lfu_k_min < 1)
					lfu_k_min = 1;
				if (lfu_k_max > DSA_SLOTS_MAX)
					lfu_k_max = DSA_SLOTS_MAX;
				if (lfu_k_max < lfu_k_min)
					lfu_k_max = lfu_k_min;
				LOG_TRACE("dsa_admission: %s, slots: %d, auto_k: %d [%d..%d]\n",
					ticket_mode ? "ticket" : "lfu",
					dsa_max_threads, lfu_auto_k, lfu_k_min, lfu_k_max);
                        }

			if (dsa_init()) {
				LOG_ERROR("Didn't find any usable DSAs. Falling back to using CPUs.\n");
				use_std_lib_calls = 1;
			}
    			unsigned int num, den, freq;
    			unsigned int unused;
    			unsigned long long tmp;
    			__get_cpuid( 0x15, &den, &num, &freq, &unused );
    			freq /= 1000;
    			LOG_TRACE( "Core Freq = %u kHz\n", freq );
    			LOG_TRACE( "TSC Mult  = %u\n", num );
    			LOG_TRACE( "TSC Den   = %u\n", den );
    			freq *= num;
    			freq /= den;
    			LOG_TRACE( "CPU freq = %u kHz\n", freq );
			if (freq > 0) {
				tsc_khz = freq;
				shed_hi_cycles =
				    (uint64_t)shed_hi_us * tsc_khz / 1000;
				shed_lo_cycles =
				    (uint64_t)shed_lo_us * tsc_khz / 1000;
				{
					int us = 8;
					const char *e =
					    getenv("DTO_SPINYIELD_US");

					if (e != NULL && atoi(e) > 0)
						us = atoi(e);
					spinyield_cycles =
					    (uint64_t)us * tsc_khz / 1000;
				}
			}
			if (dto_shed)
				LOG_TRACE("shed: enabled, hi %d us (%llu cyc), lo %d us (%llu cyc)\n",
				    shed_hi_us,
				    (unsigned long long)shed_hi_cycles,
				    shed_lo_us,
				    (unsigned long long)shed_lo_cycles);
    			LOG_TRACE( "Requested wait: %llu nsec\n", tpause_wait_time );
    			tmp = tpause_wait_time;
    			tmp *= freq;
    			tpause_wait_time = tmp / NSEC_PER_MSEC;
    			LOG_TRACE( "Requested wait duration: %llu cycles\n", tpause_wait_time );

			/* Started here, before dto_initialized = 1, so that
			 * every interposed mem* call issued from inside
			 * pthread_create's allocator still takes the
			 * dto_internal_* path: there is no re-entrancy window
			 * to guard. Creating the poller lazily from the wait
			 * path would put pthread_create underneath an
			 * interposed memcpy. */
			if (wait_method == WAIT_AGGREGATOR && num_wqs > 0 &&
			    !use_std_lib_calls) {
				/* every knob is clamped: a negative or absurd
				 * value must not turn into a huge unsigned
				 * cycle budget or a zero futex timeout */
				/* SIZE GATE. ABOVE this a copy arms and blocks
				 * with no spin at all; at or below it spins to
				 * completion. The compare is strict, so 64 leaves
				 * an exactly-64KB copy -- the measured worst case
				 * for this wait method -- on the spin side.
				 * 64KB is the instructed default
				 * (1.86us of device time by the measured cost
				 * model). The ">4us" reading of the same
				 * instruction is ~187KB, and the microbenchmark
				 * crossover is between 128 and 256, so
				 * 64/128/187/256/512 are the sweep points. 0
				 * blocks every op; 1048576 (1GB) spins every op.
				 * Both ends are valid sweep bounds. Clamped in
				 * KB so the shift cannot overflow the uint32_t
				 * it is compared against. */
				agg_block_bytes = (uint32_t)agg_clamp(
				    agg_getenv_int("DTO_AGG_BLOCK_KB", 64),
				    0, 1048576) << 10;

				agg_postarm_cyc = agg_us_to_cyc(agg_clamp(
				    agg_getenv_int("DTO_AGG_POSTARM_US", 0),
				    0, 10000));
				agg_idle_cyc = agg_us_to_cyc(agg_clamp(
				    agg_getenv_int("DTO_AGG_IDLE_US", 200),
				    0, 1000000));
				agg_idle_min_cyc = agg_us_to_cyc(agg_clamp(
				    agg_getenv_int("DTO_AGG_IDLE_MIN_US", 4),
				    0, 1000000));
				/* never 0: the window is halved and doubled,
				 * and 0 is an absorbing state */
				if (agg_idle_min_cyc == 0)
					agg_idle_min_cyc = 1;
				if (agg_idle_min_cyc > agg_idle_cyc)
					agg_idle_min_cyc = agg_idle_cyc;
				/* SAFETY VALVE for the spin path, which under the
				 * gate has no other bound. dto_dsa_bof defaults
				 * to 1, so a page fault does NOT abort the
				 * descriptor with status 0x03 -- the device
				 * stalls it while the IOMMU is serviced and
				 * writes no completion record for the whole
				 * duration. A wedged descriptor or a WQ reset
				 * writes none ever. Neither may pin a core.
				 * 200us is ~100x the device time of the largest
				 * op that can reach the spin path at the default
				 * gate, ~20x a heavily queued one, and 250x
				 * below agg_dead_cyc, so it always fires long
				 * before the poller-death watchdog can be
				 * implicated, and never in steady state.
				 *
				 * The floor is the load-bearing part. The cap is
				 * absolute time, but the spin path's legitimate
				 * duration is a function of a DIFFERENT knob.
				 * Decoupled, DTO_AGG_BLOCK_KB=8192 quietly puts
				 * a 200us cap in front of ops that legitimately
				 * need 149us: every sub-threshold op then burns
				 * a full cap of spin AND a futex round trip --
				 * the worst of both policies, reached silently
				 * through a knob that does not name the valve.
				 * Flooring at 8x the modelled device time of the
				 * gate makes the valve track the policy
				 * automatically; 8x is loose enough not to cry
				 * wolf on a busy device and tight enough to catch
				 * a stuck descriptor within a millisecond at any
				 * sane gate. Never 0: a 0 cap would make the spin
				 * loop a no-op and convert the whole policy to
				 * block-everything through a knob that does not
				 * name the gate. */
				agg_spin_cap_cyc = agg_us_to_cyc((uint64_t)agg_clamp(
				    agg_getenv_int("DTO_AGG_SPIN_CAP_US", 200),
				    1, 1000000));
				{
					uint64_t floor_us =
					    (8ULL * agg_model_ns(agg_block_bytes)
					    + 999ULL) / 1000ULL;	/* round up */
					uint64_t floor_cyc = agg_us_to_cyc(floor_us);

					if (agg_spin_cap_cyc < floor_cyc)
						agg_spin_cap_cyc = floor_cyc;
				}
				agg_degrade_cyc = agg_us_to_cyc(1000ULL *
				    (uint64_t)agg_clamp(agg_getenv_int(
					"DTO_AGG_DEGRADE_MS", 100), 0, 60000));
				/* How long the poller's heartbeat must stay
				 * frozen before a worker calls it dead. Must
				 * exceed any plausible run-queue delay for a
				 * SCHED_OTHER thread on an oversubscribed
				 * node, or ordinary scheduling latency turns
				 * into a process-wide fallback to spinyield. */
				agg_dead_cyc = agg_us_to_cyc(1000ULL *
				    (uint64_t)agg_clamp(agg_getenv_int(
					"DTO_AGG_DEAD_MS", 50), 1, 60000));
				agg_nslots = agg_clamp(
				    agg_getenv_int("DTO_AGG_SLOTS",
					dsa_max_threads > 0 ? dsa_max_threads : 64),
				    1, AGG_SLOTS_MAX);
				agg_cpu = agg_getenv_int("DTO_AGG_CPU", -1);
				agg_rt = agg_getenv_int("DTO_AGG_RT", 0);
				agg_nodes = 1;
				if (numa_available() != -1) {
					int mx = numa_max_node();

					if (mx > 0 && agg_nslots >=
					    2u * (uint32_t)(mx + 1))
						agg_nodes = (uint32_t)(mx + 1);
				}
				/* 999999, not 1000000: these become tv_nsec
				 * directly, and a tv_nsec of 1e9 is EINVAL,
				 * which would silently turn blocking into a
				 * spin loop. */
				agg_to_us = agg_clamp(agg_getenv_int(
				    "DTO_AGG_TIMEOUT_US", 100), 1, 999999);
				agg_to_max_us = agg_clamp(agg_getenv_int(
				    "DTO_AGG_TIMEOUT_MAX_US", 1000), 1, 999999);
				if (agg_to_max_us < agg_to_us)
					agg_to_max_us = agg_to_us;

				/* getenv on a removed name is silent, so without
				 * these a stale sweep harness produces a run that
				 * looks configured and is not. */
				if (getenv("DTO_AGG_ADAPT"))
					LOG_ERROR("DTO_AGG_ADAPT is obsolete and ignored; the spin/block decision is now DTO_AGG_BLOCK_KB\n");
				if (getenv("DTO_AGG_SPIN_MIN_US") ||
				    getenv("DTO_AGG_SPIN_MAX_US"))
					LOG_ERROR("DTO_AGG_SPIN_MIN_US/DTO_AGG_SPIN_MAX_US are obsolete and ignored; see DTO_AGG_BLOCK_KB and DTO_AGG_SPIN_CAP_US\n");
				LOG_TRACE("aggregator gate: block at > %u bytes (modelled %llu ns of device time), spin cap %llu us, postarm %llu us\n",
				    agg_block_bytes,
				    (unsigned long long)agg_model_ns(agg_block_bytes),
				    (unsigned long long)(agg_spin_cap_cyc * 1000ULL / tsc_khz),
				    (unsigned long long)(agg_postarm_cyc * 1000ULL / tsc_khz));

				/* pthread keys survive fork, so create one
				 * once per process image and never in the
				 * child handler, which would leak a key per
				 * fork. */
				if (!agg_key_ready &&
				    pthread_key_create(&agg_key, agg_slot_release) == 0)
					agg_key_ready = 1;

				if (agg_key_ready) {
					/* Built once per process image and
					 * reused. init_dto() is also reached
					 * from the pthread_atfork child
					 * handler, where POSIX allows only
					 * async-signal-safe calls:
					 * pthread_attr_init takes glibc's
					 * internal allocator lock, which a
					 * thread that did not survive the fork
					 * may have been holding. */
					if (!agg_attr_ready) {
						pthread_attr_init(&agg_attr);
						pthread_attr_setstacksize(
						    &agg_attr, 128 * 1024);
						agg_attr_ready = 1;
					}
					/* P threads, created unconditionally
					 * here, 128KB of stack each. They
					 * cannot be created lazily: doing it
					 * from the wait path would put
					 * pthread_create underneath an
					 * interposed memcpy. */
					if (agg_start_pollers() != 0)
						wait_method = WAIT_SPINYIELD;
				} else {
					LOG_ERROR("aggregator: pthread_key_create failed, using spinyield\n");
					wait_method = WAIT_SPINYIELD;
				}
			}
    

			// display configuration
			LOG_TRACE("log_level: %d, collect_stats: %d, use_std_lib_calls: %d, dsa_min_size: %lu, "
				"cpu_size_fraction: %.2f, wait_method: %s, auto_adjust_knobs: %d, numa_awareness: %s, dto_dsa_cc: %d, dto_dsa_bof: %d, dto_use_c02: %d, max_wqs_supported: %d, dsa_max_threads: %d\n",
				log_level, collect_stats, use_std_lib_calls, dsa_min_size,
				cpu_size_fraction_float, wait_names[wait_method], auto_adjust_knobs, numa_aware_names[is_numa_aware], dto_dsa_cc, dto_dsa_bof, dto_use_c02, max_wqs_supported, dsa_max_threads);
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
	/* The poller must be gone before the WQ portals are unmapped and
	 * before log_fd is closed. Every shard's degraded_until is stamped
	 * first so that no thread can enter a new aggregator wait during
	 * teardown,
	 * and every armed slot is woken so no worker is left blocked: each
	 * one re-tests its status byte, sees agg_stop and polls its own
	 * descriptor to completion. */
	if (agg_any_created) {
		struct timespec abs;
		uint32_t sidx, i;

		for (sidx = 0; sidx < agg_npollers; sidx++)
			atomic_store_explicit(&agg_shards[sidx].degraded_until,
			    ~0ull, memory_order_seq_cst);
		atomic_store_explicit(&agg_stop, 1, memory_order_seq_cst);
		atomic_store_explicit(&agg_started, 0, memory_order_seq_cst);

		/* The wake phase must COMPLETE FOR ALL SHARDS BEFORE ANY JOIN
		 * BEGINS. Waking and joining shard by shard leaves shard 1
		 * sleeping out its 100ms backstop while this code is blocked
		 * on shard 0. */
		for (sidx = 0; sidx < agg_npollers; sidx++) {
			atomic_store_explicit(&agg_hot[sidx].park, 0,
			    memory_order_seq_cst);
			agg_futex_wake(&agg_hot[sidx].park, 1);
		}
		for (sidx = 0; sidx < agg_npollers; sidx++) {
			uint32_t hi = atomic_load(&agg_hot[sidx].hi);
			uint32_t limit = agg_shards[sidx].limit;

			if (limit > AGG_SLOTS_MAX)
				limit = AGG_SLOTS_MAX;
			if (hi > limit)
				hi = limit;
			for (i = agg_shards[sidx].lo; i < hi; i++)
				agg_futex_wake(&agg_slots[i].seq, INT_MAX);
		}

		/* ONE shared ABSOLUTE deadline for every join. Per-shard
		 * deadlines would make worst-case teardown P x 200ms = 3.2s at
		 * P = 16, out of a destructor at process exit. */
		clock_gettime(CLOCK_REALTIME, &abs);
		abs.tv_nsec += 200 * 1000 * 1000;
		if (abs.tv_nsec >= NSEC_PER_SEC) {
			abs.tv_sec++;
			abs.tv_nsec -= NSEC_PER_SEC;
		}
		/* Bounded join, never pthread_cancel and never a free: every
		 * object a poller touches is .bss for the life of the
		 * process, so an unreaped poller is harmless. */
		for (sidx = 0; sidx < agg_npollers; sidx++) {
			if (!agg_shards[sidx].created)
				continue;
			if (pthread_timedjoin_np(agg_shards[sidx].tid, NULL,
			    &abs) != 0)
				LOG_ERROR("aggregator shard %u did not exit in 200ms; leaving it running\n",
				    sidx);
			else
				agg_shards[sidx].created = 0;
		}

		agg_any_created = 0;
		for (i = 0; i < agg_nslots; i++) {
			if (agg_stats[i].block || agg_stats[i].spinhit)
				LOG_TRACE("aggregator slot %u (shard %u): spinhit %u, early %u, blocked %u, spincap %u, unknown %u, timeouts %u\n",
				    i, agg_slot_shard[i],
				    agg_stats[i].spinhit,
				    agg_stats[i].early,
				    agg_stats[i].block,
				    agg_stats[i].spincap,
				    agg_stats[i].unknown,
				    agg_stats[i].timeout);
		}
	}

	{
		uint64_t ca = atomic_load(&dto_wait_calls_async);
		uint64_t cb = atomic_load(&dto_wait_calls_batch);

		if (ca | cb)
			LOG_TRACE("public wait API: async %llu calls / %llu waited, batch %llu calls / %llu waited\n",
			    (unsigned long long)ca,
			    (unsigned long long)atomic_load(&dto_wait_entered_async),
			    (unsigned long long)cb,
			    (unsigned long long)atomic_load(&dto_wait_entered_batch));
	}

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
#endif
	if (log_fd != -1)
		close(log_fd);

	cleanup_devices();
}

//static __always_inline  struct dto_wq *get_wq(void *buf) {
//    struct dto_wq* wq = NULL;
//
//    if (wq_index != -1) {
//        wq = &wqs[wq_index];
//    } else {
//        if (is_numa_aware) {
//            int status[1] = {-1};
//
//            // get the numa node for the target DSA device
//            const int numa_node = get_numa_node(pthread_getspecific(thread_buf_key));
//            if (numa_node >= 0 && numa_node < MAX_NUMA_NODES) {
//                struct dto_device* dev = devices[numa_node];
//                if (dev != NULL &&
//                    dev->num_wqs > 0) {
//                    wq = dev->wqs[dev->next_wq++ % dev->num_wqs];
//                }
//            }
//        }
//
//        if (wq == NULL) {
//            wq = &wqs[next_wq++ % num_wqs];
//        }
//    }
//
//    return wq;
//}

/* One scheduling probe: the yield round-trip time is the per-op exposure a
 * synchronous offload risks whenever its thread loses the CPU between
 * submit and completion. Windows of SHED_PROBE_WINDOW probes drive the
 * shed level with hysteresis in both directions. */
static void shed_probe(void)
{
	uint64_t t0 = __rdtsc(), dt, c;
	uint64_t avg;
	int lvl;

	sched_yield();
	dt = __rdtsc() - t0;
	thr_probe_ctr = SHED_PROBE_PERIOD - 1;

	atomic_fetch_add_explicit(&shed_probe_cycles, dt,
	    memory_order_relaxed);
	if (atomic_fetch_add_explicit(&shed_probe_count, 1,
	    memory_order_relaxed) != SHED_PROBE_WINDOW - 1)
		return;

	c = atomic_exchange(&shed_probe_cycles, 0);
	atomic_store(&shed_probe_count, 0);
	avg = c / SHED_PROBE_WINDOW;
	lvl = atomic_load(&shed_level);
	if (avg > shed_hi_cycles && lvl < SHED_LEVEL_MAX) {
		atomic_store(&shed_level, lvl + 1);
		LOG_TRACE("shed: level %d -> %d (yield avg %llu cycles)\n",
		    lvl, lvl + 1, (unsigned long long)avg);
	} else if (avg < shed_lo_cycles && lvl > 0) {
		atomic_store(&shed_level, lvl - 1);
		LOG_TRACE("shed: level %d -> %d (yield avg %llu cycles)\n",
		    lvl, lvl - 1, (unsigned long long)avg);
	}
}

/* WQ selection for a thread that holds a slot: NUMA buffer-centric when
 * enabled, else a stable thread-number mapping (same policy as fcfs). */
static __always_inline struct dto_wq *wq_for_thread(void *buf, int tnum)
{
	struct dto_wq *wq = NULL;

	if (is_numa_aware) {
		const int numa_node = get_numa_node(buf);

		if (numa_node >= 0 && numa_node < MAX_NUMA_NODES) {
			struct dto_device *dev = devices[numa_node];

			if (dev != NULL && dev->num_wqs > 0)
				wq = dev->wqs[dev->next_wq++ % dev->num_wqs];
		}
		return wq != NULL ? wq : &wqs[0];
	}
	return &wqs[tnum % num_wqs];
}

/* Application hint: the calling thread entered (active=1) or left
 * (active=0) a phase that produces offload-eligible work. Advisory and
 * optional -- resolved by callers via dlsym, no-op unless
 * DTO_DSA_ADMISSION=ticket. Counted, not boolean, so nested holders (or a
 * release arriving on a thread that never acquired, after a cross-thread
 * ticket move) degrade gracefully instead of corrupting state. */
void dto_thread_active(int active)
{
	if (active) {
		if (thr_hint < INT32_MAX)
			thr_hint++;
	} else if (thr_hint > 0) {
		if (--thr_hint == 0)
			thr_hint_linger = HINT_LINGER_OPS;
	}
}

static inline int thr_is_hinted(void)
{
	if (thr_hint > 0)
		return 1;
	if (thr_hint_linger > 0) {
		thr_hint_linger--;
		return 1;
	}
	return 0;
}

static struct dto_wq *get_wq_lfu(void *buf, size_t opsz)
{
	uint32_t idx, ops;
	uint8_t f;
	int i, victim;
	int hinted = ticket_mode ? thr_is_hinted() : 0;

	if (unlikely(thr_num < 0))
		thr_num = atomic_fetch_add(&num_threads, 1);
	idx = (uint32_t)thr_num & (FREQ_TABLE_SIZE - 1);

	/* Sampled, byte-weighted frequency bump (relaxed and racy on
	 * purpose): 1 in freq_sample eligible ops per thread updates the
	 * shared table; the rest touch only TLS. The weight scales with
	 * bytes -- 16KB counts 1, doubling per octave, capped at 8 -- so a
	 * checkpoint image outranks a stream of gate-sized ops. */
	if (thr_bump_ctr != 0) {
		thr_bump_ctr--;
		ops = 0;
	} else {
		unsigned int w = 1, nf;
		size_t v = opsz >> 14;

		thr_bump_ctr = (uint16_t)freq_sample - 1;
		while (v > 1 && w < 8) {
			v >>= 1;
			w++;
		}
		f = atomic_load_explicit(&thr_freq_tab[idx].v,
		    memory_order_relaxed);
		nf = (unsigned int)f + w;
		atomic_store_explicit(&thr_freq_tab[idx].v,
		    nf > FREQ_MAX ? FREQ_MAX : (uint8_t)nf,
		    memory_order_relaxed);

		/* Aging rides the sampled branch: one thread per window
		 * halves the table so idle holders decay. */
		ops = atomic_fetch_add_explicit(&admit_eligible_ops, 1,
		    memory_order_relaxed) + 1;
		if (unlikely((ops & (age_window_samples - 1)) == 0)) {
			uint32_t epoch = atomic_load(&admit_age_epoch);

			if (atomic_compare_exchange_strong(&admit_age_epoch,
			    &epoch, epoch + 1)) {
				for (i = 0; i < FREQ_TABLE_SIZE; i++) {
					uint8_t v2 = atomic_load_explicit(
					    &thr_freq_tab[i].v,
					    memory_order_relaxed);
					atomic_store_explicit(
					    &thr_freq_tab[i].v, v2 >> 1,
					    memory_order_relaxed);
				}
			}
		}
	}

	/* Holder fast path: still the owner iff the slot is in the live
	 * range (auto-K may have shrunk it) and the generation matches. */
	if (thr_slot >= 0) {
		if (thr_slot < atomic_load_explicit(&lfu_k,
		    memory_order_relaxed) &&
		    atomic_load_explicit(&dsa_slots[thr_slot].gen,
		    memory_order_acquire) == thr_slot_gen) {
			/* Keep the slot's hint flag fresh (store only on
			 * change; the line is effectively thread-private
			 * between challenges) so challengers can class
			 * holders without touching TLS of other threads. */
			if (ticket_mode &&
			    thr_slot_hint_mirror != (uint8_t)hinted) {
				thr_slot_hint_mirror = (uint8_t)hinted;
				atomic_store_explicit(
				    &dsa_slots[thr_slot].hinted,
				    (uint8_t)hinted, memory_order_relaxed);
			}
			return wq_for_thread(buf, thr_num);
		}
		thr_slot = -1; /* evicted or slot retired while away */
	}

	/* Challenger path, rate-limited so the scan below stays amortized
	 * noise. Falling back to the CPU here is the common, cheap case. */
	if (thr_backoff-- != 0)
		return NULL;
	if (hinted) {
		/* Ticketed threads are known-eligible by construction; the
		 * short fixed cadence (plus the global gap below) is all the
		 * rate limiting they need. */
		thr_backoff = 4;
	} else {
	/* Adaptive cadence: a slotless-but-hot thread re-challenges within a
	 * few ops (its fallback work is exactly what should be offloaded),
	 * while cold threads keep the long period that makes the slot scan
	 * amortized noise. */
	f = atomic_load_explicit(&thr_freq_tab[idx].v, memory_order_relaxed);
	thr_backoff = ADMIT_APPLY_PERIOD >> (f >> 5);
	if (thr_backoff < 4)
		thr_backoff = 4;
	}

	/* Global cap: at most one challenge (one K-wide victim scan) per
	 * CHALLENGE_GLOBAL_GAP sampled ops, however many slotless threads
	 * are hot. Losing the CAS just means CPU for this op. */
	{
		uint32_t now = atomic_load_explicit(&admit_eligible_ops,
		    memory_order_relaxed);
		uint32_t stamp = atomic_load_explicit(&challenge_stamp,
		    memory_order_relaxed);

		if (now - stamp < CHALLENGE_GLOBAL_GAP)
			return NULL;
		if (!atomic_compare_exchange_strong(&challenge_stamp, &stamp,
		    now))
			return NULL;
	}

	victim = 0;
	{
		uint8_t vmin = 255, uvmin = 255;
		int uvictim = -1;
		int32_t owner;

		int kNow = atomic_load_explicit(&lfu_k, memory_order_relaxed);

		for (i = 0; i < kNow; i++) {
			owner = atomic_load_explicit(&dsa_slots[i].owner,
			    memory_order_relaxed);
			if (owner < 0) {
				victim = i;
				vmin = 0;
				break;
			}
			f = atomic_load_explicit(
			    &thr_freq_tab[(uint32_t)owner &
			    (FREQ_TABLE_SIZE - 1)].v, memory_order_relaxed);
			if (f < vmin) {
				vmin = f;
				victim = i;
			}
			if (ticket_mode &&
			    !atomic_load_explicit(&dsa_slots[i].hinted,
			    memory_order_relaxed) && f < uvmin) {
				uvmin = f;
				uvictim = i;
			}
		}
		/* Ticket outranks statistics: a hinted challenger takes the
		 * coldest unhinted slot without a frequency test (the hint is
		 * exact evidence of eligibility; the holder's isn't). Among
		 * hinted holders -- or for unhinted challengers -- frequency
		 * stays the arbiter, so daemons can still displace an idle
		 * decayed holder and churn stays bounded. */
		if (vmin != 0 && hinted && uvictim >= 0) {
			victim = uvictim;
		} else if (vmin != 0) {
			f = atomic_load_explicit(&thr_freq_tab[idx].v,
			    memory_order_relaxed);
			/* TinyLFU admission: candidate must beat the victim. */
			if (f <= vmin)
				return NULL;
		}
	}

	{
		int32_t old = atomic_load_explicit(&dsa_slots[victim].owner,
		    memory_order_relaxed);

		/* Losing the race just means CPU for this op; the thread
		 * challenges again after the backoff. The generation bump
		 * lands after the owner swap, so a deposed holder can
		 * overlap for at most its in-flight op (a transient K+1
		 * submitters, harmless: every thread has its own
		 * descriptor and completion record). */
		if (!atomic_compare_exchange_strong(&dsa_slots[victim].owner,
		    &old, thr_num))
			return NULL;
		thr_slot_gen = atomic_fetch_add(&dsa_slots[victim].gen, 1) + 1;
		thr_slot = (int16_t)victim;
		if (ticket_mode) {
			thr_slot_hint_mirror = (uint8_t)hinted;
			atomic_store_explicit(&dsa_slots[victim].hinted,
			    (uint8_t)hinted, memory_order_relaxed);
		}
	}
	return wq_for_thread(buf, thr_num);
}

static __always_inline  struct dto_wq *get_wq_inner(void* buf, size_t opsz)
{
	struct dto_wq* wq = NULL;

	if (dto_shed) {
		int lvl;

		if (thr_probe_ctr != 0)
			thr_probe_ctr--;
		else
			shed_probe();
		lvl = atomic_load_explicit(&shed_level,
		    memory_order_relaxed);
		if (lvl != 0 && opsz < ((size_t)SHED_BASE_BYTES << lvl))
			return NULL;	/* callers fall back to the CPU */
	}
	if (dsa_admission == ADMIT_LFU)
		return get_wq_lfu(buf, opsz);
        if (wq_index >= 0) {
            wq = &wqs[wq_index];
            __builtin_prefetch(wq, 0, 3);
            return wq;
        }
        if (wq_index == -2)
            return NULL;  /* thread exceeded max threads limit */

	/* First DSA use for this thread: assign a thread number and apply the
	 * DSA thread cap ONCE, independent of NUMA-awareness. Previously the
	 * is_numa_aware branch selected a WQ before this check, so the cap was
	 * skipped in buffer-centric mode (all threads used DSA). */
	if (wq_index == -1) {
		int my_thread_num = atomic_fetch_add(&num_threads, 1) + 1;
		if (dsa_max_threads > 0 && my_thread_num > dsa_max_threads) {
			thr_dsa_disabled = 1;
			wq_index = -2;  /* sentinel: this thread uses CPU only */
			LOG_TRACE("Thread id %lu (tn: %d) exceeded max threads (%d), using CPU\n",
				pthread_self(), my_thread_num, dsa_max_threads);
			return NULL;
		}
		if (is_numa_aware) {
			wq_index = -3;  /* DSA-enabled; pick a node-local WQ per buffer */
		} else {
			wq_index = my_thread_num % num_wqs;
			LOG_TRACE("Thread id %lu (tn: %d) assigned wq: %d\n", pthread_self(), my_thread_num, wq_index);
			return &wqs[wq_index];
		}
	}

	/* wq_index == -3: NUMA-aware buffer-centric selection (per target buffer) */
	if (is_numa_aware) {
		const int numa_node = get_numa_node(buf);
		if (numa_node >= 0 && numa_node < MAX_NUMA_NODES) {
			struct dto_device* dev = devices[numa_node];
			if (dev != NULL && dev->num_wqs > 0)
				wq = dev->wqs[dev->next_wq++ % dev->num_wqs];
		}
	}
	if (wq == NULL)
		wq = &wqs[0];  /* fallback if NUMA lookup fails */

	return wq;
}

/* The aggregator slot is claimed here and nowhere else, for two reasons.
 * Claiming only on a non-NULL return keeps threads that the dsa_max_threads
 * admission check is about to reject from burning slots, which in a process
 * with hundreds of threads would exhaust the table and starve the threads
 * that actually offload. And because get_wq() always runs before the
 * thr_desc.completion_addr assignment of an op, the redirection of thr_compp
 * can never move while a descriptor is in flight -- which would make the
 * worker read bytes_completed/result out of a record the device is not
 * writing. */
static __always_inline struct dto_wq *get_wq(void* buf, size_t opsz)
{
	struct dto_wq *wq = get_wq_inner(buf, opsz);

	if (unlikely(wq != NULL && wait_method == WAIT_AGGREGATOR &&
	    atomic_load_explicit(&agg_started, memory_order_relaxed))) {
		if (thr_agg_slot == -1)
			agg_claim_slot();
		else if (thr_agg_slot == -2 && --thr_agg_retry == 0) {
			thr_agg_slot = -1;	/* periodic re-claim attempt */
			agg_claim_slot();
		}
	}
	return wq;
}

/* WQ selection for a submit whose completion record is NOT thr_comp: the
 * public caller-owned async and batch ops, and the TLS-completion batch path.
 * Identical to get_wq() minus the claim, and the omission is structural
 * rather than an optimisation. Those waits reach dsa_wait_spinyield either
 * directly (dto_wait_caller_owned, which by construction never calls
 * dsa_wait_aggregator) or through dsa_wait_aggregator's pointer-identity
 * test, which rejects any comp that is not &thr_compp->status. So such a
 * thread can never arm the slot it would claim, yet would hold it until it
 * exits -- and agg_nslots defaults to dsa_max_threads or 64, not 512. In a
 * process where those threads outnumber the table (mongod's log and eviction
 * threads use only the public API) every later thread, INCLUDING the
 * interposed-memcpy threads that are the only ones able to block, gets
 * thr_agg_slot = -2 and spinyields forever. The claim gate in get_wq() was
 * written to keep threads the dsa_max_threads cap rejects from burning slots;
 * this is the same hazard from threads the cap accepts. */
static __always_inline struct dto_wq *get_wq_no_agg(void* buf, size_t opsz)
{
	return get_wq_inner(buf, opsz);
}

static void dto_memset_api(void *s, int c, size_t n)
{
        int r = 0;
        int *result = &r;

	uint64_t memset_pattern;
	size_t cpu_size, dsa_size;
	struct dto_wq *wq = get_wq(s, n);

	if (unlikely(wq == NULL)) {
		/* Not admitted to a DSA slot: report zero progress so the
		 * interposer completes the operation with the std call. */
		thr_bytes_completed = 0;
		return;
	}

	for (int i = 0; i < 8; ++i)
		((uint8_t *) &memset_pattern)[i] = (uint8_t) c;

	thr_desc.opcode = DSA_OPCODE_MEMFILL;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
        if (dto_dsa_bof)
            thr_desc.flags |= IDXD_OP_FLAG_BOF;
	thr_desc.completion_addr = (uint64_t)&thr_comp;
	thr_desc.pattern = memset_pattern;

	/* cpu_size_fraction guaranteed to be >= 0 and < 100 */
        uint64_t cpu_frac = auto_adjust_knobs == AUTO_ADJUST_KNOBS_V2 ?
            tl_cpu_size_fraction : cpu_size_fraction;
	cpu_size = n * cpu_frac / 100;
	dsa_size = n - cpu_size;

	thr_bytes_completed = 0;
	if (dsa_size <= wq->max_transfer_size) {
		thr_desc.dst_addr = (uint64_t) s + cpu_size;
		thr_desc.xfer_size = (uint32_t) dsa_size;
		thr_comp.status = 0;
		*result = dsa_submit(wq, &thr_desc);
		if (likely(*result == SUCCESS)) {
			if (cpu_size) {
				orig_memset(s, c, cpu_size);
				thr_bytes_completed = cpu_size;
			}
			*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
		}
	} else {
		uint32_t threshold;
		size_t current_cpu_size_fraction = cpu_frac;  // the cpu_size_fraction might be changed by the auto tune algorithm
		threshold = wq->max_transfer_size * 100 / (100 - current_cpu_size_fraction);

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

					orig_memset(s1, c, cpu_size);
					thr_bytes_completed += cpu_size;
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

static void dto_memset(void *s, int c, size_t n, int *result)
{
	uint64_t memset_pattern;
	size_t cpu_size, dsa_size;
	struct dto_wq *wq = get_wq(s, n);

	if (unlikely(wq == NULL)) {
		*result = -1;
		thr_bytes_completed = 0;
		return;
	}

	for (int i = 0; i < 8; ++i)
		((uint8_t *) &memset_pattern)[i] = (uint8_t) c;

	thr_desc.opcode = DSA_OPCODE_MEMFILL;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
	thr_desc.completion_addr = (uint64_t)&thr_comp;
	thr_desc.pattern = memset_pattern;

	/* cpu_size_fraction guaranteed to be >= 0 and < 100 */
        uint64_t cpu_frac = auto_adjust_knobs == AUTO_ADJUST_KNOBS_V2 ?
            tl_cpu_size_fraction : cpu_size_fraction;
	cpu_size = n * cpu_frac / 100;
	dsa_size = n - cpu_size;

	thr_bytes_completed = 0;
	if (dsa_size <= wq->max_transfer_size) {
		thr_desc.dst_addr = (uint64_t) s + cpu_size;
		thr_desc.xfer_size = (uint32_t) dsa_size;
		thr_comp.status = 0;
		*result = dsa_submit(wq, &thr_desc);
		if (likely(*result == SUCCESS)) {
			if (cpu_size) {
				orig_memset(s, c, cpu_size);
				thr_bytes_completed = cpu_size;
			}
			*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
		}
	} else {
		uint32_t threshold;
		size_t current_cpu_size_fraction = cpu_frac;  // the cpu_size_fraction might be changed by the auto tune algorithm
		threshold = wq->max_transfer_size * 100 / (100 - current_cpu_size_fraction);

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

					orig_memset(s1, c, cpu_size);
					thr_bytes_completed += cpu_size;
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

/**
 * dto_memset_pages - Zero-fill memory pages using DSA descriptors
 * @start_addr: Starting virtual address (should be page-aligned)
 * @end_addr: Ending virtual address (exclusive)
 * @page_size: Size of each page in bytes
 *
 * Distributes work across all available DSA work queues in round-robin
 * fashion for optimal performance. Falls back to CPU memset on errors.
 *
 * Note: Addresses should be page-aligned for best results.
 */
__attribute__((visibility("default"))) void dto_memset_pages(void *start_addr, void *end_addr, size_t page_size)
{
	size_t total_size, num_pages, i;
	struct dsa_hw_desc descs[MAX_PAGES_PER_CALL];
	struct dsa_completion_record comps[MAX_PAGES_PER_CALL] __attribute__((aligned(32)));
        orig_memset(comps, 0, sizeof(comps));
        orig_memset(descs, 0, sizeof(descs));
	uint8_t wq_indices[MAX_PAGES_PER_CALL];
	bool submitted[MAX_PAGES_PER_CALL];
	uint64_t zero_pattern = 0;
	size_t pages_to_process;
	void *current_addr;

	/* Input validation */
	if (start_addr >= end_addr || page_size == 0)
		return;

	total_size = (size_t)((char *)end_addr - (char *)start_addr);
	num_pages = total_size / page_size;

	if (num_pages == 0)
		return;

	/* Fall back to CPU if no work queues available or thread limit exceeded */
	if (num_wqs == 0 || !dto_dsa_memset || thr_dsa_disabled) {
		orig_memset(start_addr, 0, total_size);
		return;
	}

	current_addr = start_addr;

	/* Process pages in batches of MAX_PAGES_PER_CALL */
	while (num_pages > 0) {
		pages_to_process = num_pages > MAX_PAGES_PER_CALL ? MAX_PAGES_PER_CALL : num_pages;

		/* Initialize tracking arrays */
		for (i = 0; i < pages_to_process; i++) {
			submitted[i] = false;
		}

		/* Submit phase - distribute descriptors across all WQs in round-robin */
		for (i = 0; i < pages_to_process; i++) {
			uint8_t wq_idx = memset_pages_rr++ % num_wqs;
			struct dto_wq *wq = &wqs[wq_idx];
			void *page_addr = (char *)current_addr + (i * page_size);

			/* Check if page_size exceeds max transfer size */
			if (page_size > wq->max_transfer_size) {
				/* Fall back to CPU for this page */
				orig_memset(page_addr, 0, page_size);
				continue;
			}

			/* Setup descriptor for memfill operation */
			descs[i].opcode = DSA_OPCODE_MEMFILL;
			descs[i].flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_BOF;

			/* Add cache control if supported */
			if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
				descs[i].flags |= IDXD_OP_FLAG_CC;

			descs[i].completion_addr = (uint64_t)&comps[i];
			descs[i].dst_addr = (uint64_t)page_addr;
			descs[i].xfer_size = (uint32_t)page_size;
			descs[i].pattern = zero_pattern;

			/* Initialize completion record */
			comps[i].status = 0;

			/* Prefetch WQ portal for better performance */
			__builtin_prefetch(wq->wq_portal, 1, 3);

			/* Submit descriptor */
			int ret = dsa_submit(wq, &descs[i]);
			if (ret == SUCCESS) {
				wq_indices[i] = wq_idx;
				submitted[i] = true;
			} else {
				/* Submission failed, fall back to CPU for this page */
				orig_memset(page_addr, 0, page_size);
			}
		}

		/* Wait phase - wait for all submitted descriptors */
		for (i = 0; i < pages_to_process; i++) {
			if (submitted[i]) {
				struct dto_wq *wq = &wqs[wq_indices[i]];
				void *page_addr = (char *)current_addr + (i * page_size);

				int ret = dsa_wait(wq, &descs[i], &comps[i].status);

				/* On error, fall back to CPU */
				if (ret != SUCCESS) {
					orig_memset(page_addr, 0, page_size);
				}
			}
		}

		/* Update for next batch */
		current_addr = (char *)current_addr + (pages_to_process * page_size);
                //fprintf(stderr, "Processed %zu pages, %zu remaining\n", pages_to_process, num_pages - pages_to_process);
		num_pages -= pages_to_process;
	}
}

/* For overlapping src & dest buffers in memmove API, we can't split the memmove
 * job. Otherwise, it may lead to incorrect copy operation.
 */
static bool is_overlapping_buffers (void *dest, const void *src, size_t n)
{
	if ((dest + n) < src || (src + n) < dest)
		return false;

	return true;
}

__attribute__((visibility("default"))) uint64_t dto_crc(const void *src, size_t n, callback_t cb, void* args) {
	//submit dsa work if successful, call the callback
        if (use_std_lib_calls || thr_dsa_disabled || n < crc_dsa_min_size()) {
                if (cb) {
		    cb(args);
                }
                return crc32c_hw(src, n);
        }
	int result = 0;
	struct dto_wq *wq = get_wq(src, n);
	if (unlikely(wq == NULL)) {
		if (cb) cb(args);
		return crc32c_hw(src, n);
	}
	size_t dsa_size = n;
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	DTO_COLLECT_STATS_START(collect_stats, st);
#endif

	thr_desc.opcode = DSA_OPCODE_CRCGEN;
	/* Note: IDXD_OP_FLAG_CC must NOT be set for CRC Generation; the
	 * operation has no destination and the device fails the descriptor
	 * with DSA_COMP_INVALID_FLAGS (0x11). */
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_BOF;
	thr_desc.completion_addr = (uint64_t)&thr_comp;

	thr_bytes_completed = 0;
	thr_desc.src_addr = (uint64_t) src;
        thr_desc.dst_addr = 0; // dst_addr is not used for CRC generation
	thr_desc.xfer_size = (uint32_t) dsa_size;
        thr_desc.crc_seed = DSA_CRC_SEED_FOR_RAW;
        thr_desc.rsvd = 0;
	thr_comp.status = 0;
	result = dsa_submit(wq, &thr_desc);
	if (result == SUCCESS) {
                if (cb) {
		    cb(args);
                }
		result = dsa_wait(wq, &thr_desc, &thr_comp.status);
	}
	/* crc_seed overlays a reserved-must-be-zero field for non-CRC opcodes
	 * that share this thread-local descriptor; leaving it set would fail
	 * subsequent ops with DSA_COMP_NOZERO_RESERVE (0x12). */
	thr_desc.crc_seed = 0;
	{
		uint64_t done = thr_bytes_completed;
		uint64_t dev_crc = thr_comp.crc_val;

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCOPY_ASYNC, n, done, result);
#endif
		if (done < n)
			return 0;
		return DSA_CRC_VAL_TO_RAW(dev_crc);
	}
}

/* CPU CRC32C in the iSCSI presentation (seed and result inverted), matching
 * the device's default CRCGEN convention: f(0, data) is standard CRC32C. */
static uint32_t crc32c_iscsi_cpu(uint32_t seed, const uint8_t *data, size_t len)
{
	uint32_t crc = ~seed;

	while (len >= sizeof(uint64_t)) {
		crc = (uint32_t)_mm_crc32_u64(crc, *(const uint64_t *)data);
		data += sizeof(uint64_t);
		len -= sizeof(uint64_t);
	}
	while (len--)
		crc = _mm_crc32_u8(crc, *data++);
	return ~crc;
}

/* Seeded CRC32C in the iSCSI presentation. The device's default CRC seed and
 * result inversion implements this convention directly, so the descriptor
 * takes the caller's seed unchanged and the completion crc_val is returned
 * unchanged; chaining across chunks behaves exactly like the CPU loop.
 * CRC generation has no destination, so every failure path (below the CRC
 * gate, no WQ, oversized transfer, submit failure, partial completion) can
 * safely redo the whole buffer on the CPU. */
__attribute__((visibility("default")))
uint32_t dto_crc32c_with_seed(uint32_t seed, const void *src, size_t n)
{
	struct dto_wq *wq;
	uint32_t crc;
	int result;
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
#endif

	if (use_std_lib_calls || thr_dsa_disabled || n < crc_dsa_min_size()) {
		DTO_COLLECT_STATS_START(collect_stats, st);
		crc = crc32c_iscsi_cpu(seed, src, n);
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, CRC, n, n);
		return crc;
	}

	wq = get_wq((void *)src, n);
	if (unlikely(wq == NULL) || n > wq->max_transfer_size) {
		DTO_COLLECT_STATS_START(collect_stats, st);
		crc = crc32c_iscsi_cpu(seed, src, n);
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, CRC, n, n);
		return crc;
	}

	DTO_COLLECT_STATS_START(collect_stats, st);
	thr_desc.opcode = DSA_OPCODE_CRCGEN;
	/* CC is invalid for CRC generation (no destination); BOF only when the
	 * WQ configuration allows it. */
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_bof)
		thr_desc.flags |= IDXD_OP_FLAG_BOF;
	thr_desc.completion_addr = (uint64_t)&thr_comp;
	thr_desc.src_addr = (uint64_t)src;
	thr_desc.dst_addr = 0;
	thr_desc.xfer_size = (uint32_t)n;
	thr_desc.crc_seed = seed;
	thr_desc.rsvd = 0;
	thr_comp.status = 0;
	thr_bytes_completed = 0;

	result = dsa_submit(wq, &thr_desc);
	if (result == SUCCESS)
		result = dsa_wait(wq, &thr_desc, &thr_comp.status);
	/* crc_seed overlays a reserved-must-be-zero field for non-CRC opcodes
	 * that share this thread-local descriptor. */
	thr_desc.crc_seed = 0;
	/* Consume the completion record before anything that could re-enter
	 * the library on this thread (the stats epilogue may run the periodic
	 * dump); thr_comp and thr_bytes_completed are shared by every op. */
	{
		uint64_t done = thr_bytes_completed;
		uint32_t dev_crc = (uint32_t)thr_comp.crc_val;

		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, CRC, n,
		    done, result);
		if (done >= n)
			return dev_crc;
	}
	DTO_COLLECT_STATS_START(collect_stats, st);
	crc = crc32c_iscsi_cpu(seed, src, n);
	DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, CRC, n, n);
	return crc;
}

__attribute__((visibility("default"))) uint64_t dto_memcpy_crc_async(void *dest, const void *src, size_t n, callback_t cb, void* args) {
	//submit dsa work if successful, call the callback
        if (use_std_lib_calls || thr_dsa_disabled || n < crc_dsa_min_size()) {
                if (cb) {
		    cb(args);
                }
                orig_memcpy(dest, src, n);
                return crc32c_hw(src, n);
        }
	int result = 0;
	struct dto_wq *wq = get_wq(dest, n);
	if (unlikely(wq == NULL)) {
		if (cb) cb(args);
		orig_memcpy(dest, src, n);
		return crc32c_hw(src, n);
	}
	size_t dsa_size = n;
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	DTO_COLLECT_STATS_START(collect_stats, st);
#endif

	thr_desc.opcode = DSA_OPCODE_COPY_CRC;
	/* See dto_crc for the CRC seed/result convention. CC is valid here
	 * (the operation writes to dest), unlike for CRC Generation. BOF only
	 * when the WQ allows it: on a block_on_fault=0 queue the flag fails
	 * every descriptor with DSA_COMP_INVALID_FLAGS. */
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_bof)
		thr_desc.flags |= IDXD_OP_FLAG_BOF;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
	thr_desc.completion_addr = (uint64_t)&thr_comp;

	thr_bytes_completed = 0;
	thr_desc.src_addr = (uint64_t) src;
	thr_desc.dst_addr = (uint64_t) dest;
	thr_desc.xfer_size = (uint32_t) dsa_size;
        thr_desc.crc_seed = DSA_CRC_SEED_FOR_RAW;
        thr_desc.rsvd = 0;
	thr_comp.status = 0;
	result = dsa_submit(wq, &thr_desc);
	if (result == SUCCESS) {
                if (cb) {
		    cb(args);
                }
		result = dsa_wait(wq, &thr_desc, &thr_comp.status);
	}
	/* See dto_crc: reset the reserved-overlaying seed field. */
	thr_desc.crc_seed = 0;
	{
		uint64_t done = thr_bytes_completed;
		uint64_t dev_crc = thr_comp.crc_val;

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCOPY_ASYNC, n, done, result);
#endif
		if (done < n)
			return 0;
		return DSA_CRC_VAL_TO_RAW(dev_crc);
	}
}

/* ---- True-async CRC / Copy+CRC implementation (see dto.h) ---- */

/* A wait turns "never submitted" from a wrong answer into a permanent stall:
 * dto_async_poll on an un-submitted op reads uninitialised caller storage and
 * returns garbage once, but dto_async_wait would spin forever on a status
 * byte no device will ever write -- and dto.h's own example, like
 * WiredTiger's call site, puts the op on the stack. The magic is written as
 * the FIRST statement of the submit path, ahead of every early return, which
 * is what makes a REUSED op safe, and again immediately before the submitted
 * return. One store on a cold path. The 2^-32 false positive against stack
 * garbage is a probabilistic guard, accepted because the alternative is an
 * unconditional hang. */
#define DTO_OP_SUBMITTED_MAGIC 0x64746F53u	/* "dtoS" */

struct dto_async_op_impl {
	struct dsa_hw_desc desc;	/* 64 bytes, 64-aligned via dto_async_op */
	struct dsa_completion_record comp __attribute__((aligned(32)));
	uint32_t submitted;
};
_Static_assert(sizeof(struct dto_async_op_impl) <= sizeof(dto_async_op),
	       "dto_async_op opaque storage too small");
_Static_assert(sizeof(struct dsa_hw_desc) == 64, "unexpected descriptor size");

/* crc_seed_raw selects the CRC presentation: DSA_CRC_SEED_FOR_RAW yields the
 * raw CRC32C reported by dto_async_crc_val, while a caller-supplied seed (0
 * for a fresh checksum) yields the iSCSI presentation of
 * dto_crc32c_with_seed, which is what WiredTiger checksums use. */
static int dto_submit_async_common_seeded(dto_async_op *op, uint32_t opcode,
				   void *dest, const void *src, size_t n,
				   int cache_control, uint32_t crc_seed)
{
	struct dto_async_op_impl *impl = (struct dto_async_op_impl *)op;
	struct dto_wq *wq;

	impl->submitted = 0;		/* first, ahead of every early return */
	if (n == 0 || n > UINT32_MAX)
		return DTO_ASYNC_FALLBACK;
	if (use_std_lib_calls || thr_dsa_disabled || n < crc_dsa_min_size())
		return DTO_ASYNC_FALLBACK;
	wq = get_wq_no_agg(dest ? dest : (void *)src, n);
	if (unlikely(wq == NULL))
		return DTO_ASYNC_FALLBACK;

	memset(&impl->desc, 0, sizeof(impl->desc));
	impl->desc.opcode = opcode;
	impl->desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_bof)
		impl->desc.flags |= IDXD_OP_FLAG_BOF;
	/* CC is only legal for operations with a destination; CRC Generation
	 * would be failed by the device with DSA_COMP_INVALID_FLAGS. */
	if (cache_control && dest && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		impl->desc.flags |= IDXD_OP_FLAG_CC;
	impl->desc.completion_addr = (uint64_t)&impl->comp;
	impl->desc.src_addr = (uint64_t)src;
	impl->desc.dst_addr = (uint64_t)dest;
	impl->desc.xfer_size = (uint32_t)n;
	if (opcode == DSA_OPCODE_COPY_CRC || opcode == DSA_OPCODE_CRCGEN)
		impl->desc.crc_seed = crc_seed;
	impl->comp.status = 0;

	/* ENQCMD to a shared WQ can transiently fail when the queue is full;
	 * retry briefly before giving up so momentary bursts don't push work
	 * back onto the CPU. */
	for (int attempt = 0; ; attempt++) {
		int rc = dsa_submit(wq, &impl->desc);
		if (rc == SUCCESS) {
			impl->submitted = DTO_OP_SUBMITTED_MAGIC;
			return DTO_ASYNC_SUBMITTED;
		}
		if (rc != RETRY || attempt >= 16)
			return DTO_ASYNC_FALLBACK;
		_mm_pause();
	}
}

struct dto_batch_op {
	struct dsa_hw_desc desc __attribute__((aligned(64)));
	struct dsa_completion_record comp __attribute__((aligned(32)));
	struct dsa_hw_desc descs[DTO_BATCH_MAX] __attribute__((aligned(64)));
	struct dsa_completion_record comps[DTO_BATCH_MAX] __attribute__((aligned(32)));
	void *dst[DTO_BATCH_MAX];
	void *src[DTO_BATCH_MAX];
	size_t sizes[DTO_BATCH_MAX];
	int count;
};

__attribute__((visibility("default")))
dto_batch_op *dto_batch_op_new(void)
{
	void *p = NULL;
	if (posix_memalign(&p, 64, sizeof(struct dto_batch_op)))
		return NULL;
	orig_memset(p, 0, sizeof(struct dto_batch_op));
	return (dto_batch_op *)p;
}

__attribute__((visibility("default")))
void dto_batch_op_free(dto_batch_op *op)
{
	free(op);
}

__attribute__((visibility("default")))
int dto_submit_batch_copy(dto_batch_op *op, void **dst, void **src,
			  size_t *sizes, int count)
{
	/* Mean batch size, not just the first call: these paths exist to
	 * amortize the ~0.7us descriptor cost over many copies, and a mean
	 * near 1 means they are paying it per copy instead. */
	{
		static _Atomic uint64_t n_calls, n_members;
		uint64_t c = atomic_fetch_add_explicit(&n_calls, 1,
		    memory_order_relaxed) + 1;

		atomic_fetch_add_explicit(&n_members, (uint64_t)count,
		    memory_order_relaxed);
		if (c == 1 || c == 1000 || c == 100000)
			LOG_ERROR("public API: dto_submit_batch_copy call %llu, this count=%d, mean batch %.2f\n",
			    (unsigned long long)c, count,
			    (double)atomic_load(&n_members) / (double)c);
	}
	struct dto_wq *wq;

	/* count == 0 is the exact "nothing was submitted" test: a successful
	 * submit requires count >= 2, dto_batch_op is opaque to callers, so no
	 * new field and no ABI change. Clearing it on EVERY fallback return
	 * also clears a stale count left on a reused op. It fixes a live
	 * latent bug: the ENQCMD retry loop below can return
	 * DTO_ASYNC_FALLBACK after op->count and op->comp.status have already
	 * been set, leaving count > 0 with status 0 -- on which dto_batch_wait
	 * would block forever and a caller spinning on dto_batch_poll already
	 * spins forever today. */
	op->count = 0;
	/* a DSA batch needs at least two descriptors */
	/* count == 1 is accepted and submitted as a PLAIN descriptor below, not
	 * as a one-member batch (a DSA batch descriptor requires at least two).
	 * Rejecting it made the whole path inert for the caller that motivated
	 * it: WiredTiger's reconciliation drains its accumulator at every image
	 * grow, boundary check, split and write, and with large values a single
	 * copy lands between two of those drains almost every time -- measured
	 * count=1 on the first and every subsequent flush. A single large copy
	 * still gets the accelerator and still gets the wait; only the
	 * fixed-cost amortization that batching adds is absent. */
	if (unlikely(dto_initialized == 0 || count < 1 || count > DTO_BATCH_MAX ||
		     thr_dsa_disabled || use_std_lib_calls))
		return DTO_ASYNC_FALLBACK;
	wq = get_wq_no_agg(dst[0], sizes[0]);
	if (unlikely(wq == NULL))
		return DTO_ASYNC_FALLBACK;

	orig_memset(op->descs, 0, sizeof(op->descs[0]) * count);
	for (int i = 0; i < count; i++) {
		struct dsa_hw_desc *desc = &op->descs[i];
		if (sizes[i] == 0 || sizes[i] > wq->max_transfer_size) {
			op->count = 0;
			return DTO_ASYNC_FALLBACK;
		}
		desc->opcode = DSA_OPCODE_MEMMOVE;
		desc->flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
		if (dto_dsa_bof)
			desc->flags |= IDXD_OP_FLAG_BOF;
		if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
			desc->flags |= IDXD_OP_FLAG_CC;
		desc->src_addr = (uint64_t)src[i];
		desc->dst_addr = (uint64_t)dst[i];
		desc->xfer_size = (uint32_t)sizes[i];
		desc->completion_addr = (uint64_t)&op->comps[i];
		op->comps[i].status = 0;
		op->dst[i] = dst[i];
		op->src[i] = src[i];
		op->sizes[i] = sizes[i];
	}
	op->count = count;
	orig_memset(&op->desc, 0, sizeof(op->desc));
	if (count == 1) {
		/* Same completion record the batch descriptor would have used,
		 * so dto_batch_poll is unchanged: on failure op->comps[0] was
		 * zeroed above and never written, so its repair loop redoes
		 * this copy on the CPU exactly as it would a failed member. */
		op->desc = op->descs[0];
		op->desc.completion_addr = (uint64_t)&op->comp;
	} else {
		op->desc.opcode = DSA_OPCODE_BATCH;
		op->desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
		op->desc.desc_list_addr = (uint64_t)op->descs;
		op->desc.desc_count = count;
		op->desc.completion_addr = (uint64_t)&op->comp;
	}
	op->comp.status = 0;

	for (int attempt = 0; ; attempt++) {
		int rc = dsa_submit(wq, &op->desc);
		if (rc == SUCCESS)
			return DTO_ASYNC_SUBMITTED;
		if (rc != RETRY || attempt >= 16) {
			op->count = 0;
			return DTO_ASYNC_FALLBACK;
		}
		_mm_pause();
	}
}

__attribute__((visibility("default")))
int dto_batch_poll(dto_batch_op *op)
{
	uint8_t status = __atomic_load_n((uint8_t *)&op->comp.status,
					 __ATOMIC_ACQUIRE);
	if (status == 0)
		return DTO_ASYNC_PENDING;
	if (likely(status == DSA_COMP_SUCCESS))
		return DTO_ASYNC_DONE;
	{
		static int logged;
		if (logged < 3) {
			logged++;
			LOG_ERROR("async batch copy failed with status %x, redoing failed copies on the CPU\n", status);
		}
	}
	for (int i = 0; i < op->count; i++) {
		if (op->comps[i].status != DSA_COMP_SUCCESS)
			orig_memcpy(op->dst[i], op->src[i], op->sizes[i]);
	}
	return DTO_ASYNC_DONE;
}

/*
 * dto_batch_wait --
 *	Block until the batch's completion record is written. WAITS ONLY:
 *	dto_batch_poll still has to be called afterwards, or the CPU repair of
 *	copies the accelerator failed never happens. Returns void so the poll
 *	loop stays at the call site; see dto.h.
 */
__attribute__((visibility("default")))
void dto_batch_wait(dto_batch_op *op)
{
	uint64_t bytes = 0;
	int i;

	atomic_fetch_add_explicit(&dto_wait_calls_batch, 1,
	    memory_order_relaxed);
	if (op->count == 0)		/* never submitted, or fell back */
		return;
	if (__atomic_load_n((uint8_t *)&op->comp.status, __ATOMIC_ACQUIRE))
		return;
	/* op->desc is a DSA_OPCODE_BATCH descriptor: dto_submit_batch_copy
	 * zeroes it and then sets only opcode/flags/desc_list_addr/desc_count/
	 * completion_addr, so its xfer_size field reads 0 -- and for a batch
	 * that field is a descriptor count in any case, never a byte count.
	 * The gate needs total bytes, so sum the members. uint64 because 64
	 * members of up to max_transfer_size each would wrap a uint32. */
	for (i = 0; i < op->count; i++)
		bytes += op->sizes[i];
	if (atomic_exchange_explicit(&dto_wait_logged_batch, 1u,
	    memory_order_relaxed) == 0)
		LOG_ERROR("public wait API: first dto_batch_wait, %llu bytes over %d copies (wait method %s)\n",
		    (unsigned long long)bytes, op->count,
		    wait_names[wait_method]);
	atomic_fetch_add_explicit(&dto_wait_entered_batch, 1,
	    memory_order_relaxed);
	dto_wait_caller_owned((const volatile uint8_t *)&op->comp.status,
	    bytes);
}

__attribute__((visibility("default")))
int dto_submit_memcpy_crc(dto_async_op *op, void *dest, const void *src,
			  size_t n, int cache_control)
{
	return dto_submit_async_common_seeded(op, DSA_OPCODE_COPY_CRC, dest, src, n,
				       cache_control, DSA_CRC_SEED_FOR_RAW);
}

/*
 * dto_submit_memcpy_crc32c --
 *	Fused copy + CRC32C in the seeded (iSCSI) presentation, matching
 *	dto_crc32c_with_seed and therefore WiredTiger block/log checksums.
 *	Read the result with dto_async_crc32c_val, not dto_async_crc_val.
 */
__attribute__((visibility("default")))
int dto_submit_memcpy_crc32c(dto_async_op *op, void *dest, const void *src,
			     size_t n, uint32_t seed, int cache_control)
{
	if (atomic_exchange_explicit(&dto_sub_logged_crc, 1u,
	    memory_order_relaxed) == 0)
		LOG_ERROR("public API: first dto_submit_memcpy_crc32c, n=%zu\n", n);
	return dto_submit_async_common_seeded(op, DSA_OPCODE_COPY_CRC, dest, src, n,
				       cache_control, seed);
}

/*
 * dto_async_crc32c_val --
 *	The device's CRC value as-is: the seeded presentation.
 */
__attribute__((visibility("default")))
uint32_t dto_async_crc32c_val(const dto_async_op *op)
{
	const struct dto_async_op_impl *impl =
		(const struct dto_async_op_impl *)op;
	return (uint32_t)impl->comp.crc_val;
}

__attribute__((visibility("default")))
int dto_submit_memcpy(dto_async_op *op, void *dest, const void *src,
		      size_t n, int cache_control)
{
	return dto_submit_async_common_seeded(op, DSA_OPCODE_MEMMOVE, dest, src, n,
				       cache_control, DSA_CRC_SEED_FOR_RAW);
}

__attribute__((visibility("default")))
int dto_submit_crc(dto_async_op *op, const void *src, size_t n)
{
	return dto_submit_async_common_seeded(op, DSA_OPCODE_CRCGEN, NULL, src, n, 0,
		DSA_CRC_SEED_FOR_RAW);
}

__attribute__((visibility("default")))
int dto_async_poll(dto_async_op *op)
{
	struct dto_async_op_impl *impl = (struct dto_async_op_impl *)op;
	uint8_t status = __atomic_load_n((uint8_t *)&impl->comp.status,
					 __ATOMIC_ACQUIRE);

	if (status == 0)
		return DTO_ASYNC_PENDING;
	if (likely(status == DSA_COMP_SUCCESS))
		return DTO_ASYNC_DONE;
	LOG_ERROR("async crc op failed status %x xfersz %x\n", status,
		  impl->desc.xfer_size);
	return DTO_ASYNC_FAILED;
}

/*
 * dto_async_wait --
 *	Block until the op's completion record is written, honouring
 *	DTO_WAIT_METHOD. Reports nothing: exactly one place in the library
 *	classifies a status byte, and that is dto_async_poll, which the caller
 *	still calls afterwards and which returns precisely what the last
 *	iteration of a poll loop would have.
 */
__attribute__((visibility("default")))
void dto_async_wait(dto_async_op *op)
{
	struct dto_async_op_impl *impl = (struct dto_async_op_impl *)op;

	atomic_fetch_add_explicit(&dto_wait_calls_async, 1,
	    memory_order_relaxed);
	if (impl->submitted != DTO_OP_SUBMITTED_MAGIC)
		return;
	if (__atomic_load_n((uint8_t *)&impl->comp.status, __ATOMIC_ACQUIRE))
		return;
	/* desc.xfer_size is the identical quantity dsa_wait hands the gate for
	 * an interposed copy: the source length for CRCGEN and the copy length
	 * for MEMMOVE/COPY_CRC. */
	if (atomic_exchange_explicit(&dto_wait_logged_async, 1u,
	    memory_order_relaxed) == 0)
		LOG_ERROR("public wait API: first dto_async_wait, %u bytes (wait method %s)\n",
		    impl->desc.xfer_size, wait_names[wait_method]);
	atomic_fetch_add_explicit(&dto_wait_entered_async, 1,
	    memory_order_relaxed);
	dto_wait_caller_owned((const volatile uint8_t *)&impl->comp.status,
	    impl->desc.xfer_size);
}

__attribute__((visibility("default")))
uint64_t dto_async_crc_val(const dto_async_op *op)
{
	const struct dto_async_op_impl *impl =
		(const struct dto_async_op_impl *)op;
	return DSA_CRC_VAL_TO_RAW(impl->comp.crc_val);
}

__attribute__((visibility("default"))) void dto_memcpy_async(void *dest, const void *src, size_t n, callback_t cb, void* args) {
	//submit dsa work if successful, call the callback
	if (thr_dsa_disabled || use_std_lib_calls) {
		if (cb) cb(args);
		orig_memcpy(dest, src, n);
		return;
	}
	int result = 0;
	struct dto_wq *wq = get_wq(dest, n);
	if (unlikely(wq == NULL)) {
		if (cb) cb(args);
		orig_memcpy(dest, src, n);
		return;
	}
	size_t dsa_size = n;
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
	DTO_COLLECT_STATS_START(collect_stats, st);
#endif

	thr_desc.opcode = DSA_OPCODE_MEMMOVE;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_BOF;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
	thr_desc.completion_addr = (uint64_t)&thr_comp;

	thr_bytes_completed = 0;

	thr_desc.src_addr = (uint64_t) src;
	thr_desc.dst_addr = (uint64_t) dest;
	thr_desc.xfer_size = (uint32_t) dsa_size;
	thr_comp.status = 0;
	result = dsa_submit(wq, &thr_desc);
	if (result == SUCCESS) {
		cb(args);
		result = dsa_wait(wq, &thr_desc, &thr_comp.status);
	}
#ifdef DTO_STATS_SUPPORT
	DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCOPY_ASYNC, n, thr_bytes_completed, result);
#endif
	if (thr_bytes_completed != n) {
		/* fallback to std call if job is only partially completed */
		n -= thr_bytes_completed;
		if (thr_comp.result == 0) {
			dest = (void *)((uint64_t)dest + thr_bytes_completed);
			src = (const void *)((uint64_t)src + thr_bytes_completed);
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memcpy(dest, src, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMCOPY, n, orig_n);
#endif
	}
}

static void dto_memcpymove(void *dest, const void *src, size_t n, bool is_memcpy, int *result)
{

	struct dto_wq *wq = get_wq(dest, n);
	size_t cpu_size, dsa_size;

	if (unlikely(wq == NULL)) {
		*result = -1;
		thr_bytes_completed = 0;
		return;
	}

	thr_desc.opcode = DSA_OPCODE_MEMMOVE;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY))
		thr_desc.flags |= IDXD_OP_FLAG_CC;
        if (dto_dsa_bof)
            thr_desc.flags |= IDXD_OP_FLAG_BOF;
	thr_desc.completion_addr = (uint64_t)&thr_comp;

        uint64_t cpu_frac = auto_adjust_knobs == AUTO_ADJUST_KNOBS_V2 ?
            tl_cpu_size_fraction : cpu_size_fraction;

	/* cpu_size_fraction guaranteed to be >= 0 and < 1 */
	if (!is_memcpy && is_overlapping_buffers(dest, src, n))
		cpu_size = 0;
	else
		cpu_size = n * cpu_frac / 100;

	dsa_size = n - cpu_size;

	thr_bytes_completed = 0;

	if (dsa_size <= wq->max_transfer_size) {
		thr_desc.src_addr = (uint64_t) src + cpu_size;
		thr_desc.dst_addr = (uint64_t) dest + cpu_size;
		thr_desc.xfer_size = (uint32_t) dsa_size;
		thr_comp.status = 0;
		*result = dsa_submit(wq, &thr_desc);
		if (*result == SUCCESS) {
			if (cpu_size) {
				if (is_memcpy)
					orig_memcpy(dest, src, cpu_size);
				else
					orig_memmove(dest, src, cpu_size);
				thr_bytes_completed += cpu_size;
			}
			*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
		}
	} else {
		uint32_t threshold;
		size_t current_cpu_size_fraction = cpu_frac;  // the cpu_size_fraction might be changed by the auto tune algorithm
		threshold = wq->max_transfer_size * 100 / (100 - current_cpu_size_fraction);
		do {
			size_t len;

			len = n <= threshold ? n : threshold;

			if (!is_memcpy && is_overlapping_buffers(dest, src, len))
				cpu_size = 0;
			else
				cpu_size = len * current_cpu_size_fraction / 100;

			dsa_size = len - cpu_size;

			thr_desc.src_addr = (uint64_t) src + cpu_size + thr_bytes_completed;
			thr_desc.dst_addr = (uint64_t) dest + cpu_size + thr_bytes_completed;
			thr_desc.xfer_size = (uint32_t) dsa_size;
			thr_comp.status = 0;
			*result = dsa_submit(wq, &thr_desc);
			if (*result == SUCCESS) {
				if (cpu_size) {
					const void *src1 = src + thr_bytes_completed;
					void *dest1 = dest + thr_bytes_completed;

					if (is_memcpy)
						orig_memcpy(dest1, src1, cpu_size);
					else
						orig_memmove(dest1, src1, cpu_size);
					thr_bytes_completed += cpu_size;
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

static int dto_memcmp(const void *s1, const void *s2, size_t n, int *result)
{
	struct dto_wq *wq = get_wq((void*)s2, n);
	int cmp_result = 0;
	size_t orig_n = n;

	if (unlikely(wq == NULL)) {
		*result = -1;
		thr_bytes_completed = 0;
		return 0;
	}

	thr_desc.opcode = DSA_OPCODE_COMPARE;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	thr_desc.completion_addr = (uint64_t)&thr_comp;
	thr_comp.result = 0;

	thr_bytes_completed = 0;

	size_t chunk_base = 0;

	if (n <= wq->max_transfer_size) {
		thr_desc.src_addr = (uint64_t) s1;
		thr_desc.src2_addr = (uint64_t) s2;
		thr_desc.xfer_size = (uint32_t) n;
		*result = dsa_execute(wq, &thr_desc, &thr_comp.status);
	} else {
		do {
			size_t len;

			len = n <= wq->max_transfer_size ? n : wq->max_transfer_size;

			chunk_base = thr_bytes_completed;
			thr_desc.src_addr = (uint64_t) s1 + chunk_base;
			thr_desc.src2_addr = (uint64_t) s2 + chunk_base;
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
		/* Mismatch: the completion record's bytes_completed is the
		 * count of equal bytes within this descriptor's chunk, i.e.
		 * the chunk-relative offset of the first differing byte.
		 * (dsa_wait accumulates the full xfer_size on success, so
		 * thr_bytes_completed cannot be used to locate the byte.)
		 */
		const uint8_t *t1 =
		    (const uint8_t *)s1 + chunk_base + thr_comp.bytes_completed;
		const uint8_t *t2 =
		    (const uint8_t *)s2 + chunk_base + thr_comp.bytes_completed;

		cmp_result = *t1 - *t2;
		/* Inform the caller than the job is done even though
		 * we didn't process all the bytes
		 */
		thr_bytes_completed = orig_n;
	}
	return cmp_result;
}


static inline void *fast_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0) return dst;

    // If no harmful overlap, we can copy forward.
    // Harmful overlap exists when dst starts inside [src, src+n).
    if (d < s || d >= s + n) {
        // -------- forward copy --------
        // Small sizes: straight byte copy often wins.
        if (n < 32) {
            while (n--) *d++ = *s++;
            return dst;
        }

        // Align destination to word boundary (helps word stores).
        const size_t W = sizeof(size_t);
        while (((uintptr_t)d & (W - 1)) && n) {
            *d++ = *s++;
            --n;
        }

        // Word copy (unaligned source is OK in portable C if we only
        // do word loads from aligned addresses; but s may be unaligned.
        // We therefore only do word copies when BOTH are aligned.
        if ((((uintptr_t)s & (W - 1)) == 0) && n >= W) {
            size_t *dw = (size_t *)d;
            const size_t *sw = (const size_t *)s;

            // Copy 4 words per iteration (unroll).
            while (n >= 4 * W) {
                dw[0] = sw[0];
                dw[1] = sw[1];
                dw[2] = sw[2];
                dw[3] = sw[3];
                dw += 4;
                sw += 4;
                n  -= 4 * W;
            }
            while (n >= W) {
                *dw++ = *sw++;
                n -= W;
            }

            d = (unsigned char *)dw;
            s = (const unsigned char *)sw;
        }

        // Tail bytes
        while (n--) *d++ = *s++;
        return dst;
    } else {
        // -------- backward copy (overlap) --------
        d += n;
        s += n;

        if (n < 32) {
            while (n--) *--d = *--s;
            return dst;
        }

        const size_t W = sizeof(size_t);

        // Align destination end to word boundary.
        while (((uintptr_t)d & (W - 1)) && n) {
            *--d = *--s;
            --n;
        }

        // Word copy backwards only when BOTH are aligned.
        if ((((uintptr_t)s & (W - 1)) == 0) && n >= W) {
            size_t *dw = (size_t *)d;
            const size_t *sw = (const size_t *)s;

            // Copy backwards in chunks.
            while (n >= 4 * W) {
                dw -= 4;
                sw -= 4;
                dw[3] = sw[3];
                dw[2] = sw[2];
                dw[1] = sw[1];
                dw[0] = sw[0];
                n -= 4 * W;
            }
            while (n >= W) {
                *--dw = *--sw;
                n -= W;
            }

            d = (unsigned char *)dw;
            s = (const unsigned char *)sw;
        }

        while (n--) *--d = *--s;
        return dst;
    }
}


/* The dto_internal_mem* APIs are used only when mem* APIs are
 * called before DTO is properly initialized. So these
 * implementations dont have to be performant
 */
static void *dto_internal_memset(void *s1, int c, size_t n)
{
	volatile char *dest = s1;
	size_t i;

	for (i = 0; i < n; i++)
		dest[i] = (char)c;

	return s1;
}

static void *dto_internal_memcpymove(void *dest, const void *src, size_t n)
{
	volatile char *d = dest;
	const volatile char *s = (const volatile char *)src;
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
	const volatile unsigned char *src1 = (const volatile unsigned char *)s1;
	const volatile unsigned char *src2 = (const volatile unsigned char *)s2;
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
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		dto_memset(s1, c, n, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMSET, n, thr_bytes_completed, result);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			s1 = (void *)((uint64_t)s1 + thr_bytes_completed);
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memset(s1, c, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMSET, n, orig_n);
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
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		dto_memcpymove(dest, src, n, 1, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCOPY, n, thr_bytes_completed, result);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			if (thr_comp.result == 0) {
				dest = (void *)((uint64_t)dest + thr_bytes_completed);
				src = (const void *)((uint64_t)src + thr_bytes_completed);
			}
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memcpy(dest, src, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMCOPY, n, orig_n);
#endif
	}
	return ret;
}

void *memmove(void *dest, const void *src, size_t n)
{
	int result = 0;
	void *ret = dest;
	int use_orig_func = USE_ORIG_FUNC(n, dto_dsa_memmove);
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t orig_n = n;
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
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		dto_memcpymove(dest, src, n, 0, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMMOVE, n, thr_bytes_completed, result);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			n -= thr_bytes_completed;
			if (thr_comp.result == 0) {
				dest = (void *)((uint64_t)dest + thr_bytes_completed);
				src = (const void *)((uint64_t)src + thr_bytes_completed);
			}
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_START(collect_stats, st);
#endif
			fast_memmove(dest, src, n);
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMMOVE_INTERNAL, n, n);
#endif
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		orig_memmove(dest, src, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMMOVE, n, orig_n);
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
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif
		ret = dto_memcmp(s1, s2, n, &result);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, MEMCMP, n, thr_bytes_completed, result);
#endif
		if (thr_bytes_completed != n) {
			/* fallback to std call if job is only partially completed */
			use_orig_func = 1;
			n -= thr_bytes_completed;
			s1 = (const void *)((uint64_t)s1 + thr_bytes_completed);
			s2 = (const void *)((uint64_t)s2 + thr_bytes_completed);
		}
	}

	if (use_orig_func) {
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_START(collect_stats, st);
#endif

		ret = orig_memcmp(s1, s2, n);

#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, MEMCMP, n, orig_n);
#endif
	}
	return ret;
}

/*
 * dto_batch_copy - Batch copy operation using DSA batch descriptor
 *
 * Performs multiple memory copy operations in a single DSA batch submission.
 * After submitting the batch to DSA, calls the callback function while DSA
 * is processing. Then waits for completion using the configured wait method.
 * Falls back to memcpy for any failed operations.
 */
__attribute__((visibility("default")))
void dto_batch_copy(void **dst, void **src, size_t *sizes, int count,
                    void (*callback)(void *), void *callback_arg)
{
#ifdef DTO_STATS_SUPPORT
	struct timespec st, et;
	size_t total_bytes = 0;
	for (int i = 0; i < count; i++) {
		total_bytes += sizes[i];
	}
	DTO_COLLECT_STATS_START(collect_stats, st);
#endif

	if (unlikely(dto_initialized == 0 || count <= 0 || thr_dsa_disabled)) {
		/* Not initialized, invalid count, or thread limit exceeded - use synchronous memcpy */
		for (int i = 0; i < count; i++) {
			if (dst[i] && src[i] && sizes[i] > 0) {
				orig_memcpy(dst[i], src[i], sizes[i]);
			}
		}
		if (callback) {
			callback(callback_arg);
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_CPU_END(collect_stats, st, et, BATCH_COPY, total_bytes, total_bytes);
#endif
		return;
	}

	/* Clamp count to maximum batch size */
	if (count > MAX_BATCH_DESCS) {
		count = MAX_BATCH_DESCS;
	}

	struct dto_wq *wq = get_wq_no_agg(dst[0], sizes[0]);
	if (unlikely(wq == NULL)) {
		for (int i = 0; i < count; i++) {
			if (dst[i] && src[i] && sizes[i] > 0)
				orig_memcpy(dst[i], src[i], sizes[i]);
		}
		if (callback) callback(callback_arg);
		return;
	}
	int result;

        orig_memset(&thr_batch_descs[0], 0, sizeof(thr_batch_descs[0]) * count);
        orig_memset(&thr_desc, 0, sizeof(thr_desc));
	//memset(&thr_desc, 0, sizeof(thr_desc));
	/* Prepare individual copy descriptors */
	for (int i = 0; i < count; i++) {
		struct dsa_hw_desc *desc = &thr_batch_descs[i];
		//memset(desc, 0, sizeof(*desc));

		desc->opcode = DSA_OPCODE_MEMMOVE;
		desc->flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
		/* as for the single descriptor paths: block-on-fault only when the
		 * work queue allows it, otherwise the descriptor is rejected */
		if (dto_dsa_bof)
			desc->flags |= IDXD_OP_FLAG_BOF;
		if (dto_dsa_cc && (wq->dsa_gencap & GENCAP_CC_MEMORY)) {
			desc->flags |= IDXD_OP_FLAG_CC;
		}
		desc->src_addr = (uint64_t)src[i];
		desc->dst_addr = (uint64_t)dst[i];
		desc->xfer_size = (uint32_t)sizes[i];
		desc->completion_addr = (uint64_t)&thr_batch_sub_comps[i];
		thr_batch_sub_comps[i].status = 0;
	}

	/* Prepare batch descriptor */
	thr_desc.opcode = DSA_OPCODE_BATCH;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	thr_desc.desc_list_addr = (uint64_t)thr_batch_descs;
	thr_desc.desc_count = count;
	thr_desc.completion_addr = (uint64_t)&thr_batch_comp;
	thr_batch_comp.status = 0;

	/* Submit batch descriptor */
	result = dsa_submit(wq, &thr_desc);

	if (result == SUCCESS) {
		/* DSA job submitted - call callback while DSA is working */
		if (callback) {
			callback(callback_arg);
		}

		/* Wait for batch completion using configured wait method */
		dsa_wait_no_adjust(&thr_batch_comp.status);

		/* Check for batch-level failures and fallback if needed */
		if (thr_batch_comp.status != DSA_COMP_SUCCESS) {
			{
				static int logged;
				if (logged < 3) {
					logged++;
					int first = -1;
					for (int i = 0; i < count; i++)
						if (thr_batch_sub_comps[i].status != DSA_COMP_SUCCESS) { first = i; break; }
					LOG_ERROR("Batch copy failed with status %x (first failed desc %d status %x size %zu), falling back to memcpy\n",
					          thr_batch_comp.status, first,
					          first >= 0 ? thr_batch_sub_comps[first].status : 0,
					          first >= 0 ? sizes[first] : 0);
				}
			}
			/* Check individual completions and retry failed ones */
			for (int i = 0; i < count; i++) {
				if (thr_batch_sub_comps[i].status != DSA_COMP_SUCCESS) {
					if (dst[i] && src[i] && sizes[i] > 0) {
						orig_memcpy(dst[i], src[i], sizes[i]);
					}
				}
			}
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, BATCH_COPY, total_bytes, total_bytes, FAIL_OTHERS);
#endif
		} else {
#ifdef DTO_STATS_SUPPORT
			DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, BATCH_COPY, total_bytes, total_bytes, SUCCESS);
#endif
		}
	} else {
		/* Batch submission failed - fall back to synchronous memcpy */
		LOG_ERROR("Batch submit failed, falling back to memcpy\n");
		for (int i = 0; i < count; i++) {
			if (dst[i] && src[i] && sizes[i] > 0) {
				orig_memcpy(dst[i], src[i], sizes[i]);
			}
		}
		/* Still call callback after fallback copies complete */
		if (callback) {
			callback(callback_arg);
		}
#ifdef DTO_STATS_SUPPORT
		DTO_COLLECT_STATS_DSA_END(collect_stats, st, et, BATCH_COPY, total_bytes, total_bytes, RETRY);
#endif
	}
}

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

#define likely(x)       __builtin_expect((x), 1)
#define unlikely(x)     __builtin_expect((x), 0)

// DSA capabilities
#define GENCAP_CC_MEMORY  0x4

#define UMWAIT_DELAY_DEFAULT 100000
/* C0.1 state */
#define UMWAIT_STATE 1

#define DEFAULT_SLEEP_TIME_USEC 8  //20 //sleep mode wait

#define USE_ORIG_FUNC(n, use_dsa) (use_std_lib_calls == 1 || !use_dsa || n < dsa_min_size)
#define TS_NS(s, e) (((e.tv_sec*1000000000) + e.tv_nsec) - ((s.tv_sec*1000000000) + s.tv_nsec))

/* Maximum WQs that DTO will use. It is rather an arbitrary limit
 * to keep things simple and avoid having to dynamically allocate memory.
 * Allocating memory dynamically may create cyclic dependency and may cause
 * a hang (e.g., memset --> malloc --> alloc library calls memset --> memset)
 */
#define MAX_WQS 32
#define MAX_NUMA_NODES 32
#define DTO_DEFAULT_MIN_SIZE 8192
#define DTO_INITIALIZED 0
#define DTO_INITIALIZING 1

// thread specific variables
static __thread struct dsa_hw_desc thr_desc;
static __thread struct dsa_completion_record thr_comp __attribute__((aligned(32)));
static __thread uint64_t thr_bytes_completed;

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


// global workqueue variables
static struct dto_wq wqs[MAX_WQS];
static struct dto_device* devices[MAX_NUMA_NODES];
static uint8_t num_wqs;
static atomic_uchar next_wq;
static atomic_uchar dto_initialized;
static atomic_uchar dto_initializing;
static uint8_t use_std_lib_calls;
static size_t dsa_min_size = DTO_DEFAULT_MIN_SIZE;
static uint8_t fork_handler_registered;


enum return_code {
	SUCCESS = 0x0,
	RETRY,
	PAGE_FAULT,
	FAIL_OTHERS,
	MAX_FAILURES,
};

/* call initialize/cleanup functions when library is loaded/unloaded */
static int init_dto(void) __attribute__((constructor));
static void cleanup_dto(void) __attribute__((destructor));

static void * (*orig_memcpy)(void *dest, const void *src, size_t n);

static __always_inline unsigned char enqcmd(struct dsa_hw_desc *desc, volatile void *reg)
{
	unsigned char retry;

	asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
			"setz %0\t\n"
			: "=r"(retry) : "a" (reg), "d" (desc));
	return retry;
}

/* Reinitialize DTO in the child process. */
static void child (void)
{
	dto_initializing = 0;
	dto_initialized = 0;

	init_dto();
}

static __always_inline int dsa_submit(struct dto_wq *wq,
	struct dsa_hw_desc *hw)
{
	int ret;
	//LOG_TRACE("desc flags: 0x%x, opcode: 0x%x\n", hw->flags, hw->opcode);
	__builtin_ia32_sfence();

	if (wq->wq_mmapped) {
		ret = enqcmd(hw, wq->wq_portal);
		if (!ret)
			return SUCCESS;
	} else {
		ret = write(wq->wq_fd, hw, sizeof(*hw));
		if (ret == sizeof(*hw))
			return SUCCESS;
		else
			return FAIL_OTHERS;
	}
	return RETRY;
}


static __always_inline int dsa_wait(struct dto_wq *wq,
	struct dsa_hw_desc *hw, volatile uint8_t *comp)
{
    //wait by yielding
	//while (*comp == 0) {
	//	sched_yield();
	//}

    // busypoll
    while (*comp == 0) {
		_mm_pause();
	}

	if (likely(*comp == DSA_COMP_SUCCESS)) {
		thr_bytes_completed += hw->xfer_size;
		return SUCCESS;
	} else if ((*comp & DSA_COMP_STATUS_MASK) == DSA_COMP_PAGE_FAULT_NOBOF) {
		thr_bytes_completed += thr_comp.bytes_completed;
		return PAGE_FAULT;
	}
	
	return FAIL_OTHERS;
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
			goto fail_wq;
		}

		// open DSA WQ
		wqs[i].wq_fd = open(wqs[i].wq_path, O_RDWR);
		if (wqs[i].wq_fd < 0) {
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
				goto fail_wq;
			}
		} else {
			wqs[i].wq_mmapped = true;
			close(wqs[i].wq_fd);
		}
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

static int init_dto(void)
{
	uint8_t init_notcomplete = 0;

	if (atomic_compare_exchange_strong(&dto_initializing, &init_notcomplete, 1)) {
		char *env_str;

		// save std c lib function pointers
		//orig_memset = dlsym(RTLD_NEXT, "memset");
		orig_memcpy = dlsym(RTLD_NEXT, "memcpy");
		//orig_memmove = dlsym(RTLD_NEXT, "memmove");
		//orig_memcmp = dlsym(RTLD_NEXT, "memcmp");

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
				
				use_std_lib_calls = 1;
			}
		}

		// initialize DSA
		if (!use_std_lib_calls) {

			if (dsa_init_from_accfg()) {
				use_std_lib_calls = 1;
			}
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

	cleanup_devices();
}


static __always_inline  struct dto_wq *get_wq(void* buf)
{
	struct dto_wq* wq = NULL;

	if (wq == NULL) {
		wq = &wqs[next_wq++ % num_wqs];
	}

	return wq;
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


static void dto_memcpymove(void *dest, const void *src, size_t dsa_size, bool is_memcpy, int *result)
{
	struct dto_wq *wq = get_wq(dest);

	thr_desc.opcode = DSA_OPCODE_MEMMOVE;
	thr_desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	thr_desc.flags |= IDXD_OP_FLAG_CC;
	thr_desc.completion_addr = (uint64_t)&thr_comp;

	thr_bytes_completed = 0;

	if (dsa_size <= wq->max_transfer_size) {
		thr_desc.src_addr = (uint64_t) src;
		thr_desc.dst_addr = (uint64_t) dest;
		thr_desc.xfer_size = (uint32_t) dsa_size;
		thr_comp.status = 0;
		*result = dsa_submit(wq, &thr_desc);
		if (*result == SUCCESS) {
			
			*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
		}
	} else {
		
		do {
			size_t len;

			len = dsa_size <= wq->max_transfer_size ? dsa_size : wq->max_transfer_size;

			thr_desc.src_addr = (uint64_t) src + thr_bytes_completed;
			thr_desc.dst_addr = (uint64_t) dest + thr_bytes_completed;
			thr_desc.xfer_size = (uint32_t) len;
			thr_comp.status = 0;
			*result = dsa_submit(wq, &thr_desc);
			if (*result == SUCCESS) {
				
				*result = dsa_wait(wq, &thr_desc, &thr_comp.status);
			}

			if (*result != SUCCESS)
				break;
			dsa_size -= len;
			/* If remaining bytes are less than dsa_min_size,
			 * dont submit to DSA. Instead, complete remaining
			 * bytes on CPU
			 */
		} while (dsa_size >= dsa_min_size);
	}
}


void *memcpy(void *dest, const void *src, size_t n)
{
	int result = 0;
	void *ret = dest;
	int use_orig_func = USE_ORIG_FUNC(n, 1);

	if (unlikely(dto_initialized == 0)) {
		/* If there are other constructors in the same binary,
		 * they may run before DTO's constructor. Just use
		 * internal CPU-based implementation if DTO is not
		 * initialized yet.
		 */
		return dto_internal_memcpymove(dest, src, n);
	}

	if (!use_orig_func) {
		dto_memcpymove(dest, src, n, 1, &result);

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
		orig_memcpy(dest, src, n);
	}
	return ret;
}


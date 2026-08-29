
#ifndef DTO_H
#define DTO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*callback_t)(void*);

void dto_memcpy_async(void *dest, const void *src, size_t n, callback_t cb, void* args);
uint64_t dto_memcpy_crc_async(void *dest, const void *src, size_t n, callback_t cb, void* args);
uint64_t dto_crc(const void *src, size_t n, callback_t cb, void* args);

/* ---- True-async CRC / Copy+CRC API ----
 *
 * Unlike dto_memcpy_crc_async/dto_crc (which submit, run the callback once,
 * then BLOCK until completion), these return immediately after enqueueing
 * the descriptor and the caller polls for completion. The operation state is
 * caller-allocated so it can outlive the submitting call and be polled from
 * a different thread; nothing is stored in thread-local state.
 *
 * Lifecycle:
 *   dto_async_op op;
 *   if (dto_submit_memcpy_crc(&op, dst, src, n, 1) == DTO_ASYNC_SUBMITTED) {
 *       ... other CPU work ...
 *       while (dto_async_poll(&op) == DTO_ASYNC_PENDING) { pause/yield; }
 *       if (dto_async_poll(&op) == DTO_ASYNC_DONE)
 *           crc = dto_async_crc_val(&op);
 *       else { memcpy(dst, src, n); crc = <software crc32c>; }
 *   } else {  // DTO_ASYNC_FALLBACK: nothing was submitted or copied
 *       memcpy(dst, src, n); crc = <software crc32c>;
 *   }
 *
 * Submission falls back (DTO_ASYNC_FALLBACK) when DSA is unavailable, the
 * size is below the DTO_CRC_MIN_BYTES/DTO_MIN_BYTES gate, or enqueue fails.
 * On DTO_ASYNC_FAILED the destination contents are unspecified; redo the
 * whole operation on the CPU. CRC values match crc32c_hw (raw CRC32C,
 * seed 0), same as the synchronous API.
 *
 * @cache_control: nonzero directs the copy output toward the CPU cache
 * (IDXD_OP_FLAG_CC) when the device supports it, for destinations that will
 * be read again soon. Only meaningful for dto_submit_memcpy_crc; CRC
 * generation has no destination (CC would be rejected by the device).
 */
typedef struct dto_async_op {
	unsigned char opaque[192] __attribute__((aligned(64)));
} dto_async_op;

#define DTO_ASYNC_SUBMITTED 0
#define DTO_ASYNC_FALLBACK (-1)

#define DTO_ASYNC_PENDING 0
#define DTO_ASYNC_DONE 1
#define DTO_ASYNC_FAILED (-1)

int dto_submit_memcpy_crc(dto_async_op *op, void *dest, const void *src,
			  size_t n, int cache_control);
int dto_submit_crc(dto_async_op *op, const void *src, size_t n);
int dto_async_poll(dto_async_op *op);
uint64_t dto_async_crc_val(const dto_async_op *op);
void dto_memset_pages(void *start_addr, void *end_addr, size_t page_size);

/* Batch copy using DSA batch descriptor.
 * Copies count buffers from src[i] to dst[i] using sizes[i] bytes.
 * After submitting the DSA batch, calls callback(callback_arg) while DSA is working.
 * Then waits for completion using the configured wait method.
 * Falls back to memcpy for any failed operations.
 *
 * @dst: Array of destination pointers
 * @src: Array of source pointers
 * @sizes: Array of sizes for each copy operation
 * @count: Number of copy operations
 * @callback: Function to call after DSA submission (while DSA is working)
 * @callback_arg: Argument to pass to callback function
 */
void dto_batch_copy(void **dst, void **src, size_t *sizes, int count,
                    void (*callback)(void *), void *callback_arg);

/* Asynchronous batch copy: the caller owns the op (dto_batch_op_new /
 * dto_batch_op_free), submits up to DTO_BATCH_MAX copies as one DSA batch
 * descriptor, and polls for completion. dto_submit_batch_copy returns
 * DTO_ASYNC_SUBMITTED, or DTO_ASYNC_FALLBACK when nothing was submitted (and
 * nothing copied: the caller copies on the CPU). dto_batch_poll returns
 * DTO_ASYNC_PENDING or DTO_ASYNC_DONE; copies the accelerator failed are
 * redone on the CPU before DONE is returned. */
#define DTO_BATCH_MAX 64
typedef struct dto_batch_op dto_batch_op;
dto_batch_op *dto_batch_op_new(void);
void dto_batch_op_free(dto_batch_op *op);
int dto_submit_batch_copy(dto_batch_op *op, void **dst, void **src,
			  size_t *sizes, int count);
int dto_batch_poll(dto_batch_op *op);

#ifdef __cplusplus
}
#endif

#endif


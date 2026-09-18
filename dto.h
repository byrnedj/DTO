
#ifndef DTO_H
#define DTO_H

/* This header is installed and is the integration surface for consumers that
 * link or dlopen DTO, so it must compile on its own. It names size_t and the
 * fixed-width integer types below; without these it only ever built because
 * dto.c includes it after <stdint.h>. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*callback_t)(void*);

void dto_memcpy_async(void *dest, const void *src, size_t n, callback_t cb, void* args);
uint64_t dto_memcpy_crc_async(void *dest, const void *src, size_t n, callback_t cb, void* args);
uint64_t dto_crc(const void *src, size_t n, callback_t cb, void* args);

/* Seeded CRC32C in the iSCSI presentation (seed and result inverted):
 * dto_crc32c_with_seed(0, data, n) is standard CRC32C, and feeding one call's
 * result as the next call's seed chains across chunks exactly like the
 * equivalent software loop (the convention used by WiredTiger checksums).
 * Synchronous; always returns the correct CRC — the CPU computes it whenever
 * the size is below the DTO_CRC_MIN_BYTES gate, DSA is unavailable, or the
 * device could not complete the operation. */
uint32_t dto_crc32c_with_seed(uint32_t seed, const void *src, size_t n);

/* Advisory activity hint for ticket-aware admission
 * (DTO_DSA_ADMISSION=ticket): the calling thread entered (active=1) or left
 * (active=0) an application phase that produces offload-eligible work, e.g.
 * a database execution-control ticket. Nesting-safe (counted). No-op in
 * other admission modes; callable from apps not linked against DTO via
 * dlsym(RTLD_DEFAULT, "dto_thread_active"). */
void dto_thread_active(int active);

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

int dto_submit_memcpy(dto_async_op *op, void *dest, const void *src,
		      size_t n, int cache_control);
int dto_submit_memcpy_crc(dto_async_op *op, void *dest, const void *src,
			  size_t n, int cache_control);
int dto_submit_crc(dto_async_op *op, const void *src, size_t n);
int dto_async_poll(dto_async_op *op);

/* Block until the device has written the op's completion record, honouring
 * DTO_WAIT_METHOD (under the aggregator: spin for transfers at or below
 * DTO_AGG_BLOCK_KB, otherwise give the core back through the scheduler).
 * Replaces an unbounded busy-spin poll loop:
 *
 *   dto_async_op op;
 *   if (dto_submit_memcpy_crc32c(&op, dst, src, n, seed, 1) ==
 *       DTO_ASYNC_SUBMITTED) {
 *       ... other CPU work ...
 *       dto_async_wait(&op);
 *       while (dto_async_poll(&op) == DTO_ASYNC_PENDING) { }
 *       if (dto_async_poll(&op) == DTO_ASYNC_DONE)
 *           crc = dto_async_crc32c_val(&op);
 *   }
 *
 * It WAITS ONLY, and returns void so that the caller's poll loop stays where
 * it is: the code above is correct when this symbol is absent (it degrades to
 * exactly the old loop), correct when it is present (the loop body runs zero
 * times), and cannot be restructured into a form that skips the poll. Nothing
 * is consumed and no status is cleared, so the following dto_async_poll /
 * dto_async_crc32c_val return exactly what the loop's last iteration would
 * have.
 *
 * Returns only once the status byte is non-zero -- DONE, FAILED or a
 * page-fault status -- so the device is provably finished with the caller's
 * buffers. Idempotent, callable any number of times including after DONE, and
 * a safe no-op for an op that was never submitted, one whose submit returned
 * DTO_ASYNC_FALLBACK, or one already complete. Any thread may wait, including
 * a thread that did not submit, and several may wait on one op concurrently.
 *
 * Submit early and wait late: the wait may spin or yield, and either way it
 * stops overlapping CPU work with device time.
 *
 * There is deliberately NO timeout variant. On a timeout the device still
 * owns the destination buffer, so the caller may not free it, reuse it, redo
 * the copy on the CPU (the device may land its write afterwards) or return
 * the op storage; the only legal action is to wait again. A timeout API can
 * therefore only be misused, and its likeliest misuse is silent destination
 * corruption. dto_async_poll IS the try-wait. */
void dto_async_wait(dto_async_op *op);

/* Fused copy + CRC32C in the seeded (iSCSI) presentation: the CRC matches
 * dto_crc32c_with_seed(seed, src, n), i.e. WiredTiger's checksum convention,
 * and the copy lands in dest. Read the value with dto_async_crc32c_val. */
int dto_submit_memcpy_crc32c(dto_async_op *op, void *dest, const void *src,
			     size_t n, uint32_t seed, int cache_control);
uint32_t dto_async_crc32c_val(const dto_async_op *op);
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

/* Block until the batch descriptor's completion record is written, honouring
 * DTO_WAIT_METHOD; the gate is applied to the SUM of the member sizes, which
 * is the batch's real device time. Same contract as dto_async_wait: void,
 * idempotent, waitable from any thread, no timeout variant, and a no-op for a
 * batch that was never submitted or whose submit returned DTO_ASYNC_FALLBACK.
 *
 * It WAITS ONLY. dto_batch_poll must still be called afterwards, or the CPU
 * repair of copies the accelerator failed never happens:
 *
 *   if (dto_submit_batch_copy(op, dst, src, sizes, n) == DTO_ASYNC_SUBMITTED) {
 *       ... other CPU work ...
 *       dto_batch_wait(op);
 *       while (dto_batch_poll(op) == DTO_ASYNC_PENDING) { }
 *   }
 */
void dto_batch_wait(dto_batch_op *op);

#ifdef __cplusplus
}
#endif

#endif


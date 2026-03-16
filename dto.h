
#ifndef DTO_H
#define DTO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*callback_t)(void*);

void dto_memcpy_async(void *dest, const void *src, size_t n, callback_t cb, void* args);
uint64_t dto_memcpy_crc_async(void *dest, const void *src, size_t n, callback_t cb, void* args);
uint64_t dto_crc(const void *src, size_t n, callback_t cb, void* args);
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

/* Gather copy using DSA scatter-gather descriptor.
 * Gathers data from multiple non-contiguous source buffers into a single
 * contiguous destination buffer using DSA's gather copy operation.
 * After submitting the DSA descriptor, calls callback(callback_arg) while
 * DSA is working. Then waits for completion using the configured wait method.
 * Falls back to memcpy for failed operations.
 *
 * @dst: Destination buffer (must be at least num_srcs * src_size bytes)
 * @srcs: Array of source buffer pointers
 * @num_srcs: Number of source buffers (max 64)
 * @src_size: Size of each source buffer in bytes
 * @callback: Function to call after DSA submission (while DSA is working)
 * @callback_arg: Argument to pass to callback function
 */
void dto_gather_copy(void *dst, void **srcs, int num_srcs, size_t src_size,
                     void (*callback)(void *), void *callback_arg);

#ifdef __cplusplus
}
#endif

#endif


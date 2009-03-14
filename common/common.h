#ifndef CNT_COMMON_H_
#define CNT_COMMON_H_ 1

#include <stdlib.h>

struct cnt_image;

/**
 * Updates `buffer' and `size' with all data currently available.
 *
 * Returns 1 if transfer is complete, 0 otherwise.
 *
 * If transfer is not complete, the caller shall free the returned buffer.
 * This is because a background thread might reallocated the primary buffer, so
 * a copy has to be made for the caller.
 *
 * If the transfer is complete, the caller shall not free the returned buffer.
 * This is to avoid needless copying.
 */
typedef int (*cnt_data_callback)(void** buffer, size_t* size, void* ctx);

/**
 * Loads an entire file into a buffer, and returns a handle usable by
 * cnt_file_callback.
 *
 * Call free to destroy this handle.
 */
void* cnt_file_callback_init(const char* path);

/**
 * Returns the contents of a buffer loaded with cnt_file_callback_init.
 *
 * Always returns 1 (transfer complete).
 */
int cnt_file_callback(void** buffer, size_t* size, void* ctx);

/**
 * Allocates a cnt_image.
 *
 * You need to use this instead of malloc() to preserve binary compatibility.
 */
struct cnt_image* cnt_image_alloc();

/**
 * Frees a cnt_image and all associated data.
 */
void cnt_image_free(struct cnt_image** img);

/**
 * Set data source for image.
 *
 * You need to call this before cnt_image_load.
 */
void cnt_image_set_data_callback(struct cnt_image* image, cnt_data_callback data_callback, void* ctx);

/**
 * Load an image, progressively.
 *
 * This function either return None or the same Picture handle on each call.
 * If called multiple times, it will progressively update the Picture until all
 * data has arrived.
 */
unsigned long cnt_image_load(size_t* width, size_t* height, struct cnt_image* image);

#endif /* !CNT_COMMON_H_ */

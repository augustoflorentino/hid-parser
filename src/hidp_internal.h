#ifndef HIDP_INTERNAL_H
#define HIDP_INTERNAL_H

#include "hid-parser/hid-parser.h"

#if defined(HIDP_IMPLEMENTATION) && defined(__GNUC__)
#define HIDP_INTERN static __attribute__((unused))
#elif defined(HIDP_IMPLEMENTATION)
#define HIDP_INTERN static
#else
#define HIDP_INTERN
#endif

#define HIDP_MAX_USAGES 12288u
#define HIDP_MAX_REPORT_SIZE 256u
#define HIDP_MAX_REPORT_BYTES 16384u
#define HIDP_MAX_REPORT_ID 255u
#define HIDP_GLOBAL_STACK 4u
#define HIDP_TOLERANT_GLOBAL_STACK 64u
#define HIDP_TOLERANT_LIMIT 0xffffu

typedef struct hidp_vec {
    void *data;
    size_t len;
    size_t cap;
} hidp_vec_t;

struct hidp_desc {
    hidp_alloc_fn alloc_fn;
    hidp_free_fn free_fn;
    void *alloc_ctx;
    hidp_vec_t fields;
    hidp_vec_t ranges;
    hidp_vec_t collections;
    hidp_vec_t reports;
    hidp_vec_t warnings;
};

HIDP_INTERN int hidp_vec_push(hidp_desc_t *desc, hidp_vec_t *vec, size_t elem_size, const void *elem);
HIDP_INTERN void hidp_vec_free(hidp_desc_t *desc, hidp_vec_t *vec);
HIDP_INTERN size_t hidp_report_bytes(size_t bit_len, unsigned id);
HIDP_INTERN int hidp_next_item(const unsigned char *desc, size_t desc_len, size_t offset,
                               hidp_item_t *item);

#endif

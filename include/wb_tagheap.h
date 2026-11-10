/*
 * wb_tagheap.h -- tag-partitioned linear heap.
 *
 * Naughty Dog's tagged-lifetime heap: allocations carry a tag, the heap is a
 * set of large blocks bumped per tag, and you free a TAG rather than a pointer
 * -- no free(ptr), no per-object headers, no fragmentation inside a tag. The
 * worst case is one partially-filled block per tag instead of a full 2 MiB.
 *
 * The block registry is HIVE memory (wb_hive): every block is a stable hive
 * record, so the registry never reallocates and iteration over it is
 * cache-friendly. A block is re-used when a tag is reset, not re-malloc'd.
 *
 * C11, no deps beyond the hive.
 */
#ifndef WB_TAGHEAP_H
#define WB_TAGHEAP_H

#include <stddef.h>
#include <stdint.h>

#include "wb_hive.h"

#define WB_TAGHEAP_MAX_TAGS 32

typedef struct wb_tagheap_block {
    unsigned char *base;
    size_t         cap, used;
    uint32_t       tag;
    int            in_use;
} wb_tagheap_block;

typedef struct {
    uint32_t           tag;
    size_t             used, peak, budget;  /* budget 0 = unlimited */
    int                over;                /* an allocation exceeded the budget */
    wb_tagheap_block  *cur;                 /* the current bump block */
} wb_tagheap_tag;

typedef struct wb_tagheap {
    wb_hive        blocks;                  /* the block registry (hive memory) */
    wb_tagheap_tag tags[WB_TAGHEAP_MAX_TAGS];
    int            ntags;
    size_t         block_cap;
} wb_tagheap;

int  wb_tagheap_init(wb_tagheap *h, size_t block_cap);
void wb_tagheap_free(wb_tagheap *h);        /* returns every block to malloc */

/* Allocate `bytes` under `tag`. Returns NULL when the tag is over budget or
 * out of memory. `align` must be a power of two. */
void *wb_tagheap_alloc(wb_tagheap *h, uint32_t tag, size_t bytes, size_t align);

/* Free every block held by a tag (the Naughty Dog "free the tag" move). The
 * blocks return to the registry for reuse by the next allocation. */
void   wb_tagheap_reset_tag(wb_tagheap *h, uint32_t tag);

size_t wb_tagheap_used(const wb_tagheap *h, uint32_t tag);
size_t wb_tagheap_peak(const wb_tagheap *h, uint32_t tag);
void   wb_tagheap_set_budget(wb_tagheap *h, uint32_t tag, size_t budget);
int    wb_tagheap_over(const wb_tagheap *h, uint32_t tag);

/* ---- per-thread heaps ---------------------------------------------------- *
 * Each thread gets its own tag heap, so the common allocation path needs no
 * lock at all (Naughty Dog: per-thread blocks make 99.9% of allocations
 * lock-free). The heap is created lazily on first use on each thread. */
wb_tagheap *wb_tagheap_thread(void);
void *wb_tagheap_thread_alloc(uint32_t tag, size_t bytes, size_t align);
void  wb_tagheap_thread_reset(uint32_t tag);

#endif /* WB_TAGHEAP_H */

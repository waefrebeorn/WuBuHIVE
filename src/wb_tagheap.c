/*
 * wb_tagheap.c -- tag-partitioned linear heap. See wb_tagheap.h.
 */
#include "wb_tagheap.h"
#include "wb_tls.h"

#include <stdlib.h>
#include <string.h>

static int wb_hive_is_pow2(size_t x) { return x != 0 && (x & (x - 1)) == 0; }

static wb_tagheap_tag *wb_tagheap_find_tag(wb_tagheap *h, uint32_t tag, int create) {
    for (int i = 0; i < h->ntags; i++)
        if (h->tags[i].tag == tag) return &h->tags[i];
    if (!create || h->ntags >= WB_TAGHEAP_MAX_TAGS) return NULL;
    wb_tagheap_tag *t = &h->tags[h->ntags++];
    memset(t, 0, sizeof(*t));
    t->tag = tag;
    return t;
}

int wb_tagheap_init(wb_tagheap *h, size_t block_cap) {
    if (!h) return -1;
    memset(h, 0, sizeof(*h));
    h->block_cap = block_cap ? block_cap : (2u << 20);
    return wb_hive_init(&h->blocks, sizeof(wb_tagheap_block), 16);
}

void wb_tagheap_free(wb_tagheap *h) {
    if (!h) return;
    /* the registry holds every block; return each buffer to malloc */
    for (wb_hive_block *b = h->blocks.head; b; b = b->next)
        for (size_t s = 0; s < b->cap; s++)
            if (b->skip[s] == 0) {
                wb_tagheap_block *blk = (wb_tagheap_block *)(b->elems + s * h->blocks.elem_size);
                free(blk->base);
                blk->base = NULL;
            }
    wb_hive_clear(&h->blocks);
    h->ntags = 0;
}

static wb_tagheap_block *wb_tagheap_block_new(wb_tagheap *h, uint32_t tag) {
    /* reuse a free registry record if one exists (hive iteration) */
    for (wb_hive_block *b = h->blocks.head; b; b = b->next)
        for (size_t s = 0; s < b->cap; s++)
            if (b->skip[s] == 0) {
                wb_tagheap_block *blk = (wb_tagheap_block *)(b->elems + s * h->blocks.elem_size);
                if (!blk->in_use) {
                    if (blk->base && blk->cap != h->block_cap) { free(blk->base); blk->base = NULL; }
                    if (!blk->base) blk->base = (unsigned char *)malloc(h->block_cap);
                    if (!blk->base) return NULL;
                    blk->cap = h->block_cap; blk->used = 0; blk->tag = tag; blk->in_use = 1;
                    return blk;
                }
            }
    /* otherwise a new registry record (hive memory) */
    wb_tagheap_block *blk = (wb_tagheap_block *)wb_hive_emit(&h->blocks);
    if (!blk) return NULL;
    blk->base = (unsigned char *)malloc(h->block_cap);
    if (!blk->base) { wb_hive_drop(&h->blocks, blk); return NULL; }
    blk->cap = h->block_cap; blk->used = 0; blk->tag = tag; blk->in_use = 1;
    return blk;
}

void *wb_tagheap_alloc(wb_tagheap *h, uint32_t tag, size_t bytes, size_t align) {
    if (!h || bytes == 0) return NULL;
    if (!wb_hive_is_pow2(align)) align = sizeof(void *);

    wb_tagheap_tag *t = wb_tagheap_find_tag(h, tag, 1);
    if (!t) return NULL;
    if (t->budget && t->used + bytes > t->budget) { t->over = 1; return NULL; }

    wb_tagheap_block *blk = t->cur;
    if (!blk || !blk->in_use) {
        blk = wb_tagheap_block_new(h, tag);
        if (!blk) return NULL;
        t->cur = blk;
    }

    uintptr_t cur = (uintptr_t)(blk->base + blk->used);
    size_t pad = (size_t)((align - (cur & (align - 1))) & (align - 1));
    if (bytes > blk->cap || pad > blk->cap - blk->used || bytes > blk->cap - pad - blk->used) {
        /* the current block is full: release it to the registry and take another */
        blk->in_use = 0;
        t->cur = NULL;
        blk = wb_tagheap_block_new(h, tag);
        if (!blk) return NULL;
        t->cur = blk;
        cur = (uintptr_t)blk->base;
        pad = (size_t)((align - (cur & (align - 1))) & (align - 1));
        if (bytes > blk->cap - pad) return NULL;
    }

    blk->used += pad + bytes;
    t->used += bytes;
    if (t->used > t->peak) t->peak = t->used;
    return blk->base + blk->used - bytes;
}

static int wb_tagheap_reset_block(void *elem, void *user) {
    wb_tagheap_block *blk = (wb_tagheap_block *)elem;
    uint32_t tag = *(uint32_t *)user;
    if (blk->in_use && blk->tag == tag) { blk->in_use = 0; blk->used = 0; }
    return 0;
}

void wb_tagheap_reset_tag(wb_tagheap *h, uint32_t tag) {
    if (!h) return;
    wb_hive_foreach(&h->blocks, wb_tagheap_reset_block, &tag);
    wb_tagheap_tag *t = wb_tagheap_find_tag(h, tag, 0);
    if (t) { t->used = 0; t->cur = NULL; }
}

size_t wb_tagheap_used(const wb_tagheap *h, uint32_t tag) {
    if (!h) return 0;
    for (int i = 0; i < h->ntags; i++) if (h->tags[i].tag == tag) return h->tags[i].used;
    return 0;
}

size_t wb_tagheap_peak(const wb_tagheap *h, uint32_t tag) {
    if (!h) return 0;
    for (int i = 0; i < h->ntags; i++) if (h->tags[i].tag == tag) return h->tags[i].peak;
    return 0;
}

void wb_tagheap_set_budget(wb_tagheap *h, uint32_t tag, size_t budget) {
    wb_tagheap_tag *t = wb_tagheap_find_tag(h, tag, 1);
    if (t) t->budget = budget;
}

int wb_tagheap_over(const wb_tagheap *h, uint32_t tag) {
    if (!h) return 0;
    for (int i = 0; i < h->ntags; i++) if (h->tags[i].tag == tag) return h->tags[i].over;
    return 0;
}

/* ---- per-thread heaps ---------------------------------------------------- */

/* Each thread gets its own tag heap, so the common allocation path needs no
 * lock at all (Naughty Dog: per-thread blocks make 99.9% of allocations
 * lock-free). The slot itself is wb_tls's, so the platform answer lives in one
 * place. A per-thread heap is intentionally not freed at thread exit (Win32 TLS
 * and pthread keys both allow a destructor, but the heap outlives the thread by
 * design here); the process owns it. */
wb_tagheap *wb_tagheap_thread(void) {
    wb_tls *slot = wb_tls_slot();
    if (!slot) return NULL;
    wb_tagheap *h = (wb_tagheap *)wb_tls_get(slot);
    if (!h) {
        h = (wb_tagheap *)calloc(1, sizeof(*h));
        if (!h) return NULL;
        if (wb_tagheap_init(h, 2u << 20) != 0) { free(h); return NULL; }
        wb_tls_set(slot, h);
    }
    return h;
}

void *wb_tagheap_thread_alloc(uint32_t tag, size_t bytes, size_t align) {
    wb_tagheap *h = wb_tagheap_thread();
    return h ? wb_tagheap_alloc(h, tag, bytes, align) : NULL;
}

void wb_tagheap_thread_reset(uint32_t tag) {
    wb_tagheap *h = wb_tagheap_thread();
    if (h) wb_tagheap_reset_tag(h, tag);
}

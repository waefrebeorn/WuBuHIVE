/*
 * wb_hive.c -- THE HIVE. See wb_hive.h.
 */
#include "wb_hive.h"

#include <stdlib.h>
#include <string.h>

static wb_hive_block *wb_hive_block_new(wb_hive *h) {
    wb_hive_block *b = (wb_hive_block *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->elems = (unsigned char *)calloc(h->block_cap * h->elem_size, 1);
    b->skip  = (uint8_t *)calloc(h->block_cap, 1);
    if (!b->elems || !b->skip) { free(b->elems); free(b->skip); free(b); return NULL; }
    /* a fresh block has NO live slots: skip must be 1 (erased) everywhere,
     * otherwise foreach would visit never-written slots. */
    memset(b->skip, 1, h->block_cap);
    b->cap = h->block_cap;
    b->live = 0;
    b->next = NULL;
    return b;
}

static void wb_hive_block_free(wb_hive_block *b) {
    if (!b) return;
    free(b->elems); free(b->skip); free(b);
}

static void wb_hive_free_push(wb_hive *h, wb_hive_block *b, size_t s) {
    if (h->n_free == h->free_cap) {
        size_t nc = h->free_cap ? h->free_cap * 2 : 16;
        wb_hive_slot *nf = (wb_hive_slot *)realloc(h->free_entries, nc * sizeof(*nf));
        if (!nf) return;                 /* freelist full: the slot leaks (no reuse) */
        h->free_entries = nf;
        h->free_cap = nc;
    }
    h->free_entries[h->n_free].block = b;
    h->free_entries[h->n_free].slot = s;
    h->n_free++;
}

static int wb_hive_free_pop(wb_hive *h, wb_hive_block **b, size_t *s) {
    if (h->n_free == 0) return -1;
    h->n_free--;
    *b = h->free_entries[h->n_free].block;
    *s = h->free_entries[h->n_free].slot;
    return 0;
}

int wb_hive_init(wb_hive *h, size_t elem_size, size_t block_cap) {
    if (!h || elem_size == 0) return -1;
    memset(h, 0, sizeof(*h));
    h->elem_size = elem_size;
    h->block_cap = block_cap ? block_cap : 64;
    return 0;
}

void wb_hive_clear(wb_hive *h) {
    if (!h) return;
    wb_hive_block *b = h->head;
    while (b) { wb_hive_block *nx = b->next; wb_hive_block_free(b); b = nx; }
    free(h->free_entries);
    /* preserve the configuration (element size + block capacity): clear is a
     * content reset, not a de-initialisation. */
    size_t es = h->elem_size, bc = h->block_cap;
    memset(h, 0, sizeof(*h));
    h->elem_size = es;
    h->block_cap = bc;
}

void *wb_hive_emit(wb_hive *h) {
    if (!h) return NULL;
    wb_hive_block *b;
    size_t s;
    /* 1. reuse a freelist slot (LIFO) */
    if (wb_hive_free_pop(h, &b, &s) == 0) {
        b->skip[s] = 0; b->live++; h->total_live++; h->reuses++;
        h->hint = b;
        return b->elems + s * h->elem_size;
    }
    /* 2. append to the tail block. Freelist-less tail slots are contiguous at
     * [0, live), so the next free slot is exactly index `live`. */
    if (h->tail && h->tail->live < h->tail->cap) {
        b = h->tail; s = b->live;
        b->skip[s] = 0; b->live++; h->total_live++; h->allocs++;
        h->hint = b;
        return b->elems + s * h->elem_size;
    }
    /* 3. a new block */
    b = wb_hive_block_new(h);
    if (!b) return NULL;
    b->skip[0] = 0; b->live = 1;
    if (h->tail) h->tail->next = b; else h->head = b;
    h->tail = b;
    h->n_blocks++;
    h->total_live++;
    h->allocs++;
    h->hint = b;
    return b->elems;
}

void wb_hive_drop_at(wb_hive *h, wb_hive_block *b, size_t slot) {
    if (!h || !b || slot >= b->cap || b->skip[slot] != 0) return;
    b->skip[slot] = 1;
    b->elems[slot * h->elem_size] = 0;   /* clear the first byte; contents are dead */
    b->live--;
    h->total_live--;
    h->hint = b;                         /* drops often reverse the emits */
    wb_hive_free_push(h, b, slot);
}

void wb_hive_drop(wb_hive *h, void *elem) {
    if (!h || !elem) return;
    unsigned char *p = (unsigned char *)elem;
    /* the hint is the block of the most recent emit; drops usually reverse the
     * emits, so this is the common case and costs one range check */
    wb_hive_block *hint = h->hint;
    if (hint && p >= hint->elems && p < hint->elems + hint->cap * h->elem_size) {
        wb_hive_drop_at(h, hint, (size_t)(p - hint->elems) / h->elem_size);
        return;
    }
    for (wb_hive_block *b = h->head; b; b = b->next) {
        if (p < b->elems || p >= b->elems + b->cap * h->elem_size) continue;
        size_t s = (size_t)(p - b->elems) / h->elem_size;
        wb_hive_drop_at(h, b, s);
        return;
    }
}

size_t wb_hive_foreach(wb_hive *h, int (*fn)(void *elem, void *user), void *user) {
    if (!h || !fn) return 0;
    size_t visited = 0;
    for (wb_hive_block *b = h->head; b; b = b->next) {
        if (b->live == 0) continue;
        for (size_t s = 0; s < b->cap; s++) {
            if (b->skip[s] == 0) {
                visited++;
                if (fn(b->elems + s * h->elem_size, user)) return visited;
            }
        }
    }
    return visited;
}

size_t wb_hive_live(const wb_hive *h) { return h ? h->total_live : 0; }
size_t wb_hive_capacity(const wb_hive *h) { return h ? h->n_blocks * h->block_cap : 0; }
int    wb_hive_empty(const wb_hive *h) { return !h || h->total_live == 0; }

int wb_hive_validate(const wb_hive *h) {
    if (!h) return 0;
    size_t total = 0;
    for (const wb_hive_block *b = h->head; b; b = b->next) {
        size_t live = 0;
        for (size_t s = 0; s < b->cap; s++) if (b->skip[s] == 0) live++;
        if (live != b->live) return -1;
        total += live;
    }
    if (total != h->total_live) return -2;
    for (size_t i = 0; i < h->n_free; i++) {
        const wb_hive_block *b = h->free_entries[i].block;
        size_t s = h->free_entries[i].slot;
        if (!b || s >= b->cap || b->skip[s] != 1) return -3;
    }
    /* the append path writes at index `live`; that is only safe when every
     * slot below `live` is live, i.e. when no hole is outstanding. */
    if (h->n_free == 0) {
        for (const wb_hive_block *b = h->head; b; b = b->next) {
            for (size_t s = 0; s < b->live; s++)
                if (b->skip[s] != 0) return -4;
        }
    }
    return 0;
}

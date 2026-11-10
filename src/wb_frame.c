/*
 * wb_frame.c -- frame allocator. See wb_frame.h.
 */
#include "wb_frame.h"

#include <stdint.h>

static int wb_frame_is_pow2(size_t x) { return x != 0 && (x & (x - 1)) == 0; }

void wb_frame_init(wb_frame *f, void *buffer, size_t cap) {
    f->base = (unsigned char *)buffer;
    f->cap = buffer ? cap : 0;
    f->used = 0;
}

void *wb_frame_alloc(wb_frame *f, size_t bytes, size_t align) {
    if (!f || !f->base || bytes == 0) return NULL;
    if (!wb_frame_is_pow2(align)) align = sizeof(void *);

    uintptr_t cur = (uintptr_t)f->base + f->used;
    size_t pad = (size_t)((align - (cur & (align - 1))) & (align - 1));

    if (pad > f->cap - f->used) return NULL;        /* no room for padding */
    size_t off = f->used + pad;
    if (bytes > f->cap - off) return NULL;          /* no room for the block */
    f->used = off + bytes;
    return f->base + off;
}

void wb_frame_reset(wb_frame *f) { if (f) f->used = 0; }
size_t wb_frame_used(const wb_frame *f) { return f ? f->used : 0; }
size_t wb_frame_left(const wb_frame *f) { return f ? f->cap - f->used : 0; }

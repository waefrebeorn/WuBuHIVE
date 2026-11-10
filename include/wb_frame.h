/*
 * wb_frame.h -- frame allocator.
 *
 * A bump allocator over a fixed buffer that resets once per frame. Per-frame
 * temporaries (command buffers, visibility lists, scratch) are allocated here
 * and invalidated together at the frame boundary -- no per-object free, no
 * fragmentation, and the steady-state allocation rate is exactly zero.
 *
 * C11, no deps. Deterministic: identical request sequences reuse the same
 * addresses.
 */
#ifndef WB_FRAME_H
#define WB_FRAME_H

#include <stddef.h>

typedef struct {
    unsigned char *base;
    size_t         cap;
    size_t         used;
} wb_frame;

void   wb_frame_init(wb_frame *f, void *buffer, size_t cap);
void  *wb_frame_alloc(wb_frame *f, size_t bytes, size_t align);
void   wb_frame_reset(wb_frame *f);
size_t wb_frame_used(const wb_frame *f);
size_t wb_frame_left(const wb_frame *f);

#endif /* WB_FRAME_H */

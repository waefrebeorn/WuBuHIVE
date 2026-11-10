/*
 * wb_atomic.h -- memory-ordering primitives.
 *
 * Thin, explicit wrappers over C11 atomics so every call site states its
 * ordering instead of silently paying for seq_cst. The two orders that matter
 * in this engine:
 *   - release/acquire: publish sim state to a reader (a job hands off a result)
 *   - relaxed: counters and other order-independent aggregation
 *
 * Header-only; C11 <stdatomic.h>.
 */
#ifndef WB_ATOMIC_H
#define WB_ATOMIC_H

#include <stdatomic.h>

typedef atomic_int wb_atomic_int;

static inline void wb_atomic_store_release(wb_atomic_int *a, int v) {
    atomic_store_explicit(a, v, memory_order_release);
}
static inline int wb_atomic_load_acquire(wb_atomic_int *a) {
    return atomic_load_explicit(a, memory_order_acquire);
}
static inline int wb_atomic_load_relaxed(wb_atomic_int *a) {
    return atomic_load_explicit(a, memory_order_relaxed);
}
static inline int wb_atomic_fetch_add_relaxed(wb_atomic_int *a, int v) {
    return atomic_fetch_add_explicit(a, v, memory_order_relaxed);
}

#endif /* WB_ATOMIC_H */

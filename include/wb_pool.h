/*
 * wb_pool.h -- worker pool over hive memory.
 *
 * A fixed-size pool of worker threads with a mutex-protected FIFO queue. The
 * design keeps the determinism discipline: jobs are independent, and the
 * deterministic parallel-for (wb_pool_map) gives every chunk its own slot, so
 * the caller reduces partials in slot order and the result is independent of
 * scheduling. No job ever writes a shared accumulator.
 *
 * pthreads (winpthreads on MinGW). C11, no other deps.
 */
#ifndef WB_POOL_H
#define WB_POOL_H

typedef void (*wb_pool_fn)(void *user);

/* A map chunk: [start,end) of [0,n), writing its partial into slot `slot`. */
typedef void (*wb_pool_map_fn)(int start, int end, int slot, void *user);

typedef struct wb_pool wb_pool;

wb_pool *wb_pool_new(int nthreads);
void     wb_pool_free(wb_pool *p);

/* Queue a job; returns 0 on success, -1 if the queue is closed/out of memory. */
int  wb_pool_submit(wb_pool *p, wb_pool_fn fn, void *user);
/* Block until every submitted job has finished. */
void wb_pool_wait(wb_pool *p);
int  wb_pool_nthreads(const wb_pool *p);

/* Deterministic parallel-for: [0,n) split into nslots contiguous chunks; chunk
 * i runs on the pool and reports through slot i. The caller reduces the nslots
 * partials, so the total is order-independent. */
void wb_pool_map(wb_pool *p, wb_pool_map_fn fn, int n, int nslots, void *user);

#endif /* WB_POOL_H */

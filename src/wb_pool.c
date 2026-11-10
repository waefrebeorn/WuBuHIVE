/*
 * wb_pool.c -- worker pool. See wb_pool.h.
 *
 * The job queue is HIVE memory, not a malloc/FIFO chain: every job record is a
 * slot in a wb_hive (linked fixed blocks + skipfield + freelist), so a record's
 * address is stable while queued and the freelist recycles it on completion.
 * No malloc/free per job, no ring buffer, no compaction.
 *
 * The FIFO order is intrusive `next` pointers through the records -- the hive
 * is the memory, the links are the ordering.
 */
#include "wb_pool.h"
#include "wb_hive.h"

#include <pthread.h>
#include <stdlib.h>

/* Debug builds validate the hive after every mutation: a broken invariant is
 * reported at the exact operation instead of surfacing later as a crash. */
#ifdef WB_POOL_DEBUG
#include <stdio.h>
#define WB_POOL_CHECK_HIVE(p) do { \
    int rc_ = wb_hive_validate(&(p)->jobs); \
    if (rc_ != 0) { \
        fprintf(stderr, "HIVE CORRUPT (%d) at %s:%d\n", rc_, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)
#else
#define WB_POOL_CHECK_HIVE(p) ((void)0)
#endif

typedef struct wb_pool_job {
    wb_pool_fn     fn;      /* normal job */
    void          *user;
    wb_pool_map_fn map_fn;  /* set for a parallel-for chunk */
    int            start, end, slot;
    struct wb_pool_job *next;
} wb_pool_job;

struct wb_pool {
    pthread_t      *threads;
    int             nthreads;
    pthread_mutex_t m;
    pthread_cond_t  cv;
    wb_hive         jobs;        /* the job records (hive memory) */
    wb_pool_job    *head, *tail; /* FIFO through the records */
    int             pending;     /* submitted but not finished */
    int             shutdown;
};

static void *wb_pool_worker(void *arg) {
    wb_pool *p = (wb_pool *)arg;
    for (;;) {
        pthread_mutex_lock(&p->m);
        while (!p->shutdown && !p->head)
            pthread_cond_wait(&p->cv, &p->m);
        if (p->shutdown && !p->head) { pthread_mutex_unlock(&p->m); return NULL; }
        wb_pool_job *j = p->head;
        p->head = j->next;
        if (!p->head) p->tail = NULL;
        pthread_mutex_unlock(&p->m);

        if (j->map_fn) j->map_fn(j->start, j->end, j->slot, j->user);
        else if (j->fn) j->fn(j->user);

        /* the hive is single-threaded: recycle the record under the pool lock */
        pthread_mutex_lock(&p->m);
        wb_hive_drop(&p->jobs, j);
        WB_POOL_CHECK_HIVE(p);
        if (--p->pending == 0) pthread_cond_broadcast(&p->cv);
        pthread_mutex_unlock(&p->m);
    }
}

wb_pool *wb_pool_new(int nthreads) {
    if (nthreads < 1) nthreads = 1;
    wb_pool *p = (wb_pool *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->threads = (pthread_t *)calloc((size_t)nthreads, sizeof(pthread_t));
    if (!p->threads) { free(p); return NULL; }
    if (wb_hive_init(&p->jobs, sizeof(wb_pool_job), 64) != 0) {
        free(p->threads); free(p); return NULL;
    }
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->cv, NULL);
    p->nthreads = nthreads;
    for (int i = 0; i < nthreads; i++)
        if (pthread_create(&p->threads[i], NULL, wb_pool_worker, p) != 0) {
            /* roll back the threads we started */
            pthread_mutex_lock(&p->m);
            p->shutdown = 1;
            pthread_cond_broadcast(&p->cv);
            pthread_mutex_unlock(&p->m);
            for (int k = 0; k < i; k++) pthread_join(p->threads[k], NULL);
            wb_hive_clear(&p->jobs);
            free(p->threads); free(p);
            return NULL;
        }
    return p;
}

void wb_pool_free(wb_pool *p) {
    if (!p) return;
    wb_pool_wait(p);
    pthread_mutex_lock(&p->m);
    p->shutdown = 1;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->m);
    for (int i = 0; i < p->nthreads; i++) pthread_join(p->threads[i], NULL);
    pthread_mutex_destroy(&p->m);
    pthread_cond_destroy(&p->cv);
    wb_hive_clear(&p->jobs);
    free(p->threads);
    free(p);
}

int wb_pool_submit(wb_pool *p, wb_pool_fn fn, void *user) {
    if (!p || !fn) return -1;
    pthread_mutex_lock(&p->m);
    if (p->shutdown) { pthread_mutex_unlock(&p->m); return -1; }
    wb_pool_job *j = (wb_pool_job *)wb_hive_emit(&p->jobs);
    if (!j) { pthread_mutex_unlock(&p->m); return -1; }
    j->fn = fn; j->user = user; j->map_fn = NULL;
    j->start = j->end = j->slot = 0;
    j->next = NULL;
    if (p->tail) p->tail->next = j; else p->head = j;
    p->tail = j;
    p->pending++;
    WB_POOL_CHECK_HIVE(p);
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->m);
    return 0;
}

void wb_pool_wait(wb_pool *p) {
    if (!p) return;
    pthread_mutex_lock(&p->m);
    while (p->pending > 0) pthread_cond_wait(&p->cv, &p->m);
    pthread_mutex_unlock(&p->m);
}

int wb_pool_nthreads(const wb_pool *p) { return p ? p->nthreads : 0; }

/* ---- deterministic parallel-for ------------------------------------------ */

void wb_pool_map(wb_pool *p, wb_pool_map_fn fn, int n, int nslots, void *user) {
    if (!p || !fn || n <= 0 || nslots <= 0) return;
    if (nslots > n) nslots = n;
    for (int i = 0; i < nslots; i++) {
        int start = (int)((long)i * n / nslots);
        int end   = (int)((long)(i + 1) * n / nslots);
        pthread_mutex_lock(&p->m);
        if (p->shutdown) { pthread_mutex_unlock(&p->m); return; }
        wb_pool_job *j = (wb_pool_job *)wb_hive_emit(&p->jobs);
        if (!j) { pthread_mutex_unlock(&p->m); return; }
        j->fn = NULL; j->user = user; j->map_fn = fn;
        j->start = start; j->end = end; j->slot = i; j->next = NULL;
        if (p->tail) p->tail->next = j; else p->head = j;
        p->tail = j;
        p->pending++;
        pthread_cond_signal(&p->cv);
        pthread_mutex_unlock(&p->m);
    }
    wb_pool_wait(p);
}

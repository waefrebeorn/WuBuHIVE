/*
 * wb_threadheap_test.c per-thread tag heaps.
 *
 * Each thread gets its own heap, so the allocation path needs no lock. The gate
 * proves the heaps are per-thread (distinct instances, distinct addresses) and
 * that one thread's accounting is invisible to another.
 */
#include "wb_tagheap.h"
#include "wb_atomic.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define NTHREADS 4

static wb_tagheap *g_heaps[NTHREADS];
static void       *g_ptrs[NTHREADS];
static wb_atomic_int g_arrived;

static void *thread_fn(void *arg) {
    int i = (int)(size_t)arg;
    /* barrier: all threads must be alive at once, otherwise a thread that has
     * already exited can have its TLS address reused by a later one. */
    wb_atomic_fetch_add_relaxed(&g_arrived, 1);
    while (wb_atomic_load_relaxed(&g_arrived) < NTHREADS) { }
    wb_tagheap *h = wb_tagheap_thread();
    g_heaps[i] = h;
    /* the heap is stable across calls on the same thread */
    if (wb_tagheap_thread() != h) g_heaps[i] = (wb_tagheap *)0x1;   /* poison */
    g_ptrs[i] = wb_tagheap_thread_alloc(7u, 64, 16);
    return NULL;
}

int main(void) {
    /* the main thread's heap exists and is stable */
    wb_tagheap *main_heap = wb_tagheap_thread();
    CHECK(main_heap != NULL, "the main thread has a heap");
    CHECK(wb_tagheap_thread() == main_heap, "the per-thread heap is stable");
    CHECK(main_heap != (wb_tagheap *)0x1, "the heap pointer is valid");

    /* ---- each thread gets a distinct heap ---- */
    pthread_t th[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) pthread_create(&th[i], NULL, thread_fn, (void *)(size_t)i);
    for (int i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);

    int distinct = 1;
    for (int i = 0; i < NTHREADS; i++) {
        CHECK(g_heaps[i] != NULL && g_heaps[i] != (wb_tagheap *)0x1, "a worker thread got a valid heap");
        CHECK(g_heaps[i] != main_heap, "a worker's heap differs from the main heap");
        for (int k = i + 1; k < NTHREADS; k++)
            if (g_heaps[i] == g_heaps[k]) distinct = 0;
    }
    CHECK(distinct, "every thread has its own heap");

    /* ---- allocations are isolated per thread ---- */
    int isolated = 1;
    for (int i = 0; i < NTHREADS; i++)
        isolated &= (g_ptrs[i] != NULL)
                 && (wb_tagheap_used(g_heaps[i], 7u) == 64)
                 && (wb_tagheap_used(main_heap, 7u) == 0);   /* invisible to main */
    CHECK(isolated, "each thread's allocation is accounted on its own heap");

    /* the pointers do not alias across threads */
    int alias = 0;
    for (int i = 0; i < NTHREADS; i++)
        for (int k = i + 1; k < NTHREADS; k++)
            if (g_ptrs[i] == g_ptrs[k]) alias = 1;
    CHECK(!alias, "per-thread allocations never alias");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

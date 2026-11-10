/*
 * wb_atomic_test.c -- memory ordering.
 *
 * Message passing: a producer publishes `data` with a release store and a
 * consumer reads it after an acquire load of the flag. The acquire/release pair
 * must guarantee the consumer never observes the flag set with stale data.
 * Plus a relaxed fetch-add fan-in across threads.
 *
 * (On x86-TSO the plain stores would be ordered anyway; the test pins the
 * intended ordering so the code stays correct on weaker models.)
 */
#include "wb_atomic.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define NITER 20000

static wb_atomic_int g_flag;
static int g_data;          /* guarded by the release/acquire flag */
static int g_stale;

static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < NITER; i++) {
        g_data = i + 1;                          /* plain store */
        wb_atomic_store_release(&g_flag, 1);     /* publish */
        while (wb_atomic_load_relaxed(&g_flag) != 0) { /* wait for consume */ }
    }
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    for (int i = 0; i < NITER; i++) {
        while (wb_atomic_load_acquire(&g_flag) != 1) { /* wait for publish */ }
        int d = g_data;                          /* acquire: sees the publish */
        if (d != i + 1) g_stale++;
        wb_atomic_store_release(&g_flag, 0);     /* release: consumed */
    }
    return NULL;
}

#define NTHREADS 4
#define PER_THREAD 2500

static wb_atomic_int g_counter;

static void *adder(void *arg) {
    (void)arg;
    for (int i = 0; i < PER_THREAD; i++) wb_atomic_fetch_add_relaxed(&g_counter, 1);
    return NULL;
}

int main(void) {
    /* ---- the wrappers themselves, single-threaded ---- */
    wb_atomic_int v = (wb_atomic_int)0;
    wb_atomic_store_release(&v, 7);
    CHECK(wb_atomic_load_acquire(&v) == 7, "a release store is visible to an acquire load");
    CHECK(wb_atomic_load_relaxed(&v) == 7, "a release store is visible to a relaxed load");
    wb_atomic_store_release(&v, 0);
    CHECK(wb_atomic_load_acquire(&v) == 0, "the store is overwritten");

    wb_atomic_int c = (wb_atomic_int)10;
    int prev = wb_atomic_fetch_add_relaxed(&c, 5);
    CHECK(prev == 10, "fetch-add returns the previous value");
    CHECK(wb_atomic_load_relaxed(&c) == 15, "fetch-add adds exactly");
    wb_atomic_fetch_add_relaxed(&c, -5);
    CHECK(wb_atomic_load_relaxed(&c) == 10, "fetch-add subtracts with a negative operand");

    /* ---- message passing: release/acquire publishes the payload ---- */
    g_flag = (wb_atomic_int)0;
    g_data = 0; g_stale = 0;
    pthread_t pt, ct;
    pthread_create(&pt, NULL, producer, NULL);
    pthread_create(&ct, NULL, consumer, NULL);
    pthread_join(pt, NULL);
    pthread_join(ct, NULL);
    CHECK(g_stale == 0, "acquire/release never observes the flag with stale data");

    /* ---- relaxed fetch-add fan-in ---- */
    g_counter = (wb_atomic_int)0;
    pthread_t th[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) pthread_create(&th[i], NULL, adder, NULL);
    for (int i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);
    CHECK(wb_atomic_load_relaxed(&g_counter) == NTHREADS * PER_THREAD,
          "relaxed fetch-add aggregates across threads exactly");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

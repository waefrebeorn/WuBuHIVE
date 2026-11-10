/*
 * wb_tls_test.c -- the portable thread-local slot.
 *
 * Devil's-advocate cases: the value is per thread (a worker writing its own slot
 * must not disturb the creator's), the singleton is idempotent, and two slots
 * are independent.
 */
#include "wb_tls.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static wb_tls *g_slot;          /* shared slot, written from two threads */
static void   *g_worker_seen;   /* what the worker read before writing */
static void   *g_worker_wrote;

static void *worker(void *arg) {
    (void)arg;
    g_worker_seen = wb_tls_get(g_slot);          /* should be NULL: a new thread */
    wb_tls_set(g_slot, (void *)0x2);
    g_worker_wrote = wb_tls_get(g_slot);
    return NULL;
}

int main(void) {
    /* ---- create ---- */
    wb_tls *a = wb_tls_new();
    CHECK(a != NULL, "a slot is created");
    CHECK(wb_tls_get(a) == NULL, "a fresh slot is empty");
    CHECK(wb_tls_get(NULL) == NULL, "a NULL slot reads as empty");

    /* ---- set and get ---- */
    wb_tls_set(a, (void *)0x1);
    CHECK(wb_tls_get(a) == (void *)0x1, "the value round-trips");
    wb_tls_set(a, (void *)0xDEAD);
    CHECK(wb_tls_get(a) == (void *)0xDEAD, "the value is overwritten");
    wb_tls_set(a, NULL);
    CHECK(wb_tls_get(a) == NULL, "the value is cleared");
    wb_tls_set(NULL, (void *)0x1);               /* a NULL slot is a no-op */

    /* ---- two slots are independent ---- */
    wb_tls *b = wb_tls_new();
    CHECK(b != NULL && b != a, "a second slot is created and distinct");
    wb_tls_set(a, (void *)0xAAA);
    wb_tls_set(b, (void *)0xBBB);
    CHECK(wb_tls_get(a) == (void *)0xAAA && wb_tls_get(b) == (void *)0xBBB,
          "the slots do not alias");

    /* ---- the singleton is idempotent ---- */
    wb_tls *s1 = wb_tls_slot();
    wb_tls *s2 = wb_tls_slot();
    CHECK(s1 != NULL, "the singleton exists");
    CHECK(s1 == s2, "the singleton is created once");
    wb_tls_set(s1, (void *)0x55);
    CHECK(wb_tls_get(s2) == (void *)0x55, "the singleton is shared");

    /* ---- the value is per thread ---- */
    g_slot = wb_tls_new();
    wb_tls_set(g_slot, (void *)0x11);
    pthread_t th;
    CHECK(pthread_create(&th, NULL, worker, NULL) == 0, "the worker starts");
    pthread_join(th, NULL);
    CHECK(g_worker_seen == NULL, "a new thread starts with an empty slot");
    CHECK(g_worker_wrote == (void *)0x2, "the worker's write is visible to itself");
    CHECK(wb_tls_get(g_slot) == (void *)0x11, "the worker's write did not leak to the creator");

    /* ---- destroy ---- */
    wb_tls_destroy(a);
    wb_tls_destroy(b);
    wb_tls_destroy(g_slot);
    wb_tls_destroy(NULL);                        /* a NULL slot is a no-op */

    /* ---- determinism ---- */
    wb_tls *d1 = wb_tls_new();
    wb_tls *d2 = wb_tls_new();
    wb_tls_set(d1, (void *)0x1234);
    wb_tls_set(d2, (void *)0x1234);
    CHECK(wb_tls_get(d1) == wb_tls_get(d2), "slots behave identically");
    wb_tls_destroy(d1);
    wb_tls_destroy(d2);

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

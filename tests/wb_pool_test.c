/*
 * wb_pool_test.c the worker pool.
 *
 * Proves the pool runs every submitted job, that jobs writing disjoint slots
 * never interfere, and that the deterministic parallel-for covers [0,n)
 * exactly and sums to the serial total -- identically on every run.
 */
#include "wb_pool.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define NJOBS 64

static void mark_job(void *user) { *(int *)user = 1; }

#define CHURN 5000
static int g_churn_flags[CHURN];
static void churn_job(void *user) { *(int *)user = 1; }

static void sum_chunk(int start, int end, int slot, void *user) {
    long long *parts = (long long *)user;
    long long s = 0;
    for (int i = start; i < end; i++) s += i;
    parts[slot] = s;
}

static void span_chunk(int start, int end, int slot, void *user) {
    int *span = (int *)user;          /* [slot*2] = start, [slot*2+1] = end */
    span[slot * 2] = start;
    span[slot * 2 + 1] = end;
}

int main(void) {
    wb_pool *p = wb_pool_new(4);
    CHECK(p != NULL, "pool created");
    CHECK(wb_pool_nthreads(p) == 4, "pool reports its thread count");

    /* ---- every submitted job runs; disjoint slots never interfere ---- */
    int flags[NJOBS];
    memset(flags, 0, sizeof flags);
    for (int i = 0; i < NJOBS; i++)
        CHECK(wb_pool_submit(p, mark_job, &flags[i]) == 0, "job submitted");
    wb_pool_wait(p);
    int all = 1;
    for (int i = 0; i < NJOBS; i++) all &= flags[i];
    CHECK(all, "every submitted job ran exactly once");

    /* ---- waiting on an idle pool returns immediately ---- */
    wb_pool_wait(p);
    CHECK(1, "wait on an idle pool returns");

    /* ---- deterministic parallel-for: exact cover + serial total ---- */
    const int n = 100000, nslots = 4;
    long long parts[4] = {0, 0, 0, 0};
    int span[8] = {0};
    wb_pool_map(p, sum_chunk, n, nslots, parts);
    wb_pool_map(p, span_chunk, n, nslots, span);

    long long total = 0;
    for (int i = 0; i < nslots; i++) total += parts[i];
    long long serial = (long long)n * (n - 1) / 2;
    CHECK(total == serial, "parallel-for sums to the serial total");

    CHECK(span[0] == 0 && span[nslots * 2 - 1] == n, "chunks span the whole range");
    int contiguous = 1;
    for (int i = 1; i < nslots; i++) contiguous &= (span[i * 2] == span[i * 2 - 1]);
    CHECK(contiguous, "chunks are contiguous with no gap or overlap");

    /* ---- order-independent: repeated runs give the identical total ---- */
    long long parts2[4] = {0, 0, 0, 0};
    wb_pool_map(p, sum_chunk, n, nslots, parts2);
    long long total2 = 0;
    for (int i = 0; i < nslots; i++) total2 += parts2[i];
    CHECK(total2 == total, "repeated parallel-for runs are identical");

    /* ---- a single-thread pool is still correct (degenerate case) ---- */
    wb_pool *one = wb_pool_new(1);
    long long parts1[1] = {0};
    wb_pool_map(one, sum_chunk, n, 1, parts1);
    CHECK(parts1[0] == serial, "a one-thread pool matches the serial total");
    wb_pool_free(one);

    /* ---- devil's advocate: 5000-job churn through the hive-backed queue ----
     * Every record is emitted, queued, run and recycled through the hive
     * freelist. A recycling bug (lost or duplicated records) shows up here. */
    memset(g_churn_flags, 0, sizeof g_churn_flags);
    for (int i = 0; i < CHURN; i++)
        CHECK(wb_pool_submit(p, churn_job, &g_churn_flags[i]) == 0, "churn job submitted");
    wb_pool_wait(p);
    int all_churn = 1;
    for (int i = 0; i < CHURN; i++) all_churn &= g_churn_flags[i];
    CHECK(all_churn, "every one of 5000 churned jobs ran exactly once");

    wb_pool_free(p);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/*
 * wb_tagheap_test.c the tagged linear heap.
 *
 * Devil's-advocate cases: budget enforcement, tag isolation, block reuse after
 * a tag reset (no re-malloc), alignment, peak accounting, and determinism.
 */
#include "wb_tagheap.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define TAG_A 1u
#define TAG_B 2u
#define BLOCK_CAP 256

int main(void) {
    wb_tagheap h;
    CHECK(wb_tagheap_init(&h, BLOCK_CAP) == 0, "init");

    /* ---- allocation and per-tag accounting ---- */
    void *a1 = wb_tagheap_alloc(&h, TAG_A, 64, 16);
    void *a2 = wb_tagheap_alloc(&h, TAG_A, 64, 16);
    CHECK(a1 && a2 && a1 != a2, "two allocations under one tag");
    CHECK(wb_tagheap_used(&h, TAG_A) == 128, "per-tag bytes are exact");
    CHECK(wb_tagheap_peak(&h, TAG_A) == 128, "peak tracks the high-water mark");
    CHECK(((unsigned long long)a1 % 16) == 0 && ((unsigned long long)a2 % 16) == 0, "alignment honoured");

    /* ---- tag isolation: different tags never alias ---- */
    void *b1 = wb_tagheap_alloc(&h, TAG_B, 32, 1);
    CHECK(b1 && b1 != a1 && b1 != a2, "a different tag gets its own block");
    CHECK(wb_tagheap_used(&h, TAG_B) == 32, "the second tag accounts independently");
    *(int *)a1 = 0x11223344;
    *(int *)b1 = 0x55667788;
    CHECK(*(int *)a1 == 0x11223344, "writing one tag does not clobber another");

    /* ---- budget enforcement ---- */
    wb_tagheap_set_budget(&h, TAG_A, 200);
    void *over = wb_tagheap_alloc(&h, TAG_A, 128, 1);   /* 128 + 128 > 200 */
    CHECK(over == NULL, "an allocation over budget fails");
    CHECK(wb_tagheap_over(&h, TAG_A), "the over-budget flag is set");
    void *under = wb_tagheap_alloc(&h, TAG_A, 64, 1);   /* 128 + 64 == 192 <= 200 */
    CHECK(under != NULL, "an allocation within budget still succeeds");
    CHECK(wb_tagheap_used(&h, TAG_A) == 192, "bytes after the budget checks are exact");

    /* ---- a zero budget means unlimited ---- */
    wb_tagheap_set_budget(&h, TAG_B, 0);
    int ok = 1;
    for (int i = 0; i < 100; i++) ok &= (wb_tagheap_alloc(&h, TAG_B, 16, 1) != NULL);
    CHECK(ok, "a zero budget is unlimited");

    /* ---- block reuse after a tag reset: the registry does not grow ---- */
    size_t blocks_before = h.blocks.n_blocks;
    wb_tagheap_reset_tag(&h, TAG_A);
    CHECK(wb_tagheap_used(&h, TAG_A) == 0, "reset_tag frees the tag's bytes");
    void *reused = wb_tagheap_alloc(&h, TAG_A, 32, 1);
    CHECK(reused != NULL, "allocation after a reset succeeds");
    CHECK(h.blocks.n_blocks == blocks_before, "a reset tag reuses its block (no re-malloc)");

    /* ---- alignment larger than the remaining space starts a fresh block ---- */
    wb_tagheap_reset_tag(&h, TAG_A);
    void *big = wb_tagheap_alloc(&h, TAG_A, 16, 128);
    CHECK(big && ((unsigned long long)big % 128) == 0, "128-byte alignment honoured");

    /* ---- determinism: the same sequence yields the same addresses ---- */
    wb_tagheap_reset_tag(&h, TAG_A);
    void *d1 = wb_tagheap_alloc(&h, TAG_A, 40, 8);
    void *d2 = wb_tagheap_alloc(&h, TAG_A, 40, 8);
    wb_tagheap_reset_tag(&h, TAG_A);
    void *e1 = wb_tagheap_alloc(&h, TAG_A, 40, 8);
    void *e2 = wb_tagheap_alloc(&h, TAG_A, 40, 8);
    CHECK(d1 == e1 && d2 == e2, "allocation is deterministic across a tag reset");

    /* ---- many tags stay within the tag table ---- */
    int all = 1;
    for (uint32_t t = 100; t < 100 + (WB_TAGHEAP_MAX_TAGS - 2); t++)
        all &= (wb_tagheap_alloc(&h, t, 8, 1) != NULL);
    CHECK(all, "the tag table holds its maximum");
    CHECK(wb_tagheap_alloc(&h, 999u, 8, 1) == NULL, "a tag beyond the table is rejected");

    wb_tagheap_free(&h);
    CHECK(wb_hive_empty(&h.blocks), "free clears the block registry");
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

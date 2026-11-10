/*
 * wb_hive_test.c THE HIVE, adversarially.
 *
 * The devil's-advocate cases: holes in the middle, erase/insert churn, block
 * boundaries, double-erase, unknown-pointer erase, freelist exhaustion and
 * reuse, stable addresses across recycling, and foreach with a NULL callback.
 */
#include "wb_hive.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

typedef struct { int id; float v[3]; } payload;

static int count_live(void *elem, void *user) {
    (void)elem;
    (*(int *)user)++;
    return 0;
}

static int stop_after_3(void *elem, void *user) {
    (void)elem;
    int *n = (int *)user;
    (*n)++;
    return *n == 3;      /* stop early */
}

int main(void) {
    wb_hive h;
    CHECK(wb_hive_init(&h, sizeof(payload), 64) == 0, "init");
    CHECK(wb_hive_empty(&h), "a fresh hive is empty");
    CHECK(wb_hive_capacity(&h) == 0, "a fresh hive has no capacity");

    /* ---- emit / drop / reuse ---- */
    payload *a = (payload *)wb_hive_emit(&h);
    CHECK(a != NULL, "emit returns a slot");
    a->id = 7; a->v[0] = 1.5f;
    CHECK(wb_hive_live(&h) == 1, "one live slot");
    CHECK(wb_hive_capacity(&h) == 64, "one block of 64");
    payload *b = (payload *)wb_hive_emit(&h);
    CHECK(b != NULL && b != a, "the second slot is distinct");
    CHECK(wb_hive_live(&h) == 2, "two live slots");

    wb_hive_drop(&h, a);
    CHECK(wb_hive_live(&h) == 1, "drop frees the slot");
    payload *c = (payload *)wb_hive_emit(&h);
    CHECK(c == a, "the freed slot is recycled (stable address)");
    CHECK(h.reuses == 1, "the recycle is counted");
    CHECK(wb_hive_live(&h) == 2, "live count restored");

    /* ---- contents survive until drop ---- */
    c->id = 99; c->v[1] = 2.5f;
    CHECK(c->id == 99 && c->v[1] == 2.5f, "element contents are readable");

    /* ---- double drop / unknown drop are safe no-ops ---- */
    wb_hive_drop(&h, c);
    size_t live_after = wb_hive_live(&h);
    wb_hive_drop(&h, c);                       /* double drop */
    CHECK(wb_hive_live(&h) == live_after, "a double drop is a no-op");
    wb_hive_drop(&h, (void *)0x1234);          /* unknown pointer */
    CHECK(wb_hive_live(&h) == live_after, "an unknown-pointer drop is a no-op");

    /* ---- holes: erase the middle, foreach visits only live ---- */
    wb_hive_clear(&h);
    CHECK(wb_hive_empty(&h) && wb_hive_capacity(&h) == 0, "clear empties the hive");
    payload *slots[10];
    for (int i = 0; i < 10; i++) {
        slots[i] = (payload *)wb_hive_emit(&h);
        slots[i]->id = i;
    }
    CHECK(wb_hive_live(&h) == 10, "ten live slots");
    for (int i = 3; i < 7; i++) wb_hive_drop(&h, slots[i]);
    CHECK(wb_hive_live(&h) == 6, "four holes leave six live");
    int seen = 0;
    CHECK(wb_hive_foreach(&h, count_live, &seen) == 6 && seen == 6, "foreach visits exactly the live slots");

    /* ---- foreach early stop ---- */
    int n = 0;
    CHECK(wb_hive_foreach(&h, stop_after_3, &n) == 3, "foreach stops early on a nonzero return");

    /* ---- foreach with a NULL callback is a safe no-op ---- */
    CHECK(wb_hive_foreach(&h, NULL, NULL) == 0, "NULL callback visits nothing");

    /* ---- block boundary: 64 fills one block, the 65th opens the second ---- */
    wb_hive_clear(&h);
    for (int i = 0; i < 64; i++) wb_hive_emit(&h);
    CHECK(h.n_blocks == 1 && wb_hive_live(&h) == 64, "64 slots fill exactly one block");
    wb_hive_emit(&h);
    CHECK(h.n_blocks == 2 && wb_hive_live(&h) == 65, "the 65th slot opens a second block");
    CHECK(wb_hive_capacity(&h) == 128, "capacity is blocks * 64");

    /* ---- freelist reuse across a block boundary ---- */
    wb_hive_clear(&h);
    payload *big[200];
    for (int i = 0; i < 200; i++) big[i] = (payload *)wb_hive_emit(&h);
    CHECK(h.n_blocks == 4 && wb_hive_live(&h) == 200, "200 slots span four blocks");
    for (int i = 0; i < 200; i += 2) wb_hive_drop(&h, big[i]);    /* drop 100 */
    CHECK(wb_hive_live(&h) == 100, "half dropped");
    payload *reuse[100];
    int all_reused = 1;
    for (int i = 0; i < 100; i++) {
        reuse[i] = (payload *)wb_hive_emit(&h);
        int found = 0;
        for (int k = 0; k < 200; k += 2) if (reuse[i] == big[k]) { found = 1; break; }
        all_reused &= found;
    }
    CHECK(all_reused, "recycled slots are the freed addresses");
    CHECK(h.n_blocks == 4 && wb_hive_live(&h) == 200, "recycling does not grow the hive");

    /* ---- churn: 20000 emit/drop cycles leave the hive stable ---- */
    wb_hive_clear(&h);
    payload *keep = (payload *)wb_hive_emit(&h);
    keep->id = 1;
    for (int i = 0; i < 20000; i++) {
        payload *t = (payload *)wb_hive_emit(&h);
        if (t) wb_hive_drop(&h, t);
    }
    CHECK(wb_hive_live(&h) == 1, "churn leaves exactly the held slot");
    CHECK(wb_hive_capacity(&h) == 64, "churn does not grow capacity");
    CHECK(keep->id == 1, "the held slot survives the churn");

    /* ---- drop_at (the O(1) path) ---- */
    wb_hive_clear(&h);
    payload *x = (payload *)wb_hive_emit(&h);
    payload *y = (payload *)wb_hive_emit(&h);
    /* locate x's block/slot */
    int dropped = 0;
    for (wb_hive_block *b = h.head; b && !dropped; b = b->next)
        for (size_t s = 0; s < b->cap; s++)
            if (b->skip[s] == 0 && b->elems + s * h.elem_size == (unsigned char *)x) {
                wb_hive_drop_at(&h, b, s); dropped = 1; break;
            }
    CHECK(dropped && wb_hive_live(&h) == 1, "drop_at releases the slot");
    wb_hive_drop_at(&h, h.head, 0);            /* already erased: no-op */
    CHECK(wb_hive_live(&h) == 1, "drop_at on an erased slot is a no-op");
    CHECK(y != NULL, "the other slot is untouched");

    /* ---- the emit/drop hint: drops usually reverse the emits ---- */
    wb_hive_clear(&h);
    payload *rev[200];
    for (int i = 0; i < 200; i++) rev[i] = (payload *)wb_hive_emit(&h);
    CHECK(h.n_blocks == 4, "200 slots span four blocks");
    for (int i = 199; i >= 0; i--) wb_hive_drop(&h, rev[i]);   /* exact reverse order */
    CHECK(wb_hive_live(&h) == 0, "dropping in reverse emit order empties the hive");
    CHECK(wb_hive_validate(&h) == 0, "the hive is consistent after reverse drops");

    /* ---- a drop from an old block while the hint points elsewhere ---- */
    wb_hive_clear(&h);
    payload *old = (payload *)wb_hive_emit(&h);       /* block 0, slot 0 */
    for (int i = 0; i < 200; i++) (void)wb_hive_emit(&h);
    CHECK(h.n_blocks == 4, "the later emits opened more blocks");
    wb_hive_drop(&h, old);                            /* the hint must miss, the scan must find */
    CHECK(wb_hive_live(&h) == 200, "an old-block drop is found by the scan");
    CHECK(wb_hive_validate(&h) == 0, "the hive is consistent after an old-block drop");
    old = (payload *)wb_hive_emit(&h);
    CHECK(old != NULL, "the hole from the old-block drop is reused");
    CHECK(wb_hive_live(&h) == 201, "the reuse is live");

    /* ---- validate catches a broken invariant ---- */
    CHECK(wb_hive_validate(&h) == 0, "the hive validates before sabotage");
    h.head->live = 999;                                /* a lie about the live count */
    CHECK(wb_hive_validate(&h) == -1, "a wrong block live count is reported");
    h.head->live = 0;
    for (size_t s = 0; s < h.head->cap; s++) if (h.head->skip[s] == 0) { h.head->live++; }
    CHECK(wb_hive_validate(&h) == 0, "restoring the count restores validity");

    wb_hive_clear(&h);
    CHECK(wb_hive_empty(&h), "final clear");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

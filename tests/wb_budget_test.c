/*
 * wb_budget_test.c named byte budgets.
 *
 * Devil's-advocate cases: latched over-budget, refund clamping, peak tracking,
 * unlimited budgets, unknown names, duplicate declarations, table capacity,
 * and the build-gate predicate (any_over).
 */
#include "wb_budget.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define FRAME   "frame"
#define RENDER  "render"
#define PHYSICS "physics"

int main(void) {
    wb_budget_set b;
    wb_budget_init(&b);
    CHECK(!wb_budget_any_over(&b), "no budgets means no overrun");

    /* ---- declare + charge within limit ---- */
    CHECK(wb_budget_add(&b, FRAME, 1000) != NULL, "frame budget declared");
    CHECK(wb_budget_add(&b, RENDER, 500) != NULL, "render budget declared");
    wb_budget_charge(&b, FRAME, 400);
    CHECK(wb_budget_used(&b, FRAME) == 400, "charge is accounted");
    CHECK(wb_budget_ok(&b, FRAME) && !wb_budget_over(&b, FRAME), "within budget is ok");

    /* ---- exceed the limit: latched ---- */
    wb_budget_charge(&b, FRAME, 700);                 /* 1100 > 1000 */
    CHECK(wb_budget_used(&b, FRAME) == 1100, "over-budget charge is recorded");
    CHECK(!wb_budget_ok(&b, FRAME) && wb_budget_over(&b, FRAME), "over budget is flagged");
    CHECK(wb_budget_any_over(&b), "any_over is the build-gate predicate");

    /* ---- refund does not clear the latch ---- */
    wb_budget_refund(&b, FRAME, 200);
    CHECK(wb_budget_used(&b, FRAME) == 900, "refund reduces the charge");
    CHECK(wb_budget_over(&b, FRAME), "over stays latched until an explicit reset");
    CHECK(!wb_budget_ok(&b, FRAME), "a latched over-budget stays not-ok");

    /* ---- reset clears it (a per-frame budget) ---- */
    wb_budget_reset(&b, FRAME);
    CHECK(wb_budget_used(&b, FRAME) == 0 && !wb_budget_over(&b, FRAME), "reset clears used + over");
    CHECK(wb_budget_ok(&b, FRAME) && !wb_budget_any_over(&b), "after reset nothing is over");

    /* ---- refund clamps at zero ---- */
    wb_budget_charge(&b, RENDER, 100);
    wb_budget_refund(&b, RENDER, 9999);
    CHECK(wb_budget_used(&b, RENDER) == 0, "refund clamps at zero");

    /* ---- peak tracks the high-water mark ---- */
    wb_budget_charge(&b, RENDER, 300);
    wb_budget_charge(&b, RENDER, 100);
    wb_budget_refund(&b, RENDER, 200);
    CHECK(wb_budget_peak(&b, RENDER) == 400, "peak tracks the high-water mark");
    CHECK(wb_budget_used(&b, RENDER) == 200, "used is the current charge");

    /* ---- a zero limit means unlimited ---- */
    wb_budget_add(&b, PHYSICS, 0);
    wb_budget_charge(&b, PHYSICS, (size_t)1 << 40);
    CHECK(wb_budget_ok(&b, PHYSICS) && !wb_budget_over(&b, PHYSICS), "a zero limit is unlimited");

    /* ---- unknown names are safe no-ops ---- */
    wb_budget_charge(&b, "nope", 12345);
    wb_budget_refund(&b, "nope", 12345);
    wb_budget_reset(&b, "nope");
    CHECK(wb_budget_ok(&b, "nope") && wb_budget_used(&b, "nope") == 0, "unknown names are no-ops");
    CHECK(!wb_budget_any_over(&b), "unknown names do not trip the gate");

    /* ---- duplicate declarations return the same budget ---- */
    wb_budget *r1 = wb_budget_add(&b, RENDER, 9999);
    wb_budget *r2 = wb_budget_add(&b, RENDER, 9999);
    CHECK(r1 == r2, "re-declaring a budget returns the same entry");
    CHECK(b.n == 3, "no duplicate entry was created");

    /* ---- the table has a fixed capacity ---- */
    wb_budget_set small;
    wb_budget_init(&small);
    char names[WB_BUDGET_MAX][16];
    int added = 0;
    for (int i = 0; i < WB_BUDGET_MAX; i++) {
        snprintf(names[i], sizeof names[i], "b%d", i);
        if (wb_budget_add(&small, names[i], 0)) added++;
    }
    CHECK(added == WB_BUDGET_MAX, "the table holds its maximum");
    CHECK(wb_budget_add(&small, "one-too-many", 0) == NULL, "a full table rejects a new budget");

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

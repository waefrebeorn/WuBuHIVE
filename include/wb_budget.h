/*
 * wb_budget.h -- named byte budgets.
 *
 * Every subsystem gets a named budget; the frame's allocations are charged to
 * it and the build FAILS when a budget is exceeded instead of letting a slow
 * overrun reach production. A budget is the cheapest possible regression test:
 * it turns "we are a bit over" into a red build.
 *
 * This is the reporting half. wb_tagheap enforces per-tag budgets at
 * allocation time; this module tracks and reports any named budget, so a
 * frame's total, a subsystem's total, or a single tag can all be checked from
 * one place.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_BUDGET_H
#define WB_BUDGET_H

#include <stddef.h>

#define WB_BUDGET_MAX 32

typedef struct {
    const char *name;
    size_t limit;      /* 0 = unlimited */
    size_t used;
    size_t peak;
    int    over;       /* set once a charge exceeds the limit */
} wb_budget;

typedef struct {
    wb_budget budgets[WB_BUDGET_MAX];
    int       n;
} wb_budget_set;

void wb_budget_init(wb_budget_set *b);

/* Declare a budget with a limit (bytes). Returns the budget, or NULL if the
 * table is full. Calling again with the same name returns the same budget. */
wb_budget *wb_budget_add(wb_budget_set *b, const char *name, size_t limit);

wb_budget *wb_budget_find(wb_budget_set *b, const char *name);

/* Charge / refund bytes against a budget. Unknown names are a no-op. */
void wb_budget_charge(wb_budget_set *b, const char *name, size_t bytes);
void wb_budget_refund(wb_budget_set *b, const char *name, size_t bytes);
/* Clear a budget's used + over (a per-frame budget resets each frame). */
void wb_budget_reset(wb_budget_set *b, const char *name);

/* 1 when the budget is within its limit (or unknown), 0 when exceeded. */
int  wb_budget_ok(const wb_budget_set *b, const char *name);
int  wb_budget_over(const wb_budget_set *b, const char *name);

/* The build-gate predicate: 1 when ANY budget is over (fail the build). */
int  wb_budget_any_over(const wb_budget_set *b);

size_t wb_budget_used(const wb_budget_set *b, const char *name);
size_t wb_budget_peak(const wb_budget_set *b, const char *name);

#endif /* WB_BUDGET_H */

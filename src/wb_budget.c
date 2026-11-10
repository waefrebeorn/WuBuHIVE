/*
 * wb_budget.c -- named byte budgets. See wb_budget.h.
 */
#include "wb_budget.h"

#include <string.h>

void wb_budget_init(wb_budget_set *b) {
    if (!b) return;
    memset(b, 0, sizeof(*b));
}

wb_budget *wb_budget_find(wb_budget_set *b, const char *name) {
    if (!b || !name) return NULL;
    for (int i = 0; i < b->n; i++)
        if (b->budgets[i].name == name || strcmp(b->budgets[i].name, name) == 0)
            return &b->budgets[i];
    return NULL;
}

wb_budget *wb_budget_add(wb_budget_set *b, const char *name, size_t limit) {
    if (!b || !name) return NULL;
    wb_budget *ex = wb_budget_find(b, name);
    if (ex) return ex;
    if (b->n >= WB_BUDGET_MAX) return NULL;
    wb_budget *t = &b->budgets[b->n++];
    t->name = name;
    t->limit = limit;
    t->used = 0;
    t->peak = 0;
    t->over = 0;
    return t;
}

void wb_budget_charge(wb_budget_set *b, const char *name, size_t bytes) {
    wb_budget *t = wb_budget_find(b, name);
    if (!t) return;
    t->used += bytes;
    if (t->used > t->peak) t->peak = t->used;
    if (t->limit && t->used > t->limit) t->over = 1;
}

void wb_budget_refund(wb_budget_set *b, const char *name, size_t bytes) {
    wb_budget *t = wb_budget_find(b, name);
    if (!t) return;
    t->used = bytes > t->used ? 0 : t->used - bytes;   /* clamp: never negative */
}

void wb_budget_reset(wb_budget_set *b, const char *name) {
    wb_budget *t = wb_budget_find(b, name);
    if (!t) return;
    t->used = 0;
    t->over = 0;
}

int wb_budget_ok(const wb_budget_set *b, const char *name) {
    const wb_budget *t = wb_budget_find((wb_budget_set *)b, name);
    return !t || !t->over;
}

int wb_budget_over(const wb_budget_set *b, const char *name) {
    const wb_budget *t = wb_budget_find((wb_budget_set *)b, name);
    return t ? t->over : 0;
}

int wb_budget_any_over(const wb_budget_set *b) {
    if (!b) return 0;
    for (int i = 0; i < b->n; i++) if (b->budgets[i].over) return 1;
    return 0;
}

size_t wb_budget_used(const wb_budget_set *b, const char *name) {
    const wb_budget *t = wb_budget_find((wb_budget_set *)b, name);
    return t ? t->used : 0;
}

size_t wb_budget_peak(const wb_budget_set *b, const char *name) {
    const wb_budget *t = wb_budget_find((wb_budget_set *)b, name);
    return t ? t->peak : 0;
}

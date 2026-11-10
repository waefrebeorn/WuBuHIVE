/*
 * wb_tls.c -- thread-local pointer slots, portably. See wb_tls.h.
 */
#include "wb_tls.h"

#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

struct wb_tls { DWORD idx; };

wb_tls *wb_tls_new(void) {
    DWORD idx = TlsAlloc();
    if (idx == TLS_OUT_OF_INDEXES) return NULL;
    wb_tls *t = (wb_tls *)malloc(sizeof *t);
    if (!t) { TlsFree(idx); return NULL; }
    t->idx = idx;
    return t;
}

void wb_tls_destroy(wb_tls *t) {
    if (!t) return;
    TlsFree(t->idx);
    free(t);
}

void *wb_tls_get(wb_tls *t) { return t ? TlsGetValue(t->idx) : NULL; }
void  wb_tls_set(wb_tls *t, void *p) { if (t) TlsSetValue(t->idx, p); }

/* InitOnceExecuteOnce is Vista+, which is the floor for anything built today;
 * it removes the hand-rolled interlocked spin this replaced. */
static INIT_ONCE   wb_tls_once = INIT_ONCE_STATIC_INIT;
static wb_tls     *wb_tls_singleton;

static BOOL CALLBACK wb_tls_init_once(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    wb_tls_singleton = wb_tls_new();
    return TRUE;
}

wb_tls *wb_tls_slot(void) {
    InitOnceExecuteOnce(&wb_tls_once, wb_tls_init_once, NULL, NULL);
    return wb_tls_singleton;
}

#else /* POSIX */
#include <pthread.h>

struct wb_tls { pthread_key_t key; };

wb_tls *wb_tls_new(void) {
    wb_tls *t = (wb_tls *)malloc(sizeof *t);
    if (!t) return NULL;
    if (pthread_key_create(&t->key, NULL) != 0) { free(t); return NULL; }
    return t;
}

void wb_tls_destroy(wb_tls *t) {
    if (!t) return;
    pthread_key_delete(t->key);
    free(t);
}

void *wb_tls_get(wb_tls *t) { return t ? pthread_getspecific(t->key) : NULL; }
void  wb_tls_set(wb_tls *t, void *p) { if (t) pthread_setspecific(t->key, p); }

static pthread_once_t wb_tls_once = PTHREAD_ONCE_INIT;
static wb_tls        *wb_tls_singleton;

static void wb_tls_init_once(void) { wb_tls_singleton = wb_tls_new(); }

wb_tls *wb_tls_slot(void) {
    pthread_once(&wb_tls_once, wb_tls_init_once);
    return wb_tls_singleton;
}

#endif

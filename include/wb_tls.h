/*
 * wb_tls.h -- thread-local pointer slots, portably (WuBuHIVE).
 *
 * Every wubu project needs "one pointer per thread" at some point: a per-thread
 * heap, a per-thread scratch buffer, a per-thread log context. The obvious
 * answers are each wrong somewhere:
 *
 *   - `_Thread_local` in a struct is impossible, and on Windows a static link
 *     against winpthreads routes thread_local through emutls, which imports
 *     libwinpthread-1.dll -- and that DLL is what this machine's Code Integrity
 *     policy blocks. Win32 TLS needs no such support.
 *   - `pthread_key_create` is correct everywhere but drags in pthread on
 *     platforms that have not yet merged it into libc.
 *
 * So: Win32 TLS on Windows, pthread keys elsewhere, one API. A slot is a
 * process-wide key; each thread sees its own value.
 *
 * C11, no deps beyond the platform.
 */
#ifndef WB_TLS_H
#define WB_TLS_H

typedef struct wb_tls wb_tls;

/* Create a slot. Returns NULL on failure. */
wb_tls *wb_tls_new(void);

void   *wb_tls_get(wb_tls *t);
void    wb_tls_set(wb_tls *t, void *p);

/* Destroy a slot. Values already stored in other threads are not freed. */
void    wb_tls_destroy(wb_tls *t);

/* The process-wide singleton slot, created lazily and thread-safely. Used by
 * wb_tagheap for its per-thread heaps. Returns NULL only if slot creation fails. */
wb_tls *wb_tls_slot(void);

#endif /* WB_TLS_H */

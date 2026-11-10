# WuBuHIVE

**The hive memory substrate, extracted from the WWW engine for reuse across all
wubu projects.**

The hive is the answer to a question every engine asks eventually: *where does
mutable state with stable addresses live?* A FIFO ring gives you stable addresses
but no random erase. A linked list gives you erase but no locality and a malloc
per node. `malloc`/`free` gives you nothing stable at all.

The hive is **linked fixed blocks + a skipfield + a freelist**:

```
struct block {
    unsigned char *elems;   // inline storage, cap * elem_size
    uint8_t       *skip;    // skipfield: 1 = erased, 0 = live
    size_t         live, cap;
    struct block  *next;
};
```

- **Stable slots.** A live element's address never moves. No compaction, no
  invalidation, no handles going stale.
- **O(1) amortised insert.** Reuse a freelist slot, else append to the tail
  block, else open a new block.
- **O(1) erase.** Set the skip byte, push the slot on the freelist.
- **Cache-friendly iteration.** The skipfield skips holes in one read per slot,
  and each block is a contiguous element array.
- **A hint makes drops O(1).** The block of the most recent emit/drop is
  remembered; drops usually reverse the emits, so the common case is one range
  check instead of a block scan.

Slermed from the wubuwizard/slermes HIVE (`wubu_hive.c`) and re-cut as a
standalone library. In wubuwizard and slermes the hive replaces FIFO rings for
exactly this reason; this is that code, cleaned up and gated.

## Modules

| Module | What it is | Depends on |
|---|---|---|
| `wb_hive` | linked fixed blocks + skipfield + freelist | — |
| `wb_tagheap` | Naughty Dog tagged-lifetime heaps over hive memory, per-thread | `wb_hive`, `wb_tls` |
| `wb_budget` | named byte budgets that fail the build when exceeded | — |
| `wb_frame` | bump allocator reset once per frame | — |
| `wb_tls` | one pointer per thread, portably | — |
| `wb_atomic` | explicit memory-ordering wrappers | — |
| `wb_pool` | worker pool whose job queue is hive memory | `wb_hive`, pthread |

## Build and test

```sh
make          # build build/libwubuhive.a
make test     # run every gate
```

`make test` runs eight gates: `test-hive`, `test-tagheap`, `test-budget`,
`test-frame`, `test-atomic`, `test-tls`, `test-pool`, `test-threadheap`.

On Windows the threaded gates link winpthreads statically (`PTHREAD := -static`
in the Makefile). The static `libpthread.a` still imports `libwinpthread-1.dll`
through emutls, so only `-static` removes the dependency — and this machine's
Code Integrity policy blocks that DLL outright.

## Portability

**Thread-local storage.** `wb_tls` is the one place that knows the answer:

- Windows: Win32 TLS (`TlsAlloc`/`TlsGetValue`/`TlsSetValue`).
  `_Thread_local` under a static link routes through emutls, which imports
  `libwinpthread-1.dll`.
- POSIX: `pthread_key_create`/`pthread_getspecific`/`pthread_setspecific`.

Both expose the same three calls, plus `wb_tls_slot()`: a process-wide
lazily-created singleton slot, which is what `wb_tagheap_thread()` uses for its
per-thread heaps.

**Threads.** Only `wb_pool` and the two threaded gates need pthread. Everything
else is single-threaded and lock-free by construction.

## Reuse

The headers are in `include/`, the sources in `src/`. Copy both into a project,
add `-Iinclude`, and compile the `.c` files you use — `wb_hive.c` is the only
mandatory one. `build/libwubuhive.a` is there if you would rather link a archive.

Nothing here includes a project header, and nothing here allocates behind your
back beyond the blocks the hive itself manages.

## Layout

```
include/   wb_hive.h  wb_tagheap.h  wb_budget.h  wb_frame.h
           wb_atomic.h  wb_tls.h  wb_pool.h
src/       wb_hive.c  wb_tagheap.c  wb_budget.c  wb_frame.c
           wb_tls.c  wb_pool.c
tests/     one gate per module, plus the threaded pair
```

## License

WaefreBeorn Umbrella License v3.0 — see `LICENSE`.

# Changelog

WuBuHIVE is the hive work extracted from the WWW engine (`~WuBu~/src/core/`).
This file records what changed in the extraction, so a project that already
carries an older copy can see what it is picking up.

## 1.0.0 — extraction from WWW

### Added

- **`wb_tls`** — a portable thread-local pointer slot, new in this repo. The
  per-thread heap code in `wb_tagheap.c` used to carry its own `#ifdef _WIN32`
  block with a hand-rolled `InterlockedCompareExchange` spin; that platform
  answer now lives in one header and one source, and every other wubu project
  gets the same correct one. Windows uses Win32 TLS; POSIX uses pthread keys;
  `wb_tls_slot()` is a lazily-created process-wide singleton
  (`InitOnceExecuteOnce` / `pthread_once`).
- **`test-tls`** — a gate for the above, including the case that matters: a
  worker thread writing its own slot must not disturb the creator's value.

### Changed

- **`wb_hive` emit/drop hint.** The hive remembers the block of the most recent
  emit and the most recent drop. `wb_hive_drop` checks that block first and only
  falls back to the block scan on a miss. Drops usually reverse the emits, so
  the common case is one range check instead of walking every block — which is
  the difference between O(1) and O(blocks) for a pool that has been running a
  while. `wb_hive_validate` still holds after the change (gated).
- **`wb_tagheap` per-thread heaps** now go through `wb_tls`, dropping the
  `_Thread_local` path on POSIX along with the Windows spin. Same behaviour,
  one platform answer, and the Windows path no longer busy-waits.

### Fixed

- **`wb_hive_clear` preserves its configuration.** The first version memset the
  whole hive, wiping `elem_size` and `block_cap`; a clear followed by an emit
  then wrote elements of the wrong stride. `clear` is a content reset, not a
  de-initialisation — the configuration now survives it. Caught by the
  adversarial gate, not by inspection.
- **`wb_hive_block_new` marks a fresh block fully erased.** Without it,
  `wb_hive_foreach` visited never-written slots.

### Kept as-is (deliberately)

- `wb_hive_free_push` still drops a slot on freelist growth failure rather than
  failing the erase; the slot leaks instead of corrupting. `wb_hive_validate`
  reports this as `-4`, because a hole with no freelist entry breaks the
  append-at-`live` invariant.
- The pool's job records stay in hive memory with intrusive `next` pointers:
  the hive is the memory, the links are the ordering.

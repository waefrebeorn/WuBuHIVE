/*
 * wb_hive.h -- THE HIVE: linked fixed blocks + skipfield + freelist.
 *
 * Slermed from the wubuwizard/slermes HIVE (wubu_hive.c) and re-cut as the
 * reusable substrate it always was: this replaces FIFO rings and malloc/free
 * chains anywhere a project needs stable slots and high churn.
 *
 *   struct block {
 *       unsigned char *elems;  // inline storage, cap * elem_size
 *       uint8_t *skip;         // skipfield: 1 = erased, 0 = live
 *       size_t live, cap;
 *       struct block *next;
 *   };
 *
 * Why the hive beats a FIFO ring or a linked list:
 *   - cache: each block is a contiguous element array (vector-like locality)
 *   - stable slots: a live element's address never moves; no compaction
 *   - O(1) amortized insert: reuse a freelist slot, else append, else new block
 *   - fast erase: mark the skip byte + push the slot to the freelist
 *   - fast iterate: the skipfield skips holes in one read per slot
 *
 * The element address is stable from emit() until drop(); that is the contract
 * the pool's queue links rely on.
 *
 * C11, no deps.
 */
#ifndef WB_HIVE_H
#define WB_HIVE_H

#include <stddef.h>
#include <stdint.h>

typedef struct wb_hive_block {
    unsigned char       *elems;   /* cap * elem_size, inline */
    uint8_t             *skip;    /* cap bytes: 1 = erased, 0 = live */
    size_t               live, cap;
    struct wb_hive_block *next;
} wb_hive_block;

typedef struct {
    wb_hive_block *block;
    size_t         slot;
} wb_hive_slot;

typedef struct {
    size_t       elem_size;
    size_t       block_cap;
    wb_hive_block *head, *tail;
    size_t       n_blocks, total_live;
    wb_hive_slot *free_entries;   /* LIFO stack of reusable slots */
    size_t       n_free, free_cap;
    size_t       allocs, reuses;
    wb_hive_block *hint;          /* the block of the most recent emit: drops
                                     usually reverse the emits, so this makes the
                                     common drop O(1) instead of a block scan */
} wb_hive;

/* elem_size must be > 0; block_cap is the fixed slots-per-block. */
int  wb_hive_init(wb_hive *h, size_t elem_size, size_t block_cap);
void wb_hive_clear(wb_hive *h);

/* Acquire a live element slot (contents undefined), or NULL when out of memory.
 * The address is stable until the matching drop. */
void *wb_hive_emit(wb_hive *h);
/* Release an element obtained from emit (by pointer). */
void  wb_hive_drop(wb_hive *h, void *elem);
/* O(1) release when the caller already knows the slot. */
void  wb_hive_drop_at(wb_hive *h, wb_hive_block *blk, size_t slot);

/* Visit every live element; return nonzero from fn to stop early. */
size_t wb_hive_foreach(wb_hive *h, int (*fn)(void *elem, void *user), void *user);

size_t wb_hive_live(const wb_hive *h);
size_t wb_hive_capacity(const wb_hive *h);
int    wb_hive_empty(const wb_hive *h);

/* Validate every hive invariant. Returns 0 when consistent, or a negative code:
 *   -1 a block's live count disagrees with its skipfield
 *   -2 total_live disagrees with the sum of the block lives
 *   -3 a freelist entry is out of range or points at a live slot
 *   -4 a block's live slots are not contiguous at [0, live) while the
 *      freelist is empty (the append path's precondition)
 */
int wb_hive_validate(const wb_hive *h);

#endif /* WB_HIVE_H */

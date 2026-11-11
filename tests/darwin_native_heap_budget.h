#ifndef NIL_DARWIN_NATIVE_HEAP_BUDGET_H
#define NIL_DARWIN_NATIVE_HEAP_BUDGET_H

#include <stddef.h>
#include <stdint.h>

#define NIL_HEAP_BUDGET_BYTES (UINT64_C(96) * 1024 * 1024)
#define NIL_HEAP_BUDGET_EXIT 86
#define NIL_HEAP_TRACKER_EXIT 87
#define NIL_HEAP_ABI_VERSION 1

/* Test ABI only. Bytes are intercepted requested payloads, including in-flight
 * reservations, not malloc usable sizes, RSS, VM reservations, or process commit.
 * The fixed tracker table and allocation paths that bypass these entry points
 * are outside this metric. The harness independently checks fixture coverage. */
struct nil_heap_snapshot {
    uint64_t version, budget, live, peak, cumulative, allocations, frees;
    uint64_t reallocations, failures, unknown_frees, withheld;
    uint64_t target_size, target_allocations, target_frees;
    uint64_t denied_request, typed_calls;
};

typedef int (*nil_heap_snapshot_fn)(struct nil_heap_snapshot *, size_t);
typedef void (*nil_heap_fail_realloc_fn)(void);

#endif

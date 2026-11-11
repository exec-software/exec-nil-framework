/* Independent consumers exercise actual libc calls, including untouched calloc.
 * -fno-builtin keeps the optimizer from removing allocation/free pairs. */
#include "darwin_native_heap_budget.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#include <malloc/malloc.h>
#endif

static nil_heap_snapshot_fn snapshot;
static nil_heap_fail_realloc_fn fail_next_realloc;
static void *volatile retained[2048];
static volatile unsigned char observed;

static void write_all(int fd, const char *text) {
    size_t left = strlen(text);
    while (left) {
        ssize_t written = write(fd, text, left);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) _exit(91);
        text += written; left -= (size_t)written;
    }
}
static void require(int condition, const char *message) {
    if (!condition) { write_all(2, message); _exit(90); }
}
static struct nil_heap_snapshot state(void) {
    struct nil_heap_snapshot result;
    require(snapshot(&result, sizeof(result)), "snapshot unavailable\n");
    require(result.version == NIL_HEAP_ABI_VERSION && result.budget == NIL_HEAP_BUDGET_BYTES,
            "snapshot ABI or budget differs\n");
    return result;
}
static void live_is(uint64_t bytes) {
    require(state().live == bytes, "live requested bytes differ\n");
}

static void positive(void) {
    struct nil_heap_snapshot before = state();
    unsigned char *value = malloc(16384);
    require(value != NULL, "malloc failed\n"); live_is(before.live + 16384);
    value[0] = 19; value[16383] = 23;
    value = realloc(value, 65536);
    require(value && value[0] == 19 && value[16383] == 23, "realloc growth lost content\n");
    live_is(before.live + 65536);
    fail_next_realloc(); errno = 0;
    void *failed = realloc(value, 131072);
    require(!failed && errno == ENOMEM && value[0] == 19 && value[16383] == 23,
            "failed realloc invalidated original allocation\n");
    live_is(before.live + 65536);
    value = realloc(value, 4096);
    require(value && value[0] == 19, "realloc shrink lost content\n");
    live_is(before.live + 4096);
    free(value); live_is(before.live);
    value = realloc(NULL, 8192);
    require(value != NULL, "realloc NULL failed\n"); live_is(before.live + 8192);
    free(value); live_is(before.live);
    value = malloc(0);
    live_is(before.live); free(value); live_is(before.live);
    value = calloc(2, 4096);
    require(value != NULL, "calloc failed\n"); live_is(before.live + 8192);
    for (size_t index = 0; index < 8192; ++index) require(value[index] == 0, "calloc not zeroed\n");
    free(value); live_is(before.live);
    volatile size_t huge_count = SIZE_MAX / 3 + 1;
    errno = 0;
    require(!calloc(huge_count, 3) && errno == ENOMEM, "calloc overflow accepted\n");
    live_is(before.live);
    value = aligned_alloc(64, 4096);
    require(value && (uintptr_t)value % 64 == 0, "aligned_alloc failed\n");
    live_is(before.live + 4096); free(value); live_is(before.live);
    void *aligned = NULL;
    require(posix_memalign(&aligned, 256, 8192) == 0 && aligned && (uintptr_t)aligned % 256 == 0,
            "posix_memalign failed\n");
    live_is(before.live + 8192); free(aligned); live_is(before.live);
    value = valloc(4096);
    require(value && (uintptr_t)value % (size_t)getpagesize() == 0, "valloc failed\n");
    live_is(before.live + 4096); free(value); live_is(before.live);
    free(NULL); live_is(before.live);
#if defined(__APPLE__) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    uint64_t typed_before = state().typed_calls;
    value = malloc_type_malloc(16384, 0);
    require(value != NULL, "typed malloc failed\n"); live_is(before.live + 16384);
    value = malloc_type_realloc(value, 32768, 0);
    require(value != NULL, "typed realloc failed\n"); live_is(before.live + 32768);
    malloc_type_free(value, 0); live_is(before.live);
    value = malloc_type_calloc(2, 4096, 0);
    require(value && value[0] == 0 && value[8191] == 0, "typed calloc failed\n");
    live_is(before.live + 8192); malloc_type_free(value, 0); live_is(before.live);
    value = malloc_type_aligned_alloc(64, 4096, 0);
    require(value && (uintptr_t)value % 64 == 0, "typed aligned_alloc failed\n");
    live_is(before.live + 4096); malloc_type_free(value, 0); live_is(before.live);
    aligned = NULL;
    require(malloc_type_posix_memalign(&aligned, 256, 8192, 0) == 0 && aligned,
            "typed posix_memalign failed\n");
    live_is(before.live + 8192); malloc_type_free(aligned, 0); live_is(before.live);
    value = malloc_type_valloc(4096, 0);
    require(value != NULL, "typed valloc failed\n");
    live_is(before.live + 4096); malloc_type_free(value, 0); live_is(before.live);
    require(state().typed_calls >= typed_before + 11, "typed interception absent\n");
#endif
    /* >2 GiB cumulatively; never more than 64 KiB of this loop remains live. */
    for (size_t index = 0; index < 34000; ++index) {
        value = calloc(1, 65536);
        require(value != NULL, "sustained calloc failed\n");
        observed = value[0]; observed = value[65535];
        free(value);
    }
    live_is(before.live);
    struct nil_heap_snapshot after = state();
    require(after.cumulative - before.cumulative > UINT64_C(2) * 1024 * 1024 * 1024,
            "sustained allocation coverage too small\n");
    require(after.target_allocations >= 34000 && after.target_frees >= 34000,
            "sustained target interception incomplete\n");
    require(after.reallocations >= before.reallocations + 4 && after.failures >= before.failures + 2,
            "realloc/failure controls incomplete\n");
    write_all(1, "heap-controls-ok\n");
}

int main(int argc, char **argv) {
    snapshot = (nil_heap_snapshot_fn)dlsym(RTLD_DEFAULT, "nil_heap_budget_snapshot");
    fail_next_realloc = (nil_heap_fail_realloc_fn)dlsym(RTLD_DEFAULT, "nil_heap_budget_fail_next_realloc");
    require(snapshot && fail_next_realloc, "heap budget injection absent\n");
    require(argc == 2, "expected one control mode\n");
    (void)state();
    if (strcmp(argv[1], "positive") == 0) { positive(); return 0; }
    require(strcmp(argv[1], "retain") == 0, "unknown control mode\n");
    /* Do not touch payload pages: a footprint-only replacement can miss this.
     * Even if the oracle fails, this control requests at most 128 MiB. */
    for (size_t index = 0; index < 2048; ++index) {
        retained[index] = calloc(1, 65536);
        require(retained[index] != NULL, "retention control allocator failed first\n");
    }
    require(0, "retention control exceeded its independent safety ceiling\n");
    return 90;
}

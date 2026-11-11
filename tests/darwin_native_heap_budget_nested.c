/* Linux-only allocator shim for the Darwin oracle's accounting regression.
 * Real libc requests deliberately reenter other interposed public entry points,
 * as Darwin typed/untyped compatibility paths can do. This does not qualify
 * Mach-O injection: the hosted controls still must pass on each architecture. */
#if !defined(NIL_HEAP_BUDGET_PORTABLE_TEST) || !defined(NIL_HEAP_BUDGET_NESTED_TEST)
#error This shim is only for the explicit portable nested-accounting test.
#endif
#include "darwin_native_heap_budget.h"
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t, size_t);
extern void *__real_realloc(void *, size_t);
extern void __real_free(void *);
extern void *__real_aligned_alloc(size_t, size_t);
extern int __real_posix_memalign(void **, size_t, size_t);
extern void *__real_valloc(size_t);
extern int nil_heap_budget_snapshot(struct nil_heap_snapshot *, size_t);

/* TLS is safe in this Linux-only shim, unlike the Darwin interposer itself.
 * Volatile prevents libc's compiler-visible leaf attributes from eliminating
 * the state change around calls that GNU --wrap redirects back into this TU. */
static _Thread_local volatile int nested;
static atomic_int parallel;
static pthread_barrier_t inside, allocated, releasing;

void *nil_heap_nested_malloc(size_t bytes) {
    if (nested) return __real_malloc(bytes);
    nested = 1; void *result = calloc(1, bytes); nested = 0; return result;
}
void *nil_heap_nested_calloc(size_t count, size_t size) {
    if (nested) return __real_calloc(count, size);
    if (size && count > SIZE_MAX / size) { errno = ENOMEM; return NULL; }
    nested = 1;
    if (atomic_load(&parallel) && count * size == 65536) pthread_barrier_wait(&inside);
    void *result = malloc(count * size);
    if (result) memset(result, 0, count * size);
    nested = 0; return result;
}
void nil_heap_nested_free(void *pointer) {
    if (nested) { __real_free(pointer); return; }
    nested = 1; free(pointer); nested = 0;
}
void *nil_heap_nested_realloc(void *pointer, size_t bytes) {
    if (nested) return __real_realloc(pointer, bytes);
    nested = 1;
    void *result = bytes ? malloc(bytes) : NULL;
    if (result && pointer) {
        size_t available = malloc_usable_size(pointer);
        memcpy(result, pointer, bytes < available ? bytes : available);
    }
    if (result || !bytes) free(pointer);
    nested = 0; return result;
}
void *nil_heap_nested_aligned_alloc(size_t alignment, size_t bytes) {
    if (nested) return __real_aligned_alloc(alignment, bytes);
    nested = 1; void *result = NULL;
    int error = posix_memalign(&result, alignment, bytes);
    nested = 0; if (error) errno = error; return result;
}
int nil_heap_nested_posix_memalign(void **out, size_t alignment, size_t bytes) {
    if (nested) return __real_posix_memalign(out, alignment, bytes);
    nested = 1; int result = posix_memalign(out, alignment, bytes); nested = 0; return result;
}
void *nil_heap_nested_valloc(size_t bytes) {
    if (nested) return __real_valloc(bytes);
    nested = 1; void *result = NULL;
    int error = posix_memalign(&result, (size_t)getpagesize(), bytes);
    nested = 0; if (error) errno = error; return result;
}

__attribute__((noreturn)) static void failed(void) {
    static const char message[] = "nested allocator accounting differs\n";
    const ssize_t written = write(2, message, sizeof(message) - 1);
    (void)written; _exit(92);
}
/* Reproduce the actual hosted failure before the report constructor opens its
 * file: without outer-call accounting this exits duplicate-pointer/87. */
__attribute__((constructor(101))) static void early_nested_request(void) {
    void *result = malloc(1024); if (!result) failed(); free(result);
}
static void *parallel_request(void *unused) {
    (void)unused;
    void *result = calloc(1, 65536); if (!result) failed();
    pthread_barrier_wait(&allocated);
    pthread_barrier_wait(&releasing);
    free(result); return NULL;
}
void nil_heap_nested_verify_threads(void) {
    struct nil_heap_snapshot before, during, after;
    pthread_t threads[4];
    if (!nil_heap_budget_snapshot(&before, sizeof(before))) failed();
    if (pthread_barrier_init(&inside, NULL, 5) || pthread_barrier_init(&allocated, NULL, 5) ||
        pthread_barrier_init(&releasing, NULL, 5)) failed();
    atomic_store(&parallel, 1);
    for (size_t index = 0; index < 4; ++index)
        if (pthread_create(&threads[index], NULL, parallel_request, NULL)) failed();
    /* All four original allocators are active together. A process-global
     * reentry flag would incorrectly exclude the other three outer requests. */
    pthread_barrier_wait(&inside);
    pthread_barrier_wait(&allocated);
    if (!nil_heap_budget_snapshot(&during, sizeof(during)) ||
        during.live != before.live + 4 * 65536 || during.allocations != before.allocations + 4 ||
        during.target_allocations != before.target_allocations + 4) failed();
    pthread_barrier_wait(&releasing);
    for (size_t index = 0; index < 4; ++index) if (pthread_join(threads[index], NULL)) failed();
    atomic_store(&parallel, 0);
    if (!nil_heap_budget_snapshot(&after, sizeof(after)) || after.live != before.live ||
        after.frees != before.frees + 4 || after.unknown_frees != before.unknown_frees) failed();
    pthread_barrier_destroy(&inside); pthread_barrier_destroy(&allocated); pthread_barrier_destroy(&releasing);
}

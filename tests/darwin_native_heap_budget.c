/* Test-only allocator retention oracle for owned, non-hardened child binaries.
 * DYLD's self-image exclusion lets replacements call the original entry point:
 * https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld-interposing.h
 * No production allocator counters, sampled RSS, or privileged kernel settings.
 * Build with -fno-builtin and a macOS >=14 deployment target for typed malloc.
 */
#include "darwin_native_heap_budget.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
/* The public SDK omits dyld's convenience macro header. The loader consumes
 * pointer pairs in this documented Mach-O section; keep the declaration here. */
#define NIL_HEAP_INTERPOSE(replacement, original) \
    __attribute__((used, section("__DATA,__interpose,interposing"))) \
    static const struct { const void *new_function, *old_function; } \
    nil_heap_pair_##replacement = { (const void *)(uintptr_t)&replacement, \
                                    (const void *)(uintptr_t)&original };
#include <malloc/malloc.h>
#define ORIGINAL(name) name
#elif defined(NIL_HEAP_BUDGET_PORTABLE_TEST)
/* Linux --wrap exercises the identical accounting and controls locally. It
 * does not validate Mach-O interposition and cannot admit Darwin coverage. */
#if defined(NIL_HEAP_BUDGET_NESTED_TEST)
#define ORIGINAL(name) nil_heap_nested_##name
#else
#define ORIGINAL(name) __real_##name
#endif
extern void *ORIGINAL(malloc)(size_t);
extern void *ORIGINAL(calloc)(size_t, size_t);
extern void *ORIGINAL(realloc)(void *, size_t);
extern void ORIGINAL(free)(void *);
extern void *ORIGINAL(aligned_alloc)(size_t, size_t);
extern int ORIGINAL(posix_memalign)(void **, size_t, size_t);
extern void *ORIGINAL(valloc)(size_t);
#else
#error This test requires Darwin (or the explicit portable accounting check).
#endif

#define TABLE_COUNT (1u << 17)
#define TOMBSTONE ((void *)(uintptr_t)1)
struct entry { void *pointer; size_t bytes; };
static struct entry entries[TABLE_COUNT]; /* fixed 2 MiB on the supported hosts */
static atomic_flag tracker_lock = ATOMIC_FLAG_INIT;
static struct nil_heap_snapshot stats = {
    .version = NIL_HEAP_ABI_VERSION, .budget = NIL_HEAP_BUDGET_BYTES
};
static int report_fd = -1, ready, withhold, fail_realloc;
static char token[33];
#define ACTIVE_THREADS 256u
static struct { pthread_t owner; int active; } active_calls[ACTIVE_THREADS];

static void lock_tracker(void) {
    while (atomic_flag_test_and_set_explicit(&tracker_lock, memory_order_acquire)) {}
}
static void unlock_tracker(void) {
    atomic_flag_clear_explicit(&tracker_lock, memory_order_release);
}

/* Formatting and reporting cannot allocate, even on budget/metadata failure. */
static char *append_text(char *at, const char *text) {
    while (*text) *at++ = *text++;
    return at;
}
static char *append_number(char *at, uint64_t number) {
    char reverse[24]; size_t used = 0;
    do { reverse[used++] = (char)('0' + number % 10); number /= 10; } while (number);
    while (used) *at++ = reverse[--used];
    return at;
}
static char *field(char *at, const char *name, uint64_t value) {
    at = append_text(at, ",\""); at = append_text(at, name);
    at = append_text(at, "\":"); return append_number(at, value);
}
/* Initialization itself can fail before the report file is available. Keep
 * these failures distinguishable from a consumer's retained-allocation exit,
 * without asking stdio or the allocator to format the diagnostic. */
__attribute__((noreturn)) static void tracker_error(const char *event) {
    int saved_errno = errno;
    char line[256], *at = line;
    at = append_text(at, "nil heap tracker: ");
    at = append_text(at, event);
    at = append_text(at, "; errno=");
    at = append_number(at, (uint64_t)saved_errno);
    at = append_text(at, "\n");
    const char *next = line;
    size_t left = (size_t)(at - line);
    while (left) {
        ssize_t written = write(STDERR_FILENO, next, left);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        next += written; left -= (size_t)written;
    }
    _exit(NIL_HEAP_TRACKER_EXIT);
}
static void emit_locked(const char *event) {
    char line[2048], *at = line;
    if (report_fd < 0) return;
    at = append_text(at, "{\"event\":\""); at = append_text(at, event);
    at = append_text(at, "\",\"token\":\""); at = append_text(at, token);
    at = append_text(at, "\"");
    at = field(at, "pid", (uint64_t)getpid());
#define EMIT(member) at = field(at, #member, stats.member)
    EMIT(version); EMIT(budget); EMIT(live); EMIT(peak); EMIT(cumulative);
    EMIT(allocations); EMIT(frees); EMIT(reallocations); EMIT(failures);
    EMIT(unknown_frees); EMIT(withheld); EMIT(target_size);
    EMIT(target_allocations); EMIT(target_frees); EMIT(denied_request); EMIT(typed_calls);
#undef EMIT
    at = append_text(at, "}\n");
    size_t left = (size_t)(at - line); const char *next = line;
    while (left) {
        ssize_t written = write(report_fd, next, left);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) tracker_error("report-write");
        next += written; left -= (size_t)written;
    }
}
static void fail_locked(const char *event, int code) {
    emit_locked(event);
    if (code == NIL_HEAP_TRACKER_EXIT) tracker_error(event);
    _exit(code);
}

/* The original allocator can call another interposed public entry point:
 * notably Darwin's typed/untyped compatibility paths during initialization.
 * Charge only the outer request, including realloc's internal malloc/free.
 * Do not use C/C++ TLS: Darwin's first TLV lookup may itself allocate. pthread
 * self identity is already available to dyld before library initialization.
 * This fixed table has no lifetime allocation and fails closed if exhausted. */
static size_t enter_allocator(void) {
    const pthread_t self = pthread_self();
    size_t vacant = ACTIVE_THREADS;
    lock_tracker();
    for (size_t index = 0; index < ACTIVE_THREADS; ++index) {
        if (!active_calls[index].active) { if (vacant == ACTIVE_THREADS) vacant = index; }
        else if (pthread_equal(active_calls[index].owner, self)) {
            unlock_tracker(); return ACTIVE_THREADS;
        }
    }
    if (vacant == ACTIVE_THREADS) fail_locked("active-thread-limit", NIL_HEAP_TRACKER_EXIT);
    active_calls[vacant].owner = self; active_calls[vacant].active = 1;
    unlock_tracker(); return vacant;
}
static void leave_allocator(size_t slot) {
    int saved_errno = errno;
    lock_tracker(); active_calls[slot].active = 0; unlock_tracker();
    errno = saved_errno;
}
static void *leave_result(void *result, size_t slot) {
    leave_allocator(slot); return result;
}
#define ENTER_OR_FORWARD(call) \
    const size_t active_slot = enter_allocator(); \
    if (active_slot == ACTIVE_THREADS) return (call)

static size_t slot_for(void *pointer, int inserting) {
    size_t first_dead = TABLE_COUNT;
    size_t start = ((uintptr_t)pointer >> 4) * UINT64_C(11400714819323198485);
    for (size_t offset = 0; offset < TABLE_COUNT; ++offset) {
        size_t slot = (start + offset) & (TABLE_COUNT - 1);
        if (entries[slot].pointer == pointer) return slot;
        if (entries[slot].pointer == TOMBSTONE && first_dead == TABLE_COUNT) first_dead = slot;
        if (!entries[slot].pointer) return inserting ? (first_dead == TABLE_COUNT ? slot : first_dead) : TABLE_COUNT;
    }
    return inserting ? first_dead : TABLE_COUNT;
}
static void insert_locked(void *pointer, size_t bytes) {
    size_t slot = slot_for(pointer, 1);
    if (slot == TABLE_COUNT) fail_locked("table-full", NIL_HEAP_TRACKER_EXIT);
    if (entries[slot].pointer == pointer) fail_locked("duplicate-pointer", NIL_HEAP_TRACKER_EXIT);
    entries[slot].pointer = pointer; entries[slot].bytes = bytes;
}
static void remove_locked(size_t slot) {
    entries[slot].pointer = TOMBSTONE; entries[slot].bytes = 0;
}
static void reserve_locked(size_t bytes) {
    if (bytes > stats.budget - stats.live) {
        stats.denied_request = bytes;
        fail_locked("budget", NIL_HEAP_BUDGET_EXIT);
    }
    stats.live += bytes;
    if (stats.live > stats.peak) stats.peak = stats.live;
}
static void reserve(size_t bytes, int typed) {
    lock_tracker(); stats.typed_calls += (uint64_t)typed;
    reserve_locked(bytes); unlock_tracker();
}
static void successful_request_locked(size_t bytes) {
    if (UINT64_MAX - stats.cumulative < bytes) fail_locked("counter-overflow", NIL_HEAP_TRACKER_EXIT);
    stats.cumulative += bytes; ++stats.allocations;
    if (bytes == stats.target_size) ++stats.target_allocations;
}
static void *finish_allocate(void *pointer, size_t bytes) {
    int saved_errno = errno;
    lock_tracker();
    if (pointer) { insert_locked(pointer, bytes); successful_request_locked(bytes); }
    else { stats.live -= bytes; ++stats.failures; }
    unlock_tracker(); errno = saved_errno; return pointer;
}
static int calloc_size(size_t count, size_t size, size_t *bytes, int typed) {
    if (size && count > SIZE_MAX / size) {
        lock_tracker(); ++stats.failures; stats.typed_calls += (uint64_t)typed; unlock_tracker();
        errno = ENOMEM; return 0;
    }
    *bytes = count * size; return 1;
}
static int release(void *pointer, int typed) {
    if (!pointer) return 1;
    lock_tracker(); stats.typed_calls += (uint64_t)typed;
    size_t slot = slot_for(pointer, 0);
    if (slot == TABLE_COUNT) ++stats.unknown_frees;
    else {
        size_t bytes = entries[slot].bytes;
        if (withhold && bytes == stats.target_size) {
            ++stats.withheld; unlock_tracker(); return 0;
        }
        stats.live -= bytes; ++stats.frees;
        if (bytes == stats.target_size) ++stats.target_frees;
        remove_locked(slot);
    }
    unlock_tracker(); return 1;
}

/* Remove the old key before calling libc: realloc may release it and another
 * thread may reuse that address. Keep its bytes reserved until libc returns.
 * No tracker lock is held across a real allocator call. */
struct realloc_state { size_t old_bytes, reserved; int tracked, denied; };
static struct realloc_state begin_realloc(void *pointer, size_t bytes, int typed) {
    struct realloc_state state = {0, 0, 0, 0};
    lock_tracker(); ++stats.reallocations; stats.typed_calls += (uint64_t)typed;
    if (fail_realloc) { fail_realloc = 0; ++stats.failures; state.denied = 1; unlock_tracker(); return state; }
    size_t slot = pointer ? slot_for(pointer, 0) : TABLE_COUNT;
    if (slot != TABLE_COUNT) {
        state.old_bytes = entries[slot].bytes; state.tracked = 1; remove_locked(slot);
    }
    state.reserved = bytes > state.old_bytes ? bytes - state.old_bytes : 0;
    reserve_locked(state.reserved); unlock_tracker(); return state;
}
static void *finish_realloc(void *old_pointer, void *pointer, size_t bytes, struct realloc_state state) {
    int saved_errno = errno;
    lock_tracker(); stats.live -= state.reserved;
    if (!pointer && bytes != 0) {
        if (state.tracked) insert_locked(old_pointer, state.old_bytes);
        ++stats.failures;
    } else {
        stats.live -= state.old_bytes;
        if (state.tracked) {
            ++stats.frees;
            if (state.old_bytes == stats.target_size) ++stats.target_frees;
        }
        if (pointer) {
            stats.live += bytes; insert_locked(pointer, bytes); successful_request_locked(bytes);
        }
    }
    unlock_tracker(); errno = saved_errno; return pointer;
}

static void *budget_malloc(size_t bytes) {
    ENTER_OR_FORWARD(ORIGINAL(malloc)(bytes));
    reserve(bytes, 0); return leave_result(finish_allocate(ORIGINAL(malloc)(bytes), bytes), active_slot);
}
static void *budget_calloc(size_t count, size_t size) {
    ENTER_OR_FORWARD(ORIGINAL(calloc)(count, size));
    size_t bytes; if (!calloc_size(count, size, &bytes, 0)) return leave_result(NULL, active_slot);
    reserve(bytes, 0); return leave_result(finish_allocate(ORIGINAL(calloc)(count, size), bytes), active_slot);
}
static void budget_free(void *pointer) {
    const size_t active_slot = enter_allocator();
    if (active_slot == ACTIVE_THREADS) { ORIGINAL(free)(pointer); return; }
    if (release(pointer, 0)) ORIGINAL(free)(pointer);
    leave_allocator(active_slot);
}
static void *budget_realloc(void *pointer, size_t bytes) {
    ENTER_OR_FORWARD(ORIGINAL(realloc)(pointer, bytes));
    struct realloc_state state = begin_realloc(pointer, bytes, 0);
    if (state.denied) { errno = ENOMEM; return leave_result(NULL, active_slot); }
    return leave_result(finish_realloc(pointer, ORIGINAL(realloc)(pointer, bytes), bytes, state), active_slot);
}
static void *budget_aligned_alloc(size_t alignment, size_t bytes) {
    ENTER_OR_FORWARD(ORIGINAL(aligned_alloc)(alignment, bytes));
    reserve(bytes, 0); return leave_result(finish_allocate(ORIGINAL(aligned_alloc)(alignment, bytes), bytes), active_slot);
}
static int budget_posix_memalign(void **out, size_t alignment, size_t bytes) {
    ENTER_OR_FORWARD(ORIGINAL(posix_memalign)(out, alignment, bytes));
    reserve(bytes, 0); void *pointer = NULL;
    int result = ORIGINAL(posix_memalign)(&pointer, alignment, bytes);
    finish_allocate(result == 0 ? pointer : NULL, bytes);
    if (result == 0) *out = pointer;
    leave_allocator(active_slot);
    return result;
}
static void *budget_valloc(size_t bytes) {
    ENTER_OR_FORWARD(ORIGINAL(valloc)(bytes));
    reserve(bytes, 0); return leave_result(finish_allocate(ORIGINAL(valloc)(bytes), bytes), active_slot);
}

#if defined(__APPLE__)
NIL_HEAP_INTERPOSE(budget_malloc, malloc)
NIL_HEAP_INTERPOSE(budget_calloc, calloc)
NIL_HEAP_INTERPOSE(budget_free, free)
NIL_HEAP_INTERPOSE(budget_realloc, realloc)
NIL_HEAP_INTERPOSE(budget_aligned_alloc, aligned_alloc)
NIL_HEAP_INTERPOSE(budget_posix_memalign, posix_memalign)
NIL_HEAP_INTERPOSE(budget_valloc, valloc)

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
/* Modern Apple Clang can rewrite ordinary calls to typed allocation symbols.
 * Intercept these actual entry points too, preserving the type argument. */
static void *budget_type_malloc(size_t bytes, malloc_type_id_t type) {
    ENTER_OR_FORWARD(malloc_type_malloc(bytes, type));
    reserve(bytes, 1); return leave_result(finish_allocate(malloc_type_malloc(bytes, type), bytes), active_slot);
}
static void *budget_type_calloc(size_t count, size_t size, malloc_type_id_t type) {
    ENTER_OR_FORWARD(malloc_type_calloc(count, size, type));
    size_t bytes; if (!calloc_size(count, size, &bytes, 1)) return leave_result(NULL, active_slot);
    reserve(bytes, 1); return leave_result(finish_allocate(malloc_type_calloc(count, size, type), bytes), active_slot);
}
static void budget_type_free(void *pointer, malloc_type_id_t type) {
    const size_t active_slot = enter_allocator();
    if (active_slot == ACTIVE_THREADS) { malloc_type_free(pointer, type); return; }
    if (release(pointer, 1)) malloc_type_free(pointer, type);
    leave_allocator(active_slot);
}
static void *budget_type_realloc(void *pointer, size_t bytes, malloc_type_id_t type) {
    ENTER_OR_FORWARD(malloc_type_realloc(pointer, bytes, type));
    struct realloc_state state = begin_realloc(pointer, bytes, 1);
    if (state.denied) { errno = ENOMEM; return leave_result(NULL, active_slot); }
    return leave_result(finish_realloc(pointer, malloc_type_realloc(pointer, bytes, type), bytes, state), active_slot);
}
static void *budget_type_aligned_alloc(size_t alignment, size_t bytes, malloc_type_id_t type) {
    ENTER_OR_FORWARD(malloc_type_aligned_alloc(alignment, bytes, type));
    reserve(bytes, 1); return leave_result(finish_allocate(malloc_type_aligned_alloc(alignment, bytes, type), bytes), active_slot);
}
static int budget_type_posix_memalign(void **out, size_t alignment, size_t bytes, malloc_type_id_t type) {
    ENTER_OR_FORWARD(malloc_type_posix_memalign(out, alignment, bytes, type));
    reserve(bytes, 1); void *pointer = NULL;
    int result = malloc_type_posix_memalign(&pointer, alignment, bytes, type);
    finish_allocate(result == 0 ? pointer : NULL, bytes);
    if (result == 0) *out = pointer;
    leave_allocator(active_slot);
    return result;
}
static void *budget_type_valloc(size_t bytes, malloc_type_id_t type) {
    ENTER_OR_FORWARD(malloc_type_valloc(bytes, type));
    reserve(bytes, 1); return leave_result(finish_allocate(malloc_type_valloc(bytes, type), bytes), active_slot);
}
NIL_HEAP_INTERPOSE(budget_type_malloc, malloc_type_malloc)
NIL_HEAP_INTERPOSE(budget_type_calloc, malloc_type_calloc)
NIL_HEAP_INTERPOSE(budget_type_free, malloc_type_free)
NIL_HEAP_INTERPOSE(budget_type_realloc, malloc_type_realloc)
NIL_HEAP_INTERPOSE(budget_type_aligned_alloc, malloc_type_aligned_alloc)
NIL_HEAP_INTERPOSE(budget_type_posix_memalign, malloc_type_posix_memalign)
NIL_HEAP_INTERPOSE(budget_type_valloc, malloc_type_valloc)
#endif
#else
void *__wrap_malloc(size_t bytes) { return budget_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t size) { return budget_calloc(count, size); }
void *__wrap_realloc(void *pointer, size_t bytes) { return budget_realloc(pointer, bytes); }
void __wrap_free(void *pointer) { budget_free(pointer); }
void *__wrap_aligned_alloc(size_t alignment, size_t bytes) { return budget_aligned_alloc(alignment, bytes); }
int __wrap_posix_memalign(void **out, size_t alignment, size_t bytes) { return budget_posix_memalign(out, alignment, bytes); }
void *__wrap_valloc(size_t bytes) { return budget_valloc(bytes); }
#endif

int nil_heap_budget_snapshot(struct nil_heap_snapshot *out, size_t size) {
    if (!ready || !out || size != sizeof(*out)) return 0;
    lock_tracker(); *out = stats; unlock_tracker(); return 1;
}
void nil_heap_budget_fail_next_realloc(void) {
    lock_tracker(); fail_realloc = 1; unlock_tracker();
}

__attribute__((constructor)) static void initialize_budget(void) {
    const char *path = getenv("NIL_DARWIN_HEAP_REPORT");
    const char *given_token = getenv("NIL_DARWIN_HEAP_TOKEN");
    const char *target = getenv("NIL_DARWIN_HEAP_TARGET");
    const char *mode = getenv("NIL_DARWIN_HEAP_WITHHOLD");
    uint64_t target_size;
    int withhold_mode;
    if (!path || !*path || !given_token || strlen(given_token) != 32 || !target || !mode)
        tracker_error("initialization-environment");
    for (size_t index = 0; index < 32; ++index) {
        char value = given_token[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
            tracker_error("initialization-token");
        token[index] = value;
    }
    if (strcmp(target, "16384") == 0) target_size = 16384;
    else if (strcmp(target, "65536") == 0) target_size = 65536;
    else if (strcmp(target, "65540") == 0) target_size = 65540;
    else tracker_error("initialization-target");
    if (strcmp(mode, "0") == 0) withhold_mode = 0;
    else if (strcmp(mode, "1") == 0) withhold_mode = 1;
    else tracker_error("initialization-withhold");
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) tracker_error("report-open");
    lock_tracker();
    stats.target_size = target_size; withhold = withhold_mode; report_fd = descriptor;
    ready = 1; emit_locked("ready"); unlock_tracker();
}
__attribute__((destructor)) static void finalize_budget(void) {
    if (!ready) return;
    lock_tracker(); emit_locked("final"); close(report_fd); report_fd = -1; unlock_tracker();
}

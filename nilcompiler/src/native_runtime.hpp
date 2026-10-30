#ifndef NY_COMPILER_NATIVE_RUNTIME_HPP
#define NY_COMPILER_NATIVE_RUNTIME_HPP

#include <cstdint>

// The NIL native runtime boundary is deliberately C-shaped. Generated objects
// must never depend on a C++ standard-library ABI, and JIT and AOT programs
// must call the same symbols with the same layouts.
extern "C" {

enum nyrt_array_kind : std::uint32_t {
  NYRT_ARRAY_I32 = 1,
  NYRT_ARRAY_I64 = 2,
  NYRT_ARRAY_BOOL = 3,
  NYRT_ARRAY_U8 = 4,
  NYRT_ARRAY_F64 = 5,
  NYRT_ARRAY_TEXT = 6,
  // Compiler-proven, fully initialized tuple object handles. The runtime
  // traces the handles; exact tuple schema identity remains in typed NIL.
  NYRT_ARRAY_TUPLE = 7,
  NYRT_ARRAY_ANYTHING = 8,
};

// Versioned UI ABI. Success is 1, failure 0. Text outputs are copied into
// managed context storage; toolkit pointers never cross this boundary.
std::int64_t nyrt_ui_v1_available() noexcept;
std::int32_t nyrt_ui_v1_window(const char *title, std::int64_t width, std::int64_t height,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_widget(const char *kind, std::int64_t parent, const char *label,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_grid_attach(std::int64_t grid, std::int64_t child, std::int64_t column, std::int64_t row, std::int64_t column_span, std::int64_t row_span,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_set_text(std::int64_t handle, const char *text,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_read_text(std::int64_t handle,
    const char **value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_set_number(std::int64_t handle, const char *property, std::int64_t number,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_read_number(std::int64_t handle, const char *property,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_activate(std::int64_t handle,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_poll(std::int64_t window,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_event_number(std::int64_t snapshot, const char *field,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_event_text(std::int64_t snapshot,
    const char **value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_release_event(std::int64_t snapshot,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_destroy_widget(std::int64_t handle,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_ui_v1_close(std::int64_t window,
    std::int64_t *value, const char **error) noexcept;

// Additive database ABI. Success is 1, failure 0; outputs are context-owned.
std::int64_t nyrt_db2_available(const char *driver) noexcept;
std::int32_t nyrt_db2_open(const char *driver, const char *address,
    std::int64_t timeout_ms, std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_db2_execute(std::int64_t connection, const char *sql,
    const void *parameters, const void *nulls, std::int64_t timeout_ms,
    std::int64_t *value, const char **error) noexcept;
std::int32_t nyrt_db2_read_i64(std::int64_t operation, std::int64_t handle,
    std::int64_t row, std::int64_t column, std::int64_t *value,
    const char **error) noexcept;
std::int32_t nyrt_db2_read_text(std::int64_t operation, std::int64_t handle,
    std::int64_t row, std::int64_t column, const char **value,
    const char **error) noexcept;

std::uint32_t nyrt_abi_version() noexcept;
std::int64_t nyrt_time_now_ms() noexcept;
void nyrt_sleep_ms(std::int64_t milliseconds) noexcept;
// Structured scalar workers never receive parent-context handles. Callback
// input/output words are copied bit patterns (including f64); the runtime
// joins every worker before returning a parent-owned typed result array.
using nyrt_parallel_worker_v1 = std::uint64_t (*)(std::uint64_t);
void *nyrt_parallel_map_v1(const void *inputs, std::uint32_t input_kind,
                          std::uint32_t output_kind,
                          nyrt_parallel_worker_v1 worker) noexcept;
void nyrt_parallel_fail_v1(const char *message) noexcept;
std::int32_t nyrt_parallel_failed_v1() noexcept;
void nyrt_context_begin(int argc, const char *const *argv) noexcept;
void nyrt_context_end() noexcept;
// Additive opt-in profiling attribution. Source strings are copied only during
// NIL_MEMORY_PROFILE captures; line zero/file empty mean unavailable.
void nyrt_profile_site(const char *file, const char *function,
                       std::uint64_t line) noexcept;
[[noreturn]] void nyrt_fail(const char *message) noexcept;
// Native text is NUL-free, byte-indexed UTF-8 storage. Substring clamps a
// negative start/count to zero and clips count to the remaining byte length.
// Its result uses context-owned storage and participates in managed roots.
const char *nyrt_text_substr(const char *text, std::int64_t start,
                           std::int64_t count) noexcept;
// Compiler formatting helpers return context-owned, locale-independent text.
const char *nyrt_text_i64(std::int64_t value) noexcept;
const char *nyrt_text_f64(double value) noexcept;
const char *nyrt_text_bool(std::int64_t value) noexcept;
std::int64_t nyrt_bool_i64(std::int64_t value) noexcept;
std::int64_t nyrt_bool_f64(double value) noexcept;
std::int64_t nyrt_bool_text(const char *value) noexcept;
// Immutable, context-owned dynamic values retain an exact canonical descriptor
// and trace any managed payload. Invalid handles and mismatched unboxing fault.
void *nyrt_any_v1_box(const char *type, std::uint64_t length,
                      std::uint64_t payload) noexcept;
std::uint64_t nyrt_any_v1_unbox(const void *box, const char *type,
                                std::uint64_t length) noexcept;
std::int32_t nyrt_any_v1_is_type(const void *box, const char *type,
                                std::uint64_t length) noexcept;
[[noreturn]] void nyrt_panic(const char *message) noexcept;

// Context-owned storage backs compiler-created language values (for example,
// text concatenations, structs, and enum variants) in safe native mode. It is
// traced from compiler-published roots at safe-mode execution safepoints.
// Legacy/unmanaged callers without root frames retain invocation ownership.
void *nyrt_alloc(std::uint64_t size) noexcept;
std::int32_t nyrt_free(void *pointer) noexcept;

// Each address names an initialized, live eight-byte word. Frames are LIFO
// and remain registered until the generated function returns. This internal
// ABI is for compiler/embedding code, not arbitrary Ny pointer values.
void nyrt_roots_enter(const void *const *slots, std::uint64_t count) noexcept;
void nyrt_roots_leave(const void *const *slots) noexcept;
void nyrt_safepoint() noexcept;
void nyrt_collect() noexcept;
std::uint64_t nyrt_managed_bytes() noexcept;
std::uint64_t nyrt_managed_objects() noexcept;

// Versioned, compiler-internal synchronous fault protocol. Names are exact byte
// spans (not NUL-terminated identities). Payloads must already be proven to be
// fully initialized objects of that exact concrete struct schema. These helpers
// never dereference or take ownership of the payload; unmanaged callers retain
// their manual lifetime obligations. Managed pending/saved payloads are GC roots.
// A separate LIFO frame stack makes saved-slot identities recursion-safe even
// when generated code uses --unmanaged and has no managed root frame.
std::uint64_t nyrt_fault_v1_frame_enter() noexcept;
void nyrt_fault_v1_frame_leave(std::uint64_t frame) noexcept;
std::uint64_t nyrt_fault_v1_saved_depth() noexcept;
void nyrt_fault_v1_unwind(const std::uint64_t *saved_slots,
                         std::uint64_t saved_count) noexcept;
std::int32_t nyrt_fault_v1_pending() noexcept;
void nyrt_fault_v1_raise(const char *type, std::uint64_t type_length,
                         void *payload, std::int64_t line) noexcept;
void nyrt_fault_v1_save(std::uint64_t slot) noexcept;
std::int32_t nyrt_fault_v1_match(std::uint64_t slot, const char *type,
                                std::uint64_t type_length) noexcept;
void *nyrt_fault_v1_bind(std::uint64_t slot, const char *type,
                        std::uint64_t type_length) noexcept;
void nyrt_fault_v1_drop(std::uint64_t slot) noexcept;
void nyrt_fault_v1_resume(std::uint64_t slot, std::int32_t consume) noexcept;
void nyrt_fault_v1_report_uncaught() noexcept;

std::int64_t nyrt_arg_count() noexcept;
const char *nyrt_arg(std::int64_t index) noexcept;
[[noreturn]] void nyrt_exit(std::int64_t code) noexcept;

void nyrt_write_stdout(const char *text, std::int32_t newline) noexcept;
void nyrt_write_stderr(const char *text, std::int32_t newline) noexcept;
// Reads through LF, retaining any CR and accepting a final unterminated line.
// The result is managed text; EOF, host errors and embedded NUL fail explicitly.
const char *nyrt_read_line() noexcept;
void nyrt_flush_stdout() noexcept;

// NILData v1 returns managed array<text> [error, payload]. An empty error
// denotes success; no process-global document handles or borrowed text escape.
void *nyrt_nildata_v1_request(const char *operation, const char *source,
                              const char *argument, std::int64_t index) noexcept;

// Regex results are [status, start0, length0, ...], with absent captures -1/-1.
// Status: 0 no match, 1 match, 2 pattern, 3 offset, 4 budget, 5 engine error.
const char *nyrt_regex_validate(const char *pattern) noexcept;
void *nyrt_regex_search(const char *pattern, const char *input,
                       std::int64_t start, std::int64_t full) noexcept;
std::int64_t nyrt_regex_group_index(const char *pattern, const char *name) noexcept;

void *nyrt_array_new(std::uint32_t kind, std::int64_t length) noexcept;
void *nyrt_array_new_with_value(std::uint32_t kind, std::int64_t length,
                                std::uint64_t value) noexcept;
std::int64_t nyrt_array_length(const void *array,
                               std::uint32_t expected_kind) noexcept;
std::uint64_t nyrt_array_get(const void *array, std::uint32_t expected_kind,
                             std::int64_t index) noexcept;
std::int32_t nyrt_array_set(void *array, std::uint32_t expected_kind,
                            std::int64_t index, std::uint64_t value) noexcept;
std::int32_t nyrt_array_push(void *array, std::uint32_t expected_kind,
                             std::uint64_t value) noexcept;

// Raw Ny pointers expose the buffer's packed data address for C ABI use while
// safe-mode operations validate that address against context-owned metadata.
// Element widths are stable: i32=4, i64=8, bool=1, and u8=1 byte.
void *nyrt_buffer_new(std::uint32_t kind, std::int64_t length) noexcept;
std::int32_t nyrt_buffer_free(void *buffer) noexcept;
// Compiler-internal affine release: unlike raw FREE, a proven nonescaping
// owner needs no stale-pointer tombstone and its storage is physically freed.
std::int32_t nyrt_owned_buffer_release(void *buffer) noexcept;
void *nyrt_buffer_offset(void *buffer, std::uint32_t expected_kind,
                         std::int64_t amount, std::int32_t subtract) noexcept;
std::uint64_t nyrt_buffer_get(const void *buffer, std::uint32_t expected_kind,
                              std::int64_t index) noexcept;
std::int32_t nyrt_buffer_set(void *buffer, std::uint32_t expected_kind,
                             std::int64_t index, std::uint64_t value) noexcept;

std::int32_t nyrt_read_regular_bytes(const char *path,
                                     std::int64_t maximum_bytes,
                                     void **bytes_out,
                                     const char **error_out) noexcept;
std::int32_t nyrt_write_bytes_atomic(const char *path, const void *bytes,
                                     std::int32_t replace,
                                     const char **error_out) noexcept;

std::int32_t nyrt_try_i32(const char *text, std::int32_t *value_out,
                          const char **error_out) noexcept;
// Additive conversion ABI. Native text is NUL-free; failures clear value_out
// and return a context-owned error using the same lifetime as nyrt_try_i32.
std::int32_t nyrt_try_i64(const char *text, std::int64_t *value_out,
                          const char **error_out) noexcept;
const char *nyrt_i32_to_text(std::int32_t value) noexcept;
const char *nyrt_i64_to_text(std::int64_t value) noexcept;

} // extern "C"

#endif

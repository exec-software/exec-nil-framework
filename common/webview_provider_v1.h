/* Checked WebView provider ABI, version 1.
 * Copyright (C) 2026 Execute Software. SPDX-License-Identifier: MIT
 */
#ifndef NILWEBVIEW_PROVIDER_V1_H
#define NILWEBVIEW_PROVIDER_V1_H
#include <stdint.h>

#if defined(_WIN32)
#define NILWV_CALL __cdecl
#else
#define NILWV_CALL
#endif
#if defined(_WIN32) && defined(NILWEBVIEW_PROVIDER_BUILD)
#define NILWV_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && defined(NILWEBVIEW_PROVIDER_BUILD)
#define NILWV_EXPORT __attribute__((visibility("default")))
#else
#define NILWV_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NILWV_ABI_V1 UINT32_C(1)
#define NILWV_MAX_TEXT_BYTES UINT64_C(1048576)
#define NILWV_MAX_QUEUED_BYTES UINT64_C(8388608)
#define NILWV_MAX_EVENTS UINT32_C(256)
#define NILWV_MAX_WINDOWS UINT32_C(16)
#define NILWV_OPERATION_TIMEOUT_MS UINT32_C(30000)

/* Capability bits describe native sender metadata, never page assertions.
 * A valid GTK6 provider may advertise zero. These bits do not prove that a
 * display or browser runtime can initialize, or apply to every URL scheme. */
#define NILWV_CAP_MESSAGE_URL UINT64_C(1)
#define NILWV_CAP_MESSAGE_ORIGIN UINT64_C(2)
#define NILWV_CAP_MESSAGE_FRAME UINT64_C(4)
#define NILWV_CAP_KNOWN_MASK UINT64_C(7)

typedef struct nilwv_context_v1 nilwv_context_v1;
typedef struct nilwv_text_v1 {
    const char *data;
    uint64_t size;
} nilwv_text_v1;

/* Fields are fixed-width; C enums below name values but never cross the ABI.
 * UTF-8 is validated, with no embedded NUL. {NULL,0} is the empty string.
 * Inputs are borrowed for the call; asynchronous operations copy them.
 * Every returned text view is provider-owned until the next call using this
 * context, or context destruction. The framework must copy it immediately.
 * Error text uses the same rule; probe/create errors survive the next call
 * on their owner thread. No allocator or C++ type crosses this boundary. */
typedef struct nilwv_result_v1 {
    uint32_t size;              /* caller initializes to sizeof(result) */
    int32_t code;               /* 0 success; negative error code */
    int64_t number;             /* numeric result; zero when unused */
    nilwv_text_v1 text;         /* text result, or diagnostic if code < 0 */
} nilwv_result_v1;

enum nilwv_error_code_v1 {
    NILWV_E_UNAVAILABLE = -1,
    NILWV_E_ARGUMENT = -2,
    NILWV_E_HANDLE = -3,
    NILWV_E_THREAD = -4,
    NILWV_E_NOT_READY = -5,
    NILWV_E_BUSY = -6,
    NILWV_E_LIMIT = -7,
    NILWV_E_NATIVE = -8,
    NILWV_E_TIMEOUT = -9,
    NILWV_E_PROTOCOL = -10,
    NILWV_E_QUEUE_OVERFLOW = -11
};

enum nilwv_event_kind_v1 {
    NILWV_EVENT_READY = 1,
    NILWV_EVENT_INITIALIZATION_FAILED = 2,
    NILWV_EVENT_NAVIGATION_COMPLETED = 3,
    NILWV_EVENT_NAVIGATION_FAILED = 4,
    NILWV_EVENT_SCRIPT_COMPLETED = 5,
    NILWV_EVENT_SCRIPT_FAILED = 6,
    NILWV_EVENT_PAGE_MESSAGE = 7,
    NILWV_EVENT_NAVIGATION_REQUESTED = 8,
    NILWV_EVENT_CLOSE_REQUESTED = 9,
    NILWV_EVENT_PROVIDER_FAILED = 10
};
enum nilwv_value_kind_v1 {
    NILWV_VALUE_NONE = 0, NILWV_VALUE_JSON = 1, NILWV_VALUE_UNDEFINED = 2
};
enum nilwv_source_kind_v1 {
    NILWV_SOURCE_UNKNOWN = 0,
    NILWV_SOURCE_NATIVE_URL = 1,
    NILWV_SOURCE_NATIVE_ORIGIN = 2
};
enum nilwv_frame_kind_v1 {
    NILWV_FRAME_UNKNOWN = 0, NILWV_FRAME_MAIN = 1, NILWV_FRAME_CHILD = 2
};
enum nilwv_failure_kind_v1 {
    NILWV_FAILURE_NONE = 0,
    NILWV_FAILURE_NATIVE = 1,
    NILWV_FAILURE_SCRIPT_EXCEPTION = 2,
    NILWV_FAILURE_SERIALIZATION = 3,
    NILWV_FAILURE_ASYNC_RESULT = 4,
    NILWV_FAILURE_PAYLOAD_LIMIT = 5,
    NILWV_FAILURE_TIMEOUT = 6,
    NILWV_FAILURE_CANCELLED = 7
};

/* One context per framework execution context, owned by its platform UI
 * thread: the process main thread on macOS/Linux, the creating STA thread on
 * Windows. All methods, including release and destruction, use that thread.
 * Public numeric handles are positive, process-unique, never reused, <=INT64_MAX,
 * and validated against context, resource kind and lifetime on every call.
 * No callback from a provider enters Matrix or the VM.
 *
 * probe: no window/display initialization; result.number=1 on success.
 * create: returns a context or NULL on error; profile state is provider-owned
 * and isolated from personal browser profiles. Neither probe nor create starts
 * a global event loop. window: returns a pending hidden window, then poll emits
 * exactly one Ready/InitializationFailed unless explicitly closed first.
 *
 * show/close/release_event: number=1. navigate/load_html/evaluate_function:
 * positive operation ID, then one matching completion/failure event. Reject
 * overlapping navigation/script operations with BUSY; do not queue another.
 * navigate authorizes one initial URL request. HTTP redirects and uninitiated
 * navigations (including page actions/new windows) are cancelled and reported
 * as NavigationRequested; a cancelled active navigation completes as failed.
 * After that completion the application may explicitly follow a requested URL.
 * load_html has no base URI; evaluate_function accepts a synchronous function
 * body with explicit return, JSON.stringify result semantics, distinct undefined,
 * and explicit failure for thenables or unsuccessful serialization.
 *
 * poll pumps a bounded batch (up to 64 native dispatch iterations, 2ms soft budget) and returns an
 * owned immutable event snapshot ID, or 0 for empty. Retained snapshots count
 * toward queue limits. The framework copies all fields then releases on every
 * path. Overflow latches QUEUE_OVERFLOW; operations fail until context teardown.
 * No messages or completion events are silently dropped to recover capacity.
 *
 * event_number names: window, kind, sequence, operation, value_kind,
 * source_kind, frame_kind, failure_kind. operation=0 means no initiating call.
 * event_text names: payload_json, source_url, source_origin, detail.
 * source_url/origin are empty when unavailable; browser current URL and
 * JavaScript-supplied URL/origin values must not be represented as sender proof.
 * source_origin is only a native engine-reported origin, not parsed from a URL.
 * Unknown event property names are ARGUMENT errors, not empty/zero results.
 *
 * close invalidates the window, its queued/retained events and pending operations.
 * Closing again is HANDLE error. Teardown invalidates all context resources.
 * Outstanding engine callbacks retain only independent cancelled state, never
 * a raw freed context/window pointer. Late callbacks cannot publish new events.
 * Browser processes may finish asynchronously after object closure; the provider
 * remains responsible for its own isolated profile/cleanup state. No claim is
 * made that cancelling a script stops executing JavaScript.
 */
typedef struct nilwv_api_v1 {
    uint32_t abi_version;
    uint32_t size;
    uint64_t capabilities;
    const char *provider_name;  /* static NUL-terminated UTF-8 */
    const char *provider_build; /* static NUL-terminated UTF-8 */

    void (NILWV_CALL *probe)(nilwv_result_v1 *);
    void (NILWV_CALL *context_create)(nilwv_context_v1 **, nilwv_result_v1 *);
    void (NILWV_CALL *context_destroy)(nilwv_context_v1 *);
    void (NILWV_CALL *window)(nilwv_context_v1 *, nilwv_text_v1, int64_t, int64_t, nilwv_result_v1 *);
    void (NILWV_CALL *show)(nilwv_context_v1 *, int64_t, nilwv_result_v1 *);
    void (NILWV_CALL *navigate)(nilwv_context_v1 *, int64_t, nilwv_text_v1, nilwv_result_v1 *);
    void (NILWV_CALL *load_html)(nilwv_context_v1 *, int64_t, nilwv_text_v1, nilwv_result_v1 *);
    void (NILWV_CALL *evaluate_function)(nilwv_context_v1 *, int64_t, nilwv_text_v1, nilwv_result_v1 *);
    void (NILWV_CALL *poll)(nilwv_context_v1 *, int64_t, nilwv_result_v1 *);
    void (NILWV_CALL *event_number)(nilwv_context_v1 *, int64_t, nilwv_text_v1, nilwv_result_v1 *);
    void (NILWV_CALL *event_text)(nilwv_context_v1 *, int64_t, nilwv_text_v1, nilwv_result_v1 *);
    void (NILWV_CALL *release_event)(nilwv_context_v1 *, int64_t, nilwv_result_v1 *);
    void (NILWV_CALL *close)(nilwv_context_v1 *, int64_t, nilwv_result_v1 *);
} nilwv_api_v1;

typedef const nilwv_api_v1 *(NILWV_CALL *nilwv_get_api_v1_fn)(uint32_t, uint32_t);

/* Exactly this exported C name; NULL rejects unsupported ABI/table size.
 * The table and function pointers remain stable for process lifetime.
 * Runtime loads only a caller-selected absolute provider path, validates every
 * mandatory pointer and the exact ABI, calls probe, then creates its context.
 * The runtime never unloads or hot-reloads a successfully loaded provider.
 * No provider call may allow a C++ exception to cross this boundary.
 */
NILWV_EXPORT const nilwv_api_v1 *NILWV_CALL
nilwebview_provider_v1_get_api(uint32_t requested_version, uint32_t minimum_table_size);

#ifdef __cplusplus
}
#endif
#endif

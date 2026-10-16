#ifndef NY_COMPILER_MATRIX_HAG_BRIDGE_HPP
#define NY_COMPILER_MATRIX_HAG_BRIDGE_HPP

// matrix_hag_bridge.hpp
// A dynamically loaded bridge from the NIL runtime to HAG's C ABI.
//
// This is what lets a Matrix program issue GPU workloads: the VM's
// INTEROP_CALL handler resolves its own pointers to host buffers, then calls
// through here.
//
// WHY DYNAMIC LOADING
//
// The library is opened with dlopen at first use rather than linked. Three
// reasons, and the first is the important one:
//
//   1. The NIL runtime must not acquire a build dependency on a GPU runtime.
//      One nilinterpreter binary runs on machines with and without HAG, and a
//      machine without it must still run every program that does not ask for
//      a GPU.
//   2. It matches HAG's own posture: refuse rather than silently substitute.
//      If the library is absent the bridge says so; it never falls back to a
//      software path the caller did not ask for.
//   3. The bridge is inert until something explicitly reaches for it, which
//      keeps the reachable surface of an ordinary program unchanged.
//
// The library is found through NY_HAG_LIBRARY when set, otherwise by the
// platform's ordinary search for "libexec_hag.so". Nothing here searches the
// filesystem on its own.

#include <cstdint>
#include <string>

namespace matrix_hag {

// Status codes mirroring exec_hag.h. Negative values are failures; the bridge
// adds its own below HAG's range so a caller can tell "HAG refused" from "the
// bridge could not reach HAG".
constexpr int hag_ok = 0;
constexpr int bridge_unavailable = -100;  // library missing or symbols absent
constexpr int bridge_bad_pointer = -101;  // VM pointer did not resolve
constexpr int bridge_out_of_bounds = -102; // region outside the VM allocation
constexpr int bridge_not_byte_backed = -103; // handle holds values, not bytes

// True when the library is present and every required symbol resolved.
// Attempts the load exactly once; afterwards returns the cached result.
bool available();

// Why `available()` returned false, for a diagnostic. Empty when available.
const std::string &unavailable_reason();

// A borrowed description of a status code, from HAG where it owns the code and
// from the bridge where it does not.
std::string status_text(int code);

// Thin typed wrappers. Each returns a HAG status, or a bridge status when the
// library is unavailable. `handle` is HAG's opaque pointer as an integer so it
// can live in a VM value.
int queue_open(const std::string &device_path, const unsigned char *program,
               std::uint32_t program_bytes, std::uint32_t slot0,
               std::uint32_t slot1, std::uint32_t slot2, std::uint32_t vgprs,
               std::uint32_t max_groups, std::uint64_t budget_bytes,
               std::int64_t *out_handle);
int queue_write(std::int64_t handle, std::uint32_t slot, std::uint64_t offset,
                const unsigned char *bytes, std::uint64_t length);
int queue_read(std::int64_t handle, std::uint32_t slot, std::uint64_t offset,
               unsigned char *bytes, std::uint64_t length);
int queue_dispatch(std::int64_t handle, std::uint32_t program,
                   const std::uint64_t offsets[3], const std::uint32_t lengths[3],
                   const std::uint32_t scalars[2], std::uint32_t groups);
std::uint64_t queue_allocation_bytes(std::int64_t handle);
int queue_is_poisoned(std::int64_t handle);
int queue_close(std::int64_t handle);
std::uint32_t min_kernel_vgprs();
std::uint32_t max_kernel_vgprs();
std::uint32_t max_dispatch_groups();

} // namespace matrix_hag

#endif // NY_COMPILER_MATRIX_HAG_BRIDGE_HPP

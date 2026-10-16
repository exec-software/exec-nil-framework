#ifndef NY_COMPILER_OWNED_RUNTIME_HPP
#define NY_COMPILER_OWNED_RUNTIME_HPP
#include <cstdint>

// Internal C ABI: slot identities are local-storage addresses, never source
// names or per-function integer indices (which collide during recursion).
extern "C" {
std::uint64_t nyrt_owned_v1_depth() noexcept;
void nyrt_owned_v1_enter() noexcept;
void nyrt_owned_v1_bind(std::uint64_t slot, void *buffer) noexcept;
void *nyrt_owned_v1_take(std::uint64_t slot) noexcept;
void nyrt_owned_v1_leave() noexcept;
void nyrt_owned_v1_unwind(std::uint64_t depth) noexcept;
// Test/embedding observations, not language pointers or mutable runtime access.
std::uint64_t nyrt_owned_v1_live() noexcept;
std::uint64_t nyrt_owned_v1_drops() noexcept;
}
#endif

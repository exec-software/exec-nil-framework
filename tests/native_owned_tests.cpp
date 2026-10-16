#include "native_runtime.hpp"
#include "owned_runtime.hpp"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void *buffer(std::uint32_t kind = NYRT_ARRAY_I32) {
  auto *value = nyrt_buffer_new(kind, 4);
  require(value != nullptr, "allocation failed");
  return value;
}
std::uint64_t identity(void **slot) {
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(slot));
}
void normal_and_moves() {
  nyrt_context_begin(0, nullptr);
  const auto before = nyrt_owned_v1_drops();
  void *first = buffer();
  nyrt_owned_v1_enter();
  nyrt_owned_v1_bind(identity(&first), first);
  require(nyrt_owned_v1_live() == 1, "initial owner missing");
  nyrt_owned_v1_enter();
  void *second = nyrt_owned_v1_take(identity(&first));
  first = nullptr;
  nyrt_owned_v1_bind(identity(&second), second);
  require(nyrt_owned_v1_live() == 1 && nyrt_owned_v1_drops() == before,
          "moving dropped or duplicated an owner");
  require(nyrt_buffer_set(second, NYRT_ARRAY_I32, 0, 53), "moved buffer invalid");
  require(nyrt_buffer_get(second, NYRT_ARRAY_I32, 0) == 53, "move lost contents");
  nyrt_owned_v1_leave();
  require(nyrt_owned_v1_live() == 0 && nyrt_owned_v1_drops() == before + 1,
          "lexical exit did not drop exactly once");
  require(!nyrt_buffer_free(second), "owned buffer survived its scope");
  nyrt_owned_v1_leave();
  require(nyrt_owned_v1_drops() == before + 1, "source scope double-dropped moved buffer");
  nyrt_context_end();
}
void partial_and_full_unwind() {
  nyrt_context_begin(0, nullptr);
  const auto before = nyrt_owned_v1_drops();
  nyrt_owned_v1_enter();
  void *outer = buffer(NYRT_ARRAY_I64);
  nyrt_owned_v1_bind(identity(&outer), outer);
  const auto caught_depth = nyrt_owned_v1_depth();
  nyrt_owned_v1_enter();
  void *inner = buffer(NYRT_ARRAY_U8);
  nyrt_owned_v1_bind(identity(&inner), inner);
  nyrt_owned_v1_enter();
  void *nested = buffer();
  nyrt_owned_v1_bind(identity(&nested), nested);
  nyrt_owned_v1_unwind(caught_depth);
  require(nyrt_owned_v1_live() == 1 && nyrt_owned_v1_drops() == before + 2,
          "caught fault unwind did not preserve outer owner and drop inner owners");
  require(nyrt_buffer_set(outer, NYRT_ARRAY_I64, 0, 59), "outer owner died during unwind");
  require(!nyrt_buffer_free(inner) && !nyrt_buffer_free(nested), "unwound buffers remain live");
  nyrt_owned_v1_unwind(0);
  require(nyrt_owned_v1_live() == 0 && nyrt_owned_v1_drops() == before + 3,
          "propagated fault/return unwind leaked owner");
  nyrt_context_end();
}
void context_teardown() {
  nyrt_context_begin(0, nullptr);
  const auto before = nyrt_owned_v1_drops();
  nyrt_owned_v1_enter();
  void *value = buffer();
  nyrt_owned_v1_bind(identity(&value), value);
  nyrt_context_end();
  require(nyrt_owned_v1_depth() == 0 && nyrt_owned_v1_live() == 0 &&
          nyrt_owned_v1_drops() == before + 1, "context teardown left stale ownership state");
}
void independent_threads() {
  nyrt_context_begin(0, nullptr);
  nyrt_owned_v1_enter();
  void *parent = buffer();
  nyrt_owned_v1_bind(identity(&parent), parent);
  std::atomic<unsigned> complete{0};
  const auto worker = [&complete] {
    nyrt_context_begin(0, nullptr);
    const auto before = nyrt_owned_v1_drops();
    nyrt_owned_v1_enter();
    void *local = buffer();
    nyrt_owned_v1_bind(identity(&local), local);
    const bool isolated = nyrt_owned_v1_live() == 1;
    nyrt_owned_v1_leave();
    if (isolated && nyrt_owned_v1_live() == 0 && nyrt_owned_v1_drops() == before + 1)
      ++complete;
    nyrt_context_end();
  };
  std::thread first(worker), second(worker);
  first.join(); second.join();
  require(complete == 2 && nyrt_owned_v1_live() == 1,
          "thread contexts shared ownership state");
  nyrt_owned_v1_leave();
  nyrt_context_end();
}
}
int main() {
  try {
    normal_and_moves(); partial_and_full_unwind(); context_teardown(); independent_threads();
    std::cout << "native affine lifetime, move, unwind, teardown, and isolation checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

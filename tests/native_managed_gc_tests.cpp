#include "native_runtime.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class context_scope {
public:
  context_scope() { nyrt_context_begin(0, nullptr); }
  ~context_scope() { nyrt_context_end(); }
  context_scope(const context_scope &) = delete;
  context_scope &operator=(const context_scope &) = delete;
};

// Every registered slot is an initialized eight-byte word, just like the
// compiler's shadow roots. Ordinary C++ local pointers are deliberately NOT
// roots: collection must neither scan this test's stack nor guess liveness.
class root_frame {
public:
  explicit root_frame(std::initializer_list<const void *> slots)
      : slots_(slots), count_(slots.size()) {
    if (slots_.empty())
      slots_.push_back(nullptr); // Valid frame identity, with zero scanned slots.
    nyrt_roots_enter(slots_.data(), count_);
  }
  ~root_frame() { nyrt_roots_leave(slots_.data()); }
  root_frame(const root_frame &) = delete;
  root_frame &operator=(const root_frame &) = delete;

private:
  std::vector<const void *> slots_;
  std::uint64_t count_;
};

std::uint64_t address_bits(const void *pointer) {
  static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

void *allocate(std::uint64_t size) {
  void *result = nyrt_alloc(size);
  require(result != nullptr, "managed allocation failed");
  return result;
}

void write_word(void *object, std::size_t offset, std::uint64_t value) {
  std::memcpy(static_cast<unsigned char *>(object) + offset, &value, sizeof(value));
}

void require_empty(const char *message) {
  require(nyrt_managed_objects() == 0 && nyrt_managed_bytes() == 0, message);
}

void legacy_callers_retain_invocation_lifetime() {
  context_scope context;
  void *object = allocate(64);
  void *array = nyrt_array_new_with_value(NYRT_ARRAY_I64, 2, 73);
  require(array != nullptr, "legacy array allocation failed");
  write_word(object, 0, 19);
  const auto objects = nyrt_managed_objects();
  const auto bytes = nyrt_managed_bytes();
  nyrt_collect();
  require(nyrt_managed_objects() == objects && nyrt_managed_bytes() == bytes,
          "collection reclaimed uninstrumented legacy caller storage");

  // Cross the automatic threshold too: absence of frames is intentionally not
  // equivalent to an instrumented frame with no live roots.
  allocate(5ULL * 1024ULL * 1024ULL);
  const auto large_bytes = nyrt_managed_bytes();
  nyrt_safepoint();
  require(nyrt_managed_bytes() == large_bytes &&
              nyrt_array_get(array, NYRT_ARRAY_I64, 1) == 73,
          "legacy safepoint did not preserve invocation ownership");
  require(nyrt_free(object) == 1, "legacy explicit free failed");
}

void alias_nested_and_cyclic_graphs() {
  context_scope context;
  std::uint64_t primary = 0;
  std::uint64_t alias = 0;
  root_frame frame{&primary, &alias};
  void *parent = allocate(16);
  void *child = allocate(16);
  void *left_leaf = allocate(16);
  void *right_leaf = allocate(16);
  write_word(parent, 0, address_bits(child));
  write_word(parent, 8, address_bits(left_leaf));
  write_word(child, 0, address_bits(parent)); // A real tracing cycle.
  write_word(child, 8, address_bits(right_leaf));
  write_word(left_leaf, 0, 117);
  write_word(right_leaf, 0, 223);
  primary = address_bits(parent);
  alias = primary;

  nyrt_collect();
  require(nyrt_managed_objects() == 4 && nyrt_managed_bytes() == 64,
          "nested or cyclic managed graph was not retained");
  primary = 0;
  nyrt_collect();
  require(nyrt_managed_objects() == 4,
          "clearing one alias reclaimed a still-rooted graph");
  std::uint64_t value = 0;
  std::memcpy(&value, right_leaf, sizeof(value));
  require(value == 223, "rooted nested graph contents changed");
  alias = 0;
  nyrt_collect();
  require_empty("unreachable cycle or its leaves were retained");
}

void text_arrays_trace_managed_elements() {
  context_scope context;
  std::uint64_t root = 0;
  root_frame frame{&root};
  void *owner = allocate(16);
  void *array = nyrt_array_new(NYRT_ARRAY_TEXT, 0);
  require(array != nullptr, "text array allocation failed");
  const char *first = nyrt_i64_to_text(123456789);
  const char *second = nyrt_i64_to_text(-987654321);
  require(first != nullptr && second != nullptr, "managed text allocation failed");
  require(nyrt_array_push(array, NYRT_ARRAY_TEXT, address_bits(first)) == 1 &&
              nyrt_array_push(array, NYRT_ARRAY_TEXT, address_bits(second)) == 1,
          "text array population failed");
  write_word(owner, 0, address_bits(array));
  root = address_bits(owner);

  nyrt_collect();
  require(nyrt_managed_objects() == 4,
          "owner-to-array-to-text graph did not retain every object");
  require(nyrt_array_get(array, NYRT_ARRAY_TEXT, 0) == address_bits(first) &&
              nyrt_array_get(array, NYRT_ARRAY_TEXT, 1) == address_bits(second) &&
              std::strcmp(first, "123456789") == 0 &&
              std::strcmp(second, "-987654321") == 0,
          "traced text array lost its element values");
  root = 0;
  nyrt_collect();
  require_empty("unreachable text array graph was retained");
  // Query only through validation APIs; never dereference a collected handle.
  require(nyrt_array_length(array, NYRT_ARRAY_TEXT) == -1,
          "collected text array handle was accepted");
}

void interior_roots_preserve_the_owning_block() {
  context_scope context;
  std::uint64_t root = 0;
  root_frame frame{&root};
  auto *object = static_cast<unsigned char *>(allocate(33));
  object[17] = 79;
  object[32] = 91;
  root = address_bits(object + 17); // Deliberately not word-aligned.
  nyrt_collect();
  require(nyrt_managed_objects() == 1 && nyrt_managed_bytes() == 33 &&
              object[17] == 79,
          "interior pointer did not root its owned allocation");
  root = address_bits(object + 32); // Last in-bounds byte, not one-past.
  nyrt_collect();
  require(nyrt_managed_objects() == 1 && object[32] == 91,
          "last-byte interior pointer did not retain its owner");
  root = 0;
  nyrt_collect();
  require_empty("cleared interior root retained an allocation");
}

void nested_frames_and_empty_instrumented_frames() {
  context_scope context;
  std::uint64_t outer = address_bits(allocate(32));
  root_frame outer_frame{&outer};
  {
    std::uint64_t inner = address_bits(allocate(48));
    root_frame inner_frame{&inner};
    nyrt_collect();
    require(nyrt_managed_objects() == 2 && nyrt_managed_bytes() == 80,
            "nested root frames did not preserve both invocations");
  }
  nyrt_collect();
  require(nyrt_managed_objects() == 1 && nyrt_managed_bytes() == 32,
          "leaving inner frame retained its object or lost outer roots");
  outer = 0;
  {
    root_frame empty_frame{};
    allocate(8);
    nyrt_collect();
    require_empty("zero-root instrumented frame disabled reclamation");
  }
}

void stale_and_unowned_handles_fail_without_dereference() {
  context_scope context;
  std::uint64_t unrelated = std::numeric_limits<std::uint64_t>::max();
  root_frame frame{&unrelated};
  void *array = nyrt_array_new(NYRT_ARRAY_U8, 8);
  require(array != nullptr, "stale-handle fixture allocation failed");
  void *owned = allocate(16);
  nyrt_collect();
  require_empty("unowned numeric root retained objects");
  for (void *handle : {array, reinterpret_cast<void *>(std::uintptr_t{1})}) {
    require(nyrt_array_length(handle, NYRT_ARRAY_U8) == -1 &&
                nyrt_array_get(handle, NYRT_ARRAY_U8, 0) == 0 &&
                nyrt_array_set(handle, NYRT_ARRAY_U8, 0, 1) == 0 &&
                nyrt_array_push(handle, NYRT_ARRAY_U8, 1) == 0,
            "stale or unowned array handle did not fail closed");
  }
  require(nyrt_free(owned) == 0, "collected owned block was freed twice");
  unrelated = 1;
  nyrt_collect();
  require_empty("invalid low address was followed during tracing");
}

void transient_allocations_are_bounded_at_safepoints() {
  context_scope context;
  std::uint64_t current = 0;
  root_frame frame{&current};
  constexpr std::uint64_t allocation_bytes = 4096;
  constexpr std::uint64_t threshold = 4ULL * 1024ULL * 1024ULL;
  for (std::uint64_t iteration = 0; iteration < 10000; ++iteration) {
    void *object = allocate(allocation_bytes);
    write_word(object, 0, iteration);
    current = address_bits(object);
    nyrt_safepoint();
    require(nyrt_managed_bytes() <= threshold + allocation_bytes,
            "transient owned allocations grew without safepoint reclamation");
    std::uint64_t value = 0;
    std::memcpy(&value, object, sizeof(value));
    require(value == iteration, "safepoint reclaimed the current live object");
  }
  nyrt_collect();
  require(nyrt_managed_objects() == 1 &&
              nyrt_managed_bytes() == allocation_bytes,
          "explicit collection retained old transient allocations");
  current = 0;
  nyrt_collect();
  require_empty("final transient root was not reclaimed");
}

void array_growth_counts_toward_safepoint_pressure() {
  context_scope context;
  std::uint64_t current = 0;
  root_frame frame{&current};
  constexpr std::uint64_t limit = 5ULL * 1024ULL * 1024ULL;
  for (std::uint64_t iteration = 0; iteration < 80; ++iteration) {
    void *array = nyrt_array_new(NYRT_ARRAY_I64, 0);
    require(array != nullptr, "growing array allocation failed");
    current = address_bits(array);
    for (std::uint64_t element = 0; element < 8193; ++element)
      require(nyrt_array_push(array, NYRT_ARRAY_I64, element) == 1,
              "growing array push failed");
    nyrt_safepoint();
    require(nyrt_managed_bytes() <= limit,
            "array capacity growth bypassed safepoint allocation accounting");
    require(nyrt_array_get(array, NYRT_ARRAY_I64, 8192) == 8192,
            "array growth safepoint lost the rooted descriptor or contents");
  }
  nyrt_collect();
  require(nyrt_managed_objects() == 1 && nyrt_managed_bytes() < 256 * 1024,
          "explicit collection retained discarded grown arrays");
  current = 0;
  nyrt_collect();
  require_empty("grown array was not reclaimed after clearing its root");
}

} // namespace

int main() {
  struct test_case {
    const char *name;
    void (*run)();
  };
  const test_case tests[] = {
      {"legacy invocation ownership", legacy_callers_retain_invocation_lifetime},
      {"aliases, nesting, and cycles", alias_nested_and_cyclic_graphs},
      {"managed text array elements", text_arrays_trace_managed_elements},
      {"interior roots", interior_roots_preserve_the_owning_block},
      {"nested and empty frames", nested_frames_and_empty_instrumented_frames},
      {"stale and unowned handles", stale_and_unowned_handles_fail_without_dereference},
      {"10000 transient allocations", transient_allocations_are_bounded_at_safepoints},
      {"array growth pressure", array_growth_counts_toward_safepoint_pressure},
  };
  bool passed = true;
  for (const auto &test : tests) {
    try {
      test.run();
      require_empty("context teardown retained managed storage");
    } catch (const std::exception &error) {
      std::cerr << test.name << ": " << error.what() << '\n';
      passed = false;
    }
  }
  if (passed)
    std::cout << "native managed GC: 8 contracts passed\n";
  return passed ? 0 : 1;
}

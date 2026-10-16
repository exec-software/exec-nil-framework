#include "native_runtime.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

class context_scope {
public:
  context_scope() { nyrt_context_begin(0, nullptr); }
  ~context_scope() { nyrt_context_end(); }
};

class empty_managed_frame {
  const void *identity_ = nullptr;
public:
  empty_managed_frame() { nyrt_roots_enter(&identity_, 0); }
  ~empty_managed_frame() { nyrt_roots_leave(&identity_); }
};

void *allocate(std::uint64_t bytes) {
  auto *value = nyrt_alloc(bytes);
  require(value != nullptr, "fixture allocation failed");
  return value;
}

void word(void *object, std::size_t index, const void *child) {
  const auto bits = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(child));
  std::memcpy(static_cast<unsigned char *>(object) + index * sizeof(bits), &bits, sizeof(bits));
}

void raise(const std::string &type, void *payload, std::int64_t line = 19) {
  nyrt_fault_v1_raise(type.data(), type.size(), payload, line);
}

bool matches(std::uint64_t slot, const std::string &type) {
  return nyrt_fault_v1_match(slot, type.data(), type.size()) != 0;
}

void *bind(std::uint64_t slot, const std::string &type) {
  return nyrt_fault_v1_bind(slot, type.data(), type.size());
}

void pending_crosses_frames_and_roots_nested_graph() {
  context_scope context;
  empty_managed_frame roots;
  const auto caller = nyrt_fault_v1_frame_enter();
  const auto callee = nyrt_fault_v1_frame_enter();
  void *payload = allocate(16);
  void *child = allocate(8);
  void *array = nyrt_array_new(NYRT_ARRAY_TEXT, 1);
  require(array != nullptr, "text array allocation failed");
  const auto *text = nyrt_i64_to_text(123456789);
  require(text != nullptr, "text allocation failed");
  require(nyrt_array_set(array, NYRT_ARRAY_TEXT, 0,
      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(text))) == 1,
      "text array initialization failed");
  word(payload, 0, child);
  word(payload, 1, array);
  allocate(64); // No language root: this allocation must disappear.
  raise("first::Problem", payload);
  require(nyrt_fault_v1_pending() == 1, "raise did not set pending state");
  nyrt_fault_v1_frame_leave(callee);
  nyrt_collect();
  require(nyrt_managed_objects() == 4, "pending fault did not retain nested graph across return");
  nyrt_fault_v1_save(3);
  require(nyrt_fault_v1_pending() == 0 && nyrt_fault_v1_saved_depth() == 1,
          "saving did not suspend propagation");
  require(matches(3, std::string("first::") + "Problem") && !matches(3, "second::Problem"),
          "fault matching used short name or string-pointer identity");
  require(bind(3, "first::Problem") == payload && std::strcmp(text, "123456789") == 0,
          "saved fault lost original payload identity");
  nyrt_collect();
  require(nyrt_managed_objects() == 4, "saved fault graph was not rooted");
  nyrt_fault_v1_drop(3);
  nyrt_collect();
  require(nyrt_managed_objects() == 0, "dropping fault retained otherwise unowned graph");
  nyrt_fault_v1_frame_leave(caller);
}

void nested_slots_and_non_lifo_cleanup() {
  context_scope context;
  empty_managed_frame roots;
  const auto frame = nyrt_fault_v1_frame_enter();
  void *outer = allocate(8);
  void *inner = allocate(8);
  raise("Outer", outer, 21);
  nyrt_fault_v1_save(11);
  const auto nested = nyrt_fault_v1_frame_enter();
  raise("Inner", inner, 45);
  nyrt_fault_v1_save(11); // Same slot number in another invocation is independent.
  require(bind(11, "Inner") == inner, "recursive slot identity aliased its caller");
  nyrt_fault_v1_resume(11, 1);
  nyrt_fault_v1_frame_leave(nested);
  nyrt_fault_v1_save(22);
  require(bind(11, "Outer") == outer && bind(22, "Inner") == inner,
          "nested propagation overwrote suspended outer fault");
  nyrt_fault_v1_drop(11); // Cleanup may replace an earlier saved fault.
  nyrt_collect();
  require(nyrt_managed_objects() == 1 && bind(22, "Inner") == inner,
          "non-LIFO drop lost replacement fault or retained old root");
  nyrt_fault_v1_resume(22, 1);
  require(nyrt_fault_v1_saved_depth() == 0, "consuming resume retained its saved slot");
  nyrt_fault_v1_frame_leave(frame);
  nyrt_collect();
  require(nyrt_managed_objects() == 1 && nyrt_fault_v1_pending(),
          "propagation return did not preserve pending payload root");
}

void retaining_rethrow_and_enter_snapshot() {
  context_scope context;
  empty_managed_frame roots;
  const auto frame = nyrt_fault_v1_frame_enter();
  void *outer = allocate(8);
  raise("Outer", outer, 77);
  nyrt_fault_v1_save(9);
  const std::uint64_t saved_at_enter[] = {9};
  raise("Discarded", allocate(8));
  nyrt_fault_v1_save(10);
  nyrt_fault_v1_resume(9, 0);
  require(nyrt_fault_v1_saved_depth() == 2, "retaining rethrow consumed source slot");
  nyrt_fault_v1_unwind(saved_at_enter, 1);
  nyrt_fault_v1_save(12);
  require(nyrt_fault_v1_saved_depth() == 2 && bind(9, "Outer") == outer &&
              bind(12, "Outer") == outer,
          "exceptional snapshot restore discarded outer token or payload identity");
  nyrt_collect();
  require(nyrt_managed_objects() == 1, "unwind retained younger discarded fault root");
  nyrt_fault_v1_drop(9);
  nyrt_fault_v1_drop(12);
  nyrt_collect();
  require(nyrt_managed_objects() == 0, "finished handler retained suspended roots");
  nyrt_fault_v1_frame_leave(frame);
}

void exact_byte_identity_and_unmanaged_caller_contract() {
  context_scope context;
  const auto frame = nyrt_fault_v1_frame_enter();
  std::uint64_t manually_owned_payload = 37;
  const std::string identity("Problem\0suffix", 14);
  raise(identity, &manually_owned_payload);
  nyrt_fault_v1_save(0);
  require(matches(0, identity) && !matches(0, "Problem") &&
              bind(0, identity) == &manually_owned_payload,
          "fault identity was truncated at NUL or unmanaged payload changed");
  nyrt_fault_v1_drop(0);
  require(manually_owned_payload == 37, "fault clear took ownership of manual payload");
  nyrt_fault_v1_frame_leave(frame);
}

#ifndef _WIN32
// Fatal probes deliberately terminate without context teardown. Use a trusted
// manual payload so leak diagnostics do not obscure the protocol diagnostic.
std::uint64_t fatal_probe_payload = 0;

void subprocess_failure(const std::function<void()> &action, const std::string &expected) {
  int descriptors[2];
  require(pipe(descriptors) == 0, "could not open diagnostic pipe");
  const auto child = fork();
  require(child >= 0, "could not fork fault protocol probe");
  if (child == 0) {
    close(descriptors[0]);
    if (dup2(descriptors[1], STDERR_FILENO) < 0) _exit(99);
    close(descriptors[1]);
    nyrt_context_begin(0, nullptr);
    action();
    _exit(99);
  }
  close(descriptors[1]);
  std::string output;
  char buffer[256];
  for (;;) {
    const auto count = read(descriptors[0], buffer, sizeof(buffer));
    if (count <= 0) break;
    output.append(buffer, static_cast<std::size_t>(count));
  }
  close(descriptors[0]);
  int status = 0;
  require(waitpid(child, &status, 0) == child, "could not wait for fault protocol probe");
  require(WIFEXITED(status) && WEXITSTATUS(status) == 1,
          "invalid fault state was not a deterministic fatal runtime error");
  if (output != expected)
    throw std::runtime_error("fault protocol diagnostic changed: " + output);
}

void malformed_runtime_protocol_is_fatal() {
  const auto expected = "NIL native runtime error: invalid fault protocol state\n";
  subprocess_failure([] { nyrt_fault_v1_frame_leave(1); }, expected);
  subprocess_failure([] { nyrt_fault_v1_frame_enter(); nyrt_fault_v1_save(1); }, expected);
  subprocess_failure([] { nyrt_fault_v1_frame_enter(); nyrt_fault_v1_drop(1); }, expected);
  subprocess_failure([] { nyrt_fault_v1_frame_enter(); nyrt_fault_v1_resume(1, 1); }, expected);
  subprocess_failure([] { nyrt_fault_v1_frame_enter(); nyrt_fault_v1_raise("F", 1, nullptr, 1); }, expected);
  subprocess_failure([] {
    const auto frame = nyrt_fault_v1_frame_enter();
    raise("First", &fatal_probe_payload); nyrt_fault_v1_save(1);
    nyrt_fault_v1_frame_leave(frame);
  }, expected);
  subprocess_failure([] {
    nyrt_fault_v1_frame_enter(); raise("First", &fatal_probe_payload); nyrt_fault_v1_save(1);
    bind(1, "Other");
  }, expected);
  subprocess_failure([] {
    nyrt_fault_v1_frame_enter(); raise("First", &fatal_probe_payload); nyrt_fault_v1_save(1);
    raise("Second", &fatal_probe_payload); nyrt_fault_v1_save(1);
  }, expected);
  subprocess_failure([] {
    nyrt_fault_v1_frame_enter(); raise("First", &fatal_probe_payload);
    nyrt_fault_v1_frame_enter();
  }, expected);
  subprocess_failure([] {
    nyrt_fault_v1_frame_enter(); raise("First", &fatal_probe_payload);
    const std::uint64_t missing[] = {18}; nyrt_fault_v1_unwind(missing, 1);
  }, expected);
  subprocess_failure([] {
    const auto frame = nyrt_fault_v1_frame_enter();
    raise("module::Problem", allocate(8), 123);
    nyrt_fault_v1_save(4); nyrt_fault_v1_resume(4, 0);
    nyrt_fault_v1_frame_leave(frame);
    nyrt_fault_v1_report_uncaught();
    nyrt_context_end();
    _exit(1);
  }, "Ny uncaught fault: module::Problem at line 123\n");
}
#endif

} // namespace

int main() {
  try {
    pending_crosses_frames_and_roots_nested_graph();
    nested_slots_and_non_lifo_cleanup();
    retaining_rethrow_and_enter_snapshot();
    exact_byte_identity_and_unmanaged_caller_contract();
#ifndef _WIN32
    malformed_runtime_protocol_is_fatal();
#endif
    std::cout << "native fault runtime: passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native fault runtime: " << error.what() << '\n';
    return 1;
  }
}

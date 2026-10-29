#include "native_runtime.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::uint64_t word(const void *pointer) {
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

void *box(const char *type, std::uint64_t value) {
  return nyrt_any_v1_box(type, std::strlen(type), value);
}

void values_and_roots() {
  nyrt_context_begin(0, nullptr);
  char descriptor[] = "i64";
  void *integer = box(descriptor, 42);
  descriptor[0] = 'f';
  require(nyrt_any_v1_is_type(integer, "i64", 3) == 1,
          "anything descriptor was not copied into immutable storage");
  require(nyrt_any_v1_is_type(integer, "i32", 3) == 0,
          "anything type testing widened numeric identity");
  require(nyrt_any_v1_unbox(integer, "i64", 3) == 42,
          "integer anything round trip changed its payload");
  const double negative_zero = -0.0;
  std::uint64_t bits;
  std::memcpy(&bits, &negative_zero, sizeof(bits));
  require(nyrt_any_v1_unbox(box("f64", bits), "f64", 3) == bits,
          "anything changed floating-point payload bits");
  require(nyrt_any_v1_unbox(box("bool", 0), "bool", 4) == 0 &&
          nyrt_any_v1_unbox(box("bool", 1), "bool", 4) == 1,
          "anything lost a valid boolean state");
  require(nyrt_bool_i64(0) == 0 && nyrt_bool_i64(1) == 1 &&
          nyrt_bool_f64(-0.0) == 0 && nyrt_bool_f64(1.0) == 1,
          "strict boolean conversion rejected 0 or 1");
  require(std::string(nyrt_text_bool(0)) == "false" &&
          std::string(nyrt_text_bool(1)) == "true",
          "strict boolean formatting changed valid output");
  for (const auto *text : {"true", "TRUE", "Yes", "1", "on"})
    require(nyrt_bool_text(text) == 1, "text boolean conversion lost a true spelling");
  for (const auto *text : {"false", "FALSE", "No", "0", "off"})
    require(nyrt_bool_text(text) == 0, "text boolean conversion lost a false spelling");
  nyrt_context_end();

  nyrt_context_begin(0, nullptr);
  const char *payload = nyrt_text_substr("retained payload", 0, 16);
  void *text_box = box("text", word(payload));
  void *root = nyrt_array_new_with_value(NYRT_ARRAY_ANYTHING, 2, word(text_box));
  require(root != nullptr, "initialized anything array allocation failed");
  const void *slots[] = {&root};
  nyrt_roots_enter(slots, 1);
  (void)nyrt_text_substr("unreachable garbage", 0, 19);
  nyrt_collect();
  require(nyrt_managed_objects() == 3,
          "collector did not retain precisely the array, box, and managed payload");
  for (int index = 0; index < 2; ++index) {
    const auto retained = reinterpret_cast<const void *>(
        static_cast<std::uintptr_t>(nyrt_array_get(root, NYRT_ARRAY_ANYTHING, index)));
    const auto text = reinterpret_cast<const char *>(
        static_cast<std::uintptr_t>(nyrt_any_v1_unbox(retained, "text", 4)));
    require(std::string(text) == "retained payload", "GC lost a boxed text payload");
  }
  require(nyrt_array_push(root, NYRT_ARRAY_ANYTHING, word(box("i64", 9))) == 1,
          "anything array could not append another concrete type");
  require(nyrt_array_set(root, NYRT_ARRAY_ANYTHING, 0, word(box("bool", 1))) == 1,
          "anything array could not replace a concrete type");
  nyrt_collect();
  require(nyrt_array_length(root, NYRT_ARRAY_ANYTHING) == 3,
          "anything array changed size during collection");
  root = nullptr;
  nyrt_collect();
  require(nyrt_managed_objects() == 0, "unreachable anything graph was retained");
  nyrt_roots_leave(slots);
  require(nyrt_array_new(NYRT_ARRAY_ANYTHING, 0) != nullptr,
          "empty anything array was rejected");
  nyrt_context_end();
}

#ifndef _WIN32
void fails(const char *name, const std::function<void()> &operation,
           const char *diagnostic) {
  int output[2];
  require(::pipe(output) == 0, "could not create diagnostic pipe");
  std::fflush(nullptr);
  const auto process = ::fork();
  require(process >= 0, "could not create runtime rejection child");
  if (process == 0) {
    ::close(output[0]);
    if (::dup2(output[1], STDERR_FILENO) < 0) std::_Exit(90);
    ::close(output[1]);
    nyrt_context_begin(0, nullptr);
    operation();
    std::_Exit(0);
  }
  ::close(output[1]);
  std::string error;
  char buffer[512];
  for (;;) {
    const auto count = ::read(output[0], buffer, sizeof(buffer));
    if (count > 0) error.append(buffer, static_cast<std::size_t>(count));
    else if (count < 0 && errno == EINTR) continue;
    else break;
  }
  ::close(output[0]);
  int status = 0;
  while (::waitpid(process, &status, 0) < 0) {
    if (errno != EINTR) throw std::runtime_error("could not wait for rejection child");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 1 ||
      error.find(diagnostic) == std::string::npos)
    throw std::runtime_error(std::string(name) + ": expected a diagnosed runtime failure, got " + error);
}

void invalid_values() {
  fails("mismatched unbox", [] { nyrt_any_v1_unbox(box("i64", 5), "bool", 4); },
        "expected 'bool', got 'i64'");
  fails("null box", [] { nyrt_any_v1_is_type(nullptr, "i64", 3); }, "absent");
  fails("forged box", [] {
    nyrt_any_v1_unbox(reinterpret_cast<void *>(std::uintptr_t{1}), "i64", 3);
  }, "foreign");
  fails("allocation masquerading as box", [] {
    nyrt_any_v1_is_type(nyrt_alloc(64), "i64", 3);
  }, "foreign");
  fails("box from previous context", [] {
    void *old = box("i64", 1);
    nyrt_context_end();
    nyrt_context_begin(0, nullptr);
    nyrt_any_v1_is_type(old, "i64", 3);
  }, "foreign");
  fails("borrowed descriptor", [] { box("ref<i32>", 1); }, "descriptor");
  fails("noncanonical descriptor", [] { box("boolean", 1); }, "descriptor");
  fails("absent payload", [] { box("text", 0); }, "initialized value");
  fails("nonempty default array", [] { nyrt_array_new(NYRT_ARRAY_ANYTHING, 1); }, "initial value");
  fails("uninitialized initial element", [] {
    nyrt_array_new_with_value(NYRT_ARRAY_ANYTHING, 1, 0);
  }, "absent");
  fails("anything out of bounds", [] {
    nyrt_array_get(nyrt_array_new(NYRT_ARRAY_ANYTHING, 0), NYRT_ARRAY_ANYTHING, 0);
  }, "out of bounds");
  fails("invalid pushed box", [] {
    nyrt_array_push(nyrt_array_new(NYRT_ARRAY_ANYTHING, 0), NYRT_ARRAY_ANYTHING, 0);
  }, "absent");
  fails("invalid stored box", [] {
    void *items = nyrt_array_new_with_value(NYRT_ARRAY_ANYTHING, 1, word(box("i64", 5)));
    nyrt_array_set(items, NYRT_ARRAY_ANYTHING, 0, 1);
  }, "foreign");
  fails("integer boolean", [] { nyrt_bool_i64(2); }, "0 or 1");
  fails("negative boolean", [] { nyrt_bool_i64(-1); }, "0 or 1");
  fails("fractional boolean", [] { nyrt_bool_f64(0.5); }, "0 or 1");
  fails("NaN boolean", [] { nyrt_bool_f64(std::numeric_limits<double>::quiet_NaN()); }, "0 or 1");
  fails("infinite boolean", [] { nyrt_bool_f64(std::numeric_limits<double>::infinity()); }, "0 or 1");
  fails("unrecognized text boolean", [] { nyrt_bool_text("perhaps"); }, "recognized bool");
  fails("whitespace text boolean", [] { nyrt_bool_text(" true"); }, "recognized bool");
  fails("numeric text boolean", [] { nyrt_bool_text("2"); }, "recognized bool");
  fails("absent text boolean", [] { nyrt_bool_text(nullptr); }, "recognized bool");
  fails("formatted boolean", [] { nyrt_text_bool(2); }, "0 or 1");
  fails("boxed boolean", [] { box("bool", 2); }, "0 or 1");
  fails("boxed byte range", [] { box("u8", 256); }, "declared type");
  fails("boxed integer range", [] { box("i32", std::uint64_t{1} << 40); }, "declared type");
  fails("initial boolean", [] { nyrt_array_new_with_value(NYRT_ARRAY_BOOL, 1, 2); }, "0 or 1");
  fails("stored boolean", [] {
    nyrt_array_set(nyrt_array_new(NYRT_ARRAY_BOOL, 1), NYRT_ARRAY_BOOL, 0, 2);
  }, "0 or 1");
  fails("pushed boolean", [] {
    nyrt_array_push(nyrt_array_new(NYRT_ARRAY_BOOL, 0), NYRT_ARRAY_BOOL, 2);
  }, "0 or 1");
  fails("pointer boolean", [] {
    nyrt_buffer_set(nyrt_buffer_new(NYRT_ARRAY_BOOL, 1), NYRT_ARRAY_BOOL, 0, 2);
  }, "0 or 1");
}
#endif

} // namespace

int main() {
  try {
    values_and_roots();
#ifndef _WIN32
    invalid_values();
#endif
    std::cout << "native dynamic values and strict booleans passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

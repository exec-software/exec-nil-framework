#ifndef NIL_MEMORY_PROFILE_HPP
#define NIL_MEMORY_PROFILE_HPP

#include <cstdint>
#include <memory>
#include <string_view>

namespace ny::memory_profile {

// A capture owns one runtime invocation, not the process allocator. No
// allocation addresses or language values are serialized. The first opted-in
// session in a process owns the capture; other sessions remain uninstrumented.
class Session {
public:
  Session() noexcept;
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  // NIL_MEMORY_PROFILE is a new destination path. Existing files are never
  // truncated, including symlinks. False means an explicit capture error.
  bool begin(std::string_view route) noexcept;
  bool enabled() const noexcept;
  void site(std::string_view file, std::string_view function,
            std::uint64_t line) noexcept;
  void allocate(const void *identity, std::uint64_t bytes,
                std::string_view kind) noexcept;
  void resize(const void *identity, std::uint64_t bytes) noexcept;
  void release(const void *identity) noexcept;
  void collection(std::uint64_t duration_ns, std::uint64_t freed_bytes,
                  std::uint64_t freed_objects) noexcept;
  // Snapshot before runtime teardown. Live values are retained, not proven
  // leaks. On failure a diagnostic is emitted and false is returned.
  bool finish(std::string_view status = "completed", bool completed = true) noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace ny::memory_profile
#endif

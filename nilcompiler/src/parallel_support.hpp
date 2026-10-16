#ifndef NY_COMPILER_PARALLEL_SUPPORT_HPP
#define NY_COMPILER_PARALLEL_SUPPORT_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ny::parallel_support {

// A deliberately bounded fork/join primitive shared by both execution routes.
// Every callback receives its exclusive host-worker slot and input index; the
// caller owns input copies and per-worker execution state. No thread survives
// either a successful return or an exception, including thread-start failure.
inline constexpr std::size_t maximum_items = 1024 * 1024;
inline constexpr std::size_t maximum_threads = 8;

inline std::size_t worker_count(std::size_t count) {
  if (count > maximum_items)
    throw std::runtime_error("PARALLEL_MAP: input exceeds 1048576 items");
  if (count == 0) return 0;
  const auto available = std::max(2U, std::thread::hardware_concurrency());
  return std::min(count, std::min(maximum_threads,
                                 static_cast<std::size_t>(available)));
}

template <typename Work>
void for_each(std::size_t count, Work work) {
  const std::size_t slots = worker_count(count);
  if (slots == 0) return;
  std::atomic<std::size_t> next{0};
  std::vector<std::exception_ptr> failures(count);
  std::vector<std::thread> threads;
  threads.reserve(slots);
  struct join_scope {
    std::vector<std::thread> &threads;
    ~join_scope() {
      for (auto &thread : threads)
        if (thread.joinable()) thread.join();
    }
  } joining{threads};
  for (std::size_t slot = 0; slot < slots; ++slot) {
    threads.emplace_back([&, slot] {
      for (;;) {
        const auto index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= count) return;
        try {
          work(slot, index);
        } catch (...) {
          failures[index] = std::current_exception();
        }
      }
    });
  }
  for (auto &thread : threads) thread.join();
  // Scheduling does not choose the reported error: the lowest failed input
  // wins, after all submitted items have finished and every worker has joined.
  for (std::size_t index = 0; index < count; ++index) {
    if (!failures[index]) continue;
    try {
      std::rethrow_exception(failures[index]);
    } catch (const std::exception &error) {
      throw std::runtime_error("PARALLEL_MAP: worker at input " +
                               std::to_string(index) + " failed: " + error.what());
    } catch (...) {
      throw std::runtime_error("PARALLEL_MAP: worker at input " +
                               std::to_string(index) + " failed");
    }
  }
}

} // namespace ny::parallel_support

#endif

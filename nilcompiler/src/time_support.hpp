#ifndef NY_COMPILER_TIME_SUPPORT_HPP
#define NY_COMPILER_TIME_SUPPORT_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace ny::time_support {

inline std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch()).count();
}

inline void sleep_ms(std::int64_t milliseconds) {
  // Preserve the language's nonpositive-duration no-op. Bound each conversion
  // to the host clock's finer units so even a full-width i64 cannot overflow
  // inside sleep_for before it starts waiting.
  constexpr std::int64_t day_ms = 24LL * 60 * 60 * 1000;
  while (milliseconds > 0) {
    const auto chunk = std::min(milliseconds, day_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
    milliseconds -= chunk;
  }
}

} // namespace ny::time_support
#endif

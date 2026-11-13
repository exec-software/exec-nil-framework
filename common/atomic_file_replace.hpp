#ifndef NIL_COMMON_ATOMIC_FILE_REPLACE_HPP
#define NIL_COMMON_ATOMIC_FILE_REPLACE_HPP

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nil::detail {

inline bool replace_output_file(const std::filesystem::path &staging,
                                const std::filesystem::path &output,
                                std::error_code &error) {
#ifdef _WIN32
  // A recently executed image or a reader without delete sharing can briefly
  // prevent replacement. Retain the compiler's bounded retry policy for every
  // executable publisher. Persistent locks and access failures still fail;
  // never unlink the old output or relax its permissions to make progress.
  for (int attempt = 0;; ++attempt) {
    if (MoveFileExW(staging.c_str(), output.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      error.clear();
      return true;
    }
    const DWORD code = GetLastError();
    const bool transient = code == ERROR_ACCESS_DENIED ||
                           code == ERROR_SHARING_VIOLATION ||
                           code == ERROR_LOCK_VIOLATION;
    if (!transient || attempt >= 50) {
      error = std::error_code(static_cast<int>(code), std::system_category());
      return false;
    }
    Sleep(20);
  }
#else
  std::filesystem::rename(staging, output, error);
  return !error;
#endif
}

} // namespace nil::detail

#endif

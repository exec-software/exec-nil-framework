#ifndef NIL_COMMON_PRIVATE_DIRECTORY_HPP
#define NIL_COMMON_PRIVATE_DIRECTORY_HPP

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#include <cerrno>
#endif

namespace nil::detail {

// Create a fresh, privately named directory under `parent`.
//
// Preserve exclusive creation without mkdtemp, which Windows does not have:
// std::filesystem::create_directory reports whether it was the creator, so a
// name that already exists is never adopted. The name is drawn from a
// non-deterministic source so a caller cannot be made to collide on purpose,
// and the loop is bounded so exhaustion is an error rather than a hang.
//
// On POSIX creation is atomically owner-only, matching mkdtemp. Windows
// inherits the parent's ACL; callers must use trusted output/TEMP directories.
// A random name and exclusive creation do not imply cross-user isolation.
inline std::filesystem::path create_private_directory(
    const std::filesystem::path &parent, const std::string &prefix) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device source;
  std::uniform_int_distribution<std::size_t> pick(0, sizeof(alphabet) - 2);
  std::error_code error;
  std::filesystem::create_directories(parent, error);
  for (int attempt = 0; attempt < 128; ++attempt) {
    std::string name = prefix;
    for (int i = 0; i < 10; ++i) name += alphabet[pick(source)];
    const auto candidate = parent / name;
#ifdef _WIN32
    if (!std::filesystem::create_directory(candidate, error)) continue;
#else
    // chmod after default-permission mkdir leaves a writable race window.
    if (::mkdir(candidate.c_str(), 0700) != 0) {
      if (errno == EEXIST) continue;
      throw std::runtime_error("cannot create private directory in " + parent.string());
    }
#endif
    return candidate;
  }
  throw std::runtime_error("cannot create private directory in " +
                           parent.string());
}

}  // namespace nil::detail

#endif  // NIL_COMMON_PRIVATE_DIRECTORY_HPP

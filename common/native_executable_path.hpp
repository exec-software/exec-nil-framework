#ifndef NIL_COMMON_NATIVE_EXECUTABLE_PATH_HPP
#define NIL_COMMON_NATIVE_EXECUTABLE_PATH_HPP

#include <filesystem>

namespace nil::detail {

// Where the native backend actually installs the executable for a requested
// --output path.
//
// Windows applications carry the .exe suffix by convention and the shell will
// not run a file without it, so the backend appends one unless the requested
// path already ends that way. A caller that wants to stat, publish or run the
// artifact afterwards has to apply the identical rule, and spelling it out
// twice is how the two drift: niltest looked for "tests.native" while the
// compiler had written "tests.native.exe", and reported the compilation as
// having produced nothing.
//
// Note this is not the only suffix rule in the toolchain and the others are
// not interchangeable: nillinker appends only when the requested path has no
// extension at all, and the bundle writer maps .nil instead of appending.
// This function is specifically the native-backend rule.
inline std::filesystem::path native_executable_path(
    std::filesystem::path requested) {
#ifdef _WIN32
  if (requested.extension() != ".exe") requested += ".exe";
#endif
  return requested;
}

}  // namespace nil::detail

#endif  // NIL_COMMON_NATIVE_EXECUTABLE_PATH_HPP

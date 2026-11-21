#ifndef NY_COMPILER_SECURE_FILE_IO_HPP
#define NY_COMPILER_SECURE_FILE_IO_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ny::detail {

struct BoundedFileRead {
  bool ok = false;
  std::vector<char> bytes;
  std::string error;
};

struct AtomicFileWrite {
  bool ok = false;
  std::string error;
};

struct FileLockResult {
  bool ok = false;
  std::int64_t token = 0;
  std::string error;
};

// A three-state metadata answer. `ok` says the entry was inspected; `value`
// is the predicate's verdict. A missing or uninspectable entry is an error,
// never a false verdict, so a caller cannot read "absent" as "unsafe" or the
// reverse.
struct FileCheck {
  bool ok = false;
  bool value = false;
  std::string error;
};

// Registry ownership is the execution-context boundary. Tokens are process-
// monotonic and never recycled, and never expose the host descriptor. Locks
// are advisory; callers must control the directory ancestry and cooperate.
class FileLockRegistry {
public:
  FileLockRegistry() = default;
  ~FileLockRegistry() noexcept;
  FileLockRegistry(const FileLockRegistry &) = delete;
  FileLockRegistry &operator=(const FileLockRegistry &) = delete;

  // create=true exclusively creates a fresh private backing file; false
  // requires an existing private regular file. Acquisition never blocks.
  FileLockResult acquire(const std::string &path, bool create);
  // Query only this registry; never releases, reacquires, or inspects a path.
  // Missing, stale, foreign and nonpositive tokens have a false verdict.
  FileCheck is_held(std::int64_t token) const;
  AtomicFileWrite release(std::int64_t token);
  void clear() noexcept;
  bool empty() const noexcept { return descriptors_.empty(); }

private:
  std::map<std::int64_t, int> descriptors_;
};

bool valid_file_path(const std::string &path);
bool valid_file_path(const std::filesystem::path &path);
bool valid_file_path(const char *path);
// Language text filenames are strict UTF-8 on every host, without Unicode
// normalization. These entry points do not depend on the process code page.
BoundedFileRead read_regular_file_bounded_utf8(const std::string &path,
                                              std::uint64_t maximum_bytes);
AtomicFileWrite write_file_atomic_utf8(const std::string &path,
                                      const std::vector<char> &bytes,
                                      bool replace);
// POSIX only. Directory synchronization is explicit; durable publication
// synchronizes the same anchored directory used to publish the destination.
// An error after publication does not imply that the old destination remains.
// Directory synchronization and directory metadata inspection reject trailing
// separators except for filesystem-root paths, preserving no-follow rules.
AtomicFileWrite sync_directory_utf8(const std::string &path);
AtomicFileWrite write_file_durable_utf8(const std::string &path,
                                       const std::vector<char> &bytes,
                                       bool replace);
// POSIX only. Exclusive owner-only directory creation: an existing entry is
// never adopted, and the final mode is set by creation rather than a later
// chmod, which would leave a writable window.
AtomicFileWrite create_private_directory_utf8(const std::string &path);
// POSIX only. No-follow metadata predicates. These inspect a path, so the
// answer describes the entry at inspection time and is not a safe substitute
// for opening the entry under O_NOFOLLOW: a preflight check cannot close the
// window between inspection and use. Publication and lock acquisition keep
// their own checked opens.
FileCheck private_regular_file_utf8(const std::string &path);
FileCheck private_directory_utf8(const std::string &path);
// Host callers retain the native path / legacy narrow-string contract.
BoundedFileRead read_regular_file_bounded(const std::string &path,
                                          std::uint64_t maximum_bytes);
BoundedFileRead read_regular_file_bounded(const std::filesystem::path &path,
                                          std::uint64_t maximum_bytes);
BoundedFileRead read_regular_file_bounded(const char *path,
                                          std::uint64_t maximum_bytes);
AtomicFileWrite write_file_atomic(const std::string &path,
                                  const std::vector<char> &bytes,
                                  bool replace);
AtomicFileWrite write_file_atomic(const std::filesystem::path &path,
                                  const std::vector<char> &bytes,
                                  bool replace);
AtomicFileWrite write_file_atomic(const char *path,
                                  const std::vector<char> &bytes,
                                  bool replace);

} // namespace ny::detail

#endif

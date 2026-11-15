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
AtomicFileWrite sync_directory_utf8(const std::string &path);
AtomicFileWrite write_file_durable_utf8(const std::string &path,
                                       const std::vector<char> &bytes,
                                       bool replace);
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

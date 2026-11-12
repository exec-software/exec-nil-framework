#ifndef NY_COMPILER_SECURE_FILE_IO_HPP
#define NY_COMPILER_SECURE_FILE_IO_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ny::detail {

struct BoundedFileRead {
  bool ok = false;
  std::vector<char> bytes;
  std::string error;
};

struct RootedFileSnapshot {
  bool ok = false;
  std::vector<char> bytes;
  std::int64_t modified_ms = 0;
  std::string error;
};

// The trusted root selector is opened once. Its ancestors are outside the
// relative-path trust boundary. Every relative component is opened from the
// preceding directory handle, rejecting links/reparse points. The relative
// name is strict UTF-8, slash-separated, with no empty, dot, parent, colon or
// backslash components. Only regular files up to maximum_bytes are read;
// maximum_bytes itself cannot exceed 512 MiB. Bytes and mtime are inspected
// through the same final handle, rejecting observed changes during the read.
// This is an anchored namespace read, not a filesystem transaction: privileged
// mounts, hard links and changes after the snapshot are outside its contract.
RootedFileSnapshot read_rooted_file_snapshot_utf8(const std::string &root,
                                                 const std::string &relative,
                                                 std::uint64_t maximum_bytes);

struct AtomicFileWrite {
  bool ok = false;
  std::string error;
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

#include "secure_file_io.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <limits>
#include <optional>
#include <random>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// These flags are part of the security and liveness contract below. A POSIX
// target that does not expose them must not silently compile a weaker version
// of the API.
#if !defined(O_NOFOLLOW)
#error "Ny secure file I/O requires O_NOFOLLOW on POSIX"
#endif
#if !defined(O_NONBLOCK)
#error "Ny secure file I/O requires O_NONBLOCK on POSIX"
#endif
#if !defined(O_CLOEXEC)
#error "Ny secure file I/O requires O_CLOEXEC on POSIX"
#endif
#if !defined(AT_SYMLINK_NOFOLLOW)
#error "Ny secure file I/O requires AT_SYMLINK_NOFOLLOW on POSIX"
#endif
#if !defined(O_DIRECTORY)
#error "Ny secure file I/O requires O_DIRECTORY on POSIX"
#endif
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ny::detail {

bool valid_file_path(const std::string &path) {
  return !path.empty() && path.find('\0') == std::string::npos;
}

bool valid_file_path(const std::filesystem::path &path) {
  const auto &native = path.native();
  return !native.empty() &&
         native.find(std::filesystem::path::value_type{}) == native.npos;
}

bool valid_file_path(const char *path) {
  return path != nullptr && valid_file_path(std::string(path));
}

namespace {

std::optional<std::filesystem::path>
path_from_api_string(const std::string &path) {
  if (!valid_file_path(path))
    return std::nullopt;
  try {
#ifdef _WIN32
    // Preserve the old CreateFileA-facing API contract for existing narrow
    // callers while routing the actual operation through the wide API. New
    // Unicode-aware callers should pass std::filesystem::path directly.
    if (path.size() > static_cast<std::size_t>(
                          (std::numeric_limits<int>::max)()))
      return std::nullopt;
    const int narrow_size = static_cast<int>(path.size());
    const int wide_size = MultiByteToWideChar(
        CP_ACP, 0, path.data(), narrow_size, nullptr, 0);
    if (wide_size <= 0)
      return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, path.data(), narrow_size, wide.data(),
                            wide_size) != wide_size)
      return std::nullopt;
    return std::filesystem::path(std::move(wide));
#else
    return std::filesystem::path(path);
#endif
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

#ifndef _WIN32

namespace {

class UniqueFileDescriptor {
public:
  explicit UniqueFileDescriptor(int descriptor = -1) : descriptor_(descriptor) {}
  ~UniqueFileDescriptor() {
    if (descriptor_ >= 0)
      ::close(descriptor_);
  }
  UniqueFileDescriptor(const UniqueFileDescriptor &) = delete;
  UniqueFileDescriptor &operator=(const UniqueFileDescriptor &) = delete;
  UniqueFileDescriptor(UniqueFileDescriptor &&other) noexcept
      : descriptor_(other.release()) {}
  UniqueFileDescriptor &operator=(UniqueFileDescriptor &&other) noexcept {
    if (this != &other) {
      if (descriptor_ >= 0)
        ::close(descriptor_);
      descriptor_ = other.release();
    }
    return *this;
  }
  int get() const { return descriptor_; }
  int release() {
    const int descriptor = descriptor_;
    descriptor_ = -1;
    return descriptor;
  }

private:
  int descriptor_;
};

bool same_regular_file_snapshot(const struct stat &before,
                                const struct stat &after) {
  if (before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      before.st_size != after.st_size || before.st_mtime != after.st_mtime ||
      before.st_ctime != after.st_ctime)
    return false;
#if defined(__APPLE__)
  return before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec &&
         before.st_ctimespec.tv_nsec == after.st_ctimespec.tv_nsec;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) ||      \
    defined(__OpenBSD__)
  return before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
         before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
#else
  return true;
#endif
}

std::string atomic_temporary_name() {
  static std::atomic<std::uint64_t> sequence{0};
  static std::random_device entropy;
  const std::uint64_t random_bits =
      (static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy();
  return ".ny-write-" + std::to_string(static_cast<unsigned long>(::getpid())) +
         "-" + std::to_string(random_bits) + "-" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

} // namespace

BoundedFileRead read_regular_file_bounded(const std::filesystem::path &path,
                                          std::uint64_t maximum_bytes) {
  if (!valid_file_path(path))
    return {false, {}, "file.read_regular_bytes: invalid path"};

  // Opening a FIFO for reading can otherwise block before fstat can reject it.
  const int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW;
  UniqueFileDescriptor input(::open(path.c_str(), flags));
  if (input.get() < 0)
    return {false, {},
            "file.read_regular_bytes: cannot open a no-follow regular file"};

  struct stat before {};
  if (::fstat(input.get(), &before) != 0 || !S_ISREG(before.st_mode))
    return {false, {},
            "file.read_regular_bytes: input is not a regular file"};
  if (before.st_size < 0 ||
      static_cast<std::uint64_t>(before.st_size) > maximum_bytes)
    return {false, {},
            "file.read_regular_bytes: input exceeds maximum_bytes"};

  std::vector<char> bytes(static_cast<std::size_t>(before.st_size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count =
        ::read(input.get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return {false, {},
              count == 0
                  ? "file.read_regular_bytes: input changed while reading"
                  : "file.read_regular_bytes: input read failed"};
    offset += static_cast<std::size_t>(count);
  }

  char extra = 0;
  ssize_t extra_count = -1;
  do {
    extra_count = ::read(input.get(), &extra, 1);
  } while (extra_count < 0 && errno == EINTR);
  if (extra_count != 0)
    return {false, {},
            extra_count > 0
                ? "file.read_regular_bytes: input changed while reading"
                : "file.read_regular_bytes: input read failed"};

  struct stat after {};
  if (::fstat(input.get(), &after) != 0 || !S_ISREG(after.st_mode) ||
      !same_regular_file_snapshot(before, after))
    return {false, {},
            "file.read_regular_bytes: input changed while reading"};
  return {true, std::move(bytes), {}};
}

AtomicFileWrite write_file_atomic(const std::filesystem::path &path,
                                  const std::vector<char> &bytes,
                                  bool replace) {
  if (!valid_file_path(path))
    return {false, "file.write_bytes_atomic: invalid destination path"};
  const std::filesystem::path destination = path;
  const std::filesystem::path filename = destination.filename();
  if (filename.empty() || filename == "." || filename == "..")
    return {false, "file.write_bytes_atomic: invalid destination path"};
  std::filesystem::path parent = destination.parent_path();
  if (parent.empty())
    parent = ".";

  const int directory_flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY;
  UniqueFileDescriptor directory(::open(parent.c_str(), directory_flags));
  if (directory.get() < 0)
    return {false,
            "file.write_bytes_atomic: cannot open destination directory"};

  if (replace) {
    struct stat destination_status {};
    if (::fstatat(directory.get(), filename.c_str(), &destination_status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
      if (!S_ISREG(destination_status.st_mode))
        return {false,
                "file.write_bytes_atomic: destination is not a regular file"};
    } else if (errno != ENOENT) {
      return {false,
              "file.write_bytes_atomic: cannot inspect destination"};
    }
  }

  std::string temporary_name;
  UniqueFileDescriptor temporary;
  for (int attempt = 0; attempt < 128; ++attempt) {
    temporary_name = atomic_temporary_name();
    // Publication deliberately uses this fresh private inode even when it
    // replaces an existing file; metadata from the old inode is not copied.
    const int temporary_flags =
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW;
    const int descriptor = ::openat(directory.get(), temporary_name.c_str(),
                                    temporary_flags, 0600);
    if (descriptor >= 0) {
      temporary = UniqueFileDescriptor(descriptor);
      break;
    }
    if (errno != EEXIST)
      return {false,
              "file.write_bytes_atomic: cannot create staging file"};
  }
  if (temporary.get() < 0)
    return {false, "file.write_bytes_atomic: cannot create staging file"};

  // openat's creation mode is filtered through the caller's umask. Enforce the
  // API's exact private-inode contract before any content becomes observable.
  int chmod_status = -1;
  do {
    chmod_status = ::fchmod(temporary.get(), S_IRUSR | S_IWUSR);
  } while (chmod_status != 0 && errno == EINTR);
  if (chmod_status != 0) {
    ::unlinkat(directory.get(), temporary_name.c_str(), 0);
    return {false, "file.write_bytes_atomic: cannot secure staging file"};
  }

  // Cleanup is best effort. In particular, publication cannot be safely
  // reported as failed or rolled back after linkat has made the destination
  // visible, so a rare unlink failure can leave the private staging name.
  auto discard_temporary = [&]() {
    if (!temporary_name.empty())
      ::unlinkat(directory.get(), temporary_name.c_str(), 0);
  };
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = ::write(temporary.get(), bytes.data() + offset,
                                  bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      discard_temporary();
      return {false, "file.write_bytes_atomic: staging write failed"};
    }
    offset += static_cast<std::size_t>(count);
  }
  if (::fsync(temporary.get()) != 0) {
    discard_temporary();
    return {false, "file.write_bytes_atomic: staging flush failed"};
  }
  const int temporary_descriptor = temporary.release();
  if (::close(temporary_descriptor) != 0) {
    discard_temporary();
    return {false, "file.write_bytes_atomic: staging close failed"};
  }

  if (!replace) {
    // linkat is an atomic no-clobber publication: every existing directory
    // entry, including a dangling symlink, makes it fail with EEXIST.
    if (::linkat(directory.get(), temporary_name.c_str(), directory.get(),
                 filename.c_str(), 0) != 0) {
      const int publish_error = errno;
      discard_temporary();
      return {false,
              publish_error == EEXIST
                  ? "file.write_bytes_atomic: destination already exists"
                  : "file.write_bytes_atomic: cannot publish staging file"};
    }
    discard_temporary();
    return {true, {}};
  }

  // renameat replaces the destination directory entry; it never follows a
  // symlink that may be introduced after the no-follow preflight above.
  if (::renameat(directory.get(), temporary_name.c_str(), directory.get(),
                 filename.c_str()) != 0) {
    discard_temporary();
    return {false, "file.write_bytes_atomic: cannot publish staging file"};
  }
  return {true, {}};
}

#else

namespace {

class UniqueWindowsHandle {
public:
  explicit UniqueWindowsHandle(HANDLE handle = INVALID_HANDLE_VALUE)
      : handle_(handle) {}
  ~UniqueWindowsHandle() {
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
  }
  UniqueWindowsHandle(const UniqueWindowsHandle &) = delete;
  UniqueWindowsHandle &operator=(const UniqueWindowsHandle &) = delete;
  UniqueWindowsHandle(UniqueWindowsHandle &&other) noexcept
      : handle_(other.release()) {}
  UniqueWindowsHandle &operator=(UniqueWindowsHandle &&other) noexcept {
    if (this != &other) {
      if (handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
      handle_ = other.release();
    }
    return *this;
  }
  HANDLE get() const { return handle_; }
  HANDLE release() {
    const HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return handle;
  }

private:
  HANDLE handle_;
};

bool same_regular_file_snapshot(const BY_HANDLE_FILE_INFORMATION &before,
                                const BY_HANDLE_FILE_INFORMATION &after) {
  return before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
         before.nFileIndexHigh == after.nFileIndexHigh &&
         before.nFileIndexLow == after.nFileIndexLow &&
         before.nFileSizeHigh == after.nFileSizeHigh &&
         before.nFileSizeLow == after.nFileSizeLow &&
         CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) == 0;
}

std::filesystem::path
atomic_temporary_path(const std::filesystem::path &parent) {
  static std::atomic<std::uint64_t> sequence{0};
  static std::random_device entropy;
  const std::uint64_t random_bits =
      (static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy();
  const std::string name =
      ".ny-write-" + std::to_string(GetCurrentProcessId()) + "-" +
      std::to_string(random_bits) + "-" +
      std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
  return parent / name;
}

} // namespace

BoundedFileRead read_regular_file_bounded(const std::filesystem::path &path,
                                          std::uint64_t maximum_bytes) {
  if (!valid_file_path(path))
    return {false, {}, "file.read_regular_bytes: invalid path"};

  UniqueWindowsHandle input(CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (input.get() == INVALID_HANDLE_VALUE)
    return {false, {},
            "file.read_regular_bytes: cannot open a no-follow regular file"};

  BY_HANDLE_FILE_INFORMATION before {};
  if (GetFileType(input.get()) != FILE_TYPE_DISK ||
      !GetFileInformationByHandle(input.get(), &before) ||
      (before.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
    return {false, {},
            "file.read_regular_bytes: input is not a regular file"};
  const std::uint64_t size =
      (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32U) |
      before.nFileSizeLow;
  if (size > maximum_bytes)
    return {false, {},
            "file.read_regular_bytes: input exceeds maximum_bytes"};

  std::vector<char> bytes(static_cast<std::size_t>(size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD count = 0;
    const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, (std::numeric_limits<DWORD>::max)()));
    if (!ReadFile(input.get(), bytes.data() + offset, requested, &count,
                  nullptr) || count == 0)
      return {false, {},
              "file.read_regular_bytes: input changed while reading"};
    offset += count;
  }
  char extra = 0;
  DWORD extra_count = 0;
  if (!ReadFile(input.get(), &extra, 1, &extra_count, nullptr))
    return {false, {}, "file.read_regular_bytes: input read failed"};

  BY_HANDLE_FILE_INFORMATION after {};
  if (extra_count != 0 || !GetFileInformationByHandle(input.get(), &after) ||
      !same_regular_file_snapshot(before, after))
    return {false, {},
            "file.read_regular_bytes: input changed while reading"};
  return {true, std::move(bytes), {}};
}

AtomicFileWrite write_file_atomic(const std::filesystem::path &path,
                                  const std::vector<char> &bytes,
                                  bool replace) {
  if (!valid_file_path(path))
    return {false, "file.write_bytes_atomic: invalid destination path"};
  const std::filesystem::path destination = path;
  if (destination.filename().empty() || destination.filename() == "." ||
      destination.filename() == "..")
    return {false, "file.write_bytes_atomic: invalid destination path"};
  std::filesystem::path parent = destination.parent_path();
  if (parent.empty())
    parent = ".";

  if (replace) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
      if ((attributes &
           (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
        return {false,
                "file.write_bytes_atomic: destination is not a regular file"};
    } else {
      const DWORD status = GetLastError();
      if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
        return {false,
                "file.write_bytes_atomic: cannot inspect destination"};
    }
  }

  std::filesystem::path temporary_path;
  UniqueWindowsHandle temporary;
  for (int attempt = 0; attempt < 128; ++attempt) {
    temporary_path = atomic_temporary_path(parent);
    const HANDLE handle = CreateFileW(
        temporary_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
      temporary = UniqueWindowsHandle(handle);
      break;
    }
    const DWORD create_error = GetLastError();
    if (create_error != ERROR_FILE_EXISTS &&
        create_error != ERROR_ALREADY_EXISTS)
      return {false,
              "file.write_bytes_atomic: cannot create staging file"};
  }
  if (temporary.get() == INVALID_HANDLE_VALUE)
    return {false, "file.write_bytes_atomic: cannot create staging file"};

  // Cleanup is best effort. A sharing or host I/O failure can leave the named
  // staging artifact behind; it does not change publication's result.
  auto discard_temporary = [&]() {
    if (!temporary_path.empty())
      DeleteFileW(temporary_path.c_str());
  };
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD count = 0;
    const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, (std::numeric_limits<DWORD>::max)()));
    if (!WriteFile(temporary.get(), bytes.data() + offset, requested, &count,
                   nullptr) || count == 0) {
      const HANDLE failed = temporary.release();
      CloseHandle(failed);
      discard_temporary();
      return {false, "file.write_bytes_atomic: staging write failed"};
    }
    offset += count;
  }
  if (!FlushFileBuffers(temporary.get())) {
    const HANDLE failed = temporary.release();
    CloseHandle(failed);
    discard_temporary();
    return {false, "file.write_bytes_atomic: staging flush failed"};
  }
  const HANDLE staged = temporary.release();
  if (!CloseHandle(staged)) {
    discard_temporary();
    return {false, "file.write_bytes_atomic: staging close failed"};
  }

  const DWORD move_flags =
      MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
  if (!MoveFileExW(temporary_path.c_str(), path.c_str(), move_flags)) {
    const DWORD publish_error = GetLastError();
    discard_temporary();
    return {false,
            !replace && (publish_error == ERROR_FILE_EXISTS ||
                         publish_error == ERROR_ALREADY_EXISTS)
                ? "file.write_bytes_atomic: destination already exists"
                : "file.write_bytes_atomic: cannot publish staging file"};
  }
  return {true, {}};
}

#endif

BoundedFileRead read_regular_file_bounded(const std::string &path,
                                          std::uint64_t maximum_bytes) {
  const auto native_path = path_from_api_string(path);
  if (!native_path)
    return {false, {}, "file.read_regular_bytes: invalid path"};
  return read_regular_file_bounded(*native_path, maximum_bytes);
}

BoundedFileRead read_regular_file_bounded(const char *path,
                                          std::uint64_t maximum_bytes) {
  if (path == nullptr)
    return {false, {}, "file.read_regular_bytes: invalid path"};
  return read_regular_file_bounded(std::string(path), maximum_bytes);
}

AtomicFileWrite write_file_atomic(const std::string &path,
                                  const std::vector<char> &bytes,
                                  bool replace) {
  const auto native_path = path_from_api_string(path);
  if (!native_path)
    return {false, "file.write_bytes_atomic: invalid destination path"};
  return write_file_atomic(*native_path, bytes, replace);
}

AtomicFileWrite write_file_atomic(const char *path,
                                  const std::vector<char> &bytes,
                                  bool replace) {
  if (path == nullptr)
    return {false, "file.write_bytes_atomic: invalid destination path"};
  return write_file_atomic(std::string(path), bytes, replace);
}

} // namespace ny::detail

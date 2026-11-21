#include "../nilcompiler/src/secure_file_io.hpp"
#include "../common/content_digest.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__linux__) && defined(NIL_TEST_WRAP_FSYNC) && NIL_TEST_WRAP_FSYNC
namespace {
// This seam exists only in the Linux test executable. Production helpers have
// no fault switch, environment override, or alternate synchronization path.
enum class sync_fault { none, directory, regular };
std::atomic<sync_fault> pending_sync_fault{sync_fault::none};
}

extern "C" int __real_fsync(int descriptor);
extern "C" int __wrap_fsync(int descriptor) {
  struct stat metadata {};
  if (::fstat(descriptor, &metadata) == 0) {
    auto selected = S_ISDIR(metadata.st_mode) ? sync_fault::directory
                    : S_ISREG(metadata.st_mode) ? sync_fault::regular
                                                : sync_fault::none;
    if (selected != sync_fault::none &&
        pending_sync_fault.compare_exchange_strong(selected, sync_fault::none)) {
      errno = EIO;
      return -1;
    }
  }
  return __real_fsync(descriptor);
}
#endif

namespace {
namespace fs = std::filesystem;
using ny::detail::FileLockRegistry;

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Result>
void require_error(const Result &result, const std::string &private_marker) {
  require(!result.ok, "unsafe operation unexpectedly succeeded");
  require(!result.error.empty(), "operation failed without a diagnostic");
  require(result.error.find(private_marker) == std::string::npos,
          "error disclosed a supplied private path");
}

class fixture {
public:
  fixture() {
#if defined(__unix__) || defined(__APPLE__)
    std::string pattern = (fs::temp_directory_path() / "nil-durable-io-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const auto directory = ::mkdtemp(writable.data());
    require(directory != nullptr, "cannot create private durability fixture");
    root = fs::absolute(directory);
#else
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::temp_directory_path() / ("nil-durable-io-" + std::to_string(stamp));
    require(fs::create_directory(root), "cannot create durability fixture");
#endif
  }
  ~fixture() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  fs::path root;
};

std::string read_bytes(const fs::path &path) {
  require(fs::file_size(path) <= 4096, "unexpectedly large fixture file");
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "cannot read fixture file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const fs::path &path, const std::string &content) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "cannot create fixture file");
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  require(static_cast<bool>(output), "cannot finish fixture write");
#if defined(__unix__) || defined(__APPLE__)
  require(::chmod(path.c_str(), 0600) == 0, "cannot secure fixture file");
#endif
}

#if defined(__unix__) || defined(__APPLE__)
class child_guard {
public:
  explicit child_guard(pid_t child) : child_(child) {}
  ~child_guard() {
    if (child_ > 0) {
      ::kill(child_, SIGKILL);
      int status = 0;
      while (::waitpid(child_, &status, 0) < 0 && errno == EINTR) {}
    }
  }

  int wait() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      int status = 0;
      const auto observed = ::waitpid(child_, &status, WNOHANG);
      if (observed == child_) {
        child_ = -1;
        return status;
      }
      require(observed == 0 || (observed < 0 && errno == EINTR),
              "cannot observe child process");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("durability child exceeded its deadline");
  }

private:
  pid_t child_;
};

template <typename Function>
void in_bounded_child(Function function) {
  const auto child = ::fork();
  require(child >= 0, "cannot fork durability fixture");
  if (child == 0) {
    try {
      function();
      ::_exit(0);
    } catch (...) {
      ::_exit(1);
    }
  }
  child_guard owner(child);
  const auto status = owner.wait();
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "durability child contract failed");
}

void check_directory_sync(const fs::path &root) {
  const auto synced = ny::detail::sync_directory_utf8(root.u8string());
  require(synced.ok && synced.error.empty(), "directory synchronization failed");
  const auto missing = root / "private-sync-marker-missing";
  require_error(ny::detail::sync_directory_utf8(missing.u8string()), "private-sync-marker");
  const auto regular = root / "private-sync-marker-file";
  write_bytes(regular, "retained");
  require_error(ny::detail::sync_directory_utf8(regular.u8string()), "private-sync-marker");
  const auto linked = root / "private-sync-marker-link";
  fs::create_directory_symlink(root, linked);
  require_error(ny::detail::sync_directory_utf8(linked.u8string()), "private-sync-marker");
  for (const auto *suffix : {"/", "///"}) {
    require_error(ny::detail::sync_directory_utf8(linked.u8string() + suffix),
                  "private-sync-marker");
    // Real directories use the same operand rule, without trying to infer the
    // entry kind by following a slash-terminated pathname first.
    require_error(ny::detail::sync_directory_utf8(root.u8string() + suffix),
                  root.filename().u8string());
  }
  for (const auto *root_operand : {"/", "///"}) {
    const auto root_sync = ny::detail::sync_directory_utf8(root_operand);
    require(root_sync.error != "file.sync_directory: invalid directory path",
            "root directory was rejected as a trailing-separator operand");
  }
  require_error(ny::detail::sync_directory_utf8(""), "private-sync-marker");
  require_error(ny::detail::sync_directory_utf8(std::string("private-sync-marker\0tail", 24)),
                "private-sync-marker");
  require_error(ny::detail::sync_directory_utf8(root.u8string() + "/private-sync-marker-" + '\xff'),
                "private-sync-marker");
  require(read_bytes(regular) == "retained", "failed synchronization changed file contents");
}

void check_durable_publication(const fs::path &root) {
  const auto target = root / "checkpoint-caf\xc3\xa9.bin";
  const std::vector<char> first{0, '\n', '\r', static_cast<char>(0x80), static_cast<char>(0xff)};
  auto created = ny::detail::write_file_durable_utf8(target.u8string(), first, false);
  require(created.ok && created.error.empty(), "durable no-clobber write failed");
  require(read_bytes(target) == std::string(first.begin(), first.end()), "durable bytes changed");
  struct stat metadata {};
  require(::stat(target.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0600,
          "durable publication did not create a private inode");
  const std::vector<char> next{'n', 'e', 'x', 't'};
  require_error(ny::detail::write_file_durable_utf8(target.u8string(), next, false), "checkpoint-caf");
  require(read_bytes(target) == std::string(first.begin(), first.end()), "no-clobber replaced existing bytes");
  const auto alias = root / "old-checkpoint-alias";
  fs::create_hard_link(target, alias);
  require(::chmod(target.c_str(), 0644) == 0, "cannot widen replacement fixture mode");
  const auto replaced = ny::detail::write_file_durable_utf8(target.u8string(), next, true);
  require(replaced.ok, "durable replacement failed");
  require(read_bytes(target) == "next", "durable replacement did not publish new bytes");
  require(read_bytes(alias) == std::string(first.begin(), first.end()), "replacement mutated an old hardlink");
  require(::stat(target.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0600 && metadata.st_nlink == 1,
          "durable replacement did not publish a fresh private inode");
  require(ny::detail::write_file_durable_utf8(target.u8string(), {}, true).ok,
          "empty durable replacement failed");
  require(read_bytes(target).empty(), "empty durable publication retained old contents");
  const auto absent = root / "replace-absent";
  require(ny::detail::write_file_durable_utf8(absent.u8string(), next, true).ok,
          "durable replacement could not create an absent destination");

  const auto sentinel = root / "private-write-marker-sentinel";
  write_bytes(sentinel, "must remain unchanged");
  const auto symlink = root / "private-write-marker-symlink";
  fs::create_symlink(sentinel, symlink);
  for (const auto replace : {false, true}) {
    require_error(ny::detail::write_file_durable_utf8(symlink.u8string(), next, replace), "private-write-marker");
    require(read_bytes(sentinel) == "must remain unchanged", "durable write followed a destination symlink");
  }
  const auto fifo = root / "private-write-marker-fifo";
  require(::mkfifo(fifo.c_str(), 0600) == 0, "cannot create durable FIFO fixture");
  in_bounded_child([&] {
    require_error(ny::detail::write_file_durable_utf8(fifo.u8string(), next, true), "private-write-marker");
  });
  require_error(ny::detail::write_file_durable_utf8(root.u8string(), next, true), root.u8string());
  require_error(ny::detail::write_file_durable_utf8("", next, false), "private-write-marker");
  const auto bad_utf8 = root.u8string() + "/private-write-marker-" + '\xff';
  require_error(ny::detail::write_file_durable_utf8(bad_utf8, next, false), "private-write-marker");
  auto nul_path = (root / "private-write-marker-nul").u8string();
  nul_path += std::string("\0tail", 5);
  require_error(ny::detail::write_file_durable_utf8(nul_path, next, false), "private-write-marker");
  require(!fs::exists(root / "private-write-marker-nul"), "NUL path was truncated into a created file");
  for (const auto &entry : fs::directory_iterator(root))
    require(entry.path().filename().string().rfind(".ny-write-", 0) != 0,
            "durable publication left a staging file behind");
}

#if defined(__linux__) && defined(NIL_TEST_WRAP_FSYNC) && NIL_TEST_WRAP_FSYNC
class sync_fault_scope {
public:
  explicit sync_fault_scope(sync_fault selected) {
    pending_sync_fault.store(selected);
  }
  ~sync_fault_scope() { pending_sync_fault.store(sync_fault::none); }
};

void check_synchronization_failures(const fs::path &root) {
  const std::vector<char> candidate{'c', 'a', 'n', 'd', 'i', 'd', 'a', 't', 'e'};
  for (const auto replace : {false, true}) {
    const auto target = root / (replace ? "private-sync-fault-replace" : "private-sync-fault-create");
    if (replace) write_bytes(target, "previous");
    sync_fault_scope fault(sync_fault::directory);
    const auto result = ny::detail::write_file_durable_utf8(target.u8string(), candidate, replace);
    require_error(result, "private-sync-fault");
    require(result.error == "file.write_bytes_durable: publication durability uncertain",
            "post-publication failure did not identify uncertain durability");
    require(pending_sync_fault.load() == sync_fault::none,
            "durable publication did not reach its directory barrier");
    require(read_bytes(target) == "candidate",
            "uncertain publication incorrectly restored or hid the visible candidate");
  }

  const auto legacy_path = root / "private-sync-fault-legacy";
  {
    sync_fault_scope fault(sync_fault::directory);
    const auto legacy = ny::detail::write_file_atomic_utf8(legacy_path.u8string(), candidate, false);
    require(legacy.ok && legacy.error.empty(), "visibility-only atomic write changed its flush semantics");
    require(pending_sync_fault.load() == sync_fault::directory,
            "visibility-only atomic write unexpectedly synchronized its directory");
    require(read_bytes(legacy_path) == "candidate", "legacy atomic write failed to publish candidate bytes");
    const auto directory = ny::detail::sync_directory_utf8(root.u8string());
    require_error(directory, root.u8string());
    require(directory.error == "file.sync_directory: synchronization failed",
            "standalone directory sync did not report its fixed synchronization failure");
    require(pending_sync_fault.load() == sync_fault::none,
            "standalone directory sync did not consume the selected fault");
  }

  const auto target = root / "private-sync-fault-staging";
  write_bytes(target, "previous");
  {
    sync_fault_scope fault(sync_fault::regular);
    const auto result = ny::detail::write_file_durable_utf8(target.u8string(), candidate, true);
    require_error(result, "private-sync-fault");
    require(result.error == "file.write_bytes_durable: staging flush failed",
            "pre-publication failure did not report its staging barrier");
    require(pending_sync_fault.load() == sync_fault::none,
            "staging write did not reach its regular-file barrier");
    require(read_bytes(target) == "previous", "failed staging sync changed the canonical destination");
  }
  for (const auto &entry : fs::directory_iterator(root))
    require(entry.path().filename().string().rfind(".ny-write-", 0) != 0,
            "failed staging synchronization left a temporary file");
  require(ny::detail::sync_directory_utf8(root.u8string()).ok,
          "one-shot test fault remained active after its selected failure");
}
#endif

void check_lock_lifecycle(const fs::path &root) {
  const auto path = root / "writer.lock";
  FileLockRegistry first;
  FileLockRegistry second;
  require(first.empty() && second.empty(), "new lock registry is not empty");
  require(first.is_held(1).ok && !first.is_held(1).value,
          "empty registry reported an unknown token as held");
  const auto held = first.acquire(path.u8string(), true);
  require(held.ok && held.token > 0 && held.error.empty(), "cannot create and acquire a private lock");
  require(!first.empty(), "held lock was not retained by its registry");
  require(first.is_held(held.token).ok && first.is_held(held.token).value,
          "registry did not report its held token");
  require(second.is_held(held.token).ok && !second.is_held(held.token).value &&
              !first.is_held(0).value && !first.is_held(-1).value,
          "registry query accepted a foreign or nonpositive token");
  struct stat metadata {};
  require(::stat(path.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0600 && metadata.st_nlink == 1,
          "lock creation did not retain a private regular file");
  require_error(first.acquire(path.u8string(), true), "writer.lock");
  require_error(first.acquire(path.u8string(), false), "writer.lock");
  require_error(second.acquire(path.u8string(), false), "writer.lock");
  require_error(second.release(held.token), "writer.lock");
  require(!first.empty() && second.empty(), "wrong registry changed a live lock");
  in_bounded_child([&] {
    FileLockRegistry child_registry;
    require_error(child_registry.acquire(path.u8string(), false), "writer.lock");
  });
  require(first.release(held.token).ok, "lock release failed");
  require(first.empty(), "released lock remains in registry");
  require(first.is_held(held.token).ok && !first.is_held(held.token).value,
          "registry query retained a released token");
  require_error(first.release(held.token), "writer.lock");
  require_error(first.release(0), "writer.lock");
  require_error(first.release(-1), "writer.lock");
  const auto next = second.acquire(path.u8string(), false);
  require(next.ok && next.token != held.token, "lock token was recycled after release");
  second.clear();
  require(second.empty(), "clear did not release registry locks");
  require(second.is_held(next.token).ok && !second.is_held(next.token).value,
          "registry query retained a cleared token");
  require_error(second.release(next.token), "writer.lock");
  const auto after_clear = first.acquire(path.u8string(), false);
  require(after_clear.ok && after_clear.token != next.token && after_clear.token != held.token,
          "lock token was recycled after registry clear");
  first.clear();
  const auto other = root / "destructor.lock";
  std::int64_t destroyed_token = 0;
  {
    FileLockRegistry transient;
    const auto acquired = transient.acquire(other.u8string(), true);
    require(acquired.ok, "cannot acquire destructor fixture lock");
    destroyed_token = acquired.token;
  }
  const auto after_destructor = second.acquire(other.u8string(), false);
  require(after_destructor.ok && after_destructor.token != destroyed_token,
          "registry destruction did not release lock or recycled its token");
  require_error(second.release(destroyed_token), "destructor.lock");
  second.clear();
}

void check_unsafe_locks(const fs::path &root) {
  FileLockRegistry registry;
  const auto missing = root / "private-lock-marker-missing";
  require_error(registry.acquire(missing.u8string(), false), "private-lock-marker");
  require(!fs::exists(missing), "open-existing lock silently initialized a missing file");
  const auto target = root / "private-lock-marker-target";
  write_bytes(target, "sentinel");
  const auto alias = root / "private-lock-marker-hardlink";
  fs::create_hard_link(target, alias);
  require_error(registry.acquire(target.u8string(), false), "private-lock-marker");
  require_error(registry.acquire(alias.u8string(), false), "private-lock-marker");
  fs::remove(alias);
  const auto link = root / "private-lock-marker-symlink";
  fs::create_symlink(target, link);
  require_error(registry.acquire(link.u8string(), false), "private-lock-marker");
  require_error(registry.acquire(link.u8string(), true), "private-lock-marker");
  require(::chmod(target.c_str(), 0644) == 0, "cannot widen unsafe lock fixture mode");
  require_error(registry.acquire(target.u8string(), false), "private-lock-marker");
  require(::chmod(target.c_str(), 0600) == 0, "cannot restore lock fixture mode");
  require_error(registry.acquire(root.u8string(), false), root.u8string());
  const auto fifo = root / "private-lock-marker-fifo";
  require(::mkfifo(fifo.c_str(), 0600) == 0, "cannot create lock FIFO fixture");
  in_bounded_child([&] {
    FileLockRegistry child_registry;
    require_error(child_registry.acquire(fifo.u8string(), false), "private-lock-marker");
  });
  require_error(registry.acquire("", true), "private-lock-marker");
  require_error(registry.acquire(root.u8string() + "/private-lock-marker-" + '\xff', true), "private-lock-marker");
  auto nul_path = (root / "private-lock-marker-nul").u8string();
  nul_path += std::string("\0tail", 5);
  require_error(registry.acquire(nul_path, true), "private-lock-marker");
  require(!fs::exists(root / "private-lock-marker-nul"), "NUL lock path was truncated into a created file");
  require(registry.empty(), "rejected lock operation retained a live descriptor");
  require(read_bytes(target) == "sentinel", "rejected lock operation changed fixture bytes");
}

void check_process_death_cleanup(const fs::path &root) {
  const auto path = root / "process-death.lock";
  write_bytes(path, "");
  int ready[2] = {-1, -1};
  require(::pipe(ready) == 0, "cannot create bounded lock coordination pipe");
  const auto child = ::fork();
  if (child < 0) {
    ::close(ready[0]);
    ::close(ready[1]);
    throw std::runtime_error("cannot fork lock owner");
  }
  if (child == 0) {
    ::close(ready[0]);
    FileLockRegistry registry;
    const auto held = registry.acquire(path.u8string(), false);
    const char marker = held.ok ? '1' : '0';
    if (::write(ready[1], &marker, 1) != 1 || !held.ok) ::_exit(2);
    ::close(ready[1]);
    for (;;) ::pause();
  }
  child_guard owner(child);
  ::close(ready[1]);
  pollfd waiting{ready[0], POLLIN, 0};
  int observed = -1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) break;
    observed = ::poll(&waiting, 1, static_cast<int>(remaining));
  } while (observed < 0 && errno == EINTR);
  char marker = '0';
  const auto count = observed > 0 ? ::read(ready[0], &marker, 1) : 0;
  ::close(ready[0]);
  require(observed > 0 && count == 1 && marker == '1', "child did not acquire its lock within deadline");
  FileLockRegistry parent_registry;
  require_error(parent_registry.acquire(path.u8string(), false), "process-death.lock");
  require(::kill(child, SIGKILL) == 0, "cannot terminate child lock owner");
  const auto status = owner.wait();
  require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "lock owner did not die as expected");
  const auto acquired = parent_registry.acquire(path.u8string(), false);
  require(acquired.ok, "process death did not release the operating-system lock");
  require(parent_registry.release(acquired.token).ok, "cannot release post-crash lock");
}
void check_private_metadata(const fs::path &root) {
  const auto created = root / "owner-only";
  require(ny::detail::create_private_directory_utf8(created.u8string()).ok,
          "cannot create an owner-only directory");
  struct stat status {};
  require(::lstat(created.c_str(), &status) == 0 && S_ISDIR(status.st_mode) &&
              (status.st_mode & 07777) == 0700,
          "created directory is not owner-only");
  // An existing entry is never adopted, so a caller cannot be handed a
  // directory it did not create.
  require_error(ny::detail::create_private_directory_utf8(created.u8string()), "owner-only");
  require_error(ny::detail::create_private_directory_utf8(std::string("bad\0path", 8)), "bad");

  auto verdict = [](const ny::detail::FileCheck &check, bool expected,
                    const std::string &message) {
    require(check.ok, "metadata inspection failed: " + message);
    require(check.value == expected, message);
  };

  const auto secret = root / "secret";
  write_bytes(secret, "payload");
  verdict(ny::detail::private_regular_file_utf8(secret.u8string()), true,
          "a 0600 owned regular file is private");
  verdict(ny::detail::private_directory_utf8(created.u8string()), true,
          "a 0700 owned directory is private");
  // Cross-kind answers are verdicts, not errors.
  verdict(ny::detail::private_regular_file_utf8(created.u8string()), false,
          "a directory is not a private regular file");
  verdict(ny::detail::private_directory_utf8(secret.u8string()), false,
          "a regular file is not a private directory");

  const auto loose = root / "loose";
  write_bytes(loose, "payload");
  require(::chmod(loose.c_str(), 0644) == 0, "cannot loosen fixture mode");
  verdict(ny::detail::private_regular_file_utf8(loose.u8string()), false,
          "a 0644 file is not private");
  const auto loose_directory = root / "loose-directory";
  require(::mkdir(loose_directory.c_str(), 0755) == 0, "cannot create loose directory");
  // The negative fixture must stay non-private even under an embedding
  // process's restrictive umask. This changes only our newly created fixture.
  require(::chmod(loose_directory.c_str(), 0755) == 0,
          "cannot loosen fixture directory mode");
  verdict(ny::detail::private_directory_utf8(loose_directory.u8string()), false,
          "a 0755 directory is not private");

  // A symlink is judged as itself: it never inherits its target's verdict.
  const auto link = root / "link";
  require(::symlink(secret.c_str(), link.c_str()) == 0, "cannot create fixture symlink");
  verdict(ny::detail::private_regular_file_utf8(link.u8string()), false,
          "a symlink to a private file is not a private regular file");
  const auto directory_link = root / "directory-link";
  require(::symlink(created.c_str(), directory_link.c_str()) == 0,
          "cannot create fixture directory symlink");
  verdict(ny::detail::private_directory_utf8(directory_link.u8string()), false,
          "a symlink to a private directory is not a private directory");
  for (const auto *suffix : {"/", "///"}) {
    require_error(ny::detail::private_directory_utf8(directory_link.u8string() + suffix),
                  "directory-link");
    require_error(ny::detail::private_directory_utf8(created.u8string() + suffix),
                  "owner-only");
  }
  // Root has no final named entry to follow. Its privacy verdict depends on
  // the actual owner/mode, but it must remain an inspectable operand.
  for (const auto *root_operand : {"/", "///"}) {
    require(ny::detail::private_directory_utf8(root_operand).ok,
            "filesystem-root metadata could not be inspected");
  }

  // A second hard link means another name can reach the same inode.
  const auto shared = root / "shared";
  require(::link(secret.c_str(), shared.c_str()) == 0, "cannot create fixture hard link");
  verdict(ny::detail::private_regular_file_utf8(shared.u8string()), false,
          "a multiply-linked file is not private");
  require(::unlink(shared.c_str()) == 0, "cannot remove fixture hard link");

  const auto pipe = root / "pipe";
  require(::mkfifo(pipe.c_str(), 0600) == 0, "cannot create fixture FIFO");
  verdict(ny::detail::private_regular_file_utf8(pipe.u8string()), false,
          "a FIFO is not a private regular file");

  // Absence cannot be inspected; it is an error, not a false verdict.
  const auto absent = root / "absent-metadata-marker";
  require_error(ny::detail::private_regular_file_utf8(absent.u8string()),
                "absent-metadata-marker");
  require_error(ny::detail::private_directory_utf8(absent.u8string()),
                "absent-metadata-marker");
}

void check_private_directory_umask(const fs::path &root) {
  const auto sentinel = root / "umask-existing-file";
  const auto existing = root / "umask-existing-directory";
  const auto link = root / "umask-existing-link";
  write_bytes(sentinel, "retained");
  require(ny::detail::create_private_directory_utf8(existing.u8string()).ok,
          "cannot create existing-directory umask control");
  fs::create_directory_symlink(existing, link);

  // Change umask only in bounded children; an embedding process's creation
  // policy must not be changed by either the API or its regression test.
  for (const mode_t mask : {0000, 0077, 0700, 0777}) {
    const auto created = root / ("umask-created-" + std::to_string(mask));
    in_bounded_child([&] {
      ::umask(mask);
      const auto result = ny::detail::create_private_directory_utf8(created.u8string());
      require(::umask(mask) == mask, "private creation changed the caller's umask");
      struct stat metadata {};
      require(::lstat(created.c_str(), &metadata) == 0 &&
                  S_ISDIR(metadata.st_mode) && metadata.st_uid == ::geteuid(),
              "umask creation did not leave its owned directory");
      const auto verdict = ny::detail::private_directory_utf8(created.u8string());
      require(verdict.ok, "cannot inspect newly created umask directory");
      if ((mask & 0700) == 0) {
        require(result.ok && result.error.empty() && verdict.value &&
                    (metadata.st_mode & 07777) == 0700,
                "successful private creation did not have exact mode 0700");
      } else {
        require_error(result, created.filename().u8string());
        require(result.error ==
                    "file.create_private_directory: created directory is not owner-only" &&
                    !verdict.value && (metadata.st_mode & 07777) == 0,
                "restrictive umask was silently repaired or reported as success");
      }
      for (const auto &retained : {sentinel, existing, link}) {
        const auto rejected = ny::detail::create_private_directory_utf8(retained.u8string());
        require_error(rejected, retained.filename().u8string());
        require(rejected.error == "file.create_private_directory: entry already exists",
                "umask creation adopted an existing entry");
      }
    });
    require(read_bytes(sentinel) == "retained" && fs::is_symlink(link),
            "umask failure changed an existing entry");
    struct stat retained {};
    require(::lstat(existing.c_str(), &retained) == 0 &&
                S_ISDIR(retained.st_mode) && (retained.st_mode & 07777) == 0700,
            "umask failure changed an existing directory's mode");
    // The failed directory is empty. Removing that exact fixture name needs
    // permission on its owned parent, even when its own mode is 0000.
    require(::rmdir(created.c_str()) == 0, "cannot remove umask fixture directory");
  }
}

#else
void check_unsupported_host(const fs::path &root) {
  const auto target = root / "private-unsupported-marker";
  require_error(ny::detail::sync_directory_utf8(root.u8string()), "private-unsupported-marker");
  require_error(ny::detail::write_file_durable_utf8(target.u8string(), {'x'}, false), "private-unsupported-marker");
  FileLockRegistry registry;
  require_error(registry.acquire(target.u8string(), true), "private-unsupported-marker");
  require_error(registry.is_held(0), "private-unsupported-marker");
  require_error(ny::detail::create_private_directory_utf8(target.u8string()),
                "private-unsupported-marker");
  require_error(ny::detail::private_regular_file_utf8(target.u8string()),
                "private-unsupported-marker");
  require_error(ny::detail::private_directory_utf8(target.u8string()),
                "private-unsupported-marker");
  require(registry.empty() && !fs::exists(target), "unsupported host mutated its namespace");
}
#endif

void check_content_digest() {
  // RFC 6234 vectors, so a wrong digest fails rather than merely staying stable.
  nil::detail::sha256 empty;
  require(empty.finish() ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 of no bytes is wrong");
  nil::detail::sha256 abc;
  abc.update("abc", 3);
  require(abc.finish() ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 of \"abc\" is wrong");
  // An embedded NUL must be digested, not treated as a terminator.
  nil::detail::sha256 embedded;
  const char zeroed[] = {'a', '\0', 'b'};
  embedded.update(zeroed, sizeof(zeroed));
  require(embedded.finish() ==
              "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138",
          "a digest over embedded NUL bytes must cover every byte");
}
} // namespace

int main() {
  try {
    fixture owned;
#if defined(__unix__) || defined(__APPLE__)
    check_directory_sync(owned.root);
    check_durable_publication(owned.root);
#if defined(__linux__) && defined(NIL_TEST_WRAP_FSYNC) && NIL_TEST_WRAP_FSYNC
    check_synchronization_failures(owned.root);
#endif
    check_lock_lifecycle(owned.root);
    check_unsafe_locks(owned.root);
    check_process_death_cleanup(owned.root);
    check_private_metadata(owned.root);
    check_private_directory_umask(owned.root);
#else
    check_unsupported_host(owned.root);
#endif
    check_content_digest();
    std::cout << "durable-file-io-ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "durable-file-io: " << error.what() << '\n';
    return 1;
  }
}

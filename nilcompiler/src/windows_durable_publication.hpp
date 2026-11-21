#ifndef NIL_WINDOWS_DURABLE_PUBLICATION_HPP
#define NIL_WINDOWS_DURABLE_PUBLICATION_HPP

#include "secure_file_io.hpp"

#ifdef _WIN32
#include "windows_private_entries.hpp"
#include <winternl.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <random>

namespace ny::detail::windows_durable_publication {

using create_file_function = NTSTATUS (NTAPI *)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER,
    ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
using query_file_function = NTSTATUS (NTAPI *)(
    HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
using set_file_function = NTSTATUS (NTAPI *)(
    HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

template <typename Function>
inline Function native_function(const char *name) {
  const HMODULE module = GetModuleHandleW(L"ntdll.dll");
  const FARPROC symbol = module ? GetProcAddress(module, name) : nullptr;
  Function function = nullptr;
  static_assert(sizeof(function) == sizeof(symbol), "Windows function pointer size");
  std::memcpy(&function, &symbol, sizeof(function));
  return function;
}

inline create_file_function create_file_api() {
  static const auto function = native_function<create_file_function>("NtCreateFile");
  return function;
}

inline set_file_function set_file_api() {
  static const auto function = native_function<set_file_function>("NtSetInformationFile");
  return function;
}

// MS-FSA directory-flush semantics and Appendix A note 80 specify directory persistence for
// NTFS; other filesystem success responses do not establish that guarantee.
// Both facts below come from the held directory, never from a path/drive
// lookup. Unknown locality or filesystem is unsupported, before mutation.
// https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/0de7dc40-9627-437e-a4df-c4696cdc3d02
// https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fsa/4e3695bd-7574-4f24-a223-b4679c065b63
inline bool local_ntfs(HANDLE directory) {
  static const auto query = native_function<query_file_function>("NtQueryInformationFile");
  if (!query) return false;
  // FILE_IS_REMOTE_DEVICE_INFORMATION has one BOOLEAN member. Initialize it
  // to remote and require an actual completed response, not a zeroed fallback.
  BOOLEAN remote = TRUE;
  IO_STATUS_BLOCK io{};
  constexpr auto information_class = static_cast<FILE_INFORMATION_CLASS>(51);
  if (query(directory, &io, &remote, sizeof(remote), information_class) != 0 ||
      io.Status != 0 || io.Information != sizeof(remote) || remote != FALSE)
    return false;
  wchar_t filesystem[MAX_PATH + 1]{};
  DWORD flags = 0;
  if (!GetVolumeInformationByHandleW(directory, nullptr, 0, nullptr, nullptr,
                                    &flags, filesystem, MAX_PATH + 1))
    return false;
  return std::wstring_view(filesystem) == L"NTFS" &&
         (flags & FILE_PERSISTENT_ACLS) != 0 && (flags & FILE_READ_ONLY_VOLUME) == 0;
}

inline bool directory_operand(const std::filesystem::path &path) {
  // A standalone '.' selects the already-established cwd. It is not accepted
  // as a component or trailing suffix of a supplied pathname. Bare output
  // filenames use this same selector for their parent.
  return path.native() == L"." || windows_private_entries::valid_path(path, true);
}

inline HANDLE open_directory(const std::filesystem::path &path) {
  if (!directory_operand(path)) return INVALID_HANDLE_VALUE;
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return handle;
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  FILE_STANDARD_INFO standard{};
  if (GetFileType(handle) != FILE_TYPE_DISK ||
      !GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                   sizeof(attributes)) ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                   sizeof(standard)) ||
      !standard.Directory || standard.DeletePending) {
    CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
  }
  return handle;
}

inline AtomicFileWrite sync_directory(const std::filesystem::path &path) {
  if (!directory_operand(path))
    return {false, "file.sync_directory: invalid directory path"};
  nil::detail::storage_handle directory(open_directory(path));
  if (directory.get() == INVALID_HANDLE_VALUE)
    return {false, "file.sync_directory: cannot open directory"};
  if (!local_ntfs(directory.get()))
    return {false, "file.sync_directory: requires a local NTFS directory"};
  if (!FlushFileBuffers(directory.get()))
    return {false, "file.sync_directory: synchronization failed"};
  return {true, {}};
}

// Every child name here is one validated component, relative to the already
// opened directory. No process cwd lookup or parent pathname reopen occurs.
inline HANDLE open_child(HANDLE directory, const std::wstring &name,
                         ACCESS_MASK access, ULONG sharing, bool create,
                         PSECURITY_DESCRIPTOR security, NTSTATUS &status) {
  const auto api = create_file_api();
  status = static_cast<NTSTATUS>(0xc000000dL); // STATUS_INVALID_PARAMETER
  if (!api || name.empty() || name.size() > 32767 ||
      name.find_first_of(L"\\/:") != std::wstring::npos ||
      !windows_private_entries::valid_path(std::filesystem::path(name)))
    return INVALID_HANDLE_VALUE;
  UNICODE_STRING string{};
  string.Buffer = const_cast<wchar_t *>(name.data());
  string.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
  string.MaximumLength = string.Length;
  OBJECT_ATTRIBUTES attributes{};
  attributes.Length = sizeof(attributes);
  attributes.RootDirectory = directory;
  attributes.ObjectName = &string;
  attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE, no handle inheritance.
  attributes.SecurityDescriptor = security;
  IO_STATUS_BLOCK io{};
  HANDLE handle = INVALID_HANDLE_VALUE;
  constexpr ULONG file_open = 1, file_create = 2;
  constexpr ULONG no_follow = 0x00200000, synchronous = 0x20, non_directory = 0x40;
  status = api(&handle, access | SYNCHRONIZE, &attributes, &io, nullptr,
      FILE_ATTRIBUTE_NORMAL, sharing, create ? file_create : file_open,
      no_follow | synchronous | non_directory, nullptr, 0);
  const ULONG_PTR expected = create ? 2 : 1; // FILE_CREATED / FILE_OPENED.
  if (status != 0 || io.Status != 0 || io.Information != expected ||
      handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (status == 0) status = static_cast<NTSTATUS>(0xc0000001L); // STATUS_UNSUCCESSFUL
    return INVALID_HANDLE_VALUE;
  }
  return handle;
}

inline bool regular_destination(HANDLE handle) {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  FILE_STANDARD_INFO standard{};
  return GetFileType(handle) == FILE_TYPE_DISK &&
      GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                   sizeof(attributes)) &&
      (attributes.FileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
      GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                   sizeof(standard)) &&
      !standard.Directory && !standard.DeletePending;
}

class staging_file {
public:
  explicit staging_file(HANDLE handle) : handle_(handle) {}
  staging_file(const staging_file &) = delete;
  staging_file &operator=(const staging_file &) = delete;
  ~staging_file() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      if (!published_) {
        // Delete only this held object, never a potentially replaced pathname.
        // Cleanup failure may leave an orphan; it cannot change the old target.
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(handle_, FileDispositionInfo, &disposition,
                                   sizeof(disposition));
      }
      CloseHandle(handle_);
    }
  }
  HANDLE get() const { return handle_; }
  void published() { published_ = true; }
  bool close() {
    const HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return CloseHandle(handle) != FALSE;
  }
private:
  HANDLE handle_;
  bool published_ = false;
};

inline AtomicFileWrite write_file(const std::filesystem::path &path,
                                  const std::vector<char> &bytes, bool replace) {
  constexpr const char *operation = "file.write_bytes_durable";
  const auto failure = [](const char *reason) -> AtomicFileWrite {
    return {false, std::string("file.write_bytes_durable: ") + reason};
  };
  if (!windows_private_entries::valid_path(path))
    return failure("invalid destination path");
  const auto name = path.filename().native();
  auto parent = path.parent_path();
  if (parent.empty()) parent = L".";
  nil::detail::storage_handle directory(open_directory(parent));
  if (directory.get() == INVALID_HANDLE_VALUE) return failure("cannot open directory");
  if (!local_ntfs(directory.get())) return failure("requires a local NTFS directory");
  if (!create_file_api() || !set_file_api())
    return failure("relative file operations unavailable");

  if (replace) {
    NTSTATUS status = 0;
    nil::detail::storage_handle previous(open_child(directory.get(), name,
        FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        false, nullptr, status));
    if (previous.get() != INVALID_HANDLE_VALUE) {
      if (!regular_destination(previous.get())) return failure("destination is not a regular file");
    } else if (status != static_cast<NTSTATUS>(0xc0000034L)) { // STATUS_OBJECT_NAME_NOT_FOUND
      return failure("cannot inspect destination");
    }
  }

  nil::detail::owner_security identity;
  std::error_code error;
  if (!identity.initialize(false, error)) return failure("cannot establish owner identity");
  auto security = identity.attributes();
  HANDLE raw = INVALID_HANDLE_VALUE;
  static std::atomic<std::uint64_t> sequence{0};
  for (int attempt = 0; attempt < 128; ++attempt) {
    std::wstring candidate;
    try {
      std::random_device entropy;
      const auto random = (static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy();
      candidate = L".ny-write-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
          std::to_wstring(random) + L"-" +
          std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
    } catch (...) {
      return failure("cannot create staging file");
    }
    NTSTATUS status = 0;
    raw = open_child(directory.get(), candidate,
        GENERIC_WRITE | DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES,
        0, true, security.lpSecurityDescriptor, status);
    if (raw != INVALID_HANDLE_VALUE) break;
    if (status != static_cast<NTSTATUS>(0xc0000035L)) // STATUS_OBJECT_NAME_COLLISION
      return failure("cannot create staging file");
  }
  if (raw == INVALID_HANDLE_VALUE) return failure("cannot create staging file");
  staging_file temporary(raw);
  const auto private_file = windows_private_entries::inspect_handle(
      temporary.get(), false, operation, identity);
  if (!private_file.ok || !private_file.value) return failure("staging file is not private and regular");
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD count = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, (std::numeric_limits<DWORD>::max)()));
    DWORD written = 0;
    if (!WriteFile(temporary.get(), bytes.data() + offset, count, &written, nullptr) ||
        written == 0 || written > count)
      return failure("staging write failed");
    offset += written;
  }
  if (!FlushFileBuffers(temporary.get())) return failure("staging flush failed");

  const std::size_t name_bytes = name.size() * sizeof(wchar_t);
  // Include the full fixed structure and space beyond the counted name for a
  // zero terminator. Short leaves must not produce a buffer smaller than the
  // native FILE_RENAME_INFORMATION structure. Its layout is FILE_RENAME_INFO.
  const std::size_t rename_bytes = sizeof(FILE_RENAME_INFO) + name_bytes;
  // max_align_t storage keeps the variable-sized native structure aligned.
  std::vector<std::max_align_t> storage(
      (rename_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
  std::memset(storage.data(), 0, storage.size() * sizeof(std::max_align_t));
  auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
  rename->ReplaceIfExists = replace ? TRUE : FALSE;
  rename->RootDirectory = directory.get();
  rename->FileNameLength = static_cast<DWORD>(name_bytes);
  std::memcpy(rename->FileName, name.data(), name_bytes);
  // The Win32 wrapper can reject a supplied RootDirectory. Use the native
  // handle-relative operation directly, retaining the same write-capable
  // directory for publication and the final persistence barrier.
  // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information
  IO_STATUS_BLOCK renamed{};
  renamed.Status = static_cast<NTSTATUS>(0xc0000001L); // STATUS_UNSUCCESSFUL
  const auto status = set_file_api()(temporary.get(), &renamed, rename,
      static_cast<ULONG>(rename_bytes), static_cast<FILE_INFORMATION_CLASS>(10));
  if (status != 0 || renamed.Status != 0) {
    const auto error_status = status != 0 ? status : renamed.Status;
    return failure(!replace && error_status == static_cast<NTSTATUS>(0xc0000035L)
                       ? "destination already exists" : "cannot publish staging file");
  }
  temporary.published();
  // Publication has happened. Never remove the candidate or imply the prior
  // destination survived if close or directory synchronization fails. Close
  // the writing handle before the final directory flush, so close-time file
  // metadata updates precede that last persistence request too.
  const bool closed = temporary.close();
  // Even an uncertain close must not suppress the final directory barrier.
  const bool synchronized = FlushFileBuffers(directory.get()) != FALSE;
  if (!closed || !synchronized)
    return failure("publication durability uncertain");
  return {true, {}};
}

} // namespace ny::detail::windows_durable_publication
#endif // _WIN32
#endif // NIL_WINDOWS_DURABLE_PUBLICATION_HPP

#include "../common/private_directory.hpp"

#ifndef _WIN32
#error This contract requires the real Win32 filesystem APIs.
#endif

#include <winioctl.h>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// This executable is deliberately a single translation unit. Only its private
// copy of the inline implementation sees these redirects; production has no
// fault switches and this target must not also link secure_file_io.cpp.
BOOL WINAPI contract_flush(HANDLE handle);
BOOL WINAPI contract_close(HANDLE handle);
BOOL WINAPI contract_volume(HANDLE handle, LPWSTR volume, DWORD volume_size,
    LPDWORD serial, LPDWORD component_size, LPDWORD flags, LPWSTR filesystem,
    DWORD filesystem_size);
#define FlushFileBuffers contract_flush
#define CloseHandle contract_close
#define GetVolumeInformationByHandleW contract_volume
#include "../nilcompiler/src/windows_durable_publication.hpp"
#undef GetVolumeInformationByHandleW
#undef CloseHandle
#undef FlushFileBuffers

namespace {
namespace fs = std::filesystem;
namespace publication = ny::detail::windows_durable_publication;
namespace entries = ny::detail::windows_private_entries;
using nil::detail::storage_handle;

enum class flush_fault { none, regular, directory };
flush_fault pending = flush_fault::none;
bool fault_consumed = false;
bool fail_published_close = false, close_fault_consumed = false;
bool force_non_ntfs = false;
std::vector<bool> flushes;
fs::path pin_from, pin_to;
bool pin_attempted = false, pin_refused = false;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void reset_observation() {
  pending = flush_fault::none;
  fault_consumed = false;
  fail_published_close = close_fault_consumed = false;
  force_non_ntfs = false;
  flushes.clear();
  pin_from.clear();
  pin_to.clear();
  pin_attempted = pin_refused = false;
}

void refused(const ny::detail::AtomicFileWrite &result) {
  require(!result.ok && !result.error.empty(), "rejection must include a diagnostic");
  require(result.error.find("nil-win-publication-") == std::string::npos &&
          result.error.find("private-marker") == std::string::npos,
          "rejection disclosed a private pathname");
}

unsigned completed = 0;
template<class Function>
void contract(const char *name, Function body) {
  reset_observation();
  body();
  reset_observation();
  ++completed;
  std::cout << "PASS " << name << '\n';
}

struct fixture {
  fs::path root = nil::detail::create_private_directory(fs::temp_directory_path(),
                                                       "nil-win-publication-");
  std::vector<fs::path> junctions;
  ~fixture() {
    for (const auto &path : junctions) RemoveDirectoryW(path.c_str());
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

std::set<std::wstring> names(const fs::path &root) {
  std::set<std::wstring> result;
  for (const auto &entry : fs::directory_iterator(root)) result.insert(entry.path().filename().wstring());
  return result;
}

void write_fixture(const fs::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "open owned publication fixture");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  require(static_cast<bool>(output), "finish owned publication fixture");
}

std::string read_fixture(const fs::path &path) {
  require(fs::file_size(path) <= 4096, "fixture unexpectedly grew");
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "read owned publication fixture");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void private_file(const fs::path &path) {
  const auto result = entries::inspect_regular_file(path);
  require(result.ok && result.value && result.error.empty(), "publication must be a private single-link regular file");
}

void make_public(const fs::path &path) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)",
      SDDL_REVISION_1, &descriptor, nullptr), "create public source descriptor");
  PACL acl = nullptr;
  BOOL present = FALSE, defaulted = FALSE;
  const bool extracted = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present;
  const DWORD status = extracted ? SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
      SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      nullptr, nullptr, acl, nullptr) : ERROR_INVALID_SECURITY_DESCR;
  LocalFree(descriptor);
  require(status == ERROR_SUCCESS, "apply public source ACL");
}

void junction(fixture &owned, const fs::path &path, const fs::path &target) {
  require(fs::create_directory(path), "create owned junction entry");
  owned.junctions.push_back(path);
  const auto shown = fs::absolute(target).wstring();
  const auto substitute = L"\\??\\" + shown;
  struct data {
    DWORD tag;
    USHORT length, reserved, substitute_offset, substitute_length, print_offset, print_length;
    WCHAR names[1];
  };
  std::vector<char> storage(offsetof(data, names) +
      (substitute.size() + shown.size() + 2) * sizeof(WCHAR), 0);
  auto *value = reinterpret_cast<data *>(storage.data());
  value->tag = IO_REPARSE_TAG_MOUNT_POINT;
  value->length = static_cast<USHORT>(storage.size() - 8);
  value->substitute_length = static_cast<USHORT>(substitute.size() * sizeof(WCHAR));
  value->print_offset = static_cast<USHORT>((substitute.size() + 1) * sizeof(WCHAR));
  value->print_length = static_cast<USHORT>(shown.size() * sizeof(WCHAR));
  std::memcpy(value->names, substitute.data(), value->substitute_length);
  std::memcpy(reinterpret_cast<char *>(value->names) + value->print_offset, shown.data(), value->print_length);
  storage_handle held(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  require(held.get() != INVALID_HANDLE_VALUE, "open owned junction entry");
  DWORD returned = 0;
  require(DeviceIoControl(held.get(), FSCTL_SET_REPARSE_POINT, value,
      static_cast<DWORD>(storage.size()), nullptr, 0, &returned, nullptr), "set owned junction reparse data");
}

void run_contracts(fixture &owned) {
  const auto &root = owned.root;
  const std::vector<char> binary{0, '\n', '\r', static_cast<char>(0x80), static_cast<char>(0xff)};
  const std::string exact(binary.begin(), binary.end());
  const std::vector<char> next{'n', 'e', 'x', 't'};
  contract("real-local-ntfs-and-directory-flush", [&] {
    storage_handle handle(CreateFileW(root.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    require(handle.get() != INVALID_HANDLE_VALUE, "open local filesystem control");
    wchar_t filesystem[32]{};
    require(GetVolumeInformationByHandleW(handle.get(), nullptr, 0, nullptr, nullptr, nullptr,
        filesystem, 32) && std::wcscmp(filesystem, L"NTFS") == 0,
        "contract requires a real NTFS fixture volume");
    const auto result = publication::sync_directory(root);
    require(result.ok && result.error.empty(), "real directory flush failed");
    require(flushes == std::vector<bool>{true}, "directory sync must flush the actual directory handle");
  });
  contract("binary-create-private-and-ordered-barriers", [&] {
    const auto path = root / fs::u8path(u8"caf\u00e9-\u65e5\U0001f512.bin");
    auto expected = names(root);
    expected.insert(path.filename().wstring());
    const auto result = publication::write_file(path, binary, false);
    require(result.ok && result.error.empty() && read_fixture(path) == exact, "binary no-clobber publication");
    require(flushes == std::vector<bool>({false, true}), "flush staging bytes before directory metadata");
    private_file(path);
    require(names(root) == expected, "successful publication left a staging entry");
  });
  contract("no-clobber-preserves-existing-bytes-and-links", [&] {
    const auto target = root / "private-marker-existing", alias = root / "existing-alias";
    write_fixture(target, "retained");
    require(CreateHardLinkW(alias.c_str(), target.c_str(), nullptr), "create old inode alias");
    const auto before = names(root);
    refused(publication::write_file(target, next, false));
    require(read_fixture(target) == "retained" && read_fixture(alias) == "retained" && names(root) == before,
            "no-clobber refusal changed existing data or namespace");
  });
  contract("replacement-uses-fresh-private-single-link-file", [&] {
    const auto target = root / "replace-public", alias = root / "replace-public-alias";
    write_fixture(target, "old public bytes");
    make_public(target);
    require(CreateHardLinkW(alias.c_str(), target.c_str(), nullptr), "create replacement alias");
    const auto before = names(root);
    require(publication::write_file(target, next, true).ok, "replace existing regular file");
    require(read_fixture(target) == "next" && read_fixture(alias) == "old public bytes",
            "replacement must not mutate the old file through its other name");
    private_file(target);
    require(names(root) == before && flushes == std::vector<bool>({false, true}),
            "replacement barriers or staging cleanup changed");
  });
  contract("replace-absent-and-empty-payload", [&] {
    // A single UTF-16 code unit exercises the native rename buffer minimum.
    const auto target = root / "r";
    require(publication::write_file(target, next, true).ok && read_fixture(target) == "next",
            "replace=true must also publish an absent target");
    require(publication::write_file(target, {}, true).ok && read_fixture(target).empty(),
            "empty replacement must remove prior bytes");
    private_file(target);
    require(flushes == std::vector<bool>({false, true, false, true}), "each replacement must use both barriers");
  });
  contract("staging-flush-failure-preserves-prior-state", [&] {
    const auto existing = root / "private-marker-staging-old", absent = root / "private-marker-staging-absent";
    write_fixture(existing, "prior bytes");
    const auto before = names(root);
    for (const auto &target : {existing, absent}) {
      reset_observation();
      pending = flush_fault::regular;
      const auto result = publication::write_file(target, next, true);
      refused(result);
      require(fault_consumed && result.error == "file.write_bytes_durable: staging flush failed",
              "staging fault must reach the real publication path");
      require(flushes == std::vector<bool>{false}, "staging failure must precede any directory flush");
      require(read_fixture(existing) == "prior bytes" && !fs::exists(absent) && names(root) == before,
              "staging failure changed prior destination or leaked its temporary file");
    }
  });
  contract("post-publication-flush-failure-reports-uncertainty", [&] {
    const auto existing = root / "private-marker-uncertain-old", absent = root / "private-marker-uncertain-absent";
    write_fixture(existing, "prior bytes");
    for (const auto &target : {existing, absent}) {
      reset_observation();
      auto expected = names(root);
      expected.insert(target.filename().wstring());
      pending = flush_fault::directory;
      const auto result = publication::write_file(target, next, true);
      refused(result);
      require(fault_consumed && result.error == "file.write_bytes_durable: publication durability uncertain",
              "post-publication failure must not claim rollback or successful durability");
      require(flushes == std::vector<bool>({false, true}) && read_fixture(target) == "next",
              "directory flush failure occurs after new bytes are published");
      private_file(target);
      require(names(root) == expected, "uncertain publication left staging garbage or removed destination");
    }
  });
  contract("explicit-directory-flush-failure", [&] {
    const auto before = names(root);
    pending = flush_fault::directory;
    const auto result = publication::sync_directory(root);
    refused(result);
    require(fault_consumed && result.error == "file.sync_directory: synchronization failed" &&
        flushes == std::vector<bool>{true} && names(root) == before,
        "explicit failed flush must report the barrier failure without namespace changes");
  });
  contract("post-publication-close-failure-still-flushes-directory", [&] {
    const auto target = root / "private-marker-close-uncertain";
    auto expected = names(root);
    expected.insert(target.filename().wstring());
    fail_published_close = true;
    const auto result = publication::write_file(target, next, false);
    refused(result);
    require(close_fault_consumed && result.error == "file.write_bytes_durable: publication durability uncertain",
            "published close failure must report durability uncertainty");
    require(flushes == std::vector<bool>({false, true}) && read_fixture(target) == "next",
            "published close failure must still attempt the final directory barrier");
    private_file(target);
    require(names(root) == expected, "close uncertainty changed the published namespace");
  });
  contract("unsupported-filesystem-fails-before-mutation", [&] {
    const auto before = names(root);
    const auto target = root / "private-marker-non-ntfs";
    force_non_ntfs = true;
    refused(publication::write_file(target, next, false));
    refused(publication::sync_directory(root));
    require(flushes.empty() && names(root) == before, "unsupported filesystem reached writes or flushes");
  });
  contract("wrong-kind-and-final-reparse-rejected", [&] {
    const auto directory = root / "private-marker-directory";
    require(entries::create_directory(directory).ok, "create directory-kind fixture");
    write_fixture(directory / "sentinel", "target retained");
    const auto link = root / "private-marker-junction";
    junction(owned, link, directory);
    const auto before = names(root);
    for (const auto &target : {directory, link}) {
      refused(publication::write_file(target, next, false));
      refused(publication::write_file(target, next, true));
    }
    refused(publication::sync_directory(link));
    const auto regular = directory / "sentinel";
    refused(publication::sync_directory(regular));
    require(read_fixture(regular) == "target retained" && names(root) == before,
            "wrong-kind/reparse refusal altered its target or namespace");
  });
  contract("reparse-parent-is-not-followed", [&] {
    const auto target = root / "private-marker-parent-target";
    require(entries::create_directory(target).ok, "create parent target fixture");
    const auto alias = root / "private-marker-parent-junction";
    junction(owned, alias, target);
    refused(publication::write_file(alias / "injected", next, false));
    require(fs::is_empty(target), "publication followed a reparse final parent");
  });
  contract("missing-parent-and-invalid-operands", [&] {
    const auto before = names(root);
    const auto base = root / "private-marker-invalid";
    const std::vector<fs::path> invalid{
        fs::path(), fs::path(base.wstring() + L":stream"), fs::path(base.wstring() + L"."),
        fs::path(base.wstring() + L" "), fs::path(base.wstring() + L"/"),
        fs::path(base.wstring() + L"\\"), root / "CON", root / "NUL.txt", root / "COM1.log",
        fs::path(root.wstring() + L"/./private-marker-invalid"),
        fs::path(root.wstring() + L"/../private-marker-invalid"),
        fs::path(L"\\\\?\\" + base.wstring()), fs::path(L"\\\\.\\NUL"),
        fs::path(L"C:private-marker-invalid"),
        fs::path(base.wstring() + std::wstring(1, wchar_t(0xd800))),
        fs::path(base.wstring() + std::wstring(L"\0tail", 5))};
    require(invalid.size() == 16, "publication invalid-path inventory changed");
    for (const auto &path : invalid) {
      refused(publication::write_file(path, next, false));
      refused(publication::write_file(path, next, true));
      refused(publication::sync_directory(path));
    }
    refused(publication::write_file(root / "missing-parent" / "target", next, false));
    refused(publication::sync_directory(root / "missing-directory"));
    require(flushes.empty() && names(root) == before, "invalid operand reached publication or mutated namespace");
  });
  contract("occupied-destination-preserved-on-rename-failure", [&] {
    const auto target = root / "private-marker-open-destination";
    write_fixture(target, "held prior bytes");
    const auto before = names(root);
    {
      storage_handle held(CreateFileW(target.c_str(), GENERIC_READ,
          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
          FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      require(held.get() != INVALID_HANDLE_VALUE, "hold destination without delete sharing");
      refused(publication::write_file(target, next, true));
    }
    require(read_fixture(target) == "held prior bytes" && names(root) == before,
            "rename refusal changed destination or leaked staging entry");
  });
  contract("directory-handle-pins-publication-namespace", [&] {
    const auto directory = root / "pinned-parent", moved = root / "pinned-parent-moved";
    require(entries::create_directory(directory).ok, "create namespace pin fixture");
    pin_from = directory;
    pin_to = moved;
    const auto target = directory / "payload";
    require(publication::write_file(target, next, false).ok, "publish through retained parent handle");
    require(pin_attempted && pin_refused && !fs::exists(moved) && read_fixture(target) == "next",
            "publication parent was replaceable while its stage was being flushed");
    require(MoveFileExW(directory.c_str(), moved.c_str(), 0), "completed publication leaked parent handle");
    require(read_fixture(moved / "payload") == "next", "released parent namespace no longer points at published bytes");
  });
}
} // namespace

BOOL WINAPI contract_flush(HANDLE handle) {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)))
    return FALSE;
  const bool directory = (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  flushes.push_back(directory);
  if (!directory && !pin_from.empty() && !pin_attempted) {
    pin_attempted = true;
    pin_refused = !MoveFileExW(pin_from.c_str(), pin_to.c_str(), 0) && GetLastError() == ERROR_SHARING_VIOLATION;
  }
  if (pending == (directory ? flush_fault::directory : flush_fault::regular)) {
    pending = flush_fault::none;
    fault_consumed = true;
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
  }
  return FlushFileBuffers(handle);
}

BOOL WINAPI contract_volume(HANDLE handle, LPWSTR volume, DWORD volume_size,
    LPDWORD serial, LPDWORD component_size, LPDWORD flags, LPWSTR filesystem,
    DWORD filesystem_size) {
  const BOOL result = GetVolumeInformationByHandleW(handle, volume, volume_size, serial,
      component_size, flags, filesystem, filesystem_size);
  if (result && force_non_ntfs && filesystem && filesystem_size >= 6)
    std::wmemcpy(filesystem, L"FAT32", 6);
  return result;
}

BOOL WINAPI contract_close(HANDLE handle) {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  const bool regular = GetFileType(handle) == FILE_TYPE_DISK &&
      GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) &&
      !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
  const BOOL closed = CloseHandle(handle);
  if (closed && regular && fail_published_close) {
    // Actually close our handle so a synthetic error cannot leak it. Only
    // publication-header close calls are redirected, not RAII fixture cleanup.
    fail_published_close = false;
    close_fault_consumed = true;
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
  }
  return closed;
}

int main() {
  try {
    fixture owned;
    run_contracts(owned);
    require(completed == 15, "Windows publication contract inventory changed");
    std::cout << "windows-durable-publication-ok: 15 contracts; 16 invalid paths\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "windows-durable-publication: " << error.what() << '\n';
    return 1;
  }
}

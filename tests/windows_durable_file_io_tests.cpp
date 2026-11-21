#include "../nilcompiler/src/secure_file_io.hpp"
#include "../nilcompiler/src/windows_private_entries.hpp"
#include "../common/private_directory.hpp"

#ifndef _WIN32
#error This contract requires the real Win32 filesystem and locking APIs.
#endif

#include <winioctl.h>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace entries = ny::detail::windows_private_entries;
using ny::detail::FileLockRegistry;
using nil::detail::storage_handle;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template<class Result>
void refused(const Result &result, const char *message = "rejection must have an error") {
  require(!result.ok && !result.error.empty(), message);
  require(result.error.find("nil-win-durable-") == std::string::npos &&
              result.error.find("private-marker") == std::string::npos,
          "rejection disclosed a private pathname");
}

void verdict(const ny::detail::FileCheck &result, bool expected) {
  require(result.ok && result.error.empty() && result.value == expected,
          "metadata/query verdict differs from its three-state contract");
}

unsigned completed = 0;
template<class Function>
void contract(const char *name, Function body) {
  body();
  ++completed;
  std::cout << "PASS " << name << '\n';
}

struct fixture {
  fs::path root = nil::detail::create_private_directory(fs::temp_directory_path(),
                                                       "nil-win-durable-");
  std::vector<fs::path> junctions;
  ~fixture() {
    // Remove the exact owned reparse entries before ordinary recursive cleanup.
    // In particular, cleanup must not traverse a junction's target.
    for (const auto &path : junctions) RemoveDirectoryW(path.c_str());
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

void write_file(const fs::path &path, const std::string &bytes, bool inherited = false) {
  nil::detail::owner_security identity;
  std::error_code error;
  require(identity.initialize(false, error), "initialize fixture identity");
  SECURITY_DESCRIPTOR descriptor{};
  require(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION),
          "initialize inherited fixture descriptor");
  require(SetSecurityDescriptorOwner(&descriptor, identity.sid(), FALSE),
          "set inherited fixture owner");
  // Omit the DACL only for the inherited-profile control; explicit ownership
  // avoids TokenOwner group defaults on an elevated CI worker.
  auto attributes = identity.attributes();
  if (inherited) attributes.lpSecurityDescriptor = &descriptor;
  storage_handle file(CreateFileW(path.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &attributes, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL, nullptr));
  require(file.get() != INVALID_HANDLE_VALUE, "create fixture file exclusively");
  DWORD count = 0;
  require(WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
              count == bytes.size(), "write fixture bytes");
}

std::string read_file(const fs::path &path) {
  require(fs::file_size(path) <= 4096, "fixture unexpectedly grew");
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "read fixture file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void make_public(const fs::path &path) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
      L"D:P(A;OICI;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr),
      "create public fixture descriptor");
  PACL acl = nullptr;
  BOOL present = FALSE, defaulted = FALSE;
  const bool extracted = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present;
  const auto result = extracted ? SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
      SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      nullptr, nullptr, acl, nullptr) : ERROR_INVALID_SECURITY_DESCR;
  LocalFree(descriptor);
  require(result == ERROR_SUCCESS, "apply public fixture ACL");
}

class restricted_identity {
public:
  restricted_identity() {
    HANDLE process = nullptr;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process),
            "open fixture process token");
    storage_handle source(process);
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(world);
    require(CreateWellKnownSid(WinWorldSid, nullptr, world, &size), "create restricting SID");
    SID_AND_ATTRIBUTES restricting{world, 0};
    require(CreateRestrictedToken(source.get(), DISABLE_MAX_PRIVILEGE, 0, nullptr,
        0, nullptr, 1, &restricting, &token_), "create restricted fixture identity");
  }
  ~restricted_identity() { if (token_) CloseHandle(token_); }
  template<class Function>
  auto run(Function body) {
    require(ImpersonateLoggedOnUser(token_), "impersonate restricted fixture identity");
    struct revert {
      ~revert() { if (!RevertToSelf()) std::terminate(); }
    } scope;
    return body();
  }
  DWORD open(const fs::path &path, DWORD access, DWORD creation = OPEN_EXISTING) {
    require(ImpersonateLoggedOnUser(token_), "impersonate restricted fixture identity");
    const HANDLE file = CreateFileW(path.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!RevertToSelf()) std::terminate();
    return error;
  }
private:
  HANDLE token_ = nullptr;
};

void junction(fixture &owned, const fs::path &path, const fs::path &target) {
  require(fs::create_directory(path), "create junction entry");
  owned.junctions.push_back(path);
  const std::wstring shown = fs::absolute(target).wstring();
  const std::wstring substitute = L"\\??\\" + shown;
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
  std::memcpy(reinterpret_cast<char *>(value->names) + value->print_offset,
              shown.data(), value->print_length);
  storage_handle handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  require(handle.get() != INVALID_HANDLE_VALUE, "open owned junction entry");
  DWORD returned = 0;
  require(DeviceIoControl(handle.get(), FSCTL_SET_REPARSE_POINT, value,
      static_cast<DWORD>(storage.size()), nullptr, 0, &returned, nullptr),
      "set junction without symlink privileges");
}

std::set<std::wstring> names(const fs::path &root) {
  std::set<std::wstring> result;
  for (const auto &entry : fs::directory_iterator(root)) result.insert(entry.path().filename().wstring());
  return result;
}

// Win32 command-line quoting, including backslashes immediately before quotes
// and the closing quote. No shell or command interpreter participates.
std::wstring quote(const std::wstring &argument) {
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (const auto ch : argument) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

class child {
public:
  child(const wchar_t *mode, const fs::path &path) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    ready_ = CreateEventW(&attributes, TRUE, FALSE, nullptr);
    stop_ = CreateEventW(&attributes, TRUE, FALSE, nullptr);
    try {
      require(ready_ && stop_, "create child coordination events");
      std::vector<wchar_t> executable(32768);
      const auto count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
      require(count && count < executable.size(), "locate current test executable");
      std::wstring command = quote(std::wstring(executable.data(), count)) + L" --child " +
          quote(mode) + L" " + quote(path.wstring()) + L" " +
          std::to_wstring(reinterpret_cast<std::uintptr_t>(ready_)) + L" " +
          std::to_wstring(reinterpret_cast<std::uintptr_t>(stop_));
      SIZE_T size = 0;
      InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
      require(size != 0, "size child handle whitelist");
      std::vector<unsigned char> buffer(size);
      auto *list = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
      require(InitializeProcThreadAttributeList(list, 1, 0, &size), "initialize child handle whitelist");
      HANDLE handles[]{ready_, stop_};
      const bool updated = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          handles, sizeof(handles), nullptr, nullptr) != 0;
      STARTUPINFOEXW startup{};
      startup.StartupInfo.cb = sizeof(startup);
      startup.lpAttributeList = list;
      PROCESS_INFORMATION process{};
      const bool started = updated && CreateProcessW(executable.data(), command.data(), nullptr,
          nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
          &startup.StartupInfo, &process);
      DeleteProcThreadAttributeList(list);
      require(started, "launch bounded owned child");
      process_ = process.hProcess;
      CloseHandle(process.hThread);
    } catch (...) {
      if (ready_) CloseHandle(ready_);
      if (stop_) CloseHandle(stop_);
      throw;
    }
  }
  ~child() {
    if (process_) {
      if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
        // This is only the handle returned for this exact child, never a name,
        // PID search, job shared with another process, or machine-wide kill.
        TerminateProcess(process_, 99);
        WaitForSingleObject(process_, 5000);
      }
      CloseHandle(process_);
    }
    CloseHandle(ready_);
    CloseHandle(stop_);
  }
  child(const child &) = delete;
  child &operator=(const child &) = delete;
  void ready() {
    HANDLE handles[]{ready_, process_};
    require(WaitForMultipleObjects(2, handles, FALSE, 5000) == WAIT_OBJECT_0,
            "child failed before lock-ready signal or exceeded deadline");
  }
  void finish() {
    require(SetEvent(stop_), "signal owned child completion");
    wait(0);
  }
  void wait(DWORD expected) {
    require(WaitForSingleObject(process_, 5000) == WAIT_OBJECT_0, "owned child exceeded deadline");
    DWORD code = 0;
    require(GetExitCodeProcess(process_, &code) && code == expected, "owned child contract failed");
  }
  void crash() {
    require(TerminateProcess(process_, 77), "terminate exact owned lock holder");
    wait(77);
  }
private:
  HANDLE ready_ = nullptr, stop_ = nullptr, process_ = nullptr;
};

int child_main(const std::wstring &mode, const fs::path &path, HANDLE ready, HANDLE stop) {
  if (mode == L"registry-blocked") {
    FileLockRegistry registry;
    refused(registry.acquire(path.u8string(), false));
    require(registry.empty(), "failed child acquisition retained a token");
    return 0;
  }
  if (mode == L"registry-hold") {
    FileLockRegistry registry;
    const auto lock = registry.acquire(path.u8string(), false);
    require(lock.ok, "child cannot acquire registry lock");
    require(SetEvent(ready), "signal registry lock readiness");
    require(WaitForSingleObject(stop, 15000) == WAIT_OBJECT_0, "child stop deadline");
    require(registry.release(lock.token).ok, "release child registry lock");
    return 0;
  }
  require(mode == L"raw-blocked" || mode == L"raw-hold", "unknown child operation");
  storage_handle handle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  require(handle.get() != INVALID_HANDLE_VALUE, "external lock must open backing file");
  OVERLAPPED overlap{};
  const bool acquired = LockFileEx(handle.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                                   0, 1, 0, &overlap) != 0;
  const auto error = GetLastError();
  if (mode == L"raw-blocked") {
    if (acquired) UnlockFileEx(handle.get(), 0, 1, 0, &overlap);
    require(!acquired && error == ERROR_LOCK_VIOLATION,
            "external byte-zero contender must fail specifically on lock contention");
    return 0;
  }
  require(acquired, "external child cannot lock byte zero");
  require(SetEvent(ready), "signal external lock readiness");
  require(WaitForSingleObject(stop, 15000) == WAIT_OBJECT_0, "external child stop deadline");
  require(UnlockFileEx(handle.get(), 0, 1, 0, &overlap), "release external child lock");
  return 0;
}

void private_contracts(fixture &owned) {
  const auto &root = owned.root;
  const auto directory = root / "private-marker-directory";
  const auto file = root / "private-marker-file";
  contract("exclusive-owner-only-create", [&] {
    require(ny::detail::create_private_directory_utf8(directory.u8string()).ok,
            "create private directory");
    verdict(ny::detail::private_directory_utf8(directory.u8string()), true);
    write_file(file, "retained\n");
    verdict(ny::detail::private_regular_file_utf8(file.u8string()), true);
    refused(ny::detail::create_private_directory_utf8(directory.u8string()));
    refused(ny::detail::create_private_directory_utf8(file.u8string()));
    require(read_file(file) == "retained\n", "exclusive creation clobbered bytes");
  });
  contract("inherited-owner-only-profile", [&] {
    const auto inherited = directory / "inherited-file";
    write_file(inherited, "inherited", true);
    verdict(ny::detail::private_regular_file_utf8(inherited.u8string()), true);
    nil::detail::owner_security identity;
    std::error_code error;
    require(identity.initialize(true, error), "initialize inherited directory identity");
    SECURITY_DESCRIPTOR descriptor{};
    require(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorOwner(&descriptor, identity.sid(), FALSE), "inherited directory owner");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
    const auto nested = directory / "inherited-directory";
    require(CreateDirectoryW(nested.c_str(), &attributes), "create inherited directory");
    verdict(ny::detail::private_directory_utf8(nested.u8string()), true);
    FileLockRegistry registry;
    const auto lock = registry.acquire(inherited.u8string(), false);
    require(lock.ok && registry.release(lock.token).ok, "lock inherited owner-only file");
  });
  contract("public-parent-private-creation-access-check", [&] {
    const auto shared = root / "public-parent";
    require(fs::create_directory(shared), "create public parent");
    make_public(shared);
    const auto public_file = shared / "control";
    write_file(public_file, "public");
    make_public(public_file);
    restricted_identity reader;
    require(reader.open(public_file, FILE_READ_DATA) == ERROR_SUCCESS &&
            reader.open(public_file, FILE_WRITE_DATA) == ERROR_SUCCESS,
            "restricted reader/writer negative control must have access");
    const auto secret = shared / "private-child";
    require(ny::detail::create_private_directory_utf8(secret.u8string()).ok,
            "create private child under public parent");
    const auto payload = secret / "payload";
    write_file(payload, "private", true);
    require(reader.open(payload, FILE_READ_DATA) == ERROR_ACCESS_DENIED &&
            reader.open(payload, FILE_WRITE_DATA) == ERROR_ACCESS_DENIED &&
            reader.open(secret / "injection", FILE_WRITE_DATA, CREATE_NEW) == ERROR_ACCESS_DENIED,
            "private creation must exclude restricted reads, writes, and child injection");
  });
  contract("public-acl-false-and-lock-refusal", [&] {
    const auto loose_file = root / "private-marker-public-file";
    const auto loose_dir = root / "private-marker-public-directory";
    write_file(loose_file, "unchanged");
    require(ny::detail::create_private_directory_utf8(loose_dir.u8string()).ok, "public directory fixture");
    make_public(loose_file);
    make_public(loose_dir);
    verdict(ny::detail::private_regular_file_utf8(loose_file.u8string()), false);
    verdict(ny::detail::private_directory_utf8(loose_dir.u8string()), false);
    FileLockRegistry registry;
    refused(registry.acquire(loose_file.u8string(), false));
    require(registry.empty() && read_file(loose_file) == "unchanged", "public refusal changed fixture");
  });
  contract("wrong-owner-descriptor-false", [&] {
    // A normal account cannot assign an arbitrary foreign owner to an NTFS
    // file. Exercise the same production predicate with this real file's ACL
    // and a well-known foreign owner; no privilege or account setup is needed.
    nil::detail::owner_security identity;
    std::error_code error;
    require(identity.initialize(false, error), "initialize descriptor identity");
    PSECURITY_DESCRIPTOR raw = nullptr;
    PACL acl = nullptr;
    PSID owner = nullptr;
    require(GetNamedSecurityInfoW(file.c_str(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
        &acl, nullptr, &raw) == ERROR_SUCCESS, "read real fixture security descriptor");
    struct descriptor_owner { PSECURITY_DESCRIPTOR value; ~descriptor_owner() { LocalFree(value); } } held{raw};
    verdict(entries::inspect_descriptor(raw, identity.sid(), false, "test.descriptor"), true);
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(world);
    require(CreateWellKnownSid(WinWorldSid, nullptr, world, &size), "create foreign owner SID");
    SECURITY_DESCRIPTOR altered{};
    require(InitializeSecurityDescriptor(&altered, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorOwner(&altered, world, FALSE) &&
        SetSecurityDescriptorDacl(&altered, TRUE, acl, FALSE) &&
        SetSecurityDescriptorControl(&altered, SE_DACL_PROTECTED, SE_DACL_PROTECTED),
        "construct wrong-owner descriptor control");
    verdict(entries::inspect_descriptor(&altered, identity.sid(), false, "test.descriptor"), false);
    require(SetSecurityDescriptorOwner(&altered, owner, FALSE), "restore matching owner control");
    verdict(entries::inspect_descriptor(&altered, identity.sid(), false, "test.descriptor"), true);
    refused(entries::inspect_descriptor(nullptr, identity.sid(), false, "test.descriptor"));
    refused(entries::inspect_descriptor(raw, nullptr, false, "test.descriptor"));
    verdict(ny::detail::private_regular_file_utf8(file.u8string()), true);
  });
  contract("cross-kind-false", [&] {
    verdict(ny::detail::private_regular_file_utf8(directory.u8string()), false);
    verdict(ny::detail::private_directory_utf8(file.u8string()), false);
    FileLockRegistry registry;
    refused(registry.acquire(directory.u8string(), false));
    refused(registry.acquire(directory.u8string(), true));
  });
  contract("junction-no-follow-and-no-adoption", [&] {
    const auto link = root / "private-marker-junction";
    junction(owned, link, directory);
    verdict(ny::detail::private_directory_utf8(link.u8string()), false);
    verdict(ny::detail::private_regular_file_utf8(link.u8string()), false);
    refused(ny::detail::create_private_directory_utf8(link.u8string()));
    FileLockRegistry registry;
    refused(registry.acquire(link.u8string(), false));
    refused(registry.acquire(link.u8string(), true));
    verdict(ny::detail::private_directory_utf8(directory.u8string()), true);
  });
  contract("single-link-required", [&] {
    const auto alias = root / "private-marker-hardlink";
    require(CreateHardLinkW(alias.c_str(), file.c_str(), nullptr), "create hard-link fixture");
    verdict(ny::detail::private_regular_file_utf8(file.u8string()), false);
    verdict(ny::detail::private_regular_file_utf8(alias.u8string()), false);
    FileLockRegistry registry;
    refused(registry.acquire(file.u8string(), false));
    refused(registry.acquire(alias.u8string(), false));
    require(DeleteFileW(alias.c_str()), "remove owned hard link");
    verdict(ny::detail::private_regular_file_utf8(file.u8string()), true);
    require(read_file(file) == "retained\n", "hard-link rejection modified payload");
  });
  contract("missing-and-uninspectable-are-errors", [&] {
    const auto missing = root / "private-marker-missing";
    refused(ny::detail::private_regular_file_utf8(missing.u8string()), "missing regular metadata must be Error");
    refused(ny::detail::private_directory_utf8(missing.u8string()), "missing directory metadata must be Error");
    FileLockRegistry registry;
    refused(registry.acquire(missing.u8string(), false), "missing existing lock must be Error");
    require(!fs::exists(missing), "open-existing created a missing lock");
    const auto unreadable = root / "private-marker-uninspectable";
    write_file(unreadable, "uninspectable");
    nil::detail::owner_security identity;
    std::error_code error;
    require(identity.initialize(false, error), "initialize uninspectable fixture identity");
    LPWSTR sid = nullptr;
    require(ConvertSidToStringSidW(identity.sid(), &sid), "format fixture owner SID");
    const std::wstring sddl = L"D:P(D;;RC;;;OW)(A;;FA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
        SDDL_REVISION_1, &descriptor, nullptr), "construct uninspectable fixture ACL");
    PACL acl = nullptr;
    BOOL present = FALSE, defaulted = FALSE;
    const bool extracted = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present;
    const DWORD applied = extracted ? SetNamedSecurityInfoW(const_cast<LPWSTR>(unreadable.c_str()),
        SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr) : ERROR_INVALID_SECURITY_DESCR;
    LocalFree(descriptor);
    require(applied == ERROR_SUCCESS, "deny READ_CONTROL on only the owned fixture");
    // OWNER RIGHTS removes the owner's implicit READ_CONTROL grant. This is
    // a real inspection failure, unlike metadata opens which bypass data
    // sharing restrictions. DELETE/WRITE_DAC remain available for cleanup.
    // An elevated runner may have backup privileges enabled. Disable them in
    // this owned impersonation token rather than assuming machine policy.
    restricted_identity restricted;
    refused(restricted.run([&] { return ny::detail::private_regular_file_utf8(unreadable.u8string()); }),
            "uninspectable regular metadata must be Error");
    refused(restricted.run([&] { return ny::detail::private_directory_utf8(unreadable.u8string()); }),
            "uninspectable directory metadata must be Error");
    refused(restricted.run([&] { return registry.acquire(unreadable.u8string(), false); }),
            "uninspectable existing lock must be Error");
    require(registry.empty(), "inspection error retained registry state");
  });
  contract("utf8-without-code-page-conversion", [&] {
    const auto unicode = root / fs::u8path(u8"caf\u00e9-\u65e5\U0001f512");
    require(ny::detail::create_private_directory_utf8(unicode.u8string()).ok, "UTF-8 directory creation");
    verdict(ny::detail::private_directory_utf8(unicode.u8string()), true);
    const auto name = unicode / fs::u8path(u8"\u00e9.lock");
    FileLockRegistry registry;
    const auto lock = registry.acquire(name.u8string(), true);
    require(lock.ok, "UTF-8 lock creation");
    verdict(ny::detail::private_regular_file_utf8(name.u8string()), true);
    require(registry.release(lock.token).ok && fs::exists(name), "UTF-8 lock release");
  });
  contract("invalid-paths-no-normalization-or-mutation", [&] {
    const auto prefix = (root / "private-marker-invalid").u8string();
    const auto before = names(root);
    const std::vector<std::string> paths{
        "", prefix + std::string("\0suffix", 7), prefix + static_cast<char>(0xff),
        prefix + ":stream", prefix + ".", prefix + " ", prefix + "/", prefix + "\\",
        (root / "CON").u8string(), (root / "nul.txt").u8string(),
        (root / "COM1.log").u8string(), (root / fs::u8path(u8"LPT\u00b9.txt")).u8string(),
        (root / "CONIN$").u8string(), root.u8string() + "/./private-marker-invalid",
        root.u8string() + "/../private-marker-invalid", "\\\\?\\" + prefix,
        "\\\\.\\NUL", "C:private-marker-invalid"};
    require(paths.size() == 18, "invalid-path inventory changed");
    FileLockRegistry registry;
    for (const auto &path : paths) {
      refused(ny::detail::create_private_directory_utf8(path));
      refused(ny::detail::private_regular_file_utf8(path));
      refused(ny::detail::private_directory_utf8(path));
      refused(registry.acquire(path, false));
      refused(registry.acquire(path, true));
    }
    require(names(root) == before && registry.empty(), "invalid paths mutated the owned namespace");
    require(!fs::exists(root / "private-marker-invalid"), "invalid path was truncated or normalized");
  });
}

void lock_contracts(const fs::path &root) {
  contract("registry-ownership-stale-and-monotonic-tokens", [&] {
    FileLockRegistry first, second;
    const auto path = root / "tokens.lock";
    for (const auto token : {std::int64_t(0), std::int64_t(-1), std::numeric_limits<std::int64_t>::max()}) {
      verdict(first.is_held(token), false);
      refused(first.release(token));
    }
    const auto lock = first.acquire(path.u8string(), true);
    require(lock.ok && lock.token > 0 && !first.empty(), "acquire initial token");
    verdict(first.is_held(lock.token), true);
    verdict(second.is_held(lock.token), false);
    refused(second.release(lock.token));
    verdict(first.is_held(lock.token), true);
    refused(second.acquire(path.u8string(), false));
    refused(first.acquire(path.u8string(), false));
    require(first.release(lock.token).ok, "release initial token");
    verdict(first.is_held(lock.token), false);
    refused(first.release(lock.token));
    const auto next = second.acquire(path.u8string(), false);
    require(next.ok && next.token > lock.token, "tokens must be process-monotonic across registries");
    verdict(first.is_held(next.token), false);
    require(second.release(next.token).ok && first.empty() && second.empty(), "release monotonic token");
  });
  contract("no-clobber-create-and-payload-preservation", [&] {
    const auto path = root / "private-marker-existing.lock";
    write_file(path, "preserve this payload");
    FileLockRegistry registry;
    refused(registry.acquire(path.u8string(), true));
    require(read_file(path) == "preserve this payload", "lock creation truncated an existing file");
    const auto lock = registry.acquire(path.u8string(), false);
    require(lock.ok && registry.release(lock.token).ok, "acquire/release existing file");
    require(read_file(path) == "preserve this payload", "locking modified an existing payload");
    const auto fresh = root / "fresh.lock";
    const auto created = registry.acquire(fresh.u8string(), true);
    require(created.ok, "exclusively create fresh lock");
    verdict(ny::detail::private_regular_file_utf8(fresh.u8string()), true);
    refused(registry.acquire(fresh.u8string(), true));
    require(registry.release(created.token).ok && fs::file_size(fresh) == 0,
            "fresh lock must remain an empty private file after release");
  });
  contract("live-handle-pins-final-namespace-entry", [&] {
    const auto path = root / "pinned.lock";
    const auto replacement = root / "replacement.lock";
    const auto moved = root / "moved.lock";
    write_file(path, "pinned");
    write_file(replacement, "replacement");
    FileLockRegistry registry;
    const auto lock = registry.acquire(path.u8string(), false);
    require(lock.ok, "acquire namespace pin");
    require(!DeleteFileW(path.c_str()) && GetLastError() == ERROR_SHARING_VIOLATION,
            "live lock must deny deletion of its backing entry");
    require(!MoveFileExW(path.c_str(), moved.c_str(), 0) && GetLastError() == ERROR_SHARING_VIOLATION,
            "live lock must deny renaming of its backing entry");
    const BOOL replaced = MoveFileExW(replacement.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    const DWORD replacement_error = GetLastError();
    require(!replaced && (replacement_error == ERROR_SHARING_VIOLATION ||
                         replacement_error == ERROR_ACCESS_DENIED),
            "live lock must deny replacement of its backing entry");
    verdict(registry.is_held(lock.token), true);
    require(registry.release(lock.token).ok, "release namespace pin");
    require(read_file(path) == "pinned" && read_file(replacement) == "replacement" &&
            !fs::exists(moved), "namespace contention changed existing entries");
    require(MoveFileExW(path.c_str(), moved.c_str(), 0), "released handle must stop pinning the name");
  });
  contract("clear-retires-all-tokens-and-closes-handles", [&] {
    FileLockRegistry first, second;
    const auto a = root / "clear-a.lock", b = root / "clear-b.lock";
    const auto one = first.acquire(a.u8string(), true), two = first.acquire(b.u8string(), true);
    require(one.ok && two.ok, "acquire two clear fixtures");
    first.clear();
    first.clear();
    verdict(first.is_held(one.token), false);
    verdict(first.is_held(two.token), false);
    refused(first.release(one.token));
    require(first.empty(), "clear must empty registry");
    const auto after_a = second.acquire(a.u8string(), false), after_b = second.acquire(b.u8string(), false);
    require(after_a.ok && after_b.ok && after_a.token > two.token && after_b.token > after_a.token,
            "clear must release every OS lock without recycling tokens");
    second.clear();
    require(DeleteFileW(a.c_str()) && DeleteFileW(b.c_str()), "clear must close namespace-pinning handles");
  });
  contract("registry-destruction-releases-os-lock", [&] {
    const auto path = root / "destructor.lock";
    std::int64_t old = 0;
    {
      FileLockRegistry transient;
      const auto lock = transient.acquire(path.u8string(), true);
      require(lock.ok, "acquire destruction fixture");
      old = lock.token;
    }
    FileLockRegistry next;
    const auto lock = next.acquire(path.u8string(), false);
    require(lock.ok && lock.token > old, "registry destruction must release its OS lock");
    verdict(next.is_held(old), false);
    require(next.release(lock.token).ok && DeleteFileW(path.c_str()), "destruction must leave no open handles");
  });
  contract("cross-process-registry-contention", [&] {
    const auto path = root / "process-registry.lock";
    FileLockRegistry registry;
    const auto lock = registry.acquire(path.u8string(), true);
    require(lock.ok, "acquire parent process lock");
    child contender(L"registry-blocked", path);
    contender.wait(0);
    verdict(registry.is_held(lock.token), true);
    require(registry.release(lock.token).ok, "release parent process lock");
  });
  contract("external-lockfileex-contends-with-registry", [&] {
    const auto path = root / "process-external-contender.lock";
    FileLockRegistry registry;
    const auto lock = registry.acquire(path.u8string(), true);
    require(lock.ok, "acquire external contention fixture");
    child contender(L"raw-blocked", path);
    contender.wait(0);
    verdict(registry.is_held(lock.token), true);
    require(registry.release(lock.token).ok, "release external contention fixture");
  });
  contract("registry-contends-with-external-lockfileex", [&] {
    const auto path = root / "process-external-owner.lock";
    write_file(path, "external payload");
    child external(L"raw-hold", path);
    external.ready();
    FileLockRegistry registry;
    refused(registry.acquire(path.u8string(), false));
    require(registry.empty(), "external contention retained registry state");
    external.finish();
    const auto lock = registry.acquire(path.u8string(), false);
    require(lock.ok && registry.release(lock.token).ok, "acquire after external unlock");
    require(read_file(path) == "external payload", "external cooperation changed payload");
  });
  contract("child-normal-release-and-process-death-release", [&] {
    const auto path = root / "process-death.lock";
    write_file(path, "death control");
    FileLockRegistry registry;
    {
      child normal(L"registry-hold", path);
      normal.ready();
      refused(registry.acquire(path.u8string(), false));
      normal.finish();
    }
    const auto after_normal = registry.acquire(path.u8string(), false);
    require(after_normal.ok && registry.release(after_normal.token).ok, "acquire after normal child exit");
    {
      child crashed(L"registry-hold", path);
      crashed.ready();
      refused(registry.acquire(path.u8string(), false));
      crashed.crash();
    }
    // Windows may finish releasing outstanding process file locks shortly
    // after its process handle becomes signalled; retry only within 2 seconds.
    ny::detail::FileLockResult recovered;
    const auto deadline = GetTickCount64() + 2000;
    do {
      recovered = registry.acquire(path.u8string(), false);
      if (recovered.ok) break;
      Sleep(10);
    } while (GetTickCount64() < deadline);
    require(recovered.ok && registry.release(recovered.token).ok, "process death must release its lock");
    require(read_file(path) == "death control", "process cleanup changed backing bytes");
  });
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc == 6 && std::wstring(argv[1]) == L"--child") {
      return child_main(argv[2], fs::path(argv[3]),
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(argv[4]))),
          reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(argv[5]))));
    }
    require(argc == 1, "unexpected contract arguments");
    fixture owned;
    private_contracts(owned);
    lock_contracts(owned.root);
    require(completed == 20, "Windows durable contract inventory changed");
    std::cout << "windows-durable-file-io-ok: 20 contracts; 18 invalid paths\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "windows-durable-file-io: " << error.what() << '\n';
    return 1;
  }
}

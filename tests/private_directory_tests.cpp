#include "../common/private_directory.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void write_file(const fs::path &path) {
  std::ofstream stream(path, std::ios::binary);
  stream << "private-directory-contract\n";
  stream.close();
  require(!stream.fail(), "cannot write fixture");
}
struct fixture {
  fs::path root = nil::detail::create_private_directory(fs::temp_directory_path(), "nil-private-test-");
  ~fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
};

#ifdef _WIN32
using nil::detail::storage_handle;

void grant_public_fixture_access(const fs::path &path) {
  // Only dummy data in this exclusively-created fixture is made public. A
  // restricting Everyone SID provides an independent access-check boundary;
  // no additional account or machine-wide ACL/policy changes are needed.
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  PACL old_acl = nullptr, new_acl = nullptr;
  require(GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, &old_acl, nullptr, &descriptor) == ERROR_SUCCESS, "read fixture ACL");
  BYTE world[SECURITY_MAX_SID_SIZE]{};
  DWORD size = sizeof(world);
  require(CreateWellKnownSid(WinWorldSid, nullptr, world, &size), "create Everyone SID");
  EXPLICIT_ACCESSW entry{};
  entry.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE;
  entry.grfAccessMode = GRANT_ACCESS;
  entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(world);
  const DWORD merged = SetEntriesInAclW(1, &entry, old_acl, &new_acl);
  DWORD applied = merged;
  if (merged == ERROR_SUCCESS)
    applied = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, new_acl, nullptr);
  if (new_acl) LocalFree(new_acl);
  LocalFree(descriptor);
  require(applied == ERROR_SUCCESS, "set public fixture ACL");
}

class restricted_identity {
public:
  restricted_identity() {
    HANDLE process = nullptr;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process),
            "open current token");
    storage_handle source(process);
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(world);
    require(CreateWellKnownSid(WinWorldSid, nullptr, world, &size), "create restricting SID");
    SID_AND_ATTRIBUTES restricting{world, 0};
    require(CreateRestrictedToken(source.get(), DISABLE_MAX_PRIVILEGE, 0, nullptr,
        0, nullptr, 1, &restricting, &token_), "create restricted token");
  }
  ~restricted_identity() { if (token_) CloseHandle(token_); }
  DWORD open(const fs::path &path, DWORD access, DWORD creation = OPEN_EXISTING) const {
    require(ImpersonateLoggedOnUser(token_), "impersonate restricted token");
    const HANDLE handle = CreateFileW(path.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    // Never continue running cleanup or diagnostics under the restricted token.
    if (!RevertToSelf()) std::terminate();
    return error;
  }
private:
  HANDLE token_ = nullptr;
};

void windows_contract(const fs::path &root) {
  std::error_code error;
  const auto shared = root / fs::u8path(u8"public parent caf\u00e9");
  require(fs::create_directory(shared), "create shared fixture parent");
  grant_public_fixture_access(shared);
  const auto control = shared / "public.txt";
  write_file(control);
  restricted_identity restricted;
  require(restricted.open(control, FILE_READ_DATA) == ERROR_SUCCESS,
          "negative control must permit restricted reader");
  require(restricted.open(control, FILE_WRITE_DATA) == ERROR_SUCCESS,
          "negative control must permit restricted writer");
  {
    // READ_DATA is intentional: metadata-only handles do not establish the
    // same sharing exclusion. Exercise the seal helper's retained-handle mode.
    storage_handle held(CreateFileW(control.c_str(), READ_CONTROL | WRITE_DAC |
        FILE_READ_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    require(held.get() != INVALID_HANDLE_VALUE, "hold readable staging file");
    const HANDLE deletion = CreateFileW(control.c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD deletion_error = GetLastError();
    if (deletion != INVALID_HANDLE_VALUE) CloseHandle(deletion);
    require(deletion == INVALID_HANDLE_VALUE && deletion_error == ERROR_SHARING_VIOLATION,
            "held staging file must deny delete access");
  }
  const auto private_dir = nil::detail::create_private_directory(shared, "private-");
  const auto secret = private_dir / "private.txt";
  write_file(secret);
  require(restricted.open(secret, FILE_READ_DATA) == ERROR_ACCESS_DENIED,
          "private child must deny restricted reader");
  require(restricted.open(secret, FILE_WRITE_DATA) == ERROR_ACCESS_DENIED,
          "private child must deny restricted writer");
  require(restricted.open(private_dir / "injected.txt", FILE_WRITE_DATA, CREATE_NEW) == ERROR_ACCESS_DENIED,
          "private directory must deny restricted creation");
  require(fs::create_directory(private_dir / "nested"), "create nested directory");
  write_file(private_dir / "nested" / "nested.txt");
  require(restricted.open(private_dir / "nested" / "nested.txt", FILE_READ_DATA) == ERROR_ACCESS_DENIED,
          "private ACL must propagate to nested descendants");
  require(!nil::detail::create_owner_only_directory(private_dir, error) &&
      (error.value() == ERROR_ALREADY_EXISTS || error.value() == ERROR_FILE_EXISTS),
      "existing directory must not be adopted");
  require(fs::is_regular_file(secret), "collision must preserve existing contents");
  require(!nil::detail::create_owner_only_directory(control, error), "existing file must not be adopted");
  // A copied public interpreter can retain its source DACL on Windows. Seal
  // that regular file before appending any private bundle bytes.
  const auto copied = shared / "copied-stub.exe";
  require(CopyFileW(control.c_str(), copied.c_str(), TRUE), "copy public fixture stub");
  require(restricted.open(copied, FILE_READ_DATA) == ERROR_SUCCESS, "copied stub negative control");
  require(nil::detail::secure_owner_only_file(copied, error), "seal copied stub");
  require(restricted.open(copied, FILE_READ_DATA) == ERROR_ACCESS_DENIED, "sealed stub must deny reader");
  require(restricted.open(copied, FILE_WRITE_DATA) == ERROR_ACCESS_DENIED, "sealed stub must deny writer");
  nil::detail::owner_security creator;
  require(creator.initialize(false, error), "read effective creator identities");
  require(SetNamedSecurityInfoW(const_cast<LPWSTR>(copied.c_str()), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION, creator.default_owner(), nullptr, nullptr, nullptr) == ERROR_SUCCESS,
      "restore fixture default owner (may differ from TokenUser)");
  PSECURITY_DESCRIPTOR read_only = nullptr;
  require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FRFX;;;WD)",
      SDDL_REVISION_1, &read_only, nullptr), "read-only source descriptor");
  PACL read_only_acl = nullptr;
  BOOL present = FALSE, defaulted = FALSE;
  require(GetSecurityDescriptorDacl(read_only, &present, &read_only_acl, &defaulted) && present,
          "read-only source ACL");
  const DWORD readonly_status = SetNamedSecurityInfoW(const_cast<LPWSTR>(copied.c_str()), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      nullptr, nullptr, read_only_acl, nullptr);
  LocalFree(read_only);
  require(readonly_status == ERROR_SUCCESS, "make owned stub read-only to public");
  require(restricted.open(copied, FILE_READ_DATA) == ERROR_SUCCESS, "read-only negative control");
  require(nil::detail::secure_owner_only_file(copied, error), "seal default-owned read-only stub without initial WRITE_OWNER");
  require(restricted.open(copied, FILE_READ_DATA) == ERROR_ACCESS_DENIED, "sealed read-only stub must deny reader");
  require(!nil::detail::secure_owner_only_file(shared, error), "file seal must reject directories");
  require(restricted.open(control, FILE_READ_DATA) == ERROR_SUCCESS, "directory rejection must preserve parent ACL");
  const auto link = shared / "stub-link.exe";
  require(CreateSymbolicLinkW(link.c_str(), control.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE),
          "create required symlink fixture (enable Developer Mode or symlink privilege)");
  require(!nil::detail::secure_owner_only_file(link, error), "file seal must reject reparse points");
  require(restricted.open(control, FILE_READ_DATA) == ERROR_SUCCESS, "reparse rejection must preserve target ACL");
}
#endif
} // namespace

int main() {
  try {
    fixture test;
    const auto first = nil::detail::create_private_directory(test.root, "child-");
    const auto second = nil::detail::create_private_directory(test.root, "child-");
    require(first != second && fs::is_directory(first) && fs::is_directory(second), "fresh directories");
    write_file(first / "sentinel");
#ifdef _WIN32
    windows_contract(test.root);
#else
    struct stat info{};
    require(::stat(first.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700,
            "private directory must have POSIX mode 0700");
#endif
    std::cout << "private directory contract passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "private directory contract: " << error.what() << '\n';
    return 1;
  }
}

#ifndef NIL_COMMON_WINDOWS_PRIVATE_STORAGE_HPP
#define NIL_COMMON_WINDOWS_PRIVATE_STORAGE_HPP

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace nil::detail {

inline bool windows_storage_error(std::error_code &error, DWORD code = GetLastError()) {
  error = std::error_code(static_cast<int>(code), std::system_category());
  return false;
}

class storage_handle {
public:
  explicit storage_handle(HANDLE value) : value_(value) {}
  ~storage_handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  storage_handle(const storage_handle &) = delete;
  storage_handle &operator=(const storage_handle &) = delete;
  HANDLE get() const { return value_; }
private:
  HANDLE value_;
};

class owner_security {
public:
  ~owner_security() { if (descriptor_) LocalFree(descriptor_); }
  owner_security() = default;
  owner_security(const owner_security &) = delete;
  owner_security &operator=(const owner_security &) = delete;

  bool initialize(bool directory, std::error_code &error) {
    HANDLE raw_token = nullptr;
    // Use the effective identity, including an impersonating caller. Never
    // silently fall back to the process identity after a token-query failure.
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_token)) {
      if (GetLastError() != ERROR_NO_TOKEN ||
          !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
        return windows_storage_error(error);
    }
    storage_handle token(raw_token);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > 65536)
      return windows_storage_error(error, ERROR_INVALID_DATA);
    user_.resize(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, user_.data(), bytes, &bytes))
      return windows_storage_error(error);
    sid_ = reinterpret_cast<TOKEN_USER *>(user_.data())->User.Sid;
    bytes = 0;
    GetTokenInformation(token.get(), TokenOwner, nullptr, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0 || bytes > 65536)
      return windows_storage_error(error, ERROR_INVALID_DATA);
    default_owner_.resize(bytes);
    if (!GetTokenInformation(token.get(), TokenOwner, default_owner_.data(), bytes, &bytes))
      return windows_storage_error(error);
    default_owner_sid_ = reinterpret_cast<TOKEN_OWNER *>(default_owner_.data())->Owner;
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid_, &text)) return windows_storage_error(error);
    std::wstring sid_text;
    try { sid_text = text; } catch (...) { LocalFree(text); throw; }
    LocalFree(text);
    // Protected DACL: do not merge a shared parent's permissive inheritable
    // ACEs. Child files/directories inherit only this effective user's access.
    const auto sddl = L"O:" + sid_text + L"D:P(A;" +
                      std::wstring(directory ? L"OICI" : L"") + L";FA;;;" + sid_text + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
      return windows_storage_error(error);
    BOOL present = FALSE, defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(descriptor_, &present, &acl_, &defaulted) ||
        !present || !acl_) return windows_storage_error(error, ERROR_INVALID_SECURITY_DESCR);
    error.clear();
    return true;
  }

  SECURITY_ATTRIBUTES attributes() const {
    return {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
  }
  PSID sid() const { return sid_; }
  PSID default_owner() const { return default_owner_sid_; }
  PACL acl() const { return acl_; }

  bool verify(HANDLE handle, bool directory, std::error_code &error) const {
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)))
      return windows_storage_error(error);
    if ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
      return windows_storage_error(error, ERROR_INVALID_DATA);
    PSECURITY_DESCRIPTOR actual = nullptr;
    PSID owner = nullptr;
    PACL acl = nullptr;
    const DWORD status = GetSecurityInfo(handle, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &acl, nullptr, &actual);
    if (status != ERROR_SUCCESS) return windows_storage_error(error, status);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    void *raw_ace = nullptr;
    bool valid = GetSecurityDescriptorControl(actual, &control, &revision) &&
        (control & SE_DACL_PROTECTED) && owner && EqualSid(owner, sid_) &&
        acl && IsValidAcl(acl) && acl->AceCount == 1 && GetAce(acl, 0, &raw_ace);
    if (valid) {
      const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(raw_ace);
      valid = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
          ace->Header.AceFlags == (directory ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0) &&
          ace->Mask == FILE_ALL_ACCESS &&
          EqualSid(const_cast<DWORD *>(&ace->SidStart), sid_);
    }
    LocalFree(actual);
    if (!valid) return windows_storage_error(error, ERROR_INVALID_SECURITY_DESCR);
    error.clear();
    return true;
  }
private:
  std::vector<unsigned char> user_;
  std::vector<unsigned char> default_owner_;
  PSID sid_ = nullptr;
  PSID default_owner_sid_ = nullptr;
  PACL acl_ = nullptr;
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};

// The descriptor is supplied at the creation boundary, not applied after a
// default-ACL creation window. Verify before any sensitive child is written;
// filesystems that ignore security descriptors fail closed. Parent ancestry
// must still be trusted: no DACL prevents an administrator or an ancestor owner
// from replacing paths, just as POSIX 0700 does not protect hostile ancestors.
inline bool create_owner_only_directory(const std::filesystem::path &path,
                                        std::error_code &error) {
  owner_security security;
  if (!security.initialize(true, error)) return false;
  auto attributes = security.attributes();
  if (!CreateDirectoryW(path.c_str(), &attributes)) return windows_storage_error(error);
  bool verified = false;
  {
    storage_handle handle(CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) windows_storage_error(error);
    else verified = security.verify(handle.get(), true, error);
  }
  if (!verified) RemoveDirectoryW(path.c_str()); // empty, exclusively created by this call
  return verified;
}

// For newly-created files within a caller-owned private staging directory:
// CopyFile can preserve the copied program's DACL. Seal the public interpreter
// stub before appending a bundled program, and verify native staging outputs.
inline bool secure_owner_only_file(const std::filesystem::path &path,
                                  std::error_code &error) {
  owner_security security;
  if (!security.initialize(false, error)) return false;
  storage_handle handle(CreateFileW(path.c_str(), READ_CONTROL | WRITE_DAC | FILE_READ_DATA |
      FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (handle.get() == INVALID_HANDLE_VALUE) return windows_storage_error(error);
  FILE_ATTRIBUTE_TAG_INFO info{};
  if (!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &info, sizeof(info)))
    return windows_storage_error(error);
  if (info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
    return windows_storage_error(error, ERROR_INVALID_DATA);
  // An owner can replace the DACL even when a copied public program grants
  // only read/execute access. Fresh Windows files may use TokenOwner (an
  // enabled group for an elevated token) rather than TokenUser. Accept only
  // those two creator identities. This check does not establish provenance by
  // itself: the caller must have exclusively staged the file in its directory.
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  PSID owner = nullptr;
  const DWORD owner_status = GetSecurityInfo(handle.get(), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor);
  if (owner_status != ERROR_SUCCESS) return windows_storage_error(error, owner_status);
  const bool user_owned = owner && EqualSid(owner, security.sid());
  const bool owned = user_owned || (owner && EqualSid(owner, security.default_owner()));
  LocalFree(descriptor);
  if (!owned) return windows_storage_error(error, ERROR_INVALID_OWNER);
  const DWORD status = SetSecurityInfo(handle.get(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
      nullptr, nullptr, security.acl(), nullptr);
  if (status != ERROR_SUCCESS) return windows_storage_error(error, status);
  if (!user_owned) {
    // The newly protected DACL now grants WRITE_OWNER. Reopen the same held
    // object (not its pathname), then make ownership as narrow as its DACL.
    // No privilege is enabled and the original handle still denies deletion.
    storage_handle writable_owner(ReOpenFile(handle.get(), WRITE_OWNER,
        FILE_SHARE_READ, FILE_FLAG_OPEN_REPARSE_POINT));
    if (writable_owner.get() == INVALID_HANDLE_VALUE) return windows_storage_error(error);
    const DWORD owner_change = SetSecurityInfo(writable_owner.get(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION, security.sid(), nullptr, nullptr, nullptr);
    if (owner_change != ERROR_SUCCESS) return windows_storage_error(error, owner_change);
  }
  return security.verify(handle.get(), false, error);
}

} // namespace nil::detail
#endif // _WIN32
#endif

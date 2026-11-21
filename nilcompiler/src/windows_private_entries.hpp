#ifndef NIL_WINDOWS_PRIVATE_ENTRIES_HPP
#define NIL_WINDOWS_PRIVATE_ENTRIES_HPP

#include "secure_file_io.hpp"

#ifdef _WIN32
#include "../../common/windows_private_storage.hpp"

#include <cstddef>
#include <string_view>

namespace ny::detail::windows_private_entries {

inline bool separator(wchar_t value) { return value == L'\\' || value == L'/'; }

inline wchar_t ascii_upper(wchar_t value) {
  return value >= L'a' && value <= L'z' ? value - L'a' + L'A' : value;
}

inline bool reserved_component(std::wstring_view component) {
  // Win32 recognizes device basenames even before an extension. Trim spaces
  // immediately before that extension too; never let normalization choose a
  // device after our checks. Superscript 1, 2 and 3 also name COM/LPT devices.
  auto base = component.substr(0, component.find(L'.'));
  while (!base.empty() && base.back() == L' ') base.remove_suffix(1);
  std::wstring upper;
  upper.reserve(base.size());
  for (const auto value : base) upper.push_back(ascii_upper(value));
  if (upper == L"CON" || upper == L"PRN" || upper == L"AUX" ||
      upper == L"NUL" || upper == L"CONIN$" || upper == L"CONOUT$" ||
      upper == L"CLOCK$")
    return true;
  if (upper.size() != 4 ||
      (upper.compare(0, 3, L"COM") != 0 && upper.compare(0, 3, L"LPT") != 0))
    return false;
  return (upper[3] >= L'0' && upper[3] <= L'9') || upper[3] == L'\u00b9' ||
         upper[3] == L'\u00b2' || upper[3] == L'\u00b3';
}

// Restricted ordinary DOS paths: relative names or drive-absolute names,
// either separator, and no normalization of components. UNC/device/extended
// namespaces, drive-relative/current-drive-rooted paths, ADS, dot/parent/empty
// components, reserved devices, leading spaces and trailing dots/spaces are
// rejected. Only an explicit drive root may end in a separator, and only when
// requested. This is a naming policy, not an ancestry sandbox: parents remain
// trusted. The UTF-8 runtime entry points validate before constructing a path;
// direct host callers must also supply well-formed UTF-16.
inline bool valid_path(const std::filesystem::path &path, bool allow_root = false) {
  const auto &name = path.native();
  if (name.empty() || name.size() > 32767 || separator(name.front())) return false;
  std::size_t begin = 0;
  if (name.size() >= 2 && name[1] == L':') {
    const auto drive = ascii_upper(name[0]);
    if (drive < L'A' || drive > L'Z' || name.size() < 3 || !separator(name[2]))
      return false;
    begin = 3;
    if (begin == name.size()) return allow_root;
  }
  for (std::size_t index = begin; index < name.size(); ++index) {
    const auto value = static_cast<unsigned>(name[index]);
    if (value < 32 || value == L'<' || value == L'>' || value == L':' ||
        value == L'"' || value == L'|' || value == L'?' || value == L'*')
      return false;
    if (value >= 0xd800 && value <= 0xdbff) {
      if (++index == name.size() || name[index] < 0xdc00 || name[index] > 0xdfff)
        return false;
    } else if (value >= 0xdc00 && value <= 0xdfff) {
      return false;
    }
  }
  while (begin < name.size()) {
    const auto end = name.find_first_of(L"\\/", begin);
    const auto count = (end == std::wstring::npos ? name.size() : end) - begin;
    const std::wstring_view component(name.data() + begin, count);
    if (component.empty() || component.front() == L' ' ||
        component.back() == L' ' || component.back() == L'.' ||
        reserved_component(component))
      return false;
    if (end == std::wstring::npos) return true;
    begin = end + 1;
  }
  return false;
}

inline FileCheck inspection_error(const char *operation) {
  return {false, false, std::string(operation) + ": cannot inspect entry"};
}

// Accept only the effective user's ownership and one full-access
// allow ACE for that same SID. The ACE is either explicit in a protected DACL,
// or inherited from an owner-only parent; directories must propagate it to
// files and directories. Additional/deny/object/callback/inherit-only ACEs,
// null/empty DACLs and default group ownership are not this private profile.
// Inheritance is an inspected current state, not a promise about future ACL
// changes. This read-only descriptor predicate is shared by the held-handle
// check and deterministic malformed/wrong-owner controls in the Windows tests.
inline FileCheck inspect_descriptor(PSECURITY_DESCRIPTOR descriptor,
                                   PSID expected_user, bool directory,
                                   const char *operation) {
  PSID owner = nullptr;
  PACL acl = nullptr;
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  BOOL present = FALSE, defaulted = FALSE;
  if (!descriptor || !IsValidSecurityDescriptor(descriptor) ||
      !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
      !GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) ||
      !GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted))
    return inspection_error(operation);
  if (!expected_user || !IsValidSid(expected_user))
    return inspection_error(operation);
  if (!owner || !IsValidSid(owner) || !EqualSid(owner, expected_user) ||
      !present || !acl || !IsValidAcl(acl) || acl->AceCount != 1)
    return {true, false, {}};
  void *raw = nullptr;
  if (!GetAce(acl, 0, &raw)) return inspection_error(operation);
  const auto *header = static_cast<const ACE_HEADER *>(raw);
  constexpr std::size_t sid_offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
  if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceSize < sid_offset + 8)
    return {true, false, {}};
  const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw);
  const BYTE propagation = directory ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
  const bool explicit_private = (control & SE_DACL_PROTECTED) != 0 &&
                                header->AceFlags == propagation;
  const bool inherited_private = header->AceFlags == (propagation | INHERITED_ACE);
  if ((!explicit_private && !inherited_private) || ace->Mask != FILE_ALL_ACCESS)
    return {true, false, {}};
  const auto *sid_bytes = reinterpret_cast<const BYTE *>(raw) + sid_offset;
  // Bound the variable-length SID inside its ACE before invoking SID helpers.
  const std::size_t sid_size = 8 + 4 * static_cast<std::size_t>(sid_bytes[1]);
  if (sid_size != header->AceSize - sid_offset)
    return {true, false, {}};
  const PSID trustee = const_cast<BYTE *>(sid_bytes);
  return {true, IsValidSid(trustee) && EqualSid(trustee, expected_user), {}};
}

// Inspect an already-held, no-follow handle without opening or changing a
// pathname. Retain that handle for any subsequent action; a path preflight
// cannot establish the identity of a later open.
inline FileCheck inspect_handle(HANDLE handle, bool directory,
                               const char *operation,
                               const nil::detail::owner_security &identity) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    return inspection_error(operation);
  SetLastError(ERROR_SUCCESS);
  const DWORD kind = GetFileType(handle);
  if (kind == FILE_TYPE_UNKNOWN && GetLastError() != ERROR_SUCCESS)
    return inspection_error(operation);
  if (kind != FILE_TYPE_DISK) return {true, false, {}};
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                  sizeof(attributes)))
    return inspection_error(operation);
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      bool(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
    return {true, false, {}};
  FILE_STANDARD_INFO standard{};
  if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                  sizeof(standard)))
    return inspection_error(operation);
  if (bool(standard.Directory) != directory || standard.DeletePending ||
      (!directory && standard.NumberOfLinks != 1))
    return {true, false, {}};

  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD status = GetSecurityInfo(handle, SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      nullptr, nullptr, nullptr, nullptr, &descriptor);
  if (status != ERROR_SUCCESS) return inspection_error(operation);
  struct descriptor_owner {
    PSECURITY_DESCRIPTOR value;
    ~descriptor_owner() { if (value) LocalFree(value); }
  } owned{descriptor};
  return inspect_descriptor(descriptor, identity.sid(), directory, operation);
}

inline FileCheck inspect_handle(HANDLE handle, bool directory, const char *operation) {
  nil::detail::owner_security identity;
  std::error_code error;
  if (!identity.initialize(directory, error)) return inspection_error(operation);
  return inspect_handle(handle, directory, operation, identity);
}

inline FileCheck inspect_path(const std::filesystem::path &path, bool directory,
                             const char *operation) {
  if (!valid_path(path, directory))
    return {false, false, std::string(operation) + ": invalid " +
                          (directory ? "directory" : "file") + " path"};
  nil::detail::storage_handle handle(CreateFileW(path.c_str(),
      READ_CONTROL | FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  return inspect_handle(handle.get(), directory, operation);
}

inline FileCheck inspect_regular_file(const std::filesystem::path &path) {
  return inspect_path(path, false, "file.is_private_regular_file");
}

inline FileCheck inspect_directory(const std::filesystem::path &path) {
  return inspect_path(path, true, "file.is_private_directory");
}

inline AtomicFileWrite create_directory(const std::filesystem::path &path) {
  constexpr const char *operation = "file.create_private_directory";
  if (!valid_path(path)) return {false, std::string(operation) + ": invalid directory path"};
  nil::detail::owner_security identity;
  std::error_code error;
  if (!identity.initialize(true, error))
    return {false, std::string(operation) + ": cannot establish owner identity"};
  auto attributes = identity.attributes();
  if (!CreateDirectoryW(path.c_str(), &attributes)) {
    const DWORD status = GetLastError();
    return {false, std::string(operation) +
        ((status == ERROR_ALREADY_EXISTS || status == ERROR_FILE_EXISTS)
             ? ": entry already exists" : ": cannot create directory")};
  }
  nil::detail::storage_handle handle(CreateFileW(path.c_str(),
      READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  const auto verified = inspect_handle(handle.get(), true, operation, identity);
  // Fail closed without path-based repair/removal. A post-creation failure can
  // leave the new entry; reopening permissively or deleting a replaced name
  // would be a different and weaker operation. Parents remain trusted.
  if (!verified.ok || !verified.value)
    return {false, std::string(operation) + ": created directory is not owner-only"};
  return {true, {}};
}

} // namespace ny::detail::windows_private_entries
#endif // _WIN32
#endif // NIL_WINDOWS_PRIVATE_ENTRIES_HPP

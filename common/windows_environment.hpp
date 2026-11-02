#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nil::detail {

inline std::optional<std::wstring> environment_utf16(const std::string &text) {
  if (text.empty()) return std::wstring{};
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return std::nullopt;
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (!size) return std::nullopt;
  std::wstring result(size, L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
          static_cast<int>(text.size()), result.data(), size) != size)
    return std::nullopt;
  return result;
}

inline std::string environment_utf8(const wchar_t *text, std::size_t length) {
  if (!length) return {};
  if (length > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::runtime_error("environment: host value is too large");
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
      static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (!size) throw std::runtime_error("environment: host value is not valid Unicode");
  std::string result(size, '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
          static_cast<int>(length), result.data(), size, nullptr, nullptr) != size)
    throw std::runtime_error("environment: cannot encode host value");
  return result;
}

// Read the OS environment, not the CRT's cached copy. In particular, an empty
// Win32 value is present; only ERROR_ENVVAR_NOT_FOUND means it is absent.
inline std::optional<std::string> windows_environment_get(const std::string &name) {
  const auto wide_name = environment_utf16(name);
  if (!wide_name)
    throw std::runtime_error("environment.get: variable name is not valid UTF-8");
  // Windows limits an individual value to 32,767 UTF-16 code units. A single
  // bounded read avoids the size-query/read race with another host thread.
  std::vector<wchar_t> buffer(32768);
  SetLastError(ERROR_SUCCESS);
  const DWORD size = GetEnvironmentVariableW(wide_name->c_str(), buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
  if (!size) {
    const auto error = GetLastError();
    if (error == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
    if (error != ERROR_SUCCESS)
      throw std::runtime_error("environment.get: host environment is unavailable");
    return std::string{};
  }
  if (size >= buffer.size())
    throw std::runtime_error("environment.get: host value exceeds its size limit");
  return environment_utf8(buffer.data(), size);
}

inline bool windows_environment_set(const std::string &name,
                                    const std::optional<std::string> &value) {
  const auto wide_name = environment_utf16(name);
  if (!wide_name) return false;
  if (!value) {
    if (SetEnvironmentVariableW(wide_name->c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
  }
  const auto wide_value = environment_utf16(*value);
  return wide_value && SetEnvironmentVariableW(wide_name->c_str(), wide_value->c_str()) != 0;
}

} // namespace nil::detail
#endif

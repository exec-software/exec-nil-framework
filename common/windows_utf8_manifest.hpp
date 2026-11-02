#ifndef NIL_COMMON_WINDOWS_UTF8_MANIFEST_HPP
#define NIL_COMMON_WINDOWS_UTF8_MANIFEST_HPP

#include <string_view>

namespace nil::detail {

// Generated executables need the same process encoding as the framework
// tools. Keep this built into both link frontends: an installed SDK must not
// depend on a manifest at a build-tree or source-tree path. The caller writes
// it inside its private staging directory and passes /MANIFEST:EMBED together
// with /MANIFESTINPUT to the Windows linker before atomic publication.
// See https://learn.microsoft.com/windows/apps/design/globalizing/use-utf8-code-page
// and cmake/utf8.manifest (the framework tools' matching manifest).
inline constexpr std::string_view windows_utf8_manifest = R"manifest(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
)manifest";

} // namespace nil::detail

#endif // NIL_COMMON_WINDOWS_UTF8_MANIFEST_HPP

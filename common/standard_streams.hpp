#ifndef NIL_COMMON_STANDARD_STREAMS_HPP
#define NIL_COMMON_STANDARD_STREAMS_HPP

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ny {

#ifdef _WIN32
namespace detail {
inline bool standard_stream_is_console(FILE *stream) noexcept {
  const int descriptor = _fileno(stream);
  if (descriptor < 0)
    return false;
  const auto handle = _get_osfhandle(descriptor);
  if (handle == -1)
    return false;
  DWORD mode = 0;
  // FILE_TYPE_CHAR also includes NUL. Inspect the CRT's actual handle, not
  // GetStdHandle: an embedding caller may have redirected stdio with _dup2.
  return GetConsoleMode(reinterpret_cast<HANDLE>(handle), &mode) != FALSE;
}
} // namespace detail
#endif

// Put the standard streams in binary mode, so a tool reads and writes the same
// bytes on every host.
//
// Windows C stdio defaults to text mode, which rewrites '\n' as "\r\n" on the
// way out and collapses "\r\n" back to '\n' on the way in. Both directions are
// wrong for everything here:
//
//   * --to-nildata and the JSON reports are data formats, so a consumer that
//     hashes them or counts their bytes gets a different answer per platform;
//   * a NIL program's output would differ from the same program's output on
//     Linux, so one recorded transcript cannot describe both;
//   * a source piped in through --source-stdin would arrive with its CRLF
//     endings silently rewritten, moving every byte offset the compiler then
//     reports back as a diagnostic, completion or span;
//   * console.read_line would hand a program different text than it gets on
//     Linux for the same input bytes;
//   * any payload holding a 0x0A or 0x0D byte would be corrupted in transit.
//
// Rust and Go make the same choice in both directions: no translation. A
// parser that wants to accept CRLF still can -- it just has to say so, rather
// than inheriting it from the platform.
//
// A Windows console has its own encoding, independent of the process UTF-8
// manifest. Select UTF-8 only for directions actually backed by a console;
// redirected files/pipes remain untouched byte streams. This changes the
// attached console (shared with other processes attached to that console),
// never the machine's locale, registry, or default console configuration.
// https://learn.microsoft.com/windows/console/console-code-pages
//
// Call this first in main(), before anything is read or written.
inline void use_untranslated_standard_streams() noexcept {
#ifdef _WIN32
  (void)_setmode(_fileno(stdin), _O_BINARY);
  (void)_setmode(_fileno(stdout), _O_BINARY);
  (void)_setmode(_fileno(stderr), _O_BINARY);
  if (detail::standard_stream_is_console(stdin))
    (void)SetConsoleCP(CP_UTF8);
  if (detail::standard_stream_is_console(stdout) ||
      detail::standard_stream_is_console(stderr))
    (void)SetConsoleOutputCP(CP_UTF8);
#endif
}

}  // namespace ny

#endif  // NIL_COMMON_STANDARD_STREAMS_HPP

#ifndef NIL_COMMON_STANDARD_STREAMS_HPP
#define NIL_COMMON_STANDARD_STREAMS_HPP

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace ny {

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
// Call this first in main(), before anything is read or written.
inline void use_untranslated_standard_streams() noexcept {
#ifdef _WIN32
  (void)_setmode(_fileno(stdin), _O_BINARY);
  (void)_setmode(_fileno(stdout), _O_BINARY);
  (void)_setmode(_fileno(stderr), _O_BINARY);
#endif
}

}  // namespace ny

#endif  // NIL_COMMON_STANDARD_STREAMS_HPP

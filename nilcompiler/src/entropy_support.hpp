#ifndef NIL_ENTROPY_SUPPORT_HPP
#define NIL_ENTROPY_SUPPORT_HPP

// Cryptographically secure random bytes for the NIL runtime.
//
// Before this, nothing in the standard library could produce an unpredictable
// value. `nil.std.math` is MINSTD — a 31-bit linear congruential generator
// whose global stream starts at the seed 12345 — and says in its own comments
// that it is not cryptographic; two draws reveal the whole stream.
// `nil.std.security` had base64 and hex and no random at all. /dev/urandom was
// not a way round it either: the file intrinsics refuse a character device by
// design, and the unchecked read returns nothing for one.
//
// That gap is not an inconvenience, it is a vulnerability generator. Session
// identifiers, CSRF tokens, password-reset links and nonces are all bearer
// credentials, and a credential drawn from MINSTD is a credential anyone can
// compute. Any library that needed one had to either invent something
// predictable or refuse to offer the feature.
//
// This asks the operating system and does nothing clever. There is no
// userspace pool to get wrong, no seeding, no state, and no fallback to a
// weak source: if the kernel cannot provide entropy, the answer is an error,
// because silently returning something guessable is how this goes wrong.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace ny::entropy_support {

/**
 * The most one call may ask for.
 *
 * A key is 32 bytes and a generous token is 64. A request for a megabyte of
 * entropy is a program using the wrong tool — a stream cipher seeded from
 * here is the right one — and the ceiling makes that a readable error instead
 * of a quiet stall on a machine whose pool is cold.
 */
constexpr std::int64_t max_random_bytes = 1024;

struct Drawn {
  std::string bytes;
  std::string error;
  bool ok() const { return error.empty(); }
};

#if !defined(_WIN32)

/**
 * Fill from the kernel.
 *
 * getrandom(2) first, because it needs no file descriptor and so cannot fail
 * because the process ran out of them or because something unmounted /dev.
 * A short return is retried: the call is documented to give fewer bytes than
 * asked for when a signal arrives, and treating that as failure would make an
 * unrelated SIGWINCH break a token.
 */
inline bool fill_from_kernel(unsigned char *into, std::size_t count,
                             std::string &error) {
#if defined(__linux__) && defined(SYS_getrandom)
  std::size_t filled = 0;
  bool syscall_usable = true;
  while (filled < count) {
    const long taken = ::syscall(SYS_getrandom, into + filled, count - filled, 0);
    if (taken > 0) {
      filled += static_cast<std::size_t>(taken);
      continue;
    }
    if (taken < 0 && errno == EINTR)
      continue;
    // ENOSYS means a kernel older than the call; anything else here is real,
    // but the device is still worth trying before giving up on the machine.
    syscall_usable = false;
    break;
  }
  if (syscall_usable && filled == count)
    return true;
#elif defined(__APPLE__)
  // Darwin supplies getentropy without opening a device. Each call accepts at
  // most 256 bytes; a failed OS request never falls back to a userspace PRNG.
  std::size_t filled = 0;
  while (filled < count) {
    const std::size_t chunk = count - filled > 256 ? 256 : count - filled;
    if (::getentropy(into + filled, chunk) == 0) {
      filled += chunk;
      continue;
    }
    if (errno == EINTR) continue;
    error = "random: no entropy source is available on this system";
    return false;
  }
  return true;
#endif

  // The device, for a kernel without the syscall. Opened and closed per call:
  // this is not a hot path, and holding a descriptor open for the life of the
  // process is a descriptor a program did not ask the runtime to keep.
  std::FILE *device = std::fopen("/dev/urandom", "rb");
  if (device == nullptr) {
    error = "random: no entropy source is available on this system";
    return false;
  }
  const std::size_t taken = std::fread(into, 1, count, device);
  std::fclose(device);
  if (taken != count) {
    error = "random: the entropy source returned fewer bytes than requested";
    return false;
  }
  return true;
}

#else

inline bool fill_from_kernel(unsigned char *into, std::size_t count,
                             std::string &error) {
  // Resolve the system DLL explicitly. This avoids a CRT include-order
  // dependency and does not add a loader search through the working directory.
  using Generate = NTSTATUS (WINAPI *)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
  static const Generate generate = []() -> Generate {
    const HMODULE library = ::LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return nullptr;
    const auto result = reinterpret_cast<Generate>(::GetProcAddress(library, "BCryptGenRandom"));
    if (!result) ::FreeLibrary(library);
    // Keep a successful module loaded for the lifetime of the function pointer.
    return result;
  }();
  if (count > (std::numeric_limits<ULONG>::max)() || !generate ||
      generate(nullptr, into, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    error = "random: no entropy source is available on this system";
    return false;
  }
  return true;
}

#endif

inline Drawn random_bytes(std::int64_t count) {
  Drawn drawn;
  if (count < 1) {
    drawn.error = "random: ask for at least one byte";
    return drawn;
  }
  if (count > max_random_bytes) {
    drawn.error = "random: a single draw is limited to " +
                  std::to_string(max_random_bytes) + " bytes";
    return drawn;
  }
  std::string buffer(static_cast<std::size_t>(count), '\0');
  if (!fill_from_kernel(reinterpret_cast<unsigned char *>(buffer.data()),
                        buffer.size(), drawn.error))
    return drawn;
  drawn.bytes = std::move(buffer);
  return drawn;
}

/**
 * The same bytes as lower-case hex.
 *
 * Here rather than left to the caller because the caller is often a Matrix
 * page, and Matrix cannot index the byte array this would otherwise return.
 * A session id wants to be text anyway, and hex is the encoding that survives
 * a cookie, a URL and a log line without further escaping.
 */
inline Drawn random_hex(std::int64_t count) {
  Drawn drawn = random_bytes(count);
  if (!drawn.ok())
    return drawn;
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(drawn.bytes.size() * 2);
  for (const unsigned char byte : drawn.bytes) {
    out.push_back(digits[(byte >> 4) & 0x0F]);
    out.push_back(digits[byte & 0x0F]);
  }
  drawn.bytes = std::move(out);
  return drawn;
}

/**
 * A uniform number in [0, bound).
 *
 * By rejection, not by remainder. Taking a 64-bit draw modulo the bound makes
 * the low values more likely whenever the bound does not divide 2^64, and for
 * a small bound the bias is small enough to never be noticed and large enough
 * to matter to anything counting on uniformity.
 */
struct Chosen {
  std::int64_t value = 0;
  std::string error;
  bool ok() const { return error.empty(); }
};

inline Chosen random_below(std::int64_t bound) {
  Chosen chosen;
  if (bound < 1) {
    chosen.error = "random: the bound must be at least one";
    return chosen;
  }
  const std::uint64_t span = static_cast<std::uint64_t>(bound);
  // The largest multiple of the span that fits; draws at or above it are
  // thrown away so every remaining draw maps to exactly one result.
  const std::uint64_t limit = std::uint64_t(-1) - (std::uint64_t(-1) % span) - 1;

  // Bounded attempts: with the worst-case span just over half the range, the
  // chance of thirty-two rejections in a row is below one in four billion, so
  // reaching the end of this loop means the entropy source is not random.
  for (int attempt = 0; attempt < 32; ++attempt) {
    Drawn drawn = random_bytes(8);
    if (!drawn.ok()) {
      chosen.error = drawn.error;
      return chosen;
    }
    std::uint64_t raw = 0;
    for (int byte = 0; byte < 8; ++byte)
      raw = (raw << 8) | static_cast<unsigned char>(drawn.bytes[static_cast<std::size_t>(byte)]);
    if (raw <= limit) {
      chosen.value = static_cast<std::int64_t>(raw % span);
      return chosen;
    }
  }
  chosen.error = "random: the entropy source did not yield a usable draw";
  return chosen;
}

} // namespace ny::entropy_support
#endif

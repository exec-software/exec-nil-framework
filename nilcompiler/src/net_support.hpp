#ifndef NIL_NET_SUPPORT_HPP
#define NIL_NET_SUPPORT_HPP

// Stream sockets for the NIL runtime.
//
// The standard library has carried a transport-independent HTTP module and an
// unbuilt socket design for some time, with nothing underneath either: there
// was no network intrinsic of any kind, so no NIL or Matrix program could
// accept a connection. This is that layer, kept to the smallest surface a
// server and a client actually need.
//
// Everything here returns a value and never raises. A network call fails for
// reasons entirely outside the program — the port is taken, the peer went
// away, the name does not resolve — and those are ordinary answers a server
// has to handle on every request, not exceptional conditions.
//
// Three decisions are worth stating because they are the ones that bite:
//
//   * Writes suppress SIGPIPE per socket on Darwin and per send on Linux.
//   * Ordinary listeners are exclusive on Windows and use SO_REUSEADDR on
//     POSIX. Sharing a port requires the explicit pre-bind constructor.
//   * Binding never resolves a name through DNS. A server binds an address it
//     already knows, and a DNS lookup on that path is a blocking call plus a
//     way to end up listening somewhere unintended. `localhost` is understood
//     directly; everything else must be numeric. Connecting does resolve,
//     because a client is given names.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ny::net_support {

#if defined(_WIN32)
using socket_descriptor = SOCKET;
constexpr socket_descriptor invalid_socket = INVALID_SOCKET;
static_assert(sizeof(socket_descriptor) == sizeof(std::uintptr_t),
              "Winsock handles require pointer-width storage");
#else
using socket_descriptor = int;
constexpr socket_descriptor invalid_socket = -1;
#endif

constexpr std::int64_t max_timeout_ms = 2147483647;


/** One read never takes more than this, however much the caller asks for. */
constexpr std::int64_t max_transfer_bytes = 8LL * 1024 * 1024;

/** A listen backlog past this is clamped; the kernel clamps anyway. */
constexpr std::int64_t max_backlog = 1024;

struct Opened {
  socket_descriptor descriptor = invalid_socket;
  std::string error;
  /**
   * Nothing to do right now, on a socket that was asked not to block.
   *
   * Told apart from an error because for an event loop it is the ordinary
   * case: poll said the listener was readable, and by the time accept ran the
   * connection had been withdrawn. Reporting that as a failure would make a
   * server log a line every time a port scanner connected and left.
   */
  bool would_block = false;
  bool ok() const { return descriptor != invalid_socket && error.empty(); }
};

struct Transferred {
  std::string data;
  std::int64_t count = 0;
  /** The peer closed its side in an orderly way. Not an error. */
  bool closed = false;
  /** Nothing available right now, on a socket asked not to block. */
  bool would_block = false;
  std::string error;
  bool ok() const { return error.empty(); }
};

#if defined(_WIN32)

// Winsock needs initialising once per process, and the counted
// WSAStartup/WSACleanup pair means a library may start it without taking the
// decision away from whatever else in the process also needs it.
inline bool ensure_started(std::string &error) {
  struct Startup {
    int result;
    Startup() : result(0) {
      WSADATA data{};
      result = WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~Startup() { if (result == 0) WSACleanup(); }
  };
  static const Startup startup;
  if (startup.result == 0) return true;
  error = "net: Winsock could not be started (" + std::to_string(startup.result) + ")";
  return false;
}

inline std::string last_error_text() {
  return "net: the socket call failed (" + std::to_string(::WSAGetLastError()) + ")";
}
inline int close_descriptor(socket_descriptor descriptor) {
  return ::closesocket(descriptor);
}
using transfer_size = int;
constexpr int no_signal_flag = 0;

#else

inline bool ensure_started(std::string &) { return true; }

inline std::string last_error_text() {
  const int captured = errno;
  const char *text = std::strerror(captured);
  return std::string("net: ") + (text ? text : "the socket call failed");
}

inline int close_descriptor(socket_descriptor descriptor) { return ::close(descriptor); }
using transfer_size = ssize_t;
#if defined(MSG_NOSIGNAL)
constexpr int no_signal_flag = MSG_NOSIGNAL;
#else
constexpr int no_signal_flag = 0;
#endif

#endif

/** Apply process-local socket safety before exposing the descriptor. */
inline std::string prepare_socket(socket_descriptor descriptor) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE,
                   reinterpret_cast<const char *>(&enabled), sizeof(enabled)) != 0)
    return last_error_text();
#endif
#if defined(_WIN32)
  if (!::SetHandleInformation(reinterpret_cast<HANDLE>(descriptor), HANDLE_FLAG_INHERIT, 0))
    return "net: the socket could not be made non-inheritable";
#else
  const int flags = ::fcntl(descriptor, F_GETFD);
  if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0)
    return last_error_text();
#endif
  return {};
}

/** Whether the last call failed only because it would have blocked. */
inline bool last_call_would_block() {
#if defined(_WIN32)
  return ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

/**
 * The address a listening socket may bind.
 *
 * Numeric only, by design — see the note at the top. `localhost` is accepted
 * because it is what a developer types and because its meaning here is not in
 * doubt; an empty host means every interface, which is what a container or a
 * reverse proxy behind a firewall wants and what a laptop on a café network
 * very much does not. The caller chooses, and this does not choose for them.
 */
inline bool fill_bind_address(const std::string &host, std::int64_t port,
                              sockaddr_in &into, std::string &error) {
  if (host.find('\0') != std::string::npos) {
    error = "net: a host must not contain a NUL byte";
    return false;
  }
  if (port < 1 || port > 65535) {
    error = "net: a port must be between 1 and 65535";
    return false;
  }
  std::memset(&into, 0, sizeof(into));
  into.sin_family = AF_INET;
  into.sin_port = htons(static_cast<std::uint16_t>(port));

  if (host.empty() || host == "0.0.0.0" || host == "*") {
    into.sin_addr.s_addr = htonl(INADDR_ANY);
    return true;
  }
  if (host == "localhost") {
    into.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return true;
  }
  if (::inet_pton(AF_INET, host.c_str(), &into.sin_addr) == 1)
    return true;

  error = "net: a bind address must be numeric, empty, or localhost";
  return false;
}

/** Open, bind and listen in one step, because no caller wants them apart. */
inline Opened listen_on(const std::string &host, std::int64_t port,
                        std::int64_t backlog, bool share_port = false) {
  Opened opened;
  if (!ensure_started(opened.error))
    return opened;

  sockaddr_in address{};
  if (!fill_bind_address(host, port, address, opened.error))
    return opened;

#if !defined(SO_REUSEPORT) || defined(_WIN32)
  if (share_port) {
    opened.error = "net: this platform has no shared-listener SO_REUSEPORT support";
    return opened;
  }
#endif
  const socket_descriptor descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
  if (descriptor == invalid_socket) {
    opened.error = last_error_text();
    return opened;
  }

  opened.error = prepare_socket(descriptor);
  const int reuse = 1;
#if defined(_WIN32)
  constexpr int reuse_option = SO_EXCLUSIVEADDRUSE;
#else
  constexpr int reuse_option = SO_REUSEADDR;
#endif
  if (opened.error.empty() &&
      ::setsockopt(descriptor, SOL_SOCKET, reuse_option,
                   reinterpret_cast<const char *>(&reuse), sizeof(reuse)) != 0)
    opened.error = last_error_text();
#if defined(SO_REUSEPORT) && !defined(_WIN32)
  if (opened.error.empty() && share_port &&
      ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEPORT,
                   reinterpret_cast<const char *>(&reuse), sizeof(reuse)) != 0)
    opened.error = last_error_text();
#endif
  if (!opened.error.empty()) {
    close_descriptor(descriptor);
    return opened;
  }

  if (::bind(descriptor, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0) {
    opened.error = last_error_text();
    close_descriptor(descriptor);
    return opened;
  }

  std::int64_t depth = backlog < 1 ? 128 : backlog;
  if (depth > max_backlog)
    depth = max_backlog;
  if (::listen(descriptor, static_cast<int>(depth)) != 0) {
    opened.error = last_error_text();
    close_descriptor(descriptor);
    return opened;
  }

  opened.descriptor = descriptor;
  return opened;
}

/** Wait for a connection. Retries an interruption rather than reporting one. */
inline Opened accept_on(socket_descriptor listening) {
  Opened opened;
  if (!ensure_started(opened.error))
    return opened;
  for (;;) {
    const socket_descriptor descriptor = ::accept(listening, nullptr, nullptr);
    if (descriptor != invalid_socket) {
      opened.error = prepare_socket(descriptor);
      if (opened.error.empty()) opened.descriptor = descriptor;
      else close_descriptor(descriptor);
      return opened;
    }
#if !defined(_WIN32)
    if (errno == EINTR)
      continue;
#endif
    if (last_call_would_block()) {
      opened.would_block = true;
      return opened;
    }
    opened.error = last_error_text();
    return opened;
  }
}

/**
 * Connect to a host, resolving a name if it is one.
 *
 * Unlike binding, this resolves: a client is handed names. The first address
 * that connects wins, so a host with both an IPv4 and an IPv6 address works
 * without the caller choosing.
 */
inline Opened connect_to(const std::string &host, std::int64_t port) {
  Opened opened;
  if (!ensure_started(opened.error))
    return opened;
  if (host.find('\0') != std::string::npos) {
    opened.error = "net: a host must not contain a NUL byte";
    return opened;
  }
  if (port < 1 || port > 65535) {
    opened.error = "net: a port must be between 1 and 65535";
    return opened;
  }
  if (host.empty()) {
    opened.error = "net: connecting needs a host";
    return opened;
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *found = nullptr;
  const std::string service = std::to_string(port);
  if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 ||
      found == nullptr) {
    opened.error = "net: the host could not be resolved";
    return opened;
  }

  std::string attempt_error = "net: no address accepted the connection";
  for (addrinfo *candidate = found; candidate != nullptr;
       candidate = candidate->ai_next) {
    const socket_descriptor descriptor = ::socket(
        candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (descriptor == invalid_socket)
      continue;
    attempt_error = prepare_socket(descriptor);
    if (!attempt_error.empty()) {
      close_descriptor(descriptor);
      continue;
    }
    if (::connect(descriptor, candidate->ai_addr,
                  static_cast<int>(candidate->ai_addrlen)) == 0) {
      ::freeaddrinfo(found);
      opened.descriptor = descriptor;
      return opened;
    }
    attempt_error = last_error_text();
    close_descriptor(descriptor);
  }
  ::freeaddrinfo(found);
  opened.error = attempt_error;
  return opened;
}

/**
 * Read what has arrived, up to a ceiling.
 *
 * A short read is normal and is not an error: TCP is a stream and a request
 * may arrive in pieces. `closed` is how the orderly end of a stream is told
 * apart from a failure, because a caller reading until the peer finishes needs
 * to know which happened and both return no data.
 */
inline Transferred read_some(socket_descriptor descriptor, std::int64_t wanted) {
  Transferred result;
  if (!ensure_started(result.error))
    return result;
  if (wanted < 1) {
    result.error = "net: a read must ask for at least one byte";
    return result;
  }
  if (wanted > max_transfer_bytes)
    wanted = max_transfer_bytes;

  std::string buffer(static_cast<std::size_t>(wanted), '\0');
  for (;;) {
    const transfer_size taken =
        ::recv(descriptor, buffer.data(), static_cast<transfer_size>(wanted), 0);
    if (taken > 0) {
      buffer.resize(static_cast<std::size_t>(taken));
      result.data = std::move(buffer);
      result.count = static_cast<std::int64_t>(taken);
      return result;
    }
    if (taken == 0) {
      result.closed = true;
      return result;
    }
#if !defined(_WIN32)
    if (errno == EINTR)
      continue;
#endif
    if (last_call_would_block()) {
      result.would_block = true;
      return result;
    }
    result.error = last_error_text();
    return result;
  }
}

/**
 * Write what the socket will take right now, and say how much that was.
 *
 * The non-blocking counterpart of write_all. A server sending a large
 * response to a client that reads it slowly cannot afford write_all: the
 * socket buffer fills, the call blocks, and one slow reader stops the whole
 * event loop. This takes what it can, reports it, and leaves the rest to the
 * caller to send when poll says the socket will accept more.
 */
inline Transferred write_some(socket_descriptor descriptor, const std::string &data,
                              std::size_t offset) {
  Transferred result;
  if (!ensure_started(result.error))
    return result;
  if (offset >= data.size())
    return result;

  for (;;) {
    const transfer_size taken =
        ::send(descriptor, data.data() + offset,
               static_cast<transfer_size>(std::min<std::size_t>(data.size() - offset,
                   static_cast<std::size_t>(max_transfer_bytes))), no_signal_flag);
    if (taken > 0) {
      result.count = static_cast<std::int64_t>(taken);
      return result;
    }
#if !defined(_WIN32)
    if (taken < 0 && errno == EINTR)
      continue;
#endif
    if (last_call_would_block()) {
      result.would_block = true;
      return result;
    }
    result.error = last_error_text();
    return result;
  }
}

/**
 * Write all of it, or say how far it got.
 *
 * Looping is the caller's job in C and should not be: a single `send` is
 * permitted to take less than it was given, and the number of servers that
 * have shipped a truncated response because nobody checked is large.
 */
inline Transferred write_all(socket_descriptor descriptor, const std::string &data) {
  Transferred result;
  if (!ensure_started(result.error))
    return result;

  std::size_t sent = 0;
  while (sent < data.size()) {
    const std::size_t remaining = data.size() - sent;
    const transfer_size taken =
        ::send(descriptor, data.data() + sent,
               static_cast<transfer_size>(std::min<std::size_t>(remaining,
                   static_cast<std::size_t>(max_transfer_bytes))), no_signal_flag);
    if (taken > 0) {
      sent += static_cast<std::size_t>(taken);
      continue;
    }
#if !defined(_WIN32)
    if (taken < 0 && errno == EINTR)
      continue;
#endif
    result.count = static_cast<std::int64_t>(sent);
    result.error = last_error_text();
    return result;
  }
  result.count = static_cast<std::int64_t>(sent);
  return result;
}

/**
 * Put a deadline on reads and writes.
 *
 * A single-threaded server with no timeout is one slow client away from
 * serving nobody: a connection that opens and then says nothing holds the
 * accept loop forever. Zero removes the deadline again.
 */
inline std::string set_timeout(socket_descriptor descriptor, std::int64_t milliseconds) {
  std::string error;
  if (!ensure_started(error))
    return error;
  if (milliseconds < 0 || milliseconds > max_timeout_ms)
    return "net: a timeout must be between 0 and 2147483647 milliseconds";

#if defined(_WIN32)
  const DWORD value = static_cast<DWORD>(milliseconds);
  if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&value), sizeof(value)) != 0 ||
      ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char *>(&value), sizeof(value)) != 0)
    return last_error_text();
#else
  timeval value{};
  value.tv_sec = static_cast<time_t>(milliseconds / 1000);
  value.tv_usec = static_cast<suseconds_t>((milliseconds % 1000) * 1000);
  if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0 ||
      ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) != 0)
    return last_error_text();
#endif
  return {};
}

/** The peer's address as text, for a log line. Never part of a decision. */
inline Transferred peer_address(socket_descriptor descriptor) {
  Transferred result;
  if (!ensure_started(result.error))
    return result;

  sockaddr_storage address{};
#if defined(_WIN32)
  int size = sizeof(address);
#else
  socklen_t size = sizeof(address);
#endif
  if (::getpeername(descriptor, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
    result.error = last_error_text();
    return result;
  }

  char host[INET6_ADDRSTRLEN] = {0};
  if (address.ss_family == AF_INET) {
    const auto *four = reinterpret_cast<const sockaddr_in *>(&address);
    if (::inet_ntop(AF_INET, &four->sin_addr, host, sizeof(host)) == nullptr) {
      result.error = "net: the peer address could not be read";
      return result;
    }
    result.data = std::string(host) + ":" + std::to_string(ntohs(four->sin_port));
    return result;
  }
  if (address.ss_family == AF_INET6) {
    const auto *six = reinterpret_cast<const sockaddr_in6 *>(&address);
    if (::inet_ntop(AF_INET6, &six->sin6_addr, host, sizeof(host)) == nullptr) {
      result.error = "net: the peer address could not be read";
      return result;
    }
    result.data = "[" + std::string(host) + "]:" + std::to_string(ntohs(six->sin6_port));
    return result;
  }
  result.error = "net: the peer uses an address family this build does not read";
  return result;
}

/**
 * Readiness flags, as a NIL program sees them.
 *
 * Deliberately not poll(2)'s numbering: POLLIN and friends differ between
 * platforms, and a program that learned the host's values would be a program
 * that breaks when it moves. These three are the ones a server acts on.
 */
constexpr std::int64_t ready_readable = 1;
constexpr std::int64_t ready_writable = 2;
/** The peer hung up, or the socket is in error. Either way it is finished. */
constexpr std::int64_t ready_finished = 4;

/** One watched socket and what it is being watched for. */
struct Watched {
  std::int64_t token = 0;
  socket_descriptor descriptor = invalid_socket;
  std::int64_t wanted = ready_readable;
};

/** One socket that is ready, and what it is ready for. */
struct Ready {
  std::int64_t token = 0;
  std::int64_t flags = 0;
};

/**
 * Whether a socket blocks when there is nothing to do.
 *
 * A blocking accept is what a one-connection-at-a-time server uses and is
 * perfectly correct for it. An event loop needs the opposite: it has already
 * been told by poll that something is ready, and a blocking call that then
 * finds nothing — a spurious wakeup, a connection withdrawn between the poll
 * and the accept — would stop the whole loop on behalf of one socket.
 */
inline std::string set_blocking(socket_descriptor descriptor, bool blocking) {
  std::string error;
  if (!ensure_started(error))
    return error;
#if defined(_WIN32)
  u_long mode = blocking ? 0 : 1;
  if (::ioctlsocket(descriptor, FIONBIO, &mode) != 0)
    return last_error_text();
#else
  const int flags = ::fcntl(descriptor, F_GETFL, 0);
  if (flags < 0)
    return last_error_text();
  const int wanted = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  if (::fcntl(descriptor, F_SETFL, wanted) < 0)
    return last_error_text();
#endif
  return {};
}

/**
 * Change SO_REUSEPORT on an existing socket where the host supports it.
 * This cannot configure a second listener before bind: shared listeners must
 * instead use listen_on(..., true) on every participating process. Windows
 * rejects enabling this option rather than emulating it with SO_REUSEADDR.
 */
inline std::string set_reuse_port(socket_descriptor descriptor, bool enabled) {
  std::string error;
  if (!ensure_started(error))
    return error;
#if defined(SO_REUSEPORT) && !defined(_WIN32)
  const int value = enabled ? 1 : 0;
  if (::setsockopt(descriptor, SOL_SOCKET, SO_REUSEPORT,
                   reinterpret_cast<const char *>(&value), sizeof(value)) != 0)
    return last_error_text();
  return {};
#else
  (void)descriptor;
  if (!enabled)
    return {};
  return "net: this platform has no SO_REUSEPORT, so a port cannot be shared";
#endif
}

/**
 * Wait until one of the watched sockets can be acted on.
 *
 * poll(2) rather than epoll or kqueue. poll is portable, needs no per-platform
 * registration, and is O(n) in the watch set — which starts to matter in the
 * thousands and is not where this runtime's limits are. The interface here
 * takes the whole set per call precisely so that a later epoll or kqueue
 * implementation can keep it without the caller noticing.
 *
 * A negative timeout waits indefinitely, which is what an idle server does
 * between requests; zero polls and returns at once.
 */
inline std::string poll_wait(const std::vector<Watched> &watching,
                             std::int64_t timeout_ms,
                             std::vector<Ready> &into) {
  into.clear();
  std::string error;
  if (!ensure_started(error))
    return error;
  if (timeout_ms > max_timeout_ms)
    return "net: a finite wait cannot exceed 2147483647 milliseconds";
  if (watching.empty())
    return {};
  if (watching.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return "net: too many watched sockets";

#if defined(_WIN32)
  std::vector<WSAPOLLFD> entries;
#else
  std::vector<pollfd> entries;
#endif
  entries.reserve(watching.size());
  for (const auto &one : watching) {
    if (one.wanted <= 0 || (one.wanted & ~(ready_readable | ready_writable)) != 0)
      return "net: watch flags must select readability, writability, or both";
#if defined(_WIN32)
    WSAPOLLFD entry{};
    entry.fd = one.descriptor;
#else
    pollfd entry{};
    entry.fd = one.descriptor;
#endif
    entry.events = 0;
    if ((one.wanted & ready_readable) != 0)
      entry.events = static_cast<short>(entry.events | POLLIN);
    if ((one.wanted & ready_writable) != 0)
      entry.events = static_cast<short>(entry.events | POLLOUT);
    entries.push_back(entry);
  }

  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
  int remaining = timeout_ms < 0 ? -1 : static_cast<int>(timeout_ms);
  for (;;) {
#if defined(_WIN32)
    const int signalled = ::WSAPoll(entries.data(), static_cast<ULONG>(entries.size()), remaining);
#else
    const int signalled = ::poll(entries.data(), static_cast<nfds_t>(entries.size()), remaining);
#endif
    if (signalled >= 0) break;
#if defined(_WIN32)
    const bool interrupted = ::WSAGetLastError() == WSAEINTR;
#else
    const bool interrupted = errno == EINTR;
#endif
    if (!interrupted) return last_error_text();
    if (timeout_ms >= 0) {
      const auto left = deadline - Clock::now();
      if (left <= Clock::duration::zero()) return {};
      remaining = static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(left).count());
    }
  }

  for (std::size_t index = 0; index < entries.size(); ++index) {
    const short revents = entries[index].revents;
    if (revents == 0)
      continue;
    std::int64_t flags = 0;
    if ((revents & POLLIN) != 0)
      flags |= ready_readable;
    if ((revents & POLLOUT) != 0)
      flags |= ready_writable;
    // POLLHUP with POLLIN still has buffered data to read first, so both are
    // reported and the caller drains before closing. POLLNVAL means the
    // descriptor is not open, which for a token-based API is a bug here
    // rather than in the program, but it must not loop forever on it.
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
      flags |= ready_finished;
    if (flags != 0)
      into.push_back(Ready{watching[index].token, flags});
  }
  return {};
}

/** Stop a write-blocked peer from keeping a response in a buffer. */
inline std::string set_no_delay(socket_descriptor descriptor, bool enabled) {
  std::string error;
  if (!ensure_started(error))
    return error;
  const int value = enabled ? 1 : 0;
  if (::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char *>(&value), sizeof(value)) != 0)
    return last_error_text();
  return {};
}

} // namespace ny::net_support
#endif

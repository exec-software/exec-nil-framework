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
//   * Writes use MSG_NOSIGNAL. Without it, a peer that closes mid-write
//     delivers SIGPIPE and the default disposition kills the process. A web
//     server that dies when a browser closes a tab is not a web server.
//   * A listening socket sets SO_REUSEADDR but never SO_REUSEPORT. The first
//     lets a restart rebind a port still in TIME_WAIT; the second would let
//     two unrelated servers share one port and silently split the traffic.
//   * Binding never resolves a name through DNS. A server binds an address it
//     already knows, and a DNS lookup on that path is a blocking call plus a
//     way to end up listening somewhere unintended. `localhost` is understood
//     directly; everything else must be numeric. Connecting does resolve,
//     because a client is given names.

#include <cstdint>
#include <string>

#if defined(_WIN32)
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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ny::net_support {

/** One read never takes more than this, however much the caller asks for. */
constexpr std::int64_t max_transfer_bytes = 8LL * 1024 * 1024;

/** A listen backlog past this is clamped; the kernel clamps anyway. */
constexpr std::int64_t max_backlog = 1024;

struct Opened {
  int descriptor = -1;
  std::string error;
  bool ok() const { return descriptor >= 0 && error.empty(); }
};

struct Transferred {
  std::string data;
  std::int64_t count = 0;
  /** The peer closed its side in an orderly way. Not an error. */
  bool closed = false;
  std::string error;
  bool ok() const { return error.empty(); }
};

#if defined(_WIN32)

// Winsock needs initialising once per process, and the counted
// WSAStartup/WSACleanup pair means a library may start it without taking the
// decision away from whatever else in the process also needs it.
inline bool ensure_started(std::string &error) {
  static bool started = false;
  static std::string start_error;
  if (!started && start_error.empty()) {
    WSADATA data;
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0)
      start_error = "net: Winsock could not be started";
    else
      started = true;
  }
  if (!start_error.empty()) {
    error = start_error;
    return false;
  }
  return true;
}

inline std::string last_error_text() { return "net: the socket call failed"; }
inline int close_descriptor(int descriptor) {
  return ::closesocket(static_cast<SOCKET>(descriptor));
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

inline int close_descriptor(int descriptor) { return ::close(descriptor); }
using transfer_size = ssize_t;
#if defined(MSG_NOSIGNAL)
constexpr int no_signal_flag = MSG_NOSIGNAL;
#else
constexpr int no_signal_flag = 0;
#endif

#endif

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
                        std::int64_t backlog) {
  Opened opened;
  if (!ensure_started(opened.error))
    return opened;

  sockaddr_in address{};
  if (!fill_bind_address(host, port, address, opened.error))
    return opened;

  const int descriptor = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
  if (descriptor < 0) {
    opened.error = last_error_text();
    return opened;
  }

  const int reuse = 1;
  ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));

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
inline Opened accept_on(int listening) {
  Opened opened;
  if (!ensure_started(opened.error))
    return opened;
  for (;;) {
    const int descriptor = static_cast<int>(::accept(listening, nullptr, nullptr));
    if (descriptor >= 0) {
      opened.descriptor = descriptor;
      return opened;
    }
#if !defined(_WIN32)
    if (errno == EINTR)
      continue;
#endif
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
    const int descriptor = static_cast<int>(::socket(
        candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol));
    if (descriptor < 0)
      continue;
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
inline Transferred read_some(int descriptor, std::int64_t wanted) {
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
inline Transferred write_all(int descriptor, const std::string &data) {
  Transferred result;
  if (!ensure_started(result.error))
    return result;

  std::size_t sent = 0;
  while (sent < data.size()) {
    const std::size_t remaining = data.size() - sent;
    const transfer_size taken =
        ::send(descriptor, data.data() + sent,
               static_cast<transfer_size>(remaining), no_signal_flag);
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
inline std::string set_timeout(int descriptor, std::int64_t milliseconds) {
  std::string error;
  if (!ensure_started(error))
    return error;
  if (milliseconds < 0)
    return "net: a timeout cannot be negative";

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
inline Transferred peer_address(int descriptor) {
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

/** Stop a write-blocked peer from keeping a response in a buffer. */
inline std::string set_no_delay(int descriptor, bool enabled) {
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

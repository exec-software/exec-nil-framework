// No PostgreSQL server is needed: owned loopback sockets exercise the client
// provider's numeric-address validation, readiness deadlines and close path.
#include "database_runtime.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
using Length = int;
struct Network {
  Network() { WSADATA data{}; require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "test Winsock startup"); }
  ~Network() { WSACleanup(); }
};
void close_socket(Socket value) { closesocket(value); }
void nonblocking(Socket value) { u_long enabled = 1; require(ioctlsocket(value, FIONBIO, &enabled) == 0, "test nonblocking socket"); }
#else
using Socket = int;
constexpr Socket invalid = -1;
using Length = socklen_t;
struct Network {};
void close_socket(Socket value) { close(value); }
void nonblocking(Socket value) { require(fcntl(value, F_SETFL, O_NONBLOCK) == 0, "test nonblocking socket"); }
#endif
struct OwnedSocket {
  Socket value;
  explicit OwnedSocket(Socket value) : value(value) { require(value != invalid, "test socket creation"); }
  ~OwnedSocket() { close_socket(value); }
  OwnedSocket(const OwnedSocket &) = delete;
  OwnedSocket &operator=(const OwnedSocket &) = delete;
};
bool readable(Socket socket, Clock::time_point deadline) {
  for (;;) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (remaining <= 0) return false;
#ifdef _WIN32
    WSAPOLLFD descriptor{socket, POLLIN, 0};
    int status = WSAPoll(&descriptor, 1, static_cast<int>(remaining));
    if (status == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
#else
    pollfd descriptor{socket, POLLIN, 0};
    int status = poll(&descriptor, 1, static_cast<int>(remaining));
    if (status < 0 && errno == EINTR) continue;
#endif
    return status > 0 && !(descriptor.revents & (POLLERR | POLLNVAL)) &&
           (descriptor.revents & (POLLIN | POLLHUP));
  }
}

void stalled_peer(int family) {
  OwnedSocket listener(socket(family, SOCK_STREAM, IPPROTO_TCP));
#ifdef _WIN32
  int exclusive = 1;
  require(setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                    reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) == 0, "test exclusive bind");
#endif
  sockaddr_storage storage{};
  Length length;
  std::string numeric;
  if (family == AF_INET) {
    auto &address = reinterpret_cast<sockaddr_in &>(storage);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    length = sizeof(address);
    numeric = "127.0.0.1";
  } else {
    auto &address = reinterpret_cast<sockaddr_in6 &>(storage);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    length = sizeof(address);
    numeric = "::1";
  }
  require(bind(listener.value, reinterpret_cast<sockaddr *>(&storage), length) == 0, "test loopback bind");
  require(getsockname(listener.value, reinterpret_cast<sockaddr *>(&storage), &length) == 0, "test ephemeral port");
  auto port = ntohs(family == AF_INET ? reinterpret_cast<sockaddr_in &>(storage).sin_port
                                    : reinterpret_cast<sockaddr_in6 &>(storage).sin6_port);
  require(listen(listener.value, 4) == 0, "test loopback listen");
  nonblocking(listener.value);
  // A real owned listener completes TCP setup but sends no PostgreSQL reply.
  // No worker thread can accidentally outlive a failing assertion.
  for (int attempt = 0; attempt != 4; ++attempt) {
    ny::database::Context context;
    auto start = Clock::now();
    auto result = context.open("postgresql", "hostaddr=" + numeric + " host=never-resolve.invalid port=" +
        std::to_string(port) + " user=nil_probe dbname=postgres password=private-redaction-marker"
        " sslmode=disable gssencmode=disable", 40);
    auto elapsed = Clock::now() - start;
    require(!result && result.error == "database: PostgreSQL connection timed out or transport failed",
            "stalled peer must return the redacted deadline/transport error");
    require(elapsed >= std::chrono::milliseconds(10) && elapsed < std::chrono::seconds(2),
            "stalled connection must actually wait with a bounded deadline");
    auto deadline = Clock::now() + std::chrono::seconds(2);
    require(readable(listener.value, deadline), "provider must reach the owned numeric listener without DNS");
    OwnedSocket peer(accept(listener.value, nullptr, nullptr));
    nonblocking(peer.value);
    std::size_t received = 0;
    for (;;) {
      require(readable(peer.value, deadline), "timed-out provider must close its client socket");
      char buffer[4096];
      int count = static_cast<int>(recv(peer.value, buffer, sizeof(buffer), 0));
      require(count >= 0, "test startup stream read");
      if (!count) break;
      received += static_cast<std::size_t>(count);
      require(received <= 16384, "test startup stream bound");
    }
    require(received > 0, "provider must send a PostgreSQL startup packet before timing out");
  }
}
}

int main() {
  try {
    if (!ny::database::available("postgresql")) return 77;
    [[maybe_unused]] Network network;
    ny::database::Context context;
#ifdef _WIN32
    const std::string invalid_address = "database: PostgreSQL requires numeric hostaddr on Windows";
#else
    const std::string invalid_address = "database: PostgreSQL requires numeric hostaddr or an absolute Unix socket host";
#endif
    for (const std::string address : {"host=never-resolve.invalid", "hostaddr=localhost", "hostaddr=127.1",
        "hostaddr=127.0.0.999", "hostaddr=127.0.0.1,127.0.0.2", "hostaddr=::gg", "hostaddr=fe80::1%3",
        "hostaddr=fe80::1%lo0", "hostaddr=fe80::1%", "hostaddr=::1%3", "hostaddr=::1%no-such-interface",
        "hostaddr=[::1]", "hostaddr='127.0.0.1 '", "dbname=postgres"}) {
      auto result = context.open("postgresql", address + " password=private-redaction-marker", 100);
      require(!result && result.error == invalid_address, "invalid host must be rejected before connection without details");
    }
    require(context.open("postgresql", "service=private-redaction-marker", 100).error ==
            "database: PostgreSQL service files are outside the bounded connection API", "service lookup rejected");
    require(context.open("postgresql", "hostaddr=127.0.0.1 host=first,second", 100).error ==
            "database: PostgreSQL requires one host", "multiple hosts rejected");
    require(context.open("postgresql", "password='private-redaction-marker", 100).error ==
            "database: invalid PostgreSQL connection options", "parser error redacted");
#ifdef _WIN32
    require(context.open("postgresql", "host=/private/not-a-windows-socket", 100).error == invalid_address,
            "Windows must not fall back to an unintended local Unix socket");
#endif
    stalled_peer(AF_INET);
    stalled_peer(AF_INET6);
    std::cout << "PostgreSQL numeric IPv4/IPv6, redaction, eight bounded stalls and socket-close checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

// Exercise the OS socket/entropy adapters without an interpreter or public port.
// Include the CRT first: entropy must not depend on an earlier feature macro.
#include <cstdlib>
#include "net_support.hpp"
#include "entropy_support.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#if !defined(_WIN32)
#include <pthread.h>
#endif

namespace {
namespace net = ny::net_support;
int checks = 0;
void require(bool value, const std::string &message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
struct Socket {
  net::socket_descriptor value = net::invalid_socket;
  explicit Socket(net::socket_descriptor opened = net::invalid_socket) : value(opened) {}
  ~Socket() { if (value != net::invalid_socket) net::close_descriptor(value); }
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
};
int unused_port() {
  std::string error;
  require(net::ensure_started(error), error);
  Socket probe(::socket(AF_INET, SOCK_STREAM, 0));
  require(probe.value != net::invalid_socket, "open port probe");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  require(::bind(probe.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "bind port probe");
#if defined(_WIN32)
  int size = sizeof(address);
#else
  socklen_t size = sizeof(address);
#endif
  require(::getsockname(probe.value, reinterpret_cast<sockaddr *>(&address), &size) == 0, "read port probe");
  return ntohs(address.sin_port);
}
void binding() {
  const std::string truncated("127.0.0.1\0suffix", 16);
  const auto invalid_bind = net::listen_on(truncated, 31337, 8);
  require(!invalid_bind.ok() && invalid_bind.error.find("NUL") != std::string::npos, "bind rejects embedded NUL");
  const auto invalid_connect = net::connect_to(truncated, 31337);
  require(!invalid_connect.ok() && invalid_connect.error.find("NUL") != std::string::npos, "connect rejects NUL before resolution");
  const int port = unused_port();
  const auto first = net::listen_on("127.0.0.1", port, 8);
  require(first.ok(), first.error);
  Socket one(first.descriptor);
  const auto second = net::listen_on("127.0.0.1", port, 8);
  Socket two(second.descriptor);
  require(!second.ok(), "ordinary listeners must not share an endpoint");
  require(!net::set_timeout(one.value, -1).empty(), "negative timeout rejected");
  require(!net::set_timeout(one.value, net::max_timeout_ms + 1).empty(), "overflowing timeout rejected");
  std::vector<net::Ready> ready;
  require(!net::poll_wait({{1, one.value, net::ready_readable}}, net::max_timeout_ms + 1, ready).empty(),
          "overflowing poll timeout rejected");
  require(!net::poll_wait({{1, one.value, 8}}, 0, ready).empty(), "unknown watch flags rejected");

  const int shared_port = unused_port();
  const auto shared_first = net::listen_on("127.0.0.1", shared_port, 8, true);
  Socket shared_one(shared_first.descriptor);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
  require(shared_first.ok(), shared_first.error);
  const auto shared_second = net::listen_on("127.0.0.1", shared_port, 8, true);
  Socket shared_two(shared_second.descriptor);
  require(shared_second.ok(), shared_second.error);
  const auto ordinary = net::listen_on("127.0.0.1", shared_port, 8);
  Socket accidental(ordinary.descriptor);
  require(!ordinary.ok(), "ordinary listener cannot join an explicitly shared endpoint");
#else
  require(!shared_first.ok() && shared_first.error.find("no shared-listener") != std::string::npos,
          "unsupported shared listening is an explicit error");
#endif
}

void stream() {
  const int port = unused_port();
  const auto opened = net::listen_on("127.0.0.1", port, 8);
  require(opened.ok(), opened.error);
  Socket listener(opened.descriptor);
  require(net::set_blocking(listener.value, false).empty(), "nonblocking listener");
  require(net::accept_on(listener.value).would_block, "idle accept is would-block");
  const auto connected = net::connect_to("127.0.0.1", port);
  require(connected.ok(), connected.error);
  Socket client(connected.descriptor);
  std::vector<net::Ready> ready;
  require(net::poll_wait({{37, listener.value, net::ready_readable}}, 1000, ready).empty() &&
          ready.size() == 1 && ready[0].token == 37 && (ready[0].flags & net::ready_readable), "listener readiness");
  const auto accepted = net::accept_on(listener.value);
  require(accepted.ok(), accepted.error);
  Socket server(accepted.descriptor);
  require(net::set_timeout(server.value, 1000).empty(), "server timeout");
  require(net::set_timeout(client.value, 1000).empty(), "client timeout");
#if defined(SO_NOSIGPIPE)
  for (const auto descriptor : {client.value, server.value}) {
    int enabled = 0; socklen_t size = sizeof(enabled);
    require(::getsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, &size) == 0 && enabled == 1,
            "both connecting and accepted sockets suppress SIGPIPE");
  }
#endif
  require(net::set_blocking(server.value, false).empty(), "nonblocking receive");
  const auto idle = net::read_some(server.value, 32);
  require(idle.ok() && idle.would_block && !idle.closed && idle.data.empty(), "would-block differs from EOF");
  const std::string bytes("prefix\0\xfftail", 12);
  const auto sent = net::write_some(client.value, bytes, 6);
  require(sent.ok() && sent.count == 6 && !sent.would_block, "send from exact byte offset");
  require(net::poll_wait({{9, server.value, net::ready_readable}}, 1000, ready).empty() && !ready.empty(), "data readiness");
  const auto data = net::read_some(server.value, 32);
  require(data.ok() && data.data == bytes.substr(6) && data.count == 6 && !data.closed && !data.would_block,
          "nonblocking bytes survive zero and invalid UTF-8");
  require(net::write_some(client.value, bytes, bytes.size()).count == 0, "empty remainder succeeds");

  // A peer that does not read eventually applies backpressure. Bound traffic
  // and memory instead of depending on a host's default socket-buffer size.
  const int buffer = 4096;
  require(::setsockopt(client.value, SOL_SOCKET, SO_SNDBUF,
      reinterpret_cast<const char *>(&buffer), sizeof(buffer)) == 0, "small send buffer");
  require(net::set_blocking(client.value, false).empty(), "nonblocking send");
  const std::string payload(256 * 1024, 'x');
  bool blocked = false;
  std::int64_t total = 0;
  for (int attempt = 0; attempt < 1024 && total < 32 * 1024 * 1024; ++attempt) {
    const auto part = net::write_some(client.value, payload, 0);
    require(part.ok(), part.error);
    if (part.would_block) { blocked = true; break; }
    require(part.count > 0 && part.count <= static_cast<std::int64_t>(payload.size()), "bounded positive partial send");
    total += part.count;
  }
  require(blocked && total > 0, "slow peer produces would-block without blocking the process");
}

void peer_close() {
  const int port = unused_port();
  const auto opened = net::listen_on("127.0.0.1", port, 8);
  require(opened.ok(), opened.error);
  Socket listener(opened.descriptor);
  const auto connected = net::connect_to("127.0.0.1", port);
  require(connected.ok(), connected.error);
  Socket client(connected.descriptor);
  const auto accepted = net::accept_on(listener.value);
  require(accepted.ok(), accepted.error);
  Socket server(accepted.descriptor);
  require(net::set_timeout(client.value, 1000).empty(), "closed-peer client timeout");
  linger reset{}; reset.l_onoff = 1; reset.l_linger = 0;
  require(::setsockopt(server.value, SOL_SOCKET, SO_LINGER,
      reinterpret_cast<const char *>(&reset), sizeof(reset)) == 0, "peer reset configuration");
  net::close_descriptor(server.value); server.value = net::invalid_socket;
  std::vector<net::Ready> ready;
  require(net::poll_wait({{1, client.value, net::ready_readable}}, 1000, ready).empty() && !ready.empty(), "peer-close observed");
  // The first failure can be ECONNRESET; a subsequent write is what exposes
  // an unsuppressed EPIPE. The process uses SIGPIPE's default disposition.
  int failed = 0;
  for (int attempt = 0; attempt < 4; ++attempt)
    if (!net::write_all(client.value, "closed peer").ok()) ++failed;
  require(failed > 0, "closed-peer writes report failure without terminating");
}

#if !defined(_WIN32)
volatile std::sig_atomic_t interruptions = 0;
void interrupted(int) { ++interruptions; }
void interrupted_wait() {
  const auto opened = net::listen_on("127.0.0.1", unused_port(), 8);
  require(opened.ok(), opened.error);
  Socket listener(opened.descriptor);
  struct sigaction action{}, before{};
  action.sa_handler = interrupted;
  sigemptyset(&action.sa_mask);
  require(sigaction(SIGUSR1, &action, &before) == 0, "install owned-thread signal handler");
  const pthread_t target = pthread_self();
  std::thread signaler([target] {
    for (int i = 0; i < 20; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      pthread_kill(target, SIGUSR1);
    }
  });
  const auto start = std::chrono::steady_clock::now();
  std::vector<net::Ready> ready;
  const auto error = net::poll_wait({{1, listener.value, net::ready_readable}}, 100, ready);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  signaler.join();
  require(sigaction(SIGUSR1, &before, nullptr) == 0, "restore signal disposition");
  require(error.empty() && ready.empty() && interruptions > 0, "signals do not turn a wait into an error");
  require(elapsed >= std::chrono::milliseconds(70) && elapsed < std::chrono::milliseconds(300),
          "finite deadline is not restarted by repeated interruptions");
}
#endif

void entropy() {
  namespace random = ny::entropy_support;
  require(!random::random_bytes(0).ok() && !random::random_bytes(1025).ok(), "entropy byte bounds");
  const auto bytes = random::random_bytes(257);
  require(bytes.ok() && bytes.bytes.size() == 257, "OS entropy fills the complete buffer");
  const auto text = random::random_hex(32);
  require(text.ok() && text.bytes.size() == 64 && text.bytes.find_first_not_of("0123456789abcdef") == std::string::npos,
          "OS entropy hex encoding");
  for (const auto bound : {std::int64_t{1}, std::int64_t{3}, (std::numeric_limits<std::int64_t>::max)()}) {
    for (int i = 0; i < 20; ++i) {
      const auto value = random::random_below(bound);
      require(value.ok() && value.value >= 0 && value.value < bound, "OS entropy bounded value");
    }
  }
  require(!random::random_below(0).ok(), "entropy invalid bound");
}
}

int main() {
  try {
#if defined(_WIN32)
    static_assert(std::is_same<net::socket_descriptor, SOCKET>::value, "socket storage must not narrow a Winsock handle");
#else
    std::signal(SIGPIPE, SIG_DFL);
#endif
    binding(); stream(); peer_close(); entropy();
#if !defined(_WIN32)
    interrupted_wait();
#endif
    std::cout << "Socket/entropy support: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

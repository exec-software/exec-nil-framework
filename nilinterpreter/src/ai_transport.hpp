#ifndef NIL_INTERPRETER_AI_TRANSPORT_HPP
#define NIL_INTERPRETER_AI_TRANSPORT_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../../common/windows_private_storage.hpp"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace ny_ai {

// One local client at a time, using the existing newline-delimited protocol.
// The callback owns all VM synchronization and must finish within its own
// bounded deadline. It runs on this transport's sole worker, never concurrently.
// start/stop are owner operations; the callback must not call either operation.
// A client idle for five seconds is disconnected, without changing VM state.
// Stop admits no new requests, but gives an already-entered callback's reply its
// normal one-second write budget before joining. The owner must bound callbacks.
class Transport {
public:
    using Handler = std::function<std::string(const std::string&)>;
    static constexpr std::size_t max_line_bytes = 65536;

    Transport(std::string handshake, Handler handler)
        : handshake_(frame(std::move(handshake))), handler_(std::move(handler)) {
        if (!handler_) throw std::invalid_argument("AI transport requires a handler");
#ifdef _WIN32
        endpoint_ = "\\\\.\\pipe\\ny_ai_" + std::to_string(GetCurrentProcessId());
#else
        endpoint_ = "/tmp/ny_ai_" + std::to_string(getpid()) + ".sock";
#endif
    }
    ~Transport() { stop(); }
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    const std::string& endpoint() const noexcept { return endpoint_; }

    // Endpoint creation, privacy and listening readiness are synchronous. A
    // startup failure never removes an endpoint that this object did not create.
    void start() {
        std::lock_guard<std::mutex> lock(lifecycle_);
        if (worker_.joinable()) throw std::logic_error("AI transport already started");
        stopping_ = false;
        try {
            open_listener();
            worker_ = std::thread([this] { serve(); });
        } catch (...) {
            stopping_ = true;
            close_listener();
            throw;
        }
    }

    void stop() noexcept {
        std::lock_guard<std::mutex> lock(lifecycle_);
        stopping_ = true;
        if (worker_.joinable()) worker_.join();
        close_listener();
    }

private:
    using Clock = std::chrono::steady_clock;
    std::string endpoint_, handshake_;
    Handler handler_;
    std::atomic<bool> stopping_{true};
    std::mutex lifecycle_;
    std::thread worker_;
#ifdef _WIN32
    HANDLE listener_ = INVALID_HANDLE_VALUE;
    bool connected_ = false;
#else
    int listener_ = -1, peer_ = -1;
    bool owns_path_ = false;
    dev_t path_device_ = 0;
    ino_t path_inode_ = 0;
#endif

    static std::string frame(std::string value) {
        if (!value.empty() && value.back() == '\n') value.pop_back();
        if (value.size() > max_line_bytes || value.find('\n') != std::string::npos)
            throw std::invalid_argument("AI transport frame exceeds one bounded line");
        value.push_back('\n');
        return value;
    }
    void pause() const { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

#ifndef _WIN32
    static bool configure_unix_socket(int socket) noexcept {
        // Some Unix hosts lack socket/accept4's atomic flag arguments. This
        // fallback preserves descriptor flags and fails before any protocol I/O
        // if nonblocking setup or per-socket SIGPIPE suppression is unavailable.
        const int status = ::fcntl(socket, F_GETFL, 0);
        if (status < 0 || ::fcntl(socket, F_SETFL, status | O_NONBLOCK) < 0) return false;
        const int descriptor = ::fcntl(socket, F_GETFD, 0);
        if (descriptor < 0 || ::fcntl(socket, F_SETFD, descriptor | FD_CLOEXEC) < 0) return false;
#ifndef MSG_NOSIGNAL
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        if (::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) return false;
#else
        return false;
#endif
#endif
        return true;
    }

    static bool same_unix_owner(int socket) noexcept {
#if defined(__linux__)
        struct PeerCredentials { pid_t pid; uid_t uid; gid_t gid; } identity{};
        socklen_t length = sizeof(identity);
        return ::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &identity, &length) == 0 &&
            length == sizeof(identity) && identity.uid == geteuid();
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
        uid_t user = 0;
        gid_t group = 0;
        return ::getpeereid(socket, &user, &group) == 0 && user == geteuid();
#else
        (void)socket;
        return false; // No unauthenticated fallback on other Unix hosts.
#endif
    }
#endif

    void open_listener() {
#ifdef _WIN32
        nil::detail::owner_security security;
        std::error_code error;
        if (!security.initialize(false, error))
            throw std::runtime_error("AI transport private security descriptor: " + error.message());
        auto attributes = security.attributes();
        const std::wstring name(endpoint_.begin(), endpoint_.end());
        listener_ = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 8192, 8192, 0, &attributes);
        if (listener_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("AI transport exclusive pipe creation failed: " + std::to_string(GetLastError()));
        // owner_security's filesystem verifier is deliberately not used for a
        // pipe. The protected owner-only DACL is supplied atomically at creation.
        // Enter listening state before reporting startup success. NOWAIT never
        // waits for a peer; a connection racing startup is retained for serve().
        if (!ConnectNamedPipe(listener_, nullptr)) {
            const DWORD code = GetLastError();
            connected_ = code == ERROR_PIPE_CONNECTED;
            if (!connected_ && code != ERROR_PIPE_LISTENING)
                throw std::runtime_error("AI transport pipe listen failed: " + std::to_string(code));
        }
#else
#if !defined(__linux__) && !defined(__APPLE__) && !defined(__FreeBSD__) && \
    !defined(__OpenBSD__) && !defined(__NetBSD__) && !defined(__DragonFly__)
        throw std::runtime_error("AI transport peer authentication is unavailable on this Unix host");
#endif
#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
        throw std::runtime_error("AI transport socket SIGPIPE protection is unavailable on this Unix host");
#endif
#if defined(__linux__)
        listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
        listener_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
#endif
        if (listener_ < 0) throw std::runtime_error("AI transport socket creation failed");
#if !defined(__linux__)
        if (!configure_unix_socket(listener_))
            throw std::runtime_error("AI transport socket flag configuration failed");
#endif
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (endpoint_.size() >= sizeof(address.sun_path))
            throw std::runtime_error("AI transport socket path is too long");
        std::memcpy(address.sun_path, endpoint_.c_str(), endpoint_.size() + 1);
        // bind is exclusive. In particular, a stale PID-shaped path is never
        // permission to unlink somebody else's file, symlink or listening socket.
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
            throw std::runtime_error("AI transport exclusive socket bind failed; existing endpoint preserved");
        struct stat created{};
        if (::lstat(endpoint_.c_str(), &created) != 0 || !S_ISSOCK(created.st_mode) || created.st_uid != geteuid())
            throw std::runtime_error("AI transport cannot establish created socket identity");
        path_device_ = created.st_dev;
        path_inode_ = created.st_ino;
        owns_path_ = true;
        // No connections can be accepted until listen. Avoid process-wide umask
        // changes: seal the bound, non-listening socket before making it usable.
        if (::chmod(endpoint_.c_str(), 0600) != 0)
            throw std::runtime_error("AI transport cannot restrict socket permissions");
        struct stat secured{};
        if (::lstat(endpoint_.c_str(), &secured) != 0 || secured.st_dev != path_device_ ||
            secured.st_ino != path_inode_ || (secured.st_mode & 0777) != 0600 ||
            secured.st_uid != geteuid() || !S_ISSOCK(secured.st_mode))
            throw std::runtime_error("AI transport socket identity or permissions changed");
        if (::listen(listener_, 1) != 0) throw std::runtime_error("AI transport socket listen failed");
#endif
    }

    void close_peer() noexcept {
#ifdef _WIN32
        if (listener_ != INVALID_HANDLE_VALUE && connected_) {
            // No FlushFileBuffers: it could block indefinitely on a stalled peer.
            // Disconnect discards unread output and is only for recycling the
            // live listener. Final shutdown closes the server handle directly,
            // preserving buffered output for the existing client to consume.
            if (!stopping_) DisconnectNamedPipe(listener_);
            connected_ = false;
        }
#else
        if (peer_ >= 0) ::close(peer_);
        peer_ = -1;
#endif
    }
    void close_listener() noexcept {
#ifdef _WIN32
        // Do not DisconnectNamedPipe here: FILE_PIPE_CLOSING_STATE retains bytes
        // already written even after the server closes its last handle. This
        // needs neither a peer-controlled flush nor a protocol acknowledgement.
        if (listener_ != INVALID_HANDLE_VALUE) CloseHandle(listener_);
        listener_ = INVALID_HANDLE_VALUE;
        connected_ = false;
#else
        close_peer();
        if (listener_ >= 0) ::close(listener_);
        listener_ = -1;
        if (owns_path_) {
            struct stat current{};
            if (::lstat(endpoint_.c_str(), &current) == 0 && S_ISSOCK(current.st_mode) &&
                current.st_dev == path_device_ && current.st_ino == path_inode_)
                ::unlink(endpoint_.c_str());
        }
        owns_path_ = false;
#endif
    }

    bool accept_peer() {
        while (!stopping_) {
#ifdef _WIN32
            if (connected_) return true;
            // In NOWAIT mode a TRUE result means the instance became available
            // after disconnect, not that a client exists. Only the documented
            // ERROR_PIPE_CONNECTED status establishes a usable connection.
            if (ConnectNamedPipe(listener_, nullptr)) { pause(); continue; }
            const DWORD code = GetLastError();
            if (code == ERROR_PIPE_CONNECTED) { connected_ = true; return true; }
            if (code == ERROR_NO_DATA) DisconnectNamedPipe(listener_);
            else if (code != ERROR_PIPE_LISTENING) return false;
#else
#if defined(__linux__)
            peer_ = ::accept4(listener_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
            peer_ = ::accept(listener_, nullptr, nullptr);
#endif
            if (peer_ >= 0) {
#if !defined(__linux__)
                if (!configure_unix_socket(peer_)) { close_peer(); continue; }
#endif
                // Filesystem permissions alone do not authenticate a peer that
                // connected before ownership changes or through a passed fd.
                if (same_unix_owner(peer_)) return true;
                close_peer();
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
#endif
            pause();
        }
        return false;
    }

    // -1 means closed/error, zero means no bytes available, positive is progress.
    int read_bytes(char* output, std::size_t capacity) {
#ifdef _WIN32
        DWORD count = 0;
        if (ReadFile(listener_, output, static_cast<DWORD>(capacity), &count, nullptr))
            return static_cast<int>(count);
        return GetLastError() == ERROR_NO_DATA ? 0 : -1;
#else
        const auto count = ::recv(peer_, output, capacity, 0);
        if (count > 0) return static_cast<int>(count);
        if (count == 0) return -1;
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
#endif
    }
    bool write_frame(const std::string& bytes, bool finish_inflight = false) {
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        std::size_t offset = 0;
        while ((!stopping_ || finish_inflight) && offset < bytes.size() && Clock::now() < deadline) {
#ifdef _WIN32
            DWORD count = 0;
            // Keep each nonblocking write below the pipe's buffer quota. A
            // maximum-sized frame must make incremental progress as its peer
            // drains the pipe, not depend on one frame fitting in the buffer.
            const auto length = std::min<std::size_t>(4096, bytes.size() - offset);
            if (!WriteFile(listener_, bytes.data() + offset,
                           static_cast<DWORD>(length), &count, nullptr)) return false;
#else
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const auto count = ::send(peer_, bytes.data() + offset, bytes.size() - offset, flags);
            if (count < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { pause(); continue; }
                return false;
            }
#endif
            offset += static_cast<std::size_t>(count);
            if (!count) pause();
        }
        return offset == bytes.size();
    }
    void serve_peer() {
        if (!write_frame(handshake_)) return;
        std::string line;
        auto idle_deadline = Clock::now() + std::chrono::seconds(5);
        char bytes[4096];
        while (!stopping_ && Clock::now() < idle_deadline) {
            const int count = read_bytes(bytes, sizeof(bytes));
            if (count < 0) return;
            if (count == 0) { pause(); continue; }
            idle_deadline = Clock::now() + std::chrono::seconds(5);
            for (int index = 0; index < count && !stopping_; ++index) {
                const char byte = bytes[index];
                if (byte == '\n') {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    // The VM can complete immediately after acknowledging a
                    // resume command. Once dispatched, finish this single reply
                    // even if stop races the callback's return. No subsequent
                    // coalesced request is dispatched after stopping is observed.
                    if (!line.empty() && !write_frame(frame(handler_(line)), true)) return;
                    line.clear();
                } else {
                    if (line.size() == max_line_bytes) return;
                    line.push_back(byte);
                }
            }
        }
    }
    void serve() noexcept {
        while (!stopping_) {
            if (!accept_peer()) break;
            try { serve_peer(); }
            catch (...) { /* A bad request/handler closes only its connection. */ }
            close_peer();
        }
    }
};
} // namespace ny_ai
#endif

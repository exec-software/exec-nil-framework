#include "../nilagent/ipc_hot_reload.hpp"
#include <atomic>
#include <exception>
#include <functional>
#include <iostream>
#include <type_traits>

namespace {
using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int pid() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}
std::string greeting(int process = pid()) {
    return "{\"event\":\"connected\",\"pid\":" + std::to_string(process) +
           ",\"ai_version\":1,\"program\":\"ny\"}\n";
}
std::string response(unsigned id = 1, std::string result = {}) {
    if (result.empty()) result = "{\"running\":true,\"ai_version\":1,\"nil_version\":\"fixture\",\"pid\":" + std::to_string(pid()) + "}";
    return "{\"id\":\"" + std::to_string(id) + "\",\"result\":" + result + ",\"error\":null}\n";
}

// Short-lived private protocol peer. No process launch or actual interpreter
// state mutation. All fixture I/O is nonblocking and deadline/stop bounded.
class fixture {
public:
    explicit fixture(std::function<void(fixture&)> body) : path(ny::ipc::get_socket_path_for_pid(pid())) {
#ifdef _WIN32
        const std::wstring wide(path.begin(), path.end());
        pipe = CreateNamedPipeW(wide.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 4096, 4096, 0, nullptr);
        require(pipe != INVALID_HANDLE_VALUE, "create exclusive private fixture pipe");
#else
        listener = socket(AF_UNIX, SOCK_STREAM, 0);
        require(listener >= 0, "create fixture listener");
        sockaddr_un address{}; address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(listener); listener = -1;
            throw std::runtime_error("exclusive fixture bind failed; existing endpoint preserved");
        }
        owns_path = true;
        if (chmod(path.c_str(), 0600) != 0 || listen(listener, 1) != 0 || fcntl(listener, F_SETFL, O_NONBLOCK) != 0) {
            cleanup(); throw std::runtime_error("initialize fixture listener");
        }
#endif
        try {
            worker = std::thread([this, body = std::move(body)] {
                try {
                    if (!accept_peer()) throw std::runtime_error("fixture accept deadline");
                    body(*this);
                    // Keep the peer alive until the client closes, so Windows
                    // DisconnectNamedPipe cannot discard an unread last frame.
                    const auto deadline = clock_type::now() + 2s;
                    while (!stop && clock_type::now() < deadline) {
                        char byte;
                        if (read_bytes(&byte, 1) < 0) break;
                        std::this_thread::sleep_for(1ms);
                    }
                } catch (...) { failure = std::current_exception(); }
            });
        } catch (...) { cleanup(); throw; }
    }
    ~fixture() { stop = true; if (worker.joinable()) worker.join(); cleanup(); }
    fixture(const fixture&) = delete;
    fixture& operator=(const fixture&) = delete;
    void finish() { stop = true; if (worker.joinable()) worker.join(); if (failure) std::rethrow_exception(failure); }
    bool send(const std::string& bytes, std::size_t chunk = 7) {
        const auto deadline = clock_type::now() + 2s;
        std::size_t offset = 0;
        while (!stop && offset < bytes.size() && clock_type::now() < deadline) {
            const auto length = std::min(chunk, bytes.size() - offset);
#ifdef _WIN32
            DWORD count = 0;
            if (!WriteFile(pipe, bytes.data() + offset, static_cast<DWORD>(length), &count, nullptr)) return false;
#else
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const auto count = ::send(peer, bytes.data() + offset, length, flags);
            if (count < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { std::this_thread::sleep_for(1ms); continue; }
                return false;
            }
#endif
            offset += static_cast<std::size_t>(count);
            if (!count) std::this_thread::sleep_for(1ms);
        }
        return offset == bytes.size();
    }
    std::string request() {
        std::string line;
        const auto deadline = clock_type::now() + 2s;
        while (!stop && line.size() < 1024 && clock_type::now() < deadline) {
            char byte;
            const int count = read_bytes(&byte, 1);
            if (count < 0) break;
            if (count) { line += byte; if (byte == '\n') return line; }
            else std::this_thread::sleep_for(1ms);
        }
        throw std::runtime_error("missing newline-delimited fixture request");
    }
    void idle() { while (!stop) std::this_thread::sleep_for(1ms); }
    void break_connection() {
#ifdef _WIN32
        DisconnectNamedPipe(pipe);
#else
        shutdown(peer, SHUT_RDWR);
#endif
    }
    const std::string path;
private:
    std::atomic<bool> stop{false};
    std::thread worker;
    std::exception_ptr failure;
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
#else
    int listener = -1, peer = -1;
    bool owns_path = false;
#endif
    bool accept_peer() {
        const auto deadline = clock_type::now() + 2s;
        while (!stop && clock_type::now() < deadline) {
#ifdef _WIN32
            if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) return true;
            if (GetLastError() != ERROR_PIPE_LISTENING && GetLastError() != ERROR_NO_DATA) return false;
#else
            peer = accept(listener, nullptr, nullptr);
            if (peer >= 0) return fcntl(peer, F_SETFL, O_NONBLOCK) == 0;
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
#endif
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }
    int read_bytes(char* data, unsigned length) {
#ifdef _WIN32
        DWORD count = 0;
        if (ReadFile(pipe, data, length, &count, nullptr)) return count ? static_cast<int>(count) : -1;
        return GetLastError() == ERROR_NO_DATA ? 0 : -1;
#else
        const auto count = recv(peer, data, length, 0);
        if (count > 0) return static_cast<int>(count);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;
#endif
    }
    void cleanup() {
#ifdef _WIN32
        if (pipe != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(pipe); CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; }
#else
        if (peer >= 0) close(peer);
        if (listener >= 0) close(listener);
        if (owns_path) unlink(path.c_str());
        peer = listener = -1; owns_path = false;
#endif
    }
};

void expect_request(fixture& server, unsigned id = 1) {
    require(server.request() == "{\"id\":\"" + std::to_string(id) + "\",\"method\":\"vm.status\"}\n",
            "client did not send actual AI status protocol");
}
void success() {
    fixture server([](fixture& peer) {
        require(peer.send(greeting(), 1), "fragmented greeting");
        expect_request(peer);
        require(peer.send(response(1, "{\"running\":true,\"ai_version\":1,\"nil_version\":\"fixture\",\"pid\":" + std::to_string(pid()) +
            ",\"text\":\"caf\u00e9 \u65e5\u672c\u8a9e\",\"large\":\"" + std::string(12000, 'x') + "\"}"), 31), "large response");
        expect_request(peer, 2);
        require(peer.send(response(2)), "second response");
    });
    const auto found = ny::ipc::find_server_pids();
    require(std::binary_search(found.begin(), found.end(), pid()), "discovery missed private endpoint");
    ny::ipc::IpcClient client;
    require(client.connect(server.path), "handshake connection failed");
    // Unsupported calls must not emit obsolete cmd requests or consume replies.
    require(ny::ipc::json_extract_string(client.send_reload("\"\n"), "ok") == "false", "reload fabricated support");
    require(ny::ipc::json_extract_string(client.send_snapshot(), "ok") == "false", "snapshot fabricated support");
    const auto status = nil::detail::json_parser(client.send_status()).parse();
    require(status.at("large").text.size() == 12000 && status.at("text").text == u8"caf\u00e9 \u65e5\u672c\u8a9e", "fragmented/large UTF8 status corrupted");
    require(ny::ipc::json_extract_string(client.send_status(), "running") == "true", "second request/ID failed");
    client.disconnect(); server.finish();
    require(!client.is_connected(), "disconnect retained state");
}
void invalid_handshake(const std::string& line) {
    fixture server([&](fixture& peer) { (void)peer.send(line, 1024); });
    ny::ipc::IpcClient client(150ms);
    require(!client.connect(server.path) && !client.is_connected(), "bad handshake accepted");
    server.finish();
}
void invalid_response(const std::string& line) {
    fixture server([&](fixture& peer) { require(peer.send(greeting()), "greeting"); expect_request(peer); (void)peer.send(line, 1024); });
    ny::ipc::IpcClient client(150ms);
    require(client.connect(server.path), "negative fixture handshake failed");
    require(client.send_status().empty() && !client.is_connected(), "bad response accepted or connection reused");
    server.finish();
}
void deadlines() {
    {
        fixture server([](fixture& peer) { peer.idle(); });
        ny::ipc::IpcClient client(100ms);
        const auto start = clock_type::now();
        require(!client.connect(server.path), "missing greeting accepted");
        require(clock_type::now() - start < 1s && client.last_error().find("deadline") != std::string::npos, "greeting deadline not enforced");
        server.finish();
    }
    {
        fixture server([](fixture& peer) { require(peer.send(greeting()), "greeting"); expect_request(peer); peer.idle(); });
        ny::ipc::IpcClient client(100ms);
        require(client.connect(server.path), "timeout fixture connect");
        const auto start = clock_type::now();
        require(client.send_status().empty() && !client.is_connected(), "silent server accepted");
        require(clock_type::now() - start < 1s && client.last_error().find("deadline") != std::string::npos, "response deadline not enforced");
        server.finish();
    }
}
void reconnect_and_error() {
    ny::ipc::IpcClient client;
    for (int iteration = 0; iteration != 3; ++iteration) {
        fixture server([](fixture& peer) {
            require(peer.send(greeting()), "greeting"); expect_request(peer);
            require(peer.send("{\"id\":\"1\",\"result\":null,\"error\":\"not ready\"}\n"), "error frame");
            expect_request(peer, 2); require(peer.send(response(2)), "recovery frame");
        });
        require(client.connect(server.path), "reconnect failed");
        require(client.send_status().empty() && client.is_connected() && client.last_error().find("not ready") != std::string::npos,
                "valid remote error lost");
        require(!client.send_status().empty(), "remote error desynchronized connection");
        require(!client.connect("\\\\remote-host\\pipe\\ny_ai_123"), "remote endpoint accepted");
        require(!client.is_connected(), "failed reconnect retained prior handle");
        server.finish();
    }
}
void disconnect_and_backpressure() {
    {
        fixture server([](fixture& peer) {
            require(peer.send(greeting()), "greeting"); expect_request(peer);
            require(peer.send("{\"id\":"), "partial frame"); peer.break_connection();
        });
        ny::ipc::IpcClient client(150ms);
        require(client.connect(server.path), "disconnect fixture connect");
        require(client.send_status().empty() && !client.is_connected(), "truncated disconnected frame accepted");
        server.finish();
    }
    {
        // This peer emits correlated responses but never drains requests. The
        // finite pipe/socket input buffer must eventually make the client's
        // write return partial/zero/EAGAIN. It must time out, not block forever.
        fixture server([](fixture& peer) {
            require(peer.send(greeting()), "greeting");
            for (unsigned id = 1; id <= 4000; ++id) if (!peer.send(response(id), 4096)) return;
        });
        ny::ipc::IpcClient client(100ms);
        require(client.connect(server.path), "backpressure fixture connect");
        const auto start = clock_type::now();
        unsigned completed = 0;
        while (completed < 4000 && !client.send_status().empty()) ++completed;
        require(completed > 1 && completed < 4000 && !client.is_connected(), "full request pipe was not bounded");
        require(clock_type::now() - start < 5s && client.last_error().find("deadline") != std::string::npos,
                "backpressure deadline not enforced");
        server.finish();
    }
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<ny::ipc::IpcClient> && !std::is_move_constructible_v<ny::ipc::IpcClient>);
    try {
        for (const auto* name : {"ny_ai_", "ny_ai_0", "ny_ai_-1", "ny_ai_01", "ny_ai_999999999999999999999999", "ny_ai_12.sock.extra"})
            require(ny::ipc::pid_from_endpoint_name(name) == 0, "ambiguous endpoint parsed");
        require(ny::ipc::get_socket_path_for_pid(0).empty(), "invalid PID created endpoint");
        success();
        invalid_handshake(greeting(pid() + 1));
        invalid_handshake("{\"event\":\"connected\",\"pid\":1,\"pid\":2,\"ai_version\":1}\n");
        invalid_handshake(std::string(65537, 'x') + "\n");
        invalid_response(response(99));
        invalid_response(response(1, "{}"));
        invalid_response(response(1, "{\"running\":true,\"ai_version\":1,\"nil_version\":\"fixture\",\"pid\":0}"));
        invalid_response("{\"id\":\"1\",\"result\":{},\"error\":null}garbage\n");
        invalid_response("{\"id\":\"1\",\"result\":{},\"error\":null,\"id\":\"1\"}\n");
        invalid_response("{\"id\":\"1\",\"result\":\"not an object\",\"error\":null}\n");
        invalid_response(std::string(65537, 'x') + "\n");
        deadlines(); reconnect_and_error(); disconnect_and_backpressure();
        const auto after = ny::ipc::find_server_pids();
        require(!std::binary_search(after.begin(), after.end(), pid()), "fixture endpoint survived cleanup");
        std::cout << "nilagent local IPC: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "nilagent local IPC: " << error.what() << '\n';
        return 1;
    }
}

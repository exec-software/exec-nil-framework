#include "../nilinterpreter/src/ai_transport.hpp"
#include <condition_variable>
#include <iostream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
int checks = 0;
const char* stage = "constructor validation";
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
void pause() { std::this_thread::sleep_for(2ms); }

class Client {
public:
    explicit Client(const std::string& endpoint) {
        const auto deadline = Clock::now() + 2s;
        while (Clock::now() < deadline) {
#ifdef _WIN32
            const std::wstring name(endpoint.begin(), endpoint.end());
            handle_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                0, nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
                if (!SetNamedPipeHandleState(handle_, &mode, nullptr, nullptr)) {
                    close(); throw std::runtime_error("client nonblocking mode");
                }
                return;
            }
#else
#if defined(__linux__)
            handle_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
            handle_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
#endif
            if (handle_ < 0) throw std::runtime_error("client socket");
#if !defined(__linux__)
            const int status = ::fcntl(handle_, F_GETFL, 0);
            const int descriptor = ::fcntl(handle_, F_GETFD, 0);
            bool configured = status >= 0 && descriptor >= 0 &&
                ::fcntl(handle_, F_SETFL, status | O_NONBLOCK) == 0 &&
                ::fcntl(handle_, F_SETFD, descriptor | FD_CLOEXEC) == 0;
#ifndef MSG_NOSIGNAL
#ifdef SO_NOSIGPIPE
            const int enabled = 1;
            configured = configured &&
                ::setsockopt(handle_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#else
            configured = false;
#endif
#endif
            if (!configured) { close(); throw std::runtime_error("client socket flag configuration"); }
#endif
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
            if (::connect(handle_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) return;
            close();
#endif
            pause();
        }
        throw std::runtime_error("client connection deadline");
    }
    ~Client() { close(); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    void close() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
#else
        if (handle_ >= 0) ::close(handle_);
        handle_ = -1;
#endif
    }
    bool send(const std::string& bytes, std::size_t chunk = 4096) {
        const auto deadline = Clock::now() + 2s;
        std::size_t offset = 0;
        while (offset < bytes.size() && Clock::now() < deadline) {
            const auto length = std::min(chunk, bytes.size() - offset);
#ifdef _WIN32
            DWORD count = 0;
            if (!WriteFile(handle_, bytes.data() + offset, static_cast<DWORD>(length), &count, nullptr)) return false;
#else
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const auto count = ::send(handle_, bytes.data() + offset, length, flags);
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
    std::string line() {
        const auto deadline = Clock::now() + 2s;
        while (Clock::now() < deadline) {
            const auto newline = pending_.find('\n');
            if (newline != std::string::npos) {
                auto result = pending_.substr(0, newline);
                pending_.erase(0, newline + 1);
                return result;
            }
            char bytes[8192];
            const int count = read(bytes, sizeof(bytes));
            if (count < 0) throw std::runtime_error("client closed before complete response");
            if (count == 0) pause();
            else pending_.append(bytes, static_cast<std::size_t>(count));
            if (pending_.size() > ny_ai::Transport::max_line_bytes + 1)
                throw std::runtime_error("client response exceeded limit");
        }
        throw std::runtime_error("client response deadline");
    }
    bool closed(std::chrono::milliseconds timeout = 2s) {
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline) {
            char byte;
            if (read(&byte, 1) < 0) return true;
            pause();
        }
        return false;
    }
#ifdef _WIN32
    void check_security() {
        ULONG process = 0;
        require(GetNamedPipeServerProcessId(handle_, &process) && process == GetCurrentProcessId(),
                "pipe belongs to this test process");
        nil::detail::owner_security expected;
        std::error_code error;
        require(expected.initialize(false, error), "test owner identity");
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PSID owner = nullptr;
        PACL acl = nullptr;
        require(GetSecurityInfo(handle_, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                    &owner, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS,
                "pipe security query");
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        void* raw = nullptr;
        bool valid = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
            (control & SE_DACL_PROTECTED) && owner && EqualSid(owner, expected.sid()) &&
            acl && IsValidAcl(acl) && acl->AceCount == 1 && GetAce(acl, 0, &raw);
        if (valid) {
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
            valid = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ace->Header.AceFlags == 0 &&
                ace->Mask == FILE_ALL_ACCESS && EqualSid(const_cast<DWORD*>(&ace->SidStart), expected.sid());
        }
        LocalFree(descriptor);
        require(valid, "pipe has protected current-user-only DACL");
    }
#endif
private:
    std::string pending_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
    int read(char* output, std::size_t capacity) {
#ifdef _WIN32
        DWORD count = 0;
        if (ReadFile(handle_, output, static_cast<DWORD>(capacity), &count, nullptr)) return static_cast<int>(count);
        return GetLastError() == ERROR_NO_DATA ? 0 : -1;
#else
        const auto count = ::recv(handle_, output, capacity, 0);
        if (count > 0) return static_cast<int>(count);
        if (count == 0) return -1;
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
#endif
    }
};

void protocol_and_lifecycle() {
    stage = "protocol startup";
    std::atomic<unsigned> calls{0};
    ny_ai::Transport server("{\"event\":\"connected\"}\n", [&](const std::string& input) {
        ++calls;
        if (input == "throw") throw std::runtime_error("fixture command failure");
        if (input == "oversize-response") return std::string(ny_ai::Transport::max_line_bytes + 1, 'x');
        if (input == "multiline-response") return std::string("one\ntwo");
        if (input == "terminated-response") return std::string("already terminated\n");
        return input;
    });
    server.start();
    rejects([&] { server.start(); }, "repeated start must fail");
#ifndef _WIN32
    struct stat info{};
    require(::lstat(server.endpoint().c_str(), &info) == 0 && S_ISSOCK(info.st_mode) &&
            (info.st_mode & 0777) == 0600 && info.st_uid == geteuid(), "socket owner-only before client connection");
#endif
    {
        stage = "initial handshake and security";
        Client client(server.endpoint());
#ifdef _WIN32
        client.check_security();
#endif
        require(client.line() == "{\"event\":\"connected\"}", "startup readiness and exact handshake");
        stage = "fragmented request";
        require(client.send("fragmented command\r\n", 1), "fragmented request write");
        require(client.line() == "fragmented command", "fragmented request and CRLF framing");
        stage = "coalesced requests";
        require(client.send("first\n\nsecond\nthird\n"), "coalesced requests write");
        require(client.line() == "first" && client.line() == "second" && client.line() == "third",
                "coalesced requests stay ordered and blank lines ignored");
        stage = "already-terminated response";
        require(client.send("terminated-response\n"), "terminated response request");
        require(client.line() == "already terminated", "already terminated response has one newline");
        const std::string maximum(ny_ai::Transport::max_line_bytes, 'z');
        stage = "maximum request write";
        require(client.send(maximum + "\n", 997), "maximum bounded request write");
        stage = "maximum response read";
        require(client.line() == maximum, "maximum response survives partial writes without truncation");
        require(calls == 6, "exact callback count for first connection");
        stage = "exclusive endpoint contender";
        ny_ai::Transport contender("other", [](const std::string& request) { return request; });
        rejects([&] { contender.start(); }, "second server must not replace live endpoint");
        contender.stop();
        require(client.send("still alive\n") && client.line() == "still alive",
                "failed contender leaves original listener and peer unchanged");
    }
    {
        stage = "oversized request reconnect";
        Client client(server.endpoint());
        require(client.line() == "{\"event\":\"connected\"}", "reconnect handshake");
        const auto before = calls.load();
        // A rejected oversized frame is never passed to the controller. The
        // sender may observe closure before all its bytes are accepted.
        client.send(std::string(ny_ai::Transport::max_line_bytes + 1, 'x') + "\n");
        require(client.closed(), "oversized request closes bounded connection");
        require(calls == before, "oversized request never dispatched");
    }
    for (const auto* command : {"throw\n", "oversize-response\n", "multiline-response\n"}) {
        stage = "invalid handler response";
        Client client(server.endpoint());
        (void)client.line();
        require(client.send(command), "invalid-response fixture request");
        require(client.closed(), "handler failure or invalid response closes connection");
    }
    {
        stage = "partial request shutdown";
        Client client(server.endpoint());
        (void)client.line();
        require(client.send("partial"), "partial frame fixture");
        const auto before = calls.load();
        const auto start = Clock::now();
        server.stop();
        require(Clock::now() - start < 1s, "stop interrupts idle partial-frame reader promptly");
        require(calls == before, "partial request never dispatched");
        require(client.closed(), "stop closes accepted client");
    }
    stage = "empty listener restart and destructor cleanup";
    server.stop(); // Idempotent cleanup.
#ifndef _WIN32
    require(::lstat(server.endpoint().c_str(), &info) != 0 && errno == ENOENT, "owned socket removed after join");
#endif
    server.start();
    const auto start = Clock::now();
    server.stop();
    require(Clock::now() - start < 1s, "stop interrupts empty accept promptly");
    {
        ny_ai::Transport automatic("ready", [](const std::string& request) { return request; });
        automatic.start();
        Client client(automatic.endpoint());
        require(client.line() == "ready", "destructor cleanup fixture handshake");
    }
    server.start(); // Destructor must have released the exclusive name.
    server.stop();
}

void idle_and_slow_peer() {
    stage = "idle peer expiry";
    std::atomic<unsigned> calls{0};
    ny_ai::Transport server("ready", [&](const std::string&) {
        ++calls;
        return std::string(ny_ai::Transport::max_line_bytes, 'x');
    });
    server.start();
    {
        Client idle(server.endpoint());
        require(idle.line() == "ready", "idle peer handshake");
        const auto start = Clock::now();
        require(idle.closed(6s), "idle peer is disconnected within bounded deadline");
        require(Clock::now() - start >= 4s && calls == 0, "idle timeout does not fabricate commands");
    }
    {
        stage = "stalled reader shutdown";
        Client stalled(server.endpoint());
        require(stalled.line() == "ready", "stalled peer reconnect handshake");
        require(stalled.send(std::string(256, '\n') + "large\nlarge\nlarge\nlarge\nlarge\nlarge\n"),
                "stalled-reader bounded requests");
        const auto callback_deadline = Clock::now() + 1s;
        while (calls == 0 && Clock::now() < callback_deadline) pause();
        require(calls > 0, "stalled-reader response begins");
        const auto start = Clock::now();
        server.stop();
        require(Clock::now() - start < 2s, "stop bounds in-flight reply to non-reading peer without flush");
    }
}

void shutdown_retains_inflight_reply() {
    stage = "in-flight reply shutdown handshake";
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    std::atomic<unsigned> calls{0};
    ny_ai::Transport server("ready", [&](const std::string&) {
        ++calls;
        {
            std::lock_guard<std::mutex> lock(mutex);
            entered = true;
        }
        changed.notify_all();
        // Model the owner acknowledging resume while the worker is still
        // returning from its controller wait. This callback remains bounded.
        std::this_thread::sleep_for(100ms);
        return std::string("final response before process exit");
    });
    server.start();
    Client client(server.endpoint());
    require(client.line() == "ready", "shutdown-race fixture handshake");
    require(client.send("resume\nmust-not-dispatch\n"), "shutdown-race coalesced commands");
    bool observed = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        observed = changed.wait_for(lock, 1s, [&] { return entered; });
    }
    const auto start = Clock::now();
    stage = "in-flight reply shutdown";
    server.stop();
    require(observed && Clock::now() - start < 2s, "in-flight shutdown deadline");
    // Intentionally do not read until stop has returned and the server handle
    // has closed. On Windows DisconnectNamedPipe would discard this response.
    stage = "completed reply read after server handle closure";
    require(client.line() == "final response before process exit", "stop discarded completed command response");
    require(client.closed(), "final response is followed by orderly closure");
    require(calls == 1, "stop dispatched another buffered command");
}

#ifndef _WIN32
void existing_paths_and_replacement() {
    stage = "preexisting and replacement endpoint preservation";
    ny_ai::Transport server("ready", [](const std::string& request) { return request; });
    const auto& path = server.endpoint();
    const int file = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    require(file >= 0, "exclusive preexisting-file fixture creation");
    struct FileCleanup { std::string path; ~FileCleanup() { ::unlink(path.c_str()); } } cleanup{path};
    require(::write(file, "keep", 4) == 4, "existing-file fixture write");
    ::close(file);
    struct stat before{}, after{};
    require(::lstat(path.c_str(), &before) == 0, "preexisting-file identity");
    rejects([&] { server.start(); }, "existing ordinary file must block startup");
    require(::lstat(path.c_str(), &after) == 0 && before.st_ino == after.st_ino && after.st_size == 4,
            "startup failure preserves existing file");
    ::unlink(path.c_str());
    require(::symlink("nil-ai-transport-nonexistent-target", path.c_str()) == 0, "existing symlink fixture");
    rejects([&] { server.start(); }, "existing symlink must block startup");
    require(::lstat(path.c_str(), &after) == 0 && S_ISLNK(after.st_mode), "existing symlink preserved");
    ::unlink(path.c_str());
    server.start();
    require(::unlink(path.c_str()) == 0, "remove own test socket pathname for replacement control");
    const int replacement = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    require(replacement >= 0, "replacement-file fixture");
    ::close(replacement);
    server.stop();
    require(::lstat(path.c_str(), &after) == 0 && S_ISREG(after.st_mode), "stop preserves replacement pathname");
}
#endif
} // namespace

int main() {
    try {
        rejects([] { ny_ai::Transport bad("one\ntwo", [](const std::string&) { return ""; }); }, "multiline handshake rejected");
        rejects([] { ny_ai::Transport bad(std::string(65537, 'x'), [](const std::string&) { return ""; }); }, "oversized handshake rejected");
        rejects([] { ny_ai::Transport bad("ready", {}); }, "empty callback rejected");
        protocol_and_lifecycle();
        idle_and_slow_peer();
        shutdown_retains_inflight_reply();
#ifndef _WIN32
        existing_paths_and_replacement();
#endif
        std::cout << "AI transport: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "AI transport test failure at " << stage << " (" << checks
                  << " checks completed): " << error.what() << '\n';
        return 1;
    }
}

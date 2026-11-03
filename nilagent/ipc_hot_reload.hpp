#pragma once
// Experimental local-only client for nilinterpreter --ai. Both transports use
// a connected handshake and newline-delimited id/method JSON, NOT legacy cmd.
// Checkpoints are capability-negotiated, same-process VM execution state.
// Code hot reload is distinct and is not implemented by this protocol.
#include "../common/strict_json.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <dirent.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace ny::ipc {
inline int positive_pid(const std::string& text) {
    if (text.empty() || text.front() == '0') return 0;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || value > (std::numeric_limits<int>::max() - (c - '0')) / 10) return 0;
        value = value * 10 + (c - '0');
    }
    return value;
}
inline int pid_from_endpoint_name(const std::string& name) {
    const std::string prefix = "ny_ai_";
#ifdef _WIN32
    const std::string suffix;
#else
    const std::string suffix = ".sock";
#endif
    if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return 0;
    return positive_pid(name.substr(prefix.size(), name.size() - prefix.size() - suffix.size()));
}
inline std::string get_socket_path_for_pid(int pid) {
    if (pid <= 0) return {};
#ifdef _WIN32
    return "\\\\.\\pipe\\ny_ai_" + std::to_string(pid);
#else
    return "/tmp/ny_ai_" + std::to_string(pid) + ".sock";
#endif
}
// A bounded snapshot of advertised endpoints, not authentication or a promise
// the process stays alive. Discovery never connects to a server.
inline std::vector<int> find_server_pids() {
    std::vector<int> pids;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
#ifdef _WIN32
    WIN32_FIND_DATAW entry{};
    struct search_handle { HANDLE value; ~search_handle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); } }
        search{FindFirstFileW(L"\\\\.\\pipe\\*", &entry)};
    if (search.value == INVALID_HANDLE_VALUE) return pids;
    for (unsigned count = 0; count < 4096 && std::chrono::steady_clock::now() < deadline; ++count) {
        std::string name;
        for (const wchar_t* p = entry.cFileName; *p && *p <= 127; ++p) name += static_cast<char>(*p);
        if (name.size() == std::wcslen(entry.cFileName)) {
            const int pid = pid_from_endpoint_name(name);
            if (pid) pids.push_back(pid);
        }
        if (!FindNextFileW(search.value, &entry)) break;
    }
#else
    struct directory_closer { void operator()(DIR* value) const { closedir(value); } };
    std::unique_ptr<DIR, directory_closer> directory(opendir("/tmp"));
    if (!directory) return pids;
    for (unsigned count = 0; count < 4096 && std::chrono::steady_clock::now() < deadline; ++count) {
        const auto* entry = readdir(directory.get());
        if (!entry) break;
        const int pid = pid_from_endpoint_name(entry->d_name);
        struct stat state{};
        if (pid && lstat(get_socket_path_for_pid(pid).c_str(), &state) == 0 && S_ISSOCK(state.st_mode)) pids.push_back(pid);
    }
#endif
    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    return pids;
}
inline std::string json_extract_string(const std::string& text, const std::string& key) {
    try {
        const auto value = nil::detail::json_parser(text).parse().at(key);
        if (value.type == nil::detail::json::kind::string) return value.text;
        if (value.type == nil::detail::json::kind::boolean) return value.boolean ? "true" : "false";
    } catch (const std::exception&) {}
    return {};
}
inline int json_extract_int(const std::string& text, const std::string& key) {
    try {
        const auto value = nil::detail::json_parser(text).parse().at(key);
        if (value.type == nil::detail::json::kind::number) return positive_pid(value.text);
    } catch (const std::exception&) {}
    return 0;
}

class IpcClient {
public:
    explicit IpcClient(std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
        : timeout_(std::clamp(timeout, std::chrono::milliseconds(1), std::chrono::milliseconds(30000))) {}
    ~IpcClient() { disconnect(); }
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&&) = delete;
    IpcClient& operator=(IpcClient&&) = delete;
    bool connect(const std::string& path) {
        disconnect(); error_.clear();
#ifdef _WIN32
        const std::string prefix = "\\\\.\\pipe\\";
#else
        const std::string prefix = "/tmp/";
#endif
        const int pid = path.compare(0, prefix.size(), prefix) == 0 ? pid_from_endpoint_name(path.substr(prefix.size())) : 0;
        if (!pid || path != get_socket_path_for_pid(pid)) return fail("invalid local AI endpoint");
        const auto deadline = clock::now() + timeout_;
        if (!open_endpoint(path, pid, deadline)) { disconnect(); return false; }
        try {
            std::string line;
            if (!read_line(line, deadline)) { disconnect(); return false; }
            const auto greeting = nil::detail::json_parser(line).parse();
            if (greeting.at("event").type != json::kind::string || greeting.at("event").text != "connected" ||
                greeting.at("pid").type != json::kind::number || positive_pid(greeting.at("pid").text) != pid ||
                greeting.at("ai_version").type != json::kind::number || greeting.at("ai_version").text != "1")
                return fail("invalid AI server handshake");
        } catch (const std::exception&) { return fail("invalid AI server handshake"); }
        connected_pid_ = pid;
        connected_ = true; return true;
    }
    void disconnect() {
#ifdef _WIN32
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
#endif
        connected_ = false; connected_pid_ = 0; incoming_.clear(); next_id_ = 0; checkpoints_ = false;
    }
    bool is_connected() const { return connected_; }
    const std::string& last_error() const { return error_; }
    std::string send_reload(const std::string&) {
        return "{\"ok\":false,\"error\":\"reload is not supported by the AI server protocol\"}";
    }
    std::string send_snapshot() {
        if (!checkpoints_) return "{\"ok\":false,\"error\":\"checkpoint capability has not been negotiated; request status first\"}";
        return send_control("vm.snapshot");
    }
    std::string send_restore(const std::string& checkpoint) {
        if (!checkpoints_) return "{\"ok\":false,\"error\":\"checkpoint capability has not been negotiated; request status first\"}";
        return send_control("vm.restore", checkpoint);
    }
    std::string send_control(const std::string& method, const std::string& checkpoint = {}) {
        if (method != "vm.pause" && method != "vm.resume" && method != "vm.step" &&
            method != "vm.inspect" && method != "vm.snapshot" && method != "vm.restore") {
            error_ = "unsupported control request"; return {};
        }
        if ((method == "vm.snapshot" || method == "vm.restore") && !checkpoints_) {
            error_ = "checkpoint capability has not been negotiated"; return {};
        }
        if (!connected_) { error_ = "not connected"; return {}; }
        const std::string id = std::to_string(++next_id_);
        auto request = json::map(); request.object["id"] = json::str(id); request.object["method"] = json::str(method);
        if (method == "vm.restore") {
            if (checkpoint.empty() || checkpoint.size() > 20 ||
                !std::all_of(checkpoint.begin(), checkpoint.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                error_ = "invalid checkpoint ID"; return {};
            }
            request.object["checkpoint"] = json::str(checkpoint);
        }
        const auto deadline = clock::now() + timeout_;
        std::string line;
        if (!write_all(nil::detail::dump(request) + "\n", deadline) || !read_line(line, deadline)) { disconnect(); return {}; }
        try {
            const auto reply = nil::detail::json_parser(line).parse();
            if (reply.at("id").type != json::kind::string || reply.at("id").text != id)
                throw std::runtime_error("mismatched response ID");
            if (reply.at("error").type != json::kind::null) {
                if (reply.at("error").type != json::kind::string || reply.at("result").type != json::kind::null)
                    throw std::runtime_error("invalid error response");
                error_ = reply.at("error").text; return "{\"ok\":false,\"error\":" + nil::detail::quote(error_) + "}";
            }
            const auto& result = reply.at("result");
            if (result.type != json::kind::object) throw std::runtime_error("invalid control result");
            if (method != "vm.inspect" && (result.at("ok").type != json::kind::boolean || !result.at("ok").boolean))
                throw std::runtime_error("invalid control acknowledgement");
            if (method == "vm.snapshot" || method == "vm.restore") {
                if (result.at("checkpoint").type != json::kind::string || result.at("checkpoint").text.empty())
                    throw std::runtime_error("missing checkpoint identity");
                if (method == "vm.restore" && result.at("checkpoint").text != checkpoint)
                    throw std::runtime_error("mismatched checkpoint identity");
            }
            error_.clear(); return nil::detail::dump(result);
        } catch (const std::exception&) { fail("invalid AI control response"); return {}; }
    }
    std::string send_status() {
        if (!connected_) { error_ = "not connected"; return {}; }
        const std::string id = std::to_string(++next_id_);
        const auto deadline = clock::now() + timeout_;
        std::string line;
        if (!write_all("{\"id\":\"" + id + "\",\"method\":\"vm.status\"}\n", deadline) || !read_line(line, deadline)) {
            disconnect(); return {};
        }
        try {
            const auto response = nil::detail::json_parser(line).parse();
            if (response.at("id").type != json::kind::string || response.at("id").text != id)
                throw std::runtime_error("mismatched response ID");
            if (response.at("error").type != json::kind::null) {
                if (response.at("error").type != json::kind::string) throw std::runtime_error("invalid error");
                error_ = "AI server rejected status: " + response.at("error").text; return {};
            }
            const auto& result = response.at("result");
            if (result.type != json::kind::object || result.at("running").type != json::kind::boolean ||
                result.at("pid").type != json::kind::number || positive_pid(result.at("pid").text) != connected_pid_ ||
                result.at("ai_version").type != json::kind::number || result.at("ai_version").text != "1" ||
                result.at("nil_version").type != json::kind::string)
                throw std::runtime_error("invalid status result");
            checkpoints_ = false;
            const auto capabilities = result.object.find("capabilities");
            if (capabilities != result.object.end()) {
                if (capabilities->second.type != json::kind::array || capabilities->second.array.size() > 32)
                    throw std::runtime_error("invalid capabilities");
                for (const auto& capability : capabilities->second.array) {
                    if (capability.type != json::kind::string) throw std::runtime_error("invalid capability");
                    if (capability.text == "checkpoint.same-process.v1") checkpoints_ = true;
                }
            }
            error_.clear(); return nil::detail::dump(result);
        } catch (const std::exception&) { fail("invalid AI server response"); return {}; }
    }
private:
    using json = nil::detail::json;
    using clock = std::chrono::steady_clock;
    using deadline_type = clock::time_point;
    static constexpr std::size_t frame_limit = 64 * 1024;
    std::chrono::milliseconds timeout_;
    bool connected_ = false;
    bool checkpoints_ = false;
    int connected_pid_ = 0;
    std::string incoming_, error_;
    unsigned long long next_id_ = 0;
#ifdef _WIN32
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    bool fail(const char* message) { error_ = message; disconnect(); return false; }
    bool wait_retry(deadline_type deadline) {
        if (clock::now() >= deadline) { error_ = "AI IPC deadline exceeded"; return false; }
        std::this_thread::sleep_for(std::min(deadline - clock::now(),
            std::chrono::duration_cast<clock::duration>(std::chrono::milliseconds(2))));
        return true;
    }
    bool open_endpoint(const std::string& path, int pid, deadline_type deadline) {
#ifdef _WIN32
        const std::wstring wide(path.begin(), path.end()); // validated ASCII local name
        do {
            pipe_ = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_PIPE_BUSY) { error_ = "could not open AI pipe"; return false; }
        } while (wait_retry(deadline));
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        ULONG server_pid = 0;
        if (!GetNamedPipeServerProcessId(pipe_, &server_pid) || server_pid != static_cast<ULONG>(pid))
            return fail("AI pipe server PID mismatch");
        // Synchronous nonblocking byte stream: no outstanding OVERLAPPED
        // buffers survive a deadline. Partial writes and ERROR_NO_DATA reads
        // are retried under one deadline, with no FlushFileBuffers wait.
        // https://learn.microsoft.com/windows/win32/ipc/named-pipe-type-read-and-wait-modes
        DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
        if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) return fail("could not set AI pipe mode");
        return true;
#else
        (void)pid;
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0 || fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd_, F_SETFL, O_NONBLOCK) < 0)
            return fail("could not create AI socket");
#ifdef SO_NOSIGPIPE
        int no_sigpipe = 1;
        if (setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) < 0)
            return fail("could not set AI socket mode");
#endif
        sockaddr_un address{}; address.sun_family = AF_UNIX;
        if (path.size() >= sizeof(address.sun_path)) return fail("AI socket path too long");
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) return true;
        if (errno != EINPROGRESS && errno != EAGAIN) return fail("could not connect AI socket");
        if (!wait_ready(POLLOUT, deadline)) return false;
        int error = 0; socklen_t length = sizeof(error);
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) return fail("could not connect AI socket");
        return true;
#endif
    }
#ifndef _WIN32
    bool wait_ready(short events, deadline_type deadline) {
        while (clock::now() < deadline) {
            pollfd state{fd_, events, 0};
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            const int result = poll(&state, 1, static_cast<int>(std::max<long long>(1, remaining)));
            if (result > 0) {
                if (state.revents & events) return true;
                error_ = "AI socket disconnected"; return false;
            }
            if (result < 0 && errno != EINTR) { error_ = "AI socket wait failed"; return false; }
        }
        error_ = "AI IPC deadline exceeded"; return false;
    }
#endif
    bool write_all(const std::string& data, deadline_type deadline) {
        std::size_t offset = 0;
        while (offset < data.size() && clock::now() < deadline) {
#ifdef _WIN32
            DWORD count = 0;
            if (!WriteFile(pipe_, data.data() + offset, static_cast<DWORD>(data.size() - offset), &count, nullptr)) {
                error_ = "AI pipe write failed"; return false;
            }
            offset += count;
            if (!count && !wait_retry(deadline)) return false;
#else
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const auto count = send(fd_, data.data() + offset, data.size() - offset, flags);
            if (count > 0) offset += static_cast<std::size_t>(count);
            else if (count < 0 && errno == EINTR) continue;
            else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (!wait_ready(POLLOUT, deadline)) return false;
            } else { error_ = "AI socket write failed"; return false; }
#endif
        }
        if (offset == data.size()) return true;
        error_ = "AI IPC deadline exceeded"; return false;
    }
    bool read_line(std::string& line, deadline_type deadline) {
        while (clock::now() < deadline) {
            const auto end = incoming_.find('\n');
            if (end != std::string::npos) {
                if (end > frame_limit) { error_ = "AI IPC frame exceeds limit"; return false; }
                line = incoming_.substr(0, end); incoming_.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            if (incoming_.size() > frame_limit) { error_ = "AI IPC frame exceeds limit"; return false; }
            char buffer[4096];
#ifdef _WIN32
            DWORD count = 0;
            if (!ReadFile(pipe_, buffer, sizeof(buffer), &count, nullptr)) {
                if (GetLastError() == ERROR_NO_DATA) { if (!wait_retry(deadline)) return false; continue; }
                error_ = "AI pipe read failed"; return false;
            }
            if (!count) { error_ = "AI pipe disconnected"; return false; }
#else
            const auto count = recv(fd_, buffer, sizeof(buffer), 0);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (!wait_ready(POLLIN, deadline)) return false;
                continue;
            }
            if (count <= 0) { error_ = "AI socket disconnected"; return false; }
#endif
            incoming_.append(buffer, static_cast<std::size_t>(count));
        }
        error_ = "AI IPC deadline exceeded"; return false;
    }
};
} // namespace ny::ipc

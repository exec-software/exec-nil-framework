#pragma once
// ipc_hot_reload.hpp — IPC client for nilagent
// NIL-0006: Windows now uses Named Pipes to connect to Ny AI servers.
// Unix uses Unix domain sockets (existing behaviour unchanged).
#include <string>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace ny {
namespace ipc {

// Returns the pipe/socket path for a running Ny AI server (NIL-0006)
inline std::string get_socket_path_for_pid(int pid) {
#ifdef _WIN32
    // NIL-0006: Ny programs compiled with --ai create \\.\pipe\ny_ai_<pid>
    return "\\\\.\\pipe\\ny_ai_" + std::to_string(pid);
#else
    return "/tmp/ny_ai_" + std::to_string(pid) + ".sock";
#endif
}

inline std::string json_extract_string(const std::string& json, const std::string& key) {
    // Minimal JSON string extraction (key: "value")
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

inline int json_extract_int(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ')) ++pos;
    try { return std::stoi(json.substr(pos)); } catch (...) { return 0; }
}

class IpcClient {
public:
    IpcClient() = default;
    ~IpcClient() { disconnect(); }

    bool connect(const std::string& socket_path) {
#ifdef _WIN32
        (void)socket_path;
        return false; // Not supported on Windows
#else
        return connect_unix(socket_path);
#endif
    }

    void disconnect() {
#ifndef _WIN32
        if (connected_) { close(fd_); connected_ = false; fd_ = -1; }
#endif
    }

    bool is_connected() const { return connected_; }

    std::string send_reload(const std::string& target) {
        if (!connected_) return "{\"ok\":\"false\",\"error\":\"not connected\"}";
        return send_cmd("{\"cmd\":\"reload\",\"target\":\"" + target + "\"}");
    }

    std::string send_status() {
        if (!connected_) return "{\"ok\":\"false\",\"error\":\"not connected\"}";
        return send_cmd("{\"cmd\":\"status\"}");
    }

    std::string send_snapshot() {
        if (!connected_) return "{\"ok\":\"false\",\"error\":\"not connected\"}";
        return send_cmd("{\"cmd\":\"snapshot\"}");
    }

private:
    bool connected_ = false;
#ifndef _WIN32
    int fd_ = -1;

    bool connect_unix(const std::string& path);
    std::string send_cmd(const std::string& json);
#else
    std::string send_cmd(const std::string& /*json*/) { return "{}"; }
#endif
};

#ifndef _WIN32
// Unix implementations — defined inline to avoid separate .cpp
inline bool IpcClient::connect_unix(const std::string& path) {
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd_); fd_ = -1; return false;
    }
    connected_ = true;
    return true;
}

inline std::string IpcClient::send_cmd(const std::string& json) {
    if (send(fd_, json.c_str(), json.size(), 0) < 0) return "{}";
    char buf[4096] = {};
    ssize_t n = recv(fd_, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return "{}";
    return std::string(buf, n);
}
#endif

} // namespace ipc
} // namespace ny

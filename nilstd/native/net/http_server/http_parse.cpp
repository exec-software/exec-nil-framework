// =============================================================================
// ml_nyhttp — NyHTTP C++ interop layer
//
// Implements all @interop("ml_nyhttp") functions declared in:
//   nyhttp/src/request.ny   — request parsing + field getters
//   nyhttp/src/server.ny    — server lifecycle + accept loop
//   nyhttp/src/response.ny  — response serialisation + send
//   nyhttp/src/router.ny    — route registration + pattern matching
//
// ABI contract (matching the Ny @interop code-generator):
//   - text params arrive as  const char*
//   - text returns must be   const char*  pointing to thread-local stable memory
//   - i32  ↔  int32_t
//   - i64  ↔  int64_t
//   - bool ↔  bool
//
// All handle maps use int64_t keys issued by a global atomic counter.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

// Platform networking
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
  #define MSG_NOSIGNAL 0
  #define close_socket(s) closesocket(s)
  // Winsock RAII initialiser — runs once when DLL is loaded
  struct WinsockInit {
      WinsockInit()  { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
      ~WinsockInit() { WSACleanup(); }
  };
  static WinsockInit g_winsock_init;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <poll.h>
  #include <errno.h>
  #define close_socket(s) close(s)
#endif

// DLL export macro
#ifdef _WIN32
  #define NY_EXPORT __declspec(dllexport)
#else
  #define NY_EXPORT
#endif

// Socket error string helper
#ifdef _WIN32
static std::string socket_error_string() {
    int err = WSAGetLastError();
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, buf, sizeof(buf), NULL);
    // Trim trailing \r\n
    std::string s(buf);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}
#else
static std::string socket_error_string() {
    return socket_error_string();
}
#endif

// ---------------------------------------------------------------------------
// Thread-local string return buffer
// ---------------------------------------------------------------------------
// All functions that return const char* write into this buffer so callers get
// a stable pointer that lives until the next call on the same thread.

static thread_local std::string tl_return_buf;

static const char* tl_str(const std::string& s) {
    tl_return_buf = s;
    return tl_return_buf.c_str();
}
static const char* tl_str(std::string&& s) {
    tl_return_buf = std::move(s);
    return tl_return_buf.c_str();
}

// ---------------------------------------------------------------------------
// Handle counter
// ---------------------------------------------------------------------------

static std::atomic<int64_t> g_next_handle{1};
static int64_t next_handle() { return g_next_handle.fetch_add(1); }

// ---------------------------------------------------------------------------
// RequestData
// ---------------------------------------------------------------------------

struct HeaderPair { std::string name, value; };

struct RequestData {
    int32_t     method_code{0};   // 0=GET … 6=OPTIONS
    std::string path;
    std::string query_string;
    std::string body;
    std::string version;
    std::string remote_addr;
    std::vector<HeaderPair> headers;
    int         client_fd{-1};    // live socket; -1 for standalone-parse handles
};

static std::mutex                                        g_req_mu;
static std::unordered_map<int64_t, RequestData>          g_requests;
static thread_local std::string                          g_parse_error;

// Map method string to code
static int32_t method_code(const std::string& m) {
    if (m == "GET")     return 0;
    if (m == "POST")    return 1;
    if (m == "PUT")     return 2;
    if (m == "DELETE")  return 3;
    if (m == "PATCH")   return 4;
    if (m == "HEAD")    return 5;
    if (m == "OPTIONS") return 6;
    return 0;
}

// Parse raw HTTP/1.1 bytes into a RequestData.  Returns true on success.
static bool parse_http_request(const std::string& raw, RequestData& out) {
    size_t pos = 0;

    // --- Request line ---
    size_t line_end = raw.find("\r\n", pos);
    if (line_end == std::string::npos) { g_parse_error = "missing request line"; return false; }

    std::string request_line = raw.substr(pos, line_end - pos);
    pos = line_end + 2;

    size_t sp1 = request_line.find(' ');
    if (sp1 == std::string::npos) { g_parse_error = "malformed request line"; return false; }
    size_t sp2 = request_line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) { g_parse_error = "malformed request line (version)"; return false; }

    std::string method_str = request_line.substr(0, sp1);
    std::string path_full  = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    out.version = request_line.substr(sp2 + 1);
    out.method_code = method_code(method_str);

    size_t qs_pos = path_full.find('?');
    if (qs_pos != std::string::npos) {
        out.path         = path_full.substr(0, qs_pos);
        out.query_string = path_full.substr(qs_pos + 1);
    } else {
        out.path         = path_full;
        out.query_string = "";
    }

    // --- Headers ---
    while (pos < raw.size()) {
        size_t end = raw.find("\r\n", pos);
        if (end == std::string::npos) break;
        if (end == pos) { pos += 2; break; }  // empty line → end of headers

        std::string header_line = raw.substr(pos, end - pos);
        pos = end + 2;

        size_t colon = header_line.find(':');
        if (colon == std::string::npos) continue;

        std::string hname  = header_line.substr(0, colon);
        std::string hvalue = header_line.substr(colon + 1);

        // Trim leading whitespace from value
        size_t vs = hvalue.find_first_not_of(" \t");
        if (vs != std::string::npos) hvalue = hvalue.substr(vs);
        // Trim trailing whitespace
        size_t ve = hvalue.find_last_not_of(" \t\r\n");
        if (ve != std::string::npos) hvalue = hvalue.substr(0, ve + 1);

        out.headers.push_back({hname, hvalue});
    }

    // --- Body ---
    out.body = raw.substr(pos);
    return true;
}

// ---------------------------------------------------------------------------
// ServerData
// ---------------------------------------------------------------------------

struct ServerData {
    std::string        host;
    int32_t            port{8080};
    int32_t            backlog{128};
    int                server_fd{-1};
    std::atomic<bool>  running{false};
    std::thread        accept_thread;

    std::mutex              queue_mu;
    std::condition_variable queue_cv;
    std::queue<int64_t>     request_queue;  // queued req handles

    std::string last_error;
};

static std::mutex                                        g_srv_mu;
static std::unordered_map<int64_t, ServerData*>          g_servers;
static thread_local std::string                          g_server_error;

// Read a full HTTP/1.1 request from a connected socket (naive, fixed 64 KB limit).
static std::string read_http_request(int fd) {
    std::string buf;
    buf.reserve(4096);
    char tmp[4096];

    // Read until we have headers end (\r\n\r\n) then handle body by Content-Length
    while (true) {
        int n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, n);

        size_t hdr_end = buf.find("\r\n\r\n");
        if (hdr_end == std::string::npos) continue;

        // Check Content-Length
        std::string lower = buf;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        size_t cl_pos = lower.find("content-length:");
        if (cl_pos == std::string::npos) break;  // no body expected

        size_t cl_val_start = cl_pos + 15;
        size_t cl_val_end   = lower.find("\r\n", cl_val_start);
        std::string cl_str  = buf.substr(cl_val_start, cl_val_end - cl_val_start);
        // Trim
        size_t s = cl_str.find_first_not_of(" \t");
        if (s != std::string::npos) cl_str = cl_str.substr(s);

        try {
            size_t content_len = (size_t)std::stoul(cl_str);
            size_t body_start  = hdr_end + 4;
            size_t body_have   = buf.size() - body_start;
            while (body_have < content_len) {
                int n2 = recv(fd, tmp, sizeof(tmp), 0);
                if (n2 <= 0) break;
                buf.append(tmp, n2);
                body_have = buf.size() - body_start;
            }
        } catch (...) {}
        break;
    }
    return buf;
}

// Accept loop — runs on ServerData::accept_thread
static void accept_loop(int64_t server_handle) {
    ServerData* srv = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_srv_mu);
        auto it = g_servers.find(server_handle);
        if (it == g_servers.end()) return;
        srv = it->second;
    }

    while (srv->running.load()) {
#ifdef _WIN32
        WSAPOLLFD pfd{};
#else
        struct pollfd pfd{};
#endif
        pfd.fd     = srv->server_fd;
        pfd.events = POLLIN;
#ifdef _WIN32
        int ret = WSAPoll(&pfd, 1, 100);  // 100 ms poll cycle
#else
        int ret = poll(&pfd, 1, 100);  // 100 ms poll cycle
#endif
        if (ret <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = (int)accept(srv->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;  // INVALID_SOCKET truncates to -1

        // Get remote address string
        char remote_ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &client_addr.sin_addr, remote_ip, sizeof(remote_ip));
        std::string remote_addr = std::string(remote_ip) + ":" +
                                  std::to_string(ntohs(client_addr.sin_port));

        // Read the HTTP request
        std::string raw = read_http_request(client_fd);
        if (raw.empty()) { close_socket(client_fd); continue; }

        // Parse it
        RequestData rd;
        rd.remote_addr = remote_addr;
        rd.client_fd   = client_fd;
        if (!parse_http_request(raw, rd)) {
            // Send a 400 directly and close
            const char* bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\nConnection: close\r\n\r\nBad Request";
            send(client_fd, bad, (int)strlen(bad), MSG_NOSIGNAL);
            close_socket(client_fd);
            continue;
        }

        // Store and enqueue
        int64_t req_handle = next_handle();
        {
            std::lock_guard<std::mutex> req_lk(g_req_mu);
            g_requests.emplace(req_handle, std::move(rd));
        }
        {
            std::lock_guard<std::mutex> q_lk(srv->queue_mu);
            srv->request_queue.push(req_handle);
        }
        srv->queue_cv.notify_one();
    }
}

// ---------------------------------------------------------------------------
// RouterData
// ---------------------------------------------------------------------------

struct RoutePattern {
    int32_t     method_code;
    int32_t     route_index;   // index in Ny routes vec
    std::string pattern;       // e.g. "/users/:id/posts"
};

struct RouteMatchData {
    int32_t                              route_index{-1};
    std::vector<std::pair<std::string, std::string>> params;
};

static std::mutex                                        g_rtr_mu;
static std::unordered_map<int64_t, std::vector<RoutePattern>*> g_routers;

static std::mutex                                        g_match_mu;
static std::unordered_map<int64_t, RouteMatchData>       g_matches;

// Split string by delimiter
static std::vector<std::string> split_str(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim))
        parts.push_back(token);
    return parts;
}

// Match a pattern against a path.  Fills params on success.  Returns true if matched.
static bool match_pattern(const std::string& pattern, const std::string& path,
                           std::vector<std::pair<std::string, std::string>>& params) {
    auto pat_segs  = split_str(pattern, '/');
    auto path_segs = split_str(path,    '/');

    // Remove empty leading segment from absolute paths
    if (!pat_segs.empty()  && pat_segs[0].empty())  pat_segs.erase(pat_segs.begin());
    if (!path_segs.empty() && path_segs[0].empty()) path_segs.erase(path_segs.begin());

    size_t pi = 0, pa = 0;
    while (pi < pat_segs.size() && pa < path_segs.size()) {
        const std::string& ps = pat_segs[pi];
        if (ps == "*") {
            // Wildcard: matches rest of path
            return true;
        } else if (!ps.empty() && ps[0] == ':') {
            // Named param
            params.push_back({ps.substr(1), path_segs[pa]});
        } else {
            if (ps != path_segs[pa]) return false;
        }
        ++pi; ++pa;
    }

    // Both must be fully consumed (unless pattern ended with *)
    return pi == pat_segs.size() && pa == path_segs.size();
}

// ---------------------------------------------------------------------------
// HTTP status phrase lookup
// ---------------------------------------------------------------------------

static const char* status_phrase(int32_t code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

// ===========================================================================
// REQUEST INTEROP
// ===========================================================================

extern "C" NY_EXPORT int64_t ml_http_parse_request(const char* raw, const char* remote_addr) {
    RequestData rd;
    rd.remote_addr = remote_addr ? remote_addr : "";
    rd.client_fd   = -1;  // standalone parse — no live socket

    if (!parse_http_request(raw ? raw : "", rd)) {
        return -1;
    }

    int64_t handle = next_handle();
    std::lock_guard<std::mutex> lk(g_req_mu);
    g_requests.emplace(handle, std::move(rd));
    return handle;
}

extern "C" NY_EXPORT int32_t ml_http_request_method(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return (it != g_requests.end()) ? it->second.method_code : 0;
}

extern "C" NY_EXPORT const char* ml_http_request_path(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return tl_str((it != g_requests.end()) ? it->second.path : "");
}

extern "C" NY_EXPORT const char* ml_http_request_query_string(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return tl_str((it != g_requests.end()) ? it->second.query_string : "");
}

extern "C" NY_EXPORT const char* ml_http_request_body(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return tl_str((it != g_requests.end()) ? it->second.body : "");
}

extern "C" NY_EXPORT int32_t ml_http_request_header_count(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return (it != g_requests.end()) ? (int32_t)it->second.headers.size() : 0;
}

extern "C" NY_EXPORT const char* ml_http_request_header_name(int64_t handle, int32_t index) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    if (it == g_requests.end()) return tl_str("");
    auto& hdrs = it->second.headers;
    if (index < 0 || (size_t)index >= hdrs.size()) return tl_str("");
    return tl_str(hdrs[index].name);
}

extern "C" NY_EXPORT const char* ml_http_request_header_value(int64_t handle, int32_t index) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    if (it == g_requests.end()) return tl_str("");
    auto& hdrs = it->second.headers;
    if (index < 0 || (size_t)index >= hdrs.size()) return tl_str("");
    return tl_str(hdrs[index].value);
}

extern "C" NY_EXPORT const char* ml_http_request_remote_addr(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return tl_str((it != g_requests.end()) ? it->second.remote_addr : "");
}

extern "C" NY_EXPORT const char* ml_http_request_version(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    return tl_str((it != g_requests.end()) ? it->second.version : "HTTP/1.1");
}

extern "C" NY_EXPORT const char* ml_http_parse_error() {
    return tl_str(g_parse_error);
}

extern "C" NY_EXPORT void ml_http_request_free(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_req_mu);
    auto it = g_requests.find(handle);
    if (it == g_requests.end()) return;
    if (it->second.client_fd >= 0) close_socket(it->second.client_fd);
    g_requests.erase(it);
}

extern "C" NY_EXPORT const char* ml_url_decode(const char* encoded) {
    if (!encoded) return tl_str("");
    std::string result;
    const char* p = encoded;
    while (*p) {
        if (*p == '%' && *(p+1) && *(p+2)) {
            char hex[3] = { *(p+1), *(p+2), '\0' };
            result += (char)strtol(hex, nullptr, 16);
            p += 3;
        } else if (*p == '+') {
            result += ' ';
            ++p;
        } else {
            result += *p++;
        }
    }
    return tl_str(result);
}

// ===========================================================================
// SERVER INTEROP
// ===========================================================================

extern "C" NY_EXPORT int64_t ml_http_server_create(const char* host, int64_t port, int64_t backlog) {
    auto* srv = new ServerData();

    srv->host    = host ? host : "0.0.0.0";
    srv->port    = (int32_t)port;
    srv->backlog = backlog > 0 ? (int32_t)backlog : 128;

    // Create and bind the listening socket
    srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->server_fd < 0) {
        g_server_error = "socket() failed: " + socket_error_string();
        delete srv;
        return -1;
    }

#ifdef _WIN32
    const char opt = 1;
#else
    int opt = 1;
#endif
    setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    if (inet_pton(AF_INET, srv->host.c_str(), &addr.sin_addr) <= 0)
        addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        g_server_error = "bind() failed on port " + std::to_string(port) + ": " +
                         socket_error_string();
        close_socket(srv->server_fd);
        delete srv;
        return -1;
    }

    if (listen(srv->server_fd, srv->backlog) < 0) {
        g_server_error = "listen() failed: " + socket_error_string();
        close_socket(srv->server_fd);
        delete srv;
        return -1;
    }

    int64_t handle = next_handle();
    std::lock_guard<std::mutex> lk(g_srv_mu);
    g_servers.emplace(handle, srv);
    return handle;
}

extern "C" NY_EXPORT int64_t ml_http_server_start(int64_t handle) {
    ServerData* srv = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_srv_mu);
        auto it = g_servers.find(handle);
        if (it == g_servers.end()) return 0;
        srv = it->second;
    }

    srv->running.store(true);
    srv->accept_thread = std::thread(accept_loop, handle);
    return 1;
}

extern "C" NY_EXPORT int64_t ml_http_next_request(int64_t handle, int32_t timeout_ms) {
    // Get a raw pointer to ServerData without holding g_srv_mu during the wait
    ServerData* srv = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_srv_mu);
        auto it = g_servers.find(handle);
        if (it == g_servers.end()) return -1;
        srv = it->second;
    }

    std::unique_lock<std::mutex> q_lk(srv->queue_mu);
    bool got = srv->queue_cv.wait_for(
        q_lk,
        std::chrono::milliseconds(timeout_ms),
        [srv]{ return !srv->request_queue.empty() || !srv->running.load(); }
    );

    if (!got || srv->request_queue.empty()) return -1;
    int64_t req_handle = srv->request_queue.front();
    srv->request_queue.pop();
    return req_handle;
}

extern "C" NY_EXPORT void ml_http_server_stop(int64_t handle) {
    std::lock_guard<std::mutex> lk(g_srv_mu);
    auto it = g_servers.find(handle);
    if (it == g_servers.end()) return;
    it->second->running.store(false);
    it->second->queue_cv.notify_all();
}

extern "C" NY_EXPORT void ml_http_server_free(int64_t handle) {
    ServerData* srv = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_srv_mu);
        auto it = g_servers.find(handle);
        if (it == g_servers.end()) return;
        srv = it->second;
        g_servers.erase(it);
    }

    srv->running.store(false);
    srv->queue_cv.notify_all();
    if (srv->accept_thread.joinable()) srv->accept_thread.join();
    if (srv->server_fd >= 0) close_socket(srv->server_fd);
    delete srv;
}

extern "C" NY_EXPORT const char* ml_http_server_error() {
    return tl_str(g_server_error);
}

// ===========================================================================
// RESPONSE INTEROP
// ===========================================================================

extern "C" NY_EXPORT void ml_http_send_response(int64_t req_handle,
                                       int32_t status_code,
                                       const char* headers_wire,
                                       const char* body) {
    int client_fd = -1;
    {
        std::lock_guard<std::mutex> lk(g_req_mu);
        auto it = g_requests.find(req_handle);
        if (it == g_requests.end()) return;
        client_fd = it->second.client_fd;
        g_requests.erase(it);
    }

    if (client_fd < 0) return;  // standalone-parse handle; nothing to send

    std::string body_str   = body         ? body         : "";
    std::string hdrs_str   = headers_wire ? headers_wire : "";

    std::string response =
        "HTTP/1.1 " + std::to_string(status_code) + " " + status_phrase(status_code) + "\r\n" +
        hdrs_str +
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n" +
        "Connection: close\r\n"
        "\r\n" +
        body_str;

    const char* p  = response.c_str();
    size_t       remaining = response.size();
    while (remaining > 0) {
        int written = send(client_fd, p, (int)remaining, MSG_NOSIGNAL);
        if (written <= 0) break;
        p         += written;
        remaining -= (size_t)written;
    }

    close_socket(client_fd);
}

// ===========================================================================
// ROUTER INTEROP
// ===========================================================================

extern "C" NY_EXPORT int64_t ml_router_create() {
    auto* routes = new std::vector<RoutePattern>();
    int64_t handle = next_handle();
    std::lock_guard<std::mutex> lk(g_rtr_mu);
    g_routers.emplace(handle, routes);
    return handle;
}

extern "C" NY_EXPORT int32_t ml_router_add(int64_t router_handle, int32_t method_code, const char* pattern) {
    std::lock_guard<std::mutex> lk(g_rtr_mu);
    auto it = g_routers.find(router_handle);
    if (it == g_routers.end()) return -1;

    auto* routes = it->second;
    int32_t index = (int32_t)routes->size();
    routes->push_back({ method_code, index, pattern ? pattern : "" });
    return index;
}

extern "C" NY_EXPORT int64_t ml_router_match(int64_t router_handle, int32_t method_code, const char* path) {
    std::vector<RoutePattern>* routes = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_rtr_mu);
        auto it = g_routers.find(router_handle);
        if (it == g_routers.end()) return -1;
        routes = it->second;
    }

    std::string path_str = path ? path : "/";
    // Strip query string if present (should already be stripped by parser, but be safe)
    size_t qs = path_str.find('?');
    if (qs != std::string::npos) path_str = path_str.substr(0, qs);

    for (const auto& rp : *routes) {
        if (rp.method_code != method_code) continue;

        RouteMatchData md;
        if (match_pattern(rp.pattern, path_str, md.params)) {
            md.route_index = rp.route_index;
            int64_t mh = next_handle();
            std::lock_guard<std::mutex> lk(g_match_mu);
            g_matches.emplace(mh, std::move(md));
            return mh;
        }
    }
    return -1;
}

extern "C" NY_EXPORT int32_t ml_router_match_route_index(int64_t match_handle) {
    std::lock_guard<std::mutex> lk(g_match_mu);
    auto it = g_matches.find(match_handle);
    return (it != g_matches.end()) ? it->second.route_index : -1;
}

extern "C" NY_EXPORT int32_t ml_router_match_param_count(int64_t match_handle) {
    std::lock_guard<std::mutex> lk(g_match_mu);
    auto it = g_matches.find(match_handle);
    return (it != g_matches.end()) ? (int32_t)it->second.params.size() : 0;
}

extern "C" NY_EXPORT const char* ml_router_match_param_name(int64_t match_handle, int32_t index) {
    std::lock_guard<std::mutex> lk(g_match_mu);
    auto it = g_matches.find(match_handle);
    if (it == g_matches.end()) return tl_str("");
    auto& p = it->second.params;
    if (index < 0 || (size_t)index >= p.size()) return tl_str("");
    return tl_str(p[index].first);
}

extern "C" NY_EXPORT const char* ml_router_match_param_value(int64_t match_handle, int32_t index) {
    std::lock_guard<std::mutex> lk(g_match_mu);
    auto it = g_matches.find(match_handle);
    if (it == g_matches.end()) return tl_str("");
    auto& p = it->second.params;
    if (index < 0 || (size_t)index >= p.size()) return tl_str("");
    return tl_str(p[index].second);
}

extern "C" NY_EXPORT void ml_router_match_free(int64_t match_handle) {
    std::lock_guard<std::mutex> lk(g_match_mu);
    g_matches.erase(match_handle);
}

extern "C" NY_EXPORT void ml_router_free(int64_t router_handle) {
    std::lock_guard<std::mutex> lk(g_rtr_mu);
    auto it = g_routers.find(router_handle);
    if (it == g_routers.end()) return;
    delete it->second;
    g_routers.erase(it);
}

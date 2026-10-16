// =============================================================================
// ml_nyweb — Static File Serving
//
// Implements static file serving with MIME type detection, Cache-Control headers,
// and ETag support.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <memory>

// DLL export macro
#ifdef _WIN32
  #define NY_EXPORT __declspec(dllexport)
#else
  #define NY_EXPORT
#endif

// ---------------------------------------------------------------------------
// Thread-local string return buffer
// ---------------------------------------------------------------------------

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
// MIME type mapping
// ---------------------------------------------------------------------------

static std::unordered_map<std::string, std::string> mime_types = {
    {"html", "text/html; charset=utf-8"},
    {"htm",  "text/html; charset=utf-8"},
    {"css",  "text/css"},
    {"js",   "application/javascript"},
    {"json", "application/json"},
    {"png",  "image/png"},
    {"jpg",  "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"gif",  "image/gif"},
    {"svg",  "image/svg+xml"},
    {"ico",  "image/x-icon"},
    {"woff", "font/woff"},
    {"woff2", "font/woff2"},
    {"ttf",  "font/ttf"},
    {"pdf",  "application/pdf"},
    {"zip",  "application/zip"},
    {"txt",  "text/plain"},
    {"md",   "text/markdown"},
    {"xml",  "application/xml"},
    {"swf",  "application/x-shockwave-flash"}
};

// ---------------------------------------------------------------------------
// Get MIME type from file extension
// ---------------------------------------------------------------------------

static std::string get_mime_type(const std::string& file_path) {
    // Find last dot
    size_t last_dot = file_path.find_last_of('.');
    if (last_dot == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = file_path.substr(last_dot + 1);

    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto it = mime_types.find(ext);
    if (it != mime_types.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

// ---------------------------------------------------------------------------
// Generate ETag for file
// ---------------------------------------------------------------------------

static std::string generate_etag(const std::string& file_path, int64_t file_size) {
    // Simple ETag: "sha1(file_path + size)" - for now use size + timestamp
    std::ostringstream etag;
    etag << "\"" << file_size << "-" << std::to_string(std::time(nullptr)) << "\"";
    return etag.str();
}

// ---------------------------------------------------------------------------
// Read file content
// ---------------------------------------------------------------------------

static std::string read_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file: " + file_path);
    }

    return std::string(buffer.begin(), buffer.end());
}

// ---------------------------------------------------------------------------
// Static file serving
// ---------------------------------------------------------------------------

extern "C" {

// Serve a static file
// Returns: option<http_server_response>
//   - On success: some(response) with proper headers
//   - On error: none (pass to next handler)
NY_EXPORT int64_t ml_serve_static_file(const char* file_path, const char* opts_json) {
    try {
        std::string path_str(file_path);

        // Parse options from JSON (simplified)
        // For now, just use default options
        int64_t cache_max_age = 86400;  // 1 day default
        bool allow_directory_listing = false;

        // Read file
        std::string content = read_file(path_str);

        // Get MIME type
        std::string mime_type = get_mime_type(path_str);

        // Generate ETag
        int64_t file_size = content.length();
        std::string etag = generate_etag(path_str, file_size);

        // Build response headers
        std::ostringstream headers;
        headers << "HTTP/1.1 200 OK\r\n";
        headers << "Content-Type: " << mime_type << "\r\n";
        headers << "Content-Length: " << file_size << "\r\n";
        headers << "ETag: " << etag << "\r\n";
        headers << "Cache-Control: max-age=" << cache_max_age << "\r\n";
        headers << "Accept-Ranges: bytes\r\n";
        headers << "Connection: keep-alive\r\n";
        headers << "\r\n";

        // Combine headers and content
        std::string response = headers.str() + content;

        // Store in thread-local buffer
        tl_return_buf = response;
        return 0;  // Success
    } catch (const std::exception& e) {
        // Return error code - caller will pass to next handler
        return 1;  // Error
    }
}

}  // extern "C"

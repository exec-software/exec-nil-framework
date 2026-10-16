#include "vm.hpp"
#include "nil_version.hpp"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include "nil_loader.hpp"

// NIL-0006: AI Module — Embedded Agent Command & Control Server
// When launched with --ai, the interpreter starts a named pipe server before
// executing main(). Agents connect and send JSON-RPC commands to inspect
// and control the running program.

// ── WebView injection (dynamic) ───────────────────────────────────────────────
// webview_inject_message_impl is exported by ml_webview.dll (loaded at runtime
// by Ny programs that use webview). We resolve it lazily via GetProcAddress so
// nilinterpreter has no hard link dependency on ml_webview.dll — programs that
// don't use webview simply get a graceful no-op for vm.send_msg.
typedef void (*webview_inject_fn)(int handle, const char* msg);

static webview_inject_fn resolve_webview_inject() {
#ifdef _WIN32
    // The Ny FFI hot-reload system may load the DLL as "ml_webview.hotN.dll"
    // rather than "ml_webview.dll". Scan all loaded modules for the export.
    HMODULE mods[256] = {};
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        char name[MAX_PATH];
        for (DWORD i = 0; i < count && i < 256; ++i) {
            if (!mods[i]) continue;
            if (GetModuleBaseNameA(GetCurrentProcess(), mods[i], name, MAX_PATH)) {
                // Match any variant: ml_webview.dll, ml_webview.hot0.dll, etc.
                if (_strnicmp(name, "ml_webview", 10) == 0) {
                    webview_inject_fn fn = (webview_inject_fn)
                        GetProcAddress(mods[i], "webview_inject_message_impl");
                    if (fn) return fn;
                }
            }
        }
    }
    return nullptr;
#else
    return nullptr; // Phase 2 Unix
#endif
}

// ── AI Server State ───────────────────────────────────────────────────────────

namespace ny_ai {

static std::atomic<bool>  g_running{false};
static ny::VM*            g_vm     = nullptr;

// ── Minimal JSON helpers ──────────────────────────────────────────────────────

static std::string json_extract(const std::string& json, const std::string& key) {
    // Extract "key":"value"
    std::string q = "\"" + key + "\":\"";
    auto pos = json.find(q);
    if (pos == std::string::npos) return "";
    pos += q.size();
    std::string out;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') break;
        if (c == '\\' && pos + 1 < json.size()) {
            char nc = json[++pos];
            if (nc == '"')  { out += '"';  }
            else if (nc == 'n')  { out += '\n'; }
            else if (nc == 'r')  { out += '\r'; }
            else if (nc == 't')  { out += '\t'; }
            else if (nc == '\\') { out += '\\'; }
            else                 { out += nc;   }
        } else {
            out += c;
        }
        ++pos;
    }
    return out;
}

static std::string json_response(const std::string& id,
                                 const std::string& result,
                                 const std::string& error = "") {
    std::ostringstream s;
    s << "{\"id\":\"" << id << "\","
      << "\"result\":" << result << ","
      << "\"error\":" << (error.empty() ? "null" : ("\"" + error + "\""))
      << "}";
    return s.str();
}

// ── Command dispatch ──────────────────────────────────────────────────────────

static std::string handle_command(const std::string& line) {
    std::string id     = json_extract(line, "id");
    std::string method = json_extract(line, "method");

    if (method == "vm.status") {
        uint32_t pid = 0;
#ifdef _WIN32
        pid = (uint32_t)GetCurrentProcessId();
#else
        pid = (uint32_t)getpid();
#endif
        std::ostringstream r;
        r << "{\"running\":true,\"ai_version\":1"
          << ",\"pid\":" << pid
          << ",\"nil_version\":\"" << NIL_VERSION << "\"}";
        return json_response(id, r.str());
    }

    if (method == "vm.pause") {
        if (g_vm) {
            g_vm->setDebugState(ny::VM::DebugState::PAUSED);
            return json_response(id, "{\"ok\":true}");
        }
        return json_response(id, "null", "vm not ready");
    }

    if (method == "vm.resume") {
        if (g_vm) {
            g_vm->setDebugState(ny::VM::DebugState::RUNNING);
            return json_response(id, "{\"ok\":true}");
        }
        return json_response(id, "null", "vm not ready");
    }

    // vm.send_msg — inject a JSON message into the WebView message queue.
    // Works transparently with Nyrd: poll_message returns AI messages alongside
    // WebView2 messages so handle_message processes them normally.
    if (method == "vm.send_msg") {
        std::string msg = json_extract(line, "json");
        if (msg.empty()) {
            return json_response(id, "null", "missing json param");
        }
        // Inject into webview handle 1 (Nyrd always uses the first handle).
        // NIL-0006: for multi-window programs, a "handle" param can be added.
        webview_inject_fn inject = resolve_webview_inject();
        if (!inject) {
            return json_response(id, "null", "webview not loaded");
        }
        try {
            inject(1, msg.c_str());
            return json_response(id, "{\"ok\":true}");
        } catch (...) {
            return json_response(id, "null", "inject failed");
        }
    }

    // vm.stack / vm.locals / vm.globals / vm.eval — Phase 2 introspection
    if (method == "vm.stack" || method == "vm.locals" ||
        method == "vm.globals" || method == "vm.eval") {
        return json_response(id,
            "{\"note\":\"Interpreter introspection is planned for Phase 2 (NIL-0006)\","
            "\"frames\":[]}");
    }

    return json_response(id, "null", "unknown method: " + method);
}

// ── Windows Named Pipe server ─────────────────────────────────────────────────

#ifdef _WIN32
static void server_thread(const std::string& pipe_name) {
    g_running = true;
    std::cerr << "[ny_ai] Server on " << pipe_name << std::endl;

    while (g_running) {
        HANDLE pipe = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            8192, 8192,
            5000,     // 5s default timeout
            nullptr
        );
        if (pipe == INVALID_HANDLE_VALUE) {
            std::cerr << "[ny_ai] CreateNamedPipe failed: " << GetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        // Handshake
        DWORD pid = GetCurrentProcessId();
        std::ostringstream hs;
        hs << "{\"event\":\"connected\",\"pid\":" << pid
           << ",\"ai_version\":1,\"program\":\"ny\"}\n";
        std::string hs_str = hs.str();
        DWORD written = 0;
        WriteFile(pipe, hs_str.c_str(), (DWORD)hs_str.size(), &written, nullptr);

        // Read-dispatch loop
        std::string line_buf;
        char buf[4096];
        DWORD nread = 0;
        while (g_running) {
            BOOL ok = ReadFile(pipe, buf, sizeof(buf) - 1, &nread, nullptr);
            if (!ok || nread == 0) break;
            buf[nread] = '\0';
            line_buf += std::string(buf, nread);

            // Process complete lines
            size_t nl;
            while ((nl = line_buf.find('\n')) != std::string::npos) {
                std::string line = line_buf.substr(0, nl);
                line_buf = line_buf.substr(nl + 1);
                // Strip \r
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) {
                    std::string resp = handle_command(line) + "\n";
                    WriteFile(pipe, resp.c_str(), (DWORD)resp.size(), &written, nullptr);
                }
            }
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    std::cerr << "[ny_ai] Server stopped" << std::endl;
}
#else
// NIL-0006 Phase 1: Unix socket server (future — mirrors Windows implementation)
static void server_thread(const std::string& /*socket_path*/) {
    g_running = true;
    std::cerr << "[ny_ai] Unix AI server not yet implemented (NIL-0006 Phase 1)" << std::endl;
    g_running = false;
}
#endif

} // namespace ny_ai

static bool has_nil_binary_magic(const char magic[4]) {
  return std::memcmp(magic, "NIL\0", 4) == 0 ||
         std::memcmp(magic, "MIL\0", 4) == 0 ||
         std::memcmp(magic, "\0LIM", 4) == 0;
}

static std::optional<std::filesystem::path>
existing_executable_path(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::is_regular_file(status))
    return std::nullopt;
#ifndef _WIN32
  constexpr auto executable_bits = std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec;
  if ((status.permissions() & executable_bits) == std::filesystem::perms::none)
    return std::nullopt;
#endif

  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return canonical;

  error.clear();
  auto absolute = std::filesystem::absolute(path, error);
  if (!error)
    return absolute;
  return path;
}

static std::optional<std::filesystem::path>
find_executable_on_path(const std::filesystem::path &name) {
  const char *search_path = std::getenv("PATH");
  if (!search_path || name.empty() || name.has_parent_path())
    return std::nullopt;

#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::istringstream entries(search_path);
  std::string entry;
  while (std::getline(entries, entry, separator)) {
    const auto directory = entry.empty() ? std::filesystem::path(".")
                                         : std::filesystem::path(entry);
    auto candidate = directory / name;
    if (auto resolved = existing_executable_path(candidate))
      return resolved;
#ifdef _WIN32
    if (!candidate.has_extension()) {
      candidate += ".exe";
      if (auto resolved = existing_executable_path(candidate))
        return resolved;
    }
#endif
  }
  return std::nullopt;
}

static std::optional<std::filesystem::path>
resolve_executable_path(const char *invocation) {
#if defined(_WIN32)
  std::vector<char> path_buffer(1024);
  while (path_buffer.size() <= 32768) {
    const DWORD length = GetModuleFileNameA(
        nullptr, path_buffer.data(), static_cast<DWORD>(path_buffer.size()));
    if (length == 0)
      break;
    if (length < path_buffer.size()) {
      if (auto resolved = existing_executable_path(
              std::filesystem::path(path_buffer.data(),
                                    path_buffer.data() + length)))
        return resolved;
      break;
    }
    path_buffer.resize(path_buffer.size() * 2);
  }
#elif defined(__linux__)
  std::error_code proc_error;
  auto proc_path = std::filesystem::read_symlink("/proc/self/exe", proc_error);
  if (!proc_error && !proc_path.empty())
    return existing_executable_path(proc_path);
#elif defined(__APPLE__)
  uint32_t path_size = 0;
  if (_NSGetExecutablePath(nullptr, &path_size) == -1 && path_size > 0) {
    std::vector<char> path_buffer(path_size);
    if (_NSGetExecutablePath(path_buffer.data(), &path_size) == 0) {
      if (auto resolved = existing_executable_path(path_buffer.data()))
        return resolved;
    }
  }
#endif

  const std::filesystem::path invoked(invocation ? invocation : "");
  if (invoked.has_parent_path()) {
    if (auto resolved = existing_executable_path(invoked))
      return resolved;
    return std::nullopt;
  }
  return find_executable_on_path(invoked);
}

enum class embedded_bundle_status { not_present, loaded, invalid };

static constexpr uint64_t max_embedded_nil_size =
    256ULL * 1024ULL * 1024ULL;

static embedded_bundle_status
load_embedded_bundle(const std::filesystem::path &executable_path,
                     std::vector<char> &bytecode, std::string &error) {
  std::ifstream executable(executable_path, std::ios::binary | std::ios::ate);
  if (!executable)
    return embedded_bundle_status::not_present;

  const std::streamoff file_size = executable.tellg();
  if (file_size < 16)
    return embedded_bundle_status::not_present;

  std::array<unsigned char, 16> trailer{};
  executable.seekg(-static_cast<std::streamoff>(trailer.size()), std::ios::end);
  if (!executable.read(reinterpret_cast<char *>(trailer.data()),
                       static_cast<std::streamsize>(trailer.size())))
    return embedded_bundle_status::not_present;

  const char *magic = reinterpret_cast<const char *>(trailer.data() + 8);
  if (std::memcmp(magic, "NYBUNDLE", 8) != 0)
    return embedded_bundle_status::not_present;

  uint64_t bytecode_size = 0;
  for (unsigned index = 0; index < 8; ++index)
    bytecode_size |= static_cast<uint64_t>(trailer[index]) << (index * 8);

  const uint64_t available_payload = static_cast<uint64_t>(file_size - 16);
  if (bytecode_size == 0 || bytecode_size > available_payload ||
      bytecode_size > max_embedded_nil_size ||
      bytecode_size > static_cast<uint64_t>(bytecode.max_size()) ||
      bytecode_size > static_cast<uint64_t>(
                          std::numeric_limits<std::streamsize>::max())) {
    error = "bundle trailer contains an invalid bytecode size";
    return embedded_bundle_status::invalid;
  }

  const auto payload_offset =
      file_size - 16 - static_cast<std::streamoff>(bytecode_size);
  try {
    bytecode.resize(static_cast<std::size_t>(bytecode_size));
  } catch (const std::bad_alloc &) {
    error = "not enough memory for the embedded bytecode payload";
    return embedded_bundle_status::invalid;
  }
  executable.seekg(payload_offset, std::ios::beg);
  if (!executable.read(bytecode.data(),
                       static_cast<std::streamsize>(bytecode_size))) {
    bytecode.clear();
    error = "bundle bytecode payload is truncated or unreadable";
    return embedded_bundle_status::invalid;
  }
  return embedded_bundle_status::loaded;
}

static void print_usage(std::ostream &out) {
  out << "Usage: nilinterpreter [options] <input.nil> [--] [args...]\n"
      << "\nOptions:\n"
      << "  --memory-mode=safe|unsafe  Select managed or manual memory mode\n"
      << "  --ai                       Enable the experimental AI control server\n"
      << "  --no-ai                    Disable the AI control server\n"
      << "  --version                  Show the nilinterpreter version\n"
      << "  -h, --help                 Show this help\n";
}

int main(int argc, char *argv[]) {
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "nilinterpreter " << NIL_VERSION << '\n';
    return 0;
  }
  // Parse CLI arguments
  std::string input_file;
  ny::MemoryMode memory_mode = ny::MemoryMode::UNSAFE_MANUAL;  // Default to unsafe (manual)
  bool ai_mode = false;   // NIL-0006: --ai flag enables the embedded AI server

  std::vector<std::string> vm_args;
  bool collect_vm_args = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (collect_vm_args) {
      vm_args.push_back(arg);
      continue;
    }
    if (arg == "--") {
      collect_vm_args = true;
      continue;
    }
    if (!input_file.empty()) {
      vm_args.push_back(arg);
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }

    if (arg == "--ai") {
      ai_mode = true;
    } else if (arg == "--no-ai") {
      ai_mode = false;
    } else if (arg.find("--memory-mode=") == 0) {
      std::string mode = arg.substr(14);
      if (mode == "safe" || mode == "gc") {
        memory_mode = ny::MemoryMode::SAFE_GC;
      } else if (mode == "unsafe" || mode == "manual") {
        memory_mode = ny::MemoryMode::UNSAFE_MANUAL;
      } else {
        std::cerr << "Unknown memory mode: " << mode << std::endl;
        std::cerr << "Valid options: safe, unsafe, gc, manual" << std::endl;
        return 1;
      }
    } else if (!arg.empty() && arg[0] != '-') {
      input_file = arg;
    } else {
      std::cerr << "Unknown nilinterpreter option: " << arg << std::endl;
      return 1;
    }
  }

  // Check for embedded bytecode (bundled executable).
  std::vector<char> embedded_bytecode;
  bool has_embedded = false;
  std::string bundle_error;
  if (const auto executable_path = resolve_executable_path(argv[0])) {
    const auto bundle_status = load_embedded_bundle(
        *executable_path, embedded_bytecode, bundle_error);
    if (bundle_status == embedded_bundle_status::invalid) {
      std::cerr << "Invalid embedded NIL bundle: " << bundle_error << std::endl;
      return 1;
    }
    has_embedded = bundle_status == embedded_bundle_status::loaded;
  }

  // A bundled executable has no external NIL input. A first positional token
  // therefore belongs to the Ny program even when the caller omits `--`.
  if (has_embedded && !input_file.empty()) {
    vm_args.insert(vm_args.begin(), input_file);
    input_file.clear();
  }

  // If no embedded bytecode and no input file, show usage
  if (!has_embedded && input_file.empty()) {
    print_usage(std::cerr);
    return 1;
  }

  std::unordered_map<std::string, ny::Value> constants;
  std::vector<ny::Instruction> program;

  if (has_embedded) {
    // Parse embedded bytecode
    std::istringstream iss(std::string(embedded_bytecode.begin(), embedded_bytecode.end()));

    // Check magic
    char magic[4] = {};
    const bool is_binary =
        static_cast<bool>(iss.read(magic, 4)) && has_nil_binary_magic(magic);
    iss.clear();
    iss.seekg(0);
    try {
      if (is_binary) {
        program = ny::parseBinaryProgram(iss, constants);
      } else {
        program = ny::parseProgram(iss, constants);
      }
    } catch (const std::exception &e) {
      std::cerr << "Embedded " << (is_binary ? "binary" : "text")
                << " NIL parse error: " << e.what() << std::endl;
      return 1;
    }
  } else {
    // Load from file
    std::ifstream ifs(input_file, std::ios::binary);
    if (!ifs) {
      std::cerr << "Cannot open " << input_file << std::endl;
      return 1;
    }

    // Check magic
    char magic[4];
    if (!ifs.read(magic, 4)) {
      std::cerr << "Failed to read file header" << std::endl;
      return 1;
    }
    ifs.seekg(0);

    const bool is_binary = has_nil_binary_magic(magic);
    try {
      if (is_binary) {
        program = ny::parseBinaryProgram(ifs, constants);
      } else {
        program = ny::parseProgram(ifs, constants);
      }
    } catch (const std::exception &e) {
      std::cerr << (is_binary ? "Binary" : "Text")
                << " NIL parse error: " << e.what() << std::endl;
      return 1;
    }
  }

  ny::VM vm;
  vm.setMemoryMode(memory_mode);
  vm.setArgs(vm_args);
  try {
    vm.loadProgram(program, constants);
  } catch (const std::exception &e) {
    std::cerr << "NIL validation error: " << e.what() << std::endl;
    return 1;
  }
  if (!vm.hasFunction("main")) {
    std::cerr << "nilinterpreter: program has no 'main' entry point" << std::endl;
    return 1;
  }

  // NIL-0006: Start AI server before executing main() so agents can connect
  // and inject commands (e.g. vm.send_msg) from the very beginning.
  std::thread ai_thread;
  if (ai_mode) {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    std::string pipe_name = "\\\\.\\pipe\\ny_ai_" + std::to_string(pid);
    ny_ai::g_vm = &vm;
    ai_thread = std::thread(ny_ai::server_thread, pipe_name);
    std::cerr << "[ny_ai] Listening on " << pipe_name << std::endl;
#else
    std::string sock_path = "/tmp/ny_ai_" + std::to_string(getpid()) + ".sock";
    ny_ai::g_vm = &vm;
    ai_thread = std::thread(ny_ai::server_thread, sock_path);
    std::cerr << "[ny_ai] Listening on " << sock_path << std::endl;
#endif
  }

  int exit_code = 0;
  try {
    vm.execute();
    exit_code = static_cast<int>(vm.getProgramExitCode());
  } catch (const ny::UncaughtFault &e) {
    // Language faults share the same deterministic diagnostic as native entry.
    // Fatal VM safety errors below remain a separate, non-recoverable category.
    std::cerr << e.what() << std::endl;
    exit_code = 1;
  } catch (const std::exception &e) {
    std::cerr << "NIL interpreter runtime error: " << e.what() << std::endl;
    exit_code = 1;
  }

  // Shutdown AI server
  if (ai_mode) {
    ny_ai::g_running = false;
    // Briefly connect to own pipe to unblock ConnectNamedPipe
#ifdef _WIN32
    HANDLE dummy = CreateFileA(
        ("\\\\.\\pipe\\ny_ai_" + std::to_string(GetCurrentProcessId())).c_str(),
        GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE) CloseHandle(dummy);
#endif
    if (ai_thread.joinable()) ai_thread.join();
  }

  return exit_code;
}

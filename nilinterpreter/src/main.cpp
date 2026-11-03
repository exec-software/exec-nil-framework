#include "vm.hpp"
#include "../../common/standard_streams.hpp"
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
// <windows.h> defines min/max as macros, which breaks
// std::numeric_limits<...>::max() and std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <cerrno>
#endif

#include "nil_loader.hpp"

// NIL-0006: AI Module — Embedded Agent Command & Control Server
// When launched with --ai, the interpreter starts a named pipe server before
// executing main(). Agents connect and send JSON Lines commands to inspect
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

#include "ai_control.hpp"
#include "ai_transport.hpp"

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

// ── Automatic crash artifacts ────────────────────────────────────────────────
//
// `nilaix/src/ny_crash.ny` has been able to write .nycrash reports for a long
// time, and nothing ever called it: `write_interpreter` was designed to be
// invoked by a *host* process that captured a dying child's stderr, and no
// such host was ever written. So the machinery existed and never fired, and a
// crashing NIL program left nothing behind.
//
// Writing the report here removes the need for a host at all, and lets the
// report carry things a stderr-scraper could never recover -- the fault type
// and source line as fields rather than prose.
//
// Three rules, because a crash reporter that misbehaves is worse than none:
// it never changes the exit code, it never throws, and it never replaces the
// stderr message a human is already reading.
static void write_crash_report(const std::string &input_file,
                               const std::string &error_type,
                               const std::string &message, int source_line,
                               int exit_code) {
  try {
    const char *enabled = std::getenv("NIL_CRASH_REPORT");
    if (enabled && std::string(enabled) == "0") return;
    const char *crash_dir = std::getenv("NIL_CRASH_DIR");
    const bool asked_for = (enabled && std::string(enabled) != "0") ||
                           (crash_dir && *crash_dir);

    std::filesystem::path directory;
    if (crash_dir && *crash_dir)
      directory = crash_dir;
    else
      directory = std::filesystem::current_path();

    std::string stem = std::filesystem::path(input_file).stem().string();
    if (stem.empty()) stem = "program";
    const auto pid = static_cast<long>(
#ifdef _WIN32
        GetCurrentProcessId()
#else
        ::getpid()
#endif
    );
    const std::filesystem::path target =
        directory / (stem + "-" + std::to_string(pid) + ".nycrash");

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) return;

    // Format `nycrash/1`, matching ny_crash.ny so existing readers (the
    // Enigma `parse_nycrash` tool) keep working. `source` is a new value
    // rather than a reused one: this report came from the interpreter itself,
    // not from scraping its stderr, and a reader should be able to tell.
    out << "# Ny Interpreter Crash Report - written by the interpreter\n";
    out << "# format: NyData v1\n\n";
    out << "[meta]\n";
    out << "format = nycrash/1\n";
    out << "source = interpreter_self\n";
    out << "nil_version = " << NIL_VERSION << "\n";
    out << "pid = " << pid << "\n";
    out << "\n[error]\n";
    out << "type = " << error_type << "\n";
    out << "message = " << message << "\n";
    out << "exit_code = " << exit_code << "\n";
    out << "\n[location]\n";
    out << "program = " << input_file << "\n";
    if (source_line > 0) out << "line = " << source_line << "\n";
    // The hint has to be earned. Advice to enable a mode that is already on,
    // or a claim that no line was available when the message plainly carries
    // one, is exactly the sort of unhelpful error text this work exists to
    // remove -- so check both before offering either.
    const char *strict = std::getenv("NIL_STRICT_BOUNDS");
    const bool strict_on = strict && *strict && std::string(strict) != "0";
    const bool message_has_line = message.find(" at line ") != std::string::npos;

    out << "\n[hint]\n";
    out << "text = ";
    if (source_line > 0 || message_has_line)
      out << "The fault is reported at the line named above. ";
    else
      out << "No source line was available for this error. ";
    out << "Run under nildebugger for a full trace, or nilinterpreter --ai to "
           "attach an agent before the program starts.";
    if (!strict_on)
      out << " If the program reads or writes an array, re-run with "
             "NIL_STRICT_BOUNDS=1: an out-of-bounds access is silent by "
             "default and its first visible symptom is usually somewhere else.";
    out << "\n";
    out.flush();
    if (!out) return;

    // Say where it went -- but only when the report was actually asked for.
    //
    // A report nobody can find is no better than none, so silence is a real
    // cost. It is still the right default: this line is stderr output that no
    // existing program emitted, and a crashing program's stderr is something
    // tools parse. Six route tests caught exactly that. So the notice is tied
    // to having opted in, and the default path stays byte-identical on stderr.
    if (asked_for) std::cerr << "[nilcrash] wrote " << target.string() << std::endl;
  } catch (...) {
    // Deliberately swallowed. A failure to describe a crash must not become a
    // second crash, and must not alter what the program reported.
  }
}

static void print_usage(std::ostream &out) {
  out << "Usage: nilinterpreter [options] <input.nil> [--] [args...]\n"
      << "\nOptions:\n"
      << "  --memory-mode=safe|unsafe  Select managed or manual memory mode\n"
      << "  --ai                       Enable the experimental AI control server\n"
      << "  --ai-wait                  Enable AI control and pause before main\n"
      << "  --strict-bounds            Fault on out-of-bounds array access (NIL-0026)\n"
      << "  --no-ai                    Disable the AI control server\n"
      << "  --version                  Show the nilinterpreter version\n"
      << "  -h, --help                 Show this help\n";
}

int main(int argc, char *argv[]) {
  ny::use_untranslated_standard_streams();
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "nilinterpreter " << NIL_VERSION << '\n';
    return 0;
  }
  // Parse CLI arguments
  std::string input_file;
  ny::MemoryMode memory_mode = ny::MemoryMode::UNSAFE_MANUAL;  // Default to unsafe (manual)
  bool ai_mode = false;
  bool ai_wait = false;
  bool strict_bounds = false;   // NIL-0006: --ai flag enables the embedded AI server

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
    } else if (arg == "--ai-wait") {
      ai_mode = true;
      ai_wait = true;
    } else if (arg == "--no-ai") {
      ai_mode = false;
      ai_wait = false;
    } else if (arg == "--strict-bounds") {
      strict_bounds = true;
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

  // Start only after endpoint creation/security succeeds. No server thread may
  // touch vm: all requests execute through the VM-owner safepoint callback.
  std::unique_ptr<ny_ai::Control> control;
  std::unique_ptr<ny_ai::Transport> transport;
  if (ai_mode) {
    // Let explicit Ny exits return through the owner so the control worker is
    // joined and its endpoint removed, preserving the requested process code.
    vm.setProcessExitTrapping(true);
    try {
#ifdef _WIN32
      const unsigned pid = GetCurrentProcessId();
#else
      const unsigned pid = static_cast<unsigned>(getpid());
#endif
      control = std::make_unique<ny_ai::Control>(pid, NIL_VERSION, ai_wait,
          [](const std::string&, const nil::detail::json& request) {
            const auto& msg = request.at("json");
            if (msg.type != nil::detail::json::kind::string || msg.text.empty() ||
                msg.text.find('\0') != std::string::npos)
              throw std::runtime_error("missing json message");
            auto inject = resolve_webview_inject();
            if (!inject) throw std::runtime_error("webview not loaded");
            inject(1, msg.text.c_str());
            auto result = nil::detail::json::map();
            result.object["ok"] = nil::detail::json::flag(true);
            return result;
          });
      const std::string handshake = "{\"event\":\"connected\",\"pid\":" +
          std::to_string(pid) + ",\"ai_version\":1,\"program\":\"ny\"}";
      transport = std::make_unique<ny_ai::Transport>(handshake,
          [&](const std::string& line) { return control->request(line); });
      transport->start();
      vm.setControlCallback([&](ny::VM& current) { control->safepoint(current); });
      std::cerr << "[ny_ai] Listening on " << transport->endpoint() << std::endl;
    } catch (const std::exception& e) {
      if (control) control->stop();
      if (transport) transport->stop();
      std::cerr << "nilinterpreter: AI control startup failed: " << e.what() << std::endl;
      return 1;
    }
  }

  if (strict_bounds) vm.setStrictBounds(true);

  int exit_code = 0;
  try {
    vm.execute();
    exit_code = vm.getRequestedExitCode().value_or(
        static_cast<int>(vm.getProgramExitCode()));
  } catch (const ny::UncaughtFault &e) {
    // Language faults share the same deterministic diagnostic as native entry.
    // Fatal VM safety errors below remain a separate, non-recoverable category.
    std::cerr << e.what() << std::endl;
    exit_code = 1;
    write_crash_report(input_file, e.faultType(), e.what(), e.sourceLine(), exit_code);
  } catch (const std::exception &e) {
    std::cerr << "NIL interpreter runtime error: " << e.what() << std::endl;
    exit_code = 1;
    write_crash_report(input_file, "NIL interpreter runtime error", e.what(), 0, exit_code);
  }

  if (control) control->stop();
  if (transport) transport->stop();
  vm.setControlCallback({});

  return exit_code;
}

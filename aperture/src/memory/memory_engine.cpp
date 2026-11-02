#include "memory_engine.hpp"
#include "../../../common/strict_json.hpp"

#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "../../../common/private_directory.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace aperture {
namespace {
namespace fs = std::filesystem;
using Json = nil::detail::json;

std::string string_field(const Json& object, const std::string& field) {
    const auto& item = object.at(field);
    if (item.type != Json::kind::string) throw std::runtime_error("invalid profile string: " + field);
    return item.text;
}

std::uint64_t count_field(const Json& object, const std::string& field) {
    const auto& item = object.at(field);
    if (item.type != Json::kind::number || item.text.empty() ||
        item.text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid profile count: " + field);
    try { return std::stoull(item.text); }
    catch (...) { throw std::runtime_error("profile count is out of range: " + field); }
}

bool flag_field(const Json& object, const std::string& field) {
    const auto& item = object.at(field);
    if (item.type != Json::kind::boolean) throw std::runtime_error("invalid profile flag: " + field);
    return item.boolean;
}

void validate_counters(const Json& object) {
    for (const char* field : {"allocated_bytes", "allocated_objects", "live_bytes", "live_objects",
                              "peak_bytes", "peak_objects", "freed_bytes", "freed_objects", "resizes"})
        count_field(object, field);
    for (const char* unit : {"bytes", "objects"}) {
        const auto allocated = count_field(object, std::string("allocated_") + unit);
        const auto freed = count_field(object, std::string("freed_") + unit);
        const auto live = count_field(object, std::string("live_") + unit);
        const auto peak = count_field(object, std::string("peak_") + unit);
        if (freed > allocated || live != allocated - freed || peak < live || peak > allocated)
            throw std::runtime_error("inconsistent profile counters");
    }
}

struct Profile { std::string raw; Json data; };
Profile read_profile(const fs::path& file) {
    constexpr std::uintmax_t maximum_bytes = 64 * 1024 * 1024;
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || !fs::is_regular_file(file) || size > maximum_bytes)
        throw std::runtime_error("cannot read memory profile (regular file up to 64 MiB required): " + file.string());
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open memory profile: " + file.string());
    std::string raw(static_cast<std::size_t>(size), '\0');
    input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
    if (input.gcount() != static_cast<std::streamsize>(raw.size()) || input.peek() != EOF)
        throw std::runtime_error("memory profile changed while reading");
    auto data = nil::detail::json_parser(raw).parse();
    if (string_field(data, "schema") != "aperture.memory.v1")
        throw std::runtime_error("unsupported memory profile schema");
    const auto route = string_field(data, "route");
    if (route != "vm" && route != "native") throw std::runtime_error("invalid profile route");
    if (string_field(data, "measurement") != "runtime_logical_bytes")
        throw std::runtime_error("unsupported profile measurement");
    const auto status = string_field(data, "status");
    if (status != "completed" && status != "failed" && status != "exited" &&
        status != "abandoned" && status != "profile_error")
        throw std::runtime_error("invalid profile status");
    flag_field(data, "completed");
    flag_field(data, "site_limit_reached");
    count_field(data, "excluded_sessions");
    validate_counters(data.at("totals"));
    for (const char* field : {"count", "duration_ns", "freed_bytes", "freed_objects"})
        count_field(data.at("collections"), field);
    for (const char* field : {"scope", "errors"}) {
        const auto& list = data.at(field);
        if (list.type != Json::kind::array) throw std::runtime_error("invalid profile list: " + std::string(field));
        for (const auto& item : list.array)
            if (item.type != Json::kind::string) throw std::runtime_error("invalid profile list entry");
    }
    const auto& sites = data.at("sites");
    if (sites.type != Json::kind::array) throw std::runtime_error("invalid profile sites");
    for (const auto& site : sites.array) {
        for (const char* field : {"file", "function", "kind"}) string_field(site, field);
        count_field(site, "line");
        validate_counters(site);
    }
    return {std::move(raw), std::move(data)};
}

bool complete(const Profile& profile) {
    const auto status = string_field(profile.data, "status");
    return flag_field(profile.data, "completed") && (status == "completed" || status == "exited") &&
           profile.data.at("errors").array.empty();
}

void print_profile(const Profile& profile, bool json, std::ostream& stream) {
    if (json) { stream << profile.raw; if (profile.raw.empty() || profile.raw.back() != '\n') stream << '\n'; return; }
    const auto& data = profile.data;
    const auto& totals = data.at("totals");
    stream << "Aperture memory profile (" << string_field(data, "route") << ", "
           << string_field(data, "status") << ")\n"
           << "  Allocated: " << count_field(totals, "allocated_bytes") << " bytes in "
           << count_field(totals, "allocated_objects") << " objects\n"
           << "  Live at session end: " << count_field(totals, "live_bytes") << " bytes in "
           << count_field(totals, "live_objects") << " objects\n"
           << "  Peak live: " << count_field(totals, "peak_bytes") << " bytes\n"
           << "  Freed: " << count_field(totals, "freed_bytes") << " bytes; resizes: "
           << count_field(totals, "resizes") << '\n'
           << "  Collections: " << count_field(data.at("collections"), "count")
           << "; elapsed: " << count_field(data.at("collections"), "duration_ns") << " ns\n"
           << "  Measurements cover runtime logical storage, not process RSS. Live objects are not classified as leaks.\n";
    std::vector<const Json*> sites;
    for (const auto& site : data.at("sites").array) sites.push_back(&site);
    std::stable_sort(sites.begin(), sites.end(), [](const Json* a, const Json* b) {
        return count_field(*a, "live_bytes") > count_field(*b, "live_bytes");
    });
    stream << "Allocation sites (largest live totals first):\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(sites.size(), 20); ++i) {
        const auto& site = *sites[i];
        auto file = string_field(site, "file");
        if (file.empty()) file = "<source unavailable>";
        stream << "  " << file << ':' << count_field(site, "line") << " in "
               << string_field(site, "function") << " [" << string_field(site, "kind") << "]: "
               << count_field(site, "live_bytes") << " live, " << count_field(site, "allocated_bytes")
               << " allocated bytes\n";
    }
    if (sites.size() > 20) stream << "  " << sites.size() - 20 << " more sites in the JSON report\n";
    if (flag_field(data, "site_limit_reached")) stream << "  Warning: allocation-site detail reached its recording limit.\n";
    if (count_field(data, "excluded_sessions")) stream << "  Warning: additional runtime sessions were excluded.\n";
    for (const auto& message : data.at("errors").array) stream << "  Profile error: " << message.text << '\n';
}

class TemporaryDirectory {
public:
    fs::path path;
    TemporaryDirectory()
        : path(nil::detail::create_private_directory(fs::temp_directory_path(),
                                                     "aperture-memory-")) {}
    ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};

// Reserve the user destination before executing anything. Failed publication
// removes only the new file this invocation created; existing files are intact.
class OutputFile {
    fs::path path;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif
    bool published = false;
public:
    explicit OutputFile(const std::string& requested) : path(requested) {
        if (path.empty()) return;
#ifdef _WIN32
        // CREATE_NEW is O_CREAT|O_EXCL: the destination is reserved only if it
        // did not already exist, so an existing report is never truncated by a
        // run that may yet fail.
        handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("cannot create output report: " + path.string());
#else
        descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor < 0) throw std::runtime_error("cannot create output report: " + path.string() + ": " + std::strerror(errno));
#endif
    }
    ~OutputFile() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (descriptor >= 0) close(descriptor);
#endif
        if (!path.empty() && !published) { std::error_code error; fs::remove(path, error); }
    }
    void publish(const std::string& raw) {
#ifdef _WIN32
        if (handle == INVALID_HANDLE_VALUE) return;
        std::size_t offset = 0;
        while (offset < raw.size()) {
            const auto remaining = raw.size() - offset;
            const DWORD request = static_cast<DWORD>(remaining > 1u << 20 ? 1u << 20 : remaining);
            DWORD written = 0;
            if (!WriteFile(handle, raw.data() + offset, request, &written, nullptr) || written == 0)
                throw std::runtime_error("cannot write output report: " + path.string());
            offset += written;
        }
        // FlushFileBuffers is fsync: a report that is reported as written has
        // reached the disk, not just the cache.
        if (!FlushFileBuffers(handle))
            throw std::runtime_error("cannot flush output report: " + path.string());
        const bool closed = CloseHandle(handle) != 0;
        handle = INVALID_HANDLE_VALUE;
        if (!closed) throw std::runtime_error("cannot close output report: " + path.string());
#else
        if (descriptor < 0) return;
        std::size_t offset = 0;
        while (offset < raw.size()) {
            const auto written = write(descriptor, raw.data() + offset, raw.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("cannot write output report: " + path.string());
            offset += static_cast<std::size_t>(written);
        }
        if (fsync(descriptor) != 0) throw std::runtime_error("cannot flush output report: " + path.string());
        const auto closed = close(descriptor); descriptor = -1;
        if (closed != 0) throw std::runtime_error("cannot close output report: " + path.string());
#endif
        published = true;
    }
};

#ifndef _WIN32
volatile sig_atomic_t active_child = 0;
volatile sig_atomic_t pending_signal = 0;
void forward_signal(int signal) {
    const int saved = errno;
    pending_signal = signal;
    if (active_child > 0) kill(active_child, signal);
    errno = saved;
}
class SignalForwarding {
    struct sigaction previous_int{}, previous_term{};
public:
    SignalForwarding() {
        active_child = 0; pending_signal = 0;
        struct sigaction action{};
        action.sa_handler = forward_signal;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, &previous_int) != 0)
            throw std::runtime_error("cannot install interruption handler");
        if (sigaction(SIGTERM, &action, &previous_term) != 0) {
            sigaction(SIGINT, &previous_int, nullptr);
            throw std::runtime_error("cannot install termination handler");
        }
    }
    ~SignalForwarding() {
        active_child = 0;
        sigaction(SIGINT, &previous_int, nullptr);
        sigaction(SIGTERM, &previous_term, nullptr);
    }
};
#else
// Windows needs no forwarding class. A console delivers Ctrl+C to every
// process attached to it, so the target receives the interruption directly --
// which is the behaviour the POSIX side installs a handler to reproduce. The
// target is deliberately left in this console and process group for exactly
// that reason, and so its own stdin keeps working.
#endif

#ifdef _WIN32
std::wstring widen(const std::string& value, const char* what) {
    if (value.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0)
        throw std::runtime_error(std::string("cannot encode ") + what + " as UTF-16: " + value);
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                        static_cast<int>(value.size()), wide.data(), needed);
    return wide;
}

// Quote one argument the way CommandLineToArgvW will parse it back. Windows
// passes a single command-line string, not an argv array, so an argument with
// a space or a quote has to be escaped here or the child silently sees two.
void append_quoted(std::wstring& line, const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    for (auto it = argument.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\') { ++it; ++backslashes; }
        if (it == argument.end()) { line.append(backslashes * 2, L'\\'); break; }
        if (*it == L'"') line.append(backslashes * 2 + 1, L'\\');
        else line.append(backslashes, L'\\');
        line += *it;
    }
    line += L'"';
}

// The child inherits this process's environment, so the variable is set here
// and restored afterwards rather than between fork and exec -- there is no
// such window on Windows. aperture runs one target at a time, so no other
// thread can observe the change.
class ScopedEnvironment {
    std::wstring name;
    std::wstring previous;
    bool had_previous = false;
public:
    ScopedEnvironment(const wchar_t* variable, const std::string& value, bool set)
        : name(variable) {
        // A zero-sized call reports the length to allocate, including the
        // terminator; zero means the variable is not set at all.
        const DWORD needed = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
        if (needed > 0) {
            previous.resize(needed);
            const DWORD copied = GetEnvironmentVariableW(name.c_str(), previous.data(), needed);
            previous.resize(copied <= needed ? copied : 0);
            had_previous = true;
        }
        const auto wide = set ? widen(value, "profile report path") : std::wstring();
        if (!SetEnvironmentVariableW(name.c_str(), set ? wide.c_str() : nullptr))
            throw std::runtime_error("cannot configure the target's profile environment");
    }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    ~ScopedEnvironment() {
        SetEnvironmentVariableW(name.c_str(), had_previous ? previous.c_str() : nullptr);
    }
};
#endif

int execute(const std::vector<std::string>& arguments, const std::string& report = {}) {
#ifdef _WIN32
    ScopedEnvironment profile(L"NIL_MEMORY_PROFILE", report, !report.empty());
    std::wstring line;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i) line += L' ';
        append_quoted(line, widen(arguments[i], "target argument"));
    }
    line += L'\0';
    const auto application = widen(arguments.front(), "target");
    std::cout.flush(); std::cerr.flush();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    // Tool progress belongs with diagnostics, never on the target's stdout.
    startup.hStdOutput = report.empty() ? GetStdHandle(STD_ERROR_HANDLE)
                                        : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION information{};
    if (!CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup,
                        &information))
        throw std::runtime_error("cannot start target process");
    CloseHandle(information.hThread);
    if (WaitForSingleObject(information.hProcess, INFINITE) != WAIT_OBJECT_0) {
        CloseHandle(information.hProcess);
        throw std::runtime_error("cannot collect target exit status");
    }
    DWORD status = 1;
    const bool read = GetExitCodeProcess(information.hProcess, &status) != 0;
    CloseHandle(information.hProcess);
    if (!read) throw std::runtime_error("cannot collect target exit status");
    return static_cast<int>(status);
#else
    SignalForwarding forwarding;
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    std::cout.flush(); std::cerr.flush();
    const auto child = fork();
    if (child < 0) throw std::runtime_error("cannot start target process");
    if (child == 0) {
        signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
        // Keep the terminal's foreground process group so input still works.
        if (report.empty()) {
            unsetenv("NIL_MEMORY_PROFILE");
            // Tool progress belongs with diagnostics, never target stdout.
            if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(126);
        }
        else if (setenv("NIL_MEMORY_PROFILE", report.c_str(), 1) != 0) _exit(126);
        execv(argv.front(), argv.data());
        const char message[] = "aperture: cannot execute target or adjacent tool\n";
        (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(127);
    }
    active_child = child;
    if (pending_signal) kill(child, pending_signal);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("cannot collect target exit status");
    }
    active_child = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
#endif
}

std::string tool(const MemoryOptions& options, const char* name) {
    auto path = fs::path(options.tool_directory) / name;
#ifdef _WIN32
    // Executables carry the suffix here, and Windows has no execute bit to
    // test: being a regular file in the tool directory is the whole check.
    path += ".exe";
    if (!fs::is_regular_file(path))
#else
    if (!fs::is_regular_file(path) || access(path.c_str(), X_OK) != 0)
#endif
        throw std::runtime_error("required adjacent NIL tool is unavailable: " + path.string());
    return path.string();
}
} // namespace

int MemoryEngine::run(const MemoryOptions& options) {
    if (!options.report.empty()) {
        const auto profile = read_profile(options.report);
        print_profile(profile, options.json, std::cout);
        return complete(profile) ? 0 : 1;
    }
    auto target = fs::absolute(options.target);
    if (!fs::is_regular_file(target)) throw std::runtime_error("target is not a regular file: " + target.string());
    auto extension = target.extension().string();
    for (char& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    const bool source = extension == ".ny" || extension == ".matrix" || extension == ".mat";
    bool bytecode = extension == ".nil";
    if (!source && !options.sdk_path.empty()) throw std::runtime_error("--sdk-path is only used for source compilation");
    if (options.native && !source) throw std::runtime_error("--native requires Ny or Matrix source; pass a native executable directly to profile it");
    if (!source && !bytecode && !options.memory_mode.empty())
        throw std::runtime_error("--memory-mode cannot change a precompiled native program");
    TemporaryDirectory temporary;
    OutputFile output(options.output);
    const auto report_path = temporary.path / "profile.json";
    if (source) {
        const auto nil = temporary.path / "program.nil";
        std::vector<std::string> compiler{tool(options, "nilcompiler"), "--quiet", "--debug-info", "--no-bundle"};
        if (!options.sdk_path.empty()) compiler.insert(compiler.end(), {"--sdk-path", options.sdk_path});
        compiler.insert(compiler.end(), {"-o", nil.string(), target.string()});
        auto status = execute(compiler);
        if (status != 0) return status;
        target = nil;
        bytecode = true;
        if (options.native) {
            const auto object = temporary.path / "program.o";
            std::vector<std::string> assembler{tool(options, "nilassembler")};
            if (options.memory_mode == "unsafe") assembler.push_back("--unmanaged");
            assembler.insert(assembler.end(), {"-o", object.string(), nil.string()});
            status = execute(assembler);
            if (status != 0) return status;
            // nillinker appends .exe to an extensionless Windows output.
            // CreateProcessW's explicit application path does not, so retain
            // the actual linked filename for the subsequent execution.
#ifdef _WIN32
            target = temporary.path / "program.exe";
#else
            target = temporary.path / "program";
#endif
            status = execute({tool(options, "nillinker"), "-o", target.string(), object.string()});
            if (status != 0) return status;
            bytecode = false;
        }
    }
    std::vector<std::string> command;
    if (bytecode) {
        command = {tool(options, "nilinterpreter"), "--no-ai"};
        if (!options.memory_mode.empty()) command.push_back("--memory-mode=" + options.memory_mode);
        command.push_back(target.string());
        command.push_back("--");
    } else command.push_back(target.string());
    command.insert(command.end(), options.arguments.begin(), options.arguments.end());
    const int status = execute(command, report_path.string());
    try {
        const auto profile = read_profile(report_path);
        output.publish(profile.raw);
        print_profile(profile, options.json, std::cerr);
        if (!options.output.empty() && !options.json)
            std::cerr << "Saved memory profile: " << fs::absolute(options.output).string() << '\n';
        if (status != 0) return status;
        if (!complete(profile)) {
            std::cerr << "aperture: runtime did not produce a complete memory profile\n";
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aperture: " << error.what() << '\n';
        return status != 0 ? status : 1;
    }
}
} // namespace aperture

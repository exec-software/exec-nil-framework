// Aperture — runtime diagnostics for the NIL framework.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "memory/memory_engine.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

static void usage() {
    std::cout <<
        "Aperture — NIL Framework Diagnostics\n\n"
        "Usage: aperture memory [options] <program.ny|matrix|nil|executable> [-- args...]\n"
        "  aperture memory --report <profile.json> [--json]\n\n"
        "Options:\n"
        "  --output <path>       Save a new aperture.memory.v1 JSON report\n"
        "  --json                Print the complete JSON report\n"
        "  --native              Compile Ny/Matrix source through the native toolchain\n"
        "  --memory-mode <mode>  safe or unsafe; VM and source compilation only\n"
        "  --sdk-path <path>     Standard-library search root for source compilation\n"
        "  --help                Show this help\n"
        "  --version             Show version information\n\n"
        "Recording preserves program stdout; the profile is printed on stderr.\n"
        "Saved-report inspection prints on stdout. Output files are never overwritten.\n"
        "CPU tracing, heap snapshots, leak detection and interactive analysis are planned.\n";
}

static std::string tool_directory(const char* executable) {
    namespace fs = std::filesystem;
    std::error_code error;
#if defined(__linux__)
    auto path = fs::read_symlink("/proc/self/exe", error);
    if (!error) return path.parent_path().string();
#endif
    fs::path supplied(executable);
    if (supplied.has_parent_path()) return fs::absolute(supplied).parent_path().string();
    if (const char* value = std::getenv("PATH")) {
        std::string paths(value);
        std::size_t start = 0;
        do {
            auto end = paths.find(':', start);
            auto directory = paths.substr(start, end - start);
            auto candidate = fs::path(directory.empty() ? "." : directory) / supplied;
            if (fs::is_regular_file(candidate, error))
                return fs::absolute(candidate).parent_path().string();
            if (end == std::string::npos) break;
            start = end + 1;
        } while (start <= paths.size());
    }
    throw std::runtime_error("cannot locate adjacent NIL tools");
}

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "aperture 0.1.0-dev (NIL Framework)\n";
            return 0;
        }
        if (argc == 1 || (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))) {
            usage();
            return argc == 1 ? 1 : 0;
        }
        const std::string command = argv[1];
        if (command == "trace" || command == "explore" || command == "agent")
            throw std::runtime_error(command + " is not implemented; use memory to record runtime allocations");
        if (command != "memory") throw std::runtime_error("unknown subcommand: " + command);
        aperture::MemoryOptions options;
        for (int i = 2; i < argc; ++i) {
            std::string argument = argv[i];
            auto value = [&]() {
                if (++i == argc) throw std::runtime_error(argument + " requires a value");
                return std::string(argv[i]);
            };
            if (argument == "--help" || argument == "-h") { usage(); return 0; }
            else if (argument == "--json") options.json = true;
            else if (argument == "--native") options.native = true;
            else if (argument == "--output") {
                if (!options.output.empty()) throw std::runtime_error("--output specified more than once");
                options.output = value();
            } else if (argument == "--report") {
                if (!options.report.empty()) throw std::runtime_error("--report specified more than once");
                options.report = value();
            } else if (argument == "--sdk-path") options.sdk_path = value();
            else if (argument == "--memory-mode") options.memory_mode = value();
            else if (argument.rfind("--memory-mode=", 0) == 0) options.memory_mode = argument.substr(14);
            else if (argument == "--") {
                if (options.target.empty()) throw std::runtime_error("program arguments require a target before --");
                while (++i < argc) options.arguments.emplace_back(argv[i]);
            } else if (!argument.empty() && argument[0] == '-') {
                throw std::runtime_error("unknown memory option: " + argument);
            } else if (options.target.empty()) options.target = argument;
            else throw std::runtime_error("only one target is accepted; separate program arguments with --");
        }
        if (!options.memory_mode.empty() && options.memory_mode != "safe" && options.memory_mode != "unsafe")
            throw std::runtime_error("--memory-mode must be safe or unsafe");
        if (!options.report.empty()) {
            if (!options.target.empty() || !options.output.empty() || options.native ||
                !options.arguments.empty() || !options.memory_mode.empty() || !options.sdk_path.empty())
                throw std::runtime_error("--report accepts only --json; it does not execute a target");
        } else if (options.target.empty()) throw std::runtime_error("memory requires a target or --report");
        options.tool_directory = tool_directory(argv[0]);
        return aperture::MemoryEngine{}.run(options);
    } catch (const std::exception& error) {
        std::cerr << "aperture: " << error.what() << '\n';
        return 1;
    }
}

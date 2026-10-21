// Aperture — main entry point
// Unified analysis and debugging suite for NIL framework.

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <getopt.h>

#include "core/aperture_core.hpp"
#include "explore/explore_engine.hpp"
#include "memory/memory_engine.hpp"
#include "trace/trace_engine.hpp"
#include "agent/agent_interface.hpp"

static constexpr char VERSION[] = "aperture 0.1.0-dev (NIL Framework)";

static void print_usage() {
    std::cout <<
        "Aperture — NIL Framework Analysis & Debugging Suite\n"
        "\n"
        "Usage: aperture <subcommand> [options] <target>\n"
        "\n"
        "Subcommands:\n"
        "  explore  Explore code structure, generate call graphs\n"
        "  memory   Profile memory allocations and detect leaks\n"
        "  trace    Trace execution performance, flame graphs\n"
        "  agent    AI-assisted interactive analysis\n"
        "\n"
        "Options:\n"
        "  --help       Show this help message\n"
        "  --version    Show version information\n"
        "  --output <f> Output file (default: stdout)\n"
        "  --json       Output structured JSON\n"
        "\n";
}

static void print_version() {
    std::cout << VERSION << '\n';
}

int main(int argc, char* argv[]) {
    // Minimal argument parsing
    std::string subcommand;
    std::string target;
    bool json = false;
    
    static struct option long_options[] = {
        {"help",    no_argument,       nullptr, 'h'},
        {"version", no_argument,       nullptr, 'v'},
        {"json",    no_argument,       nullptr, 'j'},
        {nullptr,   0,                 nullptr,  0  }
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "hjv", long_options, nullptr)) != -1) {
        switch (c) {
            case 'h': print_usage(); return 0;
            case 'v': print_version(); return 0;
            case 'j': json = true; break;
            case '?': print_usage(); return 1;
        }
    }
    
    if (optind >= argc) {
        std::cerr << "aperture: missing subcommand\n";
        print_usage();
        return 1;
    }
    
    subcommand = argv[optind++];
    
    if (optind >= argc) {
        std::cerr << "aperture: " << subcommand << " requires a target file\n";
        return 1;
    }
    
    target = argv[optind++];
    
    // Dispatch to the appropriate engine
    if (subcommand == "explore") {
        aperture::ExploreEngine engine;
        return engine.run(target, json) ? 0 : 1;
    }
    else if (subcommand == "memory") {
        aperture::MemoryEngine engine;
        return engine.run(target, json) ? 0 : 1;
    }
    else if (subcommand == "trace") {
        aperture::TraceEngine engine;
        return engine.run(target, json) ? 0 : 1;
    }
    else if (subcommand == "agent") {
        aperture::AgentInterface agent;
        return agent.interact(target, json) ? 0 : 1;
    }
    else {
        std::cerr << "aperture: unknown subcommand '" << subcommand << "'\n";
        print_usage();
        return 1;
    }
}

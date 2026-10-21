// Aperture Core — shared types and utilities
// Common foundation for all Aperture engines.

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>

namespace aperture {

// Analysis mode flags
enum class Mode {
    Human,          // Interactive / text output
    Machine,        // Structured JSON / NDJSON output
    Agent,          // AI agent interface
};

// Analysis target — what we're examining
struct TargetInfo {
    std::string path;
    bool is_nil_file;    // .nil bytecode
    bool is_native;      // native binary
    std::string language; // "ny" or "matrix"
};

// Analysis result — output of any engine
struct AnalysisResult {
    std::string summary;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> findings;
    bool success = false;
};

// Shared logger — thread-safe
class Logger {
public:
    static void info(const std::string& msg);
    static void warn(const std::string& msg);
    static void error(const std::string& msg);
    static void trace(const std::string& msg);
    
    static void set_verbose(bool v) { verbose_ = v; }
    static bool is_verbose() { return verbose_; }

private:
    static bool verbose_;
};

// Parse a target file and return metadata
TargetInfo resolve_target(const std::string& path);

} // namespace aperture

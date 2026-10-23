// Aperture records runtime memory measurements; it does not classify leaks.
#pragma once

#include <string>
#include <vector>

namespace aperture {
struct MemoryOptions {
    std::string target;
    std::string report;
    std::string output;
    std::string tool_directory;
    std::string memory_mode;
    std::string sdk_path;
    std::vector<std::string> arguments;
    bool json = false;
    bool native = false;
};

class MemoryEngine {
public:
    // Return the target's exit status, or 1 when recording/report validation fails.
    int run(const MemoryOptions& options);
};
} // namespace aperture

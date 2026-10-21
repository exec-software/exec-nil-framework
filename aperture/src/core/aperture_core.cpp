// Aperture Core — implementation

#include "aperture_core.hpp"
#include <iostream>
#include <fstream>
#include <chrono>

namespace aperture {

bool Logger::verbose_ = false;

static std::string timestamp() {
    return "[aperture] ";
}

void Logger::info(const std::string& msg) {
    std::cout << timestamp() << msg << '\n';
}

void Logger::warn(const std::string& msg) {
    std::cerr << timestamp() << "WARN: " << msg << '\n';
}

void Logger::error(const std::string& msg) {
    std::cerr << timestamp() << "ERROR: " << msg << '\n';
}

void Logger::trace(const std::string& msg) {
    if (verbose_) {
        std::cout << timestamp() << "TRACE: " << msg << '\n';
    }
}

// Resolve a target file: determine type and language
TargetInfo resolve_target(const std::string& path) {
    TargetInfo info;
    info.path = path;
    
    // Check extension
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        Logger::warn("No extension found in target: " + path);
        return info;
    }
    
    std::string ext = path.substr(dot + 1);
    
    if (ext == "nil") {
        info.is_nil_file = true;
    } else if (ext == "ny") {
        info.language = "ny";
    } else if (ext == "matrix" || ext == "mat") {
        info.language = "matrix";
    } else {
        info.is_native = true;
    }
    
    Logger::trace("Resolved target: " + path + " (nil=" + 
                  std::to_string(info.is_nil_file) + ")");
    
    return info;
}

} // namespace aperture

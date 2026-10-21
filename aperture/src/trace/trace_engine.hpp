// Aperture Trace — performance tracing engine
// CPU profiling, flame graphs, execution tracing.

#pragma once

#include "core/aperture_core.hpp"
#include <vector>
#include <map>
#include <chrono>

namespace aperture {

// A sampled stack frame
struct StackFrame {
    std::string function;
    std::string file;
    int line = 0;
    std::chrono::nanoseconds duration;
    std::chrono::nanoseconds self_time;
    int samples = 0;
};

// A flame graph snapshot
struct FlameSnapshot {
    int timestamp;
    std::vector<std::string> stack;
};

// Trace analysis results
struct TraceResult {
    std::vector<StackFrame> frames;
    std::vector<FlameSnapshot> flame_snapshots;
    std::string hotspot_function;
    std::chrono::nanoseconds total_duration;
    std::map<std::string, std::chrono::nanoseconds> function_times;
};

// Performance tracing engine
class TraceEngine {
public:
    TraceEngine();
    ~TraceEngine();
    
    // Run trace profiling on a target
    bool run(const std::string& target, bool json_output);
    
    // Generate a flame graph
    TraceResult generate_flame(const std::string& target);
    
    // Export as structured JSON
    std::string to_json(const TraceResult& result);

private:
    // Collect samples during execution
    TraceResult collect_samples(const std::string& target, int sample_rate_hz);
    
    // Identify the hotspot function
    std::string find_hotspot(const std::vector<StackFrame>& frames);
    
    // Build a call tree from frames
    std::vector<StackFrame> build_call_tree(const std::vector<FlameSnapshot>& snapshots);
};

} // namespace aperture

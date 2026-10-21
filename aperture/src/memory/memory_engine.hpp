// Aperture Memory — memory profiling engine
// Heap analysis, allocation tracing, leak detection.

#pragma once

#include "core/aperture_core.hpp"
#include <vector>
#include <map>

namespace aperture {

// A recorded allocation event
struct AllocationEvent {
    size_t address;
    size_t size;
    std::string site;        // call site (function:line)
    std::string type;        // allocated type
    int generation = 0;      // GC generation (if managed)
    bool freed = false;
};

// A heap snapshot
struct HeapSnapshot {
    int timestamp;                    // execution tick
    size_t total_allocated;           // bytes alive
    size_t total_objects;             // objects alive
    size_t total_freed;               // bytes freed since start
    std::vector<AllocationEvent> events;
};

// Memory analysis results
struct MemoryResult {
    std::vector<HeapSnapshot> snapshots;
    std::vector<std::string> leaks;    // unfreed allocations
    std::map<std::string, size_t> site_totals; // site -> total bytes allocated
    double leak_ratio;                 // leaked bytes / total allocated
};

// Memory profiling engine
class MemoryEngine {
public:
    MemoryEngine();
    ~MemoryEngine();
    
    // Run memory profiling on a target
    bool run(const std::string& target, bool json_output);
    
    // Capture a heap snapshot
    HeapSnapshot capture(const std::string& target);
    
    // Detect memory leaks
    std::vector<std::string> detect_leaks(const std::string& target);
    
    // Export as structured JSON
    std::string to_json(const MemoryResult& result);

private:
    // Trace allocations during execution
    MemoryResult trace_allocations(const std::string& target);
    
    // Compare two snapshots
    void diff_snapshots(const HeapSnapshot& before, const HeapSnapshot& after);
    
    // Identify allocation hotspots
    std::vector<std::pair<std::string, size_t>> find_hotspots(
        const std::vector<AllocationEvent>& events);
};

} // namespace aperture

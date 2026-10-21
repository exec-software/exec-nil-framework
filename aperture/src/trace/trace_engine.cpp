// Aperture Trace — implementation

#include "trace_engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace aperture {

TraceEngine::TraceEngine() {}
TraceEngine::~TraceEngine() {}

bool TraceEngine::run(const std::string& target, bool json_output) {
    Logger::info("Tracing execution: " + target);
    
    auto info = resolve_target(target);
    Logger::info("Target: " + info.path);
    
    TraceResult result = collect_samples(target, 99); // 99 Hz sampling
    
    result.hotspot_function = find_hotspot(result.frames);
    
    Logger::info("Hotspot: " + result.hotspot_function);
    
    if (json_output) {
        std::cout << to_json(result) << '\n';
    } else {
        std::cout << "=== Performance Trace: " << target << " ===\n";
        std::cout << "Total frames: " << result.frames.size() << "\n";
        std::cout << "Hotspot: " << result.hotspot_function << "\n";
        
        std::cout << "\nFunction timings:\n";
        for (auto& [fn, time] : result.function_times) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time);
            std::cout << "  " << fn << ": " << ms.count() << " ms\n";
        }
    }
    
    return true;
}

TraceResult TraceEngine::generate_flame(const std::string& target) {
    return collect_samples(target, 99);
}

TraceResult TraceEngine::collect_samples(const std::string& target, int sample_rate_hz) {
    TraceResult result;
    
    Logger::trace("Sampling at " + std::to_string(sample_rate_hz) + " Hz");
    
    // TODO: integrate with NIL interpreter's execution hooks
    // Placeholder result
    
    StackFrame main;
    main.function = "main";
    main.file = target;
    main.line = 1;
    main.samples = 42;
    main.self_time = std::chrono::nanoseconds(5000000);
    main.duration = std::chrono::nanoseconds(15000000);
    
    StackFrame calc;
    calc.function = "squared_length";
    calc.file = target;
    calc.line = 3;
    calc.samples = 128;
    calc.self_time = std::chrono::nanoseconds(8000000);
    calc.duration = std::chrono::nanoseconds(10000000);
    
    result.frames.push_back(main);
    result.frames.push_back(calc);
    
    result.function_times["main"] = main.duration;
    result.function_times["squared_length"] = calc.duration;
    
    result.total_duration = std::chrono::nanoseconds(15000000);
    
    return result;
}

std::string TraceEngine::find_hotspot(const std::vector<StackFrame>& frames) {
    std::string hotspot;
    size_t max_samples = 0;
    
    for (auto& frame : frames) {
        if (frame.samples > static_cast<int>(max_samples)) {
            max_samples = frame.samples;
            hotspot = frame.function;
        }
    }
    
    return hotspot.empty() ? "(none)" : hotspot;
}

std::vector<StackFrame> TraceEngine::build_call_tree(const std::vector<FlameSnapshot>& snapshots) {
    // Aggregate snapshots into a call tree
    std::vector<StackFrame> tree;
    
    for (auto& snap : snapshots) {
        for (auto& fn : snap.stack) {
            StackFrame frame;
            frame.function = fn;
            frame.samples = 1;
            tree.push_back(frame);
        }
    }
    
    return tree;
}

std::string TraceEngine::to_json(const TraceResult& result) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"type\": \"trace\",\n";
    out << "  \"total_duration_ns\": " << result.total_duration.count() << ",\n";
    out << "  \"hotspot\": \"" << result.hotspot_function << "\",\n";
    out << "  \"frames\": [";
    
    bool first = true;
    for (auto& frame : result.frames) {
        if (!first) out << ",";
        first = false;
        out << "\n    {\n";
        out << "      \"function\": \"" << frame.function << "\",\n";
        out << "      \"file\": \"" << frame.file << "\",\n";
        out << "      \"line\": " << frame.line << ",\n";
        out << "      \"samples\": " << frame.samples << ",\n";
        out << "      \"duration_ns\": " << frame.duration.count() << ",\n";
        out << "      \"self_time_ns\": " << frame.self_time.count() << "\n";
        out << "    }";
    }
    
    out << "\n  ]\n";
    out << "}\n";
    
    return out.str();
}

} // namespace aperture

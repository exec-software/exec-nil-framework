// Aperture Agent — implementation

#include "agent_interface.hpp"
#include "explore/explore_engine.hpp"
#include "memory/memory_engine.hpp"
#include "trace/trace_engine.hpp"
#include <iostream>
#include <sstream>

namespace aperture {

AgentInterface::AgentInterface() {}
AgentInterface::~AgentInterface() {}

bool AgentInterface::interact(const std::string& target, bool json_output) {
    Logger::info("AI agent mode: " + target);
    
    auto info = resolve_target(target);
    Logger::info("Analyzing: " + info.path);
    
    // Run a comprehensive analysis
    AgentResponse response;
    
    // 1. Explore the code
    auto explore_resp = ask_call_graph(target, "main");
    response.findings.insert(response.findings.end(), 
                            explore_resp.findings.begin(), explore_resp.findings.end());
    
    // 2. Check memory
    auto memory_resp = ask_memory(target);
    response.findings.insert(response.findings.end(),
                            memory_resp.findings.begin(), memory_resp.findings.end());
    response.recommendations.insert(response.recommendations.end(),
                                    memory_resp.recommendations.begin(), 
                                    memory_resp.recommendations.end());
    
    // 3. Check performance
    auto trace_resp = ask_performance(target);
    response.findings.insert(response.findings.end(),
                            trace_resp.findings.begin(), trace_resp.findings.end());
    response.recommendations.insert(response.recommendations.end(),
                                    trace_resp.recommendations.begin(),
                                    trace_resp.recommendations.end());
    
    // 4. Generate recommendations
    response.analysis = format_for_agent(target + ": " + std::to_string(response.findings.size()) + 
                                          " findings, " + std::to_string(response.recommendations.size()) +
                                          " recommendations");
    
    if (json_output) {
        // Export as structured JSON
        std::ostringstream out;
        out << "{\n  \"type\": \"agent_analysis\",\n";
        out << "  \"target\": \"" << target << "\",\n";
        out << "  \"analysis\": \"" << response.analysis << "\",\n";
        out << "  \"findings\": [";
        for (size_t i = 0; i < response.findings.size(); i++) {
            out << "\"" << response.findings[i] << "\"";
            if (i + 1 < response.findings.size()) out << ", ";
        }
        out << "],\n  \"recommendations\": [";
        for (size_t i = 0; i < response.recommendations.size(); i++) {
            out << "\"" << response.recommendations[i] << "\"";
            if (i + 1 < response.recommendations.size()) out << ", ";
        }
        out << "]\n}\n";
        std::cout << out.str() << '\n';
    } else {
        // Human-readable
        std::cout << response.analysis << "\n";
        
        if (!response.findings.empty()) {
            std::cout << "\nFindings:\n";
            for (auto& f : response.findings) {
                std::cout << "  - " << f << "\n";
            }
        }
        
        if (!response.recommendations.empty()) {
            std::cout << "\nRecommendations:\n";
            for (auto& r : response.recommendations) {
                std::cout << "  * " << r << "\n";
            }
        }
    }
    
    return true;
}

AgentResponse AgentInterface::analyze(const std::string& target, const AnalysisQuery& query) {
    if (query.type == "call_graph") {
        return delegate_to_explore(target, query);
    } else if (query.type == "leak") {
        return delegate_to_memory(target, query);
    } else if (query.type == "hotspot") {
        return delegate_to_trace(target, query);
    }
    
    AgentResponse fallback;
    fallback.analysis = "Unknown query type: " + query.type;
    return fallback;
}

AgentResponse AgentInterface::ask_call_graph(const std::string& target, const std::string& function) {
    Logger::info("Agent query: call graph for '" + function + "'");
    
    ExploreEngine engine;
    auto result = engine.generate_call_graph(target);
    
    AgentResponse response;
    response.analysis = "Call graph contains " + std::to_string(result.functions.size()) + " functions";
    
    for (auto& fn : result.functions) {
        response.findings.push_back("Function: " + fn.name + " at " + fn.file + ":" + std::to_string(fn.line));
    }
    
    return response;
}

AgentResponse AgentInterface::ask_memory(const std::string& target) {
    Logger::info("Agent query: memory analysis");
    
    MemoryEngine engine;
    auto leaks = engine.detect_leaks(target);
    
    AgentResponse response;
    response.analysis = "Memory scan complete";
    
    if (leaks.empty()) {
        response.analysis += " — no leaks detected";
    } else {
        response.analysis += " — " + std::to_string(leaks.size()) + " leaks found";
        response.recommendations.push_back("Review " + std::to_string(leaks.size()) + " allocation sites for leaks");
    }
    
    return response;
}

AgentResponse AgentInterface::ask_performance(const std::string& target) {
    Logger::info("Agent query: performance analysis");
    
    TraceEngine engine;
    auto result = engine.generate_flame(target);
    
    AgentResponse response;
    response.analysis = "Performance trace: hotspot is " + result.hotspot_function;
    
    for (auto& frame : result.frames) {
        response.findings.push_back(
            frame.function + ": " + std::to_string(frame.samples) + " samples");
    }
    
    if (!result.hotspot_function.empty()) {
        response.recommendations.push_back("Profile " + result.hotspot_function + " for optimization");
    }
    
    return response;
}

AgentResponse AgentInterface::recommend(const std::string& target) {
    // Aggregate all analyses and produce recommendations
    AgentResponse response;
    response.analysis = "Comprehensive analysis of " + target;
    
    // Check all engines
    auto mem = ask_memory(target);
    auto perf = ask_performance(target);
    
    response.findings.insert(response.findings.end(),
                            mem.findings.begin(), mem.findings.end());
    response.findings.insert(response.findings.end(),
                            perf.findings.begin(), perf.findings.end());
    response.recommendations.insert(response.recommendations.end(),
                                    mem.recommendations.begin(), mem.recommendations.end());
    response.recommendations.insert(response.recommendations.end(),
                                    perf.recommendations.begin(), perf.recommendations.end());
    
    return response;
}

// Delegation methods — route queries to the right engine
AgentResponse AgentInterface::delegate_to_explore(const std::string& target, const AnalysisQuery&) {
    return ask_call_graph(target, "main");
}

AgentResponse AgentInterface::delegate_to_memory(const std::string& target, const AnalysisQuery&) {
    return ask_memory(target);
}

AgentResponse AgentInterface::delegate_to_trace(const std::string& target, const AnalysisQuery&) {
    return ask_performance(target);
}

std::string AgentInterface::format_for_agent(const std::string& raw_data) {
    // Format findings for machine-readable consumption
    return raw_data;
}

} // namespace aperture

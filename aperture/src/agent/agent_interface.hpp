// Aperture Agent — AI agent interface
// Conversational analysis, machine-readable output, recommendation engine.

#pragma once

#include "core/aperture_core.hpp"
#include <vector>
#include <string>

namespace aperture {

// An analysis question the agent can answer
struct AnalysisQuery {
    std::string question;
    std::string type;        // "call_graph", "leak", "hotspot", "pattern"
    std::map<std::string, std::string> params;
};

// Agent response
struct AgentResponse {
    std::string analysis;
    std::vector<std::string> recommendations;
    std::vector<std::string> findings;
    std::string json_export;
};

// Agent interface — AI-driven analysis
class AgentInterface {
public:
    AgentInterface();
    ~AgentInterface();
    
    // Interactive analysis session
    bool interact(const std::string& target, bool json_output);
    
    // Process a single query
    AgentResponse analyze(const std::string& target, const AnalysisQuery& query);
    
    // Ask about call graphs
    AgentResponse ask_call_graph(const std::string& target, const std::string& function);
    
    // Ask about memory usage
    AgentResponse ask_memory(const std::string& target);
    
    // Ask about performance
    AgentResponse ask_performance(const std::string& target);
    
    // Generate a recommendation report
    AgentResponse recommend(const std::string& target);

private:
    // Delegate to explore engine
    AgentResponse delegate_to_explore(const std::string& target, const AnalysisQuery& query);
    
    // Delegate to memory engine
    AgentResponse delegate_to_memory(const std::string& target, const AnalysisQuery& query);
    
    // Delegate to trace engine
    AgentResponse delegate_to_trace(const std::string& target, const AnalysisQuery& query);
    
    // Format findings for AI consumption
    std::string format_for_agent(const std::string& raw_data);
};

} // namespace aperture

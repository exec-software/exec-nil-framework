// Aperture Explore — code exploration engine
// Decompilation, call graph generation, dependency analysis.

#pragma once

#include "core/aperture_core.hpp"
#include <vector>
#include <map>

namespace aperture {

// A node in a call graph
struct CallGraphNode {
    std::string name;           // function/method name
    std::string file;           // source file
    int line = 0;               // declaration line
    std::vector<std::string> calls;   // outgoing calls
    std::vector<std::string> called_by; // incoming calls
    std::vector<std::string> types;    // referenced types
};

// Explore results
struct ExploreResult {
    std::vector<CallGraphNode> functions;
    std::map<std::string, std::vector<std::string>> modules; // module -> functions
    std::map<std::string, std::vector<std::string>> type_hierarchy; // parent -> children
};

// Main exploration engine
class ExploreEngine {
public:
    ExploreEngine();
    ~ExploreEngine();
    
    // Run exploration on a target file
    bool run(const std::string& target, bool json_output);
    
    // Generate a call graph from bytecode
    ExploreResult generate_call_graph(const std::string& target);
    
    // Export as structured JSON
    std::string to_json(const ExploreResult& result);

private:
    // Parse NIL bytecode for call metadata
    ExploreResult parse_nil_bytecode(const std::string& target);
    
    // Analyze source-level declarations
    ExploreResult analyze_source(const std::string& target, const std::string& language);
    
    // Build transitive call graph
    void build_call_edges(ExploreResult& result);
};

} // namespace aperture

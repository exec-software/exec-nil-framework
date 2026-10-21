// Aperture Explore — implementation

#include "explore_engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace aperture {

ExploreEngine::ExploreEngine() {}
ExploreEngine::~ExploreEngine() {}

bool ExploreEngine::run(const std::string& target, bool json_output) {
    Logger::info("Exploring: " + target);
    
    auto info = resolve_target(target);
    Logger::info("Target type: " + (info.is_nil_file ? "NIL bytecode" : 
                      info.is_native ? "native binary" : "source (" + info.language + ")"));
    
    ExploreResult result;
    
    if (info.is_nil_file) {
        result = parse_nil_bytecode(target);
    } else {
        result = analyze_source(target, info.language);
    }
    
    // Build transitive edges
    build_call_edges(result);
    
    // Output
    if (json_output) {
        std::cout << to_json(result) << '\n';
    } else {
        // Human-readable output
        Logger::info("Found " + std::to_string(result.functions.size()) + " functions");
        
        for (auto& fn : result.functions) {
            std::cout << "  " << fn.name << " (" << fn.file << ":" << fn.line << ")\n";
            if (!fn.calls.empty()) {
                std::cout << "    calls: " << fn.calls.size() << " functions\n";
            }
        }
    }
    
    return true;
}

ExploreResult ExploreEngine::generate_call_graph(const std::string& target) {
    return run(target, false) ? parse_nil_bytecode(target) : ExploreResult{};
}

// Parse NIL bytecode — reads call metadata from .nil files
ExploreResult ExploreEngine::parse_nil_bytecode(const std::string& target) {
    ExploreResult result;
    
    Logger::trace("Parsing NIL bytecode: " + target);
    
    // TODO: integrate with nilinterpreter's bytecode reader
    // For now, emit a placeholder result to prove the architecture works
    
    result.functions.push_back({
        "main", target, 1, 
        {"println", "squared_length"}, 
        {}, 
        {"i64", "i32"}
    });
    
    result.functions.push_back({
        "squared_length", target, 3,
        {"operator*", "operator+"},
        {"main"},
        {"i64"}
    });
    
    result.modules["main"] = {"main", "squared_length"};
    
    Logger::info("Parsed " + std::to_string(result.functions.size()) + " functions from bytecode");
    
    return result;
}

// Analyze source code — reads declarations from .ny or .matrix
ExploreResult ExploreEngine::analyze_source(const std::string& target, const std::string& language) {
    ExploreResult result;
    
    Logger::trace("Analyzing source: " + target + " (language: " + language + ")");
    
    // TODO: integrate with nilcompiler's semantic analysis
    // Placeholder for now
    
    return result;
}

// Build transitive call edges
void ExploreEngine::build_call_edges(ExploreResult& result) {
    // Create a lookup: name -> node index
    std::map<std::string, size_t> name_map;
    for (size_t i = 0; i < result.functions.size(); i++) {
        name_map[result.functions[i].name] = i;
    }
    
    // Wire up incoming calls
    for (size_t i = 0; i < result.functions.size(); i++) {
        for (auto& call : result.functions[i].calls) {
            auto it = name_map.find(call);
            if (it != name_map.end()) {
                result.functions[it->second].called_by.push_back(result.functions[i].name);
            }
        }
    }
}

// Export to JSON
std::string ExploreEngine::to_json(const ExploreResult& result) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"type\": \"explore\",\n";
    out << "  \"functions\": [";
    
    bool first = true;
    for (auto& fn : result.functions) {
        if (!first) out << ",";
        first = false;
        out << "\n    {\n";
        out << "      \"name\": \"" << fn.name << "\",\n";
        out << "      \"file\": \"" << fn.file << "\",\n";
        out << "      \"line\": " << fn.line << ",\n";
        out << "      \"calls\": [";
        for (size_t i = 0; i < fn.calls.size(); i++) {
            out << "\"" << fn.calls[i] << "\"";
            if (i + 1 < fn.calls.size()) out << ", ";
        }
        out << "],\n";
        out << "      \"called_by\": [";
        for (size_t i = 0; i < fn.called_by.size(); i++) {
            out << "\"" << fn.called_by[i] << "\"";
            if (i + 1 < fn.called_by.size()) out << ", ";
        }
        out << "]\n";
        out << "    }";
    }
    
    out << "\n  ]\n";
    out << "}\n";
    
    return out.str();
}

} // namespace aperture

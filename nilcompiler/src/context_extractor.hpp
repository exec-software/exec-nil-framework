#ifndef NY_COMPILER_CONTEXT_EXTRACTOR_HPP
#define NY_COMPILER_CONTEXT_EXTRACTOR_HPP

// context_extractor.hpp
// AI context extraction for the Ny language.
//
// This component parses Ny source files and extracts structured information from
// @doc and @ai_context annotations, building a queryable knowledge base.
//
// Purpose:
//  - Extract intent, assumptions, constraints, and trade-offs
//  - Build semantic index for intent-based code search
//  - Generate rich documentation with AI context
//  - Enable assumption validation and trade-off analysis
//
// Usage:
//  context_extractor extractor;
//  extractor.extract_from_file("mycode.ny");
//  std::string json = extractor.to_json();

#include "ast.hpp"
#include "parser.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ny {

// Structured @doc annotation data
struct doc_context {
  std::string summary;
  std::string description;
  std::map<std::string, std::string> params; // param_name -> description
  std::string returns;
  std::vector<std::string> throws;
  std::vector<std::string> examples;
  std::string complexity;
  std::string author;
  std::string since;
  std::vector<std::string> see_also;

  bool empty() const { return summary.empty() && description.empty(); }
};

// Structured @ai_context annotation data
struct ai_context {
  std::string intent;
  std::vector<std::string> assumptions;
  std::vector<std::string> constraints;
  std::map<std::string, std::string> trade_offs; // aspect -> explanation
  std::vector<std::string> related_topics;
  std::string typical_usage;
  std::vector<std::string> anti_patterns;

  bool empty() const { return intent.empty(); }
};

// @category annotation (hierarchical organization)
struct category_context {
  std::string path; // e.g., "algorithms.sorting.comparison_based"

  bool empty() const { return path.empty(); }
};

// @performance annotation
struct performance_context {
  std::string time_complexity;
  std::string space_complexity;
  std::string best_case;
  std::string worst_case;
  std::string cache_behavior;
  bool parallelizable = false;
  bool io_bound = false;
  std::map<std::string, std::string> benchmarks;

  bool empty() const { return time_complexity.empty(); }
};

// Source location for extracted context
struct source_location {
  std::string filename;
  int line = 0;
  int column = 0;

  source_location() = default;
  source_location(std::string f, int l, int c = 0)
      : filename(std::move(f)), line(l), column(c) {}
};

// Extracted function context
struct function_context {
  std::string name;
  std::string qualified_name; // module::submodule::function_name
  std::string signature;      // Full function signature
  source_location location;

  // Extracted annotations
  doc_context doc;
  ai_context ai;
  category_context category;
  performance_context performance;

  // Additional metadata
  bool is_public = false;
  bool is_generic = false;
  std::vector<std::string> generic_params;
  std::vector<std::string> other_annotations; // @inline, @pure, etc.

  // Convert to JSON object
  std::string to_json(int indent = 0) const;
};

// Extracted struct/class context
struct struct_context {
  std::string name;
  std::string qualified_name;
  source_location location;

  // Annotations
  doc_context doc;
  ai_context ai;
  category_context category;

  // Metadata
  bool is_public = false;
  bool is_generic = false;
  std::vector<std::string> generic_params;
  std::vector<std::string> fields;

  std::string to_json(int indent = 0) const;
};

// Extracted module context
struct module_context {
  std::string name;
  std::string qualified_name;
  source_location location;

  // Annotations
  doc_context doc;
  ai_context ai;

  // Contents
  std::vector<function_context> functions;
  std::vector<struct_context> structs;

  std::string to_json(int indent = 0) const;
};

// Complete extracted context from a codebase
struct extracted_context {
  std::string codebase_name;
  std::string version;
  std::string timestamp;

  std::vector<function_context> functions;
  std::vector<struct_context> structs;
  std::vector<module_context> modules;

  // Index by category for fast lookup
  std::map<std::string, std::vector<function_context *>> category_index;

  // Index by topic for semantic search
  std::map<std::string, std::vector<function_context *>> topic_index;

  // Build indices after extraction
  void build_indices();

  // Export formats
  std::string to_json(bool pretty = true) const;
  std::string to_markdown() const;
  std::string to_html() const;

  // Query interface
  std::vector<function_context *> find_by_category(const std::string &category);
  std::vector<function_context *> find_by_topic(const std::string &topic);
  std::vector<function_context *>
  find_by_intent_substring(const std::string &query);
};

// Main context extractor class
class context_extractor {
public:
  context_extractor() = default;

  // Extract from single file
  void extract_from_file(const std::string &filename);

  // Extract from multiple files
  void extract_from_files(const std::vector<std::string> &filenames);

  // Extract from directory (recursive)
  void extract_from_directory(const std::string &dir_path,
                              const std::string &pattern = "*.ny");

  // Get extracted context
  const extracted_context &get_context() const { return context_; }
  extracted_context &get_context() { return context_; }

  // Set codebase metadata
  void set_codebase_name(const std::string &name) {
    context_.codebase_name = name;
  }
  void set_version(const std::string &version) {
    context_.version = version;
  }

  // Export extracted context
  std::string to_json(bool pretty = true) const {
    return context_.to_json(pretty);
  }
  std::string to_markdown() const { return context_.to_markdown(); }
  std::string to_html() const { return context_.to_html(); }

  // Save to file
  void save_json(const std::string &output_path, bool pretty = true) const;
  void save_markdown(const std::string &output_path) const;
  void save_html(const std::string &output_path) const;

  // Error handling
  bool has_errors() const { return !errors_.empty(); }
  const std::vector<std::string> &get_errors() const { return errors_; }

private:
  // Extract from parsed AST
  void extract_from_ast(const std::shared_ptr<ast_program> &program,
                        const std::string &filename);

  // Visit declarations
  void visit_function(const std::shared_ptr<ast_function_decl> &func,
                      const std::string &filename,
                      const std::string &module_prefix);
  void visit_struct(const std::shared_ptr<ast_struct_decl> &struc,
                    const std::string &filename,
                    const std::string &module_prefix);
  void visit_class(const std::shared_ptr<ast_class_decl> &cls,
                   const std::string &filename,
                   const std::string &module_prefix);
  void visit_module(const std::shared_ptr<ast_module_decl> &mod,
                    const std::string &filename,
                    const std::string &parent_prefix);

  // Parse annotation arguments (e.g., "@doc(summary='...')")
  doc_context parse_doc_annotation(const ast_annotation &anno);
  ai_context parse_ai_context_annotation(const ast_annotation &anno);
  category_context parse_category_annotation(const ast_annotation &anno);
  performance_context parse_performance_annotation(const ast_annotation &anno);

  // Helper: parse key-value pairs from annotation args
  std::map<std::string, std::string>
  parse_annotation_args(const std::string &args);

  // Helper: parse array from annotation (e.g., "['a', 'b', 'c']")
  std::vector<std::string> parse_array_arg(const std::string &array_str);

  // Helper: parse map from annotation (e.g., "{'k1': 'v1', 'k2': 'v2'}")
  std::map<std::string, std::string> parse_map_arg(const std::string &map_str);

  // Helper: extract string value (handles quotes)
  std::string extract_string_value(const std::string &str);

  // Helper: build function signature string
  std::string build_signature(const std::shared_ptr<ast_function_decl> &func);

  // State
  extracted_context context_;
  std::string current_module_prefix_;
  std::vector<std::string> errors_;
};

// Utility: Generate timestamp in ISO 8601 format
std::string generate_timestamp();

// Utility: Escape string for JSON
std::string json_escape(const std::string &str);

// Utility: Format JSON with indentation
std::string format_json_indent(int level);

} // namespace ny

#endif // NY_COMPILER_CONTEXT_EXTRACTOR_HPP

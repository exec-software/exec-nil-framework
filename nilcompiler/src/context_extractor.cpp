// context_extractor.cpp
// AI Context Extraction Tool Implementation

#include "context_extractor.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <regex>

namespace ny {

namespace fs = std::filesystem;

// =============================================================================
// Utility Functions
// =============================================================================

std::string generate_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

std::string json_escape(const std::string &str) {
  std::string result;
  result.reserve(str.size() + 10);
  for (char c : str) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (c < 32) {
        std::stringstream ss;
        ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
           << static_cast<int>(c);
        result += ss.str();
      } else {
        result += c;
      }
    }
  }
  return result;
}

std::string format_json_indent(int level) {
  return std::string(level * 2, ' ');
}

// =============================================================================
// Context to JSON Conversion
// =============================================================================

std::string function_context::to_json(int indent) const {
  std::stringstream ss;
  std::string ind = format_json_indent(indent);
  std::string ind1 = format_json_indent(indent + 1);

  ss << ind << "{\n";
  ss << ind1 << "\"name\": \"" << json_escape(name) << "\",\n";
  ss << ind1 << "\"qualified_name\": \"" << json_escape(qualified_name)
     << "\",\n";
  ss << ind1 << "\"signature\": \"" << json_escape(signature) << "\",\n";
  ss << ind1 << "\"location\": {\n";
  ss << ind1 << "  \"file\": \"" << json_escape(location.filename) << "\",\n";
  ss << ind1 << "  \"line\": " << location.line << "\n";
  ss << ind1 << "},\n";
  ss << ind1 << "\"is_public\": " << (is_public ? "true" : "false") << ",\n";

  // @doc context
  if (!doc.empty()) {
    ss << ind1 << "\"doc\": {\n";
    if (!doc.summary.empty())
      ss << ind1 << "  \"summary\": \"" << json_escape(doc.summary) << "\",\n";
    if (!doc.description.empty())
      ss << ind1 << "  \"description\": \"" << json_escape(doc.description)
         << "\",\n";
    if (!doc.params.empty()) {
      ss << ind1 << "  \"params\": {\n";
      bool first = true;
      for (const auto &[key, val] : doc.params) {
        if (!first)
          ss << ",\n";
        ss << ind1 << "    \"" << json_escape(key) << "\": \""
           << json_escape(val) << "\"";
        first = false;
      }
      ss << "\n" << ind1 << "  },\n";
    }
    if (!doc.returns.empty())
      ss << ind1 << "  \"returns\": \"" << json_escape(doc.returns) << "\",\n";
    if (!doc.complexity.empty())
      ss << ind1 << "  \"complexity\": \"" << json_escape(doc.complexity)
         << "\",\n";
    if (!doc.examples.empty()) {
      ss << ind1 << "  \"examples\": [";
      for (size_t i = 0; i < doc.examples.size(); i++) {
        if (i > 0)
          ss << ", ";
        ss << "\"" << json_escape(doc.examples[i]) << "\"";
      }
      ss << "]\n";
    }
    ss << ind1 << "},\n";
  }

  // @ai_context
  if (!ai.empty()) {
    ss << ind1 << "\"ai_context\": {\n";
    if (!ai.intent.empty())
      ss << ind1 << "  \"intent\": \"" << json_escape(ai.intent) << "\",\n";
    if (!ai.assumptions.empty()) {
      ss << ind1 << "  \"assumptions\": [";
      for (size_t i = 0; i < ai.assumptions.size(); i++) {
        if (i > 0)
          ss << ", ";
        ss << "\"" << json_escape(ai.assumptions[i]) << "\"";
      }
      ss << "],\n";
    }
    if (!ai.constraints.empty()) {
      ss << ind1 << "  \"constraints\": [";
      for (size_t i = 0; i < ai.constraints.size(); i++) {
        if (i > 0)
          ss << ", ";
        ss << "\"" << json_escape(ai.constraints[i]) << "\"";
      }
      ss << "],\n";
    }
    if (!ai.trade_offs.empty()) {
      ss << ind1 << "  \"trade_offs\": {\n";
      bool first = true;
      for (const auto &[key, val] : ai.trade_offs) {
        if (!first)
          ss << ",\n";
        ss << ind1 << "    \"" << json_escape(key) << "\": \""
           << json_escape(val) << "\"";
        first = false;
      }
      ss << "\n" << ind1 << "  },\n";
    }
    if (!ai.related_topics.empty()) {
      ss << ind1 << "  \"related_topics\": [";
      for (size_t i = 0; i < ai.related_topics.size(); i++) {
        if (i > 0)
          ss << ", ";
        ss << "\"" << json_escape(ai.related_topics[i]) << "\"";
      }
      ss << "],\n";
    }
    if (!ai.typical_usage.empty())
      ss << ind1 << "  \"typical_usage\": \"" << json_escape(ai.typical_usage)
         << "\",\n";
    if (!ai.anti_patterns.empty()) {
      ss << ind1 << "  \"anti_patterns\": [";
      for (size_t i = 0; i < ai.anti_patterns.size(); i++) {
        if (i > 0)
          ss << ", ";
        ss << "\"" << json_escape(ai.anti_patterns[i]) << "\"";
      }
      ss << "]\n";
    }
    ss << ind1 << "},\n";
  }

  // @category
  if (!category.empty()) {
    ss << ind1 << "\"category\": \"" << json_escape(category.path) << "\",\n";
  }

  // @performance
  if (!performance.empty()) {
    ss << ind1 << "\"performance\": {\n";
    if (!performance.time_complexity.empty())
      ss << ind1 << "  \"time_complexity\": \""
         << json_escape(performance.time_complexity) << "\",\n";
    if (!performance.space_complexity.empty())
      ss << ind1 << "  \"space_complexity\": \""
         << json_escape(performance.space_complexity) << "\",\n";
    ss << ind1 << "  \"parallelizable\": "
       << (performance.parallelizable ? "true" : "false") << "\n";
    ss << ind1 << "}\n";
  } else {
    // Remove trailing comma
    std::string content = ss.str();
    if (content.back() == '\n' && content[content.size() - 2] == ',') {
      ss.str(content.substr(0, content.size() - 2) + "\n");
    }
  }

  ss << ind << "}";
  return ss.str();
}

std::string struct_context::to_json(int indent) const {
  std::stringstream ss;
  std::string ind = format_json_indent(indent);
  std::string ind1 = format_json_indent(indent + 1);

  ss << ind << "{\n";
  ss << ind1 << "\"name\": \"" << json_escape(name) << "\",\n";
  ss << ind1 << "\"qualified_name\": \"" << json_escape(qualified_name)
     << "\",\n";
  ss << ind1 << "\"location\": {\n";
  ss << ind1 << "  \"file\": \"" << json_escape(location.filename) << "\",\n";
  ss << ind1 << "  \"line\": " << location.line << "\n";
  ss << ind1 << "},\n";

  if (!doc.empty()) {
    ss << ind1 << "\"doc\": {\n";
    if (!doc.summary.empty())
      ss << ind1 << "  \"summary\": \"" << json_escape(doc.summary) << "\"\n";
    ss << ind1 << "}\n";
  }

  ss << ind << "}";
  return ss.str();
}

std::string extracted_context::to_json(bool pretty) const {
  std::stringstream ss;
  int indent = pretty ? 1 : 0;

  ss << "{\n";
  ss << format_json_indent(indent) << "\"codebase\": \"" << json_escape(codebase_name) << "\",\n";
  ss << format_json_indent(indent) << "\"version\": \"" << json_escape(version) << "\",\n";
  ss << format_json_indent(indent) << "\"timestamp\": \"" << json_escape(timestamp) << "\",\n";

  // Functions
  ss << format_json_indent(indent) << "\"functions\": [\n";
  for (size_t i = 0; i < functions.size(); i++) {
    if (i > 0)
      ss << ",\n";
    ss << functions[i].to_json(indent + 1);
  }
  ss << "\n" << format_json_indent(indent) << "],\n";

  // Structs
  ss << format_json_indent(indent) << "\"structs\": [\n";
  for (size_t i = 0; i < structs.size(); i++) {
    if (i > 0)
      ss << ",\n";
    ss << structs[i].to_json(indent + 1);
  }
  ss << "\n" << format_json_indent(indent) << "]\n";

  ss << "}\n";
  return ss.str();
}

std::string extracted_context::to_markdown() const {
  std::stringstream ss;

  ss << "# " << codebase_name << " - API Documentation\n\n";
  ss << "**Version**: " << version << "\n";
  ss << "**Generated**: " << timestamp << "\n\n";

  ss << "---\n\n";
  ss << "## Functions\n\n";

  for (const auto &func : functions) {
    ss << "### " << func.name << "\n\n";

    if (!func.doc.summary.empty()) {
      ss << func.doc.summary << "\n\n";
    }

    ss << "```ny\n" << func.signature << "\n```\n\n";

    if (!func.ai.intent.empty()) {
      ss << "> **Intent**: " << func.ai.intent << "\n\n";
    }

    if (!func.doc.description.empty()) {
      ss << func.doc.description << "\n\n";
    }

    if (!func.doc.params.empty()) {
      ss << "**Parameters**:\n";
      for (const auto &[param, desc] : func.doc.params) {
        ss << "- `" << param << "` - " << desc << "\n";
      }
      ss << "\n";
    }

    if (!func.doc.returns.empty()) {
      ss << "**Returns**: " << func.doc.returns << "\n\n";
    }

    if (!func.doc.complexity.empty()) {
      ss << "**Complexity**: " << func.doc.complexity << "\n\n";
    }

    if (!func.ai.assumptions.empty()) {
      ss << "**Assumptions**:\n";
      for (const auto &assumption : func.ai.assumptions) {
        ss << "- " << assumption << "\n";
      }
      ss << "\n";
    }

    if (!func.ai.trade_offs.empty()) {
      ss << "**Trade-offs**:\n\n";
      ss << "| Aspect | Explanation |\n";
      ss << "|--------|-------------|\n";
      for (const auto &[aspect, expl] : func.ai.trade_offs) {
        ss << "| " << aspect << " | " << expl << " |\n";
      }
      ss << "\n";
    }

    if (!func.ai.related_topics.empty()) {
      ss << "**Related Topics**: ";
      for (size_t i = 0; i < func.ai.related_topics.size(); i++) {
        if (i > 0)
          ss << ", ";
        ss << "`" << func.ai.related_topics[i] << "`";
      }
      ss << "\n\n";
    }

    if (!func.category.path.empty()) {
      ss << "**Category**: " << func.category.path << "\n\n";
    }

    ss << "---\n\n";
  }

  return ss.str();
}

std::string extracted_context::to_html() const {
  std::stringstream ss;

  ss << "<!DOCTYPE html>\n<html>\n<head>\n";
  ss << "<meta charset=\"UTF-8\">\n";
  ss << "<title>" << codebase_name << " - API Documentation</title>\n";
  ss << "<style>\n";
  ss << "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', "
        "Arial, sans-serif; line-height: 1.6; max-width: 1200px; margin: 0 "
        "auto; padding: 20px; }\n";
  ss << "h1, h2, h3 { color: #333; }\n";
  ss << ".function { border: 1px solid #ddd; border-radius: 8px; padding: "
        "20px; margin: 20px 0; }\n";
  ss << ".intent { background: #e3f2fd; padding: 10px; border-radius: 4px; "
        "margin: 10px 0; }\n";
  ss << ".signature { background: #f5f5f5; padding: 10px; border-radius: "
        "4px; font-family: monospace; }\n";
  ss << "table { border-collapse: collapse; width: 100%; margin: 10px 0; }\n";
  ss << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
  ss << "th { background: #f0f0f0; }\n";
  ss << "</style>\n</head>\n<body>\n";

  ss << "<h1>" << codebase_name << " - API Documentation</h1>\n";
  ss << "<p><strong>Version</strong>: " << version << "</p>\n";
  ss << "<p><strong>Generated</strong>: " << timestamp << "</p>\n";

  ss << "<hr>\n<h2>Functions</h2>\n";

  for (const auto &func : functions) {
    ss << "<div class=\"function\" id=\"" << func.name << "\">\n";
    ss << "<h3>" << func.name << "</h3>\n";

    if (!func.doc.summary.empty()) {
      ss << "<p>" << func.doc.summary << "</p>\n";
    }

    if (!func.ai.intent.empty()) {
      ss << "<div class=\"intent\"><strong>Intent</strong>: " << func.ai.intent
         << "</div>\n";
    }

    ss << "<div class=\"signature\">" << func.signature << "</div>\n";

    if (!func.doc.description.empty()) {
      ss << "<p>" << func.doc.description << "</p>\n";
    }

    if (!func.ai.trade_offs.empty()) {
      ss << "<h4>Trade-offs</h4>\n<table>\n";
      ss << "<tr><th>Aspect</th><th>Explanation</th></tr>\n";
      for (const auto &[aspect, expl] : func.ai.trade_offs) {
        ss << "<tr><td>" << aspect << "</td><td>" << expl << "</td></tr>\n";
      }
      ss << "</table>\n";
    }

    ss << "</div>\n";
  }

  ss << "</body>\n</html>\n";
  return ss.str();
}

void extracted_context::build_indices() {
  category_index.clear();
  topic_index.clear();

  for (auto &func : functions) {
    if (!func.category.path.empty()) {
      category_index[func.category.path].push_back(&func);
    }
    for (const auto &topic : func.ai.related_topics) {
      topic_index[topic].push_back(&func);
    }
  }
}

std::vector<function_context *>
extracted_context::find_by_category(const std::string &category) {
  auto it = category_index.find(category);
  if (it != category_index.end()) {
    return it->second;
  }
  return {};
}

std::vector<function_context *>
extracted_context::find_by_topic(const std::string &topic) {
  auto it = topic_index.find(topic);
  if (it != topic_index.end()) {
    return it->second;
  }
  return {};
}

std::vector<function_context *>
extracted_context::find_by_intent_substring(const std::string &query) {
  std::vector<function_context *> results;
  std::string lower_query = query;
  std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(),
                 ::tolower);

  for (auto &func : functions) {
    std::string lower_intent = func.ai.intent;
    std::transform(lower_intent.begin(), lower_intent.end(),
                   lower_intent.begin(), ::tolower);
    if (lower_intent.find(lower_query) != std::string::npos) {
      results.push_back(&func);
    }
  }
  return results;
}

// =============================================================================
// Context Extractor Implementation
// =============================================================================

void context_extractor::extract_from_file(const std::string &filename) {
  try {
    std::ifstream file(filename);
    if (!file.is_open()) {
      errors_.push_back("Cannot open file: " + filename);
      return;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    parser_options opts;
    opts.filename = filename;
    parser p(source, opts);

    auto program = p.parse_program();
    if (p.has_errors()) {
      for (const auto &diag : p.diagnostics()) {
        errors_.push_back(filename + ":" + std::to_string(diag.line) + ": " +
                          diag.message);
      }
      return;
    }

    extract_from_ast(program, filename);
  } catch (const std::exception &e) {
    errors_.push_back("Error parsing " + filename + ": " + e.what());
  }
}

void context_extractor::extract_from_files(
    const std::vector<std::string> &filenames) {
  for (const auto &filename : filenames) {
    extract_from_file(filename);
  }
  context_.timestamp = generate_timestamp();
  context_.build_indices();
}

void context_extractor::extract_from_directory(const std::string &dir_path,
                                               const std::string &pattern) {
  std::vector<std::string> files;
  const std::string requested_extension = fs::path(pattern).extension().string();

  try {
    for (const auto &entry : fs::recursive_directory_iterator(dir_path)) {
      if (entry.is_regular_file()) {
        if (requested_extension.empty() ||
            entry.path().extension() == requested_extension) {
          files.push_back(entry.path().string());
        }
      }
    }
  } catch (const std::exception &e) {
    errors_.push_back("Error scanning directory: " + std::string(e.what()));
    return;
  }

  extract_from_files(files);
}

void context_extractor::extract_from_ast(
    const std::shared_ptr<ast_program> &program, const std::string &filename) {
  if (!program)
    return;

  current_module_prefix_.clear();
  for (const auto &decl : program->declarations) {
    if (!decl)
      continue;

    if (auto func = std::dynamic_pointer_cast<ast_function_decl>(decl)) {
      visit_function(func, filename, current_module_prefix_);
    } else if (auto struc = std::dynamic_pointer_cast<ast_struct_decl>(decl)) {
      visit_struct(struc, filename, current_module_prefix_);
    } else if (auto cls = std::dynamic_pointer_cast<ast_class_decl>(decl)) {
      visit_class(cls, filename, current_module_prefix_);
    } else if (auto mod = std::dynamic_pointer_cast<ast_module_decl>(decl)) {
      visit_module(mod, filename, current_module_prefix_);
    }
  }
}

void context_extractor::visit_function(
    const std::shared_ptr<ast_function_decl> &func,
    const std::string &filename, const std::string &module_prefix) {
  function_context ctx;
  ctx.name = func->name;
  ctx.qualified_name = module_prefix.empty() ? func->name
                                              : module_prefix + "::" + func->name;
  ctx.signature = build_signature(func);
  ctx.location = source_location(filename, func->line);
  ctx.is_public = (func->visibility == visibility_kind::visibility_public);

  // Extract annotations
  for (const auto &anno : func->get_annotations()) {
    if (anno.name == "doc") {
      ctx.doc = parse_doc_annotation(anno);
    } else if (anno.name == "ai_context") {
      ctx.ai = parse_ai_context_annotation(anno);
    } else if (anno.name == "category") {
      ctx.category = parse_category_annotation(anno);
    } else if (anno.name == "performance") {
      ctx.performance = parse_performance_annotation(anno);
    } else {
      ctx.other_annotations.push_back(anno.name);
    }
  }

  context_.functions.push_back(ctx);
}

void context_extractor::visit_struct(
    const std::shared_ptr<ast_struct_decl> &struc,
    const std::string &filename, const std::string &module_prefix) {
  struct_context ctx;
  ctx.name = struc->name;
  ctx.qualified_name = module_prefix.empty() ? struc->name
                                              : module_prefix + "::" + struc->name;
  ctx.location = source_location(filename, struc->line);
  ctx.is_public = (struc->visibility == visibility_kind::visibility_public);

  for (const auto &anno : struc->get_annotations()) {
    if (anno.name == "doc") {
      ctx.doc = parse_doc_annotation(anno);
    } else if (anno.name == "ai_context") {
      ctx.ai = parse_ai_context_annotation(anno);
    } else if (anno.name == "category") {
      ctx.category = parse_category_annotation(anno);
    }
  }

  context_.structs.push_back(ctx);
}

void context_extractor::visit_class(
    const std::shared_ptr<ast_class_decl> &cls, const std::string &filename,
    const std::string &module_prefix) {
  // Treat class similar to struct
  struct_context ctx;
  ctx.name = cls->name;
  ctx.qualified_name = module_prefix.empty() ? cls->name
                                              : module_prefix + "::" + cls->name;
  ctx.location = source_location(filename, cls->line);
  ctx.is_public = (cls->visibility == visibility_kind::visibility_public);

  for (const auto &anno : cls->get_annotations()) {
    if (anno.name == "doc") {
      ctx.doc = parse_doc_annotation(anno);
    } else if (anno.name == "ai_context") {
      ctx.ai = parse_ai_context_annotation(anno);
    }
  }

  context_.structs.push_back(ctx);
}

void context_extractor::visit_module(
    const std::shared_ptr<ast_module_decl> &mod, const std::string &filename,
    const std::string &parent_prefix) {
  std::string new_prefix =
      parent_prefix.empty() ? mod->name : parent_prefix + "::" + mod->name;
  current_module_prefix_ = new_prefix;

  module_context ctx;
  ctx.name = mod->name;
  ctx.qualified_name = new_prefix;
  ctx.location = source_location(filename, mod->line);
  for (const auto &anno : mod->get_annotations()) {
    if (anno.name == "doc") {
      ctx.doc = parse_doc_annotation(anno);
    } else if (anno.name == "ai_context") {
      ctx.ai = parse_ai_context_annotation(anno);
    }
  }
  context_.modules.push_back(std::move(ctx));
}

std::string context_extractor::extract_string_value(const std::string &str) {
  std::string result = str;
  // Remove quotes if present
  if (result.size() >= 2 &&
      ((result.front() == '"' && result.back() == '"') ||
       (result.front() == '\'' && result.back() == '\''))) {
    result = result.substr(1, result.size() - 2);
  }
  return result;
}

std::map<std::string, std::string>
context_extractor::parse_annotation_args(const std::string &args) {
  std::map<std::string, std::string> result;

  // Simple parser for key=value pairs
  std::regex pair_regex(R"((\w+)\s*=\s*([^,]+))");
  std::sregex_iterator iter(args.begin(), args.end(), pair_regex);
  std::sregex_iterator end;

  while (iter != end) {
    std::string key = (*iter)[1].str();
    std::string value = (*iter)[2].str();

    // Trim whitespace
    key.erase(0, key.find_first_not_of(" \t\n\r"));
    key.erase(key.find_last_not_of(" \t\n\r") + 1);
    value.erase(0, value.find_first_not_of(" \t\n\r"));
    value.erase(value.find_last_not_of(" \t\n\r") + 1);

    result[key] = extract_string_value(value);
    ++iter;
  }

  return result;
}

std::vector<std::string>
context_extractor::parse_array_arg(const std::string &array_str) {
  std::vector<std::string> result;

  std::string content = array_str;
  // Remove [ ] if present
  if (content.front() == '[' && content.back() == ']') {
    content = content.substr(1, content.size() - 2);
  }

  std::regex item_regex(R"regex("([^"]*?)"|'([^']*?)')regex");
  std::sregex_iterator iter(content.begin(), content.end(), item_regex);
  std::sregex_iterator end;

  while (iter != end) {
    std::string item = (*iter)[1].matched ? (*iter)[1].str() : (*iter)[2].str();
    result.push_back(item);
    ++iter;
  }

  return result;
}

std::map<std::string, std::string>
context_extractor::parse_map_arg(const std::string &map_str) {
  std::map<std::string, std::string> result;

  std::string content = map_str;
  // Remove { } if present
  if (content.front() == '{' && content.back() == '}') {
    content = content.substr(1, content.size() - 2);
  }

  std::regex pair_regex(R"(["']([^"']+)["']\s*:\s*["']([^"']+)["'])");
  std::sregex_iterator iter(content.begin(), content.end(), pair_regex);
  std::sregex_iterator end;

  while (iter != end) {
    std::string key = (*iter)[1].str();
    std::string value = (*iter)[2].str();
    result[key] = value;
    ++iter;
  }

  return result;
}

doc_context
context_extractor::parse_doc_annotation(const ast_annotation &anno) {
  doc_context ctx;

  if (!anno.args.has_value()) {
    return ctx;
  }

  auto args = parse_annotation_args(anno.args.value());

  if (args.count("summary")) {
    ctx.summary = args["summary"];
  }
  if (args.count("description")) {
    ctx.description = args["description"];
  }
  if (args.count("returns")) {
    ctx.returns = args["returns"];
  }
  if (args.count("complexity")) {
    ctx.complexity = args["complexity"];
  }
  if (args.count("author")) {
    ctx.author = args["author"];
  }

  return ctx;
}

ai_context
context_extractor::parse_ai_context_annotation(const ast_annotation &anno) {
  ai_context ctx;

  if (!anno.args.has_value()) {
    return ctx;
  }

  auto args = parse_annotation_args(anno.args.value());

  if (args.count("intent")) {
    ctx.intent = args["intent"];
  }
  if (args.count("typical_usage")) {
    ctx.typical_usage = args["typical_usage"];
  }
  if (args.count("assumptions")) {
    ctx.assumptions = parse_array_arg(args["assumptions"]);
  }
  if (args.count("constraints")) {
    ctx.constraints = parse_array_arg(args["constraints"]);
  }
  if (args.count("related_topics")) {
    ctx.related_topics = parse_array_arg(args["related_topics"]);
  }
  if (args.count("anti_patterns")) {
    ctx.anti_patterns = parse_array_arg(args["anti_patterns"]);
  }
  if (args.count("trade_offs")) {
    ctx.trade_offs = parse_map_arg(args["trade_offs"]);
  }

  return ctx;
}

category_context
context_extractor::parse_category_annotation(const ast_annotation &anno) {
  category_context ctx;

  if (anno.args.has_value()) {
    ctx.path = extract_string_value(anno.args.value());
  }

  return ctx;
}

performance_context
context_extractor::parse_performance_annotation(const ast_annotation &anno) {
  performance_context ctx;

  if (!anno.args.has_value()) {
    return ctx;
  }

  auto args = parse_annotation_args(anno.args.value());

  if (args.count("time_complexity")) {
    ctx.time_complexity = args["time_complexity"];
  }
  if (args.count("space_complexity")) {
    ctx.space_complexity = args["space_complexity"];
  }
  if (args.count("parallelizable")) {
    ctx.parallelizable = (args["parallelizable"] == "true");
  }

  return ctx;
}

std::string context_extractor::build_signature(
    const std::shared_ptr<ast_function_decl> &func) {
  std::stringstream ss;

  ss << "function " << func->name << "(";

  for (size_t i = 0; i < func->params.size(); i++) {
    if (i > 0)
      ss << ", ";
    ss << func->params[i].name;
    if (func->params[i].type_name) {
      ss << ": " << *func->params[i].type_name;
    }
  }

  ss << ")";

  if (func->return_type) {
    ss << " -> " << *func->return_type;
  }

  return ss.str();
}

void context_extractor::save_json(const std::string &output_path,
                                  bool pretty) const {
  std::ofstream file(output_path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open output file: " + output_path);
  }
  file << to_json(pretty);
}

void context_extractor::save_markdown(const std::string &output_path) const {
  std::ofstream file(output_path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open output file: " + output_path);
  }
  file << to_markdown();
}

void context_extractor::save_html(const std::string &output_path) const {
  std::ofstream file(output_path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open output file: " + output_path);
  }
  file << to_html();
}

} // namespace ny

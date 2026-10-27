#include "matrix_submission.hpp"
#include "matrix_parser.hpp"
#include "submission_request.hpp"
#include "submission_profile.hpp"
#include "codegen.hpp"
#include "secure_file_io.hpp"
#include "type_checker.hpp"
#include "../../common/strict_json.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace matrix {
namespace {

using nil::detail::json;

std::string diagnostic_text(const std::string &text) {
  try {
    nil::detail::valid_utf8(text);
    return text;
  } catch (const std::exception &) {
    // A filesystem path may contain arbitrary bytes. Keep the protocol valid
    // UTF-8 even when the input cannot be opened or decoded.
    constexpr char hex[] = "0123456789abcdef";
    std::string escaped;
    for (unsigned char byte : text) {
      if (byte < 0x80) escaped.push_back(static_cast<char>(byte));
      else {
        escaped += "\\x";
        escaped.push_back(hex[byte >> 4]);
        escaped.push_back(hex[byte & 15]);
      }
    }
    return escaped;
  }
}

void issue(submission_report &report, const std::string &stage,
           const std::string &message, const std::string &location,
           int line = 0, const std::string &code = "MS0001") {
  // Keep failure reports bounded even for large identifier/literal diagnostics.
  const auto safe = diagnostic_text(message);
  std::string bounded = safe;
  if (bounded.size() > 4096) {
    bounded.resize(4096);
    while (!bounded.empty() &&
           (static_cast<unsigned char>(safe[bounded.size()]) & 0xc0) == 0x80)
      bounded.pop_back();
    bounded += "...";
  }
  if (report.diagnostics.size() < 64)
    report.diagnostics.push_back({stage, code, bounded, diagnostic_text(location), line, 0, 0});
}

void validate_input(const std::string &source) {
  if (source.size() > submission_max_input_bytes)
    throw std::runtime_error("submission input exceeds 65536 bytes");
  nil::detail::valid_utf8(source);
  if (source.find('\0') != std::string::npos)
    throw std::runtime_error("submission input must not contain NUL");
}

void check_tree(submission_report &report,
                const std::shared_ptr<ny::ast_block_stmt> &body,
                const ny::ast_expr_ptr &value, const std::string &filename) {
  if (!body) throw std::runtime_error("submission has no statement body");
  for (const auto &diagnostic : ny::validate_submission_ast(*body, value))
    issue(report, "profile", diagnostic.message,
          diagnostic.source_file.empty() ? filename : diagnostic.source_file, diagnostic.line,
          "MS1001");
  if (!report.diagnostics.empty()) return;

  // Infer the expression in its actual lexical context through the ordinary
  // shared checker. An expression statement is legal in a void paragraph.
  // After inference it becomes the synthetic return and is checked again.
  auto block = std::make_shared<ny::ast_block_stmt>(*body);
  if (value)
    block->statements.push_back(
        std::make_shared<ny::ast_expr_stmt>(value, value->line));
  auto function = std::make_shared<ny::ast_function_decl>(
      "__nil_submission", std::vector<ny::ast_param>{}, "void", block);
  ny::ast_program program;
  program.filename = filename;
  program.declarations.push_back(function);
  const auto check = [&]() {
    ny::type_checker checker;
    if (checker.check(program)) return true;
    for (const auto &diagnostic : checker.get_diagnostics())
      issue(report, "type", diagnostic.message,
            diagnostic.source_file.empty() ? filename : diagnostic.source_file,
            diagnostic.line, "MS2001");
    if (report.diagnostics.empty())
      issue(report, "type", "submission failed type checking", filename,
            0, "MS2001");
    return false;
  };
  if (!check()) return;
  const std::string result_type = value ? value->inferred_type.value_or("") : "void";
  if (result_type.empty()) {
    issue(report, "type", "submission result has no concrete type", filename,
          value ? value->line : 0, "MS2001");
    return;
  }
  if (value) {
    function->return_type = result_type;
    block->statements.back() =
        std::make_shared<ny::ast_return_stmt>(value, value->line);
    if (!check()) return;
  }

  ny::codegen_options options;
  options.include_debug_info = false;
  options.include_source_debug = false;
  options.mode = ny::build_mode::release_mode;
  ny::codegen generator(options);
  std::vector<ny::codegen_diagnostic> diagnostics;
  const auto module = generator.generate_module(program, &diagnostics);
  for (const auto &diagnostic : diagnostics)
    issue(report, "lowering", diagnostic.message, filename, diagnostic.line,
          "MS3001");
  if (!report.diagnostics.empty()) return;
  if (!module) throw std::runtime_error("submission lowering produced no module");
  try {
    ny::validate_nil_submission_profile(*module);
  } catch (const std::exception &error) {
    issue(report, "nil-profile", error.what(), filename, 0, "MS1002");
    return;
  }
  std::ostringstream output;
  ny::nil_writer writer(output, options);
  writer.emit_text_module(*module);
  auto text = output.str();
  if (text.size() > 1024 * 1024)
    throw std::runtime_error("submission NIL output exceeds 1048576 bytes");
  for (const auto &compiled : module->functions)
    report.instruction_count += compiled.body.size();
  report.result_type = module->functions.front().return_type;
  report.nil_text = std::move(text);
  report.status = "complete";
}

} // namespace

submission_report check_submission_source(const std::string &source,
                                          const std::string &filename) {
  submission_report report;
  report.input_kind = "matrix";
  try {
    validate_input(source);
    parser_options options;
    options.filename = filename;
    options.submission = true;
    parser frontend(source, options);
    const auto parsed = frontend.parse_submission();
    if (parsed.state != submission_state::complete) {
      report.status = parsed.state == submission_state::incomplete
                          ? "incomplete" : "invalid";
      for (const auto &diagnostic : parsed.diagnostics) {
        const auto before = report.diagnostics.size();
        issue(report, "parse", diagnostic.message, filename,
              diagnostic.primary.line, diagnostic.code);
        if (report.diagnostics.size() != before) {
          report.diagnostics.back().column = diagnostic.primary.column;
          report.diagnostics.back().length = diagnostic.primary.length;
        }
      }
      return report;
    }
    check_tree(report, parsed.body, parsed.value, filename);
  } catch (const std::exception &error) {
    issue(report, "input", error.what(), filename);
  }
  return report;
}

submission_report check_submission_request(const std::string &source,
                                           const std::string &filename) {
  submission_report report;
  report.input_kind = "nildata-request";
  try {
    validate_input(source);
    const auto decoded = decode_submission_request(source);
    check_tree(report, decoded.body, decoded.value, filename);
  } catch (const submission_request_error &error) {
    issue(report, "request", error.what(), error.path, 0, "MS4001");
  } catch (const std::exception &error) {
    issue(report, "request", error.what(), filename, 0, "MS4001");
  }
  return report;
}

std::string submission_report_json(const submission_report &report) {
  auto out = json::map();
  out.object["schema"] = json::str("nil.matrix.submission-check.v1");
  out.object["profile"] = json::str("matrix.pure.v1");
  out.object["input_kind"] = json::str(report.input_kind);
  out.object["status"] = json::str(report.status);
  out.object["executed"] = json::flag(false);
  out.object["result_type"] = report.status == "complete"
                                  ? json::str(report.result_type) : json{};
  out.object["nil"] = report.status == "complete"
                          ? json::str(report.nil_text) : json{};
  json count;
  count.type = json::kind::number;
  count.text = std::to_string(report.instruction_count);
  out.object["instruction_count"] = std::move(count);
  auto diagnostics = json::list();
  for (const auto &diagnostic : report.diagnostics) {
    auto item = json::map();
    item.object["stage"] = json::str(diagnostic.stage);
    item.object["code"] = json::str(diagnostic.code);
    item.object["message"] = json::str(diagnostic.message);
    item.object["location"] = json::str(diagnostic.location);
    for (const auto &entry : {std::make_pair("line", diagnostic.line),
                             std::make_pair("column", diagnostic.column),
                             std::make_pair("length", diagnostic.length)}) {
      json number;
      number.type = json::kind::number;
      number.text = std::to_string(entry.second);
      item.object[entry.first] = std::move(number);
    }
    diagnostics.array.push_back(std::move(item));
  }
  out.object["diagnostics"] = std::move(diagnostics);
  return nil::detail::dump(out);
}

int run_submission_check_cli(bool request, const std::string &path) {
  submission_report report;
  report.input_kind = request ? "nildata-request" : "matrix";
  try {
    std::string source;
    if (path == "-") {
#ifdef _WIN32
      if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        throw std::runtime_error("cannot enable binary submission stdin");
#endif
      char byte;
      while (std::cin.get(byte)) {
        if (source.size() == submission_max_input_bytes)
          throw std::runtime_error("submission input exceeds 65536 bytes");
        source.push_back(byte);
      }
      if (!std::cin.eof()) throw std::runtime_error("cannot read submission input");
    } else {
      const auto input = ny::detail::read_regular_file_bounded(
          path, submission_max_input_bytes);
      if (!input.ok) throw std::runtime_error(input.error);
      source.assign(input.bytes.begin(), input.bytes.end());
    }
    report = request ? check_submission_request(source, path)
                     : check_submission_source(source, path);
  } catch (const std::exception &error) {
    issue(report, "input", error.what(), path);
  }
  std::cout << submission_report_json(report) << '\n';
  return report.status == "complete" ? 0 : report.status == "incomplete" ? 2 : 1;
}

} // namespace matrix

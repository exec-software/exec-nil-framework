#include "submission_session_cli.hpp"

#include "submission_session.hpp"
#include "matrix_lexer.hpp"
#include "secure_file_io.hpp"
#include "../../common/strict_json.hpp"

#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace matrix {
namespace {
using nil::detail::json;

void binary_stdin() {
#ifdef _WIN32
  if (_setmode(_fileno(stdin), _O_BINARY) == -1)
    throw std::runtime_error("cannot enable binary submission stdin");
#endif
}

bool terminal() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
  return isatty(fileno(stdin)) && isatty(fileno(stdout));
#endif
}

std::string safe_text(const std::string &value) {
  try {
    nil::detail::valid_utf8(value);
    return value;
  } catch (const std::exception &) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char byte : value) {
      if (byte < 0x80) out += static_cast<char>(byte);
      else {
        out += "\\x";
        out += hex[byte >> 4];
        out += hex[byte & 15];
      }
    }
    return out;
  }
}

evaluation_report input_error(const std::string &message, const std::string &location,
                              std::size_t binding_count, bool request = false,
                              bool incomplete = false) {
  evaluation_report report;
  report.check.input_kind = request ? "nildata-request" : "matrix";
  report.check.status = incomplete ? "incomplete" : "invalid";
  report.check.diagnostics.push_back({"input", "MS5001", safe_text(message),
                                      safe_text(location), 0, 0, 0});
  report.binding_count = binding_count;
  return report;
}

int exit_status(const evaluation_report &report) {
  return report.check.status == "complete" ? 0 : report.check.status == "incomplete" ? 2 : 1;
}

std::string terminal_quote(const std::string &text) {
  const auto quoted = nil::detail::quote(safe_text(text));
  std::string out;
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < quoted.size();) {
    const auto start = i;
    const auto first = static_cast<unsigned char>(quoted[i++]);
    unsigned point = first;
    unsigned continuation = 0;
    if (first >= 0xf0) { point &= 7; continuation = 3; }
    else if (first >= 0xe0) { point &= 15; continuation = 2; }
    else if (first >= 0xc0) { point &= 31; continuation = 1; }
    while (continuation--)
      point = (point << 6) | (static_cast<unsigned char>(quoted[i++]) & 63);
    // JSON already quotes C0 controls. Also escape terminal C1 controls and
    // invisible directional/separator characters that can disguise output.
    if ((point >= 0x7f && point <= 0x9f) || point == 0x61c ||
        (point >= 0x200b && point <= 0x200f) ||
        (point >= 0x2028 && point <= 0x202e) ||
        (point >= 0x2060 && point <= 0x206f) || point == 0xfeff) {
      out += "\\u";
      for (int shift = 12; shift >= 0; shift -= 4) out += hex[(point >> shift) & 15];
    } else out.append(quoted, start, i - start);
  }
  return out;
}

std::string terminal_text(const std::string &text) {
  const auto quoted = terminal_quote(text);
  return quoted.substr(1, quoted.size() - 2);
}

std::string display_value(const ny::submission_value &value) {
  const bool array = value.type.rfind("array<", 0) == 0;
  const bool tuple = value.type.rfind("tuple<", 0) == 0;
  if (!array && !tuple)
    return value.type == "text" ? terminal_quote(value.scalar) : terminal_text(value.scalar);
  std::string out = array ? "[" : "(";
  for (std::size_t i = 0; i < value.elements.size(); ++i) {
    if (i) out += ", ";
    out += display_value(value.elements[i]);
  }
  if (tuple && value.elements.size() == 1) out += ',';
  return out + (array ? "]" : ")");
}

void display_binding(const submission_binding &binding) {
  std::cout << terminal_text(binding.name) << " = " << display_value(binding.value)
            << " : " << terminal_text(binding.value.type) << '\n';
}

void print(const evaluation_report &report, bool interactive = false) {
  if (!interactive) {
    std::cout << evaluation_report_json(report) << '\n';
    return;
  }
  if (report.check.status == "complete") {
    if (report.value)
      std::cout << display_value(*report.value) << " : " << terminal_text(report.value->type) << '\n';
    for (const auto &binding : report.added) display_binding(binding);
    if (!report.value && report.added.empty()) std::cout << "ok\n";
    return;
  }
  for (const auto &diagnostic : report.check.diagnostics) {
    std::cout << terminal_text(report.check.status) << ": " << terminal_text(diagnostic.message);
    if (!diagnostic.location.empty()) {
      std::cout << " (" << terminal_text(diagnostic.location);
      if (diagnostic.line > 0) {
        std::cout << ':' << diagnostic.line;
        if (diagnostic.column > 0) std::cout << ':' << diagnostic.column;
      }
      std::cout << ')';
    }
    std::cout << '\n';
  }
  if (report.check.diagnostics.empty()) std::cout << terminal_text(report.check.status) << '\n';
}

std::string read_source(const std::string &path) {
  if (path != "-") {
    const auto input = ny::detail::read_regular_file_bounded(path, submission_max_input_bytes);
    if (!input.ok) throw std::runtime_error(input.error);
    return std::string(input.bytes.begin(), input.bytes.end());
  }
  binary_stdin();
  std::string source;
  char byte;
  while (std::cin.get(byte)) {
    if (source.size() == submission_max_input_bytes)
      throw std::runtime_error("submission input exceeds 65536 bytes");
    source += byte;
  }
  if (!std::cin.eof()) throw std::runtime_error("cannot read submission input");
  return source;
}

struct shell_line {
  std::string text;
  bool present = false, newline = false, oversized = false;
};

shell_line read_line() {
  shell_line result;
  char byte;
  while (std::cin.get(byte)) {
    result.present = true;
    if (byte == '\n') {
      result.newline = true;
      return result;
    }
    // Keep draining an oversized physical line without growing its buffer.
    if (result.text.size() == submission_max_input_bytes) result.oversized = true;
    else result.text += byte;
  }
  if (!std::cin.eof()) throw std::runtime_error("cannot read Matrix shell input");
  return result;
}

bool open_text_block(const std::string &source) {
  lexer scanner(source, 0, true);
  (void)scanner.scan();
  for (const auto &error : scanner.errors())
    if (error.open_text_block) return true;
  return false;
}

void print_bindings(const submission_session &session, bool interactive) {
  if (interactive) {
    if (session.bindings().empty()) std::cout << "No retained bindings.\n";
    for (const auto &binding : session.bindings()) display_binding(binding);
    return;
  }
  // Values are already validated, owned data. Compose their canonical JSON
  // directly: reparsing typed value objects would impose a second, lower
  // nesting limit because each value adds both an object and an elements array.
  std::string report = "{\"schema\":\"nil.matrix.session-bindings.v1\",\"bindings\":[";
  bool first = true;
  for (const auto &binding : session.bindings()) {
    if (!first) report += ',';
    first = false;
    report += "{\"name\":" + nil::detail::quote(binding.name) +
              ",\"value\":" + submission_value_json(binding.value) + "}";
  }
  report += "],\"binding_count\":" + std::to_string(session.bindings().size()) + "}";
  std::cout << report << '\n';
}

void help(bool interactive) {
  const std::pair<const char *, const char *> commands[] = {
      {":begin", "Begin an atomic submission; nothing runs until :submit."},
      {":submit", "Evaluate the pending atomic submission."},
      {":cancel", "Discard pending input."},
      {":bindings", "List retained names and typed values."},
      {":reset", "Clear retained values and pending input."},
      {":quit", "Exit and discard pending input."},
      {":help", "Show these commands."}};
  if (interactive) {
    std::cout << "Matrix pure shell. Complete lines evaluate immediately; incomplete input continues.\n";
    for (const auto &command : commands)
      std::cout << command.first << "  " << command.second << '\n';
    return;
  }
  auto report = json::map();
  report.object["schema"] = json::str("nil.matrix.shell-help.v1");
  auto entries = json::list();
  for (const auto &command : commands) {
    auto entry = json::map();
    entry.object["command"] = json::str(command.first);
    entry.object["description"] = json::str(command.second);
    entries.array.push_back(std::move(entry));
  }
  report.object["commands"] = std::move(entries);
  std::cout << nil::detail::dump(report) << '\n';
}
} // namespace

int run_submission_evaluate_cli(bool request, const std::string &path) {
  evaluation_report report;
  try {
    submission_session session;
    report = session.evaluate(read_source(path), request, path);
  } catch (const std::exception &error) {
    report = input_error(error.what(), path, 0, request);
  }
  print(report);
  return exit_status(report);
}

int run_matrix_shell_cli() {
  submission_session session;
  const bool interactive = terminal();
  bool failed = false, explicit_block = false, discarding = false;
  std::string pending;
  std::optional<evaluation_report> incomplete;
  const auto clear = [&] {
    pending.clear();
    incomplete.reset();
    explicit_block = false;
    discarding = false;
  };
  const auto reject = [&](const std::string &message) {
    print(input_error(message, "<shell>", session.bindings().size()), interactive);
    failed = true;
  };
  const auto evaluate = [&] {
    auto report = session.evaluate(pending, false, "<shell>");
    if (report.check.status == "incomplete") {
      incomplete = std::move(report);
      if (explicit_block) print(*incomplete, interactive);
      return;
    }
    print(report, interactive);
    failed |= exit_status(report) != 0;
    clear();
  };
  try {
    binary_stdin();
    if (interactive) help(true);
    for (;;) {
      if (interactive) {
        std::cout << (discarding ? "discard> " : explicit_block ? "block> " : pending.empty() ? "matrix> " : "...> ");
        std::cout.flush();
      }
      auto line = read_line();
      if (!line.present) break;
      if (line.oversized) {
        reject("submission line exceeds 65536 bytes; pending input discarded");
        pending.clear(); incomplete.reset();
        // The unbuffered suffix might contain a raw-text opener. Require an
        // explicit recovery command before interpreting any following line.
        discarding = true;
        continue;
      }
      auto command = line.text;
      if (!command.empty() && command.back() == '\r') command.pop_back();
      const bool literal_line = !discarding && !pending.empty() &&
          !command.empty() && command.front() == ':' && open_text_block(pending);
      if (!literal_line) {
        if (command == ":quit") return interactive ? 0 : failed ? 1 : 0;
        if (command == ":help") { help(interactive); continue; }
        if (command == ":bindings") { print_bindings(session, interactive); continue; }
        if (command == ":cancel") { clear(); continue; }
        if (command == ":reset") { session.reset(); clear(); continue; }
        if (command == ":submit") {
          if (discarding) { clear(); continue; }
          if (!explicit_block && pending.empty()) { reject("no pending submission to submit"); continue; }
          evaluate();
          continue;
        }
        if (discarding) continue;
        if (command == ":begin") {
          if (explicit_block || !pending.empty()) reject("pending input already exists; submit or cancel it first");
          else explicit_block = true;
          continue;
        }
        if (!command.empty() && command.front() == ':') {
          reject("unknown shell command; use :help");
          continue;
        }
      }
      if (pending.empty() && !explicit_block && line.text.find_first_not_of(" \t\r") == std::string::npos)
        continue;
      const auto bytes = line.text.size() + (line.newline ? 1 : 0);
      if (bytes > submission_max_input_bytes - pending.size()) {
        reject("submission input exceeds 65536 bytes; pending input discarded");
        discarding = true;
        pending.clear(); incomplete.reset();
        continue;
      }
      pending += line.text;
      if (line.newline) pending += '\n';
      if (!explicit_block) evaluate();
    }
    if (explicit_block && !discarding) {
      print(input_error("atomic submission is pending; use :submit or :cancel", "<shell>",
                        session.bindings().size(), false, true), interactive);
      return failed ? 1 : 2;
    }
    if (incomplete) {
      print(*incomplete, interactive);
      return failed ? 1 : 2;
    }
  } catch (const std::exception &error) {
    reject(error.what());
  }
  return failed ? 1 : 0;
}
} // namespace matrix

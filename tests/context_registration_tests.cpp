#include "codegen.hpp"
#include "executable_context.hpp"
#include "matrix_parser.hpp"
#include "parser.hpp"
#include "type_checker.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

const std::string ny_source = R"ny(module context_registration
@ai_context("Unused declaration retains intent")
fn unused() -> i64
  return 7
end fn
@ai_context("Main retains intent π")
fn main() -> i32
  println("42")
  return 0
end fn
)ny";

const std::string matrix_source = R"matrix(book context-registration
context "Unused declaration retains intent"
unused returns number begin paragraph
  give 7
end paragraph
context "Main retains intent π"
main returns override integer begin paragraph
  say "42"
  give 0
end paragraph
)matrix";

std::shared_ptr<ny::ast_program> parse(bool is_matrix, const std::string &source) {
  std::shared_ptr<ny::ast_program> program;
  if (is_matrix) {
    matrix::parser parser(source);
    program = parser.parse_program();
    check(!parser.has_errors(), "Matrix fixture parses");
    if (parser.has_errors()) return nullptr;
  } else {
    ny::parser parser(source);
    program = parser.parse_program();
    check(!parser.has_errors(), "Ny fixture parses");
    if (parser.has_errors()) return nullptr;
  }
  ny::type_checker checker;
  const bool checked = program && checker.check(*program);
  check(checked, "fixture type-checks");
  return checked ? program : nullptr;
}

bool registration_name(const std::string &name) {
  return name.rfind("__nilaix_register_ai_context_", 0) == 0 ||
         name.rfind("__nilaix_init_", 0) == 0;
}

void generation_contract(bool is_matrix) {
  const auto program = parse(is_matrix, is_matrix ? matrix_source : ny_source);
  if (!program) return;
  for (const auto mode : {ny::build_mode::debug_mode, ny::build_mode::test_mode,
                          ny::build_mode::release_mode}) {
    std::string default_context;
    for (const bool registration : {true, false}) {
      ny::codegen_options options;
      check(options.emit_context_registration, "registration remains enabled by default");
      options.mode = mode;
      options.emit_context_registration = registration;
      ny::codegen generator(options);
      std::vector<ny::codegen_diagnostic> diagnostics;
      const auto module = generator.generate_module(*program, &diagnostics);
      check(module && diagnostics.empty(), "every generation mode remains valid");
      if (!module || !diagnostics.empty()) continue;
      const auto context = ny::context_manifest_json(*module);
      if (registration) default_context = context;
      else check(context == default_context, "suppression preserves the complete authored context manifest");

      std::size_t helpers = 0;
      std::size_t initializers = 0;
      std::size_t foreign_registrations = 0;
      std::size_t registration_calls = 0;
      for (const auto &function : module->functions) {
        helpers += function.name.rfind("__nilaix_register_ai_context_", 0) == 0;
        initializers += function.name.rfind("__nilaix_init_", 0) == 0;
        for (const auto &instruction : function.body) {
          if (instruction.opname == "INTEROP_CALL" && instruction.args.size() >= 2 &&
              instruction.args[0] == "nilaix" && instruction.args[1] == "register_ai_context_internal")
            ++foreign_registrations;
          if (instruction.opname == "CALL" && !instruction.args.empty() &&
              registration_name(instruction.args[0])) {
            ++registration_calls;
            check(std::any_of(module->functions.begin(), module->functions.end(),
                [&](const auto &candidate) { return candidate.name == instruction.args[0]; }),
                "every generated registration call has a body");
          }
        }
      }
      const bool generated = registration && mode != ny::build_mode::release_mode;
      check(helpers == (generated ? 2U : 0U), "only enabled non-release generation emits helpers");
      check(foreign_registrations == (generated ? 2U : 0U), "foreign registration is suppressed at generation");
      const bool initialized = generated && mode == ny::build_mode::test_mode;
      check(initializers == (initialized ? 1U : 0U), "test initializer follows helper generation");
      check((registration_calls != 0) == initialized, "suppression leaves no dangling initialization calls");
    }
  }
}

void explicit_foreign_call_is_not_stripped() {
  const auto program = parse(false, ny_source + R"ny(
@interop("nilaix")
fn explicit_registration() -> void
end fn
)ny");
  if (!program) return;
  ny::codegen_options options;
  options.emit_context_registration = false;
  ny::codegen generator(options);
  std::vector<ny::codegen_diagnostic> diagnostics;
  const auto module = generator.generate_module(*program, &diagnostics);
  check(module && diagnostics.empty(), "explicit foreign fixture generates");
  if (!module) return;
  bool found = false;
  for (const auto &function : module->functions)
    for (const auto &instruction : function.body)
      found |= instruction.opname == "INTEROP_CALL" && instruction.args.size() >= 2 &&
               instruction.args[0] == "nilaix" && instruction.args[1] == "explicit_registration";
  check(found, "explicit foreign calls are retained, never filtered from emitted artifacts");
}
} // namespace

int main() {
  try {
    generation_contract(false);
    generation_contract(true);
    explicit_foreign_call_is_not_stripped();
  } catch (const std::exception &error) {
    ++failures;
    std::cerr << "Unexpected exception: " << error.what() << '\n';
  }
  std::cout << checks << " context registration checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}

#include "annotation_arguments.hpp"
#include "analysis.hpp"
#include "codegen.hpp"
#include "nil_loader.hpp"
#include "parser.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
int failures = 0;
void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

void literal_contract() {
  const auto parsed = ny::parse_annotation_arguments(
      R"args("a,\"b\"\\c\n", true, -9223372036854775808, 1.25e-2, io.console.write, mode=vendor::safe, enabled=false)args");
  check(parsed.error.empty() && parsed.arguments.size() == 7, "all literal kinds and named properties parse");
  if (!parsed.error.empty() || parsed.arguments.size() != 7) return;
  check(parsed.arguments[0].kind == "text" && parsed.arguments[0].value == "a,\"b\"\\c\n",
        "quoted commas, quotes, slash, and newline keep their literal identity");
  check(parsed.arguments[1].kind == "boolean" && parsed.arguments[1].value == "true", "boolean category");
  check(parsed.arguments[2].kind == "integer", "signed minimum integer category");
  check(parsed.arguments[3].kind == "floating", "finite floating category");
  check(parsed.arguments[4].kind == "symbol" && !parsed.arguments[4].name, "qualified provider compatibility");
  check(parsed.arguments[5].name == "mode" && parsed.arguments[5].value == "vendor::safe", "named qualified symbol");
  check(parsed.arguments[6].name == "enabled" && parsed.arguments[6].value == "false", "named boolean");
  const auto serialized = ny::serialize_annotation_arguments(parsed.arguments);
  const auto again = ny::parse_annotation_arguments(serialized);
  check(again.error.empty() && ny::serialize_annotation_arguments(again.arguments) == serialized,
        "canonical serialization round-trips without evaluating metadata");
  const auto raw = ny::parse_annotation_arguments(R"args("C:\new\file")args", true);
  check(raw.error.empty() && raw.arguments[0].value == R"args(C:\new\file)args",
        "Matrix raw text does not interpret backslashes");
  check(ny::parse_annotation_arguments("").error.empty(), "empty arguments are valid");
  check(ny::parse_annotation_arguments("0x7fffffffffffffff").error.empty(), "hexadecimal literal compatibility");
  for (const auto &text : {"foo()", "1+2", "x=1,x=2", "x=1,2", "[1]", "{x:1}",
                           "1,", ",1", "x=", "foo bar", "foo.", "foo::", "\"unterminated",
                           "1e9999", "9223372036854775808", "1.0.0", "x==1"})
    check(!ny::parse_annotation_arguments(text).error.empty(), std::string("reject malformed/executable argument: ") + text);
  check(!ny::parse_annotation_arguments(R"args("bad\0text")args").error.empty(), "NUL metadata rejects");
  check(!ny::parse_annotation_arguments(std::string(1024U * 1024U + 1U, 'a')).error.empty(), "argument bytes bounded");
  std::string many = "0";
  for (int i = 0; i < 1024; ++i) many += ",0";
  check(!ny::parse_annotation_arguments(many).error.empty(), "argument count bounded");
  bool rejected_kind = false;
  try { (void)ny::serialize_annotation_arguments({{std::nullopt, "boolean", "fn()"}}); }
  catch (const std::invalid_argument &) { rejected_kind = true; }
  check(rejected_kind, "serializer cannot fabricate a literal category");
  check(ny::annotation_name_matches("@inline", "inline") &&
        ny::annotation_name_matches("@inline()", "inline") &&
        !ny::annotation_name_matches("@inline_custom", "inline") &&
        !ny::annotation_name_matches("@testcase", "test"), "builtin matching uses exact names");
}

void source_contract() {
  const std::string source = R"ny(
@custom("a,\"b\"\\c\n", true, -3, 1.5, io.console.write, key="value")
fn sample() -> i32
 return 0
end fn
)ny";
  ny::parser_options options;
  options.filename = "annotations.ny";
  options.enable_preprocessor = false;
  ny::parser parser(source, options);
  auto program = parser.parse_program();
  check(!parser.has_errors() && program->declarations.size() == 1, "Ny source literal annotations parse");
  if (parser.has_errors() || program->declarations.empty()) return;
  const auto &annotation = program->declarations.front()->annotations.front();
  check(annotation.line == 2, "annotation retains its original source line");
  check(annotation.args && annotation.args->find('\n') != std::string::npos,
        "legacy raw arguments retain decoded Ny text controls");
  check(annotation.typed_arguments && annotation.typed_arguments->size() == 6 &&
        annotation.typed_arguments->front().value == "a,\"b\"\\c\n",
        "typed arguments retain decoded text independently of raw spelling");
  ny::analysis_session session;
  session.primary_path = options.filename;
  session.sources[options.filename] = source;
  session.reflect(*program, options.filename);
  const auto json = session.json(true);
  check(json.find("\"arguments_parsed\":true") != std::string::npos &&
        json.find("\"kind\":\"floating\"") != std::string::npos &&
        json.find("\"name\":\"key\"") != std::string::npos,
        "reflection adds typed arguments without removing raw metadata");
  const auto companion = session.companion_source();
  check(companion.find("pub fn annotation_argument_count()") != std::string::npos &&
        companion.find("annotation_argument_info { annotation_id: 0") != std::string::npos,
        "generated snapshot exposes literal argument records");
  ny::parser legacy("@ai_context(intent=\"legacy\", assumptions=[\"unchanged\"])\nfn legacy()\nend fn\n", options);
  const auto old = legacy.parse_program();
  check(!legacy.has_errors() && old->declarations.size() == 1 &&
        !old->declarations.front()->annotations.front().typed_arguments,
        "existing structured context metadata remains opaque and preserved");
  for (const auto &arguments : {"foo()", "outer(inner(\"payload\"))", "[1]", "config={\"name\":\"value\"}"}) {
    const auto composite_source = std::string("@custom(") + arguments + ")\nfn composite()\nend fn\n";
    ny::parser composite(composite_source, options);
    const auto composite_program = composite.parse_program();
    const bool retained = !composite.has_errors() && composite_program->declarations.size() == 1 &&
        composite_program->declarations.front()->annotations.size() == 1;
    check(retained && composite_program->declarations.front()->annotations.front().args == arguments &&
          !composite_program->declarations.front()->annotations.front().typed_arguments,
          std::string("Ny custom composite metadata remains opaque and retained: ") + arguments);
    ny::analysis_session composite_session;
    composite_session.primary_path = options.filename;
    composite_session.sources[options.filename] = composite_source;
    composite_session.reflect(*composite_program, options.filename);
    check(composite_session.annotations.size() == 1 &&
          composite_session.annotations.front().arguments == arguments &&
          composite_session.json(true).find("\"arguments_parsed\":false,\"parsed_arguments\":[]") != std::string::npos,
          std::string("reflection does not reinterpret Ny custom composite metadata: ") + arguments);
  }
  for (const auto &arguments : {"foo bar", "x=1,x=2", "key=", "1+2"}) {
    ny::parser invalid(std::string("@custom(") + arguments + ")\nfn rejected()\nend fn\n", options);
    (void)invalid.parse_program();
    check(invalid.has_errors(), std::string("Ny parser diagnoses invalid custom arguments: ") + arguments);
  }
  ny::parser unterminated("@custom(\"text\"", options);
  (void)unterminated.parse_program();
  check(unterminated.has_errors(), "unterminated annotation list is diagnosed");
  ny::parser qualified("@Vendor.Tag(value=true)\nfn qualified()\nend fn\n", options);
  const auto qualified_program = qualified.parse_program();
  check(!qualified.has_errors() && qualified_program->declarations.front()->annotations.front().name == "Vendor::Tag",
        "dotted annotation names normalize namespace separators and preserve custom case");
  ny::parser reserved("@nil_binding_cell\nstruct Private\nend struct\n", options);
  (void)reserved.parse_program();
  check(reserved.has_errors(), "source cannot claim compiler-owned cell annotations");
}

void artifact_contract() {
  ny::nil_module module;
  module.module_name = "annotation_artifact";
  const std::string annotation = "@custom(\"line\nnext\tand\\path\") ";
  module.structs = {{"Data", {{"id", "i64"}}, {annotation}}, {"Object", {}, {}}};
  ny::nil_enum_def choice;
  choice.name = "Choice";
  choice.variants = {{"None", ""}};
  choice.annotations = {annotation};
  module.enums = {choice};
  ny::nil_class_def object;
  object.name = "Object";
  object.annotations = {annotation};
  module.classes = {object};
  ny::nil_function main;
  main.name = "main";
  main.return_type = "void";
  main.body = {{"RET", {}}};
  main.annotations = {"@inline_custom", "@test_custom"};
  module.functions = {main};
  std::ostringstream encoded;
  ny::nil_writer writer(encoded);
  writer.emit_text_module(module);
  check(encoded.str().find("# declaration_annotation") != std::string::npos,
        "type annotations with controls use escaped physical records");
  std::istringstream input(encoded.str());
  const auto decoded = ny::parseTextNilModule(input);
  check(decoded->structs.front().annotations == std::vector<std::string>{annotation} &&
        decoded->enums.front().annotations == std::vector<std::string>{annotation} &&
        decoded->classes.front().annotations == std::vector<std::string>{annotation},
        "record, enum, and class annotation text round-trips losslessly");
  check(!decoded->functions.front().is_inline && !decoded->functions.front().is_test,
        "custom annotation prefixes do not activate builtin flags");
  std::unordered_map<std::string, ny::Value> constants;
  std::istringstream vm_input(encoded.str());
  const auto program = ny::parseProgram(vm_input, constants);
  check(!program.empty() && program.front().module_metadata &&
        program.front().module_metadata->structs.front().annotations == std::vector<std::string>{annotation},
        "VM textual loader retains escaped type annotations");
  for (int metadata_case = 0; metadata_case < 3; ++metadata_case) {
    auto binary_module = module;
    if (metadata_case >= 1) binary_module.classes.front().annotations.clear();
    if (metadata_case == 2) {
      binary_module.structs.front().annotations.clear();
      binary_module.enums.front().annotations.clear();
    }
    std::ostringstream binary_output(std::ios::out | std::ios::binary);
    ny::nil_binary_writer binary_writer(binary_output, {});
    binary_writer.emit_binary_module(binary_module);
    std::istringstream binary_input(binary_output.str(), std::ios::in | std::ios::binary);
    const auto binary_decoded = ny::parseBinaryNilModule(binary_input);
    check(binary_decoded->structs.front().annotations == binary_module.structs.front().annotations &&
          binary_decoded->enums.front().annotations == binary_module.enums.front().annotations &&
          binary_decoded->classes.front().annotations == binary_module.classes.front().annotations &&
          binary_decoded->functions.front().annotations == main.annotations,
          "binary module retains annotation metadata with class/type flag combinations");
    std::istringstream binary_vm_input(binary_output.str(), std::ios::in | std::ios::binary);
    std::unordered_map<std::string, ny::Value> binary_constants;
    const auto binary_program = ny::parseBinaryProgram(binary_vm_input, binary_constants);
    check(!binary_program.empty() && binary_program.front().module_metadata &&
          binary_program.front().module_metadata->structs.front().annotations == binary_module.structs.front().annotations &&
          binary_program.front().module_metadata->enums.front().annotations == binary_module.enums.front().annotations &&
          binary_program.front().module_metadata->classes.front().annotations == binary_module.classes.front().annotations,
          "binary VM retains annotation metadata with class/type flag combinations");
    const auto entry = std::find_if(binary_program.begin(), binary_program.end(),
        [](const auto &instruction) { return instruction.is_function_entry; });
    check(entry != binary_program.end() && entry->function_annotations == main.annotations,
          "binary VM function entry retains custom annotations");
  }
  module.functions.front().is_inline = true;
  std::ostringstream inline_encoded;
  ny::nil_writer inline_writer(inline_encoded);
  inline_writer.emit_text_module(module);
  check(inline_encoded.str().find("@inline_custom") != std::string::npos,
        "inline flag does not discard similarly named custom metadata");
  auto inline_module = module;
  inline_module.functions.front().annotations = {
      "@inline(reason=\"hot path\")", "@inline(level=2)", "@inline(reason=\"hot path\")"};
  std::ostringstream inline_properties;
  ny::nil_writer inline_properties_writer(inline_properties);
  inline_properties_writer.emit_text_module(inline_module);
  std::istringstream inline_properties_input(inline_properties.str());
  const auto inline_properties_decoded = ny::parseTextNilModule(inline_properties_input);
  check(inline_properties_decoded->functions.front().is_inline &&
        inline_properties_decoded->functions.front().annotations == inline_module.functions.front().annotations,
        "textual inline metadata retains arguments, duplicates, and source order without an extra marker");
  std::ostringstream binary_inline_properties(std::ios::out | std::ios::binary);
  ny::nil_binary_writer binary_inline_writer(binary_inline_properties, {});
  binary_inline_writer.emit_binary_module(inline_module);
  std::istringstream binary_inline_input(binary_inline_properties.str(), std::ios::in | std::ios::binary);
  const auto binary_inline_decoded = ny::parseBinaryNilModule(binary_inline_input);
  check(binary_inline_decoded->functions.front().annotations == inline_properties_decoded->functions.front().annotations,
        "text and binary inline metadata agree");
  inline_module.functions.front().annotations.clear();
  std::ostringstream inline_flag;
  ny::nil_writer inline_flag_writer(inline_flag);
  inline_flag_writer.emit_text_module(inline_module);
  std::istringstream inline_flag_input(inline_flag.str());
  const auto inline_flag_decoded = ny::parseTextNilModule(inline_flag_input);
  check(inline_flag_decoded->functions.front().is_inline &&
        inline_flag_decoded->functions.front().annotations == std::vector<std::string>{"@inline"},
        "inline flag without explicit metadata still emits its marker");
  for (const auto &body : {std::string("# declaration_annotation \"@custom\"\n"),
                          std::string("function main() -> void:\n# declaration_annotation \"@custom\"\nRET\nend function\n"),
                          std::string("# function_annotation \"@custom\"\nstruct Data\nend struct\n")}) {
    bool rejected = false;
    try { std::istringstream invalid("module invalid\n" + body); (void)ny::parseTextNilModule(invalid); }
    catch (const std::runtime_error &) { rejected = true; }
    check(rejected, "orphan or misplaced escaped annotation record rejects");
  }
}
} // namespace

int main() {
  try { literal_contract(); source_contract(); artifact_contract(); }
  catch (const std::exception &error) { ++failures; std::cerr << "Unexpected exception: " << error.what() << '\n'; }
  std::cout << checks << " annotation argument checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}

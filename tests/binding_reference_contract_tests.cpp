#include "binding_reference.hpp"
#include "codegen.hpp"
#include "nil_contract.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
int failures = 0;
int checks = 0;
const std::string cell = "$nil.binding.cell.i64";
const std::string reference = "ref<" + cell + ">";

void check(bool condition, const std::string &label) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}

void rejects(const std::function<void()> &operation, const std::string &label) {
  bool rejected = false;
  try { operation(); }
  catch (const std::runtime_error &) { rejected = true; }
  check(rejected, label);
}

ny::nil_function function(std::string name, std::vector<std::string> parameters,
                          std::string result, std::vector<ny::nil_instruction> body) {
  ny::nil_function value;
  value.name = std::move(name);
  value.params = std::move(parameters);
  value.return_type = std::move(result);
  value.body = std::move(body);
  return value;
}

std::vector<ny::nil_instruction> initialized_cell() {
  return {{"NEW_STRUCT", {cell}}, {"PUSH_CONST", {"5"}}, {"CAST", {"i64"}},
          {"SET_FIELD", {"value"}}, {"STORE_LOCAL", {"storage"}}};
}

ny::nil_module fixture() {
  ny::nil_module module;
  module.module_name = "binding_reference_contract";
  module.structs = {{cell, {{"value", "i64"}}, {"@nil_binding_cell"}}};
  module.functions = {
      function("bump", {"target:" + reference}, "void",
          {{"STORE_LOCAL", {"target"}}, {"LOAD_LOCAL", {"target"}},
           {"LOAD_LOCAL", {"target"}}, {"GET_FIELD", {"value"}},
           {"PUSH_CONST", {"1"}}, {"ADD", {"i64"}}, {"SET_FIELD", {"value"}},
           {"POP", {}}, {"RET", {}}}),
      function("forward", {"target:" + reference}, "void",
          {{"STORE_LOCAL", {"target"}}, {"LOAD_LOCAL", {"target"}},
           {"CALL", {"bump", "1"}}, {"RET", {}}}),
      function("aliases", {"first:" + reference, "second:" + reference}, "void",
          {{"STORE_LOCAL", {"second"}}, {"STORE_LOCAL", {"first"}},
           {"LOAD_LOCAL", {"first"}}, {"LOAD_LOCAL", {"first"}}, {"GET_FIELD", {"value"}},
           {"PUSH_CONST", {"1"}}, {"ADD", {"i64"}}, {"SET_FIELD", {"value"}}, {"POP", {}},
           {"LOAD_LOCAL", {"second"}}, {"LOAD_LOCAL", {"second"}}, {"GET_FIELD", {"value"}},
           {"PUSH_CONST", {"10"}}, {"ADD", {"i64"}}, {"SET_FIELD", {"value"}}, {"POP", {}},
           {"LOAD_LOCAL", {"first"}}, {"LOAD_LOCAL", {"first"}}, {"GET_FIELD", {"value"}},
           {"LOAD_LOCAL", {"second"}}, {"GET_FIELD", {"value"}}, {"ADD", {"i64"}},
           {"SET_FIELD", {"value"}}, {"POP", {}}, {"RET", {}}}),
      function("main", {}, "void", initialized_cell())};
  auto &body = module.functions.back().body;
  const std::vector<ny::nil_instruction> tail = {
      {"REF_LOCAL", {"storage"}}, {"REF_LOCAL", {"storage"}}, {"CALL", {"aliases", "2"}},
      {"REF_LOCAL", {"storage"}}, {"CALL", {"forward", "1"}},
      {"LOAD_LOCAL", {"storage"}}, {"GET_FIELD", {"value"}}, {"CONSOLE_WRITELN", {}}, {"RET", {}}};
  body.insert(body.end(), tail.begin(), tail.end());
  return module;
}

std::string serialize(const ny::nil_module &module, bool binary) {
  std::ostringstream output;
  if (binary) {
    ny::codegen_options options;
    ny::nil_binary_writer writer(output, options);
    writer.emit_binary_module(module);
  } else {
    ny::nil_writer writer(output);
    writer.emit_text_module(module);
  }
  return output.str();
}

std::vector<ny::Instruction> parse(const ny::nil_module &module, bool binary,
                                  std::unordered_map<std::string, ny::Value> &constants) {
  std::istringstream input(serialize(module, binary));
  return binary ? ny::parseBinaryProgram(input, constants) : ny::parseProgram(input, constants);
}

void rejected_module(const ny::nil_module &module, const std::string &label) {
  rejects([&] { ny::validate_nil_class_contract(module); }, label + " direct validation");
  for (bool binary : {false, true}) {
    const auto encoded = serialize(module, binary);
    const std::string format = binary ? " binary" : " text";
    rejects([&] {
      std::istringstream input(encoded);
      if (binary) (void)ny::parseBinaryNilModule(input);
      else (void)ny::parseTextNilModule(input);
    }, label + format + " module");
    rejects([&] {
      std::istringstream input(encoded);
      std::unordered_map<std::string, ny::Value> constants;
      if (binary) (void)ny::parseBinaryProgram(input, constants);
      else (void)ny::parseProgram(input, constants);
    }, label + format + " VM loader");
  }
}

void rejected_tail(std::vector<ny::nil_instruction> tail, const std::string &label,
                   std::string result = "void") {
  auto module = fixture();
  module.functions.back().return_type = std::move(result);
  auto &body = module.functions.back().body;
  body = initialized_cell();
  body.insert(body.end(), tail.begin(), tail.end());
  rejected_module(module, label);
}

void positive_encodings() {
  for (bool binary : {false, true}) {
    const auto module = fixture();
    ny::validate_nil_class_contract(module);
    std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(module, binary, constants);
    check(!program.empty() && program.front().module_metadata &&
          program.front().module_metadata->structs.front().annotations ==
              std::vector<std::string>{"@nil_binding_cell"},
          "both VM encodings retain explicit compiler cell schema");
    ny::VM vm;
    std::ostringstream output;
    vm.setOutputStream(output);
    vm.loadProgram(program, constants);
    vm.execute();
    check(output.str() == "33\n", "same binding aliases immediately and forwards without copying");
    output.str(""); output.clear();
    vm.resetExecution("main");
    for (unsigned steps = 0; steps < 160 && vm.getPC() < vm.getProgram().size(); ++steps)
      vm.stepInstruction();
    check(output.str() == "33\n", "debugger reset reconstructs cells and stepping preserves aliases");

    std::istringstream input(serialize(module, binary));
    const auto decoded = binary ? ny::parseBinaryNilModule(input) : ny::parseTextNilModule(input);
    check(decoded->functions.front().params == std::vector<std::string>{"target:" + reference} &&
          decoded->structs.front().annotations == std::vector<std::string>{"@nil_binding_cell"},
          "native module readers retain exact cell and reference signatures");

    // A loader result is not a permanent validation token: embedders can edit
    // instruction vectors or replace their shared metadata before VM loading.
    auto unregistered = program;
    unregistered.front().module_metadata.reset();
    rejects([&] { vm.loadProgram(unregistered, constants); }, "missing VM metadata rejects cell program");

    auto forged = program;
    auto metadata = std::make_shared<ny::nil_module>(*program.front().module_metadata);
    metadata->structs.front().annotations.clear();
    forged.front().module_metadata = metadata;
    rejects([&] { vm.loadProgram(forged, constants); }, "VM rechecks forged schema annotation");

    auto changed = program;
    for (auto &instruction : changed)
      if (instruction.op == ny::Opcode::REF_LOCAL) {
        instruction.op = ny::Opcode::PUSH_CONST;
        instruction.args = {"0"};
        break;
      }
    rejects([&] { vm.loadProgram(changed, constants); }, "VM rechecks changed cell argument provenance");
  }
}

void negative_contracts() {
  rejected_tail({{"LOAD_LOCAL", {"storage"}}, {"RET", {}}}, "returning a cell", cell);
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"RET", {}}}, "returning a reference", reference);
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"RET", {}}}, "erasing returned storage to any", "any");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"STORE_GLOBAL", {"escaped"}}, {"RET", {}}}, "global escape");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"NEW_ARRAY_LITERAL", {"1"}}, {"POP", {}}, {"RET", {}}}, "array escape");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"NEW_VARIANT", {"Container::Item", "1"}}, {"POP", {}}, {"RET", {}}}, "enum escape");
  rejected_tail({{"PUSH_CONST", {"0"}}, {"REF_LOCAL", {"storage"}}, {"WRITE_PTR", {}}, {"RET", {}}}, "raw-pointer escape");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"CAST", {"i64"}}, {"POP", {}}, {"RET", {}}}, "cast erases storage provenance");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"CALL", {"external", "1"}}, {"RET", {}}}, "unknown call escape");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"INTEROP_CALL", {"intrinsic", "external", "1", "void"}}, {"RET", {}}}, "foreign call escape");
  rejected_tail({{"LOAD_LOCAL", {"storage"}}, {"CALL", {"bump", "1"}}, {"RET", {}}}, "value cell is not an explicit reference");
  rejected_tail({{"PUSH_CONST", {"0"}}, {"CALL", {"bump", "1"}}, {"RET", {}}}, "forged scalar reference argument");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"FAULT_RAISE", {cell}}}, "cell as fault payload");
  rejected_tail({{"LABEL", {"again"}}, {"NEW_STRUCT", {cell}}, {"PUSH_CONST", {"0"}},
                 {"SET_FIELD", {"value"}}, {"POP", {}}, {"JMP", {"again"}}}, "constructor in loop");
  rejected_tail({{"REF_LOCAL", {"storage"}}, {"CALL", {"bump", "1"}},
                 {"NEW_STRUCT", {cell}}, {"PUSH_CONST", {"0"}}, {"SET_FIELD", {"value"}},
                 {"POP", {}}, {"RET", {}}}, "constructor after a call");

  auto bad = fixture();
  bad.structs.push_back({"Box", {{"item", "i64"}}});
  bad.functions.back().body = initialized_cell();
  const std::vector<ny::nil_instruction> field_escape = {
      {"NEW_STRUCT", {"Box"}}, {"REF_LOCAL", {"storage"}}, {"SET_FIELD", {"item"}}, {"POP", {}}, {"RET", {}}};
  bad.functions.back().body.insert(bad.functions.back().body.end(), field_escape.begin(), field_escape.end());
  rejected_module(bad, "field escape cannot erase reference type");
  bad = fixture();
  bad.structs.push_back({"Box", {{"item", reference}}});
  rejected_module(bad, "schema cannot retain a reference field");
  bad = fixture();
  bad.enums.push_back({"Container", {{"Item", reference}}});
  rejected_module(bad, "enum schema cannot retain a reference payload");
  bad = fixture();
  bad.structs.push_back({"$nil.binding.cell.i32", {{"value", "i32"}}, {"@nil_binding_cell"}});
  bad.functions.front().params.front() = "target:ref<$nil.binding.cell.i32>";
  rejected_module(bad, "reference widths must agree at the call boundary");
  bad = fixture();
  bad.functions.front().params.front() = "target:" + cell;
  rejected_module(bad, "cell values are forbidden parameter signatures");
  bad = fixture();
  bad.functions.front().annotations = {"@intrinsic(\"external\")"};
  rejected_module(bad, "reference parameter cannot cross an intrinsic ABI");
  bad = fixture();
  bad.functions.front().annotations = {" @intrinsic(\"external\")"};
  // Writers reject leading whitespace themselves; direct consumers must also
  // reject this in-memory module before native lowering.
  rejects([&] { ny::validate_nil_class_contract(bad); },
          "whitespace cannot disguise an intrinsic reference ABI");
  bad = fixture();
  bad.functions.front().body = {{"STORE_LOCAL", {"target"}}, {"REF_LOCAL", {"target"}},
                              {"STORE_GLOBAL", {"escaped"}}, {"RET", {}}};
  rejected_module(bad, "referencing a reference cannot erase its lifetime provenance");
  bad = fixture();
  bad.functions.front().params.front() = "target: ref< $nil.binding.cell.i64 >";
  bad.functions.front().body = {{"STORE_LOCAL", {"target"}}, {"LOAD_LOCAL", {"target"}},
                              {"CALL", {"external", "1"}}, {"RET", {}}};
  rejected_module(bad, "whitespace cannot erase reference argument provenance");
  for (const auto &type : {std::string("i64"), std::string("f64"), std::string("text")}) {
    ny::nil_module wrong_value;
    wrong_value.module_name = "wrong_cell_scalar";
    const auto schema = std::string(ny::nil_binding_cell_prefix) + type;
    wrong_value.structs = {{schema, {{"value", type}}, {"@nil_binding_cell"}}};
    wrong_value.string_constants = {{"S0", "wrong"}};
    const ny::nil_instruction scalar = type == "i64"
        ? ny::nil_instruction{"PUSH_CONST_STR", {"S0"}}
        : ny::nil_instruction{"PUSH_CONST", {type == "f64" ? "1" : "1.5"}};
    wrong_value.functions = {function("main", {}, "void",
        {{"NEW_STRUCT", {schema}}, scalar, {"SET_FIELD", {"value"}},
         {"STORE_LOCAL", {"storage"}}, {"RET", {}}})};
    rejected_module(wrong_value, "cell scalar category must agree for " + type);
  }
  bad = fixture();
  bad.functions.push_back(bad.functions.front());
  rejected_module(bad, "duplicate function identity cannot change a reference callee");
  bad = fixture();
  bad.functions.insert(bad.functions.end() - 1,
      function("escape", {"value:any"}, "void", {{"STORE_LOCAL", {"value"}},
          {"LOAD_LOCAL", {"value"}}, {"STORE_GLOBAL", {"escaped"}}, {"RET", {}}}));
  bad.functions.back().body = initialized_cell();
  const std::vector<ny::nil_instruction> shadowed_call = {
      {"PUSH_FN_PTR", {"escape"}}, {"STORE_LOCAL", {"bump"}},
      {"REF_LOCAL", {"storage"}}, {"CALL", {"bump", "1"}}, {"RET", {}}};
  bad.functions.back().body.insert(bad.functions.back().body.end(), shadowed_call.begin(), shadowed_call.end());
  rejected_module(bad, "local function value cannot redirect a checked reference call");
  for (const auto &target : {std::string("escape"), std::string("bump")}) {
    bad = fixture();
    bad.functions.insert(bad.functions.end() - 1,
        function("escape", {"value:any"}, "void", {{"STORE_LOCAL", {"value"}},
            {"LOAD_LOCAL", {"value"}}, {"STORE_GLOBAL", {"escaped"}}, {"RET", {}}}));
    bad.functions.back().body = initialized_cell();
    const std::vector<ny::nil_instruction> indirect_call = {
        {"PUSH_FN_PTR", {target}}, {"STORE_LOCAL", {"callback"}},
        {"REF_LOCAL", {"storage"}}, {"CALL", {"callback", "1"}}, {"RET", {}}};
    bad.functions.back().body.insert(bad.functions.back().body.end(), indirect_call.begin(), indirect_call.end());
    rejected_module(bad, "local indirect target cannot receive a binding reference: " + target);
  }
  bad = fixture();
  bad.functions.insert(bad.functions.end() - 1,
      function("escape", {"value:any"}, "void", {{"STORE_LOCAL", {"value"}},
          {"LOAD_LOCAL", {"value"}}, {"STORE_GLOBAL", {"escaped"}}, {"RET", {}}}));
  bad.functions.insert(bad.functions.end() - 1,
      function("invoke", {"worker:fn(any)->void", "target:" + reference}, "void",
          {{"STORE_LOCAL", {"target"}}, {"STORE_LOCAL", {"worker"}},
           {"LOAD_LOCAL", {"target"}}, {"CALL", {"worker", "1"}}, {"RET", {}}}));
  bad.functions.back().body = initialized_cell();
  const std::vector<ny::nil_instruction> indirect_parameter_call = {
      {"PUSH_FN_PTR", {"escape"}}, {"REF_LOCAL", {"storage"}},
      {"CALL", {"invoke", "2"}}, {"RET", {}}};
  bad.functions.back().body.insert(bad.functions.back().body.end(),
      indirect_parameter_call.begin(), indirect_parameter_call.end());
  rejected_module(bad, "function-valued parameter cannot receive a binding reference indirectly");
  bad = fixture();
  bad.structs.clear();
  rejected_module(bad, "reference requires declared cell schema");
  bad = fixture();
  bad.structs.front().annotations.clear();
  rejected_module(bad, "cell schema requires compiler annotation");
  bad = fixture();
  bad.structs.front().fields.front().second = "text";
  rejected_module(bad, "cell schema scalar must match its exact name");
  bad = fixture();
  bad.structs.front().fields.push_back({"extra", "i64"});
  rejected_module(bad, "cell schema has exactly one field");
  bad = fixture();
  bad.structs.push_back(bad.structs.front());
  rejected_module(bad, "cell schema must be unique");

  ny::VM vm;
  rejects([&] { vm.loadProgram({{ny::Opcode::NEW_STRUCT, {cell}}}, {}); },
          "raw VM instruction embedding cannot invent cell metadata");
}

void class_callable_contracts() {
  // Callable metadata must preserve sensitive result types recursively. This
  // module deliberately has no binding-cell schema: class-only programs need
  // the same protection when a function value is cast or passed onward.
  ny::nil_module module;
  module.module_name = "class_callable_provenance";
  module.structs = {{"Object", {}}};
  module.classes = {{"Object", false, {}, {}}};
  module.functions = {
      function("make_object", {}, "Object", {{"NEW_STRUCT", {"Object"}}, {"RET", {}}}),
      function("main", {}, "void", {{"CALL", {"make_object", "0"}}, {"POP", {}}, {"RET", {}}})};
  ny::validate_nil_class_contract(module);
  check(true, "class callable regression baseline has valid class metadata");

  auto bad = module;
  bad.functions.back().body = {
      {"PUSH_FN_PTR", {"make_object"}}, {"CAST", {"fn()->text"}},
      {"STORE_LOCAL", {"callback"}}, {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}};
  rejected_module(bad, "casting a known callable cannot erase its class result");

  bad = module;
  bad.functions.insert(bad.functions.end() - 1,
      function("invoke", {"worker:fn()->Object"}, "void",
          {{"STORE_LOCAL", {"worker"}}, {"LOAD_LOCAL", {"worker"}},
           {"CAST", {"fn()->text"}}, {"STORE_LOCAL", {"callback"}},
           {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}}));
  bad.functions.back().body = {
      {"PUSH_FN_PTR", {"make_object"}}, {"CALL", {"invoke", "1"}}, {"RET", {}}};
  rejected_module(bad, "casting a callable parameter cannot erase its class result");

  bad = module;
  bad.functions.insert(bad.functions.end() - 1,
      function("make_callback", {}, "fn()->Object", {{"PUSH_FN_PTR", {"make_object"}}, {"RET", {}}}));
  bad.functions.back().body = {
      {"PUSH_FN_PTR", {"make_callback"}}, {"CAST", {"fn()->fn()->text"}},
      {"STORE_LOCAL", {"outer"}}, {"CALL", {"outer", "0"}}, {"STORE_LOCAL", {"inner"}},
      {"CALL", {"inner", "0"}}, {"POP", {}}, {"RET", {}}};
  rejected_module(bad, "casting a nested callable cannot erase its eventual class result");

  // Opaque values cannot manufacture a checked callback signature. Keep an
  // addressed binding in these callers so every forged producer is checked
  // within the class/cell provenance profile.
  auto opaque = module;
  opaque.structs.push_back({cell, {{"value", "i64"}}, {"@nil_binding_cell"}});
  opaque.string_constants = {{"S0", "make_object"}, {"S1", "safe text"}};
  opaque.functions.back().body = initialized_cell();
  const auto append_main = [](ny::nil_module &target, const std::vector<ny::nil_instruction> &tail) {
    auto &body = target.functions.back().body;
    body.insert(body.end(), tail.begin(), tail.end());
  };

  bad = opaque;
  append_main(bad, {{"PUSH_CONST_STR", {"S0"}}, {"CAST", {"fn()->text"}},
      {"STORE_LOCAL", {"callback"}}, {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "raw function-name text cannot be cast to a checked callable");

  bad = opaque;
  bad.functions.insert(bad.functions.end() - 1,
      function("fake_callback", {}, "fn()->text", {{"PUSH_CONST_STR", {"S0"}}, {"RET", {}}}));
  append_main(bad, {{"CALL", {"fake_callback", "0"}}, {"STORE_LOCAL", {"callback"}},
      {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "callable return metadata cannot bless a raw function-name string");

  bad = opaque;
  bad.functions.insert(bad.functions.end() - 1,
      function("safe_text", {}, "text", {{"PUSH_CONST_STR", {"S1"}}, {"RET", {}}}));
  append_main(bad, {{"PUSH_CONST_STR", {"S0"}}, {"STORE_LOCAL", {"safe_text"}},
      {"CALL", {"safe_text", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "raw local function name cannot shadow a checked direct target");

  bad = opaque;
  append_main(bad, {{"LOAD_GLOBAL", {"callback", "fn()->text"}},
      {"STORE_LOCAL", {"callback"}}, {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "global load metadata cannot manufacture a checked callable");

  bad = opaque;
  bad.structs.push_back({"CallbackBox", {{"callback", "fn()->text"}}});
  bad.functions.insert(bad.functions.end() - 1,
      function("read_box", {"box:CallbackBox"}, "void", {{"STORE_LOCAL", {"box"}},
          {"LOAD_LOCAL", {"box"}}, {"GET_FIELD", {"callback"}}, {"STORE_LOCAL", {"callback"}},
          {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}}));
  append_main(bad, {{"RET", {}}});
  rejected_module(bad, "field type metadata cannot manufacture a checked callable");

  bad = opaque;
  bad.enums.push_back({"Choice", {{"Callback", "text"}}});
  append_main(bad, {{"PUSH_CONST_STR", {"S0"}}, {"NEW_VARIANT", {"Choice::Callback", "1"}},
      {"UNWRAP_VARIANT", {"1", "Choice", "fn()->text"}}, {"STORE_LOCAL", {"callback"}},
      {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "enum result metadata cannot manufacture a checked callable");

  bad = opaque;
  append_main(bad, {{"INTEROP_CALL", {"intrinsic", "foreign_callback", "0", "fn()->text"}},
      {"STORE_LOCAL", {"callback"}}, {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "interop result metadata cannot manufacture a checked callable");

  bad = opaque;
  append_main(bad, {{"PUSH_CONST", {"1"}}, {"PUSH_CONST", {"2"}}, {"ADD", {"fn()->text"}},
      {"STORE_LOCAL", {"callback"}}, {"CALL", {"callback", "0"}}, {"POP", {}}, {"RET", {}}});
  rejected_module(bad, "arithmetic result metadata cannot manufacture a checked callable");
}
} // namespace

int main() {
  try { positive_encodings(); negative_contracts(); class_callable_contracts(); }
  catch (const std::exception &error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n';
    return 1;
  }
  if (failures) return 1;
  std::cout << "PASS: " << checks << " binding-reference storage and lifetime checks\n";
  return 0;
}

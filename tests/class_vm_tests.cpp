#include "codegen.hpp"
#include "nil_contract.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace {
int failures = 0;
void check(bool value, const std::string &label) {
  if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
ny::nil_function function(std::string name, std::vector<std::string> params,
                          std::string result, std::vector<ny::nil_instruction> body) {
  ny::nil_function value; value.name = std::move(name); value.params = std::move(params);
  value.return_type = std::move(result); value.body = std::move(body); return value;
}
ny::nil_class_method method(std::string name, std::string owner, bool virt = true,
                            bool abstract = false, bool reference = false) {
  ny::nil_class_method value; value.name = name;
  value.implementation = abstract ? "" : owner + "::" + name;
  value.receiver_type = reference ? "ref<" + owner + ">" : owner;
  value.return_type = "i64"; value.is_virtual = virt; value.is_abstract = abstract;
  return value;
}
ny::nil_module fixture() {
  ny::nil_module module; module.module_name = "class_tests";
  module.structs = {{"Base", {{"x", "i64"}}}, {"Left", {{"padding", "i64"}}},
      {"Derived", {{"padding", "i64"}, {"x", "i64"}}},
      {"Leaf", {{"padding", "i64"}, {"x", "i64"}}}, {"Box", {{"item", "Base"}}}};
  module.classes = {{"Base", true, {}, {method("read", "Base", true, true),
          method("helper", "Base", false), method("refread", "Base", false, false, true)}},
      {"Left", false, {}, {}}, {"Derived", false, {"Left", "Base"}, {method("read", "Derived")}},
      {"Leaf", false, {"Derived"}, {method("read", "Leaf")}}};
  module.functions = {
      function("Base::helper", {"self:Base"}, "i64", {{"STORE_LOCAL", {"self"}},
          {"LOAD_LOCAL", {"self"}}, {"CALL_METHOD", {"Base", "read", "1"}}, {"RET", {}}}),
      function("Base::refread", {"self:ref<Base>"}, "i64", {{"STORE_LOCAL", {"self"}},
          {"LOAD_LOCAL", {"self"}}, {"GET_FIELD", {"x"}}, {"RET", {}}}),
      function("Derived::read", {"self:Derived"}, "i64", {{"STORE_LOCAL", {"self"}},
          {"LOAD_LOCAL", {"self"}}, {"GET_FIELD", {"x"}}, {"PUSH_CONST", {"1"}}, {"ADD", {"i64"}}, {"RET", {}}}),
      function("Leaf::read", {"self:Leaf"}, "i64", {{"STORE_LOCAL", {"self"}},
          {"LOAD_LOCAL", {"self"}}, {"GET_FIELD", {"x"}}, {"PUSH_CONST", {"2"}}, {"ADD", {"i64"}}, {"RET", {}}}),
      function("identity", {"value:Base"}, "Base", {{"STORE_LOCAL", {"value"}}, {"LOAD_LOCAL", {"value"}}, {"RET", {}}}),
      function("main", {}, "void", {{"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"9"}}, {"SET_FIELD", {"padding"}},
          {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}, {"STORE_LOCAL", {"leaf"}},
          {"LOAD_LOCAL", {"leaf"}}, {"CLASS_UPCAST", {"Base"}}, {"STORE_LOCAL", {"base"}},
          {"LOAD_LOCAL", {"base"}}, {"PUSH_CONST", {"42"}}, {"SET_FIELD", {"x"}}, {"POP", {}},
          {"LOAD_LOCAL", {"base"}}, {"CALL_METHOD", {"Base", "helper", "1"}}, {"CONSOLE_WRITELN", {}},
          {"REF_LOCAL", {"leaf"}}, {"CALL_METHOD", {"Leaf", "refread", "1"}}, {"CONSOLE_WRITELN", {}},
          {"LOAD_LOCAL", {"leaf"}}, {"GET_FIELD", {"padding"}}, {"CONSOLE_WRITELN", {}},
          {"NEW_STRUCT", {"Box"}}, {"LOAD_LOCAL", {"base"}}, {"CALL", {"identity", "1"}},
          {"SET_FIELD", {"item"}}, {"GET_FIELD", {"item"}}, {"CALL_METHOD", {"Base", "read", "1"}},
          {"CONSOLE_WRITELN", {}}, {"RET", {}}})};
  return module;
}
std::string serialize(const ny::nil_module &module, bool binary) {
  std::ostringstream output;
  if (binary) { ny::codegen_options options; ny::nil_binary_writer writer(output, options); writer.emit_binary_module(module); }
  else { ny::nil_writer writer(output); writer.emit_text_module(module); }
  return output.str();
}
std::vector<ny::Instruction> parse(const ny::nil_module &module, bool binary,
                                  std::unordered_map<std::string, ny::Value> &constants) {
  std::istringstream input(serialize(module, binary));
  return binary ? ny::parseBinaryProgram(input, constants) : ny::parseProgram(input, constants);
}
void rejected(const ny::nil_module &module, const std::string &label) {
  for (bool binary : {false, true}) for (bool vm : {false, true}) {
    bool caught = false;
    try {
      if (vm) { std::unordered_map<std::string, ny::Value> constants; (void)parse(module, binary, constants); }
      else { std::istringstream input(serialize(module, binary));
        if (binary) (void)ny::parseBinaryNilModule(input); else (void)ny::parseTextNilModule(input); }
    } catch (const std::runtime_error &) { caught = true; }
    check(caught, label + (binary ? " binary" : " text") + (vm ? " VM" : " module"));
  }
}
void negative_body(std::vector<ny::nil_instruction> body, const std::string &label) {
  auto module = fixture(); module.functions.back().body = std::move(body); rejected(module, label);
}
void negative_constructed(std::vector<ny::nil_instruction> body, const std::string &label) {
  std::vector<ny::nil_instruction> prefix = {{"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"9"}},
      {"SET_FIELD", {"padding"}}, {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}};
  prefix.insert(prefix.end(), body.begin(), body.end()); negative_body(std::move(prefix), label);
}

std::vector<ny::nil_instruction> base_value() {
  return {{"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"9"}}, {"SET_FIELD", {"padding"}},
      {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}, {"CLASS_UPCAST", {"Base"}}};
}
ny::nil_function &enum_main(ny::nil_module &module) {
  return *std::find_if(module.functions.begin(), module.functions.end(),
                      [](const auto &value) { return value.name == "main"; });
}
ny::nil_module class_enum_fixture() {
  auto module = fixture();
  module.enums = {{"Container", {{"Item", "Base"}, {"None", ""}, {"Other", "i64"}}},
                 {"OtherContainer", {{"Item", "Base"}, {"None", ""}}},
                 {"Nested", {{"Item", "Container"}}}};
  module.structs.push_back({"ChoiceBox", {{"item", "Container"}}});
  auto body = base_value();
  const std::vector<ny::nil_instruction> rest = {
      {"NEW_VARIANT", {"Container::Item", "1", "Container", "Base"}},
      {"STORE_GLOBAL", {"stored", "Container"}},
      {"LOAD_GLOBAL", {"stored", "Container"}}, {"CALL", {"enum_identity", "1"}},
      {"STORE_LOCAL", {"choice"}}, {"LOAD_LOCAL", {"choice"}},
      {"IS_VARIANT", {"Container::Item", "Container", "Base"}}, {"CONSOLE_WRITELN", {}},
      {"LOAD_LOCAL", {"choice"}}, {"UNWRAP_VARIANT", {"1", "Container", "Base"}},
      {"CALL_METHOD", {"Base", "read", "1"}}, {"CONSOLE_WRITELN", {}},
      {"NEW_STRUCT", {"ChoiceBox"}}, {"LOAD_LOCAL", {"choice"}}, {"SET_FIELD", {"item"}},
      {"STORE_GLOBAL", {"boxed", "ChoiceBox"}}, {"LOAD_GLOBAL", {"boxed", "ChoiceBox"}},
      {"GET_FIELD", {"item"}}, {"UNWRAP_VARIANT", {"1", "Container", "Base"}},
      {"CALL_METHOD", {"Base", "read", "1"}}, {"CONSOLE_WRITELN", {}},
      {"NEW_VARIANT", {"Container::None", "0", "Container", "void"}},
      {"STORE_GLOBAL", {"stored", "Container"}}, {"LOAD_GLOBAL", {"stored", "Container"}},
      {"IS_VARIANT", {"Container::None", "Container", "void"}}, {"CONSOLE_WRITELN", {}}, {"RET", {}}};
  body.insert(body.end(), rest.begin(), rest.end());
  enum_main(module).body = std::move(body);
  module.functions.push_back(function("enum_identity", {"value:Container"}, "Container",
      {{"STORE_LOCAL", {"value"}}, {"LOAD_LOCAL", {"value"}}, {"RET", {}}}));
  return module;
}

void class_enum_contract() {
  for (bool binary : {false, true}) {
    auto module = class_enum_fixture();
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse(module, binary, constants);
    ny::VM vm; std::ostringstream output; vm.setOutputStream(output);
    vm.loadProgram(program, constants); vm.execute();
    check(output.str() == "1\n42\n42\n1\n",
          "typed class enum survives calls, globals, record fields and inherited dispatch");
    std::istringstream source(serialize(module, binary));
    const auto parsed = binary ? ny::parseBinaryNilModule(source) : ny::parseTextNilModule(source);
    check(parsed->enums.size() == 3, "module readers accept checked class enum payloads");
    for (const auto &mutation : std::vector<std::function<void(std::vector<ny::Instruction>&)>>{
        [](auto &instructions) {
          for (auto &instruction : instructions) if (instruction.op == ny::Opcode::NEW_VARIANT) {
            instruction.args[3] = "i64"; return;
          }
        },
        [](auto &instructions) {
          for (auto &instruction : instructions) if (instruction.op == ny::Opcode::UNWRAP_VARIANT) {
            instruction.args[1] = "OtherContainer"; return;
          }
        },
        [](auto &instructions) {
          for (auto &instruction : instructions) if (instruction.op == ny::Opcode::STORE_GLOBAL) {
            instruction.args.resize(1); return;
          }
        },
        [](auto &instructions) {
          const auto metadata = instructions.front().module_metadata;
          instructions.insert(instructions.begin(), {ny::Opcode::LOAD_GLOBAL, {"stored", "Container"}});
          instructions.front().module_metadata = metadata;
        }}) {
      auto altered = program; mutation(altered); bool caught = false;
      try { vm.loadProgram(altered, constants); } catch (const std::runtime_error &) { caught = true; }
      check(caught, "embedding revalidates enum/global provenance after instruction mutation");
    }
    // Static enum identity does not tell us which variant is currently live.
    // Both payload layouts are declared, so this must reach the VM's actual
    // tag/count check rather than being accepted on metadata alone.
    for (bool empty : {false, true}) {
      auto wrong = class_enum_fixture();
      auto &body = enum_main(wrong).body;
      body = empty ? std::vector<ny::nil_instruction>{{"NEW_VARIANT", {"Container::None", "0", "Container", "void"}}}
                   : std::vector<ny::nil_instruction>{{"PUSH_CONST", {"7"}}, {"NEW_VARIANT", {"Container::Other", "1", "Container", "i64"}}};
      body.insert(body.end(), {{"UNWRAP_VARIANT", {"1", "Container", "Base"}}, {"POP", {}}, {"RET", {}}});
      auto wrong_program = parse(wrong, binary, constants);
      vm.loadProgram(wrong_program, constants); bool caught = false;
      try { vm.execute(); } catch (const std::runtime_error &error) {
        caught = std::string(error.what()).find("UNWRAP_VARIANT") != std::string::npos;
      }
      check(caught, "runtime rejects a different live enum variant before exposing its payload");
    }
  }
  const auto negative = [](std::vector<ny::nil_instruction> body, const std::string &label) {
    auto module = class_enum_fixture(); enum_main(module).body = std::move(body); rejected(module, label);
  };
  negative({{"PUSH_CONST", {"0"}}, {"NEW_VARIANT", {"Container::Item", "1", "Container", "Base"}},
            {"POP", {}}, {"RET", {}}}, "scalar cannot fabricate a class enum payload");
  for (const auto &constructor : std::vector<std::vector<std::string>>{
      {"OtherContainer::Item", "1", "Container", "Base"},
      {"Container::Missing", "1", "Container", "Base"},
      {"Container::Item", "1", "Container", "i64"},
      {"Container::Item", "0", "Container", "Base"},
      {"Missing::Item", "1", "Missing", "Base"},
      {"Container::Item", "1"}}) {
    auto body = base_value(); body.push_back({"NEW_VARIANT", constructor});
    body.insert(body.end(), {{"POP", {}}, {"RET", {}}}); negative(body, "class enum constructor requires exact schema and payload metadata");
  }
  auto without_upcast = base_value(); without_upcast.pop_back();
  without_upcast.insert(without_upcast.end(), {{"NEW_VARIANT", {"Container::Item", "1", "Container", "Base"}},
                                             {"POP", {}}, {"RET", {}}});
  negative(without_upcast, "class enum payload requires explicit checked ancestor view");
  for (const auto &tail : std::vector<std::vector<ny::nil_instruction>>{
      {{"CAST", {"i64"}}, {"POP", {}}, {"RET", {}}},
      {{"FREE", {}}, {"RET", {}}},
      {{"CALL", {"unknown", "1"}}, {"RET", {}}},
      {{"STORE_GLOBAL", {"legacy"}}, {"RET", {}}},
      {{"STORE_GLOBAL", {"wrong", "Base"}}, {"RET", {}}},
      {{"UNWRAP_VARIANT", {"1", "OtherContainer", "Base"}}, {"POP", {}}, {"RET", {}}},
      {{"UNWRAP_VARIANT", {"1", "Container", "Leaf"}}, {"POP", {}}, {"RET", {}}},
      {{"NEW_VARIANT", {"Nested::Item", "1", "Nested", "Container"}}, {"FREE", {}}, {"RET", {}}}}) {
    auto body = base_value(); body.push_back({"NEW_VARIANT", {"Container::Item", "1", "Container", "Base"}});
    body.insert(body.end(), tail.begin(), tail.end()); negative(body, "class enum provenance survives operations and nesting");
  }
  negative({{"LOAD_GLOBAL", {"legacy"}}, {"CALL", {"enum_identity", "1"}}, {"POP", {}}, {"RET", {}}},
           "legacy global cannot supply a proven class enum");
  negative({{"LOAD_GLOBAL", {"unknown", "Missing"}}, {"POP", {}}, {"RET", {}}},
           "typed global requires a real unique schema");
  negative({{"PUSH_CONST", {"0"}}, {"STORE_GLOBAL", {"wrong", "Container"}}, {"RET", {}}},
           "scalar cannot initialize a nominal enum global");
  negative({{"PUSH_CONST", {"0"}}, {"CAST", {"Container"}}, {"POP", {}}, {"RET", {}}},
           "cast metadata cannot fabricate a class enum");
  negative({{"INTEROP_CALL", {"intrinsic", "forged", "0", "Container"}}, {"POP", {}}, {"RET", {}}},
           "foreign result metadata cannot fabricate a class enum");
  for (bool annotated : {false, true}) {
    auto external = class_enum_fixture();
    external.functions.push_back(function("foreign_enum", {}, "Container",
        annotated ? std::vector<ny::nil_instruction>{{"NEW_VARIANT", {"Container::None", "0", "Container", "void"}}, {"RET", {}}}
                  : std::vector<ny::nil_instruction>{}));
    if (annotated) external.functions.back().annotations.push_back("@intrinsic(\"foreign.enum\")");
    enum_main(external).body = {{"CALL", {"foreign_enum", "0"}}, {"POP", {}}, {"RET", {}}};
    rejected(external, "external declaration cannot establish class-bearing enum return provenance");
  }
  auto join = base_value();
  join.insert(join.end(), {{"NEW_VARIANT", {"Container::Item", "1", "Container", "Base"}},
      {"STORE_LOCAL", {"choice"}}, {"PUSH_CONST", {"1"}}, {"JMP_IF", {"join"}},
      {"LOAD_LOCAL", {"missing"}}, {"STORE_LOCAL", {"choice"}}, {"LABEL", {"join"}},
      {"LOAD_LOCAL", {"choice"}}, {"FREE", {}}, {"RET", {}}});
  negative(join, "enum class taint survives an unknown local CFG predecessor");
  auto duplicate = class_enum_fixture(); duplicate.enums.push_back(duplicate.enums.front());
  rejected(duplicate, "duplicate enum schemas cannot establish class provenance");
  duplicate = class_enum_fixture(); duplicate.enums.front().variants.push_back({"Item", "Base"});
  rejected(duplicate, "duplicate variant names cannot establish class provenance");
  auto binding = class_enum_fixture(); binding.enums.front().variants[0].associated_type = "$nil.binding.cell.i64";
  rejected(binding, "enum closure cannot smuggle binding-reference storage");
  ny::VM vm; bool caught = false;
  try { vm.loadProgram({{ny::Opcode::LOAD_GLOBAL, {"unknown", "Container"}}}, {}); }
  catch (const std::runtime_error &) { caught = true; }
  check(caught, "raw embedding global metadata requires explicit schemas");
}

void nominal_global_alias_contract() {
  const auto transfer = [](const std::string &type) {
    return function("main", {}, "void", {
        {"LOAD_GLOBAL", {"stored", type}}, {"STORE_GLOBAL", {"copied", type}},
        {"PUSH_CONST", {"1"}}, {"STORE_GLOBAL", {"completed", "i64"}}, {"RET", {}}});
  };
  for (bool binary : {false, true}) {
    for (bool mismatched_schema : {false, true}) {
      ny::nil_module module; module.module_name = "global_alias_tests";
      module.structs = {{"First", {{"x", "i64"}}}, {"Second", {{"x", "text"}}},
          {"Pair", {{"first", "First"}, {"second", mismatched_schema ? "Second" : "First"}}}};
      module.functions = {transfer("Pair")};
      std::unordered_map<std::string, ny::Value> constants;
      const auto program = parse(module, binary, constants);
      ny::StructValue first; first.type_name = "First"; first.fields()["x"] = 7;
      auto second = first; second.type_name = mismatched_schema ? "Second" : "First";
      ny::StructValue pair; pair.type_name = "Pair";
      pair.struct_fields()["first"] = std::make_shared<ny::StructValue>(first);
      pair.struct_fields()["second"] = std::make_shared<ny::StructValue>(second);
      ny::VM vm; vm.loadProgram(program, constants); vm.setGlobal("stored", pair);
      bool caught = false;
      try { vm.execute(); }
      catch (const std::runtime_error &error) {
        caught = std::string(error.what()).find("global storage") != std::string::npos;
      }
      if (mismatched_schema) {
        check(caught, "shared backing storage is validated independently for each nominal schema");
        check(!vm.hasGlobal("copied") && !vm.hasGlobal("completed") &&
              std::get<ny::StructValue>(vm.getGlobal("stored")).data == pair.data &&
              first.fields().size() == 1 && first.fields().at("x") == 7 &&
              first.string_fields().empty() && first.data == second.data,
              "rejected nominal global preserves existing storage and publishes no result");
      } else {
        check(!caught && vm.hasGlobal("completed"), "shared same-schema record remains a valid nominal global");
        if (vm.hasGlobal("copied")) {
          const auto copied = std::get<ny::StructValue>(vm.getGlobal("copied"));
          check(copied.data == pair.data &&
                copied.struct_fields().at("first")->data == copied.struct_fields().at("second")->data,
                "accepted nominal global preserves repeated record identity");
        }
      }
    }
    ny::nil_module module; module.module_name = "global_recursive_tests";
    module.structs = {{"Node", {{"value", "i64"}, {"next", "Node"}}}};
    module.functions = {transfer("Node")};
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse(module, binary, constants);
    ny::StructValue node; node.type_name = "Node"; node.fields()["value"] = 9;
    node.struct_fields()["next"] = std::make_shared<ny::StructValue>(node);
    ny::VM vm; vm.loadProgram(program, constants); vm.setGlobal("stored", node);
    bool caught = false;
    try { vm.execute(); } catch (const std::runtime_error &) { caught = true; }
    check(!caught && vm.hasGlobal("completed"), "recursive same-schema global terminates validation");
    if (vm.hasGlobal("copied")) {
      const auto copied = std::get<ny::StructValue>(vm.getGlobal("copied"));
      check(copied.data == node.data && copied.struct_fields().at("next")->data == node.data,
            "recursive nominal global preserves its cycle and identity");
    }
    // The fixture deliberately creates a shared_ptr cycle. Release that edge
    // after observing it so the regression itself leaves no allocation behind.
    node.struct_fields().clear();
  }
}

void positives() {
  for (bool binary : {false, true}) {
    auto module = fixture(); std::unordered_map<std::string, ny::Value> constants;
    auto program = parse(module, binary, constants);
    check(program.front().module_metadata && program.front().module_metadata->classes.size() == 4,
          "parsed program retains explicit class registry");
    ny::VM vm; std::ostringstream output; vm.setOutputStream(output);
    vm.setMemoryMode(ny::MemoryMode::SAFE_GC); vm.loadProgram(program, constants); vm.execute();
    check(output.str() == "44\n42\n9\n44\n", "virtual, static, inherited reference, field alias and return execution");
    output.str(""); output.clear(); vm.resetExecution("main");
    for (unsigned steps = 0; steps < 200 && vm.getPC() < vm.getProgram().size(); ++steps) vm.stepInstruction();
    check(output.str() == "44\n42\n9\n44\n", "debugger single-step and reset preserve class semantics");
    auto altered = program;
    for (auto &instruction : altered) if (instruction.op == ny::Opcode::CLASS_UPCAST) {
      instruction.args[0] = "Leaf"; break;
    }
    // A retained registry must not be treated as proof of mutable instructions.
    for (auto &instruction : altered) if (instruction.op == ny::Opcode::NEW_STRUCT && instruction.args[0] == "Leaf") {
      instruction.args[0] = "Derived"; break;
    }
    bool caught = false;
    try { vm.loadProgram(altered, constants); } catch (const std::runtime_error &) { caught = true; }
    check(caught, "loader-bypass mutations are revalidated against class metadata");
    std::istringstream input(serialize(module, binary));
    const auto parsed = binary ? ny::parseBinaryNilModule(input) : ny::parseTextNilModule(input);
    check(parsed->classes.size() == 4 && parsed->classes[0].methods[2].receiver_type == "ref<Base>",
          "module readers preserve exact receiver metadata");
  }
  auto library = fixture(); library.is_library = true;
  library.embedded_sources.push_back({"class_tests.ny", "class Example {}"});
  auto bytes = serialize(library, true); std::istringstream input(bytes);
  const auto metadata = ny::parseBinaryLibraryMetadata(input);
  check(metadata.module_name == "class_tests" && metadata.sources.size() == 1,
        "v4 library source bootstrap and checked function metadata round trip");
  bool caught = false; std::istringstream assembly(bytes);
  try { (void)ny::parseBinaryNilModule(assembly); } catch (const std::runtime_error &) { caught = true; }
  check(caught, "native executable parser rejects v4 library kind");

  auto generic = fixture(); generic.structs.push_back({"Pair<i32, i64>", {}});
  auto pass = method("pair", "Leaf", false); pass.parameter_types = {"Pair<i32, i64>"}; pass.return_type = "Pair<i32, i64>";
  generic.classes.back().methods.push_back(pass);
  generic.functions.push_back(function("Leaf::pair", {"self:Leaf", "value:Pair<i32, i64>"}, "Pair<i32, i64>",
      {{"STORE_LOCAL", {"value"}}, {"STORE_LOCAL", {"self"}}, {"LOAD_LOCAL", {"value"}}, {"RET", {}}}));
  for (bool binary : {false, true}) {
    std::istringstream source(serialize(generic, binary));
    auto parsed = binary ? ny::parseBinaryNilModule(source) : ny::parseTextNilModule(source);
    check(parsed->classes.back().methods.back().parameter_types[0] == "Pair<i32, i64>", "quoted generic type metadata round trip");
  }
  auto scoped = fixture();
  scoped.functions.back().body = {{"PUSH_CONST", {"0"}}, {"JMP_IF", {"join"}},
      {"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"9"}}, {"SET_FIELD", {"padding"}},
      {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}, {"STORE_LOCAL", {"scoped"}},
      {"LABEL", {"join"}}, {"RET", {}}};
  for (bool binary : {false, true}) {
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = parse(scoped, binary, constants);
    ny::VM vm; vm.loadProgram(program, constants); vm.execute();
    check(true, "unobserved branch-local class remains legal across CFG join");
  }
}
void negatives() {
  const std::vector<ny::nil_instruction> merge_prefix = {{"PUSH_CONST", {"1"}}, {"JMP_IF", {"unknown"}},
      {"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"9"}}, {"SET_FIELD", {"padding"}},
      {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}, {"JMP", {"join"}},
      {"LABEL", {"unknown"}}, {"LOAD_LOCAL", {"missing"}}, {"LABEL", {"join"}}};
  for (const auto &tail : std::vector<std::vector<ny::nil_instruction>>{
        {{"FREE", {}}, {"RET", {}}},
        {{"CAST", {"i64"}}, {"POP", {}}, {"RET", {}}},
        {{"STORE_LOCAL", {"v"}}, {"REF_LOCAL", {"v"}}, {"FREE", {}}, {"RET", {}}},
        {{"PUSH_CONST", {"0"}}, {"SET_FIELD", {"x"}}, {"POP", {}}, {"RET", {}}}}) {
    auto body = merge_prefix; body.insert(body.end(), tail.begin(), tail.end());
    negative_body(std::move(body), "unknown stack join retains class provenance taint");
  }
  negative_body({{"PUSH_CONST", {"1"}}, {"JMP_IF", {"join"}},
      {"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"9"}}, {"SET_FIELD", {"padding"}},
      {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}, {"STORE_LOCAL", {"v"}},
      {"LABEL", {"join"}}, {"LOAD_LOCAL", {"v"}}, {"FREE", {}}, {"RET", {}}},
      "absent local join retains class provenance taint");
  negative_body({{"INTEROP_CALL", {"intrinsic", "forged", "0", "Base"}}, {"POP", {}}, {"RET", {}}}, "foreign class result fabrication");
  negative_body({{"INTEROP_CALL", {"intrinsic", "forged", "0", "Box"}}, {"POP", {}}, {"RET", {}}}, "foreign class-containing struct fabrication");
  negative_body({{"PUSH_CONST", {"0"}}, {"NEW_VARIANT", {"Container::Item", "1"}},
      {"UNWRAP_VARIANT", {"1", "Container", "Base"}}, {"POP", {}}, {"RET", {}}}, "enum class payload fabrication");
  negative_body({{"NEW_STRUCT", {"Box"}}, {"GET_FIELD", {"item"}}, {"POP", {}}, {"RET", {}}}, "uninitialized class-valued field");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"STORE_LOCAL", {"value"}}, {"RET", {}}}, "incomplete class local escape");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"CLASS_UPCAST", {"Base"}}, {"POP", {}}, {"RET", {}}}, "incomplete class upcast");
  negative_constructed({{"FREE", {}}, {"RET", {}}}, "class passed to raw FREE");
  negative_constructed({{"GET_FIELD", {"missing"}}, {"POP", {}}, {"RET", {}}}, "unknown class field");
  negative_constructed({{"CAST", {"i64"}}, {"POP", {}}, {"RET", {}}}, "class provenance erasure");
  negative_constructed({{"PUSH_CONST", {"1"}}, {"PTR_ADD", {"i64"}}, {"POP", {}}, {"RET", {}}}, "class pointer arithmetic");
  negative_constructed({{"READ_PTR", {}}, {"POP", {}}, {"RET", {}}}, "class pointer read");
  negative_constructed({{"PUSH_CONST", {"0"}}, {"WRITE_PTR", {}}, {"RET", {}}}, "class pointer write");
  negative_constructed({{"STORE_LOCAL", {"v"}}, {"REF_LOCAL", {"v"}}, {"CLASS_UPCAST", {"Base"}}, {"POP", {}}, {"RET", {}}}, "complete reference covariance");
  negative_body({{"PUSH_CONST", {"0"}}, {"CAST", {"Base"}}, {"POP", {}}, {"RET", {}}}, "CAST fabrication");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"CAST", {"Base"}}, {"POP", {}}, {"RET", {}}}, "CAST covariance");
  negative_body({{"NEW_STRUCT", {"Derived"}}, {"CLASS_UPCAST", {"Leaf"}}, {"POP", {}}, {"RET", {}}}, "downcast");
  negative_body({{"NEW_STRUCT", {"Left"}}, {"CLASS_UPCAST", {"Base"}}, {"POP", {}}, {"RET", {}}}, "unrelated upcast");
  negative_body({{"PUSH_CONST", {"0"}}, {"CALL_METHOD", {"Base", "read", "1"}}, {"POP", {}}, {"RET", {}}}, "forged receiver");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"CALL_METHOD", {"Base", "read", "1"}}, {"POP", {}}, {"RET", {}}}, "unproved receiver view");
  negative_body({{"NEW_STRUCT", {"Base"}}, {"POP", {}}, {"RET", {}}}, "abstract construction");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"FAULT_RAISE", {"Leaf"}}}, "class fault payload");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"1"}}, {"ADD", {"i64"}}, {"POP", {}}, {"RET", {}}}, "class arithmetic");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"STORE_LOCAL", {"v"}}, {"REF_LOCAL", {"v"}}, {"CLASS_UPCAST", {"Base"}}, {"POP", {}}, {"RET", {}}}, "reference covariance");
  negative_body({{"PUSH_CONST", {"0"}}, {"CALL", {"identity", "1"}}, {"POP", {}}, {"RET", {}}}, "forged class argument");
  negative_body({{"NEW_STRUCT", {"Box"}}, {"PUSH_CONST", {"0"}}, {"SET_FIELD", {"item"}}, {"POP", {}}, {"RET", {}}}, "forged class field");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"CALL_METHOD", {"Leaf", "missing", "1"}}, {"POP", {}}, {"RET", {}}}, "missing method");
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"CALL_METHOD", {"Leaf", "read", "0"}}, {"POP", {}}, {"RET", {}}}, "method arity");
  auto bad = fixture(); bad.classes.clear(); rejected(bad, "missing class metadata");
  for (const std::string reserved : {"i64", "int", "TEXT", "Void", "array", "pointer"}) {
    bad = fixture(); bad.structs.push_back({reserved, {}});
    bad.classes.push_back({reserved, false, {}, {}});
    rejected(bad, "reserved class identity " + reserved);
  }
  negative_body({{"NEW_STRUCT", {"Leaf"}}, {"PUSH_CONST", {"1"}}, {"JMP_IF", {"join"}},
      {"PUSH_CONST", {"9"}}, {"SET_FIELD", {"padding"}}, {"LABEL", {"join"}},
      {"PUSH_CONST", {"40"}}, {"SET_FIELD", {"x"}}, {"STORE_LOCAL", {"incomplete"}}, {"RET", {}}},
      "constructor initialization must hold on every CFG predecessor");
  bad = fixture(); bad.classes.push_back(bad.classes[0]); rejected(bad, "duplicate class");
  bad = fixture(); bad.classes[0].bases = {"Leaf"}; rejected(bad, "cyclic ancestry");
  bad = fixture(); bad.classes[1].bases = {"Missing"}; rejected(bad, "unknown parent");
  bad = fixture(); bad.classes[2].bases.push_back("Left"); rejected(bad, "duplicate parent");
  bad = fixture(); bad.structs[2].fields.erase(bad.structs[2].fields.begin()); rejected(bad, "missing inherited field");
  bad = fixture(); bad.classes[2].methods[0].is_virtual = false; rejected(bad, "lost virtual override");
  bad = fixture(); bad.classes[2].methods.clear(); rejected(bad, "unresolved abstract slot");
  bad = fixture(); bad.classes[2].methods[0].return_type = "text"; rejected(bad, "method return mismatch");
  bad = fixture(); bad.classes[2].methods[0].implementation = "missing"; rejected(bad, "missing implementation");
  bad = fixture(); bad.functions[4].body = {{"STORE_LOCAL", {"value"}}, {"PUSH_CONST", {"0"}}, {"RET", {}}}; rejected(bad, "forged class return");
  bad = fixture(); bad.functions.back().body = {{"RET", {}}};
  for (unsigned i = 0; i < 258; ++i) {
    const auto name = "Deep" + std::to_string(i); bad.structs.push_back({name, {}});
    bad.classes.push_back({name, false, i == 0 ? std::vector<std::string>{} : std::vector<std::string>{"Deep" + std::to_string(i - 1)}, {}});
  }
  // Reverse declaration order forces validation of the entire unvisited chain.
  std::reverse(bad.classes.begin(), bad.classes.end()); rejected(bad, "bounded ancestry depth");
  ny::VM vm; bool caught = false;
  try { vm.loadProgram({{ny::Opcode::CLASS_UPCAST, {"Base"}}}, {}); } catch (const std::runtime_error &) { caught = true; }
  check(caught, "raw embedding does not guess class metadata");
}
} // namespace
int main() {
  try { positives(); negatives(); class_enum_contract(); nominal_global_alias_contract(); }
  catch (const std::exception &error) { std::cerr << "Unexpected exception: " << error.what() << '\n'; return 1; }
  if (failures) return 1;
  std::cout << "Class metadata, provenance, dispatch, alias and version contracts passed\n";
}

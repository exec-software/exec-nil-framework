#include "json_rpc.hpp"
#include "debugger_core.hpp"
#include "nil_loader.hpp"
#include "codegen.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using ny::Instruction;
using ny::ArrayValue;
using ny::EnumValue;
using ny::StructValue;
using ny::VM;
using ny::Value;
using ny::debug::Debugger;
using ny::debug::ExecutionState;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void require_valid_json(const std::string &text, const std::string &context) {
  ny::json::ParseError error;
  require(ny::json::parse(text, &error).has_value(),
          context + " was not valid JSON at offset " +
              std::to_string(error.offset) + ": " + error.message +
              " (value: " + text + ")");
}

class TempNilFile {
public:
  TempNilFile(const std::string &stem, const std::string &contents) {
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    path_ = std::filesystem::temp_directory_path() /
            ("nildebugger-" + stem + "-" + std::to_string(nonce) + ".nil");
    std::ofstream output(path_, std::ios::binary);
    require(static_cast<bool>(output), "could not create temporary NIL file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    require(static_cast<bool>(output), "could not write temporary NIL file");
  }

  ~TempNilFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void append_u32(std::string &bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
}

void append_u64(std::string &bytes, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8)
    bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
}

void append_binary_string(std::string &bytes, const std::string &value) {
  append_u32(bytes, static_cast<std::uint32_t>(value.size()));
  bytes += value;
}

std::string binary_library_with_main(const std::string &magic) {
  std::string functions;
  append_u32(functions, 1); // function count
  append_binary_string(functions, "main");
  append_u32(functions, 0); // parameter count
  append_u32(functions, 0); // generic parameter count
  append_binary_string(functions, "void");
  append_u32(functions, 1); // instruction count
  append_binary_string(functions, "RET");
  append_u32(functions, 0); // operand count
  append_u32(functions, 1); // debug line

  std::string library = magic;
  append_u32(library, 2); // NIL v2
  append_u32(library, 1); // library artifact
  append_u32(library, 1); // section count
  append_u32(library, 2); // function section
  append_u64(library, 36); // four-byte magic + header + one table entry
  append_u64(library, static_cast<std::uint64_t>(functions.size()));
  library += functions;
  return library;
}

const char *kProgram = R"NIL(module debugger_test

function helper(value: i32):
    STORE_LOCAL value ; line 20
    LOAD_LOCAL value ; line 21
    RET ; line 22
end function

function main:
    PUSH_CONST 7 ; line 10
    STORE_GLOBAL G ; line 10
    PUSH_CONST 42 ; line 11
    CALL helper 1 ; line 11
    RET ; line 12
end function
)NIL";

void test_first_run_and_clean_restart(const std::string &path) {
  Debugger debugger;
  require(debugger.load_program(path), "debugger did not load textual NIL");

  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated,
          "first debugger run did not terminate");
  require(debugger.evaluate_expression("G") == "7",
          "first debugger run did not enter main");
  require(debugger.get_vm().getStack().size() == 1,
          "unexpected result stack after first run");
  require(debugger.get_vm().getDebugFrames().size() == 1,
          "call frames leaked after first run");
  require(debugger.get_vm().getDebugFrames().back().function_name == "main",
          "root debug frame is not main");

  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated,
          "restarted debugger run did not terminate");
  require(debugger.get_vm().getStack().size() == 1,
          "debugger restart retained the prior operand stack");
  require(debugger.get_vm().getDebugFrames().size() == 1,
          "debugger restart retained prior call frames");

  debugger.step_into(); // main entry label
  debugger.step_into(); // PUSH_CONST 7
  require(debugger.get_vm().getStack().size() == 1,
          "paused restart setup did not leave one operand");
  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated &&
              debugger.get_vm().getStack().size() == 1,
          "run from a paused invocation was not a clean restart");
}

void test_real_frames_and_locals(const std::string &path) {
  Debugger debugger;
  require(debugger.load_program(path), "debugger did not load frame fixture");
  std::string error;
  const auto breakpoint = debugger.set_breakpoint(path, 20, "", &error);
  require(breakpoint.has_value(), "could not set helper breakpoint: " + error);

  debugger.run();
  require(debugger.get_state() == ExecutionState::Paused,
          "helper breakpoint did not pause execution");
  auto frames = debugger.get_vm().getDebugFrames();
  require(frames.size() == 2, "debugger did not expose both real call frames");
  require(frames[0].function_name == "main" &&
              frames[1].function_name == "helper",
          "debug frame function names are incorrect");

  debugger.step_into();
  frames = debugger.get_vm().getDebugFrames();
  require(frames.size() == 2, "single step corrupted the real call stack");
  const auto local = frames.back().locals.find("value");
  require(local != frames.back().locals.end(),
          "helper parameter was not visible as a real local");
  require(std::holds_alternative<int64_t>(local->second) &&
              std::get<int64_t>(local->second) == 42,
          "helper local had the wrong value");
  require(debugger.evaluate_expression("value") == "42",
          "debugger expression lookup did not use the real frame");
  require(debugger.get_stack_json().find("\"fn\":\"helper\"") !=
              std::string::npos &&
              debugger.get_stack_json().find("\"fn\":\"main\"") !=
                  std::string::npos,
          "structured stack output omitted real function frames");
}

void test_selected_frame_lookup_is_lexical() {
  TempNilFile fixture(
      "frame-lookup",
      R"NIL(module frame_lookup
function helper:
    NOP ; line 41
    RET ; line 42
end function
function main:
    PUSH_CONST 7 ; line 30
    STORE_GLOBAL SHARED ; line 30
    PUSH_CONST 99 ; line 30
    STORE_LOCAL SHARED ; line 30
    PUSH_CONST 123 ; line 30
    STORE_LOCAL caller_only ; line 30
    PUSH_CONST 42 ; line 30
    STORE_LOCAL $ny.local.0.SHARED ; line 30
    NEW_STRUCT sample ; line 30
    PUSH_CONST 11 ; line 30
    SET_FIELD value ; line 30
    STORE_LOCAL $ny.local.1.record ; line 30
    CALL helper 0 ; line 31
    RET ; line 32
end function
)NIL");
  const std::string path = fixture.path().string();

  Debugger debugger;
  require(debugger.load_program(path),
          "debugger did not load lexical-frame fixture");
  std::string error;
  require(debugger.set_breakpoint(path, 41, "", &error).has_value(),
          "could not set lexical-frame breakpoint: " + error);
  debugger.run();
  require(debugger.get_state() == ExecutionState::Paused &&
              debugger.get_vm().getCallDepth() == 2,
          "lexical-frame fixture did not stop inside helper");
  require(debugger.evaluate_expression("SHARED") == "7",
          "top-frame lookup leaked a shadowing caller local instead of using the global");
  require(debugger.evaluate_expression("caller_only") ==
              "(undefined: caller_only)",
          "top-frame lookup exposed an out-of-scope caller local");
  require(debugger.evaluate_expression("$ny.local.0.SHARED") ==
              "(undefined: $ny.local.0.SHARED)",
          "exact slot lookup exposed another frame's local");
  debugger.set_frame(1);
  require(debugger.evaluate_expression("SHARED") == "99" &&
              debugger.evaluate_expression("caller_only") == "123",
          "explicit caller-frame selection did not expose caller locals");
  require(debugger.evaluate_expression("$ny.local.0.SHARED") == "42" &&
              debugger.evaluate_expression("$ny.local.1.record.value") == "11",
          "disambiguated storage slots or their fields cannot be inspected");

  Debugger conditioned;
  require(conditioned.load_program(path),
          "conditional debugger did not load lexical-frame fixture");
  require(conditioned.set_breakpoint(path, 41, "SHARED == 99", &error)
              .has_value(),
          "could not set caller-leakage condition: " + error);
  conditioned.run();
  require(conditioned.get_state() == ExecutionState::Terminated,
          "conditional breakpoint used an out-of-scope caller local");
}

void test_global_eval_and_false_condition(const std::string &path) {
  Debugger debugger;
  require(debugger.load_program(path),
          "debugger did not load conditional-breakpoint fixture");
  std::string error;
  const auto breakpoint =
      debugger.set_breakpoint(path, 10, "G == 999", &error);
  require(breakpoint.has_value(),
          "could not set conditional breakpoint: " + error);
  const auto unresolved_text =
      debugger.set_breakpoint(path, 10, "missing != ready", &error);
  require(unresolved_text.has_value(),
          "could not set unresolved-text condition: " + error);

  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated,
          "false conditional breakpoint paused instead of continuing");
  require(debugger.evaluate_expression("G") == "7",
          "global evaluation did not return directly");
}

void test_condition_language_validation(const std::string &path) {
  Debugger debugger;
  require(debugger.load_program(path),
          "debugger did not load condition-language fixture");

  const std::vector<std::string> valid = {
      "value == 42",
      "object.field != \"ready\"",
      "state == ready",
      "number >= -1.25e+3",
      "message == \"a == b\"",
  };
  for (const auto& condition : valid) {
    std::string error;
    const auto breakpoint = debugger.set_breakpoint(path, 20, condition, &error);
    require(breakpoint.has_value(),
            "valid condition was rejected ('" + condition + "'): " + error);
    require(debugger.remove_breakpoint(*breakpoint),
            "could not remove valid-condition breakpoint");
  }

  const std::vector<std::pair<std::string, std::string>> invalid = {
      {"value", "exactly one comparator"},
      {"9value == 1", "left operand"},
      {"object.field.deep == 1", "one-field dot path"},
      {"value = 1", "unsupported comparator"},
      {"value == 1 == 2", "exactly one comparator"},
      {"value == 12junk", "complete finite number"},
      {"value == 1e999", "complete finite number"},
      {"value == nan", "complete finite number"},
      {"value == \"unterminated", "unterminated"},
      {"value == \"ok\" trailing", "unexpected characters"},
      {"value == two words", "quoted string, or bare scalar"},
      {"value == \"bad\\q\"", "unsupported string escape"},
      {"value ==", "right operand is missing"},
  };
  for (const auto& [condition, expected_error] : invalid) {
    std::string error;
    require(!debugger.set_breakpoint(path, 20, condition, &error),
            "malformed condition was accepted: " + condition);
    require(error.find("invalid breakpoint condition") != std::string::npos &&
                error.find(expected_error) != std::string::npos,
            "malformed condition did not return an actionable error: " + error);
  }

  std::string error;
  const auto breakpoint =
      debugger.set_breakpoint(path, 21, "value == 42", &error);
  require(breakpoint.has_value(),
          "could not set executable valid condition: " + error);
  require(!debugger.set_breakpoint_condition(
              *breakpoint, "value == 42junk", &error),
          "malformed breakpoint-condition update was accepted");
  require(error.find("invalid breakpoint condition") != std::string::npos,
          "condition update failure was not actionable");
  const auto breakpoints = debugger.get_breakpoints();
  require(breakpoints.size() == 1 &&
              breakpoints.front().condition == "value == 42",
          "failed condition update replaced the prior valid condition");

  debugger.run();
  require(debugger.get_state() == ExecutionState::Paused &&
              debugger.get_current_line() == 21,
          "valid numeric condition did not stop at the breakpoint");
}

void test_breakpoint_resolution_failures(const std::string &path) {
  Debugger debugger;
  require(debugger.load_program(path),
          "debugger did not load breakpoint-resolution fixture");
  std::string error;
  require(!debugger.set_breakpoint(path + ".other", 10, "", &error),
          "breakpoint with a mismatched filename was accepted");
  require(!error.empty(), "filename mismatch did not explain the failure");
  require(!debugger.set_breakpoint(path, 999, "", &error),
          "unresolved breakpoint line was accepted");
  require(!error.empty(), "unresolved line did not explain the failure");
}

void test_exact_typed_conditions() {
  TempNilFile fixture(
      "typed-conditions",
      R"NIL(module typed_conditions
const TEXT_VALUE = "7"

function main:
    PUSH_CONST 9007199254740993 ; line 30
    STORE_GLOBAL BIG ; line 30
    PUSH_CONST_STR TEXT_VALUE ; line 30
    STORE_GLOBAL TEXT ; line 30
    PUSH_CONST 1.2345678901234567 ; line 30
    STORE_GLOBAL FRACTION ; line 30
    NOP ; line 31
    RET ; line 32
end function
)NIL");
  const std::string path = fixture.path().string();

  struct Case {
    std::string condition;
    bool should_stop;
  };
  const std::vector<Case> cases = {
      {"BIG == 9007199254740993", true},
      {"BIG == 9007199254740992", false},
      {"BIG > 9007199254740992.0", true},
      {"FRACTION == 1.2345678901234567", true},
      {"TEXT == 7", false},
      {"TEXT == \"7\"", true},
  };
  for (const auto &test : cases) {
    Debugger debugger;
    require(debugger.load_program(path),
            "debugger did not load typed-condition fixture");
    std::string error;
    require(debugger.set_breakpoint(path, 31, test.condition, &error)
                .has_value(),
            "typed condition was rejected ('" + test.condition + "'): " +
                error);
    debugger.run();
    require((debugger.get_state() == ExecutionState::Paused) ==
                test.should_stop,
            "typed condition produced the wrong stop decision: " +
                test.condition);
  }
}

void test_per_vm_callback_isolation(const std::string &path) {
  Debugger first;
  require(first.load_program(path), "first debugger did not load fixture");
  std::string error;
  require(first.set_breakpoint(path, 10, "missing == 1", &error).has_value(),
          "could not set first debugger breakpoint: " + error);

  // Constructing another debugger must not redirect the first VM's callback.
  Debugger second;
  require(second.load_program(path), "second debugger did not load fixture");
  first.run();
  require(first.get_state() == ExecutionState::Terminated,
          "a second debugger captured the first VM's callback");
  require(second.get_state() == ExecutionState::Paused,
          "the first VM mutated the second debugger's state");
}

void test_step_over_out_and_frame_cleanup(const std::string &path) {
  Debugger over;
  require(over.load_program(path), "step-over debugger did not load fixture");
  over.step_into(); // main label
  over.step_into(); // PUSH_CONST 7
  over.step_into(); // STORE_GLOBAL G
  over.step_into(); // PUSH_CONST 42
  require(over.get_current_line() == 11 &&
              over.get_vm().getCallDepth() == 1,
          "step-over setup did not stop on the call");
  over.step_over();
  require(over.get_state() == ExecutionState::Paused &&
              over.get_current_line() == 12 &&
              over.get_vm().getCallDepth() == 1,
          "step-over did not finish the callee in the caller frame");

  Debugger out;
  require(out.load_program(path), "step-out debugger did not load fixture");
  std::string error;
  require(out.set_breakpoint(path, 20, "", &error).has_value(),
          "could not set step-out breakpoint: " + error);
  out.run();
  require(out.get_vm().getCallDepth() == 2,
          "step-out setup did not enter helper");
  out.set_frame(1);
  require(out.get_current_frame() == 1,
          "caller frame was not selected before failed reload");

  TempNilFile invalid(
      "failed-frame-reload",
      "module invalid\nfunction helper:\n RET ; line 1\nend function\n");
  require(!out.load_program(invalid.path().string()),
          "invalid reload unexpectedly succeeded");
  require(out.get_current_frame() == 1,
          "failed reload changed the selected inspection frame");

  out.step_out();
  require(out.get_state() == ExecutionState::Paused &&
              out.get_vm().getCallDepth() == 1 &&
              out.get_current_line() == 12 &&
              out.get_current_frame() == -1,
          "step-out did not unwind cleanly or reset frame selection");
}

void test_json_value_serialization() {
  const std::string valid_utf8 = "snowman \xe2\x98\x83 emoji \xf0\x9f\x98\x80";
  const std::string valid_utf8_json = Debugger::json_str(valid_utf8);
  require(valid_utf8_json == "\"" + valid_utf8 + "\"",
          "valid UTF-8 was not preserved in JSON output");
  require_valid_json(valid_utf8_json, "valid UTF-8 string serialization");

  std::string invalid_utf8 = "invalid:";
  invalid_utf8 += "\xc0\xaf";         // overlong encoding
  invalid_utf8 += "\xed\xa0\x80";     // encoded surrogate
  invalid_utf8 += "\xf4\x90\x80\x80"; // above U+10FFFF
  invalid_utf8 += "\xe2\x82";          // truncated sequence
  invalid_utf8.push_back(static_cast<char>(0xff));
  const std::string invalid_utf8_json = Debugger::json_str(invalid_utf8);
  require(
      invalid_utf8_json ==
          "\"invalid:\\u00c0\\u00af\\u00ed\\u00a0\\u0080"
          "\\u00f4\\u0090\\u0080\\u0080\\u00e2\\u0082\\u00ff\"",
      "invalid UTF-8 bytes were not escaped deterministically");
  require_valid_json(invalid_utf8_json,
                     "arbitrary-byte string serialization");

  const double precise = 1.2345678901234567;
  const std::string precise_json = Debugger::format_value(Value{precise});
  ny::json::ParseError parse_error;
  const auto precise_value = ny::json::parse(precise_json, &parse_error);
  require(precise_value && precise_value->is_number() &&
              precise_value->as_number() == precise,
          "double formatting did not round-trip at max_digits10");

  const std::vector<double> nonfinite = {
      std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::quiet_NaN(),
  };
  for (double value : nonfinite) {
    const std::string json = Debugger::format_value(Value{value});
    require(json == "null", "non-finite double was emitted as a JSON number");
    require_valid_json(json, "non-finite scalar serialization");
  }

  StructValue node;
  node.type_name = "Node";
  node.double_fields()["bad"] = std::numeric_limits<double>::quiet_NaN();
  node.struct_fields()["self"] = std::make_shared<StructValue>(node);
  const std::string struct_json = Debugger::format_value(Value{node});
  require(struct_json.find("<cycle>") != std::string::npos &&
              struct_json.find("\"bad\":null") != std::string::npos,
          "cyclic struct or nested non-finite value was not bounded safely");
  require_valid_json(struct_json, "cyclic struct serialization");
  // The serializer must observe the cycle, but this manually assembled test
  // fixture owns its shared_ptr edges and must release them afterwards.
  node.struct_fields().clear();

  auto recursive_enum = std::make_shared<EnumValue>();
  recursive_enum->variant_name = "Loop";
  recursive_enum->payloads.emplace_back(recursive_enum);
  const std::string enum_json =
      Debugger::format_value(Value{*recursive_enum});
  require(enum_json.find("<cycle>") != std::string::npos,
          "recursive enum payload was not cycle bounded");
  require_valid_json(enum_json, "recursive enum serialization");
  recursive_enum->payloads.clear();

  ArrayValue nested;
  nested.elements.emplace_back(int64_t{1});
  for (int depth = 0; depth < 40; ++depth) {
    ArrayValue outer;
    outer.elements.emplace_back(std::move(nested));
    nested = std::move(outer);
  }
  const std::string array_json = Debugger::format_value(Value{nested});
  require(array_json.find("<max-depth>") != std::string::npos,
          "deep array serialization was not depth bounded");
  require_valid_json(array_json, "deep array serialization");

  const ny::ArrayHandle handle{17179869184LL};
  const std::string handle_json = "{\"__array_handle__\":17179869184}";
  require(Debugger::format_value(Value{handle}) == handle_json,
          "array handles were serialized as ordinary scalar integers");
  require(Debugger::format_value(Value{int64_t{17179869184LL}}) == "17179869184",
          "scalar collision was serialized as an array handle");
  StructValue holder;
  holder.type_name = "Holder";
  holder.array_fields()["items"] = handle;
  EnumValue wrapped;
  wrapped.variant_name = "Option::Some";
  wrapped.payloads.emplace_back(handle);
  require(Debugger::format_value(Value{wrapped}).find(handle_json) != std::string::npos,
          "enum array payload lost its serialized kind");
  ArrayValue legacy;
  legacy.elements.emplace_back(handle);
  require(Debugger::format_value(Value{legacy}) == "[" + handle_json + "]",
          "nested array handle lost its serialized kind");
  Debugger inspector;
  inspector.get_vm().setGlobal("holder", holder);
  require(inspector.evaluate_expression("holder.items") == handle_json,
          "debugger member lookup lost array-valued fields");
  require_valid_json(Debugger::format_value(Value{holder}),
                     "array field serialization");
}

void test_source_binding_reference_inspection() {
  for (const auto& type : {"i32", "i64", "u8", "bool", "f64", "text"}) {
    const std::string scalar_type = type;
    const std::string code =
        "module reference_inspection\nfunction main:\n"
        "LOAD_GLOBAL CELL ; line 1\nSTORE_LOCAL hidden_cell ; line 1\n"
        "# source_debug 2 \"/source/reference.ny\" 1 \"amount\" \"hidden_cell\" \"binding_ref<" +
        scalar_type + ">\" 1\nNOP ; line 2\nRET ; line 3\nend function\n";
    TempNilFile fixture("binding-reference-" + scalar_type, code);
    Debugger debugger;
    require(debugger.load_program(fixture.path().string()), "could not load reference inspection fixture");
    StructValue cell;
    cell.type_name = "$nil.binding.cell." + scalar_type;
    if (scalar_type == "f64") cell.double_fields()["value"] = 1.25;
    else if (scalar_type == "text") cell.string_fields()["value"] = "hello";
    else cell.fields()["value"] = scalar_type == "bool" ? 1 : 42;
    auto& vm = debugger.get_vm();
    vm.resetExecution("main");
    vm.setGlobal("CELL", Value{cell});
    for (int step = 0; step < 8 && vm.getProgram().at(vm.getPC()).op != ny::Opcode::NOP; ++step)
      require(vm.stepInstruction(), "reference inspection fixture ended before its source stop");
    const auto locals = debugger.get_source_locals();
    require(locals.size() == 1 && locals[0].name == "amount" && locals[0].type == scalar_type,
            "source reference inspection exposed its internal cell type");
    const auto expected = scalar_type == "f64" ? "1.25" : scalar_type == "text" ? "\"hello\"" :
                          scalar_type == "bool" ? "1" : "42";
    require(debugger.evaluate_expression("amount") == expected,
            "source reference expression did not unwrap its scalar value");
    require(debugger.get_locals_json().find("$nil.binding.cell.") == std::string::npos,
            "source locals JSON leaked its reference cell");
    if (scalar_type == "i64") {
      cell.fields()["value"] = 43;
      require(debugger.evaluate_expression("amount") == "43", "reference inspection cached a stale value");
    }
    cell.type_name = "OrdinaryRecord";
    vm.resetExecution("main");
    vm.setGlobal("CELL", Value{cell});
    for (int step = 0; step < 8 && vm.getProgram().at(vm.getPC()).op != ny::Opcode::NOP; ++step)
      require(vm.stepInstruction(), "record inspection fixture ended before its source stop");
    require(std::holds_alternative<StructValue>(debugger.get_source_locals().at(0).value),
            "reference metadata incorrectly unwrapped an unrelated record");
  }
}

void test_vm_input_routing() {
  std::istringstream source(R"NIL(module input_routing
function main:
    INTEROP_CALL intrinsic console.read_line 0 text ; line 1
    STORE_GLOBAL INPUT ; line 1
    RET ; line 2
end function
)NIL");
  std::unordered_map<std::string, Value> constants;
  const std::vector<Instruction> program = ny::parseProgram(source, constants);

  VM vm;
  vm.loadProgram(program, constants);
  std::istringstream input("routed input\n");
  vm.setInputStream(input);
  vm.execute(true);
  require(vm.hasGlobal("INPUT") &&
              std::holds_alternative<std::string>(vm.getGlobal("INPUT")) &&
              std::get<std::string>(vm.getGlobal("INPUT")) == "routed input",
          "VM console.read_line ignored the host input stream");

  VM eof_vm;
  eof_vm.loadProgram(program, constants);
  std::istringstream eof_input;
  eof_vm.setInputStream(eof_input);
  bool failed_explicitly = false;
  try {
    eof_vm.execute(true);
  } catch (const std::runtime_error &error) {
    failed_explicitly =
        std::string(error.what()).find("input is unavailable or reached EOF") !=
        std::string::npos;
  }
  require(failed_explicitly, "VM EOF did not produce an explicit input error");
}

void test_execute_true_preserves_injected_globals() {
  std::istringstream input(R"NIL(module embedding_test
function main:
    LOAD_GLOBAL HOST ; line 1
    RET ; line 1
end function
)NIL");
  std::unordered_map<std::string, Value> constants;
  const std::vector<Instruction> program = ny::parseProgram(input, constants);
  VM vm;
  vm.loadProgram(program, constants);
  vm.setGlobal("HOST", int64_t{73});
  vm.execute(true);
  require(vm.getStack().size() == 1 &&
              std::holds_alternative<int64_t>(vm.getStack().back()) &&
              std::get<int64_t>(vm.getStack().back()) == 73,
          "execute(true) erased a host-injected global");
}

void test_rejects_missing_main_and_v2_library(const std::string &valid_path) {
  TempNilFile no_main(
      "no-main",
      "module no_main\nfunction helper:\n    RET ; line 1\nend function\n");
  Debugger debugger;
  require(debugger.load_program(valid_path),
          "debugger did not load before failed-reload regression");
  require(!debugger.load_program(no_main.path().string()),
          "debugger accepted a runnable artifact without main");
  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated &&
              debugger.evaluate_expression("G") == "7",
          "failed reload corrupted the previously loaded program");

  const std::vector<std::string> accepted_magics = {
      std::string("NIL\0", 4), std::string("MIL\0", 4),
      std::string("\0LIM", 4)};
  for (std::size_t index = 0; index < accepted_magics.size(); ++index) {
    TempNilFile nil_library(
        "library-" + std::to_string(index),
        binary_library_with_main(accepted_magics[index]));
    require(!debugger.load_program(nil_library.path().string()),
            "debugger accepted a runnable NIL v2 library with binary magic " +
                std::to_string(index));
  }
  ny::nil_module class_library;
  class_library.module_name = "class_library";
  class_library.is_library = true;
  class_library.structs.push_back({"Item", {}});
  class_library.classes.push_back({"Item", false, {}, {}});
  class_library.embedded_sources.push_back({"class_library.ny", "module class_library\n"});
  ny::nil_function main;
  main.name = "main";
  main.return_type = "void";
  main.body.push_back({"RET", {}});
  class_library.functions.push_back(main);
  std::ostringstream binary;
  ny::codegen_options options;
  ny::nil_binary_writer writer(binary, options);
  writer.emit_binary_module(class_library);
  // Validate the fixture independently: rejection must concern artifact kind,
  // not an accidentally malformed v4 header or declaration.
  std::istringstream metadata(binary.str());
  require(ny::parseBinaryLibraryMetadata(metadata).module_name == "class_library",
          "v4 class-library fixture is not a valid semantic capsule");
  TempNilFile v4_library("class-library", binary.str());
  require(!debugger.load_program(v4_library.path().string()),
          "debugger accepted a v4 class library with a main function");
  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated &&
              debugger.evaluate_expression("G") == "7",
          "rejected NIL library corrupted the previously loaded program");
}

void test_secure_read_rejection_preserves_loaded_program(
    const std::string &valid_path) {
  Debugger debugger;
  require(debugger.load_program(valid_path),
          "debugger did not load before secure-read reload regression");
  const auto program_size = debugger.get_vm().getProgram().size();

  const auto directory = std::filesystem::path(valid_path).parent_path();
  require(!debugger.load_program(directory.string()),
          "debugger accepted a directory as a NIL program");
  require(debugger.get_vm().getProgram().size() == program_size,
          "failed secure-read reload replaced the loaded program");
  require(debugger.get_current_file() == valid_path,
          "failed secure-read reload replaced the current program path");

  const std::string embedded_nul_path =
      valid_path + std::string("\0ignored-suffix", 15);
  require(!debugger.load_program(embedded_nul_path),
          "debugger accepted a path containing an embedded NUL");
  require(debugger.get_vm().getProgram().size() == program_size &&
              debugger.get_current_file() == valid_path,
          "invalid-path reload replaced the loaded program or its path");

  debugger.run();
  require(debugger.get_state() == ExecutionState::Terminated &&
              debugger.evaluate_expression("G") == "7",
          "failed secure-read reload corrupted the loaded program");
}

} // namespace

int main() {
  try {
    TempNilFile program("core", kProgram);
    const std::string path = program.path().string();
    test_first_run_and_clean_restart(path);
    test_real_frames_and_locals(path);
    test_selected_frame_lookup_is_lexical();
    test_global_eval_and_false_condition(path);
    test_condition_language_validation(path);
    test_breakpoint_resolution_failures(path);
    test_exact_typed_conditions();
    test_per_vm_callback_isolation(path);
    test_step_over_out_and_frame_cleanup(path);
    test_json_value_serialization();
    test_source_binding_reference_inspection();
    test_vm_input_routing();
    test_execute_true_preserves_injected_globals();
    test_rejects_missing_main_and_v2_library(path);
    test_secure_read_rejection_preserves_loaded_program(path);
    std::cout << "nildebugger core regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "nildebugger core regression failed: " << error.what() << '\n';
    return 1;
  }
}

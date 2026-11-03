#include "codegen.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
using namespace std::chrono_literals;
unsigned checks = 0;
void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <typename Action> void rejected(Action action, const std::string &text) {
  bool failed = false;
  try { action(); }
  catch (const std::runtime_error &error) {
    failed = std::string(error.what()).find(text) != std::string::npos;
  }
  require(failed, ("expected rejection: " + text).c_str());
}
void load(ny::VM &vm, const std::string &text) {
  std::istringstream source(text);
  std::unordered_map<std::string, ny::Value> constants;
  const auto program = ny::parseProgram(source, constants);
  vm.loadProgram(program, constants);
}
void load(ny::VM &vm, const ny::nil_module &module, bool binary) {
  std::ostringstream serialized(std::ios::out | std::ios::binary);
  if (binary) {
    ny::codegen_options options;
    ny::nil_binary_writer writer(serialized, options);
    writer.emit_binary_module(module);
  } else {
    ny::nil_writer writer(serialized);
    writer.emit_text_module(module);
  }
  std::istringstream input(serialized.str(), std::ios::in | std::ios::binary);
  std::unordered_map<std::string, ny::Value> constants;
  const auto program = binary ? ny::parseBinaryProgram(input, constants)
                              : ny::parseProgram(input, constants);
  vm.loadProgram(program, constants);
}
ny::nil_function function(const std::string &name,
                         std::vector<ny::nil_instruction> body,
                         const std::string &result = "i64") {
  ny::nil_function fn;
  fn.name = name;
  fn.return_type = result;
  fn.body = std::move(body);
  return fn;
}
const char *simple_program = R"(
module control
function main() -> i64:
  PUSH_CONST 11
  STORE_GLOBAL observed
  PUSH_CONST 41
  STORE_LOCAL outer
  CALL worker 0
  RET
end function
function worker() -> i64:
  PUSH_CONST 7
  STORE_LOCAL inner
  LOAD_GLOBAL observed
  PUSH_CONST 1
  ADD
  STORE_GLOBAL observed
  PUSH_CONST 42
  RET
end function
)";
const char *tiny_program = R"(
module tiny
function main() -> i64:
  PUSH_CONST 99
  RET
end function
)";
template <typename Predicate> void until(ny::VM &vm, Predicate predicate) {
  for (unsigned steps = 0; steps < 1000; ++steps) {
    require(vm.getPC() < vm.getProgram().size(), "target boundary was not reached");
    if (predicate(vm.getProgram()[vm.getPC()])) return;
    vm.stepInstruction();
  }
  throw std::runtime_error("instruction bound exceeded");
}
std::int64_t number(const ny::Value &value) { return std::get<std::int64_t>(value); }

void control_boundaries_and_inspection() {
  ny::VM vm;
  load(vm, simple_program);
  const auto owner = std::this_thread::get_id();
  std::vector<size_t> execution_boundaries, stepped_boundaries;
  bool inspected = false;
  vm.setControlCallback([&](ny::VM &state) {
    require(std::this_thread::get_id() == owner, "control left the execution owner thread");
    execution_boundaries.push_back(state.getPC());
    if (state.getProgram()[state.getPC()].op != ny::Opcode::LOAD_GLOBAL) return;
    const auto frames = state.getDebugFrames(2, 2);
    require(frames.size() == 2 && frames[0].function_name == "main" &&
            frames[1].function_name == "worker", "inspection did not expose actual nested frames");
    require(number(frames[0].locals.at("outer")) == 41 &&
            number(frames[1].locals.at("inner")) == 7,
            "inspection lost actual local values");
    require(number(state.getGlobals().at("observed")) == 11 && state.getStack().empty(),
            "inspection was not at the exact instruction boundary");
    const auto pc = state.getPC();
    rejected([&] { state.getDebugFrames(1, 2); }, "frame limit");
    rejected([&] { state.getDebugFrames(2, 1); }, "local limit");
    unsigned visits = 0;
    state.visitDebugFrames(2, 2, [&](const auto &name, size_t, const auto &locals) {
      ++visits;
      require(name == (visits == 1 ? "main" : "worker") && locals.size() == 1,
              "noncopying frame visitor lost actual state");
    });
    require(visits == 2, "noncopying visitor omitted a frame");
    rejected([&] {
      state.visitDebugFrames(2, 1, [&](const auto &, size_t, const auto &) { ++visits; });
    }, "local limit");
    require(visits == 2, "visitor published partial frames before validating all bounds");
    require(state.getPC() == pc && number(state.getGlobal("observed")) == 11,
            "failed inspection modified execution");
    inspected = true;
  });
  vm.execute();
  require(inspected && vm.getProgramExitCode() == 42 && number(vm.getGlobal("observed")) == 12,
          "control callback altered program behavior");
  vm.resetExecution("main");
  vm.setControlCallback([&](ny::VM &state) { stepped_boundaries.push_back(state.getPC()); });
  while (vm.getPC() < vm.getProgram().size()) vm.stepInstruction();
  require(execution_boundaries == stepped_boundaries, "execute and step control boundaries differ");

  // Preserve the legacy debugger path where the step callback returns true and
  // execution immediately continues in the same VM loop iteration.
  stepped_boundaries.clear();
  vm.resetExecution("main");
  unsigned debugger_calls = 0;
  vm.setDebugCallback([&](ny::VM &, const ny::Instruction &, size_t) {
    ++debugger_calls;
    return true;
  });
  vm.setDebugState(ny::VM::DebugState::STEPPING);
  vm.execute(false);
  require(debugger_calls == 1 && execution_boundaries == stepped_boundaries,
          "control changed debugger continuation or missed its second instruction");

  unsigned replacement_calls = 0;
  vm.resetExecution("main");
  vm.setControlCallback([&](ny::VM &state) {
    if (++replacement_calls == 1) {
      load(state, tiny_program);
      state.resetExecution("main");
    }
  });
  vm.execute(false);
  require(vm.getProgramExitCode() == 99 && replacement_calls > 1,
          "program replacement retained a stale instruction or lost host control");
  vm.resetExecution("main");
  vm.setControlCallback([](ny::VM &state) { state.loadProgram({}, {}); });
  require(!vm.stepInstruction() && vm.getProgram().empty(),
          "step indexed an instruction after terminal program replacement");
  load(vm, tiny_program);
  vm.execute();
  require(vm.getProgram().empty(), "execute indexed an instruction after terminal replacement");
}

void owner_thread_pause() {
  ny::VM vm;
  load(vm, simple_program);
  std::mutex mutex;
  std::condition_variable changed;
  bool paused = false, release = false, completed = false, pause_timeout = false;
  std::exception_ptr failure;
  std::int64_t observed = -1;
  vm.setControlCallback([&](ny::VM &state) {
    if (state.getProgram()[state.getPC()].op != ny::Opcode::LOAD_GLOBAL) return;
    std::unique_lock<std::mutex> lock(mutex);
    observed = number(state.getGlobal("observed"));
    paused = true;
    changed.notify_all();
    pause_timeout = !changed.wait_for(lock, 2s, [&] { return release; });
  });
  std::thread worker([&] {
    try { vm.execute(); } catch (...) { failure = std::current_exception(); }
    std::lock_guard<std::mutex> lock(mutex);
    completed = true;
    changed.notify_all();
  });
  bool reached_pause = false, advanced_while_paused = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    reached_pause = changed.wait_for(lock, 2s, [&] { return paused || completed; }) && paused;
    if (reached_pause)
      advanced_while_paused = changed.wait_for(lock, 30ms, [&] { return completed; });
    release = true;
    changed.notify_all();
  }
  worker.join();
  if (failure) std::rethrow_exception(failure);
  require(reached_pause && !advanced_while_paused && !pause_timeout && observed == 11,
          "owner-thread pause did not stop at the acknowledged boundary");
  require(vm.getProgramExitCode() == 42 && number(vm.getGlobal("observed")) == 12,
          "owner-thread resume did not continue from the paused boundary");
}

void nested_frame_checkpoint() {
  ny::VM vm;
  load(vm, simple_program);
  vm.setArgs({"first", u8"caf\u00e9"});
  vm.setMemoryMode(ny::MemoryMode::SAFE_GC);
  vm.setStrictBounds(true);
  vm.resetExecution("main");
  until(vm, [](const auto &instruction) { return instruction.op == ny::Opcode::LOAD_GLOBAL; });
  const auto saved_pc = vm.getPC();
  const auto checkpoint = vm.createCheckpoint();
  require(checkpoint.valid() && checkpoint.retainedBytes() > 0 &&
          checkpoint.retainedBytes() <= 16 * 1024 * 1024, "invalid checkpoint accounting");
  vm.execute(false);
  require(vm.getProgramExitCode() == 42 && number(vm.getGlobal("observed")) == 12,
          "initial checkpoint continuation failed");
  for (unsigned repeat = 0; repeat < 3; ++repeat) {
    vm.setArgs({"changed"});
    vm.setStrictBounds(false);
    vm.setMemoryMode(ny::MemoryMode::UNSAFE_MANUAL);
    vm.restoreCheckpoint(checkpoint);
    const auto frames = vm.getDebugFrames(2, 2);
    require(vm.getPC() == saved_pc && frames.size() == 2 &&
            number(frames[1].locals.at("inner")) == 7 &&
            number(vm.getGlobal("observed")) == 11 && vm.getStack().empty(),
            "checkpoint did not restore the exact nested invocation");
    require(vm.getArgs() == std::vector<std::string>({"first", u8"caf\u00e9"}) &&
            vm.strictBounds() && vm.getMemoryMode() == ny::MemoryMode::SAFE_GC,
            "checkpoint lost execution-affecting configuration");
    vm.execute(false);
    require(vm.getProgramExitCode() == 42 && number(vm.getGlobal("observed")) == 12,
            "reusable checkpoint was modified by a previous continuation");
  }
  const auto completed = vm.createCheckpoint();
  vm.resetExecution("main");
  vm.restoreCheckpoint(completed);
  require(vm.getPC() >= vm.getProgram().size() && vm.getProgramExitCode() == 42,
          "completed execution checkpoint lost its terminal state");
}

void heap_and_owned_checkpoint(bool binary) {
  ny::nil_module module;
  module.module_name = "checkpoint_owned";
  module.functions.push_back(function("main", {
      {"OWN_ENTER", {}}, {"PUSH_CONST", {"1"}}, {"OWN_ALLOC", {"i64"}},
      {"STORE_LOCAL", {"pointer"}}, {"OWN_BIND", {"pointer"}},
      {"LOAD_LOCAL", {"pointer"}}, {"PUSH_CONST", {"23"}}, {"WRITE_PTR", {}},
      {"LOAD_LOCAL", {"pointer"}}, {"READ_PTR", {}}, {"STORE_GLOBAL", {"before"}},
      {"LOAD_LOCAL", {"pointer"}}, {"PUSH_CONST", {"99"}}, {"WRITE_PTR", {}},
      {"LOAD_LOCAL", {"pointer"}}, {"READ_PTR", {}}, {"STORE_GLOBAL", {"after"}},
      {"PUSH_CONST", {"1"}}, {"ALLOC", {"i64"}}, {"STORE_GLOBAL", {"next"}},
      {"OWN_LEAVE", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}
  }));
  ny::VM vm;
  load(vm, module, binary);
  vm.resetExecution("main");
  until(vm, [](const auto &instruction) { return instruction.op == ny::Opcode::READ_PTR; });
  require(vm.getStack().size() == 1, "pointer operand absent at checkpoint boundary");
  const auto pointer = number(vm.getStack().back());
  const auto checkpoint = vm.createCheckpoint();
  vm.execute(false);
  const auto next = number(vm.getGlobal("next"));
  for (unsigned repeat = 0; repeat < 3; ++repeat) {
    vm.restoreCheckpoint(checkpoint);
    require(vm.getStack().size() == 1 && number(vm.getStack().back()) == pointer,
            "checkpoint lost heap pointer operand identity");
    vm.execute(false);
    require(number(vm.getGlobal("before")) == 23 && number(vm.getGlobal("after")) == 99 &&
            number(vm.getGlobal("next")) == next && vm.getProgramExitCode() == 0,
            "heap contents, allocation identity, or affine ownership failed after restore");
  }
}

void fault_checkpoint(bool binary) {
  ny::nil_module module;
  module.module_name = "checkpoint_fault";
  module.structs.push_back({"problem::Fault", {{"value", "i64"}}});
  module.functions.push_back(function("main", {
      {"FAULT_ENTER", {"outer", "dispatch"}}, {"CALL", {"explode", "0"}},
      {"FAULT_LEAVE", {"outer"}}, {"PUSH_CONST", {"0"}}, {"RET", {}},
      {"LABEL", {"dispatch"}}, {"FAULT_SAVE", {"saved"}},
      {"FAULT_BIND", {"saved", "problem::Fault"}}, {"GET_FIELD", {"value"}},
      {"STORE_GLOBAL", {"caught"}}, {"FAULT_DROP", {"saved"}},
      {"PUSH_CONST", {"99"}}, {"RET", {}}
  }));
  module.functions.push_back(function("explode", {
      {"NEW_STRUCT", {"problem::Fault"}}, {"PUSH_CONST", {"42"}},
      {"SET_FIELD", {"value"}}, {"FAULT_RAISE", {"problem::Fault"}, 71}
  }, "void"));
  for (const auto target : {ny::Opcode::CALL, ny::Opcode::FAULT_SAVE, ny::Opcode::FAULT_BIND}) {
    ny::VM vm;
    load(vm, module, binary);
    vm.resetExecution("main");
    until(vm, [&](const auto &instruction) { return instruction.op == target; });
    const auto checkpoint = vm.createCheckpoint();
    vm.execute(false);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
      vm.restoreCheckpoint(checkpoint);
      vm.execute(false);
      require(vm.getProgramExitCode() == 99 && number(vm.getGlobal("caught")) == 42,
              "active region, pending fault, or saved fault did not survive restore");
    }
  }
}

void aggregate_aliases_and_rejections() {
  ny::VM vm;
  load(vm, tiny_program);
  ny::StructValue original;
  original.type_name = "record";
  original.fields()["number"] = 17;
  original.string_fields()["bytes"] = std::string("a\0\xff", 3);
  vm.setGlobal("first", original);
  vm.setGlobal("second", original);
  ny::ArrayValue array;
  array.elements.push_back(original);
  vm.setGlobal("array", array);
  auto shared_enum = std::make_shared<ny::EnumValue>();
  shared_enum->variant_name = "Some";
  shared_enum->payloads.push_back(std::int64_t{27});
  ny::EnumValue outer_enum;
  outer_enum.variant_name = "Pair";
  outer_enum.payloads = {shared_enum, shared_enum};
  vm.setGlobal("enum", outer_enum);
  const auto checkpoint = vm.createCheckpoint();
  original.fields()["number"] = 91;
  shared_enum->payloads[0] = std::int64_t{92};
  vm.restoreCheckpoint(checkpoint);
  auto first = std::get<ny::StructValue>(vm.getGlobal("first"));
  auto second = std::get<ny::StructValue>(vm.getGlobal("second"));
  auto restored_array = std::get<ny::ArrayValue>(vm.getGlobal("array"));
  require(first.data == second.data && first.data != original.data &&
          std::get<ny::StructValue>(restored_array.elements[0]).data == first.data &&
          first.fields().at("number") == 17 &&
          first.string_fields().at("bytes") == std::string("a\0\xff", 3),
          "checkpoint did not deep-copy values while preserving aliases and raw bytes");
  auto restored_enum = std::get<ny::EnumValue>(vm.getGlobal("enum"));
  auto left = std::get<std::shared_ptr<ny::EnumValue>>(restored_enum.payloads[0]);
  auto right = std::get<std::shared_ptr<ny::EnumValue>>(restored_enum.payloads[1]);
  require(left == right && left != shared_enum && std::get<std::int64_t>(left->payloads[0]) == 27,
          "checkpoint lost shared enum identity or retained original mutable state");
  first.fields()["number"] = 77;
  vm.restoreCheckpoint(checkpoint);
  require(std::get<ny::StructValue>(vm.getGlobal("first")).fields().at("number") == 17,
          "restored mutable objects edited the saved checkpoint");

  ny::VM rejected_vm;
  load(rejected_vm, tiny_program);
  rejected_vm.setGlobal("foreign", ny::ny::NyForeignFunc{});
  rejected([&] { rejected_vm.createCheckpoint(); }, "foreign functions");
  load(rejected_vm, tiny_program);
  rejected_vm.setGlobal("boxed", ny::BoxedValue{});
  rejected([&] { rejected_vm.createCheckpoint(); }, "boxed values");
  load(rejected_vm, tiny_program);
  ny::StructValue cycle;
  cycle.struct_fields()["self"] = std::make_shared<ny::StructValue>(cycle);
  rejected_vm.setGlobal("cycle", cycle);
  rejected([&] { rejected_vm.createCheckpoint(); }, "cyclic aggregates");
  cycle.struct_fields().clear();
  load(rejected_vm, tiny_program);
  auto enum_cycle = std::make_shared<ny::EnumValue>();
  enum_cycle->payloads.push_back(enum_cycle);
  ny::EnumValue root;
  root.payloads.push_back(enum_cycle);
  rejected_vm.setGlobal("cycle", root);
  rejected([&] { rejected_vm.createCheckpoint(); }, "cyclic aggregates");
  enum_cycle->payloads.clear();
}

void checkpoint_admission_and_limits() {
  ny::VM vm, other;
  load(vm, tiny_program);
  load(other, tiny_program);
  const auto checkpoint = vm.createCheckpoint();
  const auto original_pc = vm.getPC();
  rejected([&] { vm.restoreCheckpoint({}); }, "empty");
  rejected([&] { other.restoreCheckpoint(checkpoint); }, "different VM");
  require(vm.getPC() == original_pc && other.getPC() == original_pc,
          "invalid restore mutated execution");
  load(vm, tiny_program);
  rejected([&] { vm.restoreCheckpoint(checkpoint); }, "different loaded program");
  rejected([&] { vm.createCheckpoint(0); }, "byte limit");
  rejected([&] { vm.createCheckpoint(16 * 1024 * 1024 + 1); }, "byte limit");
  vm.setGlobal("large", std::string(100000, 'x'));
  rejected([&] { vm.createCheckpoint(4096); }, "byte limit");
  require(std::get<std::string>(vm.getGlobal("large")).size() == 100000,
          "failed bounded capture modified live values");
  load(vm, tiny_program);
  ny::ArrayValue nested;
  for (unsigned level = 0; level < 70; ++level) {
    ny::ArrayValue parent;
    parent.elements.push_back(std::move(nested));
    nested = std::move(parent);
  }
  vm.setGlobal("deep", std::move(nested));
  rejected([&] { vm.createCheckpoint(); }, "depth/node limit");
  load(vm, tiny_program);
  ny::ArrayValue wide;
  wide.elements.resize(65537, std::int64_t{0});
  vm.setGlobal("wide", std::move(wide));
  rejected([&] { vm.createCheckpoint(); }, "depth/node limit");

  for (const auto &instruction : std::vector<std::string>{
           "CONSOLE_WRITE", "CONSOLE_WRITELN", "TIME_NOW", "TIME_SLEEP",
           "INTEROP_CALL intrinsic math.sqrt 1 f64", "CALL println 1"}) {
    load(vm, std::string("module denied\nfunction main() -> void:\n  ") + instruction +
             "\n  RET\nend function\n");
    rejected([&] { vm.createCheckpoint(); }, "checkpoint profile excludes");
  }
  load(vm, tiny_program);
  auto native_program = vm.getProgram();
  native_program.insert(native_program.begin() + 1,
                        {ny::Opcode::INLINE_ASM, {"x86_64", "unused", "0", "0"}});
  vm.loadProgram(native_program, {});
  rejected([&] { vm.createCheckpoint(); }, "checkpoint profile excludes");
  ny::nil_module parallel;
  parallel.module_name = "checkpoint_parallel_rejection";
  auto worker = function("worker", {{"STORE_LOCAL", {"value"}},
                                    {"LOAD_LOCAL", {"value"}}, {"RET", {}}});
  worker.params = {"value:i64"};
  parallel.functions = {worker, function("main", {
      {"PARALLEL_MAP", {"worker", "i64", "i64"}}, {"POP", {}},
      {"PUSH_CONST", {"0"}}, {"RET", {}}})};
  load(vm, parallel, false);
  rejected([&] { vm.createCheckpoint(); }, "checkpoint profile excludes");
  load(vm, tiny_program);
  const char *old_profile = std::getenv("NIL_MEMORY_PROFILE");
  const bool had_profile = old_profile != nullptr;
  const std::string old_value = old_profile ? old_profile : "";
#ifdef _WIN32
  require(_putenv_s("NIL_MEMORY_PROFILE", "checkpoint-test-must-not-be-opened") == 0,
          "could not set profile admission fixture");
#else
  require(setenv("NIL_MEMORY_PROFILE", "checkpoint-test-must-not-be-opened", 1) == 0,
          "could not set profile admission fixture");
#endif
  bool denied = false;
  try { vm.createCheckpoint(); }
  catch (const std::runtime_error &error) {
    denied = std::string(error.what()).find("memory profiling") != std::string::npos;
  }
#ifdef _WIN32
  const int restored = _putenv_s("NIL_MEMORY_PROFILE", had_profile ? old_value.c_str() : "");
#else
  const int restored = had_profile ? setenv("NIL_MEMORY_PROFILE", old_value.c_str(), 1)
                                   : unsetenv("NIL_MEMORY_PROFILE");
#endif
  require(restored == 0 && denied, "profiling admission failed or changed the environment");
}

void shared_subtree_depth_is_order_independent() {
  const auto chain = [](unsigned links, ny::StructValue tail) {
    for (unsigned link = 0; link < links; ++link) {
      ny::StructValue parent;
      parent.type_name = "chain";
      parent.struct_fields()["next"] = std::make_shared<ny::StructValue>(tail);
      tail = std::move(parent);
    }
    return tail;
  };
  const auto enum_chain = [](unsigned links, std::shared_ptr<ny::EnumValue> tail) {
    for (unsigned link = 0; link < links; ++link) {
      auto parent = std::make_shared<ny::EnumValue>();
      parent->variant_name = "Next";
      parent->payloads.push_back(tail);
      tail = std::move(parent);
    }
    return tail;
  };
  for (const bool reverse : {false, true}) {
    for (const bool too_deep : {false, true}) {
      ny::VM vm;
      load(vm, tiny_program);
      ny::StructValue leaf;
      leaf.fields()["value"] = 42;
      const auto tail = chain(8, leaf);
      const auto deep = chain(too_deep ? 24 : 23, tail);
      vm.setGlobal(reverse ? "deep" : "shallow", reverse ? deep : tail);
      vm.setGlobal(reverse ? "shallow" : "deep", reverse ? tail : deep);
      if (too_deep) {
        rejected([&] { vm.createCheckpoint(); }, "depth/node limit");
      } else {
        auto saved = vm.createCheckpoint();
        for (unsigned repeat = 0; repeat < 8; ++repeat) {
          vm.restoreCheckpoint(saved);
          auto restored_deep = std::get<ny::StructValue>(vm.getGlobal("deep"));
          for (unsigned link = 0; link < 23; ++link)
            restored_deep = *restored_deep.struct_fields().at("next");
          require(restored_deep.data == std::get<ny::StructValue>(vm.getGlobal("shallow")).data,
                  "near-limit shared struct alias changed across capture/restore order");
          saved = vm.createCheckpoint();
        }
      }

      load(vm, tiny_program);
      auto enum_leaf = std::make_shared<ny::EnumValue>();
      enum_leaf->variant_name = "Leaf";
      enum_leaf->payloads.push_back(std::int64_t{42});
      const auto enum_tail = enum_chain(8, enum_leaf);
      ny::EnumValue shallow_enum, deep_enum;
      shallow_enum.payloads.push_back(enum_tail);
      deep_enum.payloads.push_back(enum_chain(too_deep ? 23 : 22, enum_tail));
      vm.setGlobal(reverse ? "deep" : "shallow", reverse ? deep_enum : shallow_enum);
      vm.setGlobal(reverse ? "shallow" : "deep", reverse ? shallow_enum : deep_enum);
      if (too_deep) {
        rejected([&] { vm.createCheckpoint(); }, "depth/node limit");
      } else {
        auto saved = vm.createCheckpoint();
        for (unsigned repeat = 0; repeat < 8; ++repeat) {
          vm.restoreCheckpoint(saved);
          auto deep_value = std::get<ny::EnumValue>(vm.getGlobal("deep"));
          auto restored_deep = std::get<std::shared_ptr<ny::EnumValue>>(deep_value.payloads[0]);
          for (unsigned link = 0; link < 22; ++link)
            restored_deep = std::get<std::shared_ptr<ny::EnumValue>>(restored_deep->payloads[0]);
          const auto shallow = std::get<ny::EnumValue>(vm.getGlobal("shallow"));
          require(restored_deep == std::get<std::shared_ptr<ny::EnumValue>>(shallow.payloads[0]),
                  "near-limit shared enum alias changed across capture/restore order");
          saved = vm.createCheckpoint();
        }
      }
    }
  }
}
} // namespace

int main() try {
  control_boundaries_and_inspection();
  owner_thread_pause();
  nested_frame_checkpoint();
  for (const bool binary : {false, true}) {
    heap_and_owned_checkpoint(binary);
    fault_checkpoint(binary);
  }
  aggregate_aliases_and_rejections();
  checkpoint_admission_and_limits();
  shared_subtree_depth_is_order_independent();
  std::cout << "VM owner control and deterministic checkpoints: " << checks << " checks passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "VM control/checkpoint test failed: " << error.what() << '\n';
  return 1;
}

#include "parallel_support.hpp"
#include "concurrency_contract.hpp"
#include "codegen.hpp"
#include "native_runtime.hpp"
#include "vm.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::atomic<unsigned> entered{0};
std::atomic<unsigned> active{0};
std::atomic<unsigned> peak{0};
const void *parent_array = nullptr;

void rendezvous() {
  const auto current = active.fetch_add(1) + 1;
  unsigned observed = peak.load();
  while (observed < current && !peak.compare_exchange_weak(observed, current)) {}
  entered.fetch_add(1);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (entered.load() < 2) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("parallel workers did not overlap");
    std::this_thread::yield();
  }
  active.fetch_sub(1);
}

std::uint64_t native_worker(std::uint64_t value) {
  rendezvous();
  require(nyrt_array_length(parent_array, NYRT_ARRAY_I64) == -1,
          "native worker inherited a parent array handle");
  require(nyrt_arg_count() == 0, "native worker inherited parent process arguments");
  auto *local = nyrt_array_new_with_value(NYRT_ARRAY_I64, 1, value);
  require(local && nyrt_array_get(local, NYRT_ARRAY_I64, 0) == value,
          "native worker did not have an independent allocation context");
  return value * value;
}

std::uint64_t native_float(std::uint64_t word) {
  double value;
  std::memcpy(&value, &word, sizeof(value));
  value *= 2;
  std::memcpy(&word, &value, sizeof(value));
  return word;
}

ny::nil_module module() {
  ny::nil_module result;
  ny::nil_function worker;
  worker.name = "worker";
  worker.params = {"value:i64"};
  worker.return_type = "i64";
  worker.body = {{"STORE_LOCAL", {"value"}}, {"LOAD_LOCAL", {"value"}}, {"RET", {}}};
  ny::nil_function main;
  main.name = "main";
  main.return_type = "i32";
  main.body = {{"PARALLEL_MAP", {"worker", "i64", "i64"}}, {"POP", {}}, {"PUSH_CONST", {"0"}}, {"RET", {}}};
  result.functions = {worker, main};
  return result;
}

template <typename Change>
void reject(Change change) {
  auto invalid = module();
  change(invalid);
  try { ny::validate_nil_parallel_contract(invalid); }
  catch (const std::runtime_error &) { return; }
  throw std::runtime_error("invalid isolated worker contract was accepted");
}

void flat_annotation_rejections() {
  ny::Instruction worker{ny::Opcode::LABEL, {"worker"}};
  worker.is_function_entry = true;
  worker.function_param_count = 1;
  worker.function_parameter_types = {"i64"};
  worker.function_return_type = "i64";
  ny::Instruction end{ny::Opcode::LABEL, {"worker_end"}};
  end.is_function_end = true;
  ny::Instruction main{ny::Opcode::LABEL, {"main"}};
  main.is_function_entry = true;
  main.function_return_type = "i32";
  std::vector<ny::Instruction> program = {worker,
      {ny::Opcode::STORE_LOCAL, {"value"}}, {ny::Opcode::LOAD_LOCAL, {"value"}},
      {ny::Opcode::RET, {}}, end, main,
      {ny::Opcode::PARALLEL_MAP, {"worker", "i64", "i64"}},
      {ny::Opcode::POP, {}}, {ny::Opcode::PUSH_CONST, {"0"}}, {ny::Opcode::RET, {}}};
  ny::validate_nil_parallel_contract(program);
  for (const auto &annotation : {"@intrinsic(\"math.abs\")", "@interop(\"std_sys\")",
                                 " \t@intrinsic(\"math.abs\")", " \t@interop(\"std_sys\")"}) {
    program[0].function_annotations = {annotation};
    bool rejected = false;
    try { ny::validate_nil_parallel_contract(program); }
    catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "flat worker annotation bypassed effect validation");
  }
}
} // namespace

int main() try {
  std::vector<std::uint64_t> results(64);
  const auto parent = std::this_thread::get_id();
  ny::parallel_support::for_each(results.size(), [&](std::size_t, std::size_t index) {
    require(std::this_thread::get_id() != parent, "map ran work on the caller thread");
    rendezvous();
    results[index] = index * index;
  });
  require(active == 0 && peak >= 2, "structured workers did not overlap and join");
  for (std::size_t index = 0; index < results.size(); ++index)
    require(results[index] == index * index, "parallel result order changed");
  std::atomic<unsigned> completed{0};
  bool failed = false;
  try {
    ny::parallel_support::for_each(64, [&](std::size_t, std::size_t index) {
      completed.fetch_add(1);
      if (index == 7 || index == 31) throw std::runtime_error("deliberate worker failure");
    });
  } catch (const std::runtime_error &error) {
    failed = std::string(error.what()).find("input 7 failed") != std::string::npos;
  }
  require(failed && completed == 64, "worker failure did not join all tasks deterministically");
  ny::parallel_support::for_each(0, [](std::size_t, std::size_t) {
    throw std::runtime_error("empty map invoked a worker");
  });
  try {
    (void)ny::parallel_support::worker_count(ny::parallel_support::maximum_items + 1);
    throw std::logic_error("oversized map accepted");
  } catch (const std::runtime_error &) {}

  const char *arguments[] = {"not inherited"};
  nyrt_context_begin(1, arguments);
  auto *inputs = nyrt_array_new(NYRT_ARRAY_I64, 64);
  parent_array = inputs;
  for (std::int64_t index = 0; index < 64; ++index)
    require(nyrt_array_set(inputs, NYRT_ARRAY_I64, index, index), "cannot initialize native input");
  entered = 0;
  peak = 0;
  auto *output = nyrt_parallel_map_v1(inputs, NYRT_ARRAY_I64, NYRT_ARRAY_I64, native_worker);
  require(output && peak >= 2 && active == 0, "native map workers did not overlap and join");
  require(nyrt_arg_count() == 1, "native map damaged parent context");
  for (std::int64_t index = 0; index < 64; ++index)
    require(nyrt_array_get(output, NYRT_ARRAY_I64, index) == static_cast<std::uint64_t>(index * index),
            "native map result mismatch");
  auto *floats = nyrt_array_new(NYRT_ARRAY_F64, 1);
  double floating = 1.25;
  std::uint64_t word;
  std::memcpy(&word, &floating, sizeof(word));
  nyrt_array_set(floats, NYRT_ARRAY_F64, 0, word);
  auto *float_results = nyrt_parallel_map_v1(floats, NYRT_ARRAY_F64, NYRT_ARRAY_F64, native_float);
  word = nyrt_array_get(float_results, NYRT_ARRAY_F64, 0);
  std::memcpy(&floating, &word, sizeof(word));
  require(floating == 2.5, "native map lost floating-point bits");
  nyrt_context_end();

  ny::validate_nil_parallel_contract(module());
  for (const auto &operation : {"LOAD_GLOBAL", "STORE_GLOBAL", "INTEROP_CALL", "INLINE_ASM", "ALLOC",
       "READ_PTR", "WRITE_PTR", "REF_LOCAL", "NEW_STRUCT", "PUSH_CONST_STR", "PUSH_FN_PTR",
       "TIME_SLEEP", "TIME_NOW", "FAULT_RAISE", "OWN_ENTER", "OWN_MOVE", "PARALLEL_MAP"}) {
    reject([&](auto &value) { value.functions[0].body.insert(value.functions[0].body.begin(), {operation, {}}); });
  }
  reject([](auto &value) { value.functions[0].params = {"value:text"}; });
  reject([](auto &value) { value.functions[0].return_type = "ptr<i64>"; });
  reject([](auto &value) { value.functions[0].annotations = {"@intrinsic(\"math.abs\")"}; });
  reject([](auto &value) { value.functions[0].annotations = {"@interop(\"std_sys\")"}; });
  reject([](auto &value) { value.functions[0].annotations = {" \t@intrinsic(\"math.abs\")"}; });
  reject([](auto &value) { value.functions[0].annotations = {" \t@interop(\"std_sys\")"}; });
  reject([](auto &value) { value.functions[0].body.insert(value.functions[0].body.begin(), {"CALL", {"unknown", "0"}}); });
  reject([](auto &value) { value.functions[0].body.insert(value.functions[0].body.begin(), {"CAST", {"ptr<i64>"}}); });
  reject([](auto &value) { value.functions[1].body[0].args[2] = "i32"; });
  reject([](auto &value) { value.functions.push_back(value.functions[0]); });
  flat_annotation_rejections();
  std::cout << "parallel runtime: overlap, isolation, joined failures, scalar words and rejection contracts passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}

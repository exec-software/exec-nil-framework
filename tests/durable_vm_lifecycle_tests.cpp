// A lock must leave the actual operating system when its embedded VM invocation
// ends; process-exit cleanup alone is not sufficient for native embedding.
#include "codegen.hpp"
#include "nil_loader.hpp"
#include "secure_file_io.hpp"
#include "vm.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h> // Darwin declares mkdtemp here.
#endif

namespace {
namespace fs = std::filesystem;
unsigned checks = 0;

void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

class fixture_directory {
public:
  fixture_directory() {
#if defined(__unix__) || defined(__APPLE__)
    const auto pattern = (fs::temp_directory_path() / "nil-durable-vm-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const auto directory = ::mkdtemp(writable.data());
    require(directory != nullptr, "cannot create private VM lock fixture");
    root = fs::absolute(directory);
    lock = root / "invocation.lock";
    std::ofstream output(lock, std::ios::binary);
    output.close();
    require(static_cast<bool>(output) && ::chmod(lock.c_str(), 0600) == 0,
            "cannot create private VM lock file");
#endif
  }
  ~fixture_directory() {
    if (!root.empty()) {
      std::error_code ignored;
      fs::remove_all(root, ignored);
    }
  }
  fs::path root;
  fs::path lock;
};

const std::string token_result = "nil::std::core::Result<i64,text>";
const std::string bool_result = "nil::std::core::Result<bool,text>";

ny::nil_function function(std::string name, std::vector<std::string> parameters,
                         std::string result, std::vector<ny::nil_instruction> body) {
  ny::nil_function declaration;
  declaration.name = std::move(name);
  declaration.params = std::move(parameters);
  declaration.return_type = std::move(result);
  declaration.body = std::move(body);
  return declaration;
}

struct Program {
  std::vector<ny::Instruction> code;
  std::unordered_map<std::string, ny::Value> constants;
};

Program fixture(const fs::path &path, bool binary, bool fault) {
  ny::nil_module module;
  module.module_name = "durable_vm_lifecycle";
  module.enums = {{token_result, {{"Ok", "i64"}, {"Error", "text"}}},
                  {bool_result, {{"Ok", "bool"}, {"Error", "text"}}}};
  module.string_constants = {{"lock_path", path.u8string()}};
  std::vector<ny::nil_instruction> body{
      {"PUSH_CONST_STR", {"lock_path"}}, {"PUSH_CONST", {"0"}}, {"CAST", {"bool"}},
      {"INTEROP_CALL", {"intrinsic", "file.acquire_file_lock", "2", token_result}},
      {"UNWRAP_VARIANT", {"1", token_result, "i64"}},
      {"STORE_GLOBAL", {"opened", "i64"}},
      {"LOAD_GLOBAL", {"opened", "i64"}},
      {"INTEROP_CALL", {"intrinsic", "file.file_lock_is_held", "1", bool_result}},
      {"UNWRAP_VARIANT", {"1", bool_result, "bool"}},
      {"STORE_GLOBAL", {"held_during", "bool"}}};
  if (fault) {
    body.push_back({"PUSH_CONST", {"1"}});
    body.push_back({"PUSH_CONST", {"0"}});
    body.push_back({"DIV", {"i64"}});
  } else {
    body.push_back({"PUSH_CONST", {"0"}});
  }
  body.push_back({"RET", {}});
  module.functions.push_back(function("main", {}, "i64", std::move(body)));
  module.functions.push_back(function("release", {"token:i64"}, bool_result,
      {{"STORE_LOCAL", {"token"}}, {"LOAD_LOCAL", {"token"}},
       {"INTEROP_CALL", {"intrinsic", "file.release_file_lock", "1", bool_result}},
       {"RET", {}}}));
  module.functions.push_back(function("held", {"token:i64"}, bool_result,
      {{"STORE_LOCAL", {"token"}}, {"LOAD_LOCAL", {"token"}},
       {"INTEROP_CALL", {"intrinsic", "file.file_lock_is_held", "1", bool_result}},
       {"RET", {}}}));
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
  Program program;
  program.code = binary ? ny::parseBinaryProgram(input, program.constants)
                        : ny::parseProgram(input, program.constants);
  return program;
}

void endpoint(const fs::path &path, bool held, const std::string &phase) {
  ny::detail::FileLockRegistry external;
  const auto probe = external.acquire(path.u8string(), false);
  require(probe.ok != held, phase + (held ? " released its actual lock early"
                                        : " retained its actual operating-system lock"));
  if (held)
    require(probe.error.find("lock is busy") != std::string::npos,
            phase + " failed for an unrelated filesystem reason");
}

std::int64_t pause(ny::VM &vm, const Program &program, const fs::path &path) {
  vm.loadProgram(program.code, program.constants);
  vm.resetExecution("main");
  for (int step = 0; step < 50 && !vm.hasGlobal("held_during"); ++step)
    require(vm.stepInstruction(), "invocation did not reach its lock");
  require(vm.hasGlobal("opened"), "fixture did not retain its lock token");
  const auto token = std::get<std::int64_t>(vm.getGlobal("opened"));
  require(token > 0, "lock token must be positive");
  require(vm.hasGlobal("held_during") &&
              std::get<std::int64_t>(vm.getGlobal("held_during")) == 1,
          "active invocation did not report ownership of its lock");
  endpoint(path, true, "paused invocation");
  return token;
}

void stale(ny::VM &vm, std::int64_t token) {
  vm.pushValue(token);
  vm.callFunction("held", 1);
  require(!vm.getStack().empty(), "ownership query did not return a checked result");
  const auto &held = std::get<ny::EnumValue>(vm.getStack().back());
  require(held.variant_name == "Result::Ok" && held.payloads.size() == 1 &&
              std::get<std::int64_t>(held.payloads.front()) == 0,
          "foreign or terminal token was reported as held");
  vm.pushValue(token);
  vm.callFunction("release", 1);
  require(!vm.getStack().empty(), "release did not return a checked result");
  const auto &result = std::get<ny::EnumValue>(vm.getStack().back());
  require(result.variant_name == "Result::Error", "foreign or terminal token was accepted");
  require(result.payloads.size() == 1 &&
              std::get<std::string>(result.payloads.front()) ==
                  "file.release_file_lock: invalid lock token",
          "stale token did not report its exact checked failure");
}

void lifecycle(const fs::path &path, bool binary) {
  const auto normal = fixture(path, binary, false);
  const auto faulty = fixture(path, binary, true);
  ny::VM vm;
  const auto first = pause(vm, normal, path);
  bool checkpoint_rejected = false;
  try {
    (void)vm.createCheckpoint();
  } catch (const std::runtime_error &error) {
    checkpoint_rejected = std::string(error.what()) ==
        "VM checkpoints do not support open file locks";
  }
  require(checkpoint_rejected, "paused checkpoint was not rejected specifically for its open lock");
  endpoint(path, true, "rejected checkpoint");
  {
    ny::VM foreign;
    foreign.loadProgram(normal.code, normal.constants);
    stale(foreign, first);
    endpoint(path, true, "foreign VM release attempt");
  }
  vm.execute(false);
  endpoint(path, false, "normal completion");
  stale(vm, first);

  const auto second = pause(vm, normal, path);
  require(second > first, "later invocation recycled a completed token");
  vm.resetExecution("main");
  endpoint(path, false, "debugger reset");
  // Reset prepares a new main frame. Replacing it before another host call
  // keeps this test within the embedding API's existing frame contract.
  vm.loadProgram(normal.code, normal.constants);
  stale(vm, second);

  const auto third = pause(vm, normal, path);
  require(third > second, "reset invocation recycled a token");
  vm.loadProgram(normal.code, normal.constants);
  endpoint(path, false, "program replacement");
  stale(vm, third);

  const auto fourth = pause(vm, faulty, path);
  require(fourth > third, "faulting invocation recycled a token");
  bool failed = false;
  try {
    vm.execute(false);
  } catch (const std::runtime_error &error) {
    failed = std::string(error.what()).find("Division by zero") != std::string::npos;
  }
  require(failed, "fault fixture did not execute its intended runtime error");
  endpoint(path, false, "failed invocation");
  vm.loadProgram(normal.code, normal.constants);
  stale(vm, fourth);

  std::int64_t destroyed_token = 0;
  {
    auto owned = std::make_unique<ny::VM>();
    destroyed_token = pause(*owned, normal, path);
    owned.reset();
  }
  endpoint(path, false, "paused VM destruction");
  ny::VM replacement;
  replacement.loadProgram(normal.code, normal.constants);
  stale(replacement, destroyed_token);
}
} // namespace

int main() {
  try {
#if defined(__unix__) || defined(__APPLE__)
    fixture_directory owned;
    lifecycle(owned.lock, false);
    lifecycle(owned.lock, true);
#endif
    std::cout << "Durable VM lifecycle: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Durable VM lifecycle failure: " << error.what() << '\n';
    return 1;
  }
}

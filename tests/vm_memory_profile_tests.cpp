#include "nil_loader.hpp"
#include "vm.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct Capture {
  std::filesystem::path directory;
  std::filesystem::path path;

  Capture() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt != 100; ++attempt) {
      auto candidate = std::filesystem::temp_directory_path() /
          ("nil-vm-profile-" + std::to_string(stamp) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(candidate)) {
        directory = std::move(candidate);
        break;
      }
    }
    require(!directory.empty(), "could not reserve test directory");
    path = directory / "capture.json";
#ifdef _WIN32
    require(_putenv_s("NIL_MEMORY_PROFILE", path.string().c_str()) == 0,
            "could not enable capture");
#else
    require(setenv("NIL_MEMORY_PROFILE", path.string().c_str(), 1) == 0,
            "could not enable capture");
#endif
  }

  ~Capture() {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }

  std::string read() const {
    std::ifstream input(path);
    require(input.good(), "capture was not created");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
};

void validate(const std::string &report, const char *status, bool completed) {
  require(report.find(std::string("\"status\":\"") + status + '"') != std::string::npos,
          "incorrect capture status");
  require(report.find(completed ? "\"completed\":true" : "\"completed\":false") !=
              std::string::npos,
          "incorrect capture completeness");
  const auto totals_start = report.find("\"totals\":{");
  require(totals_start != std::string::npos, "missing totals");
  const auto totals = report.substr(totals_start, report.find('}', totals_start) - totals_start);
  require(totals.find("\"live_bytes\":16,") != std::string::npos,
          "retained bytes were lost to VM teardown");
  require(totals.find("\"allocated_objects\":1,") != std::string::npos,
          "capture combined separate invocations");
  require(totals.find("\"freed_objects\":0,") != std::string::npos,
          "teardown was counted as an application release");
  require(report.find("\"file\":\"lifecycle.ny\"") != std::string::npos &&
              report.find("\"line\":17,") != std::string::npos,
          "allocation source attribution was lost");
}

void load(ny::VM &vm, bool fail) {
  std::istringstream source(fail ? R"(
module memory_profile
function main() -> i64:
  OWN_ENTER
  PUSH_CONST 4
  OWN_ALLOC i32
  STORE_LOCAL owner
  OWN_BIND owner
  PUSH_CONST 1
  PUSH_CONST 0
  DIV
  POP
  OWN_LEAVE
  PUSH_CONST 0
  RET
end function
)" : R"(
module memory_profile
function main() -> i64:
  PUSH_CONST 4
  ALLOC i32
  STORE_LOCAL buffer
  PUSH_CONST 0
  RET
end function
)");
  std::unordered_map<std::string, ny::Value> constants;
  auto program = ny::parseProgram(source, constants);
  for (auto &instruction : program) {
    if (instruction.op == ny::Opcode::ALLOC || instruction.op == ny::Opcode::OWN_ALLOC) {
      instruction.source_file = "lifecycle.ny";
      instruction.line = 17;
    }
  }
  vm.loadProgram(program, constants);
}

void pause_after_allocation(ny::VM &vm) {
  const auto &program = vm.getProgram();
  for (std::size_t index = 0; index != program.size(); ++index) {
    if (program[index].op == ny::Opcode::STORE_LOCAL) {
      vm.setBreakpoint(index + 1);
      vm.execute();
      require(vm.getPC() == index + 1, "VM did not pause after allocation");
      return;
    }
  }
  throw std::runtime_error("missing pause instruction");
}

} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "expected restart, failure, stepping, or abandoned mode");
  const std::string mode = argv[1];
  Capture capture;
  if (mode == "restart") {
    ny::VM vm;
    load(vm, false);
    pause_after_allocation(vm);
    const auto paused_pc = vm.getPC();
    vm.resetExecution("main");
    const auto report = capture.read();
    validate(report, "abandoned", false);
    vm.clearBreakpoint(paused_pc);
    vm.setDebugState(ny::VM::DebugState::RUNNING);
    vm.execute(false);
    require(capture.read() == report, "restart overwrote the first invocation's capture");
  } else if (mode == "failure") {
    ny::VM vm;
    load(vm, true);
    bool failed = false;
    try { vm.execute(); }
    catch (const std::runtime_error &) { failed = true; }
    require(failed, "division by zero did not fail");
    validate(capture.read(), "failed", false);
    const auto frames = vm.getDebugFrames();
    require(!frames.empty() && std::get<std::int64_t>(frames.back().locals.at("owner")) == 0,
            "failure no longer unwinds owned storage after the snapshot");
  } else if (mode == "stepping") {
    ny::VM vm;
    load(vm, false);
    vm.resetExecution("main");
    unsigned steps = 0;
    while (vm.getPC() < vm.getProgram().size()) {
      require(++steps < 20, "stepping did not terminate");
      vm.setDebugState(ny::VM::DebugState::STEPPING);
      vm.execute(false);
    }
    validate(capture.read(), "completed", true);
  } else if (mode == "abandoned") {
    {
      ny::VM vm;
      load(vm, false);
      pause_after_allocation(vm);
    }
    validate(capture.read(), "abandoned", false);
  } else {
    throw std::runtime_error("unknown test mode");
  }
  std::cout << "VM memory profile " << mode << " lifecycle passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "VM memory profile lifecycle failure: " << error.what() << '\n';
  return 1;
}

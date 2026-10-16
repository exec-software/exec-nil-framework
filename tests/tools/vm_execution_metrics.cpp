#include "nil_loader.hpp"
#include "vm.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

bool is_comparison(ny::Opcode opcode) {
  switch (opcode) {
  case ny::Opcode::CMP_EQ:
  case ny::Opcode::CMP_NE:
  case ny::Opcode::CMP_LT:
  case ny::Opcode::CMP_LE:
  case ny::Opcode::CMP_GT:
  case ny::Opcode::CMP_GE:
    return true;
  default:
    return false;
  }
}

std::uint64_t step_limit(const char *argument) {
  const std::string value(argument);
  if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("maximum steps must be a positive integer");
  const auto limit = std::stoull(value);
  if (limit == 0)
    throw std::runtime_error("maximum steps must be a positive integer");
  return limit;
}

} // namespace

// Test-only observer over the debugger's ordinary VM stepping API. Counts
// include the complete consumer program, including setup and verification;
// they are not a claim about isolated sorting comparisons or native timings.
int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "usage: ny_execution_metrics <textual.nil> [maximum-steps]\n";
    return 2;
  }
  try {
    const std::uint64_t limit = argc == 3 ? step_limit(argv[2]) : 50'000'000;
    std::ifstream input(argv[1]);
    if (!input)
      throw std::runtime_error("cannot open textual NIL input");
    std::unordered_map<std::string, ny::Value> constants;
    const auto program = ny::parseProgram(input, constants);
    ny::VM vm;
    vm.setMemoryMode(ny::MemoryMode::SAFE_GC);
    vm.setProcessExitTrapping(true);
    vm.setOutputStream(std::cerr);
    std::istringstream no_input;
    vm.setInputStream(no_input);
    vm.loadProgram(program, constants);
    vm.resetExecution("main");

    std::uint64_t steps = 0;
    std::uint64_t comparisons = 0;
    std::size_t max_call_depth = vm.getCallDepth();
    while (vm.getPC() < vm.getProgram().size()) {
      if (steps >= limit)
        throw std::runtime_error("VM execution exceeded the step budget");
      comparisons += is_comparison(vm.getProgram()[vm.getPC()].op) ? 1 : 0;
      ++steps;
      const bool remaining = vm.stepInstruction();
      max_call_depth = std::max(max_call_depth, vm.getCallDepth());
      if (!remaining)
        break;
    }
    const auto requested = vm.getRequestedExitCode();
    const std::int64_t exit_code = requested ? *requested : vm.getProgramExitCode();
    std::cout << "{\"steps\":" << steps << ",\"comparisons\":" << comparisons
              << ",\"max_call_depth\":" << max_call_depth
              << ",\"exit_code\":" << exit_code << "}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "VM metrics failed: " << error.what() << '\n';
    return 1;
  }
}

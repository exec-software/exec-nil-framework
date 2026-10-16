#include "inline_asm.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void expect_valid(const std::string &code, std::size_t inputs,
                  std::size_t outputs) {
  ++checks;
  const auto error = ny::inline_asm_contract_error("x86_64", code, inputs, outputs);
  if (!error.empty()) {
    ++failures;
    std::cerr << "FAIL: valid template rejected: " << code << ": " << error << '\n';
  }
}

void expect_invalid(const std::string &code, std::size_t inputs,
                    std::size_t outputs, const std::string &fragment = {}) {
  ++checks;
  const auto error = ny::inline_asm_contract_error("x86_64", code, inputs, outputs);
  if (error.empty() || error.find(fragment) == std::string::npos) {
    ++failures;
    std::cerr << "FAIL: invalid template accepted or wrong diagnostic: " << code
              << ": " << error << '\n';
  }
}

void test_positive_profile() {
  expect_valid("nop", 0, 0);
  expect_valid(" \t nop ;\n\r nop \t ; ", 8, 0);
  expect_valid("movq $1, $0\n\tbswapq $0", 1, 1);
  expect_valid("movq $1, $0; rolq $$8, $0", 1, 1);
  expect_valid("movq $1, $0; xorq $2, $0", 2, 1);
  expect_valid("movq $8, $0", 8, 1);
  expect_valid("movq $$9223372036854775807, $0", 0, 1);
  expect_valid("movq $$-9223372036854775808, $0", 0, 1);
  expect_valid("xorq $0,$0; movq $0,$0; nop", 0, 1);
  for (const std::string opcode : {"addq", "subq", "andq", "orq", "xorq"}) {
    expect_valid("movq $1,$0; " + opcode + " $2,$0", 2, 1);
    expect_valid("xorq $0,$0; " + opcode + " $$2147483647,$0", 0, 1);
    expect_valid("xorq $0,$0; " + opcode + " $$-2147483648,$0", 0, 1);
    expect_valid("movq $$1,$0; " + opcode + " $0,$0", 0, 1);
  }
  for (const std::string opcode : {"bswapq", "notq", "negq"})
    expect_valid("movq $1,$0; " + opcode + " $0", 1, 1);
  for (const std::string opcode : {"salq", "shlq", "shrq", "sarq", "rolq", "rorq"}) {
    expect_valid("movq $1,$0; " + opcode + " $$0,$0", 1, 1);
    expect_valid("movq $1,$0; " + opcode + " $$63,$0", 1, 1);
  }
}

void test_register_and_control_flow_boundaries() {
  for (const std::string code : {
           "ret", "jmp label", "call external", "syscall", "pushq %rax",
           "popq %rax", "mulq $1", "divq $1", "cpuid", "cld", "std",
           ".byte 0x90", ".quad 0", ".intel_syntax noprefix", "label: nop",
           "nop\nret", "nop; .byte 0xc3"}) {
    expect_invalid(code, 1, 1, "outside the bounded");
  }
  expect_invalid("movq %rax,$0", 0, 1, "source must be");
  expect_invalid("movq (%rax),$0", 0, 1, "source must be");
  expect_invalid("movq ($1),$0", 1, 1, "source must be");
  expect_invalid("movq external,$0", 0, 1, "source must be");
  expect_invalid("movq $1,%rax", 1, 1, "destination must be");
  expect_invalid("movq $1,($0)", 1, 1, "destination must be");
  expect_invalid("movq $1,$1", 1, 1, "inputs are read-only");
  expect_invalid("movq $1,$0; bswapq $1", 1, 1, "destination must be");
  expect_invalid("movq $1,$0; xorq $1,$1", 1, 1, "inputs are read-only");
  expect_invalid("nop %rax", 0, 0, "takes no operands");
}

void test_definite_output() {
  expect_invalid("nop", 0, 1, "must initialize");
  expect_invalid("movq $0,$0", 0, 1, "uninitialized");
  expect_invalid("addq $1,$0", 1, 1, "uninitialized");
  expect_invalid("xorq $1,$0", 1, 1, "uninitialized");
  expect_invalid("subq $0,$0", 0, 1, "uninitialized");
  expect_invalid("bswapq $0", 0, 1, "uninitialized");
  expect_invalid("notq $0", 0, 1, "uninitialized");
  expect_invalid("negq $0", 0, 1, "uninitialized");
  expect_invalid("shlq $$1,$0", 0, 1, "uninitialized");
  expect_invalid("movq $$0,$0", 1, 0, "require an output");
}

void test_operand_bounds() {
  expect_invalid("movq $2,$0", 1, 1, "out of range");
  expect_invalid("movq $9,$0", 8, 1, "out of range");
  expect_invalid("movq $10,$0", 8, 1, "out of range");
  expect_invalid("movq $00,$0", 8, 1, "out of range");
  expect_invalid("movq $x,$0", 1, 1, "placeholders");
  expect_invalid("movq ${1},$0", 1, 1, "placeholders");
  expect_invalid("movq $1,$", 1, 1, "incomplete");
  expect_invalid("movq $1", 1, 1, "requires source and output");
  expect_invalid("movq $1,$0,$0", 1, 1, "requires source and output");
  expect_invalid("movq ,$0", 1, 1, "source must be");
  expect_invalid("movq $1,", 1, 1, "destination must be");
  expect_invalid("movq $$,$0", 0, 1, "source must be");
  expect_invalid("movq $$-,$0", 0, 1, "source must be");
  expect_invalid("movq $$+1,$0", 0, 1, "source must be");
  expect_invalid("movq $$010,$0", 0, 1, "source must be");
  expect_invalid("movq $$0xff,$0", 0, 1, "source must be");
  expect_invalid("movq $$1+1,$0", 0, 1, "source must be");
  expect_invalid("movq $$external,$0", 0, 1, "source must be");
  expect_invalid("movq $$9223372036854775808,$0", 0, 1, "source must be");
  expect_invalid("movq $$-9223372036854775809,$0", 0, 1, "source must be");
  expect_invalid("movq $$18446744073709551616,$0", 0, 1, "source must be");
  expect_invalid("xorq $0,$0; addq $$2147483648,$0", 0, 1, "fit signed i32");
  expect_invalid("xorq $0,$0; andq $$-2147483649,$0", 0, 1, "fit signed i32");
  for (const std::string count : {"$$64", "$$-1", "$$-0", "$1", "$0"})
    expect_invalid("movq $1,$0; rolq " + count + ",$0", 1, 1, "range 0..63");
}

void test_metadata_bounds() {
  expect_invalid("", 0, 0, "1..65536");
  expect_invalid(std::string(65537, ' '), 0, 0, "1..65536");
  expect_invalid(std::string("nop\0ret", 7), 0, 0, "non-NUL");
  expect_invalid(" ;\n\t", 0, 0, "at least one instruction");
  expect_invalid("nop", 9, 0, "at most eight");
  expect_invalid("nop", 0, 2, "one output");
  expect_valid(std::string(65533, ' ') + "nop", 0, 0);
  ++checks;
  if (ny::inline_asm_contract_error("aarch64", "nop", 0, 0).find("explicit target") ==
      std::string::npos) {
    ++failures;
    std::cerr << "FAIL: non-x86_64 target not rejected\n";
  }
}

} // namespace

int main() {
  test_positive_profile();
  test_register_and_control_flow_boundaries();
  test_definite_output();
  test_operand_bounds();
  test_metadata_bounds();
  if (failures != 0)
    return 1;
  std::cout << "inline assembly contract checks passed: " << checks << '\n';
  return 0;
}

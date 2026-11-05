#include "inline_asm.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void expect_valid(const std::string &code, std::size_t inputs,
                  std::size_t outputs, const std::string &target = "x86_64") {
  ++checks;
  const auto error = ny::inline_asm_contract_error(target, code, inputs, outputs);
  if (!error.empty()) {
    ++failures;
    std::cerr << "FAIL: valid template rejected: " << code << ": " << error << '\n';
  }
}

void expect_invalid(const std::string &code, std::size_t inputs,
                    std::size_t outputs, const std::string &fragment = {},
                    const std::string &target = "x86_64") {
  ++checks;
  const auto error = ny::inline_asm_contract_error(target, code, inputs, outputs);
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
  for (const std::string target : {"", "arm64", "arm", "AARCH64", "x86", "unknown"}) {
    ++checks;
    if (ny::inline_asm_contract_error(target, "nop", 0, 0).find("explicit target") ==
        std::string::npos) {
      ++failures;
      std::cerr << "FAIL: unknown target not rejected: " << target << '\n';
    }
  }
}

void test_aarch64_profile() {
  const auto valid = [](const std::string &code, std::size_t inputs = 2,
                        std::size_t outputs = 1) {
    expect_valid(code, inputs, outputs, "aarch64");
  };
  const auto invalid = [](const std::string &code, const std::string &fragment,
                          std::size_t inputs = 2, std::size_t outputs = 1) {
    expect_invalid(code, inputs, outputs, fragment, "aarch64");
  };
  valid("nop", 0, 0);
  valid(" \t nop ;\n\r nop \t ; ", 8, 0);
  valid("mov $0, $8; rev $0, $0", 8);
  for (const std::string opcode : {"mov", "rev", "mvn", "neg"}) {
    valid(opcode + " $0,$1", 1);
    valid("mov $0,$1; " + opcode + " $0,$0", 1);
    invalid(opcode + " $0,$0", "uninitialized");
    invalid(opcode + " $1,$2", "inputs are read-only");
  }
  for (const std::string opcode : {"add", "sub", "and", "orr", "eor"}) {
    valid(opcode + " $0,$1,$2");
    valid("mov $0,$1; " + opcode + " $0,$0,$2");
    valid("mov $0,$1; " + opcode + " $0,$2,$0");
    valid("mov $0,$1; " + opcode + " $0,$0,$0");
    valid(opcode + " $0,$1,$1", 1);
    invalid(opcode + " $0,$0,$1", "uninitialized");
    invalid(opcode + " $0,$1,$0", "uninitialized");
    invalid(opcode + " $0,$0,$0", "uninitialized");
    invalid(opcode + " $1,$1,$2", "inputs are read-only");
    invalid(opcode + " $0,$1,#1", "register operand");
    invalid(opcode + " $0,$1,$2,lsl #1", "operand count");
  }
  for (const std::string opcode : {"lsl", "lsr", "asr", "ror"}) {
    for (const std::string count : {"0", "1", "31", "32", "63"}) {
      valid(opcode + " $0,$1,#" + count, 1);
      valid("mov $0,$1; " + opcode + " $0,$0,#" + count, 1);
    }
    for (const std::string count : {"#64", "#-1", "#-0", "#+1", "#01",
                                   "#0x1", "#1+1", "#", "#99999999999999999",
                                   "1", "$$1", "$1", "$0"})
      invalid(opcode + " $0,$1," + count, "decimal #0..#63");
    invalid(opcode + " $0,$0,#1", "uninitialized");
  }
  for (const std::string code : {
           "ret", "b label", "bl external", "br $1", "svc #0", "mrs $0,nzcv",
           "msr nzcv,$1", "ldr $0,[$1]", "str $1,[$0]", "stp $1,$2,[sp]",
           "adds $0,$1,$2", "subs $0,$1,$2", "ands $0,$1,$2", "negs $0,$1",
           "adc $0,$1,$2", "cmp $1,$2", "csel $0,$1,$2,eq", "mul $0,$1,$2",
           "movz $0,#1", "eon $0,$1,$2", "rev32 $0,$1", "rorv $0,$1,$2",
           ".byte 0", ".inst 0", "label: nop", "nop; ret", "movq $1,$0"})
    invalid(code, "outside the bounded");
  for (const std::string operand : {"x0", "w0", "sp", "xzr", "[$1]", "($1)",
                                    "#1", "$$1", "symbol", "$1+1"})
    invalid("mov $0," + operand, "register operand");
  invalid("mov $0,$2", "out of range", 1);
  invalid("mov $0,$9", "out of range", 8);
  invalid("mov $0,$10", "out of range", 8);
  invalid("mov $0,${1:x}", "placeholders");
  invalid("mov x0,$1", "destination must be");
  for (const std::string code : {"mov $0", "mov $0,$1,$2", "add $0,$1",
                                 "add $0,$1,$2,$1", "lsl $0,$1"})
    invalid(code, "operand count");
  invalid("mov $0,", "register operand");
  invalid("nop $0", "takes no operands");
  invalid("nop", "must initialize", 0);
  invalid("mov $0,$0", "require an output", 1, 0);
  invalid("", "1..65536", 0, 0);
  invalid(std::string(65537, ' '), "1..65536", 0, 0);
  invalid(std::string("nop\0ret", 7), "non-NUL", 0, 0);
  invalid(" ;\n\t", "at least one instruction", 0, 0);
  invalid("nop", "at most eight", 9, 0);
  invalid("nop", "one output", 0, 2);
  valid(std::string(65533, ' ') + "nop", 0, 0);
  expect_invalid("rev $0,$1", 1, 1, "outside the bounded");
}

} // namespace

int main() {
  test_positive_profile();
  test_register_and_control_flow_boundaries();
  test_definite_output();
  test_operand_bounds();
  test_metadata_bounds();
  test_aarch64_profile();
  if (failures != 0)
    return 1;
  std::cout << "inline assembly contract checks passed: " << checks << '\n';
  return 0;
}

#ifndef NY_COMPILER_INLINE_ASM_HPP
#define NY_COMPILER_INLINE_ASM_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ny {

namespace inline_asm_detail {

inline std::string_view trim(std::string_view text) {
  constexpr std::string_view whitespace = " \t\r\n\f\v";
  const auto first = text.find_first_not_of(whitespace);
  if (first == std::string_view::npos)
    return {};
  const auto last = text.find_last_not_of(whitespace);
  return text.substr(first, last - first + 1);
}

struct immediate {
  bool negative = false;
  std::uint64_t magnitude = 0;
};

// Accept only canonical decimal spelling. In particular, leading zeroes can
// mean octal to an assembler, and assembler expressions/symbols are outside
// this profile. LLVM uses $$ to emit AT&T's immediate '$' prefix.
inline bool parse_immediate(std::string_view text, immediate &result) {
  if (text.size() < 3 || text.substr(0, 2) != "$$")
    return false;
  text.remove_prefix(2);
  result = {};
  if (text.front() == '-') {
    result.negative = true;
    text.remove_prefix(1);
  }
  if (text.empty() || (text.size() > 1 && text.front() == '0'))
    return false;
  constexpr std::uint64_t signed_maximum = 9223372036854775807ULL;
  const auto limit = signed_maximum + (result.negative ? 1ULL : 0ULL);
  for (const char digit : text) {
    if (digit < '0' || digit > '9')
      return false;
    const auto value = static_cast<std::uint64_t>(digit - '0');
    if (result.magnitude > (limit - value) / 10ULL)
      return false;
    result.magnitude = result.magnitude * 10ULL + value;
  }
  return true;
}

inline std::string instruction_error(std::string_view instruction,
                                     std::size_t inputs, std::size_t outputs,
                                     bool &output_defined) {
  const auto split = instruction.find_first_of(" \t\r\f\v");
  const auto opcode = instruction.substr(0, split);
  const auto operands = split == std::string_view::npos
                            ? std::string_view{}
                            : trim(instruction.substr(split));
  if (opcode == "nop") {
    return operands.empty() ? std::string{}
                            : "inline assembly nop takes no operands";
  }

  const bool move = opcode == "movq";
  const bool unary = opcode == "bswapq" || opcode == "notq" || opcode == "negq";
  const bool binary = opcode == "addq" || opcode == "subq" ||
                      opcode == "andq" || opcode == "orq" || opcode == "xorq";
  const bool shift = opcode == "salq" || opcode == "shlq" ||
                     opcode == "shrq" || opcode == "sarq" ||
                     opcode == "rolq" || opcode == "rorq";
  if (!move && !unary && !binary && !shift) {
    return "inline assembly instruction is outside the bounded register-only "
           "profile (no directives, labels, control flow, or implicit registers)";
  }
  if (outputs != 1)
    return "inline assembly register operations require an output";

  const auto comma = operands.find(',');
  if (unary) {
    if (operands != "$0")
      return "inline assembly unary destination must be the output $0";
    if (!output_defined)
      return "inline assembly reads an uninitialized output $0";
    return {};
  }
  if (comma == std::string_view::npos ||
      operands.find(',', comma + 1) != std::string_view::npos) {
    return "inline assembly instruction requires source and output operands";
  }
  const auto source = trim(operands.substr(0, comma));
  const auto destination = trim(operands.substr(comma + 1));
  if (destination != "$0")
    return "inline assembly destination must be the output $0; inputs are read-only";

  immediate value;
  const bool is_immediate = parse_immediate(source, value);
  const bool is_output = source == "$0";
  const bool is_input = source.size() == 2 && source[0] == '$' &&
                        source[1] >= '1' && source[1] <= '8' &&
                        static_cast<std::size_t>(source[1] - '0') <= inputs;
  if (!is_immediate && !is_output && !is_input) {
    return "inline assembly source must be an operand placeholder or a signed "
           "i64 decimal immediate ($$value); explicit registers, memory, and "
           "expressions are unsupported";
  }
  if (shift && (!is_immediate || value.negative || value.magnitude > 63))
    return "inline assembly shift/rotate immediate must be in the range 0..63";
  if (binary && is_immediate &&
      value.magnitude > (value.negative ? 2147483648ULL : 2147483647ULL)) {
    return "inline assembly arithmetic/bitwise immediate must fit signed i32; "
           "pass wider values through an input operand";
  }

  // The output is an early-clobber register, not an input/output register.
  // Nothing may read its old contents, including movq $0,$0. The xor-zeroing
  // idiom is the one deliberate exception: it writes zero independently.
  const bool zeroes_output = opcode == "xorq" && is_output;
  if (!output_defined && !zeroes_output && (!move || is_output))
    return "inline assembly reads an uninitialized output $0";
  output_defined = true;
  return {};
}

} // namespace inline_asm_detail

// The first native asm profile is deliberately narrow: integer registers, at
// most one early-clobber output, no implicit registers or control-flow edges.
// Input registers are read-only. Only the enumerated straight-line transforms
// are accepted; this is not an arbitrary x86 assembler. Validate at both the
// source and serialized-NIL boundaries before LLVM sees the template.
inline std::string inline_asm_contract_error(const std::string &target,
                                             const std::string &code,
                                             std::size_t inputs,
                                             std::size_t outputs) {
  if (target != "x86_64")
    return "inline assembly requires the explicit target 'x86_64'";
  if (code.empty() || code.size() > 65536 || code.find('\0') != std::string::npos)
    return "inline assembly template must contain 1..65536 non-NUL bytes";
  if (inputs > 8 || outputs > 1)
    return "inline assembly supports at most eight inputs and one output";
  const std::size_t operands = inputs + outputs;
  for (std::size_t i = 0; i < code.size(); ++i) {
    if (code[i] != '$')
      continue;
    if (++i == code.size())
      return "inline assembly has an incomplete operand placeholder";
    if (code[i] == '$')
      continue;
    if (code[i] < '0' || code[i] > '9')
      return "inline assembly placeholders must be $0..$8 or $$";
    std::size_t index = static_cast<std::size_t>(code[i] - '0');
    if (i + 1 < code.size() && code[i + 1] >= '0' && code[i + 1] <= '9')
      return "inline assembly operand placeholder is out of range";
    if (index >= operands)
      return "inline assembly operand placeholder is out of range";
  }
  bool output_defined = false;
  bool has_instruction = false;
  std::size_t start = 0;
  while (start < code.size()) {
    const auto end = code.find_first_of("\n;", start);
    const auto instruction = inline_asm_detail::trim(
        std::string_view(code).substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
    if (!instruction.empty()) {
      has_instruction = true;
      const auto error = inline_asm_detail::instruction_error(
          instruction, inputs, outputs, output_defined);
      if (!error.empty())
        return error;
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  if (!has_instruction)
    return "inline assembly template must contain at least one instruction";
  if (outputs != 0 && !output_defined)
    return "inline assembly must initialize output $0";
  return {};
}

} // namespace ny

#endif

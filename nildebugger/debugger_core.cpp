#include "debugger_core.hpp"
#include "nil_loader.hpp"
#include "secure_file_io.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <cmath>
#include <limits>
#include <locale>
#include <unordered_set>

namespace ny {
namespace debug {

namespace {

constexpr std::uint64_t maximum_debugger_program_bytes =
    256ULL * 1024ULL * 1024ULL;

enum class ConditionRhsKind { Integer, Floating, Text };

struct ParsedCondition {
    std::string lhs;
    std::string comparator;
    ConditionRhsKind rhs_kind = ConditionRhsKind::Text;
    int64_t rhs_integer = 0;
    double rhs_floating = 0.0;
    std::string rhs_text;
};

std::string trim_condition_token(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool is_identifier_path(const std::string& value) {
    if (value.empty()) return false;
    bool segment_start = true;
    bool saw_dot = false;
    for (const unsigned char c : value) {
        if (segment_start) {
            if (!(std::isalpha(c) || c == '_')) return false;
            segment_start = false;
        } else if (c == '.') {
            if (saw_dot) return false;
            saw_dot = true;
            segment_start = true;
        } else if (!(std::isalnum(c) || c == '_')) {
            return false;
        }
    }
    return !segment_start;
}

bool is_bare_scalar(const std::string& value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!(std::isalpha(first) || first == '_')) return false;
    for (const unsigned char c : value) {
        if (!(std::isalnum(c) || c == '_' || c == '.' || c == ':' || c == '-'))
            return false;
    }
    return true;
}

bool parse_finite_number(const std::string& value, double& result) {
    try {
        size_t consumed = 0;
        result = std::stod(value, &consumed);
        return consumed == value.size() && std::isfinite(result);
    } catch (...) {
        return false;
    }
}

bool parse_decimal_integer(const std::string& value, int64_t& result) {
    if (value.empty()) return false;
    const char* begin = value.data();
    const char* end = begin + value.size();
    if (*begin == '+') {
        ++begin;
        if (begin == end) return false;
    }
    const auto parsed = std::from_chars(begin, end, result, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool decode_quoted_string(const std::string& value, std::string& decoded,
                          std::string& error) {
    if (value.size() < 2 || value.front() != '"') {
        error = "quoted string RHS must begin and end with a double quote";
        return false;
    }
    decoded.clear();
    for (size_t i = 1; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '"') {
            if (i + 1 != value.size()) {
                error = "unexpected characters after quoted string RHS";
                return false;
            }
            return true;
        }
        if (c < 0x20U) {
            error = "quoted string RHS contains an unescaped control character";
            return false;
        }
        if (c != '\\') {
            decoded.push_back(static_cast<char>(c));
            continue;
        }
        if (++i >= value.size()) {
            error = "quoted string RHS ends with an incomplete escape";
            return false;
        }
        switch (value[i]) {
            case '"': decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            default:
                error = std::string("unsupported string escape \\") + value[i] +
                        " in breakpoint condition";
                return false;
        }
    }
    error = "unterminated quoted string RHS";
    return false;
}

std::optional<ParsedCondition>
parse_breakpoint_condition(const std::string& condition, std::string& error) {
    error.clear();
    const std::string input = trim_condition_token(condition);
    if (input.empty()) {
        error = "condition is empty";
        return std::nullopt;
    }

    struct ComparatorMatch {
        size_t position;
        std::string value;
    };
    std::vector<ComparatorMatch> matches;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }

        std::string comparator;
        if (i + 1 < input.size()) {
            const std::string pair = input.substr(i, 2);
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=")
                comparator = pair;
        }
        if (comparator.empty() && (c == '<' || c == '>'))
            comparator.assign(1, c);
        if (!comparator.empty()) {
            matches.push_back({i, comparator});
            i += comparator.size() - 1;
            continue;
        }
        if (c == '=' || c == '!') {
            error = "unsupported comparator; use ==, !=, <, <=, >, or >=";
            return std::nullopt;
        }
    }
    if (in_string) {
        error = "unterminated quoted string RHS";
        return std::nullopt;
    }
    if (matches.size() != 1) {
        error = matches.empty()
            ? "expected exactly one comparator: ==, !=, <, <=, >, or >="
            : "condition must contain exactly one comparator";
        return std::nullopt;
    }

    ParsedCondition parsed;
    parsed.comparator = matches.front().value;
    parsed.lhs = trim_condition_token(
        input.substr(0, matches.front().position));
    if (!is_identifier_path(parsed.lhs)) {
        error = "left operand must be an identifier or one-field dot path such as value or object.field";
        return std::nullopt;
    }

    const size_t rhs_begin =
        matches.front().position + matches.front().value.size();
    const std::string rhs = trim_condition_token(input.substr(rhs_begin));
    if (rhs.empty()) {
        error = "right operand is missing";
        return std::nullopt;
    }

    if (rhs.front() == '"') {
        parsed.rhs_kind = ConditionRhsKind::Text;
        if (!decode_quoted_string(rhs, parsed.rhs_text, error))
            return std::nullopt;
        return parsed;
    }

    const bool floating_spelling =
        rhs.find_first_of(".eE") != std::string::npos;
    if (floating_spelling) {
        if (parse_finite_number(rhs, parsed.rhs_floating)) {
            parsed.rhs_kind = ConditionRhsKind::Floating;
            return parsed;
        }
    } else if (parse_decimal_integer(rhs, parsed.rhs_integer)) {
        parsed.rhs_kind = ConditionRhsKind::Integer;
        return parsed;
    }

    const unsigned char first = static_cast<unsigned char>(rhs.front());
    const bool numeric_like = std::isdigit(first) || rhs.front() == '+' ||
                              rhs.front() == '-' || rhs.front() == '.';
    std::string lowercase;
    lowercase.reserve(rhs.size());
    for (const unsigned char c : rhs)
        lowercase.push_back(static_cast<char>(std::tolower(c)));
    const bool nonfinite_name = lowercase == "nan" || lowercase == "+nan" ||
                                lowercase == "-nan" || lowercase == "inf" ||
                                lowercase == "+inf" || lowercase == "-inf" ||
                                lowercase == "infinity" ||
                                lowercase == "+infinity" ||
                                lowercase == "-infinity";
    if (numeric_like || nonfinite_name) {
        error = "numeric RHS must be a complete finite number";
        return std::nullopt;
    }
    if (!is_bare_scalar(rhs)) {
        error = "RHS must be a complete finite number, quoted string, or bare scalar";
        return std::nullopt;
    }

    parsed.rhs_kind = ConditionRhsKind::Text;
    parsed.rhs_text = rhs;
    return parsed;
}

bool compare_ordering(int ordering, const std::string& comparator) {
    if (comparator == "==") return ordering == 0;
    if (comparator == "!=") return ordering != 0;
    if (comparator == "<") return ordering < 0;
    if (comparator == "<=") return ordering <= 0;
    if (comparator == ">") return ordering > 0;
    if (comparator == ">=") return ordering >= 0;
    return false;
}

int compare_integers(int64_t lhs, int64_t rhs) {
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

int compare_floating(double lhs, double rhs) {
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
}

// Compare without first converting the integer to double. Such a conversion
// loses units above 2^53 and used to make adjacent i64 values compare equal.
int compare_integer_to_floating(int64_t lhs, double rhs) {
    constexpr double i64_upper_exclusive = 9223372036854775808.0;
    constexpr double i64_lower = -9223372036854775808.0;
    if (rhs >= i64_upper_exclusive) return -1;
    if (rhs < i64_lower) return 1;

    const int64_t truncated = static_cast<int64_t>(rhs);
    if (lhs < truncated) return -1;
    if (lhs > truncated) return 1;

    const double truncated_as_double = static_cast<double>(truncated);
    if (rhs < truncated_as_double) return 1;
    if (rhs > truncated_as_double) return -1;
    return 0;
}

bool compare_text(const std::string& lhs, const std::string& rhs,
                  const std::string& comparator) {
    if (comparator == "==") return lhs == rhs;
    if (comparator == "!=") return lhs != rhs;
    if (comparator == "<") return lhs < rhs;
    if (comparator == "<=") return lhs <= rhs;
    if (comparator == ">") return lhs > rhs;
    if (comparator == ">=") return lhs >= rhs;
    return false;
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────

Debugger::Debugger() : state_(ExecutionState::Detached) {
    vm_.setProcessExitTrapping(true);
    vm_.setDebugCallback(
        [this](VM& vm, const Instruction& instruction, size_t pc) {
            return on_vm_stop(vm, instruction, pc);
        });
}

Debugger::~Debugger() {
    detach();
    vm_.setDebugCallback({});
}

// ─────────────────────────────────────────────────────────────
// Session management
// ─────────────────────────────────────────────────────────────

bool Debugger::load_program(const std::string& path) {
    std::unordered_map<std::string, Value> constants;
    std::vector<Instruction> program;

    try {
        const auto read = ::ny::detail::read_regular_file_bounded(
            path, maximum_debugger_program_bytes);
        if (!read.ok) {
            std::cerr << "[error] Failed to load '" << path
                      << "': cannot read NIL input securely: " << read.error
                      << '\n';
            return false;
        }

        const std::string contents(read.bytes.begin(), read.bytes.end());

        const bool has_binary_magic = contents.size() >= 4 &&
            (std::memcmp(contents.data(), "NIL\0", 4) == 0 ||
             std::memcmp(contents.data(), "MIL\0", 4) == 0 ||
             std::memcmp(contents.data(), "\0LIM", 4) == 0);
        if (has_binary_magic && contents.size() >= 12) {
            const auto* artifact_kind =
                reinterpret_cast<const unsigned char*>(contents.data() + 4);
            const auto read_u32 = [](const unsigned char* bytes) {
                return static_cast<std::uint32_t>(bytes[0]) |
                       (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                       (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                       (static_cast<std::uint32_t>(bytes[3]) << 24U);
            };
            if ((read_u32(artifact_kind) == 2U || read_u32(artifact_kind) == 4U ||
                 read_u32(artifact_kind) == 5U) &&
                read_u32(artifact_kind + 4) == 1U) {
                std::cerr << "[error] Cannot run NIL library '" << path
                          << "' in the debugger.\n";
                return false;
            }
        }

        std::istringstream stream(contents, std::ios::in | std::ios::binary);
        if (has_binary_magic) {
            program = parseBinaryProgram(stream, constants);
        } else {
            program = parseProgram(stream, constants);
        }

        // Validate the runnable entry and labels before mutating an already
        // loaded VM, so a failed reload cannot leave a partial program.
        std::unordered_set<std::string> labels;
        const Instruction* main_entry = nullptr;
        for (const auto& instruction : program) {
            if (instruction.op != Opcode::LABEL || instruction.args.empty())
                continue;
            if (!labels.insert(instruction.args[0]).second) {
                throw std::runtime_error("duplicate NIL label '" +
                                         instruction.args[0] + "'");
            }
            if (instruction.is_function_entry && instruction.args[0] == "main")
                main_entry = &instruction;
        }
        if (!main_entry) {
            throw std::runtime_error("program has no 'main' entry point");
        }
        if (main_entry->function_param_count != 0) {
            throw std::runtime_error(
                "entry function 'main' must declare 0 parameters, found " +
                std::to_string(main_entry->function_param_count));
        }

        vm_.loadProgram(program, constants);
        vm_.resetExecution("main");
    } catch (const std::exception &e) {
        std::cerr << "[error] Failed to load '" << path
                  << "': " << e.what() << '\n';
        return false;
    }

    current_program_ = path;
    inspect_frame_ = -1;
    breakpoints_.clear();
    next_breakpoint_id_ = 1;
    execution_started_ = false;
    last_stop_reason_.clear();
    last_error_.clear();
    pause_requested_.store(false);
    update_state(ExecutionState::Paused);
    return true;
}

bool Debugger::attach(int pid) {
    std::cerr << "[error] Process attach is not implemented.\n";
    std::cerr << "[error] Cannot attach to PID " << pid << ".\n";
    std::cerr << "[error] Use 'nildebugger run <program.nil>' instead.\n";
    return false;
}

void Debugger::detach() {
    update_state(ExecutionState::Detached);
}

// ─────────────────────────────────────────────────────────────
// Execution control
// ─────────────────────────────────────────────────────────────

void Debugger::prepare_execution() {
    if (!execution_started_ || state_ == ExecutionState::Terminated) {
        vm_.resetExecution("main");
        inspect_frame_ = -1;
        execution_started_ = true;
    }
}

void Debugger::emit_stop(const std::string& reason, int bp_id) {
    last_stop_reason_ = reason;
    last_breakpoint_id_ = bp_id;
    if (silent_mode_) {
        update_state(ExecutionState::Paused);
        return;
    }
    if (json_mode_) {
        emit_json(get_stop_json(reason, bp_id));
    } else {
        const size_t pc = vm_.getPC();
        const int line = get_current_line();
        const int opcode = pc < vm_.getProgram().size()
            ? static_cast<int>(vm_.getProgram()[pc].op)
            : -1;
        std::cout << "[Stop] PC=" << pc << " Line=" << line
                  << " Op=" << opcode << "\n";
    }
    update_state(ExecutionState::Paused);
}

void Debugger::emit_runtime_error(const std::exception& error) {
    last_error_ = error.what();
    last_stop_reason_ = "exception";
    if (silent_mode_) {
        vm_.setDebugState(VM::DebugState::PAUSED);
        update_state(ExecutionState::Paused);
        return;
    }
    if (json_mode_) {
        emit_json("{\"event\":\"error\",\"message\":" +
                  json_str(error.what()) + "}");
    } else {
        std::cerr << "[error] Runtime: " << error.what() << "\n";
    }
    vm_.setDebugState(VM::DebugState::PAUSED);
    update_state(ExecutionState::Paused);
}

void Debugger::run() {
    try {
        // `run` is the process-style restart command.  Other execution
        // controls call prepare_execution() so their first use also enters
        // main, but continue/step retain a paused invocation.
        vm_.resetExecution("main");
        inspect_frame_ = -1;
        execution_started_ = true;
        update_state(ExecutionState::Running);
        vm_.setDebugState(VM::DebugState::RUNNING);
        vm_.execute(false);

        if (vm_.getPC() >= vm_.getProgram().size()) {
            update_state(ExecutionState::Terminated);
        } else {
            update_state(ExecutionState::Paused);
        }
    } catch (const std::exception& error) {
        emit_runtime_error(error);
    }
}

void Debugger::pause() {
    vm_.setDebugState(VM::DebugState::PAUSED);
    update_state(ExecutionState::Paused);
}

void Debugger::step_over() {
    source_step(true, false);
}

void Debugger::source_step(bool over, bool out) {
    try {
        prepare_execution();
        inspect_frame_ = -1;
        last_error_.clear();
        const size_t start_depth = vm_.getCallDepth();
        const auto start_file = get_current_file();
        const int start_line = get_current_line();
        const bool source_mapped = std::any_of(
            vm_.getProgram().begin(), vm_.getProgram().end(),
            [](const Instruction& instruction) { return !instruction.source_file.empty(); });
        update_state(ExecutionState::Running);
        bool remaining = vm_.stepInstruction();
        while (remaining) {
            if (pause_requested_.exchange(false)) {
                emit_stop("pause");
                return;
            }
            if (const auto bp_id = stopping_breakpoint_at(vm_.getPC())) {
                emit_stop("breakpoint", *bp_id);
                return;
            }
            const auto& next = vm_.getProgram()[vm_.getPC()];
            const auto depth = vm_.getCallDepth();
            const bool eligible = (!out || depth < start_depth) &&
                (!over || depth <= start_depth);
            if (eligible && (!source_mapped ||
                (next.op != Opcode::LABEL && !next.source_file.empty() && next.line > 0 &&
                 (out || depth != start_depth || next.source_file != start_file ||
                  next.line != start_line)))) break;
            remaining = vm_.stepInstruction();
        }
        if (!remaining) update_state(ExecutionState::Terminated);
        else emit_stop("step");
    } catch (const std::exception& error) {
        emit_runtime_error(error);
    }
}

void Debugger::step_into() {
    source_step(false, false);
}

void Debugger::step_out() {
    source_step(false, true);
}

void Debugger::stop_at_entry() {
    try {
        prepare_execution();
        inspect_frame_ = -1;
        update_state(ExecutionState::Running);
        while (vm_.getPC() < vm_.getProgram().size() &&
               vm_.getProgram()[vm_.getPC()].op == Opcode::LABEL) {
            if (!vm_.stepInstruction()) {
                update_state(ExecutionState::Terminated);
                return;
            }
        }
        emit_stop("entry");
    } catch (const std::exception& error) {
        emit_runtime_error(error);
    }
}

void Debugger::continue_cooperatively() {
    try {
        prepare_execution();
        inspect_frame_ = -1;
        last_error_.clear();
        update_state(ExecutionState::Running);
        bool first = true;
        while (vm_.getPC() < vm_.getProgram().size()) {
            if (pause_requested_.exchange(false)) { emit_stop("pause"); return; }
            if (!first) {
                if (const auto id = stopping_breakpoint_at(vm_.getPC())) {
                    emit_stop("breakpoint", *id);
                    return;
                }
            }
            first = false;
            if (!vm_.stepInstruction()) break;
        }
        update_state(ExecutionState::Terminated);
    } catch (const std::exception& error) { emit_runtime_error(error); }
}

void Debugger::continue_execution() {
    try {
        prepare_execution();
        inspect_frame_ = -1;
        update_state(ExecutionState::Running);
        if (vm_.hasBreakpoint(vm_.getPC()) && !vm_.stepInstruction()) {
            update_state(ExecutionState::Terminated);
            return;
        }
        vm_.setDebugState(VM::DebugState::RUNNING);
        vm_.execute(false);
        if (vm_.getPC() >= vm_.getProgram().size()) {
            update_state(ExecutionState::Terminated);
        } else {
            update_state(ExecutionState::Paused);
        }
    } catch (const std::exception& error) {
        emit_runtime_error(error);
    }
}

// ─────────────────────────────────────────────────────────────
// Breakpoints
// ─────────────────────────────────────────────────────────────

bool Debugger::breakpoint_file_matches(const std::string& file) const {
    return same_file(file, current_program_);
}

bool Debugger::same_file(const std::string& file, const std::string& other) {
    if (file == other) return true;
    std::error_code requested_error;
    std::error_code current_error;
    const auto requested = std::filesystem::weakly_canonical(file, requested_error);
    const auto current =
        std::filesystem::weakly_canonical(other, current_error);
    return !requested_error && !current_error && requested == current;
}

std::optional<int> Debugger::set_breakpoint(const std::string& file, int line,
                                             const std::string& condition,
                                             std::string* error) {
    const auto reject = [&](const std::string& message) -> std::optional<int> {
        if (error) *error = message;
        return std::nullopt;
    };
    if (!condition.empty()) {
        std::string condition_error;
        if (!parse_breakpoint_condition(condition, condition_error)) {
            return reject("invalid breakpoint condition: " + condition_error);
        }
    }
    if (line <= 0) return reject("breakpoint line must be positive");

    std::optional<size_t> resolved_pc;
    std::string resolved_function;
    std::string current_function = "<module>";
    const auto& program = vm_.getProgram();
    const bool source_mapped = std::any_of(program.begin(), program.end(),
        [](const Instruction& instruction) { return !instruction.source_file.empty(); });
    if (!source_mapped && !breakpoint_file_matches(file)) {
        return reject("NIL has no per-instruction source file map; breakpoint file must name the loaded artifact '" + current_program_ + "'");
    }
    for (size_t pc = 0; pc < program.size(); ++pc) {
        const auto& instr = program[pc];
        if (instr.is_function_entry && !instr.args.empty()) {
            current_function = instr.args[0];
        }
        if (instr.op == Opcode::LABEL || instr.line != line) continue;
        if (source_mapped && (instr.source_file.empty() || !same_file(file, instr.source_file))) continue;
        if (!resolved_pc) {
            resolved_pc = pc;
            resolved_function = current_function;
        } else if (resolved_function != current_function) {
            return reject("breakpoint line " + std::to_string(line) +
                          " is ambiguous across functions");
        }
    }
    if (!resolved_pc) {
        return reject("no executable instruction exists at line " +
                      std::to_string(line));
    }

    Breakpoint bp;
    bp.id        = next_breakpoint_id_++;
    bp.file      = file;
    bp.line      = line;
    bp.enabled   = true;
    bp.condition = condition;
    bp.pc        = *resolved_pc;
    breakpoints_.push_back(bp);
    vm_.setBreakpoint(*resolved_pc);
    if (error) error->clear();
    return bp.id;
}

bool Debugger::remove_breakpoint(int id) {
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const Breakpoint& bp) { return bp.id == id; });
    if (it == breakpoints_.end()) return false;

    size_t pc = it->pc;
    breakpoints_.erase(it);
    if (pc == static_cast<size_t>(-1)) return true;

    bool any = false;
    for (const auto& bp : breakpoints_)
        if (bp.enabled && bp.pc == pc) { any = true; break; }
    if (!any) vm_.clearBreakpoint(pc);
    return true;
}

bool Debugger::enable_breakpoint(int id, bool enable) {
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const Breakpoint& bp) { return bp.id == id; });
    if (it == breakpoints_.end()) return false;
    if (it->enabled == enable) return true;

    size_t pc = it->pc;
    it->enabled = enable;
    if (pc == static_cast<size_t>(-1)) return true;

    bool any = false;
    for (const auto& bp : breakpoints_)
        if (bp.enabled && bp.pc == pc) { any = true; break; }
    if (any) vm_.setBreakpoint(pc);
    else     vm_.clearBreakpoint(pc);
    return true;
}

bool Debugger::set_breakpoint_condition(int id, const std::string& condition,
                                        std::string* error) {
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const Breakpoint& bp) { return bp.id == id; });
    if (it == breakpoints_.end()) {
        if (error) *error = "unknown breakpoint id " + std::to_string(id);
        return false;
    }
    if (!condition.empty()) {
        std::string condition_error;
        if (!parse_breakpoint_condition(condition, condition_error)) {
            if (error)
                *error = "invalid breakpoint condition: " + condition_error;
            return false;
        }
    }
    it->condition = condition;
    if (error) error->clear();
    return true;
}

std::vector<Breakpoint> Debugger::get_breakpoints() const { return breakpoints_; }

// ─────────────────────────────────────────────────────────────
// Watches
// ─────────────────────────────────────────────────────────────

int Debugger::add_watch(const std::string& expr) {
    Watch w; w.id = next_watch_id_++; w.expr = expr;
    watches_.push_back(w);
    return w.id;
}

void Debugger::remove_watch(int id) {
    watches_.erase(std::remove_if(watches_.begin(), watches_.end(),
        [id](const Watch& w) { return w.id == id; }), watches_.end());
}

std::vector<Watch> Debugger::get_watches() const { return watches_; }

// ─────────────────────────────────────────────────────────────
// State helpers
// ─────────────────────────────────────────────────────────────

std::string Debugger::get_current_file() const {
    size_t pc = vm_.getPC();
    if (pc >= vm_.getProgram().size()) return "";
    const auto& instruction = vm_.getProgram()[pc];
    return instruction.source_file.empty() ? current_program_ : instruction.source_file;
}

int Debugger::get_current_line() const {
    size_t pc = vm_.getPC();
    if (pc >= vm_.getProgram().size()) return -1;
    return vm_.getProgram()[pc].line;
}

void Debugger::set_frame(int depth) {
    int max = static_cast<int>(vm_.getCallDepth()) - 1;
    if (depth < 0 || depth > max) depth = -1;
    inspect_frame_ = depth;
}

// ─────────────────────────────────────────────────────────────
// Value formatting
// ─────────────────────────────────────────────────────────────

std::string Debugger::json_str(const std::string& s) {
    std::string out = "\"";
    const auto append_byte_escape = [&](unsigned char byte) {
        static constexpr char hex[] = "0123456789abcdef";
        out += "\\u00";
        out.push_back(hex[(byte >> 4U) & 0x0fU]);
        out.push_back(hex[byte & 0x0fU]);
    };
    for (size_t index = 0; index < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[index]);
        if (c >= 0x80U) {
            size_t width = 0;
            std::uint32_t codepoint = 0;
            std::uint32_t minimum = 0;
            if (c >= 0xc2U && c <= 0xdfU) {
                width = 2;
                codepoint = c & 0x1fU;
                minimum = 0x80U;
            } else if (c >= 0xe0U && c <= 0xefU) {
                width = 3;
                codepoint = c & 0x0fU;
                minimum = 0x800U;
            } else if (c >= 0xf0U && c <= 0xf4U) {
                width = 4;
                codepoint = c & 0x07U;
                minimum = 0x10000U;
            }

            bool valid = width != 0 && width <= s.size() - index;
            for (size_t continuation = 1; valid && continuation < width;
                 ++continuation) {
                const unsigned char byte =
                    static_cast<unsigned char>(s[index + continuation]);
                if ((byte & 0xc0U) != 0x80U) {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6U) | (byte & 0x3fU);
            }
            valid = valid && codepoint >= minimum && codepoint <= 0x10ffffU &&
                    !(codepoint >= 0xd800U && codepoint <= 0xdfffU);
            if (valid) {
                out.append(s, index, width);
                index += width;
                continue;
            }

            // JSON strings are Unicode, while a VM string can contain arbitrary
            // bytes. Preserve an invalid byte deterministically as its U+00XX
            // code point instead of emitting malformed UTF-8 into the JSONL
            // control stream.
            append_byte_escape(c);
            ++index;
            continue;
        }

        const char ascii = static_cast<char>(c);
        switch (ascii) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20U) {
                    append_byte_escape(c);
                } else {
                    out += ascii;
                }
        }
        ++index;
    }
    out += "\"";
    return out;
}

namespace {

constexpr size_t kSerializationMaxDepth = 32;

struct SerializationContext {
    std::unordered_set<const StructData*> active_structs;
    std::unordered_set<const EnumValue*> active_enums;
    std::unordered_set<const DynamicBox*> active_boxes;
};

using ArrayElement = std::variant<std::string, int64_t, double,
                                  StructValue, ArrayValue, EnumValue, ArrayHandle, BoxedValue>;

std::string format_array_handle(const ArrayHandle& value) {
    return "{\"__array_handle__\":" + std::to_string(value.address) + "}";
}

std::string format_struct_safe(const StructValue& value,
                               SerializationContext& context, size_t depth);
std::string format_array_safe(const ArrayValue& value,
                              SerializationContext& context, size_t depth);
std::string format_enum_safe(const EnumValue& value,
                             SerializationContext& context, size_t depth);

std::string format_value_safe(const Value& value,
                              SerializationContext& context, size_t depth);
std::string format_box_safe(const BoxedValue& value,
                            SerializationContext& context, size_t depth) {
    if (depth >= kSerializationMaxDepth) return Debugger::json_str("<max-depth>");
    if (!value.box) return Debugger::json_str("<invalid-anything>");
    if (!context.active_boxes.insert(value.box.get()).second)
        return Debugger::json_str("<cycle>");
    const auto result = "{\"__type__\":\"anything\",\"__boxed_type__\":" +
        Debugger::json_str(value.box->type_name) + ",\"value\":" +
        format_value_safe(value.box->value, context, depth + 1) + "}";
    context.active_boxes.erase(value.box.get());
    return result;
}

std::string format_finite_double(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value;
    return output.str();
}

std::string format_array_element_safe(const ArrayElement& element,
                                      SerializationContext& context,
                                      size_t depth) {
    return std::visit([&](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(value);
        } else if constexpr (std::is_same_v<T, double>) {
            return format_finite_double(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return Debugger::json_str(value);
        } else if constexpr (std::is_same_v<T, StructValue>) {
            return format_struct_safe(value, context, depth);
        } else if constexpr (std::is_same_v<T, ArrayValue>) {
            return format_array_safe(value, context, depth);
        } else if constexpr (std::is_same_v<T, EnumValue>) {
            return format_enum_safe(value, context, depth);
        } else if constexpr (std::is_same_v<T, BoxedValue>) {
            return format_box_safe(value, context, depth);
        } else if constexpr (std::is_same_v<T, ArrayHandle>) {
            return format_array_handle(value);
        }
        return "null";
    }, element);
}

std::string format_struct_safe(const StructValue& value,
                               SerializationContext& context, size_t depth) {
    if (depth >= kSerializationMaxDepth)
        return Debugger::json_str("<max-depth>");
    if (!value.data)
        return Debugger::json_str("<invalid-struct>");
    const StructData* identity = value.data.get();
    if (!context.active_structs.insert(identity).second)
        return Debugger::json_str("<cycle>");

    std::string output = "{\"__type__\":" +
                         Debugger::json_str(value.type_name);
    for (const auto& [name, field] : value.fields())
        output += "," + Debugger::json_str(name) + ":" +
                  std::to_string(field);
    for (const auto& [name, field] : value.array_fields())
        output += "," + Debugger::json_str(name) + ":" +
                  format_array_handle(field);
    for (const auto& [name, field] : value.string_fields())
        output += "," + Debugger::json_str(name) + ":" +
                  Debugger::json_str(field);
    for (const auto& [name, field] : value.double_fields())
        output += "," + Debugger::json_str(name) + ":" +
                  format_finite_double(field);
    for (const auto& [name, field] : value.struct_fields())
        output += "," + Debugger::json_str(name) + ":" +
                  (field ? format_struct_safe(*field, context, depth + 1)
                         : "null");
    for (const auto& [name, field] : value.anything_fields())
        output += "," + Debugger::json_str(name) + ":" +
                  format_box_safe(field, context, depth + 1);
    for (const auto& [name, field] : value.enum_fields())
        output += "," + Debugger::json_str(name) + ":" +
                  format_enum_safe(field, context, depth + 1);
    output += "}";
    context.active_structs.erase(identity);
    return output;
}

std::string format_array_safe(const ArrayValue& value,
                              SerializationContext& context, size_t depth) {
    if (depth >= kSerializationMaxDepth)
        return Debugger::json_str("<max-depth>");
    std::string output = "[";
    for (size_t index = 0; index < value.elements.size(); ++index) {
        if (index) output += ",";
        output += format_array_element_safe(value.elements[index], context,
                                            depth + 1);
    }
    output += "]";
    return output;
}

std::string format_enum_safe(const EnumValue& value,
                             SerializationContext& context, size_t depth) {
    if (depth >= kSerializationMaxDepth)
        return Debugger::json_str("<max-depth>");
    if (!context.active_enums.insert(&value).second)
        return Debugger::json_str("<cycle>");

    std::string output = "{\"__variant__\":" +
                         Debugger::json_str(value.variant_name);
    if (!value.payloads.empty()) {
        output += ",\"__payloads__\":[";
        for (size_t index = 0; index < value.payloads.size(); ++index) {
            if (index) output += ",";
            std::visit([&](const auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    output += std::to_string(payload);
                } else if constexpr (std::is_same_v<T, double>) {
                    output += format_finite_double(payload);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    output += Debugger::json_str(payload);
                } else if constexpr (std::is_same_v<T, StructValue>) {
                    output += format_struct_safe(payload, context, depth + 1);
                } else if constexpr (std::is_same_v<T, BoxedValue>) {
                    output += format_box_safe(payload, context, depth + 1);
                } else if constexpr (std::is_same_v<T, ArrayHandle>) {
                    output += format_array_handle(payload);
                } else {
                    output += payload
                        ? format_enum_safe(*payload, context, depth + 1)
                        : "null";
                }
            }, value.payloads[index]);
        }
        output += "]";
    }
    output += "}";
    context.active_enums.erase(&value);
    return output;
}

std::string format_value_safe(const Value& value,
                              SerializationContext& context, size_t depth) {
    if (const auto* boxed = std::get_if<BoxedValue>(&value))
        return format_box_safe(*boxed, context, depth);
    if (const auto* integer = std::get_if<int64_t>(&value))
        return std::to_string(*integer);
    if (const auto* floating = std::get_if<double>(&value))
        return format_finite_double(*floating);
    if (const auto* text = std::get_if<std::string>(&value))
        return Debugger::json_str(*text);
    if (const auto* structure = std::get_if<StructValue>(&value))
        return format_struct_safe(*structure, context, depth);
    if (const auto* array = std::get_if<ArrayValue>(&value))
        return format_array_safe(*array, context, depth);
    if (const auto* array = std::get_if<ArrayHandle>(&value))
        return format_array_handle(*array);
    if (const auto* enumeration = std::get_if<EnumValue>(&value))
        return format_enum_safe(*enumeration, context, depth);
    return Debugger::json_str("<native>");
}

} // namespace

std::string Debugger::format_struct(const StructValue& value) {
    SerializationContext context;
    return format_struct_safe(value, context, 0);
}

std::string Debugger::format_array(const ArrayValue& value) {
    SerializationContext context;
    return format_array_safe(value, context, 0);
}

std::string Debugger::format_enum(const EnumValue& value) {
    SerializationContext context;
    return format_enum_safe(value, context, 0);
}

std::string Debugger::format_element(const ArrayElement& element) {
    SerializationContext context;
    return format_array_element_safe(element, context, 0);
}

std::string Debugger::format_value(const Value& value) {
    SerializationContext context;
    return format_value_safe(value, context, 0);
}

// ─────────────────────────────────────────────────────────────
// Inspection — JSON output
// ─────────────────────────────────────────────────────────────

size_t Debugger::inspection_pc(int frame_idx) const {
    const auto frames = vm_.getDebugFrames();
    const size_t depth = frame_idx < 0 ? 0 : static_cast<size_t>(frame_idx);
    if (depth >= frames.size()) return vm_.getProgram().size();
    return depth == 0 ? vm_.getPC() : frames[frames.size() - depth].return_pc;
}

std::vector<LocalVariable> Debugger::get_source_locals(int frame_idx) const {
    const auto frames = vm_.getDebugFrames();
    const size_t depth = frame_idx < 0 ? 0 : static_cast<size_t>(frame_idx);
    if (depth >= frames.size()) return {};
    const auto& locals = frames[frames.size() - 1 - depth].locals;
    const size_t pc = inspection_pc(frame_idx);
    std::vector<LocalVariable> result;
    if (pc < vm_.getProgram().size() && !vm_.getProgram()[pc].source_file.empty()) {
        std::unordered_map<std::string, const DebugLocalBinding*> visible;
        for (const auto& binding : vm_.getProgram()[pc].debug_locals) {
            const auto previous = visible.find(binding.name);
            if (previous == visible.end() || previous->second->scope_depth <= binding.scope_depth)
                visible[binding.name] = &binding;
        }
        for (const auto& entry : visible) {
            const auto& binding = *entry.second;
            const auto value = locals.find(binding.storage);
            if (value == locals.end()) continue;
            LocalVariable local{binding.name, binding.storage, binding.type, value->second};
            // Reference parameters and addressed locals use a compiler-owned
            // cell. Source inspection exposes the binding's scalar value,
            // while instruction-only NIL still shows exact runtime storage.
            if (binding.type.rfind("binding_ref<", 0) == 0 && binding.type.back() == '>') {
                const auto type = binding.type.substr(12, binding.type.size() - 13);
                const auto* cell = std::get_if<StructValue>(&value->second);
                if (cell && cell->data && cell->type_name == "$nil.binding.cell." + type) {
                    std::optional<Value> referent;
                    if (type == "i32" || type == "i64" || type == "u8" || type == "bool") {
                        const auto field = cell->fields().find("value");
                        if (field != cell->fields().end()) referent = Value{field->second};
                    } else if (type == "f64") {
                        const auto field = cell->double_fields().find("value");
                        if (field != cell->double_fields().end()) referent = Value{field->second};
                    } else if (type == "text") {
                        const auto field = cell->string_fields().find("value");
                        if (field != cell->string_fields().end()) referent = Value{field->second};
                    } else if (type == "anything") {
                        const auto field = cell->anything_fields().find("value");
                        if (field != cell->anything_fields().end() && field->second.box)
                            referent = Value{field->second};
                    }
                    if (referent) {
                        local.type = type;
                        local.value = std::move(*referent);
                    }
                }
            }
            result.push_back(std::move(local));
        }
    } else {
        for (const auto& entry : locals) result.push_back(
            {entry.first, entry.first, "", entry.second});
    }
    std::sort(result.begin(), result.end(), [](const LocalVariable& lhs, const LocalVariable& rhs) {
        return lhs.name < rhs.name;
    });
    return result;
}

std::string Debugger::get_locals_json(int frame_idx) const {
    const auto frames = vm_.getDebugFrames();
    if (frames.empty()) return "{\"frame\":0,\"fn\":\"<top>\",\"vars\":{}}";

    int depth = frame_idx < 0 ? 0 : frame_idx;
    if (depth < 0 || depth >= static_cast<int>(frames.size())) depth = 0;
    const size_t index = frames.size() - 1U - static_cast<size_t>(depth);
    const auto& frame = frames[index];

    std::string out = "{\"frame\":" + std::to_string(depth)
                    + ",\"fn\":" + json_str(frame.function_name)
                    + ",\"vars\":{";
    bool first = true;
    for (const auto& local : get_source_locals(frame_idx)) {
        if (!first) out += ",";
        out += json_str(local.name) + ":" + format_value(local.value);
        first = false;
    }
    out += "}}";
    return out;
}

std::string Debugger::get_regs_json() const {
    size_t pc         = vm_.getPC();
    size_t call_depth = vm_.getCallDepth();
    size_t stk_size   = vm_.getStack().size();
    int    line       = get_current_line();
    std::string instr_str;
    if (pc < vm_.getProgram().size()) {
        const auto& ins = vm_.getProgram()[pc];
        instr_str = std::to_string(static_cast<int>(ins.op));
        for (const auto& a : ins.args) instr_str += " " + a;
    }
    return "{\"pc\":" + std::to_string(pc)
         + ",\"line\":"       + std::to_string(line)
         + ",\"stack_depth\":" + std::to_string(stk_size)
         + ",\"call_depth\":" + std::to_string(call_depth)
         + ",\"instr\":"      + json_str(instr_str)
         + "}";
}

std::string Debugger::get_stack_json() const {
    const auto frames = vm_.getDebugFrames();
    size_t pc      = vm_.getPC();
    std::string out = "[";

    for (size_t depth = 0; depth < frames.size(); ++depth) {
        const size_t index = frames.size() - 1U - depth;
        const size_t frame_pc = depth == 0
            ? pc
            : frames[index + 1U].return_pc;
        const int line = frame_pc < vm_.getProgram().size()
            ? vm_.getProgram()[frame_pc].line
            : -1;
        if (depth) out += ",";
        out += "{\"depth\":" + std::to_string(depth)
             + ",\"fn\":"   + json_str(frames[index].function_name)
             + ",\"file\":" + json_str(frame_pc < vm_.getProgram().size() &&
                  !vm_.getProgram()[frame_pc].source_file.empty()
                  ? vm_.getProgram()[frame_pc].source_file : current_program_)
             + ",\"line\":" + std::to_string(line)
             + ",\"pc\":" +
               (frame_pc < vm_.getProgram().size()
                    ? std::to_string(frame_pc) : std::string("-1")) + "}";
    }
    out += "]";
    return out;
}

std::string Debugger::get_watches_json() const {
    std::string out = "[";
    bool first = true;
    for (const auto& w : watches_) {
        if (!first) out += ",";
        std::string val = evaluate_expression(w.expr);
        out += "{\"id\":" + std::to_string(w.id)
             + ",\"expr\":" + json_str(w.expr)
             + ",\"value\":" + json_str(val) + "}";
        first = false;
    }
    out += "]";
    return out;
}

std::string Debugger::get_breakpoints_json() const {
    std::string out = "[";
    bool first = true;
    for (const auto& bp : breakpoints_) {
        if (!first) out += ",";
        out += "{\"id\":"       + std::to_string(bp.id)
             + ",\"file\":"     + json_str(bp.file)
             + ",\"line\":"     + std::to_string(bp.line)
             + ",\"enabled\":"  + (bp.enabled ? "true" : "false")
             + ",\"condition\":" + json_str(bp.condition)
             + ",\"pc\":"       + std::to_string(bp.pc == static_cast<size_t>(-1) ? -1 : (int)bp.pc)
             + "}";
        first = false;
    }
    out += "]";
    return out;
}

std::string Debugger::get_stop_json(const std::string& reason, int bp_id) const {
    size_t pc   = vm_.getPC();
    int    line = get_current_line();
    const auto frames = vm_.getDebugFrames();
    const std::string fn =
        frames.empty() ? "<top>" : frames.back().function_name;

    std::string out = "{\"event\":\"stopped\""
                    + std::string(",\"reason\":") + json_str(reason)
                    + ",\"file\":"   + json_str(current_program_)
                    + ",\"line\":"   + std::to_string(line)
                    + ",\"pc\":"     + std::to_string(pc)
                    + ",\"fn\":"     + json_str(fn)
                    + ",\"bp_id\":"  + std::to_string(bp_id)
                    + ",\"locals\":" + get_locals_json()
                    + ",\"stack\":"  + get_stack_json()
                    + ",\"watches\":" + get_watches_json()
                    + "}";
    return out;
}

std::string Debugger::eval_json(const std::string& expr) const {
    std::string val = evaluate_expression(expr);
    return "{\"event\":\"eval_result\",\"expr\":" + json_str(expr)
         + ",\"value\":" + json_str(val) + "}";
}

// ─────────────────────────────────────────────────────────────
// Expression evaluation
// ─────────────────────────────────────────────────────────────

std::optional<Value>
Debugger::resolve_expression_value(const std::string& expr) const {
    const auto frames = vm_.getDebugFrames();
    const int depth = inspect_frame_ < 0 ? 0 : inspect_frame_;
    const int first_frame = static_cast<int>(frames.size()) - 1 - depth;
    size_t dot = expr.find('.');
    std::string base = dot == std::string::npos
        ? expr : expr.substr(0, dot);

    std::optional<Value> value;
    if (first_frame >= 0 && first_frame < static_cast<int>(frames.size())) {
        std::unordered_map<std::string, Value> locals;
        for (const auto& local : get_source_locals(depth)) locals.emplace(local.name, local.value);
        // Metadata-backed locals contain only the visible source bindings.
        // Historical NIL still exposes exact storage identities (which may
        // contain dots), preserving its explicit instruction-debug interface.
        if (const auto exact = locals.find(expr); exact != locals.end())
            return exact->second;
        const size_t member_dot = expr.rfind('.');
        if (member_dot != std::string::npos &&
            locals.count(expr.substr(0, member_dot))) {
            dot = member_dot;
            base = expr.substr(0, member_dot);
        }
        const auto found = locals.find(base);
        if (found != locals.end()) {
            value = found->second;
        }
    }
    if (!value && vm_.hasGlobal(base)) value = vm_.getGlobal(base);
    if (!value || dot == std::string::npos) return value;

    const auto* structure = std::get_if<StructValue>(&*value);
    if (!structure || !structure->data) return std::nullopt;
    const std::string field = expr.substr(dot + 1);

    if (const auto found = structure->fields().find(field);
        found != structure->fields().end())
        return Value{found->second};
    if (const auto found = structure->array_fields().find(field);
        found != structure->array_fields().end())
        return Value{found->second};
    if (const auto found = structure->string_fields().find(field);
        found != structure->string_fields().end())
        return Value{found->second};
    if (const auto found = structure->double_fields().find(field);
        found != structure->double_fields().end())
        return Value{found->second};
    if (const auto found = structure->struct_fields().find(field);
        found != structure->struct_fields().end() && found->second)
        return Value{*found->second};
    if (const auto found = structure->anything_fields().find(field);
        found != structure->anything_fields().end())
        return Value{found->second};
    if (const auto found = structure->enum_fields().find(field);
        found != structure->enum_fields().end())
        return Value{found->second};
    return std::nullopt;
}

std::string Debugger::evaluate_expression(const std::string& expr) const {
    const auto value = resolve_expression_value(expr);
    return value ? format_value(*value) : "(undefined: " + expr + ")";
}

// ─────────────────────────────────────────────────────────────
// Conditional breakpoint evaluation (identifier/dot-path + one comparator)
// ─────────────────────────────────────────────────────────────

bool Debugger::eval_condition(const std::string& cond) const {
    if (cond.empty()) return true;

    std::string parse_error;
    const auto parsed = parse_breakpoint_condition(cond, parse_error);
    if (!parsed) return false;

    const auto lhs = resolve_expression_value(parsed->lhs);
    if (!lhs) return false;

    if (parsed->rhs_kind == ConditionRhsKind::Text) {
        const auto* text = std::get_if<std::string>(&*lhs);
        return text && compare_text(*text, parsed->rhs_text,
                                    parsed->comparator);
    }

    if (const auto* integer = std::get_if<int64_t>(&*lhs)) {
        const int ordering = parsed->rhs_kind == ConditionRhsKind::Integer
            ? compare_integers(*integer, parsed->rhs_integer)
            : compare_integer_to_floating(*integer, parsed->rhs_floating);
        return compare_ordering(ordering, parsed->comparator);
    }
    if (const auto* floating = std::get_if<double>(&*lhs)) {
        if (!std::isfinite(*floating)) return false;
        const int ordering = parsed->rhs_kind == ConditionRhsKind::Integer
            ? -compare_integer_to_floating(parsed->rhs_integer, *floating)
            : compare_floating(*floating, parsed->rhs_floating);
        return compare_ordering(ordering, parsed->comparator);
    }

    // Valid syntax with an incompatible, aggregate, native, or unresolved LHS
    // is deliberately false; a condition must never accidentally break.
    return false;
}

std::optional<int> Debugger::stopping_breakpoint_at(size_t pc) const {
    for (const auto& bp : breakpoints_) {
        if (bp.pc == pc && bp.enabled && eval_condition(bp.condition)) {
            return bp.id;
        }
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────
// Old-style inspection (text REPL)
// ─────────────────────────────────────────────────────────────

std::vector<std::string> Debugger::get_stack_trace() {
    std::vector<std::string> trace;
    size_t pc = vm_.getPC();
    if (pc < vm_.getProgram().size()) {
        const auto& ins = vm_.getProgram()[pc];
        trace.push_back("#0 PC=" + std::to_string(pc)
                       + " Line=" + std::to_string(ins.line));
    }
    const auto frames = vm_.getDebugFrames();
    for (size_t depth = 0; depth < frames.size(); ++depth) {
        const auto& frame = frames[frames.size() - 1U - depth];
        trace.push_back("  #" + std::to_string(depth + 1) + " fn=" +
                       frame.function_name + " locals=" +
                       std::to_string(frame.locals.size()));
    }
    if (trace.empty()) trace.push_back("(no stack frames)");
    return trace;
}

// ─────────────────────────────────────────────────────────────
// State management
// ─────────────────────────────────────────────────────────────

void Debugger::set_state_callback(StateCallback cb) {
    state_callback_ = std::move(cb);
}

void Debugger::update_state(ExecutionState new_state) {
    if (state_ == new_state) return;
    state_ = new_state;
    if (state_callback_) state_callback_(state_);
    if (json_mode_ && !silent_mode_) {
        switch (new_state) {
            case ExecutionState::Running:
                emit_json("{\"event\":\"running\"}");
                return;
            case ExecutionState::Terminated:
                if (const auto code = vm_.getRequestedExitCode()) {
                    emit_json("{\"event\":\"terminated\",\"exit_code\":" +
                              std::to_string(*code) + "}");
                } else {
                    emit_json("{\"event\":\"terminated\"}");
                }
                return;
            case ExecutionState::Detached:
                emit_json("{\"event\":\"detached\"}");
                return;
            default: return; // Paused is emitted from on_vm_stop with full context
        }
    }
}

void Debugger::emit_json(const std::string& json_line) const {
    std::cout << json_line << "\n" << std::flush;
}

// ─────────────────────────────────────────────────────────────
// VM debug callback
// ─────────────────────────────────────────────────────────────

bool Debugger::on_vm_stop(VM& vm, const Instruction& instr, size_t pc) {
    (void)vm;

    // Check if this is a real breakpoint stop vs a stepping stop
    if (vm_.hasBreakpoint(pc)) {
        const auto bp_id = stopping_breakpoint_at(pc);
        if (!bp_id) {
            return true; // every enabled condition at this PC evaluated false
        }
        emit_stop("breakpoint", *bp_id);
        return false;
    }

    (void)instr;
    emit_stop("step");
    return false;
}

} // namespace debug
} // namespace ny

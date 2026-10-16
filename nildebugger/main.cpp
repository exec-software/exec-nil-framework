#include <iostream>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <sstream>
#include "json_rpc.hpp"
#include "debugger_core.hpp"
#include "dap.hpp"
#include "nil_version.hpp"

// ─────────────────────────────────────────────────────────────
// Help text
// ─────────────────────────────────────────────────────────────

static void print_help() {
    std::cout << "NIL debugger (nildebugger)\n"
              << "Usage:\n"
              << "  nildebugger [--json] [--memory-mode=<mode>] run <program.nil> [-- <args...>]\n"
              << "  nildebugger --dap      Source debugger using DAP over framed stdio\n"
              << "  nildebugger help\n\n"
              << "Flags:\n"
              << "  --json                 Machine-readable JSON Lines mode\n"
              << "  --memory-mode=<mode>   safe/gc or unsafe/manual\n"
              << "  --version              Show the nildebugger version\n"
              << "  -h, --help             Show this help\n";
}

static void print_repl_help() {
    std::cout << "\nNIL Debugger commands:\n"
              << "  Execution:\n"
              << "    r, run              Run / restart\n"
              << "    s, step             Step into\n"
              << "    n, next             Step over\n"
              << "    o, out              Step out\n"
              << "    c, continue         Continue to next breakpoint\n"
              << "  Breakpoints:\n"
              << "    b  <file>:<line>    Set breakpoint\n"
              << "    b  <file>:<line> if <cond>  Set conditional breakpoint\n"
              << "    d  <id>             Delete breakpoint\n"
              << "    enable  <id>        Enable breakpoint\n"
              << "    disable <id>        Disable breakpoint\n"
              << "    cond <id> <expr>    Set/clear condition on breakpoint\n"
              << "    bl                  List all breakpoints\n"
              << "  Inspection:\n"
              << "    p  <expr>           Print expression (supports obj.field)\n"
              << "    locals [frame]      Dump all locals in frame\n"
              << "    regs                Interpreter registers (PC, stack/call depth)\n"
              << "    bt                  Stack trace\n"
              << "    frame <n>           Select stack frame for inspection\n"
              << "  Watches:\n"
              << "    watch <expr>        Add watch expression\n"
              << "    unwatch <id>        Remove watch\n"
              << "    watches             List all watches\n"
              << "  Other:\n"
              << "    q, quit             Exit debugger\n"
              << "    h, help             Show this help\n\n";
}

// ─────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────

static bool parse_bp_loc(const std::string& loc, std::string& file, int& line) {
    size_t colon = loc.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == loc.size() - 1) return false;
    file = loc.substr(0, colon);
    try {
        const std::string value = loc.substr(colon + 1);
        size_t consumed = 0;
        line = std::stoi(value, &consumed);
        return consumed == value.size() && line > 0;
    }
    catch (...) { return false; }
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// ─────────────────────────────────────────────────────────────
// Text REPL
// ─────────────────────────────────────────────────────────────

static void run_text_repl(ny::debug::Debugger& dbg) {
    std::string line;
    print_repl_help();

    while (true) {
        std::cout << "(nildebugger) " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            std::cout << "Exiting debugger.\n";
            break;

        } else if (cmd == "h" || cmd == "help" || cmd == "?") {
            print_repl_help();

        } else if (cmd == "r" || cmd == "run") {
            std::cout << "Running...\n";
            dbg.run();

        } else if (cmd == "s" || cmd == "step") {
            dbg.step_into();

        } else if (cmd == "n" || cmd == "next") {
            dbg.step_over();

        } else if (cmd == "o" || cmd == "out") {
            dbg.step_out();

        } else if (cmd == "c" || cmd == "continue") {
            dbg.continue_execution();

        } else if (cmd == "b" || cmd == "break") {
            std::string loc;
            if (!(iss >> loc)) { std::cout << "Usage: b <file>:<line> [if <cond>]\n"; continue; }
            std::string file; int ln;
            if (!parse_bp_loc(loc, file, ln)) {
                std::cout << "Invalid format — use: b <file>:<line>\n"; continue;
            }
            // Optional "if <condition>"
            std::string keyword, cond;
            if (iss >> keyword) {
                if (keyword != "if") {
                    std::cout << "Unexpected breakpoint argument '" << keyword
                              << "'; use: b <file>:<line> [if <condition>]\n";
                    continue;
                }
                std::getline(iss, cond);
                cond = trim(cond);
                if (cond.empty()) {
                    std::cout << "Breakpoint 'if' requires a condition.\n";
                    continue;
                }
            }
            std::string error;
            const auto id = dbg.set_breakpoint(file, ln, cond, &error);
            if (!id) {
                std::cout << "Could not set breakpoint: " << error << "\n";
                continue;
            }
            std::cout << "Breakpoint " << *id << " set at " << file << ":" << ln;
            if (!cond.empty()) std::cout << " (condition: " << cond << ")";
            std::cout << "\n";

        } else if (cmd == "d" || cmd == "delete") {
            int id;
            if (!(iss >> id)) { std::cout << "Usage: d <id>\n"; continue; }
            if (dbg.remove_breakpoint(id))
                std::cout << "Breakpoint " << id << " removed.\n";
            else
                std::cout << "No breakpoint " << id << ".\n";

        } else if (cmd == "enable") {
            int id;
            if (!(iss >> id)) { std::cout << "Usage: enable <id>\n"; continue; }
            if (dbg.enable_breakpoint(id, true))
                std::cout << "Breakpoint " << id << " enabled.\n";
            else
                std::cout << "No breakpoint " << id << ".\n";

        } else if (cmd == "disable") {
            int id;
            if (!(iss >> id)) { std::cout << "Usage: disable <id>\n"; continue; }
            if (dbg.enable_breakpoint(id, false))
                std::cout << "Breakpoint " << id << " disabled.\n";
            else
                std::cout << "No breakpoint " << id << ".\n";

        } else if (cmd == "cond") {
            int id;
            if (!(iss >> id)) { std::cout << "Usage: cond <id> [<expr>]\n"; continue; }
            std::string cond_expr;
            std::getline(iss, cond_expr);
            cond_expr = trim(cond_expr);
            std::string error;
            if (!dbg.set_breakpoint_condition(id, cond_expr, &error)) {
                std::cout << "Could not set condition: " << error << "\n";
                continue;
            }
            if (cond_expr.empty()) std::cout << "Condition cleared on breakpoint " << id << ".\n";
            else std::cout << "Condition set: breakpoint " << id << " if " << cond_expr << "\n";

        } else if (cmd == "bl" || cmd == "breakpoints") {
            auto bps = dbg.get_breakpoints();
            if (bps.empty()) { std::cout << "No breakpoints.\n"; continue; }
            std::cout << "Breakpoints:\n";
            for (const auto& bp : bps) {
                std::cout << "  [" << bp.id << "] "
                          << bp.file << ":" << bp.line
                          << (bp.enabled ? "" : " (disabled)");
                if (!bp.condition.empty()) std::cout << "  if " << bp.condition;
                std::cout << "\n";
            }

        } else if (cmd == "p" || cmd == "print") {
            std::string expr;
            std::getline(iss, expr);
            expr = trim(expr);
            if (expr.empty()) { std::cout << "Usage: p <expression>\n"; continue; }
            std::cout << expr << " = " << dbg.evaluate_expression(expr) << "\n";

        } else if (cmd == "locals") {
            int frame_idx = -1;
            iss >> frame_idx;
            const auto frames = dbg.get_vm().getDebugFrames();
            if (frames.empty()) { std::cout << "(no locals)\n"; continue; }
            int depth = frame_idx < 0 ? 0 : frame_idx;
            if (depth < 0 || depth >= static_cast<int>(frames.size())) depth = 0;
            const auto& frame = frames[frames.size() - 1U - static_cast<size_t>(depth)];
            std::cout << "Locals (frame " << depth << ", "
                      << frame.function_name << "):\n";
            for (const auto& local : dbg.get_source_locals(depth))
                std::cout << "  " << local.name << " = " << ny::debug::Debugger::format_value(local.value) << "\n";

        } else if (cmd == "regs") {
            size_t pc   = dbg.get_vm().getPC();
            int    line = dbg.get_current_line();
            std::cout << "PC=" << pc << "  Line=" << line
                      << "  CallDepth=" << dbg.get_vm().getCallDepth()
                      << "  StackDepth=" << dbg.get_vm().getStack().size() << "\n";
            if (pc < dbg.get_vm().getProgram().size()) {
                const auto& ins = dbg.get_vm().getProgram()[pc];
                std::cout << "  Instruction: op=" << static_cast<int>(ins.op);
                for (const auto& a : ins.args) std::cout << " " << a;
                std::cout << "\n";
            }

        } else if (cmd == "bt" || cmd == "backtrace" || cmd == "stack") {
            auto trace = dbg.get_stack_trace();
            std::cout << "Stack:\n";
            for (const auto& frame : trace) std::cout << "  " << frame << "\n";

        } else if (cmd == "frame") {
            int n = -1;
            iss >> n;
            dbg.set_frame(n);
            std::cout << "Inspecting frame " << dbg.get_current_frame() << "\n";

        } else if (cmd == "watch") {
            std::string expr;
            std::getline(iss, expr);
            expr = trim(expr);
            if (expr.empty()) { std::cout << "Usage: watch <expression>\n"; continue; }
            int id = dbg.add_watch(expr);
            std::cout << "Watch " << id << " added: " << expr << "\n";

        } else if (cmd == "unwatch") {
            int id;
            if (!(iss >> id)) { std::cout << "Usage: unwatch <id>\n"; continue; }
            dbg.remove_watch(id);
            std::cout << "Watch " << id << " removed.\n";

        } else if (cmd == "watches") {
            auto ws = dbg.get_watches();
            if (ws.empty()) { std::cout << "No watches.\n"; continue; }
            std::cout << "Watches:\n";
            for (const auto& w : ws)
                std::cout << "  [" << w.id << "] " << w.expr
                          << " = " << dbg.evaluate_expression(w.expr) << "\n";

        } else {
            std::cout << "Unknown command: " << cmd << "  (type 'help')\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────
// JSON REPL (IDE mode)
// ─────────────────────────────────────────────────────────────

static std::string json_str(const std::string& s) {
    return ny::debug::Debugger::json_str(s);
}

static void run_json_repl(ny::debug::Debugger& dbg,
                          std::ostringstream& program_output) {
    auto get_str = [](const ny::json::Value& request,
                      const std::string& key) -> std::string {
        const auto* value = request.get(key);
        return value && value->is_string() ? value->as_string() : std::string{};
    };

    auto get_int = [](const ny::json::Value& request,
                      const std::string& key) -> int {
        const auto* value = request.get(key);
        if (!value || !value->is_number()) return -1;
        const double number = value->as_number();
        if (!std::isfinite(number) || std::trunc(number) != number ||
            number < 0 || number > std::numeric_limits<int>::max()) {
            return -1;
        }
        return static_cast<int>(number);
    };

    auto emit = [](const std::string& j) {
        std::cout << j << "\n" << std::flush;
    };

    auto emit_program_output = [&]() {
        const std::string output = program_output.str();
        program_output.str("");
        program_output.clear();
        if (!output.empty()) {
            emit("{\"event\":\"output\",\"category\":\"stdout\",\"output\":" +
                 json_str(output) + "}");
        }
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty()) continue;

        ny::json::ParseError parse_error;
        const auto request = ny::json::parse(line, &parse_error);
        if (!request || !request->is_object()) {
            emit("{\"event\":\"error\",\"message\":" +
                 json_str("Invalid JSON request at offset " +
                          std::to_string(parse_error.offset) + ": " +
                          (parse_error.message.empty()
                               ? std::string("expected an object")
                               : parse_error.message)) + "}");
            continue;
        }

        std::string cmd = get_str(*request, "cmd");
        if (cmd.empty()) {
            emit("{\"event\":\"error\",\"message\":\"Missing 'cmd' field\"}");
            continue;
        }

        if (cmd == "run") {
            dbg.run();
            emit_program_output();

        } else if (cmd == "step") {
            dbg.step_into();
            emit_program_output();

        } else if (cmd == "next") {
            dbg.step_over();
            emit_program_output();

        } else if (cmd == "out") {
            dbg.step_out();
            emit_program_output();

        } else if (cmd == "continue") {
            dbg.continue_execution();
            emit_program_output();

        } else if (cmd == "break") {
            std::string file = get_str(*request, "file");
            int         ln   = get_int(*request, "line");
            const auto* condition_value = request->get("condition");
            if (condition_value && !condition_value->is_string()) {
                emit("{\"event\":\"error\",\"message\":\"break condition must be a string\"}");
                continue;
            }
            std::string cond = get_str(*request, "condition");
            if (file.empty() || ln <= 0) {
                emit("{\"event\":\"error\",\"message\":\"break requires file and line\"}");
                continue;
            }
            std::string error;
            const auto id = dbg.set_breakpoint(file, ln, cond, &error);
            if (!id) {
                emit("{\"event\":\"error\",\"message\":" + json_str(error) + "}");
                continue;
            }
            emit("{\"event\":\"bp_set\",\"id\":" + std::to_string(*id)
                + ",\"file\":" + json_str(file)
                + ",\"line\":" + std::to_string(ln)
                + ",\"condition\":" + json_str(cond) + "}");

        } else if (cmd == "delete") {
            int id = get_int(*request, "id");
            if (id < 0) { emit("{\"event\":\"error\",\"message\":\"delete requires id\"}"); continue; }
            if (!dbg.remove_breakpoint(id)) {
                emit("{\"event\":\"error\",\"message\":\"unknown breakpoint id\"}");
                continue;
            }
            emit("{\"event\":\"bp_removed\",\"id\":" + std::to_string(id) + "}");

        } else if (cmd == "enable") {
            int id = get_int(*request, "id");
            if (id < 0) { emit("{\"event\":\"error\",\"message\":\"enable requires id\"}"); continue; }
            if (!dbg.enable_breakpoint(id, true)) {
                emit("{\"event\":\"error\",\"message\":\"unknown breakpoint id\"}");
                continue;
            }
            emit("{\"event\":\"bp_enabled\",\"id\":" + std::to_string(id) + "}");

        } else if (cmd == "disable") {
            int id = get_int(*request, "id");
            if (id < 0) { emit("{\"event\":\"error\",\"message\":\"disable requires id\"}"); continue; }
            if (!dbg.enable_breakpoint(id, false)) {
                emit("{\"event\":\"error\",\"message\":\"unknown breakpoint id\"}");
                continue;
            }
            emit("{\"event\":\"bp_disabled\",\"id\":" + std::to_string(id) + "}");

        } else if (cmd == "cond") {
            int         id   = get_int(*request, "id");
            const auto* condition_value = request->get("condition");
            if (condition_value && !condition_value->is_string()) {
                emit("{\"event\":\"error\",\"message\":\"condition must be a string\"}");
                continue;
            }
            std::string cond = get_str(*request, "condition");
            if (id < 0) { emit("{\"event\":\"error\",\"message\":\"cond requires id\"}"); continue; }
            std::string error;
            if (!dbg.set_breakpoint_condition(id, cond, &error)) {
                emit("{\"event\":\"error\",\"message\":" + json_str(error) + "}");
                continue;
            }
            emit("{\"event\":\"bp_cond_set\",\"id\":" + std::to_string(id)
                + ",\"condition\":" + json_str(cond) + "}");

        } else if (cmd == "list_bps") {
            emit("{\"event\":\"bp_list\",\"breakpoints\":" + dbg.get_breakpoints_json() + "}");

        } else if (cmd == "eval") {
            std::string expr = get_str(*request, "expr");
            if (expr.empty()) { emit("{\"event\":\"error\",\"message\":\"eval requires expr\"}"); continue; }
            emit(dbg.eval_json(expr));

        } else if (cmd == "locals") {
            int frame_idx = get_int(*request, "frame");
            // get_locals_json returns {"frame":...,"fn":...,"vars":...}
            // strip leading '{' and prepend event field
            std::string lj = dbg.get_locals_json(frame_idx);
            emit("{\"event\":\"locals\"," + lj.substr(1));

        } else if (cmd == "regs") {
            emit("{\"event\":\"regs\"," + dbg.get_regs_json().substr(1));

        } else if (cmd == "stack") {
            emit("{\"event\":\"stack\",\"frames\":" + dbg.get_stack_json() + "}");

        } else if (cmd == "watches") {
            emit("{\"event\":\"watches\",\"items\":" + dbg.get_watches_json() + "}");

        } else if (cmd == "watch") {
            std::string expr = get_str(*request, "expr");
            if (expr.empty()) { emit("{\"event\":\"error\",\"message\":\"watch requires expr\"}"); continue; }
            int id = dbg.add_watch(expr);
            emit("{\"event\":\"watch_added\",\"id\":" + std::to_string(id)
                + ",\"expr\":" + json_str(expr) + "}");

        } else if (cmd == "unwatch") {
            int id = get_int(*request, "id");
            if (id < 0) { emit("{\"event\":\"error\",\"message\":\"unwatch requires id\"}"); continue; }
            dbg.remove_watch(id);
            emit("{\"event\":\"watch_removed\",\"id\":" + std::to_string(id) + "}");

        } else if (cmd == "frame") {
            int n = get_int(*request, "n");
            dbg.set_frame(n);
            emit("{\"event\":\"frame_selected\",\"frame\":" + std::to_string(dbg.get_current_frame()) + "}");

        } else if (cmd == "quit") {
            emit("{\"event\":\"quit\"}");
            break;

        } else {
            emit("{\"event\":\"error\",\"message\":" + json_str("Unknown command: " + cmd) + "}");
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 1; }
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--") break;
        if (std::string(argv[i]) != "--dap") continue;
        if (argc != 2) {
            std::cerr << "Error: --dap must be used alone.\n";
            return 1;
        }
        return ny::debug::run_dap(std::cin, std::cout);
    }

    // Parse flags
    bool json_mode = false;
    ny::MemoryMode memory_mode = ny::MemoryMode::UNSAFE_MANUAL;
    bool collect_program_args = false;
    bool help_requested = false;
    bool version_requested = false;
    bool json_seen = false;
    bool memory_mode_seen = false;
    std::vector<std::string> args;
    std::vector<std::string> program_args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (collect_program_args) {
            program_args.push_back(a);
        } else if (a == "--") {
            collect_program_args = true;
        } else if (a == "--json") {
            if (json_seen) {
                std::cerr << "Error: --json was provided more than once.\n";
                return 1;
            }
            json_seen = true;
            json_mode = true;
        } else if (a == "-h" || a == "--help") {
            if (help_requested) {
                std::cerr << "Error: --help was provided more than once.\n";
                return 1;
            }
            help_requested = true;
        } else if (a == "--version") {
            if (version_requested) {
                std::cerr << "Error: --version was provided more than once.\n";
                return 1;
            }
            version_requested = true;
        } else if (a.rfind("--memory-mode=", 0) == 0) {
            if (memory_mode_seen) {
                std::cerr << "Error: --memory-mode was provided more than once.\n";
                return 1;
            }
            memory_mode_seen = true;
            const std::string mode = a.substr(14);
            if (mode == "safe" || mode == "gc")
                memory_mode = ny::MemoryMode::SAFE_GC;
            else if (mode == "unsafe" || mode == "manual")
                memory_mode = ny::MemoryMode::UNSAFE_MANUAL;
            else {
                std::cerr << "Unknown memory mode: " << mode << "\n";
                return 1;
            }
        } else if (!a.empty() && a.front() == '-') {
            std::cerr << "Unknown nildebugger option: " << a << "\n";
            return 1;
        } else {
            args.push_back(a);
        }
    }

    if (help_requested || version_requested) {
        if ((help_requested && version_requested) || json_seen ||
            memory_mode_seen || !args.empty() || collect_program_args) {
            std::cerr << "Error: --help and --version must be used alone.\n";
            return 1;
        }
        if (help_requested) {
            print_help();
        } else {
            std::cout << "nildebugger " << NIL_VERSION << "\n";
        }
        return 0;
    }
    if (args.empty()) { print_help(); return 1; }

    std::string command = args[0];

    std::ostringstream program_output;
    std::istringstream program_input;
    ny::debug::Debugger dbg;

    dbg.set_json_mode(json_mode);
    dbg.get_vm().setMemoryMode(memory_mode);
    dbg.get_vm().setArgs(program_args);
    if (json_mode) {
        dbg.get_vm().setOutputStream(program_output);
        // JSON Lines commands exclusively own stdin. Until the protocol grows
        // an explicit input request/reply event, debuggee reads see EOF.
        dbg.get_vm().setInputStream(program_input);
    }

    if (command == "run") {
        if (args.size() != 2) {
            std::cerr << "Error: run requires exactly one program path; "
                         "place program arguments after --.\n";
            return 1;
        }
        std::string target = args[1];

        if (!dbg.load_program(target)) {
            if (json_mode)
                std::cout << "{\"event\":\"error\",\"message\":"
                          << ny::debug::Debugger::json_str("Failed to load: " + target)
                          << "}\n";
            else
                std::cerr << "Failed to load program: " << target << "\n";
            return 1;
        }

        if (json_mode) {
            std::cout << "{\"event\":\"started\",\"file\":"
                      << ny::debug::Debugger::json_str(target) << "}\n" << std::flush;
            run_json_repl(dbg, program_output);
        } else {
            std::cout << "Loaded: " << target << "\n";
            run_text_repl(dbg);
        }

    } else if (command == "help") {
        if (args.size() != 1 || !program_args.empty() || json_seen ||
            memory_mode_seen) {
            std::cerr << "Error: help takes no arguments.\n";
            return 1;
        }
        print_help();

    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_help();
        return 1;
    }

    return 0;
}

#ifndef NILDEBUGGER_DEBUGGER_CORE_HPP
#define NILDEBUGGER_DEBUGGER_CORE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <optional>
#include <exception>
#include <atomic>
#include "vm.hpp"

namespace ny {
namespace debug {

// ─────────────────────────────────────────────────────────────
// Breakpoint
// ─────────────────────────────────────────────────────────────
struct Breakpoint {
    int         id;
    std::string file;
    int         line;
    bool        enabled;
    std::string condition;                    // Optional condition expression
    size_t      pc = static_cast<size_t>(-1);
};

// ─────────────────────────────────────────────────────────────
// Watch expression
// ─────────────────────────────────────────────────────────────
struct Watch {
    int         id;
    std::string expr;
};

struct LocalVariable {
    std::string name;
    std::string storage;
    std::string type;
    Value value;
};

// ─────────────────────────────────────────────────────────────
// Execution state
// ─────────────────────────────────────────────────────────────
enum class ExecutionState {
    Running,
    Paused,
    Terminated,
    Detached
};

// ─────────────────────────────────────────────────────────────
// Debugger
// ─────────────────────────────────────────────────────────────
class Debugger {
public:
    Debugger();
    ~Debugger();
    Debugger(const Debugger&) = delete;
    Debugger& operator=(const Debugger&) = delete;
    Debugger(Debugger&&) = delete;
    Debugger& operator=(Debugger&&) = delete;

    // ── Session ──────────────────────────────────────────────
    bool load_program(const std::string& path);
    bool attach(int pid);
    void detach();

    // ── Execution control ────────────────────────────────────
    void run();
    void pause();
    void step_over();
    void step_into();
    void step_out();
    void continue_execution();
    // The only cross-thread debugger operation: requests a cooperative stop
    // between real VM instructions. Inspection remains confined to a stopped VM.
    void request_pause() { pause_requested_.store(true); }
    void clear_pause_request() { pause_requested_.store(false); }
    void continue_cooperatively();
    void stop_at_entry();

    // ── Breakpoints ──────────────────────────────────────────
    std::optional<int> set_breakpoint(const std::string& file, int line,
                                      const std::string& condition = "",
                                      std::string* error = nullptr);
    bool remove_breakpoint(int id);
    bool enable_breakpoint(int id, bool enable);
    bool set_breakpoint_condition(int id, const std::string& condition,
                                  std::string* error = nullptr);
    std::vector<Breakpoint> get_breakpoints() const;

    // ── Watches ──────────────────────────────────────────────
    int  add_watch(const std::string& expr);
    void remove_watch(int id);
    std::vector<Watch> get_watches() const;

    // ── Inspection ───────────────────────────────────────────
    std::string              evaluate_expression(const std::string& expr) const;
    std::vector<std::string> get_stack_trace();
    std::vector<LocalVariable> get_source_locals(int frame_idx = -1) const;

    // Structured JSON inspection (used by --json mode + IDE)
    std::string get_locals_json(int frame_idx = -1)  const;
    std::string get_regs_json()                       const;
    std::string get_stack_json()                      const;
    std::string get_watches_json()                    const;
    std::string get_breakpoints_json()                const;
    std::string get_stop_json(const std::string& reason, int bp_id = -1) const;
    std::string eval_json(const std::string& expr)    const;

    // ── State ────────────────────────────────────────────────
    ExecutionState get_state()        const { return state_; }
    std::string    get_current_file() const;
    int            get_current_line() const;
    void           set_frame(int depth);
    int            get_current_frame() const { return inspect_frame_; }

    // ── JSON mode ────────────────────────────────────────────
    void set_json_mode(bool on) { json_mode_ = on; }
    bool is_json_mode()         const { return json_mode_; }
    void set_silent_mode(bool on) { silent_mode_ = on; }
    const std::string& last_stop_reason() const { return last_stop_reason_; }
    const std::string& last_error() const { return last_error_; }
    int last_breakpoint_id() const { return last_breakpoint_id_; }

    // ── Formatting helpers (public for REPL use) ─────────────
    static std::string format_value(const Value& val);
    static std::string json_str(const std::string& s);

    // ── VM accessor ──────────────────────────────────────────
    VM& get_vm() { return vm_; }

    // ── Callbacks ────────────────────────────────────────────
    using StateCallback = std::function<void(ExecutionState)>;
    void set_state_callback(StateCallback cb);

private:
    ExecutionState          state_;
    std::vector<Breakpoint> breakpoints_;
    int                     next_breakpoint_id_ = 1;
    std::string             current_program_;

    std::vector<Watch>      watches_;
    int                     next_watch_id_ = 1;

    // Frame selected for inspection (-1 = top/most recent frame)
    int                     inspect_frame_ = -1;

    bool                    json_mode_ = false;
    bool                    execution_started_ = false;
    bool                    silent_mode_ = false;
    std::atomic<bool>       pause_requested_{false};
    std::string             last_stop_reason_;
    std::string             last_error_;
    int                     last_breakpoint_id_ = -1;

    StateCallback           state_callback_;
    VM                      vm_;

    void update_state(ExecutionState new_state);
    void emit_json(const std::string& json_line) const;

    // Value formatting helpers
    static std::string format_struct(const StructValue& sv);
    static std::string format_array(const ArrayValue& av);
    static std::string format_enum(const EnumValue& ev);
    static std::string format_element(
        const std::variant<std::string,int64_t,double,
                           StructValue,ArrayValue,EnumValue,ArrayHandle>& el);

    std::optional<Value> resolve_expression_value(
        const std::string& expr) const;

    // Conditional breakpoint evaluation
    bool eval_condition(const std::string& cond) const;
    std::optional<int> stopping_breakpoint_at(size_t pc) const;
    bool breakpoint_file_matches(const std::string& file) const;
    static bool same_file(const std::string& lhs, const std::string& rhs);
    size_t inspection_pc(int frame_idx) const;
    void source_step(bool over, bool out);
    void prepare_execution();
    void emit_stop(const std::string& reason, int bp_id = -1);
    void emit_runtime_error(const std::exception& error);

    bool on_vm_stop(VM& vm, const Instruction& instr, size_t pc);
};

} // namespace debug
} // namespace ny

#endif // NILDEBUGGER_DEBUGGER_CORE_HPP

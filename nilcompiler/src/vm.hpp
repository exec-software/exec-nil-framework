#ifndef NY_COMPILER_VM_HPP
#define NY_COMPILER_VM_HPP

#include <cstdint>
#include <fstream>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include "source_debug.hpp"
#include "database_runtime.hpp"
#include "memory_profile.hpp"
#include "submission_value.hpp"

namespace ny {

class UncaughtFault : public std::runtime_error {
public:
  UncaughtFault(const std::string &type, int line)
      : std::runtime_error("Ny uncaught fault: " + type + " at line " +
                           std::to_string(line)) {}
};

// Memory management mode
enum class MemoryMode {
  UNSAFE_MANUAL, // Manual allocation/deallocation (C/C++ style)
  SAFE_GC        // Garbage collected (automatic memory management)
};

enum class Opcode {
  // Stack/Constants
  PUSH_CONST_STR,
  PUSH_CONST,
  PUSH_FN_PTR,
  POP, // Discard top of stack (used after void expression statements)

  // Local Variables
  LOAD_LOCAL,
  STORE_LOCAL,
  REF_LOCAL,

  // Global Variables
  LOAD_GLOBAL,
  STORE_GLOBAL,

  // Control Flow
  CALL,
  RET,
  NOP,
  JMP_IF_NOT,
  JMP,
  JMP_IF,
  LABEL,

  // Comparison
  CMP_EQ,
  CMP_NE,
  CMP_LT,
  CMP_LE,
  CMP_GT,
  CMP_GE,

  // Logical
  NOT,

  // Arithmetic
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  NEG,
  BIT_AND,
  BIT_OR,
  BIT_XOR,
  SHL,
  SHR,

  // Memory
  ALLOC,
  FREE,
  READ_PTR,
  WRITE_PTR,

  // Struct
  NEW_STRUCT,
  GET_FIELD,
  SET_FIELD,
  SET_FIELD_PTR, // (*ptr).field = val — writes through pointer into heap

  // Array
  NEW_ARRAY,
  GET_INDEX,
  SET_INDEX,
  LEN,

  // Enum
  NEW_VARIANT,
  IS_VARIANT,
  UNWRAP_VARIANT,

  // String
  NEW_ARRAY_LITERAL,
  STR_CONCAT,
  TO_STRING,
  CAST,
  STRLEN,

  // FFI (Foreign Function Interface)
  // FFI (Foreign Function Interface)
  INTEROP_CALL, // Dynamic native function call

  // Math Intrinsics
  MATH_SIN,
  MATH_COS,
  MATH_TAN,
  MATH_ASIN,
  MATH_ACOS,
  MATH_ATAN,
  MATH_ATAN2,
  MATH_SQRT,
  MATH_POW,
  MATH_EXP,
  MATH_LOG,
  MATH_LOG10,
  MATH_ABS,
  MATH_FLOOR,
  MATH_CEIL,
  MATH_ROUND,
  MATH_TRUNC,
  MATH_CBRT,
  MATH_MIN,
  MATH_MAX,

  // Console Intrinsics
  CONSOLE_WRITE,
  CONSOLE_WRITELN,

  // Time Intrinsics
  TIME_NOW,   // Push current time in milliseconds
  TIME_SLEEP, // Pop milliseconds, sleep for that duration

  // Typed raw-pointer arithmetic. Appended to preserve every historical NIL
  // opcode ordinal; binary NIL stores canonical names, but enum/table identity
  // remains an enforced compatibility invariant.
  PTR_ADD,
  PTR_SUB,
  INLINE_ASM, // Native-only, preserved by textual and binary executable NIL.

  FAULT_ENTER,   // region, dispatch label; save current operand prefix
  FAULT_LEAVE,   // region; exact innermost region removal
  FAULT_RAISE,   // concrete struct type; consume payload and propagate
  FAULT_SAVE,    // slot; consume pending into rooted frame-local metadata
  FAULT_MATCH,   // slot, concrete type; push exact-identity boolean
  FAULT_BIND,    // slot, concrete type; push shared typed payload
  FAULT_DROP,    // slot; retire saved fault
  FAULT_RESUME,  // slot; consume saved fault and propagate
  FAULT_RETHROW, // slot; retain saved fault and propagate

  CLASS_UPCAST, // checked ancestor static view, preserving dynamic identity
  CALL_METHOD, // static class, declared method, argc including receiver

  PARALLEL_MAP, // direct worker, scalar input, scalar output; structured join
  OWN_ENTER, // enter a lexical affine-allocation scope
  OWN_LEAVE, // release the innermost affine-allocation scope
  OWN_BIND, // local slot; register an allocation in the current scope
  OWN_MOVE, // local slot; transfer allocation and clear the source slot
  OWN_ALLOC, // owned scalar allocation, independent of ambient GC mode

  COUNT, // Sentinel used to keep NIL decoding exhaustive
};

struct nil_module;
struct Instruction {
  Opcode op;
  std::vector<std::string> args;
  int line = 0;
  bool is_function_entry = false;
  std::size_t function_param_count = 0;
  // Empty means historical untyped NIL, not an explicit void signature.
  std::string function_return_type;
  bool is_function_end = false;
  std::vector<std::string> function_parameter_types;
  // Immutable parsed schemas accompany the program rather than being guessed
  // from instruction spellings. Only the first instruction carries this.
  std::shared_ptr<const nil_module> module_metadata;
  std::string source_file;
  std::vector<DebugLocalBinding> debug_locals;
  std::vector<std::string> function_annotations;
};

struct StructValue;
struct EnumValue; // forward declaration for use in StructData

// Heap array identity is not an integer: every i64 bit pattern remains a valid
// scalar even when its numeric value coincides with a live VM allocation.
struct ArrayHandle {
  int64_t address = 0;
};

// Struct instance: map of field names to values
struct StructData {
  std::unordered_map<std::string, int64_t> fields;
  std::unordered_map<std::string, ArrayHandle> array_fields;
  std::unordered_map<std::string, std::string> string_fields;
  std::unordered_map<std::string, double> double_fields;
  std::unordered_map<std::string, std::shared_ptr<StructValue>> struct_fields;
  std::unordered_map<std::string, EnumValue> enum_fields;
};

// Struct instance: map of field names to values (Reference Semantics)
struct StructValue {
  std::string type_name;
  std::string static_class_view;
  std::shared_ptr<StructData> data = std::make_shared<StructData>();

  // Helper delegators
  auto& fields() { return data->fields; }
  const auto& fields() const { return data->fields; }
  auto& array_fields() { return data->array_fields; }
  const auto& array_fields() const { return data->array_fields; }
  auto& string_fields() { return data->string_fields; }
  const auto& string_fields() const { return data->string_fields; }
  auto& double_fields() { return data->double_fields; }
  const auto& double_fields() const { return data->double_fields; }
  auto& struct_fields() { return data->struct_fields; }
  const auto& struct_fields() const { return data->struct_fields; }
  auto& enum_fields() { return data->enum_fields; }
  const auto& enum_fields() const { return data->enum_fields; }
};

using EnumPayload =
    std::variant<std::string, int64_t, double, StructValue,
                 std::shared_ptr<EnumValue>, ArrayHandle>;

// Enum variant instance - stores variant name and payload values
struct EnumValue {
  std::string variant_name;  // e.g., "Option::Some" or "Result::Ok"
  std::vector<EnumPayload> payloads;
  // Concrete NEW_VARIANT/intrinsic result identity survives generic unit tags.
  std::string type_name;
};

// Array instance
struct ArrayValue {
  std::vector<std::variant<std::string, int64_t, double, StructValue, ArrayValue, EnumValue, ArrayHandle>> elements;
};

// NyForeignFunc — lightweight handle stored in globals to represent VM-visible
// native/adapted functions. It's intentionally POD so it can be placed inside
// Value (std::variant) without depending on Value itself.
namespace ny {
struct NyForeignFunc {
  void *fn_ptr = nullptr; // function pointer (adapter) with signature Value(const std::vector<Value>&, void*)
  int arity = 0;
  NyForeignFunc() = default;
  NyForeignFunc(void *p, int a) : fn_ptr(p), arity(a) {}
  // Note: call helper is implemented as a free function to avoid circular
  // dependency between NyForeignFunc and Value (Value is defined after this
  // struct and is a std::variant that contains NyForeignFunc).
};
} // namespace ny

using Value = std::variant<std::string, int64_t, double, StructValue, ArrayValue, EnumValue, ny::NyForeignFunc, ArrayHandle>;

// Forward declaration — VM is defined after Value because Value is in VM's stack/heap.
class VM;

// Free function to invoke a NyForeignFunc. Placed after Value so it can use
// the Value type without introducing circular header dependencies.
Value callNyForeignFunc(const ny::NyForeignFunc &ff, const std::vector<Value> &args, VM *vm);

class VM {
public:
  VM();
  void loadProgram(const std::vector<Instruction> &program,
                   const std::unordered_map<std::string, Value> &constants);
  void execute(bool reset = true);

  // Opt-in admission and logical resource bounds for a fresh pure submission.
  // Limits cover VM work and owned snapshots, not process RSS. Configure before
  // loading a program; ambient profiling and effectful opcodes are disabled.
  void configurePureExecution(pure_execution_limits limits = {});
  submission_value snapshotPureValue(const Value &value,
                                      const std::string &type) const;
  std::size_t pureInstructionsExecuted() const { return pure_instructions_; }
  std::size_t pureWorkBytesCharged() const { return pure_work_bytes_; }


  // Memory mode configuration
  void setMemoryMode(MemoryMode mode) { memory_mode_ = mode; }
  MemoryMode getMemoryMode() const { return memory_mode_; }

  // Arguments
  void setArgs(const std::vector<std::string>& args) { args_ = args; }
  const std::vector<std::string>& getArgs() const { return args_; }

  // Debugger Support
  enum class DebugState { RUNNING, PAUSED, STEPPING };

  struct DebugFrameSnapshot {
    std::string function_name;
    size_t return_pc = 0;
    std::unordered_map<std::string, Value> locals;
  };

  void setBreakpoint(size_t pc) { breakpoints_.insert(pc); }
  void clearBreakpoint(size_t pc) { breakpoints_.erase(pc); }
  bool hasBreakpoint(size_t pc) const { return breakpoints_.count(pc) > 0; }

  DebugState getDebugState() const { return debug_state_; }
  void setDebugState(DebugState state) { debug_state_ = state; }

  // Prepare a pristine invocation of a named zero-argument entry point without
  // executing it. Arguments, memory mode, breakpoints, and debugger callbacks
  // are configuration and therefore survive the reset; all program-owned
  // runtime state does not.
  void resetExecution(const std::string &entry_point);

  // Execute exactly the instruction at the current PC without invoking the
  // asynchronous breakpoint callback. Returns true while another instruction
  // remains. This is the primitive used by debugger step policies.
  bool stepInstruction();

  // Callback: returns true to continue, false to pause
  using DebugCallback =
      std::function<bool(VM& vm, const Instruction& instr, size_t pc)>;
  void setDebugCallback(DebugCallback cb) { debug_callback_ = std::move(cb); }

  const std::vector<Instruction>& getProgram() const { return program_; }
  size_t getPC() const { return pc_; }

  // Entry-point and process-result queries for standalone hosts. These do not
  // alter execution, so debugger stepping and embedding behavior remain
  // unchanged.
  bool hasFunction(const std::string &name) const {
    return functions_.find(name) != functions_.end();
  }
  int64_t getProgramExitCode() const {
    if (stack_.empty())
      return 0;
    const auto *result = std::get_if<int64_t>(&stack_.back());
    return result ? *result : 0;
  }

  // Stable, read-only debugger snapshots over the VM's real call stack.
  const std::vector<Value>& getStack() const { return stack_; }
  std::vector<DebugFrameSnapshot> getDebugFrames() const;
  size_t getCallDepth() const { return call_stack_.size(); }

  // Hosts that multiplex VM output with a control protocol can redirect normal
  // Ny stdout without replacing process-wide std::cout's stream buffer.
  void setOutputStream(std::ostream &stream) { output_stream_ = &stream; }
  void resetOutputStream() { output_stream_ = nullptr; }

  // Input is independently routable so a host control protocol never has to
  // share its command stream with console.read_line. EOF is an explicit VM
  // error rather than an implicit empty line.
  void setInputStream(std::istream &stream) { input_stream_ = &stream; }
  void resetInputStream() { input_stream_ = nullptr; }

  // Standalone nilinterpreter keeps process-exit behavior by default. Embedded
  // hosts and the debugger can trap std_sys.exit and inspect the requested
  // code without allowing Ny code to terminate the host process.
  void setProcessExitTrapping(bool trap) { trap_process_exit_ = trap; }
  std::optional<int> getRequestedExitCode() const {
    return requested_exit_code_;
  }

  // Public function call API used by native embedders.
  void callFunction(const std::string &name, size_t arg_count = 0);

  // Push a value onto the stack (use before callFunction to pass args)
  void pushValue(const Value &v) { stack_.push_back(v); }

  // Global variable access (used by embedded scripts that need injected context)
  void setGlobal(const std::string &name, const Value &v) { globals_[name] = v; }
  Value getGlobal(const std::string &name) const {
    auto it = globals_.find(name);
    if (it != globals_.end()) return it->second;
    return int64_t(0);
  }
  bool hasGlobal(const std::string &name) const { return globals_.count(name) > 0; }

private:
  std::optional<pure_execution_limits> pure_limits_;
  std::size_t pure_instructions_ = 0;
  std::size_t pure_work_bytes_ = 0;
  void preflightPureInstruction(const Instruction &instruction);
  void chargePureWork(std::size_t bytes);
  void chargePureValue(const Value &value, unsigned depth, std::size_t &nodes);
  void validatePureProgram(const std::vector<Instruction> &program,
                           const std::unordered_map<std::string, Value> &constants) const;

  MemoryMode memory_mode_ = MemoryMode::UNSAFE_MANUAL;  // Default to unsafe
  std::vector<std::string> args_;

  // Debug State
  std::unordered_set<size_t> breakpoints_;
  DebugState debug_state_ = DebugState::RUNNING;
  DebugCallback debug_callback_;
  bool trap_process_exit_ = false;
  std::optional<int> requested_exit_code_;

  std::vector<Instruction> program_;
  std::shared_ptr<const nil_module> class_metadata_;
  std::unordered_map<std::string, Value> constants_;
  // A simple call frame description stored on the call stack. It keeps the
  // return program-counter and the local variable map for the function.
  struct CallFrame {
    size_t return_pc = 0; // where to resume after RET
    // Values below this floor belong to the caller, not this invocation.
    // Arguments sit above the floor until the parameter STORE_LOCAL preamble.
    size_t stack_base = 0;
    std::string return_type;
    size_t entry_pc = 0;
    size_t end_pc = 0;
    bool returned = false;
    std::size_t owned_depth = 0;
    std::string fn_name;  // optional function name for debugging
    std::unordered_map<std::string, Value> locals; // locals for this frame
  };

  // Call stack: each frame represents an active function invocation.
  std::vector<CallFrame> call_stack_;
  struct OwnedScope {
    std::size_t frame_depth = 0;
    std::unordered_map<std::string, std::int64_t> slots;
  };
  std::vector<OwnedScope> owned_scopes_;
  void unwindOwned(std::size_t depth);

  struct FaultValue {
    std::string type_name;
    StructValue payload;
    int source_line = 0;
  };
  struct SavedFault {
    std::size_t frame_depth = 0;
    std::string slot;
    FaultValue fault;
  };
  struct FaultRegion {
    std::size_t frame_depth = 0;
    std::size_t stack_height = 0;
    std::string id;
    std::size_t dispatch_pc = 0;
    std::size_t owned_depth = 0;
    std::vector<std::string> saved_slots;
  };
  std::optional<FaultValue> pending_fault_;
  std::vector<SavedFault> saved_faults_;
  std::vector<FaultRegion> fault_regions_;
  void propagateFault();
  SavedFault &savedFault(const std::string &slot);

  // Global variables: module-level storage accessible from all frames.
  std::unordered_map<std::string, Value> globals_;

  // Labels mapping and data stack remain as before.
  std::unordered_map<std::string, size_t> labels_;

  // Function registry: maps function name to entry point and parameter info
  struct FunctionInfo {
    size_t entry_pc;
    size_t param_count;
    std::string return_type;
    size_t end_pc;
    std::vector<std::string> parameter_types;
  };
  std::unordered_map<std::string, FunctionInfo> functions_;

  // Heap management for manual memory (unsafe mode) and GC (safe mode)
  database::Context databases_;
  struct HeapBlock {
    std::string type_name;
    // Logical element metadata for type_name == "array". Textual and binary
    // NIL both preserve this value on NEW_ARRAY_LITERAL so scalar-width
    // canonicalization is identical to native runtime arrays.
    std::string element_type;
    size_t element_count;
    std::vector<Value> data;     // Stores generic Values
    std::vector<char> byte_data; // For raw byte storage (strings, buffers)
    bool marked = false;         // GC mark flag
  };
  struct HeapPointerLocation {
    int64_t base = 0;
    HeapBlock *block = nullptr;
    std::uint64_t element_index = 0;
    std::uint64_t stride = 0;
  };
  std::unordered_map<int64_t, HeapBlock> heap_;
  int64_t next_ptr_ =
      0x400000000; // Start at 16GB to avoid collisions with native handles
  size_t gc_threshold_ = 1024; // Number of allocations before auto-GC
  size_t alloc_count_ = 0;     // Current allocation count
  std::uint64_t total_collections_ = 0;
  std::uint64_t total_allocated_bytes_ = 0;
  std::uint64_t gc_critical_depth_ = 0;
  std::unordered_set<const StructData *> gc_marked_structs_;

  // One capture belongs to the VM's first execution. It is finalized while
  // heap blocks are still live, so retained bytes describe the program at its
  // execution boundary rather than the VM destructor's housekeeping.
  memory_profile::Session memory_profile_;
  bool memory_profile_started_ = false;
  bool memory_profile_finished_ = false;
  bool memory_profile_allowed_ = true;
  void beginMemoryProfile();
  bool finishMemoryProfile(const char *status, bool completed) noexcept;
  std::uint64_t profileHeapBlockBytes(const HeapBlock &block) const;
  std::uint64_t profileHeapBytes() const;
  void releaseHeapBlock(int64_t pointer);

  // Interpreter-owned streams never expose host descriptors to Ny code. A
  // monotonically increasing token indexes the stream and its access rights;
  // dropping the VM closes every remaining stream through RAII.
  struct FileHandle {
    std::unique_ptr<std::fstream> stream;
    bool readable = false;
    bool writable = false;
  };
  std::unordered_map<int64_t, FileHandle> file_handles_;
  int64_t next_file_handle_ = 1;

  // Type size helper (simplified - all types are 8 bytes for now)
  size_t get_type_size(const std::string &type_name) const {
    return 8; // All types treated as 64-bit for simplicity
  }

  // GC Methods (mark-sweep)
  void gcCollect();                   // Run garbage collection
  void gcMarkRoots();                 // Mark all reachable objects from roots
  void gcMarkValue(const Value &val); // Mark pointer values
  void gcSweep();                     // Free unmarked objects
  std::uint64_t heapBlockBytes(const HeapBlock &block) const;
  std::uint64_t currentHeapBytes() const;
  void storeHeapBlock(int64_t pointer, HeapBlock block);
  std::optional<HeapPointerLocation> findHeapPointer(int64_t pointer,
                                                     bool allow_one_past);
  std::optional<int64_t> moveHeapPointer(const HeapPointerLocation &location,
                                         int64_t amount, bool subtract) const;

  std::vector<Value> stack_;
  size_t pc_; // program counter
  std::istream *input_stream_ = nullptr;
  std::ostream *output_stream_ = nullptr;

  // Internal execution helpers
  void executeInstruction(const Instruction &instr);
  Value popStack();
  void pushStack(const Value &val);
  size_t availableStackValues() const;
  void requireStackValues(size_t count, const std::string &operation) const;
  void validateInstructionLocation() const;
  std::istream &inputStream();
  std::ostream &outputStream();
  void resetRuntimeState(const std::string &root_function);

  // Per-frame helper APIs
  // Push a new call frame with a return pc and (optional) function name.
  void pushFrame(const std::string &fn_name, size_t return_pc,
                 size_t argument_count = 0);

  // Pop the top call frame and return to caller semantics.
  void popFrame();

  // Helpers to get/set locals in the current frame.
  bool hasLocal(const std::string &name) const;
  void setLocal(const std::string &name, const Value &v);
  Value getLocal(const std::string &name) const;

  // Call/return helpers
  void returnFromCall();
  void validateClassValue(const Value &value, const std::string &expected) const;
  std::string classFieldType(const StructValue &value, const std::string &field) const;
};

} // namespace ny

#endif // NY_COMPILER_VM_HPP

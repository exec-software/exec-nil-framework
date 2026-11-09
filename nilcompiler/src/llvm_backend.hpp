#ifndef NY_COMPILER_LLVM_BACKEND_HPP
#define NY_COMPILER_LLVM_BACKEND_HPP

#include "vm.hpp"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <functional>

// Only compile native codegen if LLVM is available
#ifdef NY_HAS_LLVM

namespace ny {

struct nil_module;

// Returns the exact argument count for legacy builtin CALL names only when
// the module has no same-named Ny definition. Tooling and native lowering use
// this same declaration-first resolution contract.
std::optional<std::size_t>
native_builtin_argument_count(const nil_module &mod, const std::string &name);

// Native code generator using LLVM
// Implementation details hidden in .cpp file
class NativeCodegen {
public:
  NativeCodegen();
  ~NativeCodegen();

  // Initialize LLVM and JIT
  bool initialize();

  // Generate native code from NIL module
  bool generateModule(const nil_module& mod);

  // Compile to object file
  bool emitObjectFile(const std::string& path);

  // Execute JIT-compiled code
  int executeJIT(const std::vector<std::string>& program_args);

  // Load external dynamic library (DLL/.so) for JIT symbol resolution
  bool loadLibrary(const std::string& path);

  // Memory mode for allocation behavior
  void setMemoryMode(MemoryMode mode);
  MemoryMode getMemoryMode() const;

  // Emit the entry sequence under the documented freestanding symbol
  // `__ny_entry`, with no arguments, instead of the hosted C `main(int,
  // char **)`. A target without a C runtime then has one named entry to call
  // rather than a wrapper it has to recognise and feed argc/argv. The
  // sequence itself -- runtime ABI check, context begin, global
  // initialisation, user main, context end -- is unchanged.
  void setFreestandingEntry(bool enabled);

  // Mark every generated definition `noredzone`, so no leaf function uses
  // the 128 bytes below the stack pointer. A kernel or interrupt-reentrant
  // target needs that; a user-space target whose interrupt entry switches
  // stacks does not, so this stays independent of the freestanding entry
  // rather than being implied by it.
  void setDisableRedZone(bool enabled);

  // Write the generated LLVM IR, so what the backend produced can be
  // inspected and asserted rather than inferred from an object.
  bool emitLlvmIr(const std::string &path);

  // Opt in to a retained, read-only context manifest on supported native
  // targets. Annotation contents are embedded verbatim; the default is off.
  void setEmbedContext(bool enabled);

private:
  // PIMPL pattern to hide LLVM implementation details
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ny

#endif // NY_HAS_LLVM

#endif // NY_COMPILER_LLVM_BACKEND_HPP

// Compile the native-codegen interface after the real Windows RPC headers.
// These headers define `small` as `char`; the compiler's declarations must
// coexist with that caller environment without undefining its macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <rpcndr.h>

#ifndef small
#error Windows RPC small macro is required for this regression
#endif

#include "llvm_backend.hpp"
#include <type_traits>

static_assert(std::is_same<small, char>::value,
              "Including the compiler interface must preserve Windows macros");
static_assert(std::is_enum<ny::NativeCodegen::CodeModel>::value,
              "Native code-model declarations must remain valid after Windows headers");

void native_windows_header_contract(ny::NativeCodegen &codegen) {
  using Model = ny::NativeCodegen::CodeModel;
  codegen.setCodeModel(Model::Host);
  codegen.setCodeModel(Model::Small);
  codegen.setCodeModel(Model::Large);
}

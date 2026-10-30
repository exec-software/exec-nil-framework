/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "codegen.hpp"
#include "native_runtime.hpp"
#include "nil_loader.hpp"
#include "ui_runtime.hpp"
#include "vm.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace {
int checks = 0;
void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

#if defined(__linux__)
// Inspect the toolkit's real window registry, independently of the provider's
// handle map. A stale-handle rejection alone would not prove GTK was released.
struct Windows {
  void *library = nullptr;
  void *(*list)() = nullptr;
  unsigned (*size)(void *) = nullptr;
  unsigned baseline = 0;

  Windows() {
    require(ny::ui::available(), "GTK 4 is required for lifecycle --display tests");
    ny::ui::Context bootstrap;
    const auto window = bootstrap.window("UI lifecycle initialization", 100, 100);
    require(static_cast<bool>(window), window.error);
    require(static_cast<bool>(bootstrap.close(window.number)), "close initialization window");
    library = dlopen("libgtk-4.so.1", RTLD_NOW | RTLD_LOCAL);
    require(library != nullptr, "load GTK registry observer");
    list = reinterpret_cast<decltype(list)>(dlsym(library, "gtk_window_get_toplevels"));
    size = reinterpret_cast<decltype(size)>(dlsym(library, "g_list_model_get_n_items"));
    require(list && size, "GTK registry observer symbols");
    baseline = count();
  }
  ~Windows() { if (library) dlclose(library); }
  unsigned count() const { return size(list()); }
  void live(const std::string &operation) const {
    require(count() == baseline + 1, operation + " must keep exactly one GTK window live");
  }
  void empty(const std::string &operation) const {
    require(count() == baseline, operation + " must release its GTK window");
  }
};

const std::string number_result = "nil::std::core::Result<i64,text>";
const std::string text_result = "nil::std::core::Result<text,text>";
ny::nil_function function(std::string name, std::vector<std::string> parameters,
                          std::string result, std::vector<ny::nil_instruction> body) {
  ny::nil_function value;
  value.name = std::move(name); value.params = std::move(parameters);
  value.return_type = std::move(result); value.body = std::move(body);
  return value;
}

struct Program {
  std::vector<ny::Instruction> code;
  std::unordered_map<std::string, ny::Value> constants;
};
Program fixture(bool binary, bool fault) {
  ny::nil_module module;
  module.module_name = "ui_lifecycle";
  module.enums = {{number_result, {{"Ok", "i64"}, {"Error", "text"}}},
                  {text_result, {{"Ok", "text"}, {"Error", "text"}}}};
  module.string_constants = {{"title", "UI lifecycle VM window"}};
  module.functions.push_back(function("open_window", {"title:text", "width:i64", "height:i64"}, number_result,
      {{"STORE_LOCAL", {"height"}}, {"STORE_LOCAL", {"width"}}, {"STORE_LOCAL", {"title"}},
       {"LOAD_LOCAL", {"title"}}, {"LOAD_LOCAL", {"width"}}, {"LOAD_LOCAL", {"height"}},
       {"INTEROP_CALL", {"intrinsic", "ui.v1.window", "3", number_result}}, {"RET", {}}}));
  module.functions.push_back(function("read_window", {"handle:i64"}, text_result,
      {{"STORE_LOCAL", {"handle"}}, {"LOAD_LOCAL", {"handle"}},
       {"INTEROP_CALL", {"intrinsic", "ui.v1.read_text", "1", text_result}}, {"RET", {}}}));
  std::vector<ny::nil_instruction> body{
      {"PUSH_CONST_STR", {"title"}}, {"PUSH_CONST", {"240"}}, {"PUSH_CONST", {"120"}},
      {"CALL", {"open_window", "3"}}, {"UNWRAP_VARIANT", {"1", number_result, "i64"}},
      {"STORE_GLOBAL", {"opened", "i64"}}};
  if (fault) {
    body.push_back({"PUSH_CONST", {"1"}});
    body.push_back({"PUSH_CONST", {"0"}});
    body.push_back({"DIV", {"i64"}});
  } else body.push_back({"PUSH_CONST", {"0"}});
  body.push_back({"RET", {}});
  module.functions.push_back(function("main", {}, "i64", std::move(body)));

  std::ostringstream serialized(std::ios::out | std::ios::binary);
  if (binary) {
    ny::codegen_options options;
    ny::nil_binary_writer writer(serialized, options); writer.emit_binary_module(module);
  } else {
    ny::nil_writer writer(serialized); writer.emit_text_module(module);
  }
  std::istringstream input(serialized.str(), std::ios::in | std::ios::binary);
  Program result;
  result.code = binary ? ny::parseBinaryProgram(input, result.constants)
                       : ny::parseProgram(input, result.constants);
  return result;
}

std::int64_t pause_with_window(ny::VM &vm, const Program &program, const Windows &windows) {
  vm.loadProgram(program.code, program.constants);
  vm.resetExecution("main");
  for (unsigned step = 0; step < 100 && !vm.hasGlobal("opened"); ++step)
    require(vm.stepInstruction(), "VM ended before storing the live window handle");
  require(vm.hasGlobal("opened"), "VM must reach the UI lifecycle pause");
  const auto handle = std::get<std::int64_t>(vm.getGlobal("opened"));
  require(handle > 0, "UI window creation must return a positive handle");
  require(vm.getDebugState() == ny::VM::DebugState::PAUSED, "window fixture must be debugger-paused");
  windows.live("debugger pause");
  return handle;
}

void stale_vm_handle(ny::VM &vm, std::int64_t handle) {
  vm.pushValue(handle);
  vm.callFunction("read_window", 1);
  require(!vm.getStack().empty(), "stale VM read must return its Result");
  const auto *result = std::get_if<ny::EnumValue>(&vm.getStack().back());
  require(result && result->variant_name == "Result::Error" && result->payloads.size() == 1,
          "VM invocation boundary must invalidate the previous window handle");
  const auto *error = std::get_if<std::string>(&result->payloads.front());
  require(error && error->find("closed") != std::string::npos, "VM stale handle error must explain closure");
}

void vm_lifecycles(const Windows &windows) {
  for (bool binary : {false, true}) {
    const auto normal = fixture(binary, false);
    const auto failing = fixture(binary, true);
    {
      ny::VM vm;
      auto handle = pause_with_window(vm, normal, windows);
      vm.execute(false);
      windows.empty("VM normal completion");
      stale_vm_handle(vm, handle);

      (void)pause_with_window(vm, normal, windows);
      vm.resetExecution("main");
      windows.empty("VM debugger reset");
      require(!vm.hasGlobal("opened"), "debugger reset must clear the previous program globals");

      handle = pause_with_window(vm, normal, windows);
      vm.loadProgram(normal.code, normal.constants);
      windows.empty("VM program reload");
      stale_vm_handle(vm, handle);

      (void)pause_with_window(vm, failing, windows);
      bool caught = false;
      try { vm.execute(false); }
      catch (const std::runtime_error &error) {
        caught = std::string(error.what()).find("Division by zero") != std::string::npos;
      }
      require(caught, "VM must actually execute and report the division fault");
      windows.empty("VM uncaught runtime fault");
    }
    {
      auto vm = std::make_unique<ny::VM>();
      (void)pause_with_window(*vm, normal, windows);
      vm.reset();
      windows.empty("paused VM destruction");
    }
  }
}

std::int64_t native_window() {
  std::int64_t handle = 0;
  const char *error = nullptr;
  const auto status = nyrt_ui_v1_window("UI lifecycle native window", 240, 120, &handle, &error);
  require(status == 1 && handle > 0 && !error, error ? error : "native window result");
  return handle;
}
void stale_native_handle(std::int64_t handle) {
  const char *value = nullptr, *error = nullptr;
  require(nyrt_ui_v1_read_text(handle, &value, &error) == 0 && !value && error &&
          std::string(error).find("closed") != std::string::npos,
          "native context boundary must invalidate the previous window handle");
}
void native_lifecycles(const Windows &windows) {
  nyrt_context_begin(0, nullptr);
  auto handle = native_window();
  windows.live("native invocation");
  // A recoverable provider Result error must not destroy the valid window.
  stale_native_handle(-1);
  windows.live("recoverable native provider error");
  const char *value = nullptr, *error = nullptr;
  require(nyrt_ui_v1_read_text(handle, &value, &error) == 1 && value && !error,
          "native live window title read");
  const void *roots[] = {&value};
  nyrt_roots_enter(roots, 1);
  nyrt_collect();
  require(std::string(value) == "UI lifecycle native window", "native UI text must survive collection while rooted");
  std::int64_t changed = 0;
  require(nyrt_ui_v1_set_text(handle, "Changed title", &changed, &error) == 1 && changed == 1,
          "native UI title mutation");
  require(std::string(value) == "UI lifecycle native window", "native UI reads must copy provider text");
  nyrt_roots_leave(roots);
  nyrt_context_end();
  windows.empty("native invocation completion");
  stale_native_handle(handle);

  nyrt_context_begin(0, nullptr);
  handle = native_window();
  windows.live("native invocation before restart");
  nyrt_context_begin(0, nullptr);
  windows.empty("native invocation restart");
  stale_native_handle(handle);
  nyrt_context_end();
  windows.empty("idempotent native completion");
}
#endif
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2 && std::string(argv[1]) == "--display",
            "usage: nil_ui_lifecycle_tests --display (requires an isolated display)");
#if defined(__linux__)
    Windows windows;
    vm_lifecycles(windows);
    native_lifecycles(windows);
    std::cout << "UI lifecycle: " << checks << " checks passed\n";
    return 0;
#else
    std::cerr << "SKIP: GTK UI lifecycle tests require Linux\n";
    return 77;
#endif
  } catch (const std::exception &error) {
    std::cerr << "UI lifecycle test failure: " << error.what() << '\n';
    return 1;
  }
}

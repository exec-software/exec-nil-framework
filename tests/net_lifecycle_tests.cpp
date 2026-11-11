// The VM must release sockets while its host process is still alive.
#include "net_support.hpp"
#include "codegen.hpp"
#include "nil_loader.hpp"
#include "vm.hpp"
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
namespace net = ny::net_support;
int checks = 0;
void require(bool value, const std::string &message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
struct Socket {
  net::socket_descriptor value;
  explicit Socket(net::socket_descriptor descriptor) : value(descriptor) {}
  ~Socket() { if (value != net::invalid_socket) net::close_descriptor(value); }
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
};
int unused_port() {
  std::string error;
  require(net::ensure_started(error), error);
  Socket probe(::socket(AF_INET, SOCK_STREAM, 0));
  require(probe.value != net::invalid_socket, "open loopback port probe");
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  require(::bind(probe.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "bind port probe");
#ifdef _WIN32
  int size = sizeof(address);
#else
  socklen_t size = sizeof(address);
#endif
  require(::getsockname(probe.value, reinterpret_cast<sockaddr *>(&address), &size) == 0, "read port probe");
  return ntohs(address.sin_port);
}
void endpoint(int port, bool held, const std::string &phase) {
  const auto probe = net::listen_on("127.0.0.1", port, 8);
  Socket owned(probe.descriptor);
  require(probe.ok() != held, phase + (held ? " must retain the actual listening socket" : " must release the actual listening socket"));
}
const std::string result_type = "nil::std::core::Result<i64,text>";
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
Program fixture(int port, bool binary, bool fault) {
  ny::nil_module module;
  module.module_name = "net_lifecycle";
  module.enums = {{result_type, {{"Ok", "i64"}, {"Error", "text"}}}};
  module.string_constants = {{"host", "127.0.0.1"}};
  std::vector<ny::nil_instruction> body{
    {"PUSH_CONST_STR", {"host"}}, {"PUSH_CONST", {std::to_string(port)}}, {"PUSH_CONST", {"8"}},
    {"INTEROP_CALL", {"intrinsic", "net.listen", "3", result_type}},
    {"UNWRAP_VARIANT", {"1", result_type, "i64"}}, {"STORE_GLOBAL", {"opened", "i64"}},
    {"LOAD_GLOBAL", {"opened", "i64"}}, {"PUSH_CONST", {"1"}},
    {"INTEROP_CALL", {"intrinsic", "net.watch", "2", result_type}},
    {"UNWRAP_VARIANT", {"1", result_type, "i64"}}, {"STORE_GLOBAL", {"watched", "i64"}}};
  if (fault) {
    body.push_back({"PUSH_CONST", {"1"}}); body.push_back({"PUSH_CONST", {"0"}});
    body.push_back({"DIV", {"i64"}});
  } else body.push_back({"PUSH_CONST", {"0"}});
  body.push_back({"RET", {}});
  module.functions.push_back(function("main", {}, "i64", std::move(body)));
  module.functions.push_back(function("watch_count", {}, "i64",
    {{"INTEROP_CALL", {"intrinsic", "net.watch_count", "0", "i64"}}, {"RET", {}}}));
  module.functions.push_back(function("is_open", {"handle:i64"}, "bool",
    {{"STORE_LOCAL", {"handle"}}, {"LOAD_LOCAL", {"handle"}},
     {"INTEROP_CALL", {"intrinsic", "net.is_open", "1", "bool"}}, {"RET", {}}}));
  std::ostringstream bytes(std::ios::out | std::ios::binary);
  if (binary) {
    ny::codegen_options options; ny::nil_binary_writer writer(bytes, options); writer.emit_binary_module(module);
  } else { ny::nil_writer writer(bytes); writer.emit_text_module(module); }
  std::istringstream input(bytes.str(), std::ios::in | std::ios::binary);
  Program program;
  program.code = binary ? ny::parseBinaryProgram(input, program.constants) : ny::parseProgram(input, program.constants);
  return program;
}
std::int64_t pause(ny::VM &vm, const Program &program, int port) {
  vm.loadProgram(program.code, program.constants);
  vm.resetExecution("main");
  for (int i = 0; i < 50 && !vm.hasGlobal("watched"); ++i)
    require(vm.stepInstruction(), "program must reach its watched listener");
  require(vm.hasGlobal("watched") && std::get<std::int64_t>(vm.getGlobal("watched")) == 1,
          "exactly one listener is watched before pause");
  endpoint(port, true, "paused invocation");
  const auto token = std::get<std::int64_t>(vm.getGlobal("opened"));
  require(token > 0, "listener token is positive");
  return token;
}
void cleared(ny::VM &vm, std::int64_t token) {
  vm.pushValue(token); vm.callFunction("is_open", 1);
  require(std::get<std::int64_t>(vm.getStack().back()) == 0, "terminal socket token is stale");
  vm.callFunction("watch_count", 0);
  require(std::get<std::int64_t>(vm.getStack().back()) == 0, "terminal clears the watch set");
}
void lifecycle(bool binary) {
  const int port = unused_port();
  const auto normal = fixture(port, binary, false);
  const auto faulty = fixture(port, binary, true);
  ny::VM vm;
  const auto first = pause(vm, normal, port);
  vm.execute(false);
  endpoint(port, false, "normal completion");
  cleared(vm, first);

  const auto second = pause(vm, normal, port);
  require(second > first, "completed invocation tokens are never reissued");
  vm.resetExecution("main");
  endpoint(port, false, "debugger reset");
  // resetExecution leaves a prepared main frame, so invoking a separate host
  // function here would escape that frame without RET. The bind probe above
  // observes actual release without changing or executing the prepared frame.

  const auto third = pause(vm, normal, port);
  require(third > second, "reset invocation tokens are never reissued");
  vm.loadProgram(normal.code, normal.constants);
  endpoint(port, false, "program replacement");
  cleared(vm, third);

  const auto fourth = pause(vm, faulty, port);
  bool failed = false;
  try { vm.execute(false); }
  catch (const std::runtime_error &error) { failed = std::string(error.what()).find("Division by zero") != std::string::npos; }
  require(failed, "fault fixture must actually execute its division error");
  endpoint(port, false, "uncaught runtime error");
  require(fourth > third, "faulting invocation also receives a fresh token");

  {
    auto paused = std::make_unique<ny::VM>();
    pause(*paused, normal, port);
    paused.reset();
  }
  endpoint(port, false, "paused VM destruction");
}
}

int main() {
  try {
    lifecycle(false); lifecycle(true);
    std::cout << "Socket VM lifecycle: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Socket lifecycle failure: " << error.what() << '\n';
    return 1;
  }
}

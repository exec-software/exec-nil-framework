#include "process_runner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

int failures = 0;

#ifndef _WIN32
volatile sig_atomic_t termination_requested = 0;

void note_termination(int) { termination_requested = 1; }
#endif

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

fs::path self_path(const char *invocation) {
  std::error_code error;
  fs::path result = fs::absolute(invocation ? invocation : "", error);
  return error ? fs::path(invocation ? invocation : "")
               : result.lexically_normal();
}

int spawn_grandchild(const char *executable, const char *ready,
                     const char *marker) {
#ifdef _WIN32
  const intptr_t grandchild =
      _spawnl(_P_NOWAIT, executable, executable, "--grandchild-write", ready,
              marker, static_cast<const char *>(nullptr));
  if (grandchild == -1)
    return 91;
  CloseHandle(reinterpret_cast<HANDLE>(grandchild));
#else
  const pid_t grandchild = ::fork();
  if (grandchild < 0)
    return 91;
  if (grandchild == 0) {
    ::execl(executable, executable, "--grandchild-write", ready, marker,
            static_cast<char *>(nullptr));
    ::_exit(92);
  }
#endif
  return 0;
}

bool wait_for_file(const fs::path &path, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code error;
    if (fs::is_regular_file(path, error) && !error)
      return true;
    std::this_thread::sleep_for(5ms);
  }
  std::error_code error;
  return fs::is_regular_file(path, error) && !error;
}

int child_mode(int argc, char **argv) {
  const std::string mode = argv[1];
#ifndef _WIN32
  if (mode == "--child-ignore-term") {
    struct sigaction ignored{};
    ignored.sa_handler = SIG_IGN;
    ::sigemptyset(&ignored.sa_mask);
    if (::sigaction(SIGTERM, &ignored, nullptr) != 0)
      return 94;
    std::this_thread::sleep_for(5s);
    return 0;
  }
  if (mode == "--grandchild-check-leader" && argc == 5) {
    struct sigaction action{};
    action.sa_handler = note_termination;
    ::sigemptyset(&action.sa_mask);
    if (::sigaction(SIGTERM, &action, nullptr) != 0)
      return 94;
    std::ofstream ready(argv[2]);
    ready << "ready\n";
    ready.close();
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!termination_requested &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(1ms);
    if (!termination_requested)
      return 95;

    // The runner has observed the leader's exit before sending SIGTERM.
    // Its PID must still exist throughout the grace period, otherwise a
    // reused PID could receive the subsequent process-group SIGKILL.
    const pid_t leader = static_cast<pid_t>(std::stol(argv[4]));
    std::ofstream report(argv[3]);
    report << (::kill(leader, 0) == 0 ? "pinned\n" : "released\n");
    report.close();
    // Remain alive until SIGKILL, proving escalation still happens when the
    // direct child has already finished.
    std::this_thread::sleep_for(5s);
    return 96;
  }
  if (mode == "--child-exit-with-pinned-grandchild" && argc == 5) {
    const std::string leader = std::to_string(::getpid());
    const pid_t grandchild = ::fork();
    if (grandchild < 0)
      return 91;
    if (grandchild == 0) {
      ::execl(argv[2], argv[2], "--grandchild-check-leader", argv[3], argv[4],
              leader.c_str(), static_cast<char *>(nullptr));
      ::_exit(92);
    }
    return wait_for_file(argv[3], 1s) ? 0 : 93;
  }
#endif
  if (mode == "--child-exit" && argc == 3)
    return std::stoi(argv[2]);
  if (mode == "--child-sleep" && argc == 3) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::stoll(argv[2])));
    return 0;
  }
  if (mode == "--grandchild-write" && argc == 4) {
    std::ofstream ready(argv[2], std::ios::binary | std::ios::trunc);
    ready << "ready\n";
    ready.close();
    std::this_thread::sleep_for(900ms);
    std::ofstream marker(argv[3], std::ios::binary | std::ios::trunc);
    marker << "survived\n";
    return marker ? 0 : 1;
  }
  if ((mode == "--child-spawn-grandchild" ||
       mode == "--child-exit-with-grandchild") &&
      argc == 5) {
    const int spawn_result = spawn_grandchild(argv[2], argv[3], argv[4]);
    if (spawn_result != 0)
      return spawn_result;
    if (mode == "--child-spawn-grandchild") {
      std::this_thread::sleep_for(5s);
      return 0;
    }
    return wait_for_file(argv[3], 1s) ? 0 : 93;
  }
  return -1;
}

void test_quick_exit(const fs::path &self) {
  const auto result = ny::linker_detail::run_process(
      {self.string(), "--child-exit", "17"}, 2s);
  expect(result.launched, "quick child was not launched: " + result.error);
  expect(!result.timed_out, "quick child incorrectly timed out");
  expect(result.exit_code == 17, "quick child exit status was not preserved");
  expect(result.error.empty(), "quick child reported an unexpected error");
}

void test_timeout(const fs::path &self) {
  const auto started = std::chrono::steady_clock::now();
  const auto result = ny::linker_detail::run_process(
      {self.string(), "--child-sleep", "2000"}, 100ms);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  expect(result.launched, "sleeping child was not launched: " + result.error);
  expect(result.timed_out, "sleeping child did not report a timeout");
  expect(result.exit_code == 124, "timeout did not use exit status 124");
  expect(result.error.find("timed out") != std::string::npos,
         "timeout diagnostic is missing");
  expect(elapsed < 1500ms, "timeout did not bound the driver wait");
}

void test_process_group_cleanup(const fs::path &self) {
  const auto nonce = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path root =
      fs::temp_directory_path() / ("nillinker-process-runner-" + nonce);
  const fs::path ready = root.string() + ".ready";
  const fs::path survived = root.string() + ".survived";
  std::error_code ignored;
  fs::remove(ready, ignored);
  fs::remove(survived, ignored);

  const auto result = ny::linker_detail::run_process(
      {self.string(), "--child-spawn-grandchild", self.string(),
       ready.string(), survived.string()},
      400ms);
  expect(result.launched, "process-tree child was not launched: " + result.error);
  expect(result.timed_out, "process-tree child did not time out");
  expect(result.error.find("cleanup could not be confirmed") ==
             std::string::npos,
         "timed-out process-tree cleanup was not confirmed: " + result.error);
  expect(fs::is_regular_file(ready),
         "grandchild readiness evidence was not produced before timeout");
  std::this_thread::sleep_for(1s);
  expect(!fs::exists(survived),
         "a grandchild survived linker-driver process-group cleanup");

  fs::remove(ready, ignored);
  fs::remove(survived, ignored);
}

void test_successful_leader_with_descendant(const fs::path &self) {
  const auto nonce = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path root =
      fs::temp_directory_path() / ("nillinker-process-orphan-" + nonce);
  const fs::path ready = root.string() + ".ready";
  const fs::path survived = root.string() + ".survived";
  std::error_code ignored;
  fs::remove(ready, ignored);
  fs::remove(survived, ignored);

  const auto result = ny::linker_detail::run_process(
      {self.string(), "--child-exit-with-grandchild", self.string(),
       ready.string(), survived.string()},
      2s);
  expect(result.launched,
         "successful leader with descendant was not launched: " + result.error);
  expect(!result.timed_out,
         "successful leader with descendant incorrectly timed out");
  expect(result.exit_code != 0,
         "successful leader with descendant was accepted as success");
  expect(result.error.find("descendant") != std::string::npos,
         "residual descendant diagnostic is missing: " + result.error);
  expect(result.error.find("cleanup could not be confirmed") ==
             std::string::npos,
         "residual descendant cleanup was not confirmed: " + result.error);
  expect(fs::is_regular_file(ready),
         "grandchild readiness evidence was not produced before leader exit");
  std::this_thread::sleep_for(1s);
  expect(!fs::exists(survived),
         "a grandchild survived cleanup after its leader exited successfully");

  fs::remove(ready, ignored);
  fs::remove(survived, ignored);
}

#ifndef _WIN32
void test_term_resistant_timeout(const fs::path &self) {
  const auto started = std::chrono::steady_clock::now();
  const auto result = ny::linker_detail::run_process(
      {self.string(), "--child-ignore-term"}, 200ms);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  expect(result.launched && result.timed_out && result.exit_code == 124,
         "TERM-resistant driver did not report timeout: " + result.error);
  expect(result.error.find("cleanup could not be confirmed") == std::string::npos,
         "TERM-resistant direct child was not reaped: " + result.error);
  expect(elapsed < 1500ms, "TERM-resistant driver escaped its cleanup deadline");
}

void test_leader_identity_until_escalation(const fs::path &self) {
  const auto nonce = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path root =
      fs::temp_directory_path() / ("nillinker-process-identity-" + nonce);
  const fs::path ready = root.string() + ".ready";
  const fs::path report = root.string() + ".report";
  const auto started = std::chrono::steady_clock::now();
  const auto result = ny::linker_detail::run_process(
      {self.string(), "--child-exit-with-pinned-grandchild", self.string(),
       ready.string(), report.string()}, 2s);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  expect(result.launched && !result.timed_out && result.exit_code != 0,
         "leader with resistant descendant was not rejected: " + result.error);
  expect(result.error.find("descendant") != std::string::npos,
         "resistant descendant diagnostic is missing: " + result.error);
  expect(result.error.find("cleanup could not be confirmed") == std::string::npos,
         "SIGKILL cleanup was not confirmed: " + result.error);
  expect(elapsed < 1500ms, "resistant descendant cleanup exceeded its deadline");
  std::ifstream evidence(report);
  std::string state;
  std::getline(evidence, state);
  expect(state == "pinned", "leader identity was released before final group signal");

  // Every direct child must also have been reaped before run_process returns.
  int status = 0;
  expect(::waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD,
         "process runner leaked an unreaped direct child");
  std::error_code ignored;
  fs::remove(ready, ignored);
  fs::remove(report, ignored);
}

void test_rejects_auto_reaping(const fs::path &self) {
  struct sigaction original{};
  expect(::sigaction(SIGCHLD, nullptr, &original) == 0,
         "cannot read SIGCHLD for auto-reaping regression");
  struct sigaction ignored{};
  ignored.sa_handler = SIG_IGN;
  ::sigemptyset(&ignored.sa_mask);
  expect(::sigaction(SIGCHLD, &ignored, nullptr) == 0,
         "cannot configure ignored SIGCHLD for regression");
  const auto ignored_result = ny::linker_detail::run_process(
      {self.string(), "--child-exit", "0"}, 2s);
  expect(!ignored_result.launched && ignored_result.error.find("SIGCHLD") !=
             std::string::npos,
         "runner accepted SIGCHLD=SIG_IGN without a pinnable child identity");

  ignored.sa_handler = SIG_DFL;
  ignored.sa_flags = SA_NOCLDWAIT;
  expect(::sigaction(SIGCHLD, &ignored, nullptr) == 0,
         "cannot configure SA_NOCLDWAIT for regression");
  const auto autoreap_result = ny::linker_detail::run_process(
      {self.string(), "--child-exit", "0"}, 2s);
  expect(!autoreap_result.launched && autoreap_result.error.find("SIGCHLD") !=
             std::string::npos,
         "runner accepted SA_NOCLDWAIT without a pinnable child identity");
  expect(::sigaction(SIGCHLD, &original, nullptr) == 0,
         "cannot restore SIGCHLD after auto-reaping regression");
}
#endif

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    const int result = child_mode(argc, argv);
    if (result >= 0)
      return result;
  }

  const fs::path self = self_path(argc > 0 ? argv[0] : nullptr);
  test_quick_exit(self);
  test_timeout(self);
  test_process_group_cleanup(self);
  test_successful_leader_with_descendant(self);
#ifndef _WIN32
  test_term_resistant_timeout(self);
  test_leader_identity_until_escalation(self);
  test_rejects_auto_reaping(self);
#endif

  if (failures != 0) {
    std::cerr << failures << " process-runner regression(s) failed\n";
    return 1;
  }
  std::cout << "nillinker process-runner regressions passed\n";
  return 0;
}

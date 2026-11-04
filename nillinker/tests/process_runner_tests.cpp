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

#ifdef _WIN32
HANDLE spawn_windows_fixture(const std::string &executable,
                              const std::vector<std::string> &arguments) {
  auto quote = [](const std::wstring &argument) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t character : argument) {
      if (character == L'\\') {
        ++backslashes;
        continue;
      }
      result.append(character == L'"' ? 2 * backslashes + 1 : backslashes, L'\\');
      backslashes = 0;
      result.push_back(character);
    }
    result.append(2 * backslashes, L'\\');
    return result + L'"';
  };
  const auto application = fs::u8path(executable).wstring();
  std::wstring command = quote(application);
  for (const auto &argument : arguments)
    command += L" " + quote(fs::u8path(argument).wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &startup, &child))
    return nullptr;
  CloseHandle(child.hThread);
  return child.hProcess;
}
#endif

int spawn_grandchild(const char *executable, const char *ready,
                     const char *marker) {
#ifdef _WIN32
  const HANDLE grandchild = spawn_windows_fixture(
      executable, {"--grandchild-write", ready, marker});
  if (grandchild == nullptr)
    return 91;
  CloseHandle(grandchild);
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
#ifdef _WIN32
  if (mode == "--grandchild-ready-wait" && argc == 3) {
    std::ofstream ready(fs::u8path(argv[2]));
    ready << "ready\n";
    ready.close();
    std::this_thread::sleep_for(60s);
    return 0;
  }
  if (mode == "--child-msvc-auxiliaries" && argc == 8) {
    if (spawn_grandchild(argv[2], argv[3], argv[4]) != 0)
      return 91;
    const std::string second_ready = std::string(argv[3]) + ".second";
    const std::string second_marker = std::string(argv[4]) + ".second";
    const bool second = std::string(argv[5]) != "-";
    if (second && spawn_grandchild(argv[5], second_ready.c_str(),
                                   second_marker.c_str()) != 0)
      return 91;
    if (!wait_for_file(argv[3], 1s) || (second && !wait_for_file(second_ready, 1s)))
      return 93;
    std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[7])));
    return std::stoi(argv[6]);
  }
#endif
#ifndef _WIN32
  if (mode == "--child-ignore-term") {
    struct sigaction ignored{};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    if (::sigaction(SIGTERM, &ignored, nullptr) != 0)
      return 94;
    std::this_thread::sleep_for(5s);
    return 0;
  }
  if (mode == "--grandchild-check-leader" && argc == 5) {
    struct sigaction action{};
    action.sa_handler = note_termination;
    sigemptyset(&action.sa_mask);
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
#ifdef _WIN32
  expect(result.error.find("job members=") != std::string::npos &&
             result.error.find("pid=") != std::string::npos &&
             result.error.find("image=" + self.filename().string()) != std::string::npos,
         "residual descendant identity evidence is missing: " + result.error);
#endif
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

#ifdef _WIN32
void test_msvc_auxiliary_cleanup(const fs::path &self) {
  using ny::linker_detail::windows_auxiliary_cleanup;
  const auto nonce = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root = fs::temp_directory_path() /
                    fs::u8path("nillinker MSVC auxiliary café " + nonce);
  std::error_code error;
  if (!fs::create_directory(root, error)) {
    expect(false, "cannot create owned MSVC helper fixture: " + error.message());
    return;
  }
  const auto driver = root / "link.exe";
  const auto helper = root / "vctip.exe";
  const auto unknown = root / "unknown.exe";
  const auto other_dir = root / "other";
  fs::create_directory(other_dir);
  const auto wrong_helper = other_dir / "vctip.exe";
  for (const auto &path : {driver, helper, unknown, wrong_helper})
    fs::copy_file(self, path);

  std::vector<fs::path> markers;
  auto run = [&](const std::string &name, const fs::path &child,
                 windows_auxiliary_cleanup policy,
                 const fs::path &second = fs::path{}, int exit_code = 0,
                 int sleep_ms = 0, std::chrono::milliseconds timeout = 2s) {
    const auto ready = root / (name + ".ready");
    const auto marker = root / (name + ".survived");
    markers.push_back(marker);
    if (!second.empty())
      markers.emplace_back(marker.u8string() + ".second");
    const std::vector<std::string> arguments = {
        driver.u8string(), "--child-msvc-auxiliaries", child.u8string(),
        ready.u8string(), marker.u8string(),
        second.empty() ? "-" : second.u8string(), std::to_string(exit_code),
        std::to_string(sleep_ms)};
    const auto result = policy == windows_auxiliary_cleanup::none
                            ? ny::linker_detail::run_process(arguments, timeout)
                            : ny::linker_detail::run_process(arguments, timeout, policy);
    expect(result.launched, name + " driver did not launch: " + result.error);
    expect(fs::is_regular_file(ready), name + " auxiliary was not ready");
    if (!second.empty())
      expect(fs::is_regular_file(fs::u8path(ready.u8string() + ".second")),
             name + " second auxiliary was not ready");
    expect(result.error.find("cleanup could not be confirmed") == std::string::npos,
           name + " cleanup was not confirmed: " + result.error);
    return result;
  };

  const auto strict = run("strict", helper, windows_auxiliary_cleanup::none);
  expect(strict.exit_code != 0 && strict.cleaned_auxiliaries.empty(),
         "default-strict runner accepted an exact vctip.exe helper");

  // A process with the exact same full image outside this invocation's job
  // must not be touched. Keep its handle for both assertion and teardown.
  const auto outside_ready = root / "outside.ready";
  const HANDLE outside = spawn_windows_fixture(
      helper.u8string(), {"--grandchild-ready-wait", outside_ready.u8string()});
  expect(outside != nullptr && wait_for_file(outside_ready, 1s),
         "outside-job exact-image fixture did not start");
  const auto accepted = run("accepted", helper,
                            windows_auxiliary_cleanup::msvc_telemetry);
  expect(accepted.exit_code == 0 && !accepted.timed_out && accepted.error.empty(),
         "selected MSVC helper cleanup did not succeed: " + accepted.error);
  expect(accepted.cleaned_auxiliaries.size() == 1 &&
             accepted.cleaned_auxiliaries.front().process_id != 0 &&
             fs::equivalent(fs::u8path(accepted.cleaned_auxiliaries.front().image), helper),
         "selected MSVC helper cleanup evidence is missing");
  if (outside != nullptr) {
    expect(WaitForSingleObject(outside, 0) == WAIT_TIMEOUT,
           "cleanup affected an exact-image process outside the driver job");
    TerminateProcess(outside, 0);
    expect(WaitForSingleObject(outside, 1000) == WAIT_OBJECT_0,
           "outside-job test fixture cleanup failed");
    CloseHandle(outside);
  }

  const auto wrong = run("wrong-path", wrong_helper,
                         windows_auxiliary_cleanup::msvc_telemetry);
  expect(wrong.exit_code != 0 && wrong.cleaned_auxiliaries.empty() &&
             wrong.error.find("unexpected descendant") != std::string::npos,
         "wrong-directory helper rejection lacked the expected evidence: " + wrong.error);
  const auto mixed = run("mixed", helper,
                         windows_auxiliary_cleanup::msvc_telemetry, unknown);
  expect(mixed.exit_code != 0 && mixed.cleaned_auxiliaries.empty() &&
             mixed.error.find("unexpected descendant") != std::string::npos,
         "mixed known/unknown rejection lacked the expected evidence: " + mixed.error);
  const auto failed = run("failed-leader", helper,
                          windows_auxiliary_cleanup::msvc_telemetry, {}, 17);
  expect(failed.exit_code == 17 && failed.cleaned_auxiliaries.empty(),
         "nonzero MSVC leader was converted to success");
  const auto timeout = run("timeout", helper,
                           windows_auxiliary_cleanup::msvc_telemetry, {}, 0, 2000, 400ms);
  expect(timeout.timed_out && timeout.exit_code == 124 &&
             timeout.cleaned_auxiliaries.empty(),
         "MSVC helper policy changed timeout behavior");
  std::this_thread::sleep_for(1s);
  for (const auto &marker : markers)
    expect(!fs::exists(marker), "owned auxiliary survived cleanup: " + marker.u8string());
  fs::remove_all(root, error); // Only this test's uniquely created fixture.
  expect(!error, "cannot remove owned MSVC fixture: " + error.message());
}
#endif

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
  sigemptyset(&ignored.sa_mask);
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
#ifdef _WIN32
  test_msvc_auxiliary_cleanup(self);
#endif
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

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nilbuild {
struct process_result {
  int status = 0;
  std::string output;
  std::string error;
  bool cancelled = false;
  bool started = false;
  bool timed_out = false;
  bool output_limited = false;
  int signal = 0;
  std::int64_t duration_ms = 0;
};
struct process_options {
  bool fail_fast = true;
  bool report_limits = false;
  bool close_stdin = false;
  bool cleanup_descendants = false;
  int success_exit = 0;
  std::chrono::milliseconds time_limit{300000};
  std::size_t output_limit = 16 * 1024 * 1024;
  std::size_t total_output_limit = std::numeric_limits<std::size_t>::max();
};
struct process_command {
  std::vector<std::string> args;
  std::filesystem::path directory;
};

#ifdef _WIN32
// Windows delivers a console control event on a thread of its own rather than
// interrupting this one, so the flag has to be atomic rather than
// sig_atomic_t. The numbers are POSIX's on purpose: a cancelled result reports
// `128 + signal`, and a build report should not mean something different
// depending on which host produced it.
inline std::atomic<int> pending_signal{0};
inline unsigned cancellation_depth = 0;
inline constexpr int interrupt_signal = 2;   // SIGINT
inline constexpr int terminate_signal = 15;  // SIGTERM

inline BOOL WINAPI forward_console_event(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT &&
      event != CTRL_CLOSE_EVENT)
    return FALSE;
  int unset = 0;
  pending_signal.compare_exchange_strong(unset, interrupt_signal);
  // Claiming the event stops the default handler from tearing the process down
  // before the poll loop can stop the children and report what they did.
  return TRUE;
}

class cancellation_scope {
public:
  cancellation_scope() {
    if (cancellation_depth) { ++cancellation_depth; return; }
    pending_signal = 0;
    if (!SetConsoleCtrlHandler(forward_console_event, TRUE))
      throw std::runtime_error("cannot install interruption handler");
    ++cancellation_depth;
  }
  cancellation_scope(const cancellation_scope &) = delete;
  cancellation_scope &operator=(const cancellation_scope &) = delete;
  ~cancellation_scope() {
    if (--cancellation_depth) return;
    SetConsoleCtrlHandler(forward_console_event, FALSE);
  }
};

namespace detail {
inline std::wstring widen(const std::string &value, const char *what) {
  if (value.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.c_str(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
  if (needed <= 0)
    throw std::runtime_error(std::string("cannot encode ") + what +
                             " as UTF-16: " + value);
  std::wstring wide(static_cast<std::size_t>(needed), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                          static_cast<int>(value.size()), wide.data(),
                          needed) != needed)
    throw std::runtime_error(std::string("cannot encode ") + what +
                             " as UTF-16: " + value);
  return wide;
}

// Quote one argument the way CommandLineToArgvW parses it back, which is the
// rule CreateProcess's own child will apply. There is no argv array on
// Windows: the command line is one string and the callee splits it, so an
// argument holding a space or a quote must be escaped here or it silently
// becomes two arguments.
inline void append_quoted(std::wstring &line, const std::wstring &argument) {
  if (!argument.empty() &&
      argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    line += argument;
    return;
  }
  line += L'"';
  for (auto it = argument.begin();; ++it) {
    std::size_t backslashes = 0;
    while (it != argument.end() && *it == L'\\') { ++it; ++backslashes; }
    if (it == argument.end()) {
      line.append(backslashes * 2, L'\\');
      break;
    }
    if (*it == L'"') {
      line.append(backslashes * 2 + 1, L'\\');
    } else {
      line.append(backslashes, L'\\');
    }
    line += *it;
  }
  line += L'"';
}

inline std::wstring command_line(const std::vector<std::string> &arguments) {
  std::wstring line;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (i) line += L' ';
    append_quoted(line, widen(arguments[i], "child argument"));
  }
  line += L'\0';
  return line;
}

inline std::string last_error_message(DWORD code) {
  char *text = nullptr;
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<char *>(&text), 0, nullptr);
  std::string message = length && text ? std::string(text, length)
                                       : "error " + std::to_string(code);
  if (text) LocalFree(text);
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
    message.pop_back();
  return message;
}
}  // namespace detail
#endif

#ifndef _WIN32
inline volatile sig_atomic_t pending_signal = 0;
inline unsigned cancellation_depth = 0;
inline void forward_signal(int signal) {
  const int saved_errno = errno;
  if (!pending_signal) pending_signal = signal;
  errno = saved_errno;
}
class cancellation_scope {
  struct sigaction old_int{}, old_term{};
public:
  cancellation_scope() {
    if (cancellation_depth) { ++cancellation_depth; return; }
    pending_signal = 0;
    struct sigaction action{};
    action.sa_handler = forward_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &old_int) != 0)
      throw std::runtime_error("cannot install interruption handler");
    if (sigaction(SIGTERM, &action, &old_term) != 0) {
      sigaction(SIGINT, &old_int, nullptr);
      throw std::runtime_error("cannot install termination handler");
    }
    ++cancellation_depth;
  }
  cancellation_scope(const cancellation_scope &) = delete;
  cancellation_scope &operator=(const cancellation_scope &) = delete;
  ~cancellation_scope() {
    if (--cancellation_depth) return;
    sigaction(SIGINT, &old_int, nullptr);
    sigaction(SIGTERM, &old_term, nullptr);
  }
};

namespace detail {
// Only fixed stack storage, integer operations and async-signal-safe syscalls
// are allowed here: this runs after fork and before exec. Use the original
// capture pipe because stderr redirection itself may be the failing step.
template<std::size_t N>
[[noreturn]] inline void child_setup_failed(int error_pipe,
                                          const char (&stage)[N],
                                          int saved_errno) noexcept {
  static_assert(N <= 32, "child setup stage must remain bounded");
  char message[128];
  std::size_t length = 0;
  constexpr char prefix[] = "nilbuild: child setup failed at ";
  constexpr char middle[] = " (errno=";
  for (std::size_t i = 0; i + 1 < sizeof(prefix); ++i) message[length++] = prefix[i];
  for (std::size_t i = 0; i + 1 < N; ++i) message[length++] = stage[i];
  for (std::size_t i = 0; i + 1 < sizeof(middle); ++i) message[length++] = middle[i];
  char digits[sizeof(unsigned) * 3];
  std::size_t count = 0;
  auto number = static_cast<unsigned>(saved_errno);
  do { digits[count++] = static_cast<char>('0' + number % 10); number /= 10; } while (number);
  while (count) message[length++] = digits[--count];
  message[length++] = ')';
  message[length++] = '\n';
  std::size_t written = 0;
  for (unsigned attempt = 0; attempt != 4 && written < length; ++attempt) {
    const auto result = write(error_pipe, message + written, length - written);
    if (result > 0) written += static_cast<std::size_t>(result);
    else if (result != -1 || errno != EINTR) break;
  }
  _exit(126);
}
}  // namespace detail
#endif

// The driver never invokes a shell. Compiler processes have bounded capture
// and execution time, with cancellation forwarded to the child process group.
// User programs replace the driver with execv after the build completes.
// Results retain command order. Default fail-fast builds terminate active
// siblings after a failure; test runners can request independent case results.
// Commands not yet launched have started == false.
// One poll loop owns every child and signal handler, so parallel execution
// does not race process-global signal state or write interleaved build logs.
inline std::vector<process_result> execute_many(const std::vector<process_command> &commands,
                                               std::size_t jobs,
                                               const process_options &options = {}) {
#ifdef _WIN32
  const auto output_limit = options.output_limit;
  const auto time_limit = options.time_limit;
  if (!output_limit || time_limit.count() <= 0)
    throw std::runtime_error("child execution limits must be positive");
  using clock = std::chrono::steady_clock;
  if (!jobs) throw std::runtime_error("parallel process count must be greater than zero");
  for (const auto &command : commands) {
    if (command.args.empty() || command.args.front().empty())
      throw std::runtime_error("child command must specify an executable");
    for (const auto &argument : command.args)
      if (argument.find('\0') != std::string::npos)
        throw std::runtime_error("child argument contains a null character");
  }
  std::vector<process_result> results(commands.size());
  if (commands.empty()) return results;
  cancellation_scope cancellation;
  struct child_state {
    HANDLE process = nullptr;
    // One job per child is the process-group equivalent: it is what makes a
    // tool's own grandchildren killable, and KILL_ON_JOB_CLOSE means a crash
    // in this driver cannot leave a compiler running.
    HANDLE job = nullptr;
    DWORD id = 0;
    HANDLE streams[2] = {nullptr, nullptr};
    bool reaped = false;
    bool complete = false;
    std::size_t received = 0;
    clock::time_point deadline, start;
  };
  std::vector<child_state> children(commands.size());
  std::size_t next = 0, active = 0, total_received = 0;
  bool stopping = false;
  int stop_signal = 0, external_signal = 0;
  auto cancellation_deadline = clock::time_point::max();
  auto close_streams = [](child_state &child) {
    for (HANDLE &stream : child.streams)
      if (stream) { CloseHandle(stream); stream = nullptr; }
  };
  auto signal_groups = [&](int signal) {
    for (auto &child : children) if (child.process && !child.complete) {
      if (signal == terminate_signal) {
        // The closest thing to SIGTERM. It needs a console the child shares,
        // so it can legitimately fail; the forced step below then runs when
        // the grace period expires. Never a reason to abandon the child.
        if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, child.id))
          continue;
      }
      TerminateJobObject(child.job, static_cast<UINT>(128 + signal));
    }
  };
  auto cleanup = [&] {
    for (auto &child : children) if (child.process) {
      TerminateJobObject(child.job, static_cast<UINT>(128 + interrupt_signal));
      WaitForSingleObject(child.process, 2000);
      CloseHandle(child.process); child.process = nullptr;
      CloseHandle(child.job); child.job = nullptr;
      close_streams(child);
    }
  };
  auto stop = [&](int signal, std::size_t failed) {
    if (!stopping) {
      stopping = true;
      stop_signal = signal;
      cancellation_deadline = clock::now() + std::chrono::seconds(2);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (i != failed && children[i].process && !children[i].complete) {
          results[i].cancelled = true;
          if (children[i].reaped && !results[i].status) results[i].status = 128 + signal;
        }
      }
    }
    signal_groups(signal);
  };
  auto terminate_child = [&](std::size_t index) {
    auto &child = children[index];
    TerminateJobObject(child.job, static_cast<UINT>(128 + interrupt_signal));
    child.deadline = clock::time_point::max();
    // Descendants inherited the capture pipes. A bounded result must not wait
    // for them after this case has already exceeded its limits.
    close_streams(child);
  };
  auto launch = [&](std::size_t index) {
    const auto &command = commands[index];
    auto &child = children[index];
    const auto application = detail::widen(command.args.front(), "child executable");
    auto line = detail::command_line(command.args);
    const auto directory = detail::widen(command.directory.string(), "child directory");
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    HANDLE pipes[4] = {nullptr, nullptr, nullptr, nullptr};  // out r/w, err r/w
    HANDLE input = nullptr;
    const auto fail = [&](const char *what) {
      const DWORD code = GetLastError();
      for (HANDLE handle : pipes) if (handle) CloseHandle(handle);
      if (input) CloseHandle(input);
      throw std::runtime_error(std::string(what) + ": " +
                               detail::last_error_message(code));
    };
    if (!CreatePipe(&pipes[0], &pipes[1], &inheritable, 0) ||
        !CreatePipe(&pipes[2], &pipes[3], &inheritable, 0))
      fail("cannot create child output pipe");
    // Only the write ends cross into the child. A read end left inheritable
    // would keep the pipe open in the child and the parent would never see EOF.
    if (!SetHandleInformation(pipes[0], HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(pipes[2], HANDLE_FLAG_INHERIT, 0))
      fail("cannot configure child output pipe");
    // STARTF_USESTDHANDLES means all three handles are taken literally, so an
    // absent stdin (a detached parent, where GetStdHandle gives NULL) would
    // hand the child an invalid one rather than an empty one. NUL is the
    // Windows /dev/null, and it is also what close_stdin asks for outright.
    HANDLE inherited_input = GetStdHandle(STD_INPUT_HANDLE);
    if (options.close_stdin || !inherited_input ||
        inherited_input == INVALID_HANDLE_VALUE) {
      input = CreateFileW(L"NUL", GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                          OPEN_EXISTING, 0, nullptr);
      if (input == INVALID_HANDLE_VALUE) { input = nullptr; fail("cannot detach child input"); }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input ? input : inherited_input;
    startup.hStdOutput = pipes[1];
    startup.hStdError = pipes[3];
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) fail("cannot create child job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
      CloseHandle(job);
      fail("cannot configure child job");
    }
    PROCESS_INFORMATION information{};
    // Suspended, so the process is inside the job before it can run and spawn
    // anything that would escape it.
    if (!CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                            CREATE_NEW_PROCESS_GROUP,
                        nullptr, directory.empty() ? nullptr : directory.c_str(),
                        &startup, &information)) {
      CloseHandle(job);
      fail("cannot start child process");
    }
    if (!AssignProcessToJobObject(job, information.hProcess) ||
        ResumeThread(information.hThread) == static_cast<DWORD>(-1)) {
      const DWORD code = GetLastError();
      TerminateProcess(information.hProcess, 126);
      CloseHandle(information.hThread);
      CloseHandle(information.hProcess);
      CloseHandle(job);
      for (HANDLE handle : pipes) if (handle) CloseHandle(handle);
      if (input) CloseHandle(input);
      throw std::runtime_error("cannot contain child process: " +
                               detail::last_error_message(code));
    }
    CloseHandle(information.hThread);
    if (input) CloseHandle(input);
    // The parent's copies of the write ends must go, or the reads never end.
    CloseHandle(pipes[1]); CloseHandle(pipes[3]);
    child.process = information.hProcess;
    child.job = job;
    child.id = information.dwProcessId;
    child.streams[0] = pipes[0];
    child.streams[1] = pipes[2];
    child.start = clock::now();
    child.deadline = child.start + time_limit;
    results[index].started = true;
    ++active;
  };
  try {
    while (active || (!stopping && next < commands.size())) {
      if (pending_signal && !external_signal) {
        external_signal = pending_signal;
        stop(external_signal, commands.size());
      }
      while (!stopping && !pending_signal && next < commands.size() && active < jobs)
        launch(next++);
      if (!active) break;
      const auto now = clock::now();
      if (now >= cancellation_deadline) {
        signal_groups(interrupt_signal);
        if (options.report_limits) for (std::size_t i = 0; i < next; ++i)
          if (!children[i].complete) terminate_child(i);
      }
      bool progressed = false;
      for (std::size_t i = 0; i < next; ++i) {
        auto &child = children[i];
        auto &result = results[i];
        if (child.complete) continue;
        if (now >= child.deadline) {
          if (!options.report_limits) throw std::runtime_error("child execution exceeded 300 seconds");
          result.timed_out = true;
          terminate_child(i);
        }
        for (unsigned channel = 0; channel < 2; ++channel) {
          HANDLE &stream = child.streams[channel];
          if (!stream) continue;
          // PeekNamedPipe is what makes this non-blocking: ReadFile on an
          // anonymous pipe would block until data arrived, and one stalled
          // child would stop every sibling's capture and the deadline checks.
          DWORD available = 0;
          if (!PeekNamedPipe(stream, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD code = GetLastError();
            if (code == ERROR_BROKEN_PIPE) { CloseHandle(stream); stream = nullptr; continue; }
            throw std::runtime_error("cannot read child output: " +
                                     detail::last_error_message(code));
          }
          if (!available) continue;
          char buffer[8192];
          DWORD request = available < sizeof(buffer) ? available
                                                     : static_cast<DWORD>(sizeof(buffer));
          DWORD count = 0;
          if (!ReadFile(stream, buffer, request, &count, nullptr)) {
            const DWORD code = GetLastError();
            if (code == ERROR_BROKEN_PIPE) { CloseHandle(stream); stream = nullptr; continue; }
            throw std::runtime_error("cannot read child output: " +
                                     detail::last_error_message(code));
          }
          if (!count) { CloseHandle(stream); stream = nullptr; continue; }
          progressed = true;
          const auto retained = std::min({static_cast<std::size_t>(count),
                                          output_limit - child.received,
                                          options.total_output_limit - total_received});
          child.received += retained; total_received += retained;
          (channel == 0 ? result.output : result.error).append(buffer, retained);
          if (retained != static_cast<std::size_t>(count)) {
            if (!options.report_limits) throw std::runtime_error("child output exceeds 16 MiB");
            result.output_limited = true;
            terminate_child(i);
            break;
          }
        }
        if (!child.reaped && child.process &&
            WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0) {
          DWORD code = 1;
          if (!GetExitCodeProcess(child.process, &code))
            throw std::runtime_error("cannot collect child exit status: " +
                                     detail::last_error_message(GetLastError()));
          child.reaped = true;
          progressed = true;
          // Windows has no wait status, so there is nothing to decode and
          // result.signal stays 0. A job terminated above already carries
          // 128 + signal as its exit code, which keeps the two hosts' reports
          // reading the same way.
          result.status = static_cast<int>(code);
          if (result.cancelled && !result.status) result.status = 128 + stop_signal;
          if (result.status != options.success_exit && !stopping && options.fail_fast)
            stop(terminate_signal, i);
        }
        if (child.reaped && !child.streams[0] && !child.streams[1]) {
          if (stopping || options.cleanup_descendants)
            TerminateJobObject(child.job, static_cast<UINT>(128 + interrupt_signal));
          // Note a deliberate difference from the POSIX path: closing the job
          // handle below kills anything still inside it, so a descendant that
          // outlives its parent does not survive here even when
          // cleanup_descendants is false. Keeping it alive would mean leaking
          // the job handle for the life of the driver, and a build step that
          // forks a daemon is not something this driver supports on either
          // host.
          child.complete = true;
          result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              clock::now() - child.start).count();
          CloseHandle(child.process); child.process = nullptr;
          CloseHandle(child.job); child.job = nullptr;
          --active;
        }
      }
      // Matches the POSIX poll timeout: idle without burning a core.
      if (!progressed) Sleep(10);
    }
    if (pending_signal) {
      signal_groups(interrupt_signal);
      for (std::size_t i = 0; i < results.size(); ++i) {
        if (!options.fail_fast && children[i].complete && !results[i].cancelled) continue;
        results[i].cancelled = true;
        results[i].status = 128 + pending_signal;
      }
    } else if (stopping) {
      signal_groups(interrupt_signal);
    }
    cleanup();
    return results;
  } catch (...) { cleanup(); throw; }
#else
  const auto output_limit = options.output_limit;
  const auto time_limit = options.time_limit;
  if (!output_limit || time_limit.count() <= 0)
    throw std::runtime_error("child execution limits must be positive");
  using clock = std::chrono::steady_clock;
  if (!jobs) throw std::runtime_error("parallel process count must be greater than zero");
  for (const auto &command : commands) {
    if (command.args.empty() || command.args.front().empty())
      throw std::runtime_error("child command must specify an executable");
    for (const auto &argument : command.args)
      if (argument.find('\0') != std::string::npos)
        throw std::runtime_error("child argument contains a null character");
  }
  std::vector<process_result> results(commands.size());
  if (commands.empty()) return results;
  cancellation_scope cancellation;
  struct child_state {
    pid_t pid = -1;
    std::array<int, 2> streams{{-1, -1}};
    bool reaped = false;
    bool complete = false;
    std::size_t received = 0;
    clock::time_point deadline, start;
  };
  std::vector<child_state> children(commands.size());
  std::size_t next = 0, active = 0, total_received = 0;
  bool stopping = false;
  int stop_signal = 0, external_signal = 0;
  auto cancellation_deadline = clock::time_point::max();
  auto signal_groups = [&](int signal) {
    for (auto &child : children) if (child.pid > 0 && !child.complete) {
      // A reaped tool can still have descendants holding its capture pipes.
      kill(-child.pid, signal);
      if (signal == SIGKILL && !child.reaped) kill(child.pid, signal);
    }
  };
  auto cleanup = [&] {
    signal_groups(SIGKILL);
    for (auto &child : children) if (child.pid > 0) {
      if (!child.reaped) {
        int status;
        while (waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {}
      }
      for (int &fd : child.streams) if (fd >= 0) { close(fd); fd = -1; }
    }
  };
  auto stop = [&](int signal, std::size_t failed) {
    if (!stopping) {
      stopping = true;
      stop_signal = signal;
      cancellation_deadline = clock::now() + std::chrono::seconds(2);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (i != failed && children[i].pid > 0 && !children[i].complete) {
          results[i].cancelled = true;
          if (children[i].reaped && !results[i].status) results[i].status = 128 + signal;
        }
      }
    }
    signal_groups(signal);
  };
  auto terminate_child = [&](std::size_t index) {
    auto &child = children[index];
    kill(-child.pid, SIGKILL);
    if (!child.reaped) kill(child.pid, SIGKILL);
    child.deadline = clock::time_point::max();
    // Descendants may have inherited pipes. A bounded result must not wait
    // forever for them after this case has already exceeded its limits.
    for (int &fd : child.streams) if (fd >= 0) { close(fd); fd = -1; }
  };
  auto launch = [&](std::size_t index) {
    const auto &command = commands[index];
    auto &child = children[index];
    std::vector<char *> argv;
    for (const auto &argument : command.args) argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    int pipes[4] = {-1, -1, -1, -1};
    try {
      if (pipe(pipes) != 0 || pipe(pipes + 2) != 0)
        throw std::runtime_error("cannot create child output pipe");
      for (int &fd : pipes) {
        // Preserve redirection even when the caller closed a standard stream:
        // no pipe endpoint may later close a freshly duplicated stdout/stderr.
        if (fd <= STDERR_FILENO) {
          const int replacement = fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
          if (replacement == -1) throw std::runtime_error("cannot configure child output pipe");
          close(fd);
          fd = replacement;
        }
        const int flags = fcntl(fd, F_GETFD);
        if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
          throw std::runtime_error("cannot configure child output pipe");
      }
      for (int fd : {pipes[0], pipes[2]}) {
        const int flags = fcntl(fd, F_GETFL);
        if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
          throw std::runtime_error("cannot configure child output pipe");
      }
      child.pid = fork();
      if (child.pid == -1) throw std::runtime_error("cannot start child process");
      if (child.pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        if (options.close_stdin) {
          const int input = open("/dev/null", O_RDONLY);
          if (input < 0) detail::child_setup_failed(pipes[3], "open(stdin)", errno);
          if (dup2(input, STDIN_FILENO) < 0) detail::child_setup_failed(pipes[3], "dup2(stdin)", errno);
          if (input != STDIN_FILENO) close(input);
        }
        if (setpgid(0, 0) != 0) detail::child_setup_failed(pipes[3], "setpgid", errno);
        if (dup2(pipes[1], STDOUT_FILENO) == -1) detail::child_setup_failed(pipes[3], "dup2(stdout)", errno);
        if (dup2(pipes[3], STDERR_FILENO) == -1) detail::child_setup_failed(pipes[3], "dup2(stderr)", errno);
        if (chdir(command.directory.c_str()) != 0) detail::child_setup_failed(pipes[3], "chdir", errno);
        for (int fd : pipes) close(fd);
        execv(argv[0], argv.data());
        const char message[] = "nilbuild: cannot execute child tool\n";
        (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(127);
      }
      // Either side may win this race; both request the same process group.
      setpgid(child.pid, child.pid);
      close(pipes[1]); close(pipes[3]);
      child.streams = {{pipes[0], pipes[2]}};
      child.start = clock::now();
      child.deadline = child.start + time_limit;
      results[index].started = true;
      ++active;
    } catch (...) {
      for (int fd : pipes) if (fd >= 0) close(fd);
      throw;
    }
  };
  try {
    while (active || (!stopping && next < commands.size())) {
      if (pending_signal && !external_signal) {
        external_signal = pending_signal;
        stop(external_signal, commands.size());
      }
      while (!stopping && !pending_signal && next < commands.size() && active < jobs)
        launch(next++);
      if (!active) break;
      const auto now = clock::now();
      if (now >= cancellation_deadline) {
        signal_groups(SIGKILL);
        if (options.report_limits) for (std::size_t i = 0; i < next; ++i)
          if (!children[i].complete) terminate_child(i);
      }
      std::vector<pollfd> descriptors;
      std::vector<std::pair<std::size_t, unsigned>> owners;
      for (std::size_t i = 0; i < next; ++i) {
        auto &child = children[i];
        if (child.complete) continue;
        if (now >= child.deadline) {
          if (!options.report_limits) throw std::runtime_error("child execution exceeded 300 seconds");
          results[i].timed_out = true;
          terminate_child(i);
        }
        for (unsigned channel = 0; channel < 2; ++channel) if (child.streams[channel] >= 0) {
          descriptors.push_back({child.streams[channel], POLLIN, 0});
          owners.push_back({i, channel});
        }
      }
      const int ready = poll(descriptors.data(), descriptors.size(), 50);
      if (ready == -1 && errno != EINTR) throw std::runtime_error("cannot read child output");
      for (std::size_t stream = 0; stream < descriptors.size(); ++stream) {
        if (!descriptors[stream].revents) continue;
        const auto index = owners[stream].first;
        const auto channel = owners[stream].second;
        auto &child = children[index];
        auto &result = results[index];
        auto &fd = child.streams[channel];
        if (fd < 0) continue;
        char buffer[8192];
        for (;;) {
          const auto count = read(fd, buffer, sizeof(buffer));
          if (count > 0) {
            const auto retained = std::min({static_cast<std::size_t>(count), output_limit - child.received,
                options.total_output_limit - total_received});
            child.received += retained; total_received += retained;
            (channel == 0 ? result.output : result.error).append(buffer, retained);
            if (retained != static_cast<std::size_t>(count)) {
              if (!options.report_limits) throw std::runtime_error("child output exceeds 16 MiB");
              result.output_limited = true;
              terminate_child(index);
            }
            // Bound each drain to keep stderr and cancellation responsive
            // even when a child continuously fills its stdout pipe.
            break;
          } else if (count == 0) { close(fd); fd = -1; break; }
          else if (errno == EINTR) continue;
          else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
          else throw std::runtime_error("cannot read child output");
        }
      }
      for (std::size_t i = 0; i < next; ++i) {
        auto &child = children[i];
        auto &result = results[i];
        if (child.complete) continue;
        if (!child.reaped) {
          int wait_status = 0;
          const auto waited = waitpid(child.pid, &wait_status, WNOHANG);
          if (waited == child.pid) {
            child.reaped = true;
            result.signal = WIFSIGNALED(wait_status) ? WTERMSIG(wait_status) : 0;
            result.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) :
                            WIFSIGNALED(wait_status) ? 128 + WTERMSIG(wait_status) : 1;
            if (result.cancelled && !result.status) result.status = 128 + stop_signal;
            if (result.status != options.success_exit && !stopping && options.fail_fast) stop(SIGTERM, i);
          } else if (waited == -1 && errno != EINTR) {
            throw std::runtime_error("cannot collect child exit status");
          }
        }
        if (child.reaped && child.streams[0] < 0 && child.streams[1] < 0) {
          if (stopping || options.cleanup_descendants) kill(-child.pid, SIGKILL);
          child.complete = true;
          result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - child.start).count();
          --active;
        }
      }
    }
    // A signal can arrive during the final drain/reap or before the first fork.
    if (pending_signal) {
      signal_groups(SIGKILL);
      for (std::size_t i = 0; i < results.size(); ++i) {
        // A test that completed before interruption retains its result.
        if (!options.fail_fast && children[i].complete && !results[i].cancelled) continue;
        results[i].cancelled = true;
        results[i].status = 128 + pending_signal;
      }
    } else if (stopping) {
      signal_groups(SIGKILL);
    }
    return results;
  } catch (...) { cleanup(); throw; }
#endif
}
inline process_result execute(const std::vector<std::string> &arguments,
                              const std::filesystem::path &directory) {
  return execute_many({{arguments, directory}}, 1).front();
}
// Replace nilbuild with the requested program after compilation. This keeps
// terminal input, job control, cancellation and exit status native to the OS,
// without placing an artificial timeout or output limit on user applications.
inline int run_program(const std::vector<std::string> &arguments,
                       const std::filesystem::path &directory) {
#ifdef _WIN32
  // Windows has no execv: a process cannot replace its own image. The closest
  // equivalent is to run the program as a child and hand back exactly its exit
  // status, which keeps the user's terminal, stdin and Ctrl+C wired straight
  // through -- the driver stays resident as a parent and does nothing else.
  // Deliberately no job object and no new process group here: this is the
  // user's program, and it must receive Ctrl+C itself rather than have the
  // driver intercept it.
  const auto application = detail::widen(arguments.front(), "program");
  auto line = detail::command_line(arguments);
  const auto working = detail::widen(directory.string(), "project directory");
  std::cout.flush(); std::cerr.flush();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION information{};
  if (!CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE,
                      CREATE_UNICODE_ENVIRONMENT, nullptr,
                      working.empty() ? nullptr : working.c_str(), &startup,
                      &information))
    throw std::runtime_error("cannot execute program: " + arguments.front() +
                             ": " + detail::last_error_message(GetLastError()));
  CloseHandle(information.hThread);
  if (WaitForSingleObject(information.hProcess, INFINITE) != WAIT_OBJECT_0) {
    const DWORD code = GetLastError();
    CloseHandle(information.hProcess);
    throw std::runtime_error("cannot wait for program: " + arguments.front() +
                             ": " + detail::last_error_message(code));
  }
  DWORD status = 1;
  if (!GetExitCodeProcess(information.hProcess, &status)) {
    const DWORD code = GetLastError();
    CloseHandle(information.hProcess);
    throw std::runtime_error("cannot read program exit status: " +
                             detail::last_error_message(code));
  }
  CloseHandle(information.hProcess);
  return static_cast<int>(status);
#else
  std::vector<char *> argv;
  for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);
  if (chdir(directory.c_str()) != 0) throw std::runtime_error("cannot enter project directory");
  std::cout.flush(); std::cerr.flush();
  execv(argv[0], argv.data());
  throw std::runtime_error("cannot execute program: " + arguments.front() + ": " + std::strerror(errno));
#endif
}
} // namespace nilbuild

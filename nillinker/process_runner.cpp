#include "process_runner.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace ny::linker_detail {

namespace {

using steady_clock = std::chrono::steady_clock;

constexpr int timeout_exit_code = 124;
constexpr auto termination_grace = std::chrono::milliseconds(100);
constexpr auto termination_confirmation = std::chrono::milliseconds(500);
constexpr auto wait_poll_interval = std::chrono::milliseconds(10);

steady_clock::time_point deadline_after(std::chrono::milliseconds timeout) {
  const auto now = steady_clock::now();
  const auto available = steady_clock::time_point::max() - now;
  if (timeout >=
      std::chrono::duration_cast<std::chrono::milliseconds>(available))
    return steady_clock::time_point::max();
  return now + std::chrono::duration_cast<steady_clock::duration>(timeout);
}

#ifdef _WIN32

class unique_handle {
public:
  explicit unique_handle(HANDLE handle = nullptr) : handle_(handle) {}
  ~unique_handle() {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
  }

  unique_handle(const unique_handle &) = delete;
  unique_handle &operator=(const unique_handle &) = delete;

  unique_handle(unique_handle &&other) noexcept : handle_(other.release()) {}
  unique_handle &operator=(unique_handle &&other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  HANDLE get() const { return handle_; }

  HANDLE release() {
    const HANDLE handle = handle_;
    handle_ = nullptr;
    return handle;
  }

  void reset(HANDLE handle = nullptr) {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
    handle_ = handle;
  }

private:
  HANDLE handle_;
};

std::string windows_error_message(DWORD error) {
  return std::error_code(static_cast<int>(error), std::system_category())
      .message();
}

bool utf8_to_wide(const std::string &input, std::wstring &output) {
  output.clear();
  if (input.empty())
    return true;
  if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return false;

  const int count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
      nullptr, 0);
  if (count <= 0)
    return false;
  output.resize(static_cast<std::size_t>(count));
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                             static_cast<int>(input.size()), output.data(),
                             count) == count;
}

std::wstring quote_windows_argument(const std::wstring &argument) {
  std::wstring quoted;
  quoted.reserve(argument.size() + 2U);
  quoted.push_back(L'"');

  std::size_t backslashes = 0;
  for (const wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      quoted.append(backslashes * 2U + 1U, L'\\');
      quoted.push_back(L'"');
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(character);
  }

  quoted.append(backslashes * 2U, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

bool build_windows_command_line(const std::vector<std::string> &arguments,
                                std::wstring &application,
                                std::vector<wchar_t> &command_line,
                                std::string &error) {
  std::vector<std::wstring> wide_arguments;
  wide_arguments.reserve(arguments.size());
  for (const auto &argument : arguments) {
    std::wstring converted;
    if (!utf8_to_wide(argument, converted)) {
      error = "linker-driver argument is not valid UTF-8";
      return false;
    }
    wide_arguments.push_back(std::move(converted));
  }
  application = wide_arguments.front();
  if (application.empty()) {
    error = "linker-driver executable is empty";
    return false;
  }

  std::wstring joined;
  for (std::size_t index = 0; index < wide_arguments.size(); ++index) {
    if (index != 0)
      joined.push_back(L' ');
    joined += quote_windows_argument(wide_arguments[index]);
  }
  command_line.assign(joined.begin(), joined.end());
  command_line.push_back(L'\0');
  return true;
}

DWORD bounded_windows_wait(HANDLE process, steady_clock::time_point deadline) {
  const auto now = steady_clock::now();
  if (now >= deadline)
    return WAIT_TIMEOUT;

  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
  const auto count = std::max<std::int64_t>(remaining.count(), 1);
  const auto maximum_wait =
      static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)() - 1U);
  return WaitForSingleObject(
      process, static_cast<DWORD>(std::min(count, maximum_wait)));
}

bool windows_job_is_empty(HANDLE job, bool &empty, std::string &error) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
  if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                 &accounting, sizeof(accounting), nullptr)) {
    error = "cannot inspect linker-driver job: " +
            windows_error_message(GetLastError());
    return false;
  }
  empty = accounting.ActiveProcesses == 0;
  return true;
}

bool wait_for_windows_process(HANDLE process,
                              steady_clock::time_point deadline,
                              std::string &error) {
  const DWORD wait_result = bounded_windows_wait(process, deadline);
  if (wait_result == WAIT_OBJECT_0)
    return true;
  if (wait_result == WAIT_FAILED) {
    error = "cannot wait for linker driver during cleanup: " +
            windows_error_message(GetLastError());
  } else {
    error = "linker-driver process remained active after forced termination";
  }
  return false;
}

bool wait_for_windows_job_cleanup(HANDLE job, HANDLE process,
                                  steady_clock::time_point deadline,
                                  std::string &error) {
  bool process_finished = false;
  while (steady_clock::now() < deadline) {
    const DWORD wait_result = WaitForSingleObject(process, 0);
    if (wait_result == WAIT_OBJECT_0) {
      process_finished = true;
    } else if (wait_result == WAIT_FAILED) {
      error = "cannot wait for linker driver during cleanup: " +
              windows_error_message(GetLastError());
      return false;
    }

    bool job_empty = false;
    if (!windows_job_is_empty(job, job_empty, error))
      return false;
    if (process_finished && job_empty)
      return true;
    std::this_thread::sleep_for(wait_poll_interval);
  }

  const DWORD wait_result = WaitForSingleObject(process, 0);
  if (wait_result == WAIT_FAILED) {
    error = "cannot wait for linker driver during cleanup: " +
            windows_error_message(GetLastError());
    return false;
  }
  bool job_empty = false;
  if (!windows_job_is_empty(job, job_empty, error))
    return false;
  if (wait_result == WAIT_OBJECT_0 && job_empty)
    return true;

  error = !job_empty
              ? "linker-driver job remained active after forced termination"
              : "linker-driver process remained active after forced termination";
  return false;
}

bool terminate_windows_process(HANDLE process, std::string &error) {
  const BOOL terminated =
      TerminateProcess(process, static_cast<UINT>(timeout_exit_code));
  const DWORD termination_error = terminated ? ERROR_SUCCESS : GetLastError();
  std::string wait_error;
  if (wait_for_windows_process(
          process, deadline_after(termination_confirmation), wait_error))
    return true;
  error = terminated
              ? std::move(wait_error)
              : "cannot terminate linker-driver process: " +
                    windows_error_message(termination_error) + "; " + wait_error;
  return false;
}

bool terminate_windows_job(HANDLE job, HANDLE process, std::string &error) {
  const BOOL terminated =
      TerminateJobObject(job, static_cast<UINT>(timeout_exit_code));
  const DWORD termination_error = terminated ? ERROR_SUCCESS : GetLastError();
  if (!terminated)
    TerminateProcess(process, static_cast<UINT>(timeout_exit_code));

  std::string cleanup_error;
  if (wait_for_windows_job_cleanup(
          job, process, deadline_after(termination_confirmation), cleanup_error))
    return true;
  error = terminated
              ? std::move(cleanup_error)
              : "cannot terminate linker-driver job: " +
                    windows_error_message(termination_error) + "; " +
                    cleanup_error;
  return false;
}

#else

class spawn_attributes {
public:
  spawn_attributes() : error_(posix_spawnattr_init(&attributes_)) {}
  ~spawn_attributes() {
    if (error_ == 0)
      posix_spawnattr_destroy(&attributes_);
  }

  spawn_attributes(const spawn_attributes &) = delete;
  spawn_attributes &operator=(const spawn_attributes &) = delete;

  int error() const { return error_; }
  posix_spawnattr_t *get() { return &attributes_; }

private:
  posix_spawnattr_t attributes_{};
  int error_ = 0;
};

class spawn_file_actions {
public:
  spawn_file_actions() : error_(posix_spawn_file_actions_init(&actions_)) {}
  ~spawn_file_actions() {
    if (error_ == 0)
      posix_spawn_file_actions_destroy(&actions_);
  }

  spawn_file_actions(const spawn_file_actions &) = delete;
  spawn_file_actions &operator=(const spawn_file_actions &) = delete;

  int error() const { return error_; }
  posix_spawn_file_actions_t *get() { return &actions_; }

private:
  posix_spawn_file_actions_t actions_{};
  int error_ = 0;
};

class unique_file_descriptor {
public:
  explicit unique_file_descriptor(int descriptor = -1)
      : descriptor_(descriptor) {}
  ~unique_file_descriptor() {
    if (descriptor_ >= 0)
      ::close(descriptor_);
  }

  unique_file_descriptor(const unique_file_descriptor &) = delete;
  unique_file_descriptor &operator=(const unique_file_descriptor &) = delete;

  unique_file_descriptor(unique_file_descriptor &&other) noexcept
      : descriptor_(other.release()) {}
  unique_file_descriptor &operator=(unique_file_descriptor &&other) noexcept {
    if (this != &other)
      reset(other.release());
    return *this;
  }

  int get() const { return descriptor_; }

  int release() {
    const int descriptor = descriptor_;
    descriptor_ = -1;
    return descriptor;
  }

  void reset(int descriptor = -1) {
    if (descriptor_ >= 0)
      ::close(descriptor_);
    descriptor_ = descriptor;
  }

private:
  int descriptor_;
};

bool set_close_on_exec(int descriptor, std::string &error) {
  const int flags = ::fcntl(descriptor, F_GETFD);
  if (flags != -1 && ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) != -1)
    return true;
  error = "cannot secure linker-driver sentinel descriptor: " +
          std::string(std::strerror(errno));
  return false;
}

bool set_nonblocking(int descriptor, std::string &error) {
  const int flags = ::fcntl(descriptor, F_GETFL);
  if (flags != -1 && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != -1)
    return true;
  error = "cannot configure linker-driver sentinel descriptor: " +
          std::string(std::strerror(errno));
  return false;
}

bool move_descriptor_above_standard_streams(unique_file_descriptor &descriptor,
                                            std::string &error) {
  if (descriptor.get() >= 3)
    return true;
#ifdef F_DUPFD_CLOEXEC
  const int duplicate = ::fcntl(descriptor.get(), F_DUPFD_CLOEXEC, 3);
#else
  const int duplicate = ::fcntl(descriptor.get(), F_DUPFD, 3);
#endif
  if (duplicate == -1) {
    error = "cannot relocate linker-driver sentinel descriptor: " +
            std::string(std::strerror(errno));
    return false;
  }
  unique_file_descriptor relocated(duplicate);
#ifndef F_DUPFD_CLOEXEC
  if (!set_close_on_exec(relocated.get(), error))
    return false;
#endif
  descriptor = std::move(relocated);
  return true;
}

bool create_inheritance_sentinel(unique_file_descriptor &reader,
                                 unique_file_descriptor &writer,
                                 std::string &error) {
  int descriptors[2] = {-1, -1};
  if (::pipe(descriptors) != 0) {
    error = "cannot create linker-driver inheritance sentinel: " +
            std::string(std::strerror(errno));
    return false;
  }
  reader.reset(descriptors[0]);
  writer.reset(descriptors[1]);
  if (!set_close_on_exec(reader.get(), error) ||
      !set_close_on_exec(writer.get(), error) ||
      !move_descriptor_above_standard_streams(reader, error) ||
      !move_descriptor_above_standard_streams(writer, error) ||
      !set_nonblocking(reader.get(), error)) {
    return false;
  }
  return true;
}

process_result completed_process(const siginfo_t &status) {
  if (status.si_code == CLD_EXITED)
    return {true, false, status.si_status, {}};
  if (status.si_code == CLD_KILLED || status.si_code == CLD_DUMPED) {
    const int signal = status.si_status;
    return {true, false, 128 + signal,
            "linker driver terminated by signal " + std::to_string(signal)};
  }
  return {true, false, 1,
          "linker driver ended in an unknown process state"};
}

void signal_process_group(pid_t child, int signal) {
  // The direct signal is a fallback if the driver deliberately left the
  // process group that posix_spawn created for it. The child MUST remain
  // unreaped until the last signal: its reserved PID pins the group identity.
  ::kill(-child, signal);
  ::kill(child, signal);
}

bool poll_child(pid_t child, siginfo_t &status, bool &exited,
                bool &owned, std::string &error) {
  while (true) {
    siginfo_t observed{};
    if (::waitid(P_PID, static_cast<id_t>(child), &observed,
                 WEXITED | WNOHANG | WNOWAIT) == 0) {
      if (observed.si_pid == child) {
        status = observed;
        exited = true;
      }
      return true;
    }
    if (errno == EINTR)
      continue;
    // Never signal a numeric PID/group after an external reaper has released
    // our child. A caller must not reap run_process's children concurrently.
    if (errno == ECHILD)
      owned = false;
    error = "cannot wait for linker driver: " +
            std::string(std::strerror(errno));
    return false;
  }
}

bool sentinel_is_closed(int reader, bool &closed, std::string &error) {
  char bytes[64];
  // The pipe carries no data. A bounded read also tolerates a driver writing
  // to inherited descriptors without allowing it to monopolize this wait.
  const ssize_t count = ::read(reader, bytes, sizeof(bytes));
  if (count == 0) {
    closed = true;
    return true;
  }
  if (count > 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    return true;
  error = "cannot inspect linker-driver inheritance sentinel: " +
          std::string(std::strerror(errno));
  return false;
}

bool reap_child(pid_t child, bool &owned, std::string &error) {
  int status = 0;
  while (true) {
    const pid_t waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) {
      owned = false;
      return true;
    }
    if (waited < 0 && errno == EINTR)
      continue;
    if (waited < 0 && errno == ECHILD)
      owned = false;
    error = waited == 0
                ? "linker-driver process remained active after forced termination"
                : "cannot reap linker driver: " + std::string(std::strerror(errno));
    return false;
  }
}

void sleep_until_poll(steady_clock::time_point deadline) {
  const auto now = steady_clock::now();
  if (now >= deadline)
    return;
  std::this_thread::sleep_for(std::min(
      wait_poll_interval,
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)));
}

bool wait_for_posix_cleanup(pid_t child, int sentinel, bool &exited,
                            bool &owned, siginfo_t &status,
                            steady_clock::time_point deadline,
                            std::string &error) {
  while (steady_clock::now() < deadline) {
    if (!exited) {
      if (!poll_child(child, status, exited, owned, error))
        return false;
    }
    bool closed = false;
    if (!sentinel_is_closed(sentinel, closed, error))
      return false;
    if (exited && closed)
      return true;
    sleep_until_poll(deadline);
  }

  if (!exited && !poll_child(child, status, exited, owned, error))
    return false;
  bool closed = false;
  if (!sentinel_is_closed(sentinel, closed, error))
    return false;
  if (exited && closed)
    return true;
  error = !closed
              ? "linker-driver descendants remained active after forced "
                "termination"
              : "linker-driver process remained active after forced termination";
  return false;
}

bool terminate_posix_group(pid_t child, int sentinel, bool &exited,
                           bool &owned, siginfo_t &status,
                           std::string &error) {
  if (!owned) {
    error = "linker-driver identity was released by another child reaper";
    return false;
  }
  signal_process_group(child, SIGTERM);
  std::string ignored;
  if (wait_for_posix_cleanup(child, sentinel, exited, owned, status,
                             deadline_after(termination_grace), ignored)) {
    return reap_child(child, owned, error);
  }

  if (!owned) {
    error = std::move(ignored);
    return false;
  }
  // An exited leader stays a zombie until all group signaling is finished.
  // Reaping earlier lets PID reuse redirect SIGKILL to an unrelated group.
  signal_process_group(child, SIGKILL);
  const bool cleaned = wait_for_posix_cleanup(
      child, sentinel, exited, owned, status,
      deadline_after(termination_confirmation), error);
  // Even if a descendant escaped the group, release our terminated leader.
  // There must be no further group/PID signals after this point.
  std::string reap_error;
  const bool reaped = owned && reap_child(child, owned, reap_error);
  if (!reaped && error.empty())
    error = std::move(reap_error);
  return cleaned && reaped;
}

process_result finish_posix_process(pid_t child, int sentinel,
                                    siginfo_t &status, bool &owned) {
  process_result completed = completed_process(status);
  std::string sentinel_error;
  bool closed = false;
  if (sentinel_is_closed(sentinel, closed, sentinel_error) && closed) {
    std::string reap_error;
    if (reap_child(child, owned, reap_error))
      return completed;
    return {true, false, 1, std::move(reap_error)};
  }

  bool exited = true;
  std::string cleanup_error;
  const bool cleaned = terminate_posix_group(
      child, sentinel, exited, owned, status, cleanup_error);
  if (completed.exit_code == 0)
    completed.exit_code = 1;
  completed.error =
      sentinel_error.empty()
          ? "linker driver exited while descendant processes remained"
          : std::move(sentinel_error);
  if (!cleaned)
    completed.error += "; cleanup could not be confirmed: " + cleanup_error;
  return completed;
}

#endif

} // namespace

process_result run_process(const std::vector<std::string> &arguments,
                           std::chrono::milliseconds timeout) {
  if (arguments.empty())
    return {false, false, 1, "empty linker-driver command"};
  if (arguments.front().empty())
    return {false, false, 1, "linker-driver executable is empty"};
  if (timeout <= std::chrono::milliseconds::zero())
    return {false, false, 1, "linker-driver timeout must be positive"};
  if (std::any_of(arguments.begin(), arguments.end(), [](const auto &argument) {
        return argument.find('\0') != std::string::npos;
      })) {
    return {false, false, 1,
            "linker-driver arguments must not contain null bytes"};
  }

#ifdef _WIN32
  std::wstring application;
  std::vector<wchar_t> command_line;
  std::string conversion_error;
  if (!build_windows_command_line(arguments, application, command_line,
                                  conversion_error)) {
    return {false, false, 1, std::move(conversion_error)};
  }

  unique_handle job(CreateJobObjectW(nullptr, nullptr));
  if (job.get() == nullptr) {
    return {false, false, 1,
            "cannot create linker-driver job: " +
                windows_error_message(GetLastError())};
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits))) {
    return {false, false, 1,
            "cannot configure linker-driver job: " +
                windows_error_message(GetLastError())};
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process_information{};
  const DWORD creation_flags =
      CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
  if (!CreateProcessW(application.c_str(), command_line.data(), nullptr, nullptr,
                      FALSE, creation_flags, nullptr, nullptr, &startup,
                      &process_information)) {
    return {false, false, 1,
            "cannot create linker-driver process: " +
                windows_error_message(GetLastError())};
  }

  unique_handle process(process_information.hProcess);
  unique_handle thread(process_information.hThread);
  if (!AssignProcessToJobObject(job.get(), process.get())) {
    const DWORD assignment_error = GetLastError();
    std::string cleanup_error;
    const bool cleaned = terminate_windows_process(process.get(), cleanup_error);
    std::string error = "cannot contain linker-driver process in a job: " +
                        windows_error_message(assignment_error);
    if (!cleaned)
      error += "; cleanup could not be confirmed: " + cleanup_error;
    return {false, false, 1, std::move(error)};
  }
  if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
    const DWORD resume_error = GetLastError();
    std::string cleanup_error;
    const bool cleaned =
        terminate_windows_job(job.get(), process.get(), cleanup_error);
    std::string error = "cannot start linker-driver process: " +
                        windows_error_message(resume_error);
    if (!cleaned)
      error += "; cleanup could not be confirmed: " + cleanup_error;
    return {false, false, 1, std::move(error)};
  }
  thread.reset();

  const auto deadline = deadline_after(timeout);
  while (true) {
    const DWORD wait_result = bounded_windows_wait(process.get(), deadline);
    if (wait_result == WAIT_OBJECT_0) {
      DWORD exit_code = 1;
      if (!GetExitCodeProcess(process.get(), &exit_code)) {
        const DWORD exit_error = GetLastError();
        std::string cleanup_error;
        const bool cleaned =
            terminate_windows_job(job.get(), process.get(), cleanup_error);
        std::string error = "cannot read linker-driver exit code: " +
                            windows_error_message(exit_error);
        if (!cleaned)
          error += "; cleanup could not be confirmed: " + cleanup_error;
        return {true, false, 1,
                std::move(error)};
      }

      bool job_empty = false;
      std::string job_error;
      if (windows_job_is_empty(job.get(), job_empty, job_error) && job_empty)
        return {true, false, static_cast<int>(exit_code), {}};

      const bool residual_processes = job_error.empty();
      std::string cleanup_error;
      const bool cleaned =
          terminate_windows_job(job.get(), process.get(), cleanup_error);
      std::string error = residual_processes
                              ? "linker driver exited while descendant processes "
                                "remained"
                              : std::move(job_error);
      if (!cleaned)
        error += "; cleanup could not be confirmed: " + cleanup_error;
      const int result_code =
          exit_code == 0 ? 1 : static_cast<int>(exit_code);
      return {true, false, result_code, std::move(error)};
    }
    if (wait_result == WAIT_FAILED) {
      const DWORD wait_error = GetLastError();
      std::string cleanup_error;
      const bool cleaned =
          terminate_windows_job(job.get(), process.get(), cleanup_error);
      std::string error = "cannot wait for linker driver: " +
                          windows_error_message(wait_error);
      if (!cleaned)
        error += "; cleanup could not be confirmed: " + cleanup_error;
      return {true, false, 1, std::move(error)};
    }
    if (steady_clock::now() < deadline)
      continue;

    std::string cleanup_error;
    const bool cleaned =
        terminate_windows_job(job.get(), process.get(), cleanup_error);
    std::string error = "linker driver timed out after " +
                        std::to_string(timeout.count()) + " ms";
    if (!cleaned)
      error += "; cleanup could not be confirmed: " + cleanup_error;
    return {true, true, timeout_exit_code, std::move(error)};
  }
#else
  // WNOWAIT pins an exited child's PID only while the caller retains normal
  // child ownership. Auto-reaping dispositions make group cleanup unsafe.
  struct sigaction child_action{};
  if (::sigaction(SIGCHLD, nullptr, &child_action) != 0) {
    return {false, false, 1,
            "cannot inspect SIGCHLD disposition: " +
                std::string(std::strerror(errno))};
  }
  if (child_action.sa_handler == SIG_IGN ||
      (child_action.sa_flags & SA_NOCLDWAIT) != 0) {
    return {false, false, 1,
            "linker-driver execution requires a non-auto-reaping SIGCHLD disposition"};
  }

  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1U);
  for (const auto &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);

  spawn_attributes attributes;
  if (attributes.error() != 0) {
    return {false, false, 1,
            "cannot initialize linker-driver process attributes: " +
                std::string(std::strerror(attributes.error()))};
  }
  int attribute_error =
      posix_spawnattr_setflags(attributes.get(), POSIX_SPAWN_SETPGROUP);
  if (attribute_error == 0)
    attribute_error = posix_spawnattr_setpgroup(attributes.get(), 0);
  if (attribute_error != 0) {
    return {false, false, 1,
            "cannot configure linker-driver process group: " +
                std::string(std::strerror(attribute_error))};
  }

  unique_file_descriptor sentinel_reader;
  unique_file_descriptor sentinel_writer;
  std::string sentinel_error;
  if (!create_inheritance_sentinel(sentinel_reader, sentinel_writer,
                                   sentinel_error))
    return {false, false, 1, std::move(sentinel_error)};

  spawn_file_actions file_actions;
  int action_error = file_actions.error();
  if (action_error == 0)
    action_error = posix_spawn_file_actions_addclose(
        file_actions.get(), sentinel_reader.get());
  if (action_error == 0) {
    // A different destination descriptor ensures dup2 clears CLOEXEC. Only
    // the write end survives exec in the child and its ordinary descendants.
    action_error = posix_spawn_file_actions_adddup2(
        file_actions.get(), sentinel_writer.get(), sentinel_reader.get());
  }
  if (action_error == 0)
    action_error = posix_spawn_file_actions_addclose(
        file_actions.get(), sentinel_writer.get());
  if (action_error != 0) {
    return {false, false, 1,
            "cannot configure linker-driver inheritance sentinel: " +
                std::string(std::strerror(action_error))};
  }

  pid_t child = -1;
  const int spawn_error =
      posix_spawnp(&child, arguments.front().c_str(), file_actions.get(),
                   attributes.get(), argv.data(), environ);
  if (spawn_error != 0)
    return {false, false, 1, std::strerror(spawn_error)};
  sentinel_writer.reset();

  const auto deadline = deadline_after(timeout);
  siginfo_t status{};
  bool exited = false;
  bool owned = true;
  while (steady_clock::now() < deadline) {
    std::string wait_error;
    if (!poll_child(child, status, exited, owned, wait_error)) {
      std::string cleanup_error;
      if (!terminate_posix_group(child, sentinel_reader.get(), exited, owned,
                                 status, cleanup_error))
        wait_error += "; cleanup could not be confirmed: " + cleanup_error;
      return {true, false, 1, std::move(wait_error)};
    }
    if (exited)
      return finish_posix_process(child, sentinel_reader.get(), status, owned);
    sleep_until_poll(deadline);
  }

  // Resolve the boundary race before declaring a timeout.
  std::string wait_error;
  if (!poll_child(child, status, exited, owned, wait_error)) {
    std::string cleanup_error;
    if (!terminate_posix_group(child, sentinel_reader.get(), exited, owned,
                               status, cleanup_error))
      wait_error += "; cleanup could not be confirmed: " + cleanup_error;
    return {true, false, 1, std::move(wait_error)};
  }
  if (exited)
    return finish_posix_process(child, sentinel_reader.get(), status, owned);

  std::string cleanup_error;
  const bool cleaned =
      terminate_posix_group(child, sentinel_reader.get(), exited, owned, status,
                             cleanup_error);
  std::string error = "linker driver timed out after " +
                      std::to_string(timeout.count()) + " ms";
  if (!cleaned)
    error += "; cleanup could not be confirmed: " + cleanup_error;
  return {true, true, timeout_exit_code, std::move(error)};
#endif
}

} // namespace ny::linker_detail

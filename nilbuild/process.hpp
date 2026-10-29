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

#ifndef _WIN32
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
  (void)commands; (void)jobs; (void)options;
  throw std::runtime_error("nilbuild execution currently requires a POSIX host; Windows execution is not supported");
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
          if (input < 0 || dup2(input, STDIN_FILENO) < 0) _exit(126);
          if (input != STDIN_FILENO) close(input);
        }
        if (setpgid(0, 0) != 0 || dup2(pipes[1], STDOUT_FILENO) == -1 ||
            dup2(pipes[3], STDERR_FILENO) == -1 || chdir(command.directory.c_str()) != 0)
          _exit(126);
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
  (void)arguments; (void)directory;
  throw std::runtime_error("nilbuild execution currently requires a POSIX host; Windows execution is not supported");
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

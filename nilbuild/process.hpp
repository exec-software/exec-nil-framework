#pragma once

#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
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
// Results retain command order. After a failure, commands not yet launched
// have started == false; active siblings are terminated and marked cancelled.
// One poll loop owns every child and signal handler, so parallel execution
// does not race process-global signal state or write interleaved build logs.
inline std::vector<process_result> execute_many(const std::vector<process_command> &commands,
                                               std::size_t jobs) {
#ifdef _WIN32
  (void)commands; (void)jobs;
  throw std::runtime_error("nilbuild execution currently requires a POSIX host; Windows execution is not supported");
#else
  constexpr std::size_t output_limit = 16 * 1024 * 1024;
  constexpr auto time_limit = std::chrono::seconds(300);
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
    clock::time_point deadline;
  };
  std::vector<child_state> children(commands.size());
  std::size_t next = 0, active = 0;
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
      child.deadline = clock::now() + time_limit;
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
      if (now >= cancellation_deadline) signal_groups(SIGKILL);
      std::vector<pollfd> descriptors;
      std::vector<std::pair<std::size_t, unsigned>> owners;
      for (std::size_t i = 0; i < next; ++i) {
        const auto &child = children[i];
        if (child.complete) continue;
        if (now >= child.deadline)
          throw std::runtime_error("child execution exceeded 300 seconds");
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
        char buffer[8192];
        for (;;) {
          const auto count = read(fd, buffer, sizeof(buffer));
          if (count > 0) {
            child.received += static_cast<std::size_t>(count);
            if (child.received > output_limit) throw std::runtime_error("child output exceeds 16 MiB");
            (channel == 0 ? result.output : result.error).append(buffer, count);
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
            result.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) :
                            WIFSIGNALED(wait_status) ? 128 + WTERMSIG(wait_status) : 1;
            if (result.cancelled && !result.status) result.status = 128 + stop_signal;
            if (result.status && !stopping) stop(SIGTERM, i);
          } else if (waited == -1 && errno != EINTR) {
            throw std::runtime_error("cannot collect child exit status");
          }
        }
        if (child.reaped && child.streams[0] < 0 && child.streams[1] < 0) {
          if (stopping) kill(-child.pid, SIGKILL);
          child.complete = true;
          --active;
        }
      }
    }
    // A signal can arrive during the final drain/reap or before the first fork.
    if (pending_signal) {
      signal_groups(SIGKILL);
      for (auto &result : results) {
        result.cancelled = true;
        result.status = 128 + pending_signal;
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

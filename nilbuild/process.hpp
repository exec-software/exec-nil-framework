#pragma once

#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
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
};

#ifndef _WIN32
inline volatile sig_atomic_t active_group = 0;
inline volatile sig_atomic_t pending_signal = 0;
inline void forward_signal(int signal) {
  const int saved_errno = errno;
  pending_signal = signal;
  if (active_group > 0) kill(-active_group, signal);
  errno = saved_errno;
}
class cancellation_scope {
  struct sigaction old_int{}, old_term{};
public:
  cancellation_scope() {
    pending_signal = 0; active_group = 0;
    struct sigaction action{};
    action.sa_handler = forward_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &old_int) != 0)
      throw std::runtime_error("cannot install interruption handler");
    if (sigaction(SIGTERM, &action, &old_term) != 0) {
      sigaction(SIGINT, &old_int, nullptr);
      throw std::runtime_error("cannot install termination handler");
    }
  }
  ~cancellation_scope() {
    active_group = 0;
    sigaction(SIGINT, &old_int, nullptr);
    sigaction(SIGTERM, &old_term, nullptr);
  }
};
#endif

// The driver never invokes a shell. Compiler processes have bounded capture
// and execution time, with cancellation forwarded to the child process group.
// User programs replace the driver with execv after the build completes.
inline process_result execute(const std::vector<std::string> &arguments,
                              const std::filesystem::path &directory) {
#ifdef _WIN32
  (void)arguments; (void)directory;
  throw std::runtime_error("nilbuild execution currently requires a POSIX host; Windows execution is not supported");
#else
  constexpr std::size_t output_limit = 16 * 1024 * 1024;
  constexpr auto time_limit = std::chrono::seconds(300);
  cancellation_scope cancellation;
  int out[2], err[2];
  if (pipe(out) != 0) throw std::runtime_error("cannot create child output pipe");
  if (pipe(err) != 0) {
    close(out[0]); close(out[1]);
    throw std::runtime_error("cannot create child error pipe");
  }
  std::vector<char *> argv;
  for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);
  const pid_t child = fork();
  if (child == -1) {
    for (int fd : {out[0], out[1], err[0], err[1]}) close(fd);
    throw std::runtime_error("cannot start child process");
  }
  if (child == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    setpgid(0, 0);
    if (dup2(out[1], STDOUT_FILENO) == -1 || dup2(err[1], STDERR_FILENO) == -1 ||
        chdir(directory.c_str()) != 0) _exit(126);
    for (int fd : {out[0], out[1], err[0], err[1]}) close(fd);
    execv(argv[0], argv.data());
    const char message[] = "nilbuild: cannot execute child tool\n";
    (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(127);
  }
  // Either side may win this race; both request the same process group.
  setpgid(child, child);
  active_group = child;
  if (pending_signal) kill(-child, pending_signal);
  close(out[1]); close(err[1]);
  int streams[2] = {out[0], err[0]};
  bool reaped = false;
  auto cleanup = [&] {
    kill(-child, SIGKILL);
    if (!reaped) {
      kill(child, SIGKILL);
      int status;
      while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    for (int &fd : streams) if (fd >= 0) { close(fd); fd = -1; }
  };
  try {
    for (int fd : streams) {
      const int flags = fcntl(fd, F_GETFL);
      if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        throw std::runtime_error("cannot configure child output pipe");
    }
    process_result result;
    std::size_t received = 0;
    int wait_status = 0;
    const auto deadline = std::chrono::steady_clock::now() + time_limit;
    auto cancellation_deadline = std::chrono::steady_clock::time_point::max();
    while (!reaped || streams[0] >= 0 || streams[1] >= 0) {
      const auto now = std::chrono::steady_clock::now();
      if (pending_signal && cancellation_deadline == std::chrono::steady_clock::time_point::max())
        cancellation_deadline = now + std::chrono::seconds(2);
      if (now >= cancellation_deadline) { kill(-child, SIGKILL); if (!reaped) kill(child, SIGKILL); }
      if (now >= deadline)
        throw std::runtime_error("child execution exceeded 300 seconds");
      pollfd descriptors[2] = {{streams[0], POLLIN, 0}, {streams[1], POLLIN, 0}};
      const int ready = poll(descriptors, 2, 50);
      if (ready == -1 && errno != EINTR) throw std::runtime_error("cannot read child output");
      for (int i = 0; i < 2; ++i) {
        if (streams[i] < 0 || !descriptors[i].revents) continue;
        char buffer[8192];
        for (;;) {
          const auto count = read(streams[i], buffer, sizeof(buffer));
          if (count > 0) {
            received += static_cast<std::size_t>(count);
            if (received > output_limit) throw std::runtime_error("child output exceeds 16 MiB");
            (i == 0 ? result.output : result.error).append(buffer, count);
            // Bound each drain to keep stderr and cancellation responsive
            // even when a child continuously fills its stdout pipe.
            break;
          } else if (count == 0) { close(streams[i]); streams[i] = -1; break; }
          else if (errno == EINTR) continue;
          else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
          else throw std::runtime_error("cannot read child output");
        }
      }
      if (!reaped) {
        const auto waited = waitpid(child, &wait_status, WNOHANG);
        if (waited == child) reaped = true;
        else if (waited == -1 && errno != EINTR) throw std::runtime_error("cannot collect child exit status");
      }
    }
    if (pending_signal) {
      kill(-child, SIGKILL);
      result.cancelled = true; result.status = 128 + pending_signal;
      return result;
    }
    result.status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) :
                    WIFSIGNALED(wait_status) ? 128 + WTERMSIG(wait_status) : 1;
    return result;
  } catch (...) { cleanup(); throw; }
#endif
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

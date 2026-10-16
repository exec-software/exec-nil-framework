#ifndef NY_NILLINKER_PROCESS_RUNNER_HPP
#define NY_NILLINKER_PROCESS_RUNNER_HPP

#include <chrono>
#include <string>
#include <vector>

namespace ny::linker_detail {

struct process_result {
  bool launched = false;
  bool timed_out = false;
  int exit_code = 1;
  std::string error;
};

// On POSIX the caller must retain exclusive wait ownership of the spawned
// child (no concurrent catch-all reaper or changes to SIGCHLD disposition).
// Ordinary descendants are tracked by an inherited pipe and terminated as a
// process group. This is lifecycle management, not containment for programs
// that intentionally close inherited descriptors or escape their group.
process_result run_process(const std::vector<std::string> &arguments,
                           std::chrono::milliseconds timeout);

} // namespace ny::linker_detail

#endif // NY_NILLINKER_PROCESS_RUNNER_HPP

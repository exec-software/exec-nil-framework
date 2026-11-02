#ifndef NY_NILLINKER_PROCESS_RUNNER_HPP
#define NY_NILLINKER_PROCESS_RUNNER_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ny::linker_detail {

enum class windows_auxiliary_cleanup { none, msvc_telemetry };

struct auxiliary_cleanup_record {
  std::uint64_t process_id;
  std::string image;
};

struct process_result {
  bool launched = false;
  bool timed_out = false;
  int exit_code = 1;
  std::string error;
  // Explicitly requested cleanup of an owned, known optional toolchain helper.
  // Kept separate from errors so successful quiet/JSON commands stay clean.
  std::vector<auxiliary_cleanup_record> cleaned_auxiliaries;
};

// On POSIX the caller must retain exclusive wait ownership of the spawned
// child (no concurrent catch-all reaper or changes to SIGCHLD disposition).
// Ordinary descendants are tracked by an inherited pipe and terminated as a
// process group. This is lifecycle management, not containment for programs
// that intentionally close inherited descriptors or escape their group.
// Windows is strict by default. The MSVC policy is only for a positively
// selected absolute cl.exe/link.exe path: after successful driver exit, its
// exact sibling vctip.exe may be terminated, with verified empty-job cleanup.
process_result run_process(const std::vector<std::string> &arguments,
                           std::chrono::milliseconds timeout,
                           windows_auxiliary_cleanup auxiliary_policy =
                               windows_auxiliary_cleanup::none);

} // namespace ny::linker_detail

#endif // NY_NILLINKER_PROCESS_RUNNER_HPP

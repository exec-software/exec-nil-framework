// Test infrastructure only: never installed in the NIL Framework SDK.
// Reuse the linker's strict, bounded process ownership implementation rather
// than polling process names or recursively terminating numeric PIDs.
#include "../../nillinker/process_runner.hpp"
#include "../../common/standard_streams.hpp"
#include "../../common/windows_environment.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int run(const std::vector<std::string> &arguments) {
  ny::use_untranslated_standard_streams();
  try {
    if (arguments.size() < 5 || arguments[1] != "--timeout-ms" || arguments[3] != "--")
      throw std::runtime_error("usage: nil_vscode_host_runner --timeout-ms N -- EXECUTABLE [ARG ...]");
    const auto &text = arguments[2];
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
      throw std::runtime_error("invalid host deadline");
    const auto milliseconds = std::stoull(text);
    if (!milliseconds || milliseconds > 1800000)
      throw std::runtime_error("host deadline must be from 1 to 1800000 milliseconds");
    const auto result = ny::linker_detail::run_process(
        std::vector<std::string>(arguments.begin() + 4, arguments.end()),
        std::chrono::milliseconds(milliseconds));
    if (!result.error.empty()) std::cerr << "VS Code host runner: " << result.error << '\n';
    if (!result.launched || !result.error.empty() || result.timed_out)
      return result.timed_out ? 124 : 125;
    return result.exit_code;
  } catch (const std::exception &error) {
    std::cerr << "VS Code host runner: " << error.what() << '\n';
    return 125;
  }
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
  try {
    std::vector<std::string> arguments;
    for (int index = 0; index < argc; ++index)
      arguments.push_back(nil::detail::environment_utf8(argv[index], std::wcslen(argv[index])));
    return run(arguments);
  } catch (const std::exception &error) {
    std::cerr << "VS Code host runner: " << error.what() << '\n';
    return 125;
  }
}
#else
int main(int argc, char **argv) { return run(std::vector<std::string>(argv, argv + argc)); }
#endif

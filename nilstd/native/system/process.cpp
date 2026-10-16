/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 * See the LICENSE file in the project root for license terms.
 */
#include <string>
#include <vector>
#include <array>
#include <cstdio>
#include <memory>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#  include <cstdint>
#  define popen  _popen
#  define pclose _pclose
#else
#  include <sys/wait.h>
#endif

// struct must match Ny layout exactly (8 bytes, 8 bytes, 4 bytes = 20 bytes)
struct process_result {
    const char* stdout_str;
    const char* stderr_str;
    int32_t exit_code;
};

extern "C" {
process_result ml_process_execute(const char* command) {
    if (!command) return {strdup(""), strdup("invalid command"), -1};

    // Use /bin/sh -c for shell command
    std::string cmd = std::string(command) + " 2>&1";  // merge stderr for now
    std::array<char, 4096> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);

    if (!pipe) return {strdup(""), strdup("popen failed"), -1};

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    int raw = pclose(pipe.release());
#ifdef _WIN32
    int exit_code = raw;
#else
    int exit_code = WEXITSTATUS(raw);
#endif
    return {strdup(result.c_str()), strdup(""), exit_code};
}
} // extern "C"

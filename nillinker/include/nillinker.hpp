/*
 * Copyright (C) 2008 - 2026 Execute Software.  All rights reserved.
 * SPDX-License-Identifier: MIT
 * See the LICENSE file in the project root for license terms.
 */
#ifndef NY_NILLINKER_HPP
#define NY_NILLINKER_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ny {

// Links genuine host object files with the NIL native runtime. The portable NIL
// executable representation remains NIL; nillinker deliberately does not read
// or emit the abandoned NYOBJ/NYEXE prototype containers.
class Linker {
public:
  Linker(std::string driver, std::filesystem::path executable_path,
         std::chrono::milliseconds driver_timeout =
             std::chrono::milliseconds(300'000));

  bool add_object_file(const std::filesystem::path &object_path);
  bool set_runtime(const std::filesystem::path &runtime_path);
  bool link(const std::filesystem::path &output_path);

  const std::string &error() const { return error_; }

private:
  std::string driver_;
  std::filesystem::path executable_path_;
  std::vector<std::filesystem::path> object_files_;
  std::uint64_t admitted_object_bytes_ = 0;
  std::optional<std::filesystem::path> runtime_;
  std::chrono::milliseconds driver_timeout_;
  std::string error_;

  std::optional<std::filesystem::path> find_default_runtime();
};

} // namespace ny

#endif // NY_NILLINKER_HPP

/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
// Test-only niltest compiler adapter. Native application execution still uses
// the injected AppKit input driver; discovery/compilation and system linker
// children must not inherit it (Apple's launcher can have an arm64e slice).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {
int fail(const char *message) {
  std::fprintf(stderr, "AppKit test compiler adapter: %s\n", message);
  return 85;
}
const char *setting(const char *name) {
  const char *value = std::getenv(name);
  return value && *value && std::strlen(value) <= 32768 ? value : nullptr;
}
bool regular_absolute(const char *value) {
  if (!value || !fs::path(value).is_absolute()) return false;
  std::error_code error;
  return fs::is_regular_file(value, error) && !error;
}
}

int main(int argc, char **argv) {
  const char *compiler = setting("NIL_TEST_APPKIT_INPUT_COMPILER");
  const char *driver = setting("NIL_TEST_APPKIT_INPUT_DRIVER");
  const char *injection = setting("DYLD_INSERT_LIBRARIES");
  const char *token = setting("NIL_TEST_APPKIT_INPUT_TOKEN");
  const char *directory = setting("NIL_TEST_APPKIT_INPUT_DIRECTORY");
  const char *route = setting("NIL_TEST_APPKIT_INPUT_ROUTE");
  if (!regular_absolute(compiler) || !regular_absolute(driver)) return fail("declared compiler and input driver must be absolute regular files");
  if (!injection || std::strcmp(injection, driver)) return fail("injection does not match the declared fixture driver");
  if (!token || std::strlen(token) != 32 || std::strspn(token, "0123456789abcdef") != 32 ||
      !route || (std::strcmp(route, "vm") && std::strcmp(route, "native"))) return fail("invalid fixture token or route");
  std::error_code error;
  if (!directory || !fs::path(directory).is_absolute() || !fs::is_directory(directory, error) || error)
    return fail("fixture evidence directory is not an absolute directory");
  if (fs::equivalent(compiler, argv[0], error) || error) return fail("recursive or unresolved compiler adapter target");
  // Keep a private copy before removing the declaration that owns getenv's
  // storage. All ordinary environment settings and arguments remain intact.
  std::string selected(compiler);
  std::vector<char *> arguments;
  arguments.reserve(static_cast<std::size_t>(argc) + 1);
  arguments.push_back(selected.data());
  for (int index = 1; index < argc; ++index) arguments.push_back(argv[index]);
  arguments.push_back(nullptr);
  for (const char *name : {"DYLD_INSERT_LIBRARIES", "NIL_TEST_APPKIT_INPUT_COMPILER",
       "NIL_TEST_APPKIT_INPUT_DRIVER", "NIL_TEST_APPKIT_INPUT_TOKEN",
       "NIL_TEST_APPKIT_INPUT_DIRECTORY", "NIL_TEST_APPKIT_INPUT_ROUTE"})
    if (unsetenv(name)) return fail("could not remove fixture-owned environment declaration");
  execv(selected.c_str(), arguments.data());
  return fail("could not execute the declared compiler");
}

#ifndef NY_COMPILER_DIAGNOSTICS_HPP
#define NY_COMPILER_DIAGNOSTICS_HPP

// diagnostics.hpp
// Compiler diagnostic output and verbosity control
//
// This header provides a centralized system for controlling compiler output
// verbosity. It allows different parts of the compiler to emit diagnostics
// at appropriate verbosity levels.
//
// @ai_context(
//   intent="Provide centralized verbosity control for compiler diagnostics",
//   assumptions=["Global state acceptable for CLI tool", "Thread-safe not required"],
//   rationale="Eliminates debug spam and provides clean, structured output"
// )

#include <iostream>
#include <string>

// Verbosity levels
enum class verbosity_level {
  quiet = 0,   // Errors only
  normal = 1,  // Errors + warnings + success messages
  verbose = 2, // + info messages (compilation phases, module loading)
  debug = 3    // + detailed trace (parser state, type checking, codegen)
};

// Global verbosity level (defined in diagnostics.cpp)
extern int g_verbosity_level;

namespace ny {

// Diagnostic output functions

// @ai_context(
//   intent="Output error message (always shown)",
//   constraints=["Always prints regardless of verbosity level"]
// )
inline void diag_error(const std::string &msg) {
  std::cerr << "Error: " << msg << std::endl;
}

// @ai_context(
//   intent="Output warning message (shown at normal+ verbosity)",
//   constraints=["Requires verbosity >= normal"]
// )
inline void diag_warn(const std::string &msg) {
  if (g_verbosity_level >= (int)verbosity_level::normal) {
    std::cerr << "Warning: " << msg << std::endl;
  }
}

// @ai_context(
//   intent="Output info message (shown at verbose+ verbosity)",
//   constraints=["Requires verbosity >= verbose"]
// )
inline void diag_info(const std::string &msg) {
  if (g_verbosity_level >= (int)verbosity_level::verbose) {
    std::cout << "Info: " << msg << std::endl;
  }
}

// @ai_context(
//   intent="Output debug message (shown at debug verbosity only)",
//   constraints=["Requires verbosity >= debug"]
// )
inline void diag_debug(const std::string &msg) {
  if (g_verbosity_level >= (int)verbosity_level::debug) {
    std::cerr << "[debug] " << msg << std::endl;
  }
}

// @ai_context(
//   intent="Output success/completion message",
//   constraints=["Shown at normal+ verbosity"]
// )
inline void diag_success(const std::string &msg) {
  if (g_verbosity_level >= (int)verbosity_level::normal) {
    std::cout << msg << std::endl;
  }
}

// Check if debug output is enabled
inline bool is_debug_enabled() {
  return g_verbosity_level >= (int)verbosity_level::debug;
}

// Check if verbose output is enabled
inline bool is_verbose_enabled() {
  return g_verbosity_level >= (int)verbosity_level::verbose;
}

// Check if any output beyond errors is enabled
inline bool is_normal_enabled() {
  return g_verbosity_level >= (int)verbosity_level::normal;
}

} // namespace ny

#endif // NY_COMPILER_DIAGNOSTICS_HPP

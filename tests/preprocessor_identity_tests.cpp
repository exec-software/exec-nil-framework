#include "nil_version.hpp"
#include "preprocessor.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void verify_expansion(ny::preprocessor &processor, const std::string &expected) {
  const auto macro = processor.lookup_macro("__NIL_VERSION__");
  require(macro && macro->replacement == expected,
          "registered product version differs from resolved options");
  const auto result = processor.preprocess_text("__NIL_VERSION__\n", "identity.ny");
  require(result.success && result.diagnostics.empty(), "macro expansion failed");
  require(result.output == expected + "\n", "product macro did not expand exactly");
}

void verify_numeric_token_identity() {
  ny::preprocessor processor;
  const auto result = processor.preprocess_text(
      "@define e3 7\n@define E 8\n@define xDEAD 9\n"
      "@define LIMIT 3\n"
      "@define formula(e3, E) 1e3 + 1E+2 + e3 + E\n"
      "@define range(LIMIT) 0..LIMIT 1.0..LIMIT\n"
      "1e3 1E+2 1.5E-3 0xDEAD 0xFE+E\n"
      "formula(4, 5)\n0..LIMIT 1.0..LIMIT\nrange(4)\n", "numeric-tokens.ny");
  require(result.success && result.diagnostics.empty(), "numeric macro fixture failed");
  require(result.output.find("1e3 1E+2 1.5E-3 0xDEAD 0xFE+8") != std::string::npos,
          "macros must not replace identifiers inside numeric spellings");
  require(result.output.find("1e3 + 1E+2 + 4 + 5") != std::string::npos,
          "function macro parameters must not replace exponent spellings");
  require(result.output.find("0..3 1.0..3") != std::string::npos,
          "macros after numeric range boundaries must still expand");
  require(result.output.find("0..4 1.0..4") != std::string::npos,
          "function macro parameters after numeric range boundaries must still expand");
}
} // namespace

int main() {
  try {
    verify_numeric_token_identity();
    ny::preprocessor direct;
    require(direct.options().builtin_macros.at("__NIL_VERSION__") == NIL_VERSION,
            "constructor product version differs from generated identity");
    auto defaults = ny::make_default_preprocessor();
    verify_expansion(defaults, NIL_VERSION);

    ny::preprocessor_options options;
    options.builtin_macros["__NIL_VERSION__"] = "9.8.7-test.4";
    options.builtin_macros["__DATE__"] = "explicit-date";
    options.builtin_macros["__TIME__"] = "explicit-time";
    ny::preprocessor direct_override(options);
    require(direct_override.options().builtin_macros.at("__NIL_VERSION__") ==
                "9.8.7-test.4",
            "constructor overwrote explicit version");
    auto overridden = ny::make_default_preprocessor(options);
    verify_expansion(overridden, "9.8.7-test.4");
    require(overridden.lookup_macro("__DATE__")->replacement == "explicit-date" &&
                overridden.lookup_macro("__TIME__")->replacement == "explicit-time",
            "default registration overwrote caller builtin macros");
    require(options.builtin_macros.at("__NIL_VERSION__") == "9.8.7-test.4",
            "preprocessor modified caller options");
    std::cout << "preprocessor product identity and explicit overrides agree\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

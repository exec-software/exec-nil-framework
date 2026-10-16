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
} // namespace

int main() {
  try {
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

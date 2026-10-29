#include "parser.hpp"
#include "preprocessor.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;

void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

void annotation_attachment() {
  const std::string source =
      "@if false\n"
      "@disabled(\"outer\")\n"
      "fn hidden() -> i32 { return 0 }\n"
      "@if true\n"
      "@nested\n"
      "@endif\n"
      "@else\n"
      "@enabled(\"kept\")\n"
      "@endif\n"
      "fn visible() -> i32 { return 1 }\n";
  for (bool keep : {false, true}) {
    ny::preprocessor_options options;
    options.keep_directives_in_output = keep;
    ny::preprocessor processor(options);
    const auto result = processor.preprocess_text(source, "annotations.ny");
    require(result.success && result.diagnostics.empty(), "annotation conditionals preprocess cleanly");
    require(result.output.find("@disabled") == std::string::npos &&
                result.output.find("@nested") == std::string::npos &&
                result.output.find("fn hidden") == std::string::npos,
            "inactive annotations and declarations disappear even when directives are retained");
    require(result.output.find("@enabled(\"kept\")") != std::string::npos,
            "active unknown annotations retain their exact spelling");
    require(std::count(result.output.begin(), result.output.end(), '\n') == 10,
            "nested conditional removal preserves source line count");
    if (!keep)
      require(result.output == std::string(7, '\n') +
                  "@enabled(\"kept\")\n\nfn visible() -> i32 { return 1 }\n",
              "conditional output retains every active declaration on its original line");
  }

  ny::parser parser(source);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors() && program->declarations.size() == 1,
          "inactive annotated declaration does not disrupt parsing of the following declaration");
  const auto visible = std::dynamic_pointer_cast<ny::ast_function_decl>(program->declarations.front());
  require(visible && visible->name == "visible" && visible->line == 10 &&
              visible->annotations.size() == 1 && visible->annotations.front().name == "enabled" &&
              visible->annotations.front().args == std::optional<std::string>("\"kept\""),
          "only the active annotation attaches to the next function at its original source line");
}

void defined_conditionals() {
  const std::string source =
      "@define PRESENT 1\n"
      "@ifdef PRESENT\n"
      "@ifndef ABSENT\n"
      "fn present() -> i32 { return 1 }\n"
      "@else\n"
      "@discarded\n"
      "@endif\n"
      "@else\n"
      "@ifdef PRESENT\n"
      "@discarded_nested\n"
      "@endif\n"
      "@endif\n"
      "@ifndef PRESENT\n"
      "@discarded_missing\n"
      "@elif true\n"
      "fn fallback() -> i32 { return 2 }\n"
      "@endif\n";
  for (bool keep : {false, true}) {
    ny::preprocessor_options options;
    options.keep_directives_in_output = keep;
    ny::preprocessor processor(options);
    const auto result = processor.preprocess_text(source, "defined.ny");
    require(result.success && result.diagnostics.empty(), "nested defined conditionals preprocess cleanly");
    require((result.output.find("@ifdef PRESENT") != std::string::npos) == keep &&
                (result.output.find("@ifndef ABSENT") != std::string::npos) == keep,
            "ifdef and ifndef honor the directive-retention option");
    require(result.output.find("@discarded") == std::string::npos,
            "nested inactive annotations do not leak through defined conditionals");
    require(std::count(result.output.begin(), result.output.end(), '\n') == 17,
            "defined conditional branches preserve source line count");
    if (!keep)
      require(result.output == std::string(3, '\n') +
                  "fn present() -> i32 { return 1 }\n" + std::string(11, '\n') +
                  "fn fallback() -> i32 { return 2 }\n\n",
              "nested ifdef/ifndef and elif choose only the active code without shifting lines");
  }
  ny::parser parser(source);
  const auto program = parser.parse_program();
  require(program && !parser.has_errors() && program->declarations.size() == 2 &&
              program->declarations[0]->line == 4 && program->declarations[1]->line == 16,
          "ifdef and ifndef directives do not reach the language annotation parser");
}

void inactive_pragma_effects() {
  for (bool keep : {false, true}) {
    for (const auto *marker : {"@pragma once", "@schema"}) {
      int resolutions = 0;
      ny::preprocessor_options options;
      options.keep_directives_in_output = keep;
      options.resolve_source = [&](const std::filesystem::path &) -> std::optional<std::filesystem::path> {
        ++resolutions;
        return std::nullopt;
      };
      const std::string source = std::string("@if false\n") + marker +
          "\n@pragma link(\"hidden\")\n@pragma link(malformed\n@include \"missing.ny\"\n@endif\n"
          "@pragma link(\"visible\")\npayload\n";
      options.read_source = [&](const std::filesystem::path &) { return std::optional<std::string>(source); };
      ny::preprocessor processor(options);
      const auto first = processor.preprocess_file("/virtual/conditional.ny");
      const auto second = processor.preprocess_file("/virtual/conditional.ny");
      require(first.success && second.success && first.diagnostics.empty() && second.diagnostics.empty(),
              "disabled pragma payloads and missing includes produce no effects or diagnostics");
      require(first.output == second.output && first.output.find("payload\n") != std::string::npos,
              std::string("inactive ") + marker + " cannot suppress a later include visit");
      require(first.pragma_links.size() == 1 && first.pragma_links[0].lib_name == "visible" &&
                  second.pragma_links.size() == 1 && second.pragma_links[0].lib_name == "visible",
              "only active link pragmas contribute linker metadata");
      require(resolutions == 0, "inactive includes do not resolve or read their targets");
      require(std::count(first.output.begin(), first.output.end(), '\n') == 8,
              "inactive pragma and include removal preserves original line count");
    }
  }
}

void active_include_guards() {
  for (const auto *marker : {"@pragma once", "@schema"}) {
    ny::preprocessor_options options;
    const std::string source = std::string(marker) + "\npayload\n";
    options.read_source = [&](const std::filesystem::path &) { return std::optional<std::string>(source); };
    ny::preprocessor processor(options);
    const auto first = processor.preprocess_file("/virtual/guarded.ny");
    const auto second = processor.preprocess_file("/virtual/guarded.ny");
    require(first.success && first.output == "\npayload\n" && second.success && second.output.empty(),
            std::string("active ") + marker + " retains include-once behavior");
  }
}

void matrix_literal_and_name_boundaries() {
  ny::preprocessor_options options;
  options.matrix_surface = true;
  ny::preprocessor processor(options);
  const std::string source =
      "@define TOKEN 7\n@define TOKEN-PART 9\n@define PART 8\n" + std::string(R"matrix(TOKEN TOKEN-PART TOKEN-PART-extra PART TOKEN_PART TOKEN-PART2 TOKENé
"TOKEN\" TOKEN
`TOKEN` TOKEN !!! TOKEN @define PART 99
formatted "{select with "TOKEN,(x)", TOKEN} TOKEN" TOKEN
@custom(TOKEN, name being "TOKEN")
)matrix");
  const auto result = processor.preprocess_text(source, "literals.matrix");
  const std::string expected = std::string(3, '\n') + R"matrix(7 9 TOKEN-PART-extra 8 TOKEN_PART TOKEN-PART2 TOKENé
"TOKEN\" 7
`TOKEN` 7 !!! TOKEN @define PART 99
formatted "{select with "TOKEN,(x)", TOKEN} TOKEN" 7
@custom(TOKEN, name being "TOKEN")
)matrix";
  require(result.success && result.diagnostics.empty(), "Matrix literal and identifier fixture preprocesses cleanly");
  require(result.output == expected,
          "Matrix macros respect whole hyphenated/Unicode names, raw quotes, backticks, comments and formatted text");
  require(result.macros.at("PART").replacement == "8", "directives inside Matrix comments have no effect");
}

void matrix_raw_text_blocks() {
  ny::preprocessor_options options;
  options.matrix_surface = true;
  ny::preprocessor processor(options);
  const std::string source =
      "@define TOKEN 7\n@define begin changed\n@define text changed\n"
      "value block is BeGiN TeXt\n"
      "@define TOKEN 99\n@error literal only\n@endif\n TOKEN\n EnD \t TeXt\nTOKEN\n"
      "@if false\nvalue hidden is begin text\n@else\nTOKEN\nend text\n@else\nTOKEN\n@endif\n";
  const auto result = processor.preprocess_text(source, "blocks.matrix");
  const std::string expected = std::string(3, '\n') +
      "value block is BeGiN TeXt\n@define TOKEN 99\n@error literal only\n@endif\n TOKEN\n EnD \t TeXt\n7\n" +
      std::string(6, '\n') + "7\n\n";
  require(result.success && result.diagnostics.empty(), "Matrix text blocks hide directive-looking contents");
  require(result.output == expected,
          "active text blocks remain byte-exact and inactive blocks cannot alter conditional nesting");
  require(result.macros.at("TOKEN").replacement == "7", "Matrix block text cannot redefine macros");
  require(std::count(result.output.begin(), result.output.end(), '\n') == 18,
          "Matrix raw-block processing preserves source line count");

  ny::preprocessor generated(options);
  const auto opened = generated.preprocess_text(
      "@define BLOCK begin text\nvalue content is BLOCK\n@error literal\nend text\n", "generated.matrix");
  require(opened.success && opened.output == "\nvalue content is begin text\n@error literal\nend text\n",
          "a macro-generated raw block opener protects subsequent literal lines");
}

void matrix_multiline_annotations() {
  ny::preprocessor_options options;
  options.matrix_surface = true;
  const std::string metadata = R"matrix(@custom(
 TOKEN,
 "a literal )",
 !!! ) remains a comment
 @define TOKEN 99
 @error payload, not a directive
 formatted "{select with "(", TOKEN} )",
 text being begin text
 @endif
 )
 end text
)
)matrix";
  for (bool keep : {false, true}) {
    options.keep_directives_in_output = keep;
    ny::preprocessor processor(options);
    const auto result = processor.preprocess_text(
        "@define TOKEN 7\n" + metadata + "TOKEN\n@if false\n" + metadata + "@endif\nTOKEN\n",
        "annotations.matrix");
    require(result.success && result.diagnostics.empty(), "Matrix annotation payloads are opaque to directives");
    require(result.output.find(metadata) != std::string::npos &&
                result.output.find(metadata, result.output.find(metadata) + metadata.size()) == std::string::npos,
            "complete active annotation is retained exactly and disabled metadata is removed");
    require(result.macros.at("TOKEN").replacement == "7" && result.output.substr(result.output.size() - 2) == "7\n",
            "annotation property text cannot redefine macros or change enclosing conditional state");
  }
}

void matrix_function_macros() {
  ny::preprocessor_options options;
  options.matrix_surface = true;
  ny::preprocessor processor(options);
  const auto result = processor.preprocess_text(R"matrix(@define PAIR(left, right) (left, right)
@define LITERAL(item) "# ## item" plus item plus `item` plus formatted "{item}"
@define EMPTY() 42
@define ECHO-NAME(given-item) given-item
@define MANY(first, ...) [first, __VA_ARGS__]
PAIR("a,b)", "c(d")
PAIR([1, 2], nested(3, 4))
LITERAL("value")
EMPTY() EMPTY( )
ECHO-NAME("x,y)")
MANY("x,y", 2, 3)
)matrix", "functions.matrix");
  const std::string expected = std::string(5, '\n') + R"matrix(("a,b)", "c(d")
([1, 2], nested(3, 4))
"# ## item" plus "value" plus `item` plus formatted "{item}"
42 42
"x,y)"
["x,y", 2, 3]
)matrix";
  require(result.success && result.diagnostics.empty(), "Matrix function macros accept raw quoted and nested arguments");
  require(result.output == expected,
          "Matrix macro substitution preserves quoted parameters, formatted text and complete escaped names");

  for (const auto *source : {
           "@define PAIR(a,b) (a,b)\nPAIR(1)\n",
           "@define ONE(a) a\nONE(\"unterminated)\n",
           "@define ONE(a) a\nONE([1,2))\n",
           "@define ONE(a) a\nONE(,)\n",
           "@define BAD(a,a) a\n",
           "@define BAD(...,a) a\n",
           "@define BAD(a,) a\n",
           "@define BAD(a a\n",
           "@define STRINGIFY(a) #a\nSTRINGIFY(\"quoted\")\n",
           "@define PASTE(a,b) a##b\nPASTE(one,two)\n"}) {
    ny::preprocessor invalid(options);
    const auto rejected = invalid.preprocess_text(source, "invalid.matrix");
    require(!rejected.success && !rejected.diagnostics.empty() &&
                rejected.diagnostics.front().kind == ny::preproc_diag_kind::error &&
                rejected.diagnostics.front().filename == "invalid.matrix" &&
                rejected.diagnostics.front().line > 0,
            "malformed or unsupported Matrix macro forms produce located errors instead of corrupt output");
  }
}

void disabled_macro_expansion() {
  for (bool matrix : {false, true}) {
    ny::preprocessor_options options;
    options.matrix_surface = matrix;
    options.expand_macros = false;
    ny::preprocessor processor(options);
    const auto result = processor.preprocess_text(
        "@define TOKEN 7\n@if defined(TOKEN)\nTOKEN\n@endif\n", "disabled.ny");
    require(result.success && result.output == "\n\nTOKEN\n\n" && result.macros.count("TOKEN") == 1,
            "disabling macro expansion preserves source identifiers while directive definitions still work");
  }
}

void matrix_include_callbacks_and_limits() {
  ny::preprocessor_options options;
  options.matrix_surface = true;
  int reads = 0;
  options.resolve_source = [](const std::filesystem::path &path) -> std::optional<std::filesystem::path> {
    return path.filename() == "buffer.matrix" ? std::optional<std::filesystem::path>(path) : std::nullopt;
  };
  options.read_source = [&](const std::filesystem::path &) -> std::optional<std::string> {
    ++reads;
    return "@define TOKEN 7\nvalue text-value is begin text\n@error literal\nend text\n";
  };
  ny::preprocessor processor(options);
  const auto included = processor.preprocess_text("@include \"buffer.matrix\"\nTOKEN\n", "/virtual/root.matrix");
  require(included.success && reads == 1 && included.included_files.size() == 1 &&
              included.output == "\nvalue text-value is begin text\n@error literal\nend text\n7\n",
          "Matrix includes use authoritative unsaved buffers with the same surface and macro state");

  options.read_source = [](const std::filesystem::path &) -> std::optional<std::string> { return std::nullopt; };
  ny::preprocessor denied(options);
  const auto missing = denied.preprocess_text("@include \"buffer.matrix\"\n", "/virtual/root.matrix");
  require(!missing.success && !missing.diagnostics.empty(), "failed Matrix include callback cannot fall back to disk");

  for (int limit : {0, 1, 2}) {
    auto bounded = options;
    bounded.max_output_bytes = limit == 0 ? 16 : 0;
    bounded.max_include_depth = limit == 1 ? 1 : 0;
    bounded.max_include_visits = limit == 2 ? 1 : 0;
    bounded.read_source = [limit](const std::filesystem::path &) -> std::optional<std::string> {
      return limit == 1 ? "@include \"buffer.matrix\"\n" : "";
    };
    ny::preprocessor limited(bounded);
    bool rejected = false;
    try {
      limited.preprocess_text(limit == 0 ? "value text-value is begin text\nlong literal content\nend text\n" :
          "@include \"buffer.matrix\"\n@include \"buffer.matrix\"\n", "/virtual/root.matrix");
    } catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "Matrix preprocessing retains output, include-depth and include-visit limits");
  }
}
} // namespace

int main() {
  try {
    annotation_attachment();
    defined_conditionals();
    inactive_pragma_effects();
    active_include_guards();
    matrix_literal_and_name_boundaries();
    matrix_raw_text_blocks();
    matrix_multiline_annotations();
    matrix_function_macros();
    disabled_macro_expansion();
    matrix_include_callbacks_and_limits();
    std::cout << "PASS: " << checks << " preprocessor conditional and annotation checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

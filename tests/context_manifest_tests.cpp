#include "codegen.hpp"
#include "executable_context.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;
int assertions = 0;
using Bytes = std::vector<std::uint8_t>;

void check(bool condition, const std::string &label) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}

void rejects(const std::function<void()> &operation, const std::string &label) {
  bool rejected = false;
  try { operation(); } catch (const std::runtime_error &) { rejected = true; }
  check(rejected, label);
}

void put(Bytes &bytes, std::size_t offset, std::uint64_t value, unsigned width) {
  for (unsigned index = 0; index < width; ++index)
    bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
}

void u32(Bytes &bytes, std::uint32_t value) {
  const auto offset = bytes.size();
  bytes.resize(offset + 4);
  put(bytes, offset, value, 4);
}

void string(Bytes &bytes, const std::string &value) {
  u32(bytes, static_cast<std::uint32_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

ny::nil_function function(std::string name, std::string annotation) {
  ny::nil_function value;
  value.name = std::move(name);
  value.params = {"value:i64"};
  value.return_type = "i64";
  value.source_line = 7;
  value.annotations = {std::move(annotation)};
  value.body = {{"STORE_LOCAL", {"value"}}, {"LOAD_LOCAL", {"value"}}, {"RET", {}}};
  return value;
}

ny::nil_struct_def record(std::string name, std::vector<std::string> annotations) {
  ny::nil_struct_def value;
  value.name = std::move(name);
  value.fields = {{"width", "i64"}};
  value.annotations = std::move(annotations);
  return value;
}

ny::nil_class_def klass(std::string name, std::vector<std::string> annotations) {
  ny::nil_class_def value;
  value.name = std::move(name);
  value.annotations = std::move(annotations);
  return value;
}

ny::nil_enum_def choice(std::string name, std::vector<std::string> annotations) {
  ny::nil_enum_def value;
  value.name = std::move(name);
  value.variants = {{"Hidden", ""}};
  value.annotations = std::move(annotations);
  return value;
}

ny::nil_module fixture() {
  ny::nil_module module;
  module.module_name = "context_fixture";
  module.functions = {
      function("zeta", "@ai_context(\"last\")"),
      function("alpha", "@ai_context(\"Greek π, emoji 😀, quote \\\" and slash \\\\\")"),
      function("hidden", "@ai_contextual(\"not selected\")"),
      function("other", "@test")};
  module.functions[1].annotations.push_back("@ai_context");
  module.functions[1].annotations.push_back("@ai_context(\"line\nnext\t\r\b\f\")");
  // A class emits a struct record and a class record under one name, so the
  // fixture carries that collision deliberately: identity is kind plus name.
  module.structs = {record("surface", {"@ai_context(\"the struct half\")"}),
                    record("rectangle", {"@ai_context(\"a shape on screen\")"}),
                    record("untold", {"@doc(\"not an intent\")"})};
  module.classes = {klass("surface", {"@ai_context(\"the class half\")"})};
  module.enums = {choice("window_state", {"@ai_context(\"what a window is doing\")"}),
                  choice("unstated", {})};
  return module;
}

Bytes wire(std::string name = "main", std::string parameter = "value:i64",
           std::string result = "i64", std::string annotation = "@ai_context",
           std::uint32_t line = 7) {
  Bytes bytes = {'N', 'Y', 'C', 'T', 'X', 0, '\r', '\n'};
  u32(bytes, 1);
  string(bytes, "unit");
  u32(bytes, 1);
  string(bytes, name);
  u32(bytes, line);
  u32(bytes, 1);
  string(bytes, parameter);
  string(bytes, result);
  u32(bytes, 1);
  string(bytes, annotation);
  return bytes;
}

// A version 2 manifest: one annotated function, then one declaration.
Bytes declared(std::string kind = "struct", std::string name = "rectangle",
               std::string annotation = "@ai_context(\"a shape\")",
               std::uint32_t annotations = 1, std::uint32_t count = 1) {
  Bytes bytes = {'N', 'Y', 'C', 'T', 'X', 0, '\r', '\n'};
  u32(bytes, 2);
  string(bytes, "unit");
  u32(bytes, 1);
  string(bytes, "main");
  u32(bytes, 7);
  u32(bytes, 1);
  string(bytes, "value:i64");
  string(bytes, "i64");
  u32(bytes, 1);
  string(bytes, "@ai_context");
  u32(bytes, count);
  for (std::uint32_t index = 0; index < count; ++index) {
    string(bytes, kind);
    string(bytes, name);
    u32(bytes, annotations);
    for (std::uint32_t i = 0; i < annotations; ++i) string(bytes, annotation);
  }
  return bytes;
}

Bytes elf(const Bytes &context, bool duplicate = false) {
  const std::string names("\0.shstrtab\0.ny.context\0", 23);
  const std::size_t context_offset = 64;
  const std::size_t names_offset = context_offset + context.size();
  const std::size_t table = (names_offset + names.size() + 7) & ~std::size_t(7);
  Bytes bytes(table + (duplicate ? 4 : 3) * 64, 0);
  bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
  bytes[4] = 2; bytes[5] = 1; bytes[6] = 1;
  put(bytes, 16, 1, 2); // ET_REL; inspection must not execute this container.
  put(bytes, 18, 62, 2); // EM_X86_64.
  put(bytes, 20, 1, 4);
  put(bytes, 40, table, 8);
  put(bytes, 52, 64, 2);
  put(bytes, 58, 64, 2);
  put(bytes, 60, duplicate ? 4 : 3, 2);
  put(bytes, 62, 1, 2);
  std::copy(context.begin(), context.end(), bytes.begin() + context_offset);
  std::copy(names.begin(), names.end(), bytes.begin() + names_offset);
  put(bytes, table + 64, 1, 4);
  put(bytes, table + 68, 3, 4); // SHT_STRTAB.
  put(bytes, table + 64 + 24, names_offset, 8);
  put(bytes, table + 64 + 32, names.size(), 8);
  put(bytes, table + 64 + 48, 1, 8);
  for (std::size_t index = 2; index < (duplicate ? 4U : 3U); ++index) {
    const auto section = table + index * 64;
    put(bytes, section, 11, 4);
    put(bytes, section + 4, 1, 4); // SHT_PROGBITS.
    put(bytes, section + 8, 2, 8); // SHF_ALLOC, deliberately not writable.
    put(bytes, section + 24, context_offset, 8);
    put(bytes, section + 32, context.size(), 8);
    put(bytes, section + 48, 1, 8);
  }
  return bytes;
}

std::string inspect(const Bytes &bytes) {
  return ny::inspect_context_artifact(std::vector<char>(bytes.begin(), bytes.end()));
}

void positives() {
  auto module = fixture();
  const auto bytes = ny::encode_context_manifest(module);
  const auto json = ny::context_manifest_json(module);
  check(ny::decode_context_manifest_json(bytes) == json, "wire and module JSON agree");
  check(json.find("\"schema\":\"ny.context.v2\"") != std::string::npos,
        "versioned schema is explicit");
  check(json.find("\"profile\":\"annotations\"") != std::string::npos,
        "self-description profile is explicit");
  check(json.find("alpha") < json.find("zeta"), "functions sorted by identity");
  check(json.find("hidden") == std::string::npos && json.find("other") == std::string::npos,
        "only exact ai_context annotations select functions");
  check((json.find("\\n") != std::string::npos || json.find("\\u000a") != std::string::npos) &&
        (json.find("\\t") != std::string::npos || json.find("\\u0009") != std::string::npos) &&
        (json.find("\\r") != std::string::npos || json.find("\\u000d") != std::string::npos) &&
        json.find("\\\"") != std::string::npos,
        "annotation control characters and quotes escaped");
  check(json.find("π") != std::string::npos && json.find("😀") != std::string::npos,
        "valid UTF-8 annotation text retained");
  check(json.find("value:i64") != std::string::npos &&
        json.find("\"declaration_line\":7") != std::string::npos,
        "signature and declaration line retained");
  std::reverse(module.functions.begin(), module.functions.end());
  check(ny::encode_context_manifest(module) == bytes, "wire independent of function insertion order");
  check(ny::context_manifest_json(module) == json, "JSON independent of function insertion order");
  check(inspect(elf(bytes)) == json, "ELF object extracts identical context without execution");
  auto executable = elf(bytes);
  put(executable, 16, 2, 2);
  check(inspect(executable) == json, "ELF executable extracts identical context without execution");
  put(executable, 16, 3, 2);
  check(inspect(executable) == json, "ELF position-independent executable extracts context");
  ny::nil_module empty;
  check(ny::decode_context_manifest_json(ny::encode_context_manifest(empty)) ==
        ny::context_manifest_json(empty), "empty module and selected function set round trip");
  auto boundary = wire("main", "value:i64", "i64", "@ai_context", INT32_MAX);
  check(!ny::decode_context_manifest_json(boundary).empty(), "maximum signed source line accepted");

  struct grouped_numbers : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
  };
  struct restore_locale {
    std::locale previous;
    ~restore_locale() { std::locale::global(previous); }
  } saved_locale{std::locale()};
  module.functions.back().source_line = 1000;
  const auto ungrouped = ny::context_manifest_json(module);
  check(ungrouped.find("\"declaration_line\":1000") != std::string::npos,
        "locale probe includes a selected multi-digit declaration line");
  std::locale::global(std::locale(saved_locale.previous, new grouped_numbers));
  check(ny::context_manifest_json(module) == ungrouped,
        "JSON integers independent of embedding process locale");
}

void declarations() {
  const auto module = fixture();
  const auto json = ny::context_manifest_json(module);

  check(json.find("\"types\":[") != std::string::npos, "types array is present");
  check(json.find("\"kind\":\"struct\"") != std::string::npos &&
        json.find("\"kind\":\"class\"") != std::string::npos &&
        json.find("\"kind\":\"enum\"") != std::string::npos,
        "a record, class and choice each reach the manifest");
  check(json.find("a shape on screen") != std::string::npos &&
        json.find("what a window is doing") != std::string::npos,
        "type intent text is carried");
  check(json.find("untold") == std::string::npos && json.find("unstated") == std::string::npos,
        "only exact ai_context annotations select a declaration");
  check(json.find("the struct half") != std::string::npos &&
        json.find("the class half") != std::string::npos,
        "a name shared by a struct and a class keeps both statements");
  // Sorted by kind then name, so "class" precedes "enum" precedes "struct".
  check(json.find("\"kind\":\"class\"") < json.find("\"kind\":\"enum\"") &&
        json.find("\"kind\":\"enum\"") < json.find("\"kind\":\"struct\""),
        "declarations sorted by kind then identity");

  auto shuffled = fixture();
  std::reverse(shuffled.structs.begin(), shuffled.structs.end());
  std::reverse(shuffled.enums.begin(), shuffled.enums.end());
  check(ny::encode_context_manifest(shuffled) == ny::encode_context_manifest(module),
        "wire independent of declaration insertion order");
  check(ny::context_manifest_json(shuffled) == json,
        "JSON independent of declaration insertion order");

  const auto bytes = ny::encode_context_manifest(module);
  check(ny::decode_context_manifest_json(bytes) == json,
        "declaration block round trips through the wire");
  check(inspect(elf(bytes)) == json, "ELF carries the declaration block");

  // A version 1 manifest predates the block and must still be readable.
  const auto legacy = ny::decode_context_manifest_json(wire());
  check(legacy.find("\"schema\":\"ny.context.v2\"") != std::string::npos &&
        legacy.find("\"types\":[]") != std::string::npos,
        "a version 1 manifest reads as one with no declarations");

  check(!ny::decode_context_manifest_json(declared()).empty(), "a version 2 wire decodes");

  rejects([&] { ny::decode_context_manifest_json(declared("record")); },
          "unknown declaration kind");
  rejects([&] { ny::decode_context_manifest_json(declared("struct", "")); },
          "declaration without a name");
  rejects([&] { ny::decode_context_manifest_json(declared("struct", "rectangle",
                                                          "@ai_context(\"a shape\")", 0)); },
          "declaration with no annotations");
  rejects([&] { ny::decode_context_manifest_json(declared("struct", "rectangle", "@doc(\"x\")")); },
          "declaration carrying an unexpected annotation");
  rejects([&] { ny::decode_context_manifest_json(declared("struct", "rectangle",
                                                          "@ai_context(\"a shape\")", 1, 2)); },
          "duplicate declaration identity");
  auto truncated = declared();
  truncated.pop_back();
  rejects([&] { ny::decode_context_manifest_json(truncated); }, "truncated declaration block");
  auto trailing = declared();
  trailing.push_back(0);
  rejects([&] { ny::decode_context_manifest_json(trailing); }, "trailing bytes after declarations");

  // A page that states intent only on its types is a real artifact.
  const std::string typed =
      "module unit\n@ai_context(\"a shape on screen\")\nstruct rectangle\nwidth: i64\nend struct\n";
  const auto inspected = ny::inspect_context_artifact(std::vector<char>(typed.begin(), typed.end()));
  check(inspected.find("rectangle") != std::string::npos,
        "a module with types but no functions is inspectable");
}

void nil_artifact_boundaries() {
  const std::string main = "module unit\nfunction main() -> i32:\nPUSH_CONST 0\nRET\nend function\n";
  check(ny::inspect_context_artifact(std::vector<char>(main.begin(), main.end())).find(
            "\"functions\":[]") != std::string::npos,
        "valid executable with no annotations has an empty context list");
  for (const std::string input : {"", "# ordinary comment\n", "module unit\n"}) {
    rejects([&] { ny::inspect_context_artifact(std::vector<char>(input.begin(), input.end())); },
            "textual artifact without function records rejected");
  }
  for (const std::string input : {
           "module unit\nfunction helper() -> i32:\nPUSH_CONST 0\nRET\nend function\n",
           "function main(value:i32) -> i32:\nSTORE_LOCAL value\nPUSH_CONST 0\nRET\nend function\n"}) {
    check(ny::inspect_context_artifact(std::vector<char>(input.begin(), input.end())).find(
              "\"functions\":[]") != std::string::npos,
          "function metadata inspection does not require a runnable main or module identity");
  }
}

void wire_negatives() {
  const auto valid = wire();
  for (std::size_t size = 0; size < valid.size(); ++size) {
    Bytes truncated(valid.begin(), valid.begin() + size);
    rejects([&] { ny::decode_context_manifest_json(truncated); }, "truncated wire at " + std::to_string(size));
  }
  auto bad = valid;
  bad[0] = 'X';
  rejects([&] { ny::decode_context_manifest_json(bad); }, "wrong magic");
  bad = valid; put(bad, 8, 3, 4);
  rejects([&] { ny::decode_context_manifest_json(bad); }, "unsupported context version");
  // A version 1 body read as version 2 runs out before the declaration count.
  bad = valid; put(bad, 8, 2, 4);
  rejects([&] { ny::decode_context_manifest_json(bad); }, "version 2 without a declaration block");
  bad = valid; bad.push_back(0);
  rejects([&] { ny::decode_context_manifest_json(bad); }, "trailing context bytes");
  bad = valid; put(bad, 12, 16385, 4);
  rejects([&] { ny::decode_context_manifest_json(bad); }, "oversized encoded string count");
  bad = valid; put(bad, 20, 4097, 4);
  rejects([&] { ny::decode_context_manifest_json(bad); }, "oversized encoded function count");
  for (const auto &malformed : {std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
                                std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2),
                                std::string("a\0b", 3)}) {
    bad = wire("main", "value:i64", "i64", "@ai_context(" + malformed + ")");
    rejects([&] { ny::decode_context_manifest_json(bad); }, "invalid UTF-8 or NUL annotation");
  }
  for (const auto &record : {wire(""), wire("main", ""), wire("main", "value:i64", ""),
                             wire("main", "value:i64", "i64", ""),
                             wire("main", "value:i64", "i64", "@ai_context", 0x80000000U)}) {
    rejects([&] { ny::decode_context_manifest_json(record); }, "invalid required field or source line");
  }
  Bytes large(1024 * 1024 + 1, 0);
  rejects([&] { ny::decode_context_manifest_json(large); }, "oversized context payload");
}

void encode_negatives() {
  auto reject_module = [](const ny::nil_module &module, const std::string &label) {
    rejects([&] { ny::encode_context_manifest(module); }, label + " encoding");
    rejects([&] { ny::context_manifest_json(module); }, label + " JSON");
  };
  auto bad = fixture(); bad.functions.push_back(bad.functions.front());
  reject_module(bad, "duplicate selected symbol");
  bad = fixture(); bad.functions[0].name.clear(); reject_module(bad, "empty selected name");
  bad = fixture(); bad.functions[0].return_type.clear(); reject_module(bad, "empty result");
  bad = fixture(); bad.functions[0].source_line = -1; reject_module(bad, "negative source line");
  bad = fixture(); bad.functions[0].params = {""}; reject_module(bad, "empty parameter");
  bad = fixture(); bad.functions[0].params.assign(257, "value:i64"); reject_module(bad, "parameter bound");
  bad = fixture(); bad.functions[0].annotations.assign(257, "@ai_context"); reject_module(bad, "annotation bound");
  bad = fixture(); bad.functions[0].annotations = {"@ai_context(" + std::string(16384, 'x') + ")"};
  reject_module(bad, "string bound");
  bad = fixture(); bad.functions[0].annotations = {std::string("@ai_context(\xc0\xaf)", 15)};
  reject_module(bad, "invalid encoded UTF-8");
  bad = fixture(); bad.module_name = std::string("a\0b", 3); reject_module(bad, "embedded NUL");
  bad = fixture(); bad.functions.clear();
  for (unsigned index = 0; index < 4097; ++index)
    bad.functions.push_back(function("f" + std::to_string(index), "@ai_context"));
  reject_module(bad, "function bound");
  bad.functions.clear();
  for (unsigned index = 0; index < 140; ++index)
    bad.functions.push_back(function("f" + std::to_string(index), "@ai_context(" + std::string(8192, 'x') + ")"));
  reject_module(bad, "total payload bound");
}

void elf_negatives() {
  const auto valid = elf(wire());
  const std::size_t table = valid.size() - 3 * 64;
  for (std::size_t size : {0U, 4U, 16U, 63U}) {
    Bytes bad(valid.begin(), valid.begin() + size);
    rejects([&] { inspect(bad); }, "truncated ELF header");
  }
  auto mutate = [&](std::size_t offset, std::uint64_t value, unsigned width, const std::string &label) {
    auto bad = valid; put(bad, offset, value, width);
    rejects([&] { inspect(bad); }, label);
  };
  mutate(4, 1, 1, "unsupported ELF32");
  mutate(5, 2, 1, "unsupported big endian ELF");
  mutate(18, 183, 2, "unsupported ELF machine");
  mutate(40, std::numeric_limits<std::uint64_t>::max() - 1, 8, "overflowed section table offset");
  mutate(58, 1, 2, "wrong section header size");
  mutate(60, 0xffff, 2, "section table exceeds artifact");
  mutate(62, 3, 2, "out-of-range string table index");
  mutate(table + 64 + 24, valid.size() + 1, 8, "string table outside artifact");
  mutate(table + 64 + 32, std::numeric_limits<std::uint64_t>::max(), 8, "overflowed string table range");
  mutate(table + 128, 0xffffffffU, 4, "section name outside string table");
  mutate(table + 128 + 24, valid.size() + 1, 8, "context section outside artifact");
  mutate(table + 128 + 32, std::numeric_limits<std::uint64_t>::max(), 8, "overflowed context section range");
  mutate(table + 128 + 4, 8, 4, "context section cannot be NOBITS");
  mutate(table + 128, 0, 4, "missing context section");
  auto duplicate = elf(wire(), true);
  rejects([&] { inspect(duplicate); }, "duplicate context sections");
  auto bad = valid;
  const auto names_offset = 64 + wire().size();
  std::fill(bad.begin() + names_offset + 11, bad.begin() + names_offset + 23, 'x');
  rejects([&] { inspect(bad); }, "unterminated section name");
}
} // namespace

int main() {
  try {
    positives();
    declarations();
    nil_artifact_boundaries();
    wire_negatives();
    encode_negatives();
    elf_negatives();
  } catch (const std::exception &error) {
    std::cerr << "Unexpected context manifest failure: " << error.what() << '\n';
    return 1;
  }
  std::cout << "context manifest: " << assertions << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}

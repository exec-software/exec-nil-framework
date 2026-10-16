#include "executable_context.hpp"
#include "codegen.hpp"
#include "nil_loader.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ny {
namespace {
constexpr std::size_t max_bytes = 1024 * 1024;
constexpr std::size_t max_string = 16 * 1024;
constexpr std::size_t max_functions = 4096;
constexpr std::size_t max_declarations = 4096;
constexpr std::size_t max_members = 256;
constexpr std::array<std::uint8_t, 8> magic{{'N', 'Y', 'C', 'T', 'X', 0, '\r', '\n'}};

[[noreturn]] void invalid(const std::string &message) {
  throw std::runtime_error("Ny context: " + message);
}

bool valid_utf8(const std::string &text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80) continue;
    unsigned count = 0;
    std::uint32_t point = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; point = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; point = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; point = first & 0x07; minimum = 0x10000; }
    else return false;
    if (count > text.size() - i) return false;
    while (count--) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      point = (point << 6) | (next & 0x3f);
    }
    if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
  }
  return true;
}

void validate_string(const std::string &value, bool allow_empty = false) {
  if ((!allow_empty && value.empty()) || value.size() > max_string ||
      value.find('\0') != std::string::npos || !valid_utf8(value))
    invalid("invalid, oversized or non-UTF-8 string");
}

bool ai_annotation(const std::string &value) {
  return value == "@ai_context" ||
      (value.rfind("@ai_context(", 0) == 0 && value.back() == ')');
}

struct context_function {
  std::string name;
  std::uint32_t line;
  std::vector<std::string> params;
  std::string result;
  std::vector<std::string> annotations;
};
// A record, class or choice that states what it is for.
//
// NIL-0093 makes a program's stated intent part of what the artifact carries,
// and Matrix programs state most of their intent on types rather than on
// functions -- a window manager is records and choices nearly all the way
// down. Carrying only functions left those annotations write-only: present in
// the NIL text, invisible to every consumer.
//
// 'kind' is the artifact's own word ("struct", "class", "enum") rather than
// the Matrix surface's ("record", "class", "choice"). The manifest has to be
// reproducible by anyone holding the artifact, so it names what the artifact
// names; translating to a surface language here would put a mapping inside the
// format that Ny sources do not share.
//
// There is no source line: NIL carries none for a type declaration, and a
// fabricated zero would read as a real position.
struct context_declaration {
  std::string kind;
  std::string name;
  std::vector<std::string> annotations;
};
struct manifest {
  std::string module;
  std::vector<context_function> functions;
  std::vector<context_declaration> declarations;
};

bool declaration_kind(const std::string &value) {
  return value == "struct" || value == "class" || value == "enum";
}

void validate(manifest &value) {
  validate_string(value.module, true);
  if (value.functions.size() > max_functions) invalid("too many annotated functions");
  std::set<std::string> names;
  for (const auto &function : value.functions) {
    validate_string(function.name);
    validate_string(function.result);
    if (!names.insert(function.name).second) invalid("duplicate function identity");
    if (function.line > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
      invalid("source line exceeds signed 32-bit range");
    if (function.params.size() > max_members || function.annotations.empty() ||
        function.annotations.size() > max_members) invalid("invalid function member count");
    for (const auto &parameter : function.params) validate_string(parameter);
    for (const auto &annotation : function.annotations) {
      validate_string(annotation);
      if (!ai_annotation(annotation)) invalid("unexpected annotation in context record");
    }
  }
  std::sort(value.functions.begin(), value.functions.end(),
            [](const auto &left, const auto &right) { return left.name < right.name; });

  if (value.declarations.size() > max_declarations) invalid("too many annotated declarations");
  // Identity is the kind and the name together. A class emits both a struct
  // record and a class record under one name, so the name alone is not unique
  // in a valid artifact.
  std::set<std::pair<std::string, std::string>> declared;
  for (const auto &declaration : value.declarations) {
    if (!declaration_kind(declaration.kind)) invalid("unknown declaration kind");
    validate_string(declaration.name);
    if (!declared.insert({declaration.kind, declaration.name}).second)
      invalid("duplicate declaration identity");
    if (declaration.annotations.empty() || declaration.annotations.size() > max_members)
      invalid("invalid declaration member count");
    for (const auto &annotation : declaration.annotations) {
      validate_string(annotation);
      if (!ai_annotation(annotation)) invalid("unexpected annotation in context record");
    }
  }
  std::sort(value.declarations.begin(), value.declarations.end(),
            [](const auto &left, const auto &right) {
              if (left.kind != right.kind) return left.kind < right.kind;
              return left.name < right.name;
            });
}

manifest collect(const nil_module &module) {
  validate_string(module.module_name, true);
  manifest result{module.module_name, {}, {}};
  std::size_t budget = 12 + 4 + module.module_name.size() + 4;
  const auto account = [&](std::size_t amount) {
    if (amount > max_bytes - budget) invalid("manifest exceeds 1 MiB limit");
    budget += amount;
  };
  for (const auto &function : module.functions) {
    std::vector<std::string> annotations;
    for (const auto &annotation : function.annotations)
      if (ai_annotation(annotation)) {
        if (annotations.size() == max_members) invalid("too many annotations");
        validate_string(annotation);
        account(4 + annotation.size());
        annotations.push_back(annotation);
      }
    if (annotations.empty()) continue;
    if (result.functions.size() == max_functions) invalid("too many annotated functions");
    if (function.params.size() > max_members) invalid("too many parameters");
    if (function.source_line < 0) invalid("negative source line");
    validate_string(function.name);
    validate_string(function.return_type);
    account(20 + function.name.size() + function.return_type.size());
    for (const auto &parameter : function.params) {
      validate_string(parameter);
      account(4 + parameter.size());
    }
    result.functions.push_back({function.name, static_cast<std::uint32_t>(function.source_line),
                                function.params, function.return_type, std::move(annotations)});
  }

  // Derivation, not declaration: the annotations come from the artifact's own
  // records, so a program cannot understate what it says about itself any more
  // than it can understate the intrinsics it reaches.
  const auto gather = [&](const std::string &kind, const std::string &name,
                          const std::vector<std::string> &source) {
    std::vector<std::string> annotations;
    for (const auto &annotation : source)
      if (ai_annotation(annotation)) {
        if (annotations.size() == max_members) invalid("too many annotations");
        validate_string(annotation);
        account(4 + annotation.size());
        annotations.push_back(annotation);
      }
    if (annotations.empty()) return;
    if (result.declarations.size() == max_declarations)
      invalid("too many annotated declarations");
    validate_string(name);
    account(12 + kind.size() + name.size());
    result.declarations.push_back({kind, name, std::move(annotations)});
  };
  for (const auto &value : module.structs) gather("struct", value.name, value.annotations);
  for (const auto &value : module.classes) gather("class", value.name, value.annotations);
  for (const auto &value : module.enums) gather("enum", value.name, value.annotations);

  validate(result);
  return result;
}

struct writer {
  std::vector<std::uint8_t> bytes;
  void reserve(std::size_t amount) const {
    if (amount > max_bytes - bytes.size()) invalid("manifest exceeds 1 MiB limit");
  }
  void u32(std::uint32_t value) {
    reserve(4);
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back((value >> shift) & 0xff);
  }
  void string(const std::string &value) {
    reserve(4 + value.size());
    u32(static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
  }
};

std::vector<std::uint8_t> encode(const manifest &value) {
  writer output;
  output.bytes.insert(output.bytes.end(), magic.begin(), magic.end());
  // Version 2 adds the declaration block. Every manifest this compiler writes
  // is a 2, including one with no declarations, so the version says which
  // layout to read rather than what a particular program happened to contain.
  output.u32(2);
  output.string(value.module);
  output.u32(static_cast<std::uint32_t>(value.functions.size()));
  for (const auto &function : value.functions) {
    output.string(function.name);
    output.u32(function.line);
    output.u32(static_cast<std::uint32_t>(function.params.size()));
    for (const auto &parameter : function.params) output.string(parameter);
    output.string(function.result);
    output.u32(static_cast<std::uint32_t>(function.annotations.size()));
    for (const auto &annotation : function.annotations) output.string(annotation);
  }
  output.u32(static_cast<std::uint32_t>(value.declarations.size()));
  for (const auto &declaration : value.declarations) {
    output.string(declaration.kind);
    output.string(declaration.name);
    output.u32(static_cast<std::uint32_t>(declaration.annotations.size()));
    for (const auto &annotation : declaration.annotations) output.string(annotation);
  }
  return std::move(output.bytes);
}

std::string quote(const std::string &value) {
  const char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (const auto ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (ch == '"' || ch == '\\') { result += '\\'; result += ch; }
    else if (byte < 0x20) {
      result += "\\u00"; result += hex[byte >> 4]; result += hex[byte & 15];
    } else result += ch;
  }
  result += '"';
  return result;
}

void strings(std::ostream &output, const std::vector<std::string> &values) {
  output << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) output << ',';
    output << quote(values[i]);
  }
  output << ']';
}

std::string json(const manifest &value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\"schema\":\"ny.context.v2\",\"profile\":\"annotations\",\"module\":"
         << quote(value.module) << ",\"functions\":[";
  for (std::size_t i = 0; i < value.functions.size(); ++i) {
    if (i) output << ',';
    const auto &function = value.functions[i];
    output << "{\"name\":" << quote(function.name) << ",\"declaration_line\":" << function.line
           << ",\"parameters\":";
    strings(output, function.params);
    output << ",\"return_type\":" << quote(function.result) << ",\"annotations\":";
    strings(output, function.annotations);
    output << '}';
  }
  // 'types' is always present, empty included, so a consumer reads one shape
  // rather than branching on whether a program annotated any type.
  output << "],\"types\":[";
  for (std::size_t i = 0; i < value.declarations.size(); ++i) {
    if (i) output << ',';
    const auto &declaration = value.declarations[i];
    output << "{\"kind\":" << quote(declaration.kind) << ",\"name\":"
           << quote(declaration.name) << ",\"annotations\":";
    strings(output, declaration.annotations);
    output << '}';
  }
  output << "]}\n";
  return output.str();
}

struct reader {
  const std::vector<std::uint8_t> &bytes;
  std::size_t position = 0;
  std::uint32_t u32() {
    if (bytes.size() - position < 4) invalid("truncated record");
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) result |= std::uint32_t(bytes[position++]) << shift;
    return result;
  }
  std::size_t count(std::size_t maximum) {
    const auto value = u32();
    if (value > maximum) invalid("record count exceeds limit");
    return value;
  }
  std::string string() {
    const auto size = count(max_string);
    if (size > bytes.size() - position) invalid("truncated string");
    std::string value(bytes.begin() + position, bytes.begin() + position + size);
    position += size;
    return value;
  }
};

manifest decode(const std::vector<std::uint8_t> &bytes) {
  if (bytes.size() > max_bytes || bytes.size() < 12 ||
      !std::equal(magic.begin(), magic.end(), bytes.begin())) invalid("invalid context header or size");
  reader input{bytes, magic.size()};
  // A version 1 manifest has no declaration block and is read as one with
  // none. Artifacts embedded by an earlier compiler stay inspectable; refusing
  // them would make this change break reading rather than extend it.
  const auto version = input.u32();
  if (version != 1 && version != 2) invalid("unsupported context version");
  manifest result{input.string(), {}, {}};
  const auto count = input.count(max_functions);
  for (std::size_t i = 0; i < count; ++i) {
    context_function function;
    function.name = input.string();
    function.line = input.u32();
    const auto parameters = input.count(max_members);
    for (std::size_t j = 0; j < parameters; ++j) function.params.push_back(input.string());
    function.result = input.string();
    const auto annotations = input.count(max_members);
    for (std::size_t j = 0; j < annotations; ++j) function.annotations.push_back(input.string());
    result.functions.push_back(std::move(function));
  }
  if (version >= 2) {
    const auto declarations = input.count(max_declarations);
    for (std::size_t i = 0; i < declarations; ++i) {
      context_declaration declaration;
      declaration.kind = input.string();
      declaration.name = input.string();
      const auto annotations = input.count(max_members);
      for (std::size_t j = 0; j < annotations; ++j)
        declaration.annotations.push_back(input.string());
      result.declarations.push_back(std::move(declaration));
    }
  }
  if (input.position != bytes.size()) invalid("trailing context bytes");
  validate(result);
  return result;
}

bool range(std::uint64_t offset, std::uint64_t size, std::size_t total) {
  return offset <= total && size <= total - offset;
}
bool overlaps(std::uint64_t a, std::uint64_t size_a, std::uint64_t b, std::uint64_t size_b) {
  return a < b + size_b && b < a + size_a;
}

std::string inspect_elf(const std::vector<char> &bytes) {
  const auto integer = [&](std::size_t offset, unsigned width) {
    if (!range(offset, width, bytes.size())) invalid("truncated ELF record");
    std::uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i)
      result |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (i * 8);
    return result;
  };
  if (bytes.size() < 64 || bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1 ||
      integer(18, 2) != 62 || integer(20, 4) != 1 || integer(52, 2) != 64)
    invalid("native inspection requires ELF64 little-endian x86_64");
  const auto kind = integer(16, 2);
  if (kind != 1 && kind != 2 && kind != 3) invalid("unsupported ELF artifact kind");
  const auto table = integer(40, 8), count = integer(60, 2), names_index = integer(62, 2);
  if (integer(58, 2) != 64 || count == 0 || count > 4096 || names_index == 0 ||
      names_index >= count || table < 64 || !range(table, count * 64, bytes.size()))
    invalid("invalid or unsupported ELF section table");
  const auto names_header = table + names_index * 64;
  const auto names_offset = integer(names_header + 24, 8), names_size = integer(names_header + 32, 8);
  if (integer(names_header + 4, 4) != 3 || names_size == 0 || names_size > max_bytes ||
      names_offset < 64 || !range(names_offset, names_size, bytes.size()) ||
      overlaps(names_offset, names_size, table, count * 64)) invalid("invalid ELF section names");
  std::vector<std::uint8_t> context;
  bool found = false;
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto header = table + i * 64, name_offset = integer(header, 4);
    if (name_offset >= names_size) invalid("ELF section name is out of range");
    std::string name;
    std::uint64_t position = name_offset;
    for (; position < names_size && bytes[names_offset + position] != '\0'; ++position) {
      if (name.size() >= 255) invalid("ELF section name exceeds limit");
      name += bytes[names_offset + position];
    }
    if (position == names_size) invalid("unterminated ELF section name");
    if (name != ".ny.context") continue;
    if (found) invalid("duplicate native context section");
    found = true;
    const auto offset = integer(header + 24, 8), size = integer(header + 32, 8);
    if (integer(header + 4, 4) != 1 || (integer(header + 8, 8) & 0x800) ||
        size == 0 || size > max_bytes || offset < 64 || !range(offset, size, bytes.size()) ||
        overlaps(offset, size, table, count * 64) || overlaps(offset, size, names_offset, names_size))
      invalid("invalid native context section range or flags");
    context.assign(bytes.begin() + offset, bytes.begin() + offset + size);
  }
  if (!found) invalid("native artifact has no embedded context (embedding is opt-in)");
  return json(decode(context));
}
} // namespace

std::vector<std::uint8_t> encode_context_manifest(const nil_module &module) {
  return encode(collect(module));
}
std::string context_manifest_json(const nil_module &module) {
  auto value = collect(module);
  (void)encode(value); // The JSON view has exactly the embedded byte budget.
  return json(value);
}
std::string decode_context_manifest_json(const std::vector<std::uint8_t> &bytes) {
  return json(decode(bytes));
}
std::string inspect_context_artifact(const std::vector<char> &bytes) {
  if (bytes.size() > 512ULL * 1024ULL * 1024ULL) invalid("artifact exceeds 512 MiB limit");
  if (bytes.size() >= 4 && bytes[0] == '\x7f' && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F')
    return inspect_elf(bytes);
  if (bytes.size() > 256ULL * 1024ULL * 1024ULL) invalid("NIL exceeds 256 MiB limit");
  std::istringstream input(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
  const bool binary = bytes.size() >= 4 && bytes[0] == 'N' && bytes[1] == 'I' &&
      bytes[2] == 'L' && bytes[3] == '\0';
  auto module = binary ? parseBinaryNilModule(input) : parseTextNilModule(input);
  if (module->functions.empty() && module->structs.empty() && module->classes.empty() &&
      module->enums.empty())
    invalid("NIL artifact contains no function or type records");
  // Inspection validates structural metadata, not whether this module can run
  // on any backend. A function-bearing module need not declare an entrypoint.
  return context_manifest_json(*module);
}
} // namespace ny

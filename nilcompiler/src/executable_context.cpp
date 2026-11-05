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

// Read the embedded manifest out of a PE image or a COFF object.
//
// The section is named ".nyctx" rather than ".ny.context" because a PE image
// keeps only the eight bytes of a section name that fit in the section
// header; COFF's string-table indirection for longer names exists in object
// files and does not survive linking.
//
// Validation mirrors inspect_elf: the artifact must be the one architecture
// this inspects, every offset is bounds-checked against the file actually
// read, the section may not overlap the header it was found through, and a
// second section of the same name is a malformed artifact rather than a
// tie to break.
std::string inspect_pe(const std::vector<char> &bytes) {
  const auto integer = [&](std::size_t offset, unsigned width) {
    if (!range(offset, width, bytes.size())) invalid("truncated PE record");
    std::uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i)
      result |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (i * 8);
    return result;
  };

  // An image starts with the DOS stub and points at the PE signature; an
  // object file is a bare COFF header with no stub at all.
  std::uint64_t coff = 0;
  const bool image = bytes.size() >= 2 && bytes[0] == 'M' && bytes[1] == 'Z';
  if (image) {
    const auto signature = integer(0x3c, 4);
    if (signature < 0x40 || !range(signature, 4, bytes.size()) ||
        bytes[signature] != 'P' || bytes[signature + 1] != 'E' ||
        bytes[signature + 2] != '\0' || bytes[signature + 3] != '\0')
      invalid("native inspection requires a PE image or COFF object");
    coff = signature + 4;
  }

  if (integer(coff, 2) != 0x8664)
    invalid("native inspection requires PE/COFF x86_64");
  const auto count = integer(coff + 2, 2);
  const auto optional_size = integer(coff + 16, 2);
  // 96 is the number of sections a PE image may contain.
  if (count == 0 || count > 96) invalid("invalid or unsupported PE section table");
  if (image) {
    // PE32+ only: a 32-bit image would carry a different optional header and
    // is not a target this produces. Its fixed fields occupy 112 bytes; the
    // declared directory array must fit in the optional header as well.
    if (optional_size < 112 || !range(coff + 20, optional_size, bytes.size()) ||
        integer(coff + 20, 2) != 0x20b)
      invalid("native inspection requires PE32+ (64-bit) images");
    if (integer(coff + 20 + 108, 4) > (optional_size - 112) / 8)
      invalid("PE data directories exceed the optional header");
  } else if (optional_size != 0) {
    invalid("a COFF object must not carry an optional header");
  }

  const auto table = coff + 20 + optional_size;
  if (table < coff || !range(table, count * 40, bytes.size()))
    invalid("invalid or unsupported PE section table");
  const auto table_end = table + count * 40;
  const auto headers_end = image ? integer(coff + 20 + 60, 4) : table_end;
  if (headers_end < table_end || headers_end > bytes.size())
    invalid("invalid PE header extent");

  std::vector<std::uint8_t> context;
  bool found = false;
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto header = table + i * 40;
    std::string name;
    for (unsigned c = 0; c < 8; ++c) {
      const auto byte = static_cast<char>(integer(header + c, 1));
      if (byte == '\0') break;
      name += byte;
    }
    if (name != ".nyctx") continue;
    if (found) invalid("duplicate native context section");
    found = true;
    const auto virtual_size = integer(header + 8, 4);
    const auto raw_size = integer(header + 16, 4);
    const auto offset = integer(header + 20, 4);
    const auto characteristics = integer(header + 36, 4);
    // A linked image pads SizeOfRawData up to the file alignment, so
    // VirtualSize is the only exact length there; a COFF object leaves
    // VirtualSize zero and SizeOfRawData is exact. Do not let an object's
    // VirtualSize hide trailing bytes, or accept an image whose context would
    // need virtual zero-fill. The complete raw range, including image padding,
    // must exist and lie beyond all headers, not only the section table.
    const auto size = image ? virtual_size : raw_size;
    if ((characteristics & 0x00000040) == 0 ||    // CNT_INITIALIZED_DATA
        (characteristics & 0x00000020) != 0 ||    // CNT_CODE
        (characteristics & 0x00000080) != 0 ||    // CNT_UNINITIALIZED_DATA
        (characteristics & 0x20000000) != 0 ||    // MEM_EXECUTE
        size == 0 || size > max_bytes || size > raw_size || offset < headers_end ||
        !range(offset, raw_size, bytes.size()))
      invalid("invalid native context section range or flags");
    context.assign(bytes.begin() + offset, bytes.begin() + offset + size);
  }
  if (!found) invalid("native artifact has no embedded context (embedding is opt-in)");
  return json(decode(context));
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
// Mach-O context uses one ordinary, relocation-free data section. Universal
// containers and other Mach-O kinds deliberately remain outside this profile.
std::string inspect_macho(const std::vector<char> &bytes) {
  const auto integer = [&](std::uint64_t offset, unsigned width) {
    if (!range(offset, width, bytes.size())) invalid("truncated Mach-O record");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
      value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (i * 8);
    return value;
  };
  const auto name = [&](std::uint64_t offset) {
    if (!range(offset, 16, bytes.size())) invalid("truncated Mach-O name");
    std::string value;
    for (unsigned i = 0; i < 16 && bytes[offset + i] != '\0'; ++i)
      value += bytes[offset + i];
    return value;
  };
  if (bytes.size() < 32 || integer(0, 4) != 0xfeedfacf ||
      (integer(4, 4) != 0x01000007 && integer(4, 4) != 0x0100000c))
    invalid("native inspection requires thin Mach-O64 little-endian x86_64 or arm64");
  const auto kind = integer(12, 4);
  if ((kind != 1 && kind != 2) || integer(28, 4) != 0)
    invalid("unsupported Mach-O artifact kind or reserved header");
  const auto count = integer(16, 4), command_bytes = integer(20, 4);
  if (count == 0 || count > 4096 || command_bytes > max_bytes ||
      count > command_bytes / 8 || !range(32, command_bytes, bytes.size()))
    invalid("invalid Mach-O load command table");
  const auto headers_end = 32 + command_bytes;
  struct extent { std::uint64_t offset, size; };
  std::vector<extent> other_data;
  std::uint64_t context_offset = 0, context_size = 0, sections = 0;
  bool found = false;
  const auto retain_range = [&](std::uint64_t offset, std::uint64_t size) {
    if (!range(offset, size, bytes.size())) invalid("Mach-O data range exceeds artifact");
    if (size != 0) other_data.push_back({offset, size});
  };
  auto command = std::uint64_t(32);
  for (std::uint64_t i = 0; i < count; ++i) {
    if (!range(command, 8, headers_end)) invalid("truncated Mach-O load command");
    const auto type = integer(command, 4), size = integer(command + 4, 4);
    if (size < 8 || size % 8 != 0 || !range(command, size, headers_end))
      invalid("invalid Mach-O load command extent");
    if (type == 0x19) { // LC_SEGMENT_64
      if (size < 72) invalid("truncated Mach-O segment");
      const auto file_offset = integer(command + 40, 8);
      const auto file_size = integer(command + 48, 8);
      const auto section_count = integer(command + 64, 4);
      if (section_count > 4096 - sections || size != 72 + section_count * 80 ||
          !range(file_offset, file_size, bytes.size()))
        invalid("invalid Mach-O segment or section table");
      sections += section_count;
      for (std::uint64_t j = 0; j < section_count; ++j) {
        const auto section = command + 72 + j * 80;
        const auto offset = integer(section + 48, 4), length = integer(section + 40, 8);
        const auto flags = integer(section + 64, 4), section_type = flags & 0xff;
        const auto relocations = integer(section + 60, 4);
        const bool zero_fill = section_type == 1 || section_type == 0xc || section_type == 0x12;
        if (!zero_fill && length != 0 &&
            (offset < headers_end || offset < file_offset || offset - file_offset > file_size ||
             length > file_size - (offset - file_offset) || !range(offset, length, bytes.size())))
          invalid("Mach-O section is outside its segment or artifact");
        if (relocations != 0) retain_range(integer(section + 56, 4), relocations * 8);
        if (name(section) != "__nyctx") {
          if (!zero_fill) retain_range(offset, length);
          continue;
        }
        if (found) invalid("duplicate native context section");
        found = true;
        // MH_OBJECT coalesces sections into an unnamed segment. The section
        // still carries its intended final segment name, which must match.
        const auto segment_name = name(command + 8);
        if (name(section + 16) != "__DATA" ||
            (segment_name != "__DATA" && !(kind == 1 && segment_name.empty())) ||
            (flags & ~std::uint64_t(0x10000000)) != 0 || // S_REGULAR, optional NO_DEAD_STRIP
            relocations != 0 || integer(section + 56, 4) != 0 ||
            integer(section + 68, 4) != 0 || integer(section + 72, 4) != 0 ||
            integer(section + 76, 4) != 0 || integer(section + 52, 4) > 31 ||
            length == 0 || length > max_bytes || offset < headers_end ||
            !range(offset, length, bytes.size()))
          invalid("invalid native context section range or flags");
        context_offset = offset;
        context_size = length;
      }
    } else if (type == 2) { // LC_SYMTAB: nlist_64 records and the string table.
      if (size != 24) invalid("invalid Mach-O symbol table command");
      retain_range(integer(command + 8, 4), integer(command + 12, 4) * 16);
      retain_range(integer(command + 16, 4), integer(command + 20, 4));
    } else if (type == 3 || type == 0x16) { // LC_SYMSEG / LC_TWOLEVEL_HINTS.
      if (size != 16) invalid("invalid Mach-O legacy table command");
      retain_range(integer(command + 8, 4), integer(command + 12, 4) * (type == 3 ? 1 : 4));
    } else if (type == 0x31) { // LC_NOTE has 64-bit offset/size, unlike link-edit data.
      if (size != 40) invalid("invalid Mach-O note command");
      retain_range(integer(command + 24, 8), integer(command + 32, 8));
    } else if (type == 0x21 || type == 0x2c) {
      invalid("Mach-O encryption commands are unsupported");
    } else if (type == 0xb) { // LC_DYSYMTAB: its six file-offset/count pairs.
      if (size != 80) invalid("invalid Mach-O dynamic symbol table command");
      const unsigned widths[] = {8, 56, 4, 4, 8, 8};
      for (unsigned j = 0; j < 6; ++j)
        retain_range(integer(command + 32 + j * 8, 4),
                     integer(command + 36 + j * 8, 4) * widths[j]);
    } else if (type == 0x22 || type == 0x80000022) { // LC_DYLD_INFO[_ONLY].
      if (size != 48) invalid("invalid Mach-O dyld info command");
      for (unsigned j = 0; j < 5; ++j)
        retain_range(integer(command + 8 + j * 8, 4), integer(command + 12 + j * 8, 4));
    } else if (type == 0x1d || type == 0x1e || type == 0x26 || type == 0x29 ||
               type == 0x2b || type == 0x2e || type == 0x80000033 ||
               type == 0x80000034 || type == 0x36) {
      // linkedit_data_command: signatures, split info, function starts,
      // data-in-code, optimization hints, exports, chained fixups, atom info.
      if (size != 16) invalid("invalid Mach-O link-edit data command");
      retain_range(integer(command + 8, 4), integer(command + 12, 4));
    }
    command += size;
  }
  if (command != headers_end) invalid("Mach-O command count does not match its extent");
  if (!found) invalid("native artifact has no embedded context (embedding is opt-in)");
  for (const auto &data : other_data)
    if (overlaps(context_offset, context_size, data.offset, data.size))
      invalid("native context overlaps another Mach-O section or table");
  return json(decode(std::vector<std::uint8_t>(bytes.begin() + context_offset,
                                              bytes.begin() + context_offset + context_size)));
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
  if (bytes.size() >= 4) {
    std::uint32_t magic = 0;
    for (unsigned i = 0; i < 4; ++i)
      magic |= std::uint32_t(static_cast<unsigned char>(bytes[i])) << (i * 8);
    if (magic == 0xfeedfacf) return inspect_macho(bytes);
    if (magic == 0xfeedface || magic == 0xcefaedfe || magic == 0xcffaedfe ||
        magic == 0xcafebabe || magic == 0xbebafeca ||
        magic == 0xcafebabf || magic == 0xbfbafeca)
      invalid("native inspection requires thin Mach-O64 little-endian x86_64 or arm64");
  }
  // "MZ" is a PE image; a bare COFF object has no magic of its own, so it is
  // recognised by its machine type sitting in the first two bytes. A NIL
  // artifact cannot be mistaken for either: the text form begins with
  // "module" or a comment and the binary form with "NIL\0".
  if (bytes.size() >= 2 &&
      ((bytes[0] == 'M' && bytes[1] == 'Z') ||
       (static_cast<unsigned char>(bytes[0]) == 0x64 &&
        static_cast<unsigned char>(bytes[1]) == 0x86)))
    return inspect_pe(bytes);
  if (bytes.size() > 256ULL * 1024ULL * 1024ULL) invalid("NIL exceeds 256 MiB limit");
  const bool binary = bytes.size() >= 4 && bytes[0] == 'N' && bytes[1] == 'I' &&
      bytes[2] == 'L' && bytes[3] == '\0';
  const std::string contents(bytes.begin(), bytes.end());
  // Unknown native containers must not leak arbitrary bytes through the text
  // parser's diagnostics. Real UTF-8 NIL, including annotations, remains text.
  if (!binary && (!valid_utf8(contents) || contents.find('\0') != std::string::npos ||
      contents.compare(0, 8, "!<arch>\n") == 0))
    invalid("unrecognized binary artifact; expected NIL, ELF, PE/COFF or supported Mach-O");
  std::istringstream input(contents, std::ios::in | std::ios::binary);
  auto module = binary ? parseBinaryNilModule(input) : parseTextNilModule(input);
  if (module->functions.empty() && module->structs.empty() && module->classes.empty() &&
      module->enums.empty())
    invalid("NIL artifact contains no function or type records");
  // Inspection validates structural metadata, not whether this module can run
  // on any backend. A function-bearing module need not declare an entrypoint.
  return context_manifest_json(*module);
}
} // namespace ny

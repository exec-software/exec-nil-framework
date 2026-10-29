#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ny::lsp {

struct Position {
    int line = 0;
    int character = 0;
};

struct Range {
    Position start;
    Position end;
};

struct Diagnostic {
    Range range;
    int severity = 1;
    std::string message;
};

namespace diagnostic_severity {
static constexpr int Error = 1;
static constexpr int Warning = 2;
static constexpr int Information = 3;
static constexpr int Hint = 4;
}

struct Symbol {
    std::string name;
    int kind = 13;
    Range range;
    Range selectionRange;

    std::string detail;
    std::string documentation;
    std::string type;
    std::string owner_type;
    std::string module;
    int scope = 0;
    int parent = -1;
    std::size_t visible_after = 0;
    bool member = false;
    bool extension_member = false;
    std::vector<std::string> base_types;
};

struct Scope {
    int parent = -1;
    std::size_t start = 0;
    std::size_t end = 0;
    std::string module;
    bool open_at_end = false;
    std::string receiver_type;
};

struct ExcludedSpan {
    std::size_t start = 0;
    std::size_t end = 0;
    bool closed = true;
};

struct ParseResult {
    bool matrix = false;
    std::string module_name;
    std::vector<Symbol> symbols;
    std::vector<Diagnostic> diagnostics;
    std::vector<Scope> scopes;
    std::vector<ExcludedSpan> excluded;
    // Metadata is not a lexical value use. Matrix still offers annotation
    // vocabulary here, but ordinary completion/navigation must ignore it.
    std::vector<ExcludedSpan> annotations;
};

struct Completion {
    std::size_t symbol;
    Range edit;
};

class NyParser {
public:
    ParseResult parse(const std::string& uri, const std::string& text,
                      const std::string& language_id = {}) const;

    static std::vector<std::size_t> compute_line_offsets(const std::string& text);
    static std::optional<std::size_t> offset_at(const std::string& text,
                                               const std::vector<std::size_t>& line_offsets,
                                               const Position& pos);
    static Position position_at(const std::string& text,
                                const std::vector<std::size_t>& line_offsets, std::size_t offset);

    static std::string word_at(const std::string& text,
                               const std::vector<std::size_t>& line_offsets,
                               const Position& pos);
    static std::vector<Completion> complete(const ParseResult& parsed, const std::string& text,
                                            const std::vector<std::size_t>& offsets, const Position& pos);
    static std::optional<std::size_t> resolve(const ParseResult& parsed, const std::string& text,
                                             const std::vector<std::size_t>& offsets, const Position& pos);
};

namespace symbol_kind {
static constexpr int File = 1;
static constexpr int Module = 2;
static constexpr int Namespace = 3;
static constexpr int Class = 5;
static constexpr int Method = 6;
static constexpr int Field = 8;
static constexpr int Enum = 10;
static constexpr int Function = 12;
static constexpr int Variable = 13;
static constexpr int Constant = 14;
static constexpr int Struct = 23;
}

}

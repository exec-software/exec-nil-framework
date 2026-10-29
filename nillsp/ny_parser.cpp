#include "ny_parser.hpp"
#include "interpolated_text.hpp"
#include "matrix_parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <functional>
#include <tuple>

namespace ny::lsp {
namespace {

bool ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool ident(char c, const ParseResult& parsed) {
    return ident(c) || (parsed.matrix && (c == '-' || c == '`' || static_cast<unsigned char>(c) >= 0x80));
}

std::string key(const ParseResult& parsed, const std::string& name) {
    return parsed.matrix ? matrix_key(name) : name;
}

std::string matrix_type_base(std::string type) {
    // References expose the referent, but arrays and pointers do not pretend
    // to be records just because their element type is a record.
    if (matrix_key(type).rfind("reference to ", 0) == 0) type.erase(0, 13);
    const auto generic = matrix_key(type).find(" of ");
    if (generic != std::string::npos) {
        // Locate in the original spelling (the key may remove separators).
        auto lowered = type;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        });
        const auto at = lowered.find(" of ");
        if (at != std::string::npos) type.resize(at);
    }
    return matrix_key(type);
}

bool name_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

// Byte spans remain the internal representation. LSP positions are UTF-16.
std::pair<std::size_t, int> utf8_width(const std::string& text, std::size_t at) {
    const auto c = static_cast<unsigned char>(text[at]);
    std::size_t bytes = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
                        (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
    if (at + bytes > text.size()) return {1, 1};
    for (std::size_t i = 1; i < bytes; ++i)
        if ((static_cast<unsigned char>(text[at + i]) & 0xc0) != 0x80) return {1, 1};
    return {bytes, bytes == 4 ? 2 : 1};
}

struct Token {
    std::string text;
    std::size_t start;
    std::size_t end;
};

std::vector<Token> lex(const std::string& text, ParseResult& out) {
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        if (text.compare(i, 3, "###") == 0) {
            const auto end = text.find("###", i + 3);
            i = end == std::string::npos ? text.size() : end + 3;
            out.excluded.push_back({start, i, end != std::string::npos});
            for (auto newline = start; newline < i; ++newline)
                if (text[newline] == '\n') tokens.push_back({"\n", newline, newline + 1});
        } else if (text.compare(i, 2, "/*") == 0) {
            const auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            out.excluded.push_back({start, i, end != std::string::npos});
            for (auto newline = start; newline < i; ++newline)
                if (text[newline] == '\n') tokens.push_back({"\n", newline, newline + 1});
        } else if (text[i] == '#' || text.compare(i, 2, "//") == 0) {
            const auto end = text.find('\n', i);
            i = end == std::string::npos ? text.size() : end;
            out.excluded.push_back({start, i, false});
        } else if (text.compare(i, 2, "$\"") == 0) {
            i = editor_formatted_text(text, start, start + 1, false, out, lex);
            tokens.push_back({"<literal>", start, i});
        } else if (text[i] == '"' || text[i] == '\'') {
            const auto quote = text[i++];
            bool closed = false;
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) { i += 2; continue; }
                if (text[i++] == quote) { closed = true; break; }
            }
            out.excluded.push_back({start, i, closed});
            tokens.push_back({"<literal>", start, i});
        } else if (text[i] == '\n') {
            tokens.push_back({"\n", i, i + 1});
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        } else if (name_start(text[i])) {
            while (i < text.size() && ident(text[i])) ++i;
            tokens.push_back({text.substr(start, i - start), start, i});
        } else {
            ++i;
            if (text.compare(start, 2, "::") == 0 || text.compare(start, 2, "->") == 0 ||
                text.compare(start, 2, "..") == 0) ++i;
            tokens.push_back({text.substr(start, i - start), start, i});
        }
    }
    return tokens;
}

std::string type_base(std::string type) {
    if (type.rfind("ref<", 0) == 0 || type.rfind("ptr<", 0) == 0)
        type = type.substr(4, type.size() > 5 ? type.size() - 5 : 0);
    const auto generic = type.find('<');
    if (generic != std::string::npos) type.resize(generic);
    return type;
}

// This deliberately tolerant declaration index is not the compiler's type checker.
// Unknown expressions/types do not cause us to guess a receiver's member set.
class Indexer {
    const std::string& text;
    ParseResult& out;
    std::vector<Token> tokens;
    std::vector<std::size_t> lines;
    std::size_t cursor = 0;

    bool at(const std::string& value) const {
        return cursor < tokens.size() && tokens[cursor].text == value;
    }
    Range range(std::size_t start, std::size_t end) const {
        return {NyParser::position_at(text, lines, start), NyParser::position_at(text, lines, end)};
    }
    int add(const Token& name, int kind, int scope, int parent,
            std::size_t start, std::size_t end, const std::string& owner = {}) {
        Symbol symbol;
        symbol.name = name.text;
        symbol.kind = kind;
        symbol.range = range(start, end);
        symbol.selectionRange = range(name.start, name.end);
        symbol.detail = text.substr(start, end - start);
        symbol.scope = scope;
        symbol.parent = parent;
        symbol.module = out.scopes[scope].module;
        symbol.owner_type = owner;
        symbol.member = !owner.empty();
        out.symbols.push_back(std::move(symbol));
        return static_cast<int>(out.symbols.size() - 1);
    }
    int child(int parent, std::size_t start) {
        out.scopes.push_back({parent, start, text.size(), out.scopes[parent].module});
        return static_cast<int>(out.scopes.size() - 1);
    }
    std::string joined(std::size_t begin, std::size_t end) const {
        std::string result;
        for (auto i = begin; i < end; ++i)
            if (tokens[i].text != "\n") result += tokens[i].text;
        return result;
    }
    std::size_t line_end(std::size_t begin) const {
        while (begin < tokens.size() && tokens[begin].text != "\n" &&
               tokens[begin].text != ";" && tokens[begin].text != "}") ++begin;
        return begin;
    }
    std::size_t balanced(std::size_t begin, const std::string& open, const std::string& close) const {
        int depth = 0;
        for (auto i = begin; i < tokens.size(); ++i) {
            if (tokens[i].text == open) ++depth;
            if (tokens[i].text == close && --depth == 0) return i;
        }
        return tokens.size();
    }
    void finish(int scope, int symbol, std::size_t end) {
        out.scopes[scope].end = end;
        if (symbol >= 0) out.symbols[symbol].range.end = range(end, end).end;
    }
    void diagnostic(const Token& token, const std::string& message) {
        out.diagnostics.push_back({range(token.start, token.end), 1, message});
    }
    void parameters(std::size_t begin, std::size_t end, int scope, int parent,
                    const std::string& owner) {
        while (begin < end) {
            while (begin < end && (tokens[begin].text == "\n" || tokens[begin].text == ",")) ++begin;
            const auto declaration_begin = begin;
            while (begin < end && (tokens[begin].text == "mut" || tokens[begin].text == "ref" ||
                                   tokens[begin].text == ".." || tokens[begin].text == "\n")) ++begin;
            if (begin == end) break;
            const auto name = begin++;
            const auto type_begin = begin < end && tokens[begin].text == ":" ? ++begin : begin;
            int depth = 0;
            while (begin < end) {
                const auto& token = tokens[begin].text;
                if (token == "," && depth == 0) break;
                if (token == "<" || token == "(" || token == "[") ++depth;
                if (token == ">" || token == ")" || token == "]") --depth;
                ++begin;
            }
            if (!name_start(tokens[name].text.front())) continue;
            const auto id = add(tokens[name], symbol_kind::Variable, scope, parent,
                                tokens[declaration_begin].start, tokens[begin - 1].end);
            out.symbols[id].type = joined(type_begin, begin);
            if (tokens[name].text == "self" && out.symbols[id].type.empty()) out.symbols[id].type = owner;
            if (out.symbols[id].type == "Self" || out.symbols[id].type == "ref<Self>")
                out.symbols[id].type = owner;
        }
    }
    void function(int scope, int parent, const std::string& owner, bool signature, bool intrinsic,
                  std::size_t declaration_start) {
        const auto keyword = tokens[cursor++].text;
        if (cursor >= tokens.size() || !name_start(tokens[cursor].text.front())) return;
        const auto name = tokens[cursor++];
        if (at("<")) {
            const auto close = balanced(cursor, "<", ">");
            cursor = close < tokens.size() ? close + 1 : close;
        }
        if (!at("(")) return;
        const auto open = cursor;
        const auto close = balanced(cursor, "(", ")");
        if (close == tokens.size()) return;
        cursor = close + 1;
        std::string result_type;
        if (at("->")) {
            const auto begin = ++cursor;
            while (cursor < tokens.size() && !at("\n") && !at("{") && !at(";") &&
                   !at("return") && !at("let") && !at("const") && !at("if") &&
                   !at("while") && !at("for") && !at("loop") && !at("match") && !at("end")) ++cursor;
            result_type = joined(begin, cursor);
        }
        const auto end = cursor > 0 ? tokens[cursor - 1].end : name.end;
        const auto id = add(name, owner.empty() ? symbol_kind::Function : symbol_kind::Method,
                            scope, parent, declaration_start, end, owner);
        out.symbols[id].type = result_type;
        const auto body = child(scope, name.end);
        parameters(open + 1, close, body, id, owner);
        if (signature && !at("{")) { finish(body, id, end); return; }
        if (intrinsic && !at("{")) {
            auto next = cursor;
            while (next < tokens.size() && tokens[next].text == "\n") ++next;
            const auto next_word = next < tokens.size() ? tokens[next].text : "";
            if (next_word.empty() || next_word == "@" || next_word == "pub" ||
                next_word == "fn" || next_word == "function" || next_word == "module") {
                finish(body, id, end);
                return;
            }
        }
        const bool braces = at("{");
        if (braces) ++cursor;
        block(body, id, {}, braces ? "}" : keyword, false);
    }
    void variable(int scope, int parent, std::size_t declaration_start) {
        const bool constant = at("const");
        ++cursor;
        if (at("mut")) ++cursor;
        if (at("(")) {
            std::vector<Token> names;
            ++cursor;
            while (cursor < tokens.size() && name_start(tokens[cursor].text.front())) {
                names.push_back(tokens[cursor++]);
                if (!at(",")) break;
                ++cursor;
            }
            if (!at(")")) return;
            ++cursor;
            if (!at("=")) return;
            const auto end = line_end(++cursor);
            const auto finish_at = end > cursor ? tokens[end - 1].end : tokens[cursor - 1].end;
            for (const auto& name : names) {
                if (name.text == "_") continue;
                const auto id = add(name, constant ? symbol_kind::Constant : symbol_kind::Variable,
                                    scope, parent, declaration_start, finish_at);
                out.symbols[id].visible_after = finish_at;
            }
            cursor = end;
            return;
        }
        if (cursor >= tokens.size() || !name_start(tokens[cursor].text.front())) return;
        const auto name = tokens[cursor++];
        std::string declared;
        if (at(":")) {
            const auto begin = ++cursor;
            while (cursor < tokens.size() && !at("=") && !at("\n") && !at(";") && !at("}")) ++cursor;
            declared = joined(begin, cursor);
        }
        const auto value = at("=") ? cursor + 1 : cursor;
        auto end = line_end(cursor);
        // A constructor or a simple alias is enough for useful field completion.
        // Function calls, arbitrary expressions and generic substitution stay unresolved.
        if (declared.empty() && value < end && name_start(tokens[value].text.front())) {
            auto after = value + 1;
            while (after + 1 < end && tokens[after].text == "::") after += 2;
            if (after < end && tokens[after].text == "<") {
                const auto close = balanced(after, "<", ">");
                if (close < end) after = close + 1;
            }
            if (after < end && tokens[after].text == "{") declared = joined(value, after);
            else if (after == end) {
                for (auto it = out.symbols.rbegin(); it != out.symbols.rend(); ++it)
                    if (it->name == tokens[value].text && !it->member && it->scope == scope) {
                        declared = it->type;
                        break;
                    }
            }
        }
        // Skip balanced constructor braces so their fields never become declarations.
        int braces = 0;
        auto last = cursor;
        while (last < tokens.size()) {
            const auto& value_text = tokens[last].text;
            if (braces == 0 && (value_text == "\n" || value_text == ";" || value_text == "}")) break;
            if (value_text == "{") ++braces;
            if (value_text == "}") --braces;
            ++last;
        }
        const auto finish_at = last > cursor ? tokens[last - 1].end : name.end;
        const auto id = add(name, constant ? symbol_kind::Constant : symbol_kind::Variable,
                            scope, parent, declaration_start, finish_at);
        out.symbols[id].type = declared;
        out.symbols[id].visible_after = scope == 0 || out.scopes[scope].parent == 0
                                          ? 0 : finish_at;
        // Module scopes and function scopes can both be direct children of root;
        // use the outline owner to distinguish local declaration order.
        if (parent >= 0 && out.symbols[parent].kind != symbol_kind::Module)
            out.symbols[id].visible_after = finish_at;
        cursor = last;
    }
    void aggregate(int scope, int parent, std::size_t start) {
        const auto kind = tokens[cursor++].text;
        if (cursor >= tokens.size() || !name_start(tokens[cursor].text.front())) return;
        const auto name = tokens[cursor++];
        while (cursor < tokens.size() && !at("{") && !at("\n")) ++cursor;
        const auto id = add(name, kind == "struct" ? symbol_kind::Struct : kind == "enum" ? symbol_kind::Enum : symbol_kind::Class,
                            scope, parent, start, name.end);
        const auto body = child(scope, name.end);
        const bool braces = at("{");
        if (braces) ++cursor;
        block(body, id, name.text, braces ? "}" : kind, kind == "trait");
    }
    void implementation(int scope, int parent) {
        ++cursor;
        if (at("<")) {
            const auto close = balanced(cursor, "<", ">");
            cursor = close < tokens.size() ? close + 1 : close;
        }
        auto begin = cursor;
        while (cursor < tokens.size() && !at("{") && !at("\n")) {
            if (at("for")) begin = cursor + 1;
            ++cursor;
        }
        const auto owner = type_base(joined(begin, cursor));
        const auto body = child(scope, begin < tokens.size() ? tokens[begin].start : text.size());
        const bool braces = at("{");
        if (braces) ++cursor;
        block(body, parent, owner, braces ? "}" : "impl", false);
    }
    void control(int scope, int parent) {
        const auto keyword = tokens[cursor++];
        const auto begin = cursor;
        while (cursor < tokens.size() && !at("\n") && !at("{") && !at("then") && !at("do")) ++cursor;
        const bool braces = at("{");
        if (braces || at("then") || at("do")) ++cursor;
        const auto body = child(scope, keyword.end);
        if (keyword.text == "for" && begin < tokens.size() && name_start(tokens[begin].text.front())) {
            const auto id = add(tokens[begin], symbol_kind::Variable, body, parent,
                                tokens[begin].start, tokens[begin].end);
            out.symbols[id].visible_after = cursor < tokens.size() ? tokens[cursor].start : keyword.end;
        }
        block(body, -1, {}, braces ? "}" : keyword.text, false, parent);
    }
    void block(int scope, int parent, const std::string& owner, const std::string& ending,
               bool signatures, int outline_parent = -1) {
        if (outline_parent < 0) outline_parent = parent;
        bool intrinsic = false;
        while (cursor < tokens.size()) {
            const auto start = tokens[cursor].start;
            if (at("}") && ending == "}") {
                finish(scope, parent, tokens[cursor++].end);
                return;
            }
            if (at("end")) {
                const auto end = tokens[cursor++];
                const auto kind = cursor < tokens.size() ? tokens[cursor++].text : "";
                if (ending.empty()) { diagnostic(end, "Unexpected 'end " + kind + "'"); continue; }
                const auto normalized = [](const std::string& value) { return value == "function" ? "fn" : value; };
                if (normalized(kind) != normalized(ending))
                    diagnostic(end, "Mismatched block end: expected 'end " + ending + "' but found 'end " + kind + "'");
                finish(scope, parent, tokens[cursor - 1].end);
                return;
            }
            if ((at("else") || at("elseif")) && ending == "if") {
                const bool elseif = at("elseif");
                const auto boundary = tokens[cursor++].start;
                finish(scope, -1, boundary);
                scope = child(out.scopes[scope].parent, boundary);
                const bool else_if = at("if");
                if (else_if) ++cursor;
                if (elseif || else_if) {
                    while (cursor < tokens.size() && !at("then") && !at("\n")) ++cursor;
                    if (at("then")) ++cursor;
                }
                continue;
            }
            if ((at("fault") || at("recover")) && ending == "attempt") {
                const bool handler = at("fault");
                const auto boundary = tokens[cursor++].start;
                finish(scope, -1, boundary);
                scope = child(out.scopes[scope].parent, boundary);
                if (handler) {
                    const auto type_begin = cursor;
                    while (cursor < tokens.size() && !at("as") && !at("\n")) ++cursor;
                    const auto fault_type = joined(type_begin, cursor);
                    if (at("as") && cursor + 1 < tokens.size() && tokens[cursor + 1].text != "\n") {
                        const auto name = tokens[++cursor];
                        const auto id = add(name, symbol_kind::Variable, scope, outline_parent, name.start, name.end);
                        out.symbols[id].type = fault_type;
                        ++cursor;
                    }
                }
                // Only the first `as` declares the handler binding. Casts in
                // its guard are expressions, not extra variable declarations.
                while (cursor < tokens.size() && !at("\n")) ++cursor;
                continue;
            }
            if (at("module") && ending.empty()) {
                if (scope != 0) finish(scope, parent, start);
                ++cursor;
                const auto begin = cursor;
                while (cursor < tokens.size() && !at("\n") && !at(";")) ++cursor;
                if (begin == cursor) continue;
                auto module = tokens[begin];
                module.text = joined(begin, cursor);
                module.end = tokens[cursor - 1].end;
                out.module_name = module.text;
                parent = add(module, symbol_kind::Module, 0, -1, start, module.end);
                scope = child(0, start);
                out.scopes[scope].module = module.text;
                outline_parent = parent;
                continue;
            }
            if (at("@") && cursor + 1 < tokens.size()) {
                intrinsic = intrinsic || tokens[cursor + 1].text == "intrinsic";
                cursor += 2;
                if (at("(")) {
                    const auto close = balanced(cursor, "(", ")");
                    cursor = close < tokens.size() ? close + 1 : close;
                }
                continue;
            }
            while (at("pub") || at("private") || at("protected") || at("virtual") ||
                   at("override") || at("abstract") || at("extern") || at("static")) {
                ++cursor;
                if (at("(")) {
                    const auto close = balanced(cursor, "(", ")");
                    cursor = close < tokens.size() ? close + 1 : close;
                }
            }
            if (cursor == tokens.size()) break;
            if (at("fn") || at("function")) {
                function(scope, outline_parent, owner, signatures, intrinsic, start);
                intrinsic = false;
            }
            else if (at("struct") || at("class") || at("trait") || at("enum")) aggregate(scope, outline_parent, start);
            else if (at("impl")) implementation(scope, outline_parent);
            else if (at("let") || at("const")) variable(scope, outline_parent, start);
            else if (at("if") || at("while") || at("for") || at("loop") || at("match") || at("attempt")) control(scope, outline_parent);
            else if (!owner.empty() && name_start(tokens[cursor].text.front()) &&
                     cursor + 1 < tokens.size() && tokens[cursor + 1].text == ":") {
                const auto name = tokens[cursor];
                cursor += 2;
                const auto begin = cursor;
                int depth = 0;
                while (cursor < tokens.size()) {
                    if (depth == 0 && (at(",") || at("\n") || at("}"))) break;
                    if (at("<") || at("(")) ++depth;
                    if (at(">") || at(")")) --depth;
                    ++cursor;
                }
                const auto id = add(name, symbol_kind::Field, scope, outline_parent, start,
                                    cursor > begin ? tokens[cursor - 1].end : name.end, owner);
                out.symbols[id].type = joined(begin, cursor);
            } else if (at("{")) {
                ++cursor;
                const auto body = child(scope, start);
                block(body, -1, {}, "}", false, outline_parent);
            } else ++cursor;
        }
        finish(scope, parent, text.size());
        out.scopes[scope].open_at_end = true;
        if (!ending.empty()) {
            const auto at_start = out.scopes[scope].start;
            out.diagnostics.push_back({range(at_start, at_start), 1, "Unclosed block: '" + ending + "'"});
        }
    }

public:
    Indexer(const std::string& source, ParseResult& result)
        : text(source), out(result), tokens(lex(source, result)), lines(NyParser::compute_line_offsets(source)) {}
    void run() {
        out.scopes.push_back({-1, 0, text.size(), {}});
        block(0, -1, {}, {}, false);
    }
};

bool excluded(const ParseResult& parsed, std::size_t offset) {
    for (const auto& span : parsed.excluded)
        if (offset >= span.start && (offset < span.end || (offset == span.end && !span.closed))) return true;
    return false;
}

int scope_at(const ParseResult& parsed, std::size_t offset) {
    int result = 0;
    for (std::size_t i = 1; i < parsed.scopes.size(); ++i) {
        const auto& scope = parsed.scopes[i];
        if (offset >= scope.start && offset < scope.end) result = static_cast<int>(i);
    }
    // Incomplete documents remain useful at EOF, including an unclosed function.
    if (offset == parsed.scopes[0].end)
        for (std::size_t i = 1; i < parsed.scopes.size(); ++i)
            if (parsed.scopes[i].end == offset && parsed.scopes[i].open_at_end) result = static_cast<int>(i);
    return result;
}

std::vector<std::size_t> visible(const ParseResult& parsed, std::size_t offset) {
    std::vector<std::size_t> result;
    std::set<std::string> names;
    for (int scope = scope_at(parsed, offset); scope >= 0; scope = parsed.scopes[scope].parent) {
        for (std::size_t i = parsed.symbols.size(); i-- > 0;) {
            const auto& symbol = parsed.symbols[i];
            if (symbol.scope == scope && !symbol.member && symbol.visible_after <= offset && names.insert(key(parsed, symbol.name)).second)
                result.push_back(i);
        }
    }
    return result;
}

std::vector<std::size_t> matrix_members(const ParseResult& parsed, std::string type, const std::string& module) {
    std::set<std::string> visiting;
    std::function<std::map<std::string, std::size_t>(std::string)> collect = [&](std::string target) {
        std::map<std::string, std::size_t> result;
        const auto full_target = matrix_key(target);
        target = matrix_type_base(target);
        if (!visiting.insert(target).second) return result;
        const Symbol* declaration = nullptr;
        for (const auto& symbol : parsed.symbols)
            if (!symbol.member && symbol.module == module && matrix_key(symbol.name) == target &&
                (symbol.kind == symbol_kind::Struct || symbol.kind == symbol_kind::Class || symbol.kind == symbol_kind::Enum)) {
                declaration = &symbol;
                break;
            }
        if (declaration && !declaration->type.empty()) result = collect(declaration->type); // alias
        std::set<std::string> ambiguous;
        if (declaration) {
            for (const auto& base : declaration->base_types) {
                for (const auto& member : collect(base))
                    if (!result.emplace(member).second) ambiguous.insert(member.first);
            }
        }
        for (const auto& name : ambiguous) result.erase(name);
        // Locally declared extensions also work on primitive receivers.
        std::map<std::string, std::vector<std::size_t>> extensions;
        for (std::size_t i = 0; i < parsed.symbols.size(); ++i) {
            const auto& member = parsed.symbols[i];
            if (!member.member || member.module != module || matrix_type_base(member.owner_type) != target) continue;
            if (member.extension_member) {
                const auto owner = matrix_key(member.owner_type);
                if (owner.find(" of ") == std::string::npos || owner == full_target)
                    extensions[matrix_key(member.name)].push_back(i);
            } else result[matrix_key(member.name)] = i;
        }
        for (const auto& extension : extensions)
            if (extension.second.size() == 1) result.emplace(extension.first, extension.second.front());
        visiting.erase(target);
        return result;
    };
    std::vector<std::size_t> result;
    for (const auto& member : collect(std::move(type))) result.push_back(member.second);
    return result;
}

std::vector<std::size_t> matrix_candidates(const ParseResult& parsed, const std::string& text,
                                          std::size_t offset, std::size_t prefix_start,
                                          const std::vector<std::size_t>& available) {
    auto before = prefix_start;
    while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) --before;
    if (before > 0 && text[before - 1] == ':') return {};
    const bool dot = before > 0 && text[before - 1] == '.';
    if (dot) --before;
    while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) --before;
    auto end = before;
    before = matrix_name_span(text, before).first;
    auto receiver = text.substr(before, end - before);
    const bool my = matrix_key(receiver) == "my";
    if (!dot && !my) return available;
    std::vector<std::string> chain{receiver};
    while (dot && before > 0 && text[before - 1] == '.') {
        end = --before;
        before = matrix_name_span(text, before).first;
        chain.push_back(text.substr(before, end - before));
    }
    if (before > 0 && text[before - 1] == ':') return {};
    std::reverse(chain.begin(), chain.end());
    auto scope = scope_at(parsed, offset);
    const auto module = parsed.scopes[scope].module;
    std::string type;
    // 'my field' and 'my field.child' are receiver-first Matrix spellings.
    auto prior = before;
    while (prior > 0 && (text[prior - 1] == ' ' || text[prior - 1] == '\t')) --prior;
    const auto prior_end = prior;
    while (prior > 0 && ident(text[prior - 1], parsed)) --prior;
    const bool prefixed_my = matrix_key(text.substr(prior, prior_end - prior)) == "my";
    if (matrix_key(chain.front()) == "my" || prefixed_my) {
        while (scope >= 0 && parsed.scopes[scope].receiver_type.empty()) scope = parsed.scopes[scope].parent;
        if (scope < 0) return {};
        type = parsed.scopes[scope].receiver_type;
        if (!prefixed_my) chain.erase(chain.begin());
    } else {
        for (const auto index : available) {
            const auto& symbol = parsed.symbols[index];
            if (matrix_key(symbol.name) != matrix_key(chain.front())) continue;
            if (symbol.kind == symbol_kind::Variable || symbol.kind == symbol_kind::Constant) type = symbol.type;
            else if (symbol.kind == symbol_kind::Enum || symbol.kind == symbol_kind::Struct || symbol.kind == symbol_kind::Class) type = symbol.name;
            break;
        }
        chain.erase(chain.begin());
    }
    if (type.empty()) return {};
    for (const auto& field : chain) {
        std::string next_type;
        for (const auto index : matrix_members(parsed, type, module)) {
            const auto& symbol = parsed.symbols[index];
            if (matrix_key(symbol.name) == matrix_key(field) && symbol.kind == symbol_kind::Field) next_type = symbol.type;
        }
        if (next_type.empty()) return {};
        type = next_type;
    }
    return matrix_members(parsed, type, module);
}

std::vector<std::size_t> candidates(const ParseResult& parsed, const std::string& text,
                                   std::size_t offset, std::size_t prefix_start) {
    auto available = visible(parsed, offset);
    if (parsed.matrix) return matrix_candidates(parsed, text, offset, prefix_start, available);
    auto before = prefix_start;
    while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) --before;
    if (before == 0 || (text[before - 1] != '.' && text[before - 1] != ':')) return available;
    // Imported namespaces/workspace files require compiler resolution. Fail closed.
    if (text[before - 1] == ':') return {};
    auto receiver_end = --before;
    while (before > 0 && ident(text[before - 1])) --before;
    const auto receiver = text.substr(before, receiver_end - before);
    if (receiver.empty()) return {};
    if (before > 0 && (text[before - 1] == '.' || text[before - 1] == ':')) return {};
    const Symbol* variable = nullptr;
    for (const auto i : available)
        if (parsed.symbols[i].name == receiver) { variable = &parsed.symbols[i]; break; }
    if (!variable || variable->type.empty() ||
        (variable->kind != symbol_kind::Variable && variable->kind != symbol_kind::Constant)) return {};
    const auto type = type_base(variable->type);
    // Do not guess a same-spelled type in another module.
    bool known = false;
    for (const auto& symbol : parsed.symbols)
        if ((symbol.kind == symbol_kind::Struct || symbol.kind == symbol_kind::Class || symbol.kind == symbol_kind::Enum) &&
            symbol.name == type && symbol.module == variable->module) known = true;
    if (!known) return {};
    std::vector<std::size_t> result;
    std::set<std::string> names;
    for (std::size_t i = 0; i < parsed.symbols.size(); ++i) {
        const auto& symbol = parsed.symbols[i];
        if (symbol.member && symbol.owner_type == type && symbol.module == variable->module && names.insert(symbol.name).second)
            result.push_back(i);
    }
    return result;
}

} // namespace

std::vector<std::size_t> NyParser::compute_line_offsets(const std::string& text) {
    std::vector<std::size_t> offsets{0};
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') offsets.push_back(i + 1);
    return offsets;
}

std::optional<std::size_t> NyParser::offset_at(const std::string& text,
                                             const std::vector<std::size_t>& offsets, const Position& pos) {
    if (pos.line < 0 || pos.character < 0 || static_cast<std::size_t>(pos.line) >= offsets.size()) return std::nullopt;
    auto offset = offsets[static_cast<std::size_t>(pos.line)];
    int column = 0;
    while (offset < text.size() && text[offset] != '\n' && text[offset] != '\r' && column < pos.character) {
        const auto width = utf8_width(text, offset);
        column += width.second;
        offset += width.first;
    }
    if (column != pos.character) return std::nullopt;
    return offset;
}

Position NyParser::position_at(const std::string& text, const std::vector<std::size_t>& offsets, std::size_t offset) {
    offset = std::min(offset, text.size());
    const auto found = std::upper_bound(offsets.begin(), offsets.end(), offset);
    const auto line = found == offsets.begin() ? 0 : static_cast<std::size_t>(found - offsets.begin() - 1);
    int column = 0;
    for (auto at = offsets[line]; at < offset;) {
        const auto width = utf8_width(text, at);
        column += width.second;
        at += width.first;
    }
    return {static_cast<int>(line), column};
}

std::string NyParser::word_at(const std::string& text, const std::vector<std::size_t>& offsets, const Position& pos) {
    const auto offset = offset_at(text, offsets, pos);
    if (!offset) return {};
    auto begin = *offset;
    auto end = begin;
    while (begin > 0 && ident(text[begin - 1])) --begin;
    while (end < text.size() && ident(text[end])) ++end;
    return text.substr(begin, end - begin);
}

ParseResult NyParser::parse(const std::string& uri, const std::string& text, const std::string& language_id) const {
    auto lowered = uri.substr(0, uri.find_first_of("?#"));
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto suffix = lowered.rfind('.');
    if (language_id == "matrix" || (language_id.empty() && suffix != std::string::npos &&
        (lowered.substr(suffix) == ".matrix" || lowered.substr(suffix) == ".mat"))) return parse_matrix(text);
    ParseResult result;
    Indexer(text, result).run();
    const auto offsets = compute_line_offsets(text);
    for (const auto& span : result.excluded) {
        if (span.closed || span.start >= text.size()) continue;
        const bool block_comment = text.compare(span.start, 3, "###") == 0 ||
                                   text.compare(span.start, 2, "/*") == 0;
        const bool literal = text[span.start] == '"' || text[span.start] == '\'';
        if (block_comment || literal) {
            const auto start = position_at(text, offsets, span.start);
            result.diagnostics.push_back({{start, start}, 1,
                block_comment ? "Unclosed block comment" : "Unclosed string or character literal"});
        }
    }
    return result;
}

std::vector<Completion> NyParser::complete(const ParseResult& parsed, const std::string& text,
                                          const std::vector<std::size_t>& offsets, const Position& pos) {
    const auto offset = offset_at(text, offsets, pos);
    if (!offset || excluded(parsed, *offset)) return {};
    auto begin = *offset;
    auto end = begin;
    while (begin > 0 && ident(text[begin - 1], parsed)) --begin;
    while (end < text.size() && ident(text[end], parsed)) ++end;
    if (parsed.matrix) std::tie(begin, end) = matrix_name_span(text, *offset);
    const auto prefix = key(parsed, text.substr(begin, *offset - begin));
    std::vector<Completion> result;
    for (const auto index : candidates(parsed, text, *offset, begin))
        if (key(parsed, parsed.symbols[index].name).compare(0, prefix.size(), prefix) == 0)
            result.push_back({index, {position_at(text, offsets, begin), position_at(text, offsets, end)}});
    std::sort(result.begin(), result.end(), [&](const Completion& a, const Completion& b) {
        return parsed.symbols[a.symbol].name < parsed.symbols[b.symbol].name;
    });
    return result;
}

std::optional<std::size_t> NyParser::resolve(const ParseResult& parsed, const std::string& text,
                                           const std::vector<std::size_t>& offsets, const Position& pos) {
    const auto offset = offset_at(text, offsets, pos);
    if (!offset || excluded(parsed, *offset)) return std::nullopt;
    auto begin = *offset;
    auto end = begin;
    while (begin > 0 && ident(text[begin - 1], parsed)) --begin;
    while (end < text.size() && ident(text[end], parsed)) ++end;
    if (parsed.matrix) std::tie(begin, end) = matrix_name_span(text, *offset);
    const auto name = key(parsed, text.substr(begin, end - begin));
    for (const auto index : candidates(parsed, text, *offset, begin))
        if (key(parsed, parsed.symbols[index].name) == name) return index;
    return std::nullopt;
}

} // namespace ny::lsp

#include "matrix_parser.hpp"
#include "interpolated_text.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace ny::lsp {
namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
std::string lowercase(std::string value) {
    for (auto& c : value) c = lower(c);
    return value;
}
bool name_start(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return (lower(c) >= 'a' && lower(c) <= 'z') || c == '_' || byte >= 0x80;
}
bool name_part(char c) { return name_start(c) || (c >= '0' && c <= '9') || c == '-'; }
std::string normalized_line(const std::string& text) {
    std::string result;
    bool space = false;
    for (const auto c : text) {
        if (c == ' ' || c == '\t' || c == '\r') { space = !result.empty(); continue; }
        if (space) result += ' ';
        space = false;
        result += lower(c);
    }
    return result;
}
struct Token {
    std::string text;
    std::string word;
    std::size_t start;
    std::size_t end;
    bool name = false;
};

std::vector<Token> lex(const std::string& text, ParseResult& out) {
    std::vector<Token> tokens;
    const auto lines = NyParser::compute_line_offsets(text);
    auto diagnostic = [&](std::size_t start, std::size_t end, const std::string& message) {
        out.diagnostics.push_back({{NyParser::position_at(text, lines, start),
                                   NyParser::position_at(text, lines, end)}, 1, message});
    };
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        if (text.compare(i, 3, "!!!") == 0) {
            const auto end = text.find('\n', i);
            i = end == std::string::npos ? text.size() : end;
            out.excluded.push_back({start, i, false});
        } else if (text[i] == '"') {
            ++i;
            while (i < text.size() && text[i] != '"' && text[i] != '\n' && text[i] != '\r') ++i;
            const bool closed = i < text.size() && text[i] == '"';
            if (closed) ++i;
            out.excluded.push_back({start, i, closed});
            tokens.push_back({text.substr(start, i - start), "<text>", start, i});
            if (!closed) diagnostic(start, i, "Unclosed text literal; quoted Matrix text stays on one line");
        } else if (text[i] == '`') {
            ++i;
            while (i < text.size() && text[i] != '`' && text[i] != '\n' && text[i] != '\r') ++i;
            if (i < text.size() && text[i] == '`') ++i;
            tokens.push_back({text.substr(start, i - start), {}, start, i, true});
        } else if (text[i] == '\n' || text[i] == ';' ||
                   (text[i] == '.' && (i + 1 == text.size() || !name_start(text[i + 1])))) {
            tokens.push_back({text.substr(i, 1), "\n", i, i + 1});
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        } else if (name_start(text[i])) {
            while (i < text.size() && name_part(text[i])) ++i;
            const auto spelling = text.substr(start, i - start);
            const auto word = lowercase(spelling);
            auto quote = i;
            while (quote < text.size() && (text[quote] == ' ' || text[quote] == '\t')) ++quote;
            if (word == "formatted" && quote > i && quote < text.size() && text[quote] == '"') {
                i = editor_formatted_text(text, start, quote, true, out, lex);
                tokens.push_back({text.substr(start, i - start), "<text>", start, i});
                continue;
            }
            auto eol = text.find('\n', i);
            if (eol == std::string::npos) eol = text.size();
            if (word == "begin" && normalized_line(text.substr(i, eol - i)) == "text") {
                i = eol < text.size() ? eol + 1 : eol;
                bool closed = false;
                while (i < text.size()) {
                    auto end = text.find('\n', i);
                    if (end == std::string::npos) end = text.size();
                    closed = normalized_line(text.substr(i, end - i)) == "end text";
                    i = closed || end == text.size() ? end : end + 1;
                    if (closed) break;
                }
                out.excluded.push_back({start, i, closed});
                tokens.push_back({"begin text", "<text>", start, i});
                if (!closed) diagnostic(start, start + 5, "Unclosed text block; close it with a line containing 'end text'");
            } else tokens.push_back({spelling, word, start, i, true});
        } else if ((text[i] >= '0' && text[i] <= '9') ||
                   (text[i] == '-' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9')) {
            if (text[i] == '-') ++i;
            if (text.compare(i, 2, "0x") == 0 || text.compare(i, 2, "0X") == 0) {
                i += 2;
                while (i < text.size() && std::isxdigit(static_cast<unsigned char>(text[i]))) ++i;
            } else {
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                if (i + 1 < text.size() && text[i] == '.' && std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
                    ++i;
                    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
                if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                    ++i;
                    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
                    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
            }
            tokens.push_back({text.substr(start, i - start), "<number>", start, i});
        } else {
            ++i;
            if (text.compare(start, 2, "::") == 0) ++i;
            const auto value = text.substr(start, i - start);
            tokens.push_back({value, value, start, i});
        }
    }
    return tokens;
}

bool modifier(const std::string& word) {
    return word == "public" || word == "abstract" || word == "virtual" ||
           word == "replaces" || word == "forever";
}
bool declaration_clause(const std::string& word) {
    return word == "receives" || word == "returns" || word == "begin" || word == "of" ||
           word == "on" || word == "without" || word == "inherits" || word == "provides" || word == "means";
}
bool aggregate_kind(const std::string& word) {
    return word == "record" || word == "choice" || word == "class" || word == "contract" ||
           word == "implementation" || word == "extension";
}

class Indexer {
    const std::string& text;
    ParseResult& out;
    std::vector<Token> tokens;
    std::vector<std::size_t> lines;
    std::size_t cursor = 0;
    struct Inference { int symbol; std::size_t begin; std::size_t end; };
    std::vector<Inference> inference;

    bool at(const std::string& value, std::size_t ahead = 0) const {
        return cursor + ahead < tokens.size() && tokens[cursor + ahead].word == value;
    }
    Range range(std::size_t start, std::size_t end) const {
        return {NyParser::position_at(text, lines, start), NyParser::position_at(text, lines, end)};
    }
    std::string joined(std::size_t begin, std::size_t end) const {
        std::string result;
        for (auto i = begin; i < end; ++i) {
            if (tokens[i].word == "\n") continue;
            if (!result.empty() && tokens[i].word != "." && tokens[i].word != "::" &&
                tokens[i - 1].word != "." && tokens[i - 1].word != "::") result += ' ';
            result += tokens[i].text;
        }
        return result;
    }
    std::size_t statement_end(std::size_t begin) const {
        while (begin < tokens.size() && tokens[begin].word != "\n" &&
               tokens[begin].word != "end" && tokens[begin].word != "otherwise") ++begin;
        return begin;
    }
    std::size_t significant(std::size_t begin) const {
        while (begin < tokens.size() && tokens[begin].word == "\n" && tokens[begin].text == "\n") ++begin;
        return begin;
    }
    std::size_t header_end(std::size_t begin) const {
        for (auto i = begin; i < tokens.size(); ++i) {
            if (tokens[i].word == "end" || tokens[i].word == "otherwise") return i;
            if (tokens[i].word != "\n") continue;
            if (tokens[i].text != "\n") return i;
            const auto next = significant(i);
            if (next == tokens.size()) return i;
            const auto following = significant(next + 1);
            const auto previous = i > begin ? tokens[i - 1].word : "";
            if (declaration_clause(tokens[next].word) || tokens[next].word == "meeting" ||
                previous == "," || previous == "and" || previous == "as" ||
                (following < tokens.size() && tokens[following].word == "as")) continue;
            return i;
        }
        return tokens.size();
    }
    int child(int parent, std::size_t start, const std::string& receiver = {}) {
        out.scopes.push_back({parent, start, text.size(), out.scopes[parent].module, false, receiver});
        return static_cast<int>(out.scopes.size() - 1);
    }
    void finish(int scope, int symbol, std::size_t end) {
        out.scopes[scope].end = end;
        if (symbol >= 0) out.symbols[symbol].range.end = range(end, end).end;
    }
    int add(const Token& name, int kind, int scope, int parent, std::size_t start,
            std::size_t end, const std::string& owner = {}) {
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
    void parameters(std::size_t begin, std::size_t end, int scope, int parent) {
        while (begin < end) {
            if (tokens[begin].word == "on" || tokens[begin].word == "without") break;
            if (tokens[begin].word == "," || tokens[begin].word == "and" || tokens[begin].word == "\n") { ++begin; continue; }
            const bool variadic = tokens[begin].word == "more";
            if (variadic || tokens[begin].word == "variable") ++begin;
            if (begin >= end) break;
            const auto name = begin++;
            if (begin >= end || tokens[begin].word != "as") break;
            const auto type_begin = ++begin;
            while (begin < end) {
                const auto& word = tokens[begin].word;
                auto next = significant(begin + 1);
                if (next < end && tokens[next].word == "variable") next = significant(next + 1);
                const auto after = significant(next + 1);
                if (word == "on" || word == "without" || word == "defaulting" ||
                    ((word == "," || word == "and") && next < end && after < end && tokens[after].word == "as")) break;
                ++begin;
            }
            const auto id = add(tokens[name], symbol_kind::Variable, scope, parent,
                                tokens[name].start, begin > type_begin ? tokens[begin - 1].end : tokens[name].end);
            out.symbols[id].type = (variadic ? "array of " : "") + joined(type_begin, begin);
            if (begin < end && tokens[begin].word == "defaulting") {
                while (begin < end && tokens[begin].word != "," && tokens[begin].word != "and") ++begin;
            }
        }
    }
    bool declaration(int scope, int parent, const std::string& owner, const std::string& container) {
        const auto start = cursor;
        auto subject = cursor;
        while (subject < tokens.size() && modifier(tokens[subject].word)) ++subject;
        const auto first_clause = significant(subject + 1);
        if (first_clause >= tokens.size() || !tokens[subject].name || !declaration_clause(tokens[first_clause].word)) return false;
        const auto header_finish = header_end(subject);
        auto begin = subject + 1;
        while (begin < header_finish && tokens[begin].word != "begin") ++begin;
        const auto kind_at = significant(begin + 1);
        const auto kind = kind_at < header_finish ? tokens[kind_at].word : "";
        if (tokens[first_clause].word == "means") {
            cursor = header_finish;
            const auto id = add(tokens[subject], symbol_kind::Struct, scope, parent, tokens[start].start,
                                tokens[header_finish - 1].end);
            out.symbols[id].type = joined(first_clause + 1, header_finish);
            return true;
        }
        if (aggregate_kind(kind)) {
            const bool implementation = kind == "implementation" || kind == "extension";
            const auto name = tokens[subject];
            const auto id = add(name, implementation ? symbol_kind::Namespace : kind == "choice" ? symbol_kind::Enum :
                                kind == "record" ? symbol_kind::Struct : symbol_kind::Class,
                                scope, parent, tokens[start].start, tokens[kind_at].end);
            if (kind == "class") {
                auto base = subject + 1;
                while (base < begin && tokens[base].word != "inherits") ++base;
                if (base < begin) {
                    auto part = ++base;
                    for (; base <= begin; ++base) {
                        if (base == begin || tokens[base].word == ",") {
                            out.symbols[id].base_types.push_back(joined(part, base));
                            part = base + 1;
                        }
                    }
                }
            }
            auto target_end = begin;
            for (auto i = subject + 1; i < begin; ++i)
                if (tokens[i].word == "provides" || (tokens[i].word == "of" && i + 1 < begin && tokens[i + 1].word == "type")) { target_end = i; break; }
            const auto target = implementation ? joined(subject, target_end) : name.text;
            cursor = kind_at + 1;
            const auto body = child(scope, tokens[kind_at].end);
            block(body, id, target, kind);
            return true;
        }
        if (kind != "paragraph" && kind != "test" && !kind.empty()) return false;
        // Body-less declarations are valid in contracts/abstract classes and
        // for annotated intrinsics. Never treat an ordinary expression as one.
        auto receives = subject + 1;
        while (receives < begin && tokens[receives].word != "receives") ++receives;
        auto returns = subject + 1;
        while (returns < begin && tokens[returns].word != "returns") ++returns;
        if (kind.empty() && receives == begin && returns == begin) return false;
        const auto end = kind.empty() ? tokens[header_finish - 1].end : tokens[kind_at].end;
        const auto id = add(tokens[subject], owner.empty() ? symbol_kind::Function : symbol_kind::Method,
                            scope, parent, tokens[start].start, end, owner);
        if (returns < begin) out.symbols[id].type = joined(returns + 1, begin);
        out.symbols[id].extension_member = container == "extension";
        bool associated = false;
        for (auto i = subject + 1; i < begin; ++i) if (tokens[i].word == "without") associated = true;
        const auto body = child(scope, tokens[subject].end, associated ? "" : owner);
        if (receives < begin) parameters(receives + 1, std::min(returns, begin), body, id);
        cursor = kind.empty() ? header_finish : kind_at + 1;
        if (kind.empty()) finish(body, id, end);
        else block(body, id, {}, kind);
        return true;
    }
    void binding(int scope, int parent) {
        const auto start = cursor;
        const auto kind = tokens[cursor++].word;
        if (at("variable")) ++cursor; // own variable
        if (at("(")) {
            std::vector<Token> names;
            ++cursor;
            while (cursor < tokens.size() && tokens[cursor].name) {
                names.push_back(tokens[cursor++]);
                if (!at(",")) break;
                ++cursor;
            }
            if (!at(")")) return;
            ++cursor;
            if (!at("is")) return;
            ++cursor;
            skip_expression();
            const auto end = tokens[cursor - 1].end;
            for (const auto& name : names) {
                if (name.text == "_") continue;
                const auto id = add(name, kind == "constant" ? symbol_kind::Constant : symbol_kind::Variable,
                                    scope, parent, tokens[start].start, end);
                out.symbols[id].visible_after = end;
            }
            return;
        }
        if (cursor >= tokens.size() || !tokens[cursor].name) return;
        const auto name = tokens[cursor++];
        std::string type;
        if (at("as")) {
            const auto begin = ++cursor;
            while (cursor < tokens.size() && !at("is") && !at("sized") && !at("\n") && !at("end")) ++cursor;
            type = joined(begin, cursor);
        }
        if (at("is") || at("sized")) ++cursor;
        const auto expression = cursor;
        const auto id = add(name, kind == "constant" ? symbol_kind::Constant : symbol_kind::Variable,
                            scope, parent, tokens[start].start, name.end);
        out.symbols[id].type = type;
        if (at("anonymous")) {
            const auto head = cursor++;
            auto begin = cursor;
            while (begin < tokens.size() && tokens[begin].word != "begin" && tokens[begin].word != "\n") ++begin;
            if (begin + 1 < tokens.size() && tokens[begin + 1].word == "paragraph") {
                // Matrix paragraph values cannot capture enclosing locals.
                auto lexical_parent = scope;
                while (lexical_parent > 0 && out.scopes[lexical_parent].parent > 0)
                    lexical_parent = out.scopes[lexical_parent].parent;
                if (out.scopes[lexical_parent].module.empty()) lexical_parent = 0;
                const auto body = child(lexical_parent, tokens[head].end);
                auto returns = cursor;
                while (returns < begin && tokens[returns].word != "returns") ++returns;
                if (at("receives")) parameters(cursor + 1, returns, body, id);
                cursor = begin + 2;
                block(body, id, {}, "paragraph");
            } else skip_expression();
        } else if (at("consider") || at("when")) {
            control(scope, parent);
        } else skip_expression();
        const auto end = cursor > expression ? tokens[cursor - 1].end : name.end;
        out.symbols[id].range.end = range(end, end).end;
        out.symbols[id].detail = text.substr(tokens[start].start, end - tokens[start].start);
        if (parent >= 0 && out.symbols[parent].kind != symbol_kind::Module) out.symbols[id].visible_after = end;
        if (type.empty()) inference.push_back({id, expression, cursor});
    }
    void ownership_binding(int scope, int parent) {
        const auto keyword = tokens[cursor++];
        const auto end = statement_end(cursor);
        auto name = cursor;
        auto source = cursor;
        if (keyword.word == "borrow") {
            while (source < end && tokens[source].word != "from") ++source;
            ++source;
        } else {
            while (name < end && tokens[name].word != "into") ++name;
            ++name;
        }
        if (name < end && source < end && tokens[name].name) {
            const auto id = add(tokens[name], symbol_kind::Variable, scope, parent, keyword.start, tokens[end - 1].end);
            out.symbols[id].visible_after = tokens[end - 1].end;
            // Both names are useful even when the view's precise type cannot
            // be inferred. A move destination retains its source's type.
            if (keyword.word == "move") inference.push_back({id, source, source + 1});
        }
        cursor = end;
    }
    void skip_expression() {
        int nested = 0;
        while (cursor < tokens.size()) {
            if (nested == 0 && (at("\n") || at("end") || at("otherwise"))) break;
            if (at("when") || at("consider")) ++nested;
            if (at("end") && (at("when", 1) || at("consider", 1))) {
                if (nested == 0) break;
                --nested;
                cursor += 2;
            } else ++cursor;
        }
    }
    void control(int scope, int parent) {
        const auto keyword = tokens[cursor++];
        const auto body = child(scope, keyword.end);
        if (keyword.word == "repeat" && at("for")) {
            ++cursor;
            if (at("each")) ++cursor;
            if (cursor < tokens.size() && tokens[cursor].name) {
                const auto name = tokens[cursor++];
                const auto id = add(name, symbol_kind::Variable, body, parent, name.start, name.end);
                out.symbols[id].visible_after = name.end;
            }
        }
        if (keyword.word == "when") {
            while (cursor < tokens.size() && !at("then") && !at("give") && !at("\n")) ++cursor;
            if (at("then") || at("give")) ++cursor;
        } else if (keyword.word != "attempt") {
            while (cursor < tokens.size() && !at("\n") && !at("end")) ++cursor;
        }
        block(body, -1, {}, keyword.word, parent);
    }
    void block(int scope, int parent, const std::string& owner, const std::string& ending, int outline_parent = -1) {
        if (outline_parent < 0) outline_parent = parent;
        while (cursor < tokens.size()) {
            if (at("\n")) { ++cursor; continue; }
            const auto start = cursor;
            if (at("end") && cursor + 1 < tokens.size()) {
                if (ending.empty()) { cursor += 2; continue; }
                // Recover without swallowing a parent's end while a control
                // block is still being edited.
                if (!at(ending, 1)) {
                    finish(scope, parent, tokens[cursor].start);
                    return;
                }
                cursor += 2;
                finish(scope, parent, tokens[cursor - 1].end);
                return;
            }
            if ((at("otherwise") && ending == "when") ||
                ((at("when") || at("otherwise")) && ending == "consider") ||
                ((at("caught") || at("always")) && ending == "attempt")) {
                const auto arm = tokens[cursor++];
                const auto scope_parent = out.scopes[scope].parent;
                finish(scope, -1, arm.start);
                scope = child(scope_parent, arm.start);
                if (ending == "attempt" && arm.word == "caught") {
                    const auto type_begin = cursor;
                    while (cursor < tokens.size() && !at("as") && !at("\n")) ++cursor;
                    const auto type = joined(type_begin, cursor);
                    if (at("as") && cursor + 1 < tokens.size() && tokens[cursor + 1].name) {
                        const auto name = tokens[++cursor];
                        const auto id = add(name, symbol_kind::Variable, scope, outline_parent, name.start, name.end);
                        if (matrix_key(type) != "anything") out.symbols[id].type = type;
                        ++cursor;
                    }
                    while (cursor < tokens.size() && !at("\n")) ++cursor;
                } else if (ending == "consider" && arm.word == "when") {
                    const auto next = significant(cursor + 1);
                    if (cursor < tokens.size() && tokens[cursor].name && next < tokens.size() &&
                        (tokens[next].word == "then" || tokens[next].word == "give" || tokens[next].word == "provided") &&
                        tokens[cursor].word != "true" && tokens[cursor].word != "false") {
                        const auto name = tokens[cursor++];
                        add(name, symbol_kind::Variable, scope, outline_parent, name.start, name.end);
                    }
                    // Payload bindings belong to one arm, including its guard.
                    while (cursor < tokens.size() && !at("then") && !at("give") && !at("\n")) {
                        if (at("holding") && cursor + 1 < tokens.size() && tokens[cursor + 1].name) {
                            const auto name = tokens[++cursor];
                            add(name, symbol_kind::Variable, scope, outline_parent, name.start, name.end);
                        }
                        ++cursor;
                    }
                    if (at("then") || at("give")) ++cursor;
                } else if (at("when")) {
                    while (cursor < tokens.size() && !at("then") && !at("give") && !at("\n")) ++cursor;
                    if (at("then") || at("give")) ++cursor;
                }
                continue;
            }
            if ((at("book") || at("chapter") || at("library")) && ending.empty()) {
                if (scope != 0) finish(scope, parent, tokens[cursor].start);
                const auto keyword = tokens[cursor++];
                const auto begin = cursor;
                cursor = statement_end(cursor);
                if (begin == cursor) continue;
                auto name = tokens[begin];
                name.text = joined(begin, cursor);
                name.end = tokens[cursor - 1].end;
                out.module_name = name.text;
                parent = add(name, symbol_kind::Module, 0, -1, keyword.start, name.end);
                scope = child(0, keyword.start);
                out.scopes[scope].module = name.text;
                outline_parent = parent;
                continue;
            }
            auto modified = cursor;
            while (modified < tokens.size() && modifier(tokens[modified].word)) ++modified;
            if (modified < tokens.size() && (tokens[modified].word == "value" || tokens[modified].word == "variable" || tokens[modified].word == "constant")) {
                cursor = modified;
                binding(scope, outline_parent);
            } else if (at("allocate") || at("own")) binding(scope, outline_parent);
            else if (at("borrow") || at("move")) ownership_binding(scope, outline_parent);
            else if (at("field") && !owner.empty()) {
                ++cursor;
                if (cursor >= tokens.size() || !tokens[cursor].name) continue;
                const auto name = tokens[cursor++];
                if (at("as")) ++cursor;
                const auto begin = cursor;
                cursor = statement_end(cursor);
                const auto id = add(name, symbol_kind::Field, scope, outline_parent, tokens[start].start,
                                    cursor > begin ? tokens[cursor - 1].end : name.end, owner);
                out.symbols[id].type = joined(begin, cursor);
            } else if (ending == "choice" && at("when") && cursor + 1 < tokens.size()) {
                const auto name = tokens[++cursor];
                ++cursor;
                const auto id = add(name, symbol_kind::Constant, scope, outline_parent, tokens[start].start, name.end, owner);
                if (at("holding")) {
                    const auto begin = ++cursor;
                    cursor = statement_end(cursor);
                    out.symbols[id].type = joined(begin, cursor);
                }
            } else if (declaration(scope, outline_parent, owner, ending)) {
                // declaration consumed its header/body
            } else if (at("when") || at("repeat") || at("consider") || at("attempt")) control(scope, outline_parent);
            else {
                // Unknown/import/annotation statements are intentionally not
                // scanned as declarations. Their words may resemble names.
                skip_expression();
            }
            if (cursor == start) ++cursor;
        }
        finish(scope, parent, text.size());
        out.scopes[scope].open_at_end = true;
    }
    void infer() {
        for (const auto& pending : inference) {
            if (pending.begin >= pending.end) continue;
            auto& symbol = out.symbols[pending.symbol];
            const auto& first = tokens[pending.begin];
            auto stop = pending.begin + 1;
            if (first.word == "build") {
                while (stop < pending.end && tokens[stop].word != "with" && tokens[stop].word != "\n") ++stop;
                symbol.type = joined(pending.begin + 1, stop);
                continue;
            }
            if (stop == pending.end && (first.word == "<text>" || first.word == "quote" || first.word == "newline" || first.word == "tab")) symbol.type = "text";
            else if (stop == pending.end && (first.word == "true" || first.word == "false")) symbol.type = "boolean";
            else if (stop == pending.end && first.word == "<number>") {
                const auto magnitude = first.text.front() == '-' ? first.text.substr(1) : first.text;
                const bool hex = magnitude.compare(0, 2, "0x") == 0 || magnitude.compare(0, 2, "0X") == 0;
                symbol.type = !hex && magnitude.find_first_of(".eE") != std::string::npos ? "floating" : "number";
            }
            else if (first.name) {
                for (auto scope = symbol.scope; scope >= 0; scope = out.scopes[scope].parent) {
                    bool found = false;
                    for (std::size_t i = out.symbols.size(); i-- > 0;) {
                        const auto& target = out.symbols[i];
                        if (static_cast<int>(i) == pending.symbol || target.scope != scope || target.member ||
                            target.visible_after > first.start || matrix_key(target.name) != matrix_key(first.text)) continue;
                        if ((stop < pending.end && tokens[stop].word == "with" && target.kind == symbol_kind::Function) ||
                            (stop == pending.end && (target.kind == symbol_kind::Variable || target.kind == symbol_kind::Constant)))
                            symbol.type = target.type;
                        found = true;
                        break;
                    }
                    if (found) break;
                }
            }
        }
    }
public:
    Indexer(const std::string& source, ParseResult& result)
        : text(source), out(result), tokens(lex(source, result)), lines(NyParser::compute_line_offsets(source)) {}
    void run() {
        out.scopes.push_back({-1, 0, text.size(), {}});
        block(0, -1, {}, {});
        infer();
    }
};

} // namespace

std::string matrix_key(std::string spelling) {
    if (!spelling.empty() && spelling.front() == '`') spelling.erase(spelling.begin());
    if (!spelling.empty() && spelling.back() == '`') spelling.pop_back();
    std::string result;
    for (const auto c : spelling) if (c != '-' && c != '_') result += lower(c);
    return result;
}

std::pair<std::size_t, std::size_t> matrix_name_span(const std::string& text, std::size_t offset) {
    const auto line = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    auto start = line == std::string::npos ? 0 : line + 1;
    while (start < text.size() && start <= offset && text[start] != '\n') {
        if (text.compare(start, 3, "!!!") == 0) break;
        if (text[start] == '"') {
            ++start;
            while (start < text.size() && text[start] != '"' && text[start] != '\n') ++start;
            if (start < text.size() && text[start] == '"') ++start;
            continue;
        }
        if (text[start] != '`') { ++start; continue; }
        auto end = start + 1;
        while (end < text.size() && text[end] != '`' && text[end] != '\n' && text[end] != '\r') ++end;
        if (end < text.size() && text[end] == '`') ++end;
        if (offset >= start && offset <= end) return {start, end};
        start = end;
    }
    auto begin = offset, end = offset;
    while (begin > 0 && name_part(text[begin - 1])) --begin;
    while (end < text.size() && name_part(text[end])) ++end;
    return {begin, end};
}

ParseResult parse_matrix(const std::string& text) {
    ParseResult result;
    result.matrix = true;
    Indexer(text, result).run();
    return result;
}

std::vector<MatrixKeyword> matrix_keywords(const ParseResult& parsed, const std::string& text,
                                          const std::vector<std::size_t>& offsets, Position position) {
    const auto at = NyParser::offset_at(text, offsets, position);
    if (!at) return {};
    for (const auto& span : parsed.excluded)
        if (*at >= span.start && (*at < span.end || (*at == span.end && !span.closed))) return {};
    const auto [begin, end] = matrix_name_span(text, *at);
    if (begin < text.size() && text[begin] == '`') return {};
    if (begin > 0 && (text[begin - 1] == '.' || text[begin - 1] == ':' || text[begin - 1] == '`')) return {};
    ParseResult context;
    const auto before = lex(text.substr(offsets[position.line], begin - offsets[position.line]), context);
    const auto last = before.empty() ? "" : before.back().word;
    if (last == "my") return {};
    const auto prefix = lowercase(text.substr(begin, *at - begin));
    // These are language spellings, not speculative imported library symbols.
    std::vector<std::string> words;
    int kind = 14;
    if (last == "as" || last == "returns" || last == "becomes" || last == "means" ||
        last == "of" || last == "holding" || last == "to") {
        words = {"number", "floating", "boolean", "text", "nothing", "override", "array of", "tuple of", "pointer to", "reference to", "owned array of", "shared view of", "exclusive view of"};
        kind = 25; // TypeParameter: language type vocabulary
    } else if (last == "override") words = {"integer", "long", "byte", "double"};
    else if (last == "begin") words = {"paragraph", "record", "class", "choice", "contract", "implementation", "extension", "test", "text"};
    else if (last == "end") words = {"paragraph", "record", "class", "choice", "contract", "implementation", "extension", "test", "when", "repeat", "consider", "attempt"};
    else if (last == "repeat") words = {"while", "for each", "for", "forever"};
    else if (last == "bitwise") words = {"and", "or", "xor", "not"};
    else if (last == "shifted") words = {"left by", "right by"};
    else if (last == "the") words = {"length of", "paragraph"};
    else if (last == "with") words = {"nothing", "more"};
    else if (last == "value" || last == "variable" || last == "constant" || last == "field" || last == "book") return {};
    else if (last.empty() || last == "\n") words = {"value", "variable", "constant", "set", "say", "give", "evaluate", "when", "otherwise", "repeat", "consider", "attempt", "caught", "always", "raise", "append", "allocate", "release", "own", "borrow", "move", "stop repeating", "continue repeating", "book", "library", "read", "cite", "public", "end"};
    else words = {"true", "false", "nothing", "build", "my", "the length of", "the paragraph", "anonymous", "formatted", "quote", "newline", "tab", "negative", "plus", "minus", "times", "over", "remainder", "equals", "does not equal", "is less than", "is greater than", "is at least", "is at most", "and", "or", "not", "bitwise", "shifted", "becomes", "with", "receives", "returns", "begin"};
    std::vector<MatrixKeyword> result;
    const Range edit{NyParser::position_at(text, offsets, begin), NyParser::position_at(text, offsets, end)};
    for (const auto& word : words)
        if (word.compare(0, prefix.size(), prefix) == 0) result.push_back({word, kind, edit});
    return result;
}

} // namespace ny::lsp

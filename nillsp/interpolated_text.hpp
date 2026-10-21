#ifndef NIL_LSP_INTERPOLATED_TEXT_HPP
#define NIL_LSP_INTERPOLATED_TEXT_HPP

#include "ny_parser.hpp"
#include "../nilcompiler/src/formatted_text.hpp"

namespace ny::lsp {

// Keep the whole value a single structural token. Only the literal portions
// are excluded from lexical completion; nested strings/comments inside holes
// retain their ordinary exclusions. Semantic resolution remains the compiler's.
template <typename ScanExpression>
std::size_t editor_formatted_text(const std::string& text, std::size_t start,
                                  std::size_t quote, bool matrix,
                                  ParseResult& out, ScanExpression scan_expression) {
    const auto scanned = ny::scan_formatted_text(text, quote, matrix);
    if (!scanned.error.empty()) {
        // Tolerant editing must not invent expression ranges for malformed
        // delimiters. The compiler supplies the precise syntax diagnostic.
        out.excluded.push_back({start, scanned.end, false});
        return scanned.end;
    }
    auto literal = start;
    for (const auto& piece : scanned.pieces) {
        if (!piece.expression) continue;
        out.excluded.push_back({literal, piece.start, true});
        ParseResult nested;
        scan_expression(text.substr(piece.start, piece.length), nested);
        for (auto span : nested.excluded) {
            span.start += piece.start;
            span.end += piece.start;
            out.excluded.push_back(span);
        }
        // A cursor immediately before '}' still belongs to the expression.
        literal = piece.start + piece.length + 1;
    }
    out.excluded.push_back({literal, scanned.end, true});
    return scanned.end;
}

} // namespace ny::lsp
#endif

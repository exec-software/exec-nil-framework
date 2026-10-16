#pragma once

#include "ny_parser.hpp"

#include <utility>

namespace ny::lsp {

// A tolerant, document-local index. The compiler remains the authority on
// legality and cross-book name/type resolution.
ParseResult parse_matrix(const std::string& text);
std::string matrix_key(std::string spelling);
std::pair<std::size_t, std::size_t> matrix_name_span(const std::string& text, std::size_t offset);

struct MatrixKeyword {
    std::string label;
    int kind = 14; // CompletionItemKind.Keyword
    Range edit;
};

std::vector<MatrixKeyword> matrix_keywords(const ParseResult& parsed, const std::string& text,
                                          const std::vector<std::size_t>& offsets, Position position);

}

#ifndef NIL_MATRIX_SUBMISSION_REQUEST_HPP
#define NIL_MATRIX_SUBMISSION_REQUEST_HPP

#include "ast.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace matrix {

struct decoded_submission_request {
  std::shared_ptr<ny::ast_block_stmt> body;
  ny::ast_expr_ptr value; // null for a void submission
};

struct submission_request_error : std::runtime_error {
  std::string path;
  submission_request_error(const std::string &location, const std::string &message)
      : std::runtime_error(location + ": " + message), path(location) {}
};

// Decode nil.matrix.submission-request.v1, carried by a versioned NILData
// document, directly to the shared AST. This grants no authority and performs
// no execution. Ordinary type checking and matrix.pure.v1 admission remain
// mandatory. Schema errors throw submission_request_error with a request path.
//
// Limits: 64 KiB input, 4096 constructed AST nodes and expression depth 32.
// NILData's own document nesting limit also applies. Names are one unescaped
// Matrix identifier, with ordinary Matrix case/separator-folded lookup. A node's
// source_file is its request path; line 0 denotes the absence of source lines.
decoded_submission_request decode_submission_request(
    const std::string &source, const std::vector<std::string> &existing_names = {});

} // namespace matrix
#endif

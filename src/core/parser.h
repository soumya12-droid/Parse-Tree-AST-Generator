#pragma once

#include <memory>
#include <vector>

#include "core/diagnostic.h"
#include "core/parse_tree.h"
#include "core/token.h"

// Maximum depth of nested parentheses accepted. Recursive descent uses the
// call stack, so unbounded nesting would let hostile input crash the program;
// deeper input is reported as a syntax error instead.
constexpr int kMaxNestingDepth = 256;

struct ParseResult {
    // Non-null only when parsing succeeded. A failed parse never yields a tree.
    std::unique_ptr<ParseNode> tree;
    // At most one entry: parsing stops at the first syntax error.
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return tree != nullptr && diagnostics.empty(); }
};

// Recursive-descent parser for the grammar in the README. `tokens` must be
// the complete stream from a successful lex() (ending with exactly one END).
//
// On a syntax error, the diagnostic has Phase::Syntax, the position and text
// of the offending token (found is empty for end of input), and `expected`
// lists the token types the parser was looking for at that point. For a
// missing ')' or ';' (the offending token cannot continue an expression) the
// list names just that delimiter. When an operand-like token (identifier,
// number, '(') directly follows an operand, a forgotten operator is at least as
// likely, so the list is the four operators plus the delimiter.
ParseResult parse(const std::vector<Token>& tokens);

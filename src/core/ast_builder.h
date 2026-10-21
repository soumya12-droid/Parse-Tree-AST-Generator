#pragma once

#include <cstddef>
#include <memory>

#include "core/ast.h"
#include "core/parse_tree.h"

// IMPLEMENTATION SAFETY LIMIT - not a rule of the language.
//
// The grammar places no bound on how many operands an expression has;
// `a = 1 + 1 + ... + 1;` is valid for any length. This constant only caps how
// deep a tree this implementation is willing to build, because the AST,
// the printers and later consumers (JSON writer, tree layout) walk it
// recursively. Inputs over the limit are reported as a controlled diagnostic,
// never as a wrong or partial tree.
//
// What is counted: the height of a node is 1 + the greatest height of its
// children, and a leaf has height 1. The ROOT COUNTS: Program -> Assignment ->
// Identifier is height 3. So in `r = x1 op x2 op ... op xn;` the AST height
// is n + 2 (n-1 nested operations, Assignment, Program), and the longest
// accepted statement has n = kMaxAstHeight - 2 = 998 operands (997 operators).
// Statements do not add to each other: Program's height is 1 + the tallest
// statement. Parentheses add no nodes.
//
// What the limit does and does not guarantee: it bounds the recursion depth
// of AstNode destruction and formatAst() to at most kMaxAstHeight frames. It
// does NOT by itself prove those operations are stack-safe: that depends on
// frame size, optimisation level and the thread's stack size. Measured on
// Windows x64 with clang++ 22 and a 1 MB stack, this implementation's AST
// destruction and formatAst() survived 1500 levels at -O0 (10000 and 3000 at
// -O1), i.e. a margin of only about 1.5x at -O0. Consumers running on
// smaller stacks must not assume 1000 levels is safe.
constexpr int kMaxAstHeight = 1000;

struct AstBuildResult {
    std::unique_ptr<AstNode> ast;  // null if tooDeep
    bool tooDeep = false;
    // Index of the first token of the tallest subtree at the point the limit
    // was exceeded: the start of the operand chain, or of the statement when
    // the statement as a whole is what pushed Program over the limit.
    std::size_t tooDeepAt = 0;
};

// Transforms the concrete parse tree produced by parse() into an AST:
//   - expression / term nodes with one operand collapse to that operand;
//   - `a - b - c` (flat in the parse tree) folds left-to-right into
//     Subtract(Subtract(a, b), c), so left associativity is explicit;
//   - parentheses produce no node, they only shape the tree;
//   - `statement` becomes Assignment(Identifier, value); `program` becomes Program.
//
// `root` must be a well-formed tree from parse(). Throws std::invalid_argument
// if it is not (a programming error, not a user input error).
AstBuildResult buildAst(const ParseNode& root);

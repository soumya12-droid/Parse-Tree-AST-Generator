#pragma once

#include <string>
#include <vector>

#include "core/ast.h"
#include "core/diagnostic.h"
#include "core/parse_tree.h"
#include "core/token.h"

// "1:5 ID(b)" - position, token type, lexeme. END is printed as "1:15 END".
std::string formatToken(const Token& token);

// One formatToken() line per token, each terminated by '\n'.
std::string formatTokens(const std::vector<Token>& tokens);

// "Lexical error at line 1, column 7: Invalid character '$'"
// When the diagnostic has expected token types, a second line follows:
//   "  Expected: identifier, number, or '('"
std::string formatDiagnostic(const Diagnostic& diagnostic);

// One formatDiagnostic() entry per diagnostic, each terminated by '\n'.
std::string formatDiagnostics(const std::vector<Diagnostic>& diagnostics);

// ASCII tree, one node per line, e.g.
//   program
//   `-- statement
//       |-- ID(a)
// Nonterminals print as their grammar name, terminals as TYPE(lexeme).
std::string formatParseTree(const ParseNode& root);

// Same drawing style for the AST, e.g.
//   Program
//   `-- Assignment
//       |-- Identifier(a)
//       `-- Add
// Identifier and Number print their value; operations print their kind.
std::string formatAst(const AstNode& root);

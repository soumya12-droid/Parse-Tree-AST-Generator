#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/ast.h"
#include "core/diagnostic.h"
#include "core/parse_tree.h"
#include "core/token.h"

// Result of running the whole front end: lex -> parse -> build AST.
//
// Invariant: when ok() is false, `parseTree` and `ast` are both null, so an
// invalid input can never be presented as a successful parse.
struct AnalysisResult {
    // The complete token stream (ending with END) when `tokenized` is true;
    // otherwise only the tokens recognised before lexing failed.
    std::vector<Token> tokens;
    bool tokenized = false;

    // Lexical errors, or the single syntax error, or an AST-limit error.
    std::vector<Diagnostic> diagnostics;

    std::unique_ptr<ParseNode> parseTree;
    std::unique_ptr<AstNode> ast;

    bool ok() const { return diagnostics.empty() && parseTree != nullptr && ast != nullptr; }
};

AnalysisResult analyze(const std::string& source);

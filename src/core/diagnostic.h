#pragma once

#include <string>
#include <vector>

#include "core/token.h"

// An error found while processing source text. Shared by all compiler phases;
// only the lexical phase produces diagnostics so far.
enum class Phase {
    Lexical,
    Syntax
};

struct Diagnostic {
    Phase phase;
    std::string message;
    int line;            // 1-based
    int column;          // 1-based
    std::string found;   // offending character or lexeme, exactly as in the source

    // Token types that would have been valid at this position. Empty for
    // lexical errors; filled in by the parser for syntax errors. Kept last
    // with a default so existing five-field initializers still compile.
    std::vector<TokenType> expected = {};

    // True when the input is rejected only because of an implementation safety
    // limit (parenthesis nesting depth, AST height), not because it violates
    // the grammar. Lets front ends tell "your program is wrong" from "this
    // tool will not process something this deep". Defaulted for the same reason.
    bool implementationLimit = false;
};

inline const char* phaseName(Phase phase) {
    switch (phase) {
        case Phase::Lexical: return "Lexical";
        case Phase::Syntax:  return "Syntax";
    }
    return "Unknown";
}

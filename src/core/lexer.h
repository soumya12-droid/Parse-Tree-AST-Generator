#pragma once

#include <string>
#include <vector>

#include "core/diagnostic.h"
#include "core/token.h"

struct LexResult {
    // Tokens recognised so far. When ok() is true this is the complete stream
    // and ends with exactly one END token. When ok() is false the stream is
    // incomplete (no END token) and must not be passed to later phases.
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return diagnostics.empty(); }
};

// Scans `source` left to right. Never throws and never discards characters
// silently: every character is either part of a token, whitespace, or reported
// as a diagnostic with its line and column.
LexResult lex(const std::string& source);

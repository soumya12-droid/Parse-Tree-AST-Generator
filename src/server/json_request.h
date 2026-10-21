#pragma once

#include <string>

// Parsing of the one request body the server accepts:
//
//     { "source": "<program text>" }
//
// The body must be one complete, strictly valid JSON document (RFC 8259) whose
// top level is an object with exactly one "source" member holding a string.
// Other members are allowed and ignored. This is a validator-parser, not a
// general JSON library; it never evaluates anything and its nesting depth and
// work are bounded by the body size.
struct AnalyzeRequest {
    bool ok = false;
    std::string source;  // decoded UTF-8 text (\uXXXX escapes resolved)
    std::string error;   // short reason when !ok; contains no request content
};

AnalyzeRequest parseAnalyzeRequest(const std::string& body);

// Maximum nesting of arrays/objects accepted anywhere in the body.
constexpr int kMaxJsonDepth = 32;

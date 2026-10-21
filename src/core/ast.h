#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Kinds of node in the Abstract Syntax Tree. Only operations, identifiers and
// literals survive; grammar symbols (expression/term/factor) and punctuation
// ('=', ';', parentheses) are dropped.
enum class AstKind {
    Program,     // children: zero or more Assignment
    Assignment,  // children: [Identifier target, value expression]
    Add,         // children: [left, right]
    Subtract,    // children: [left, right]
    Multiply,    // children: [left, right]
    Divide,      // children: [left, right]
    Identifier,  // leaf; value = name
    Number       // leaf; value = literal text exactly as written
};

inline const char* astKindName(AstKind kind) {
    switch (kind) {
        case AstKind::Program:    return "Program";
        case AstKind::Assignment: return "Assignment";
        case AstKind::Add:        return "Add";
        case AstKind::Subtract:   return "Subtract";
        case AstKind::Multiply:   return "Multiply";
        case AstKind::Divide:     return "Divide";
        case AstKind::Identifier: return "Identifier";
        case AstKind::Number:     return "Number";
    }
    return "Unknown";
}

struct AstNode {
    AstKind kind = AstKind::Program;
    std::string value;  // Identifier name or Number literal; empty otherwise
    std::vector<std::unique_ptr<AstNode>> children;

    // Half-open range [firstToken, endToken) of indices into the token vector,
    // the same convention as ParseNode. A binary operation covers both operands
    // including any parentheses around them; a parenthesised operand's own
    // node covers only what is inside the parentheses. An Assignment includes
    // its ';'. The END token is never covered.
    std::size_t firstToken = 0;
    std::size_t endToken = 0;
};

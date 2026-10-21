#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/token.h"

// Grammar symbols that appear as nodes of the concrete parse tree. Leaves are
// always Terminal nodes; every token the parser consumes (including
// punctuation such as '=', ';' and parentheses) is kept as one.
enum class ParseSymbol {
    Program,
    Statement,
    Expression,
    Term,
    Factor,
    Terminal
};

inline const char* parseSymbolName(ParseSymbol symbol) {
    switch (symbol) {
        case ParseSymbol::Program:    return "program";
        case ParseSymbol::Statement:  return "statement";
        case ParseSymbol::Expression: return "expression";
        case ParseSymbol::Term:       return "term";
        case ParseSymbol::Factor:     return "factor";
        case ParseSymbol::Terminal:   return "terminal";
    }
    return "unknown";
}

// One node per grammar production, mirroring the EBNF directly:
//   program    -> statement*                       children: statements
//   statement  -> ID '=' expression ';'            children: ID, '=', expression, ';'
//   expression -> term (('+'|'-') term)*           children: term, then (op, term) pairs, flat
//   term       -> factor (('*'|'/') factor)*       children: factor, then (op, factor) pairs, flat
//   factor     -> NUMBER | ID | '(' expression ')' children: one terminal, or '(' expr ')'
// Repetition is kept flat: `b - c - d` is one expression node with five
// children. Left associativity is a property of the AST built later.
struct ParseNode {
    ParseSymbol symbol = ParseSymbol::Program;
    Token token{};  // meaningful only when symbol == Terminal
    std::vector<std::unique_ptr<ParseNode>> children;

    // Half-open range [firstToken, endToken) of indices into the token vector
    // that this node covers. The END token is never part of any node.
    std::size_t firstToken = 0;
    std::size_t endToken = 0;
};

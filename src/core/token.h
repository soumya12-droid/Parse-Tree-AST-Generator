#pragma once

#include <string>

// Token kinds for the supported grammar (see README / docs for the grammar).
enum class TokenType {
    ID,
    NUMBER,
    ASSIGN,     // =
    PLUS,       // +
    MINUS,      // -
    MUL,        // *
    DIV,        // /
    LPAREN,     // (
    RPAREN,     // )
    SEMICOLON,  // ;
    END         // end of input
};

struct Token {
    TokenType type;
    std::string lexeme;  // exact source text; empty for END
    int line;            // 1-based
    int column;          // 1-based
};

inline const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::ID:        return "ID";
        case TokenType::NUMBER:    return "NUMBER";
        case TokenType::ASSIGN:    return "ASSIGN";
        case TokenType::PLUS:      return "PLUS";
        case TokenType::MINUS:     return "MINUS";
        case TokenType::MUL:       return "MUL";
        case TokenType::DIV:       return "DIV";
        case TokenType::LPAREN:    return "LPAREN";
        case TokenType::RPAREN:    return "RPAREN";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::END:       return "END";
    }
    return "UNKNOWN";
}

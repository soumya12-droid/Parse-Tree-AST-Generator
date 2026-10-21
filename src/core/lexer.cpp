#include "core/lexer.h"

#include <cstddef>

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isIdStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool isIdPart(char c) { return isIdStart(c) || isDigit(c); }

bool isHorizontalSpace(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

// Length in bytes of the character starting at source[i]. A well-formed UTF-8
// sequence counts as one character so that it is reported once, with one column.
std::size_t characterLength(const std::string& source, std::size_t i) {
    const unsigned char lead = static_cast<unsigned char>(source[i]);
    std::size_t length = 1;
    if (lead >= 0xF0 && lead <= 0xF7) length = 4;
    else if (lead >= 0xE0 && lead <= 0xEF) length = 3;
    else if (lead >= 0xC2 && lead <= 0xDF) length = 2;

    if (i + length > source.size()) return 1;
    for (std::size_t k = 1; k < length; ++k) {
        const unsigned char next = static_cast<unsigned char>(source[i + k]);
        if (next < 0x80 || next > 0xBF) return 1;
    }
    return length;
}

// Human-readable form of a character for messages: 'x', or '\x00' for
// unprintable single bytes.
std::string describeCharacter(const std::string& text) {
    if (text.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(text[0]);
        if (c < 0x20 || c >= 0x7F) {
            static const char* hex = "0123456789ABCDEF";
            std::string out = "'\\x";
            out += hex[c >> 4];
            out += hex[c & 0x0F];
            out += '\'';
            return out;
        }
    }
    return "'" + text + "'";
}

bool singleCharTokenType(char c, TokenType& type) {
    switch (c) {
        case '=': type = TokenType::ASSIGN;    return true;
        case '+': type = TokenType::PLUS;      return true;
        case '-': type = TokenType::MINUS;     return true;
        case '*': type = TokenType::MUL;       return true;
        case '/': type = TokenType::DIV;       return true;
        case '(': type = TokenType::LPAREN;    return true;
        case ')': type = TokenType::RPAREN;    return true;
        case ';': type = TokenType::SEMICOLON; return true;
        default:  return false;
    }
}

}  // namespace

LexResult lex(const std::string& source) {
    LexResult result;
    const std::size_t n = source.size();
    std::size_t i = 0;
    int line = 1;
    int column = 1;

    while (i < n) {
        const char c = source[i];

        // Line breaks: "\r\n" counts as one break; a lone '\r' or '\n' also does.
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < n && source[i + 1] == '\n') ++i;
            ++i;
            ++line;
            column = 1;
            continue;
        }

        if (isHorizontalSpace(c)) {
            ++i;
            ++column;
            continue;
        }

        if (isIdStart(c)) {
            const std::size_t start = i;
            while (i < n && isIdPart(source[i])) ++i;
            result.tokens.push_back({TokenType::ID, source.substr(start, i - start), line, column});
            column += static_cast<int>(i - start);
            continue;
        }

        if (isDigit(c)) {
            const std::size_t start = i;
            while (i < n && isDigit(source[i])) ++i;
            result.tokens.push_back({TokenType::NUMBER, source.substr(start, i - start), line, column});
            column += static_cast<int>(i - start);
            continue;
        }

        TokenType type;
        if (singleCharTokenType(c, type)) {
            result.tokens.push_back({type, std::string(1, c), line, column});
            ++i;
            ++column;
            continue;
        }

        // Anything else is an invalid character: report it and keep scanning
        // so that all lexical errors in the input are found in one pass.
        const std::size_t length = characterLength(source, i);
        const std::string found = source.substr(i, length);
        std::string message = "Invalid character " + describeCharacter(found);
        if (c == '.' && i > 0 && isDigit(source[i - 1])) {
            message += " (decimal numbers are not supported)";
        }
        result.diagnostics.push_back({Phase::Lexical, message, line, column, found});
        i += length;
        ++column;
    }

    if (result.ok()) {
        result.tokens.push_back({TokenType::END, "", line, column});
    }
    return result;
}

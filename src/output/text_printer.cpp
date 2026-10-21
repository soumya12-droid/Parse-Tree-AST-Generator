#include "output/text_printer.h"

namespace {

// "ID(a)", "PLUS(+)"; END has no lexeme and prints as "END".
std::string tokenLabel(const Token& token) {
    std::string out = tokenTypeName(token.type);
    if (token.type != TokenType::END) {
        out += "(" + token.lexeme + ")";
    }
    return out;
}

// How a token type reads in an "Expected: ..." list.
std::string describeTokenType(TokenType type) {
    switch (type) {
        case TokenType::ID:        return "identifier";
        case TokenType::NUMBER:    return "number";
        case TokenType::ASSIGN:    return "'='";
        case TokenType::PLUS:      return "'+'";
        case TokenType::MINUS:     return "'-'";
        case TokenType::MUL:       return "'*'";
        case TokenType::DIV:       return "'/'";
        case TokenType::LPAREN:    return "'('";
        case TokenType::RPAREN:    return "')'";
        case TokenType::SEMICOLON: return "';'";
        case TokenType::END:       return "end of input";
    }
    return "unknown";
}

// "a", "a or b", "a, b, or c".
std::string describeExpected(const std::vector<TokenType>& expected) {
    std::string out;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (i > 0) {
            if (expected.size() == 2) out += " or ";
            else if (i + 1 == expected.size()) out += ", or ";
            else out += ", ";
        }
        out += describeTokenType(expected[i]);
    }
    return out;
}

// Draws the descendants of `node`, one per line, using `label(child)` for text.
template <typename Node, typename LabelFn>
void appendTree(const Node& node, const std::string& prefix, std::string& out, LabelFn label) {
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const Node& child = *node.children[i];
        const bool last = i + 1 == node.children.size();
        out += prefix + (last ? "`-- " : "|-- ") + label(child) + "\n";
        appendTree(child, prefix + (last ? "    " : "|   "), out, label);
    }
}

std::string parseNodeLabel(const ParseNode& node) {
    return node.symbol == ParseSymbol::Terminal ? tokenLabel(node.token)
                                                : parseSymbolName(node.symbol);
}

// "Identifier(a)", "Number(5)", "Add".
std::string astNodeLabel(const AstNode& node) {
    std::string out = astKindName(node.kind);
    if (node.kind == AstKind::Identifier || node.kind == AstKind::Number) {
        out += "(" + node.value + ")";
    }
    return out;
}

}  // namespace

std::string formatToken(const Token& token) {
    return std::to_string(token.line) + ":" + std::to_string(token.column) + " " +
           tokenLabel(token);
}

std::string formatTokens(const std::vector<Token>& tokens) {
    std::string out;
    for (const Token& token : tokens) {
        out += formatToken(token) + "\n";
    }
    return out;
}

std::string formatDiagnostic(const Diagnostic& diagnostic) {
    std::string out = std::string(phaseName(diagnostic.phase)) + " error at line " +
                      std::to_string(diagnostic.line) + ", column " +
                      std::to_string(diagnostic.column) + ": " + diagnostic.message;
    if (!diagnostic.expected.empty()) {
        out += "\n  Expected: " + describeExpected(diagnostic.expected);
    }
    return out;
}

std::string formatDiagnostics(const std::vector<Diagnostic>& diagnostics) {
    std::string out;
    for (const Diagnostic& diagnostic : diagnostics) {
        out += formatDiagnostic(diagnostic) + "\n";
    }
    return out;
}

std::string formatParseTree(const ParseNode& root) {
    std::string out = parseNodeLabel(root) + "\n";
    appendTree(root, "", out, parseNodeLabel);
    return out;
}

std::string formatAst(const AstNode& root) {
    std::string out = astNodeLabel(root) + "\n";
    appendTree(root, "", out, astNodeLabel);
    return out;
}

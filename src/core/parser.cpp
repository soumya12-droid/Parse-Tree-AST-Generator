#include "core/parser.h"

#include <string>
#include <utility>

namespace {

// Thrown by the parser functions to unwind to parse() at the first error.
struct SyntaxFailure {
    Diagnostic diagnostic;
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    // program -> statement*
    std::unique_ptr<ParseNode> parseProgram() {
        auto node = begin(ParseSymbol::Program);
        while (current().type != TokenType::END) {
            node->children.push_back(parseStatement());
        }
        return finish(std::move(node));
    }

private:
    const std::vector<Token>& tokens_;
    std::size_t pos_ = 0;
    int depth_ = 0;

    // The parser never consumes END, so pos_ always indexes a valid token.
    const Token& current() const { return tokens_[pos_]; }

    std::unique_ptr<ParseNode> begin(ParseSymbol symbol) const {
        auto node = std::make_unique<ParseNode>();
        node->symbol = symbol;
        node->firstToken = pos_;
        return node;
    }

    std::unique_ptr<ParseNode> finish(std::unique_ptr<ParseNode> node) const {
        node->endToken = pos_;
        return node;
    }

    // Consumes the current token as a leaf. Callers have already checked its type.
    std::unique_ptr<ParseNode> terminal() {
        auto node = begin(ParseSymbol::Terminal);
        node->token = current();
        ++pos_;
        return finish(std::move(node));
    }

    static std::string unexpected(const Token& token) {
        if (token.type == TokenType::END) return "Unexpected end of input";
        return "Unexpected token '" + token.lexeme + "'";
    }

    static std::string where(const Token& token) {
        return "line " + std::to_string(token.line) + ", column " + std::to_string(token.column);
    }

    [[noreturn]] static void fail(const Token& at, const std::string& message,
                                  std::vector<TokenType> expected,
                                  bool implementationLimit = false) {
        throw SyntaxFailure{Diagnostic{Phase::Syntax, message, at.line, at.column, at.lexeme,
                                       std::move(expected), implementationLimit}};
    }

    // statement -> ID '=' expression ';'
    std::unique_ptr<ParseNode> parseStatement() {
        auto node = begin(ParseSymbol::Statement);

        if (current().type != TokenType::ID) {
            fail(current(), unexpected(current()), {TokenType::ID, TokenType::END});
        }
        node->children.push_back(terminal());

        if (current().type != TokenType::ASSIGN) {
            fail(current(), unexpected(current()), {TokenType::ASSIGN});
        }
        node->children.push_back(terminal());

        node->children.push_back(parseExpression());

        if (current().type != TokenType::SEMICOLON) {
            const Token& found = current();
            if (found.type == TokenType::RPAREN) {
                fail(found, "Unmatched ')': there is no matching '('", {TokenType::SEMICOLON});
            }
            const Token& previous = tokens_[pos_ - 1];
            // An operand-like token straight after an operand (`b c`, `5a`,
            // `c (d)`) is most likely a forgotten operator, so claiming only a
            // missing ';' would mislead. The exception is an identifier that
            // starts the next statement (`b` newline `c = ...`): there the
            // missing ';' really is the problem. `found` is not END here, so
            // the token after it exists.
            const bool startsOperand = found.type == TokenType::ID ||
                                       found.type == TokenType::NUMBER ||
                                       found.type == TokenType::LPAREN;
            const bool startsStatement =
                found.type == TokenType::ID && tokens_[pos_ + 1].type == TokenType::ASSIGN;
            if (startsOperand && !startsStatement) {
                fail(found, unexpected(found) + " after '" + previous.lexeme + "' at " + where(previous),
                     {TokenType::PLUS, TokenType::MINUS, TokenType::MUL, TokenType::DIV,
                      TokenType::SEMICOLON});
            }
            fail(found,
                 unexpected(found) + "; missing ';' after '" + previous.lexeme + "' at " +
                     where(previous),
                 {TokenType::SEMICOLON});
        }
        node->children.push_back(terminal());

        return finish(std::move(node));
    }

    // expression -> term (('+' | '-') term)*
    std::unique_ptr<ParseNode> parseExpression() {
        auto node = begin(ParseSymbol::Expression);
        node->children.push_back(parseTerm());
        while (current().type == TokenType::PLUS || current().type == TokenType::MINUS) {
            node->children.push_back(terminal());
            node->children.push_back(parseTerm());
        }
        return finish(std::move(node));
    }

    // term -> factor (('*' | '/') factor)*
    std::unique_ptr<ParseNode> parseTerm() {
        auto node = begin(ParseSymbol::Term);
        node->children.push_back(parseFactor());
        while (current().type == TokenType::MUL || current().type == TokenType::DIV) {
            node->children.push_back(terminal());
            node->children.push_back(parseFactor());
        }
        return finish(std::move(node));
    }

    // factor -> NUMBER | ID | '(' expression ')'
    std::unique_ptr<ParseNode> parseFactor() {
        auto node = begin(ParseSymbol::Factor);

        switch (current().type) {
            case TokenType::NUMBER:
            case TokenType::ID:
                node->children.push_back(terminal());
                break;

            case TokenType::LPAREN: {
                const Token open = current();
                if (depth_ >= kMaxNestingDepth) {
                    fail(open,
                         "Parentheses nested too deeply: this tool allows at most " +
                             std::to_string(kMaxNestingDepth) +
                             " levels (implementation limit, not a grammar rule)",
                         {}, true);
                }
                node->children.push_back(terminal());

                ++depth_;
                node->children.push_back(parseExpression());
                --depth_;

                if (current().type != TokenType::RPAREN) {
                    const Token& found = current();
                    // As at the end of a statement: an operand-like token right
                    // after an operand is more likely a forgotten operator than
                    // a forgotten ')'.
                    if (found.type == TokenType::ID || found.type == TokenType::NUMBER ||
                        found.type == TokenType::LPAREN) {
                        const Token& previous = tokens_[pos_ - 1];
                        fail(found,
                             unexpected(found) + " after '" + previous.lexeme + "' at " +
                                 where(previous) + "; the '(' at " + where(open) +
                                 " is not closed yet",
                             {TokenType::PLUS, TokenType::MINUS, TokenType::MUL, TokenType::DIV,
                              TokenType::RPAREN});
                    }
                    fail(found,
                         unexpected(found) + "; missing ')' to close '(' at " + where(open),
                         {TokenType::RPAREN});
                }
                node->children.push_back(terminal());
                break;
            }

            default:
                fail(current(), unexpected(current()),
                     {TokenType::ID, TokenType::NUMBER, TokenType::LPAREN});
        }

        return finish(std::move(node));
    }
};

}  // namespace

ParseResult parse(const std::vector<Token>& tokens) {
    ParseResult result;

    if (tokens.empty() || tokens.back().type != TokenType::END) {
        result.diagnostics.push_back(Diagnostic{
            Phase::Syntax, "Internal error: token stream does not end with END", 1, 1, "", {}});
        return result;
    }

    try {
        Parser parser(tokens);
        result.tree = parser.parseProgram();
    } catch (const SyntaxFailure& failure) {
        result.tree.reset();
        result.diagnostics.push_back(failure.diagnostic);
    }
    return result;
}

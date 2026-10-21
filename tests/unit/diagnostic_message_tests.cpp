// Exact diagnostics (message, position, phase, expected tokens, limit flag)
// for each kind of problem a student is likely to meet. These pin the wording
// of the error messages: change them only when the new text is clearer.

#include <string>
#include <vector>

#include "core/analyzer.h"
#include "core/ast_builder.h"
#include "core/parser.h"
#include "output/text_printer.h"
#include "test_framework.h"

namespace {

using Types = std::vector<TokenType>;

const Types kOperand = {TokenType::ID, TokenType::NUMBER, TokenType::LPAREN};
const Types kOperatorOrSemicolon = {TokenType::PLUS, TokenType::MINUS, TokenType::MUL,
                                    TokenType::DIV, TokenType::SEMICOLON};
const Types kOperatorOrRparen = {TokenType::PLUS, TokenType::MINUS, TokenType::MUL,
                                 TokenType::DIV, TokenType::RPAREN};

void expectDiagnostic(const std::string& source, Phase phase, const std::string& message, int line,
                      int column, const std::string& found, const Types& expected,
                      bool implementationLimit = false) {
    const AnalysisResult result = analyze(source);
    CHECK(!result.ok());
    CHECK(result.ast == nullptr);
    CHECK(result.parseTree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.size() != 1) return;
    const Diagnostic& d = result.diagnostics[0];
    CHECK(d.phase == phase);
    CHECK_EQ(d.message, message);
    CHECK_EQ(d.line, line);
    CHECK_EQ(d.column, column);
    CHECK_EQ(d.found, found);
    CHECK(d.expected == expected);
    CHECK(d.implementationLimit == implementationLimit);
}

}  // namespace

// ---- Lexical -----------------------------------------------------------------------------

TEST(message_invalid_character) {
    expectDiagnostic("a = b $ c;", Phase::Lexical, "Invalid character '$'", 1, 7, "$", {});
}

TEST(message_decimal_number_explains_itself) {
    expectDiagnostic("a = 3.14;", Phase::Lexical,
                     "Invalid character '.' (decimal numbers are not supported)", 1, 6, ".", {});
}

TEST(message_unprintable_character_is_shown_as_hex) {
    expectDiagnostic(std::string("a = \x01;"), Phase::Lexical, "Invalid character '\\x01'", 1, 5,
                     "\x01", {});
}

// ---- Syntax: what is missing or unexpected -----------------------------------------------------

TEST(message_unexpected_operator_where_an_operand_is_needed) {
    // The wording from the project proposal's invalid-input example.
    expectDiagnostic("a = b + * 5;", Phase::Syntax, "Unexpected token '*'", 1, 9, "*", kOperand);
    CHECK_EQ(formatDiagnostic(analyze("a = b + * 5;").diagnostics[0]),
             "Syntax error at line 1, column 9: Unexpected token '*'\n"
             "  Expected: identifier, number, or '('");
}

TEST(message_missing_operand) {
    expectDiagnostic("a = b +;", Phase::Syntax, "Unexpected token ';'", 1, 8, ";", kOperand);
    expectDiagnostic("a = ;", Phase::Syntax, "Unexpected token ';'", 1, 5, ";", kOperand);
    expectDiagnostic("a =", Phase::Syntax, "Unexpected end of input", 1, 4, "", kOperand);
}

TEST(message_missing_assignment_operator) {
    expectDiagnostic("a b = c;", Phase::Syntax, "Unexpected token 'b'", 1, 3, "b",
                     {TokenType::ASSIGN});
    expectDiagnostic("a", Phase::Syntax, "Unexpected end of input", 1, 2, "", {TokenType::ASSIGN});
}

TEST(message_statement_must_start_with_an_identifier) {
    expectDiagnostic("= 1;", Phase::Syntax, "Unexpected token '='", 1, 1, "=",
                     {TokenType::ID, TokenType::END});
}

TEST(message_missing_closing_parenthesis_names_the_open_one) {
    expectDiagnostic("a = (b + c;", Phase::Syntax,
                     "Unexpected token ';'; missing ')' to close '(' at line 1, column 5", 1, 11,
                     ";", {TokenType::RPAREN});
    expectDiagnostic("a = (b", Phase::Syntax,
                     "Unexpected end of input; missing ')' to close '(' at line 1, column 5", 1, 7,
                     "", {TokenType::RPAREN});
}

TEST(message_unmatched_closing_parenthesis) {
    expectDiagnostic("a = b + c);", Phase::Syntax, "Unmatched ')': there is no matching '('", 1,
                     10, ")", {TokenType::SEMICOLON});
}

TEST(message_missing_semicolon_when_the_next_statement_starts) {
    expectDiagnostic("a = b + c\nd = 1;", Phase::Syntax,
                     "Unexpected token 'd'; missing ';' after 'c' at line 1, column 9", 2, 1, "d",
                     {TokenType::SEMICOLON});
    expectDiagnostic("a = b + c", Phase::Syntax,
                     "Unexpected end of input; missing ';' after 'c' at line 1, column 9", 1, 10,
                     "", {TokenType::SEMICOLON});
}

// ---- Syntax: forgotten operator versus forgotten delimiter ----------------------------------------

TEST(message_operand_after_operand_does_not_claim_a_missing_semicolon) {
    expectDiagnostic("a = b c;", Phase::Syntax, "Unexpected token 'c' after 'b' at line 1, column 5",
                     1, 7, "c", kOperatorOrSemicolon);
    expectDiagnostic("x = 5a;", Phase::Syntax, "Unexpected token 'a' after '5' at line 1, column 5",
                     1, 6, "a", kOperatorOrSemicolon);
    expectDiagnostic("a = b (c);", Phase::Syntax,
                     "Unexpected token '(' after 'b' at line 1, column 5", 1, 7, "(",
                     kOperatorOrSemicolon);
    expectDiagnostic("a = (b + c) d;", Phase::Syntax,
                     "Unexpected token 'd' after ')' at line 1, column 11", 1, 13, "d",
                     kOperatorOrSemicolon);
    expectDiagnostic("a = b\n5 = 3;", Phase::Syntax,
                     "Unexpected token '5' after 'b' at line 1, column 5", 2, 1, "5",
                     kOperatorOrSemicolon);
}

TEST(message_an_identifier_that_starts_the_next_statement_is_a_missing_semicolon) {
    expectDiagnostic("a = b\nc = d;", Phase::Syntax,
                     "Unexpected token 'c'; missing ';' after 'b' at line 1, column 5", 2, 1, "c",
                     {TokenType::SEMICOLON});
}

TEST(message_operand_after_operand_inside_parentheses) {
    expectDiagnostic("a = (b c);", Phase::Syntax,
                     "Unexpected token 'c' after 'b' at line 1, column 6; the '(' at line 1, "
                     "column 5 is not closed yet",
                     1, 8, "c", kOperatorOrRparen);
    expectDiagnostic("a = ((b) 5);", Phase::Syntax,
                     "Unexpected token '5' after ')' at line 1, column 8; the '(' at line 1, "
                     "column 5 is not closed yet",
                     1, 10, "5", kOperatorOrRparen);
}

TEST(message_other_tokens_after_an_operand_keep_the_delimiter_wording) {
    // '=' and ';' cannot be a forgotten operand, so the delimiter message stands.
    expectDiagnostic("a = b = c;", Phase::Syntax,
                     "Unexpected token '='; missing ';' after 'b' at line 1, column 5", 1, 7, "=",
                     {TokenType::SEMICOLON});
    expectDiagnostic("a = (b = c);", Phase::Syntax,
                     "Unexpected token '='; missing ')' to close '(' at line 1, column 5", 1, 8,
                     "=", {TokenType::RPAREN});
}

TEST(message_expected_list_is_rendered_for_students) {
    CHECK_EQ(formatDiagnostic(analyze("a = b c;").diagnostics[0]),
             "Syntax error at line 1, column 7: Unexpected token 'c' after 'b' at line 1, column 5\n"
             "  Expected: '+', '-', '*', '/', or ';'");
    CHECK_EQ(formatDiagnostic(analyze("a = (b c);").diagnostics[0]),
             "Syntax error at line 1, column 8: Unexpected token 'c' after 'b' at line 1, column 6; "
             "the '(' at line 1, column 5 is not closed yet\n"
             "  Expected: '+', '-', '*', '/', or ')'");
}

// ---- Implementation limits -----------------------------------------------------------------------------

TEST(message_parenthesis_nesting_limit) {
    const int tooDeep = kMaxNestingDepth + 1;
    const std::string source =
        "a = " + std::string(tooDeep, '(') + "b" + std::string(tooDeep, ')') + ";";
    expectDiagnostic(source, Phase::Syntax,
                     "Parentheses nested too deeply: this tool allows at most 256 levels "
                     "(implementation limit, not a grammar rule)",
                     1, 4 + tooDeep, "(", {}, /*implementationLimit=*/true);
}

TEST(message_ast_height_limit) {
    std::string source = "r = 1";
    for (int i = 1; i < kMaxAstHeight - 1; ++i) source += "+1";
    source += ";";
    expectDiagnostic(source, Phase::Syntax,
                     "Expression too complex: its syntax tree would be more than 1000 levels deep "
                     "(implementation limit, not a grammar rule)",
                     1, 1, "r", {}, /*implementationLimit=*/true);
}

TEST(message_limits_never_claim_the_program_is_invalid_or_list_expected_tokens) {
    const std::string deep = "a = " + std::string(kMaxNestingDepth + 1, '(') + "b" +
                             std::string(kMaxNestingDepth + 1, ')') + ";";
    const std::string text = analyze(deep).diagnostics[0].message;
    CHECK(text.find("invalid") == std::string::npos);
    CHECK(text.find("implementation limit") != std::string::npos);
    CHECK(analyze(deep).diagnostics[0].expected.empty());
}

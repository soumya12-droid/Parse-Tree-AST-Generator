#include <string>

#include "core/analyzer.h"
#include "core/ast_builder.h"
#include "core/diagnostic.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "test_framework.h"

// ---- implementationLimit flag -------------------------------------------------

TEST(diagnostic_implementation_limit_defaults_to_false) {
    const Diagnostic d{Phase::Syntax, "m", 1, 1, "x"};
    CHECK(!d.implementationLimit);
}

// One source per distinct failure site in the lexer and parser: none of them
// may be classified as an implementation limit.
TEST(every_ordinary_error_site_is_not_an_implementation_limit) {
    const char* const sources[] = {
        "= 1;",          // statement does not start with an identifier
        "a b = 1;",      // missing '='
        "a",             // end of input where '=' is required
        "a =",           // end of input where an operand is required
        "a = ;",         // missing operand
        "a = b + * 5;",  // unexpected operator
        "a = b c;",      // missing ';'
        "a = b + c",     // missing ';' at end of input
        "a = b + c);",   // unmatched ')'
        "a = (b + c;",   // missing ')'
        "a = (b",        // missing ')' at end of input
        "a = ();",       // empty parentheses
        "a = $;",        // lexical: invalid character
        "a = 3.14;",     // lexical: decimal
    };
    for (const char* source : sources) {
        const AnalysisResult result = analyze(source);
        CHECK(!result.ok());
        CHECK_EQ(result.diagnostics.size(), 1);
        if (result.diagnostics.empty()) continue;
        CHECK(!result.diagnostics[0].implementationLimit);
    }
}

TEST(input_exactly_at_each_limit_is_accepted_and_not_a_diagnostic) {
    const std::string atNesting = "a = " + std::string(kMaxNestingDepth, '(') + "b" +
                                  std::string(kMaxNestingDepth, ')') + ";";
    CHECK(analyze(atNesting).ok());
    std::string atHeight = "r = 1";
    for (int i = 1; i < kMaxAstHeight - 2; ++i) atHeight += "+1";
    CHECK(analyze(atHeight + ";").ok());
}

TEST(grammar_errors_are_not_flagged_as_implementation_limits) {
    const char* const sources[] = {"a = b + * 5;", "a = (b + c;", "a = b $ c;", "a = b c;"};
    for (const char* source : sources) {
        const AnalysisResult result = analyze(source);
        CHECK(!result.ok());
        CHECK_EQ(result.diagnostics.size(), 1);
        if (result.diagnostics.empty()) continue;
        CHECK(!result.diagnostics[0].implementationLimit);
    }
}

TEST(nesting_limit_error_is_flagged_as_implementation_limit) {
    const std::string source = "a = " + std::string(kMaxNestingDepth + 1, '(') + "b" +
                               std::string(kMaxNestingDepth + 1, ')') + ";";
    const AnalysisResult result = analyze(source);
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK(result.diagnostics[0].implementationLimit);
    CHECK(result.diagnostics[0].phase == Phase::Syntax);
}

TEST(ast_height_limit_error_is_flagged_as_implementation_limit) {
    std::string source = "r = 1";
    for (int i = 1; i < kMaxAstHeight - 1; ++i) source += "+1";
    source += ";";
    const AnalysisResult result = analyze(source);
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK(result.diagnostics[0].implementationLimit);
}

// ---- expected token list --------------------------------------------------------

TEST(diagnostic_expected_defaults_to_empty) {
    const Diagnostic d{Phase::Lexical, "message", 1, 2, "$"};
    CHECK(d.expected.empty());
}

TEST(lexical_diagnostics_have_no_expected_tokens) {
    const LexResult result = lex("a = $;");
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK(result.diagnostics[0].expected.empty());
}

TEST(diagnostic_can_carry_expected_token_types) {
    Diagnostic d{Phase::Syntax, "Unexpected token '*'", 1, 9, "*"};
    d.expected = {TokenType::ID, TokenType::NUMBER, TokenType::LPAREN};
    CHECK_EQ(d.expected.size(), 3);
    CHECK(d.expected[0] == TokenType::ID);
    CHECK(d.expected[2] == TokenType::LPAREN);
    CHECK_EQ(d.line, 1);
    CHECK_EQ(d.column, 9);
}

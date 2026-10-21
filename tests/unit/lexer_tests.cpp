#include <string>

#include "core/lexer.h"
#include "output/text_printer.h"
#include "test_framework.h"

namespace {

// "1:1 ID(a) 1:3 ASSIGN(=) ... 1:15 END" - tokens with positions on one line.
std::string withPositions(const LexResult& result) {
    std::string out;
    for (const Token& token : result.tokens) {
        if (!out.empty()) out += " ";
        out += formatToken(token);
    }
    return out;
}

// "ID(a) ASSIGN(=) ... END" - token types and lexemes only.
std::string kinds(const std::string& source) {
    const LexResult result = lex(source);
    std::string out;
    for (const Token& token : result.tokens) {
        if (!out.empty()) out += " ";
        out += tokenTypeName(token.type);
        if (token.type != TokenType::END) out += "(" + token.lexeme + ")";
    }
    return out;
}

}  // namespace

// ---- Baselines from the project requirements -----------------------------

TEST(baseline_assignment_with_precedence_operators) {
    const LexResult result = lex("a = b + c * 5;");
    CHECK(result.ok());
    CHECK_EQ(withPositions(result),
             "1:1 ID(a) 1:3 ASSIGN(=) 1:5 ID(b) 1:7 PLUS(+) 1:9 ID(c) 1:11 MUL(*) "
             "1:13 NUMBER(5) 1:14 SEMICOLON(;) 1:15 END");
}

TEST(baseline_invalid_dollar_reported_at_line1_col7) {
    const LexResult result = lex("a = b $ c;");
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    const Diagnostic& d = result.diagnostics[0];
    CHECK(d.phase == Phase::Lexical);
    CHECK_EQ(d.line, 1);
    CHECK_EQ(d.column, 7);
    CHECK_EQ(d.found, "$");
    CHECK_EQ(d.message, "Invalid character '$'");
}

TEST(baseline_parenthesised_expression) {
    CHECK_EQ(kinds("result = (x + y) * z;"),
             "ID(result) ASSIGN(=) LPAREN(() ID(x) PLUS(+) ID(y) RPAREN()) MUL(*) ID(z) "
             "SEMICOLON(;) END");
    CHECK_EQ(withPositions(lex("result = (x + y) * z;")),
             "1:1 ID(result) 1:8 ASSIGN(=) 1:10 LPAREN(() 1:11 ID(x) 1:13 PLUS(+) 1:15 ID(y) "
             "1:16 RPAREN()) 1:18 MUL(*) 1:20 ID(z) 1:21 SEMICOLON(;) 1:22 END");
}

TEST(baseline_multiline_positions) {
    const LexResult result = lex("x = 1;\n  y = x * 2;\n\nz = (y);");
    CHECK(result.ok());
    CHECK_EQ(withPositions(result),
             "1:1 ID(x) 1:3 ASSIGN(=) 1:5 NUMBER(1) 1:6 SEMICOLON(;) "
             "2:3 ID(y) 2:5 ASSIGN(=) 2:7 ID(x) 2:9 MUL(*) 2:11 NUMBER(2) 2:12 SEMICOLON(;) "
             "4:1 ID(z) 4:3 ASSIGN(=) 4:5 LPAREN(() 4:6 ID(y) 4:7 RPAREN()) 4:8 SEMICOLON(;) "
             "4:9 END");
}

// ---- Identifiers and numbers ---------------------------------------------

TEST(identifiers_letters_underscores_digits) {
    CHECK_EQ(kinds("abc"), "ID(abc) END");
    CHECK_EQ(kinds("_x1"), "ID(_x1) END");
    CHECK_EQ(kinds("a_b_9"), "ID(a_b_9) END");
    CHECK_EQ(kinds("__"), "ID(__) END");
    CHECK_EQ(kinds("Total2"), "ID(Total2) END");
}

TEST(identifier_cannot_start_with_digit) {
    CHECK_EQ(kinds("1a"), "NUMBER(1) ID(a) END");
    CHECK_EQ(kinds("a1"), "ID(a1) END");
}

TEST(keywords_are_ordinary_identifiers) {
    CHECK_EQ(kinds("while if int"), "ID(while) ID(if) ID(int) END");
}

TEST(integer_literals) {
    CHECK_EQ(kinds("0"), "NUMBER(0) END");
    CHECK_EQ(kinds("42"), "NUMBER(42) END");
    CHECK_EQ(kinds("007"), "NUMBER(007) END");
    CHECK_EQ(kinds("1234567890"), "NUMBER(1234567890) END");
}

TEST(minus_is_an_operator_not_part_of_a_number) {
    CHECK_EQ(kinds("-5"), "MINUS(-) NUMBER(5) END");
    CHECK_EQ(kinds("a-5"), "ID(a) MINUS(-) NUMBER(5) END");
}

// ---- Operators and delimiters --------------------------------------------

TEST(each_operator_and_delimiter) {
    CHECK_EQ(kinds("="), "ASSIGN(=) END");
    CHECK_EQ(kinds("+"), "PLUS(+) END");
    CHECK_EQ(kinds("-"), "MINUS(-) END");
    CHECK_EQ(kinds("*"), "MUL(*) END");
    CHECK_EQ(kinds("/"), "DIV(/) END");
    CHECK_EQ(kinds("("), "LPAREN(() END");
    CHECK_EQ(kinds(")"), "RPAREN()) END");
    CHECK_EQ(kinds(";"), "SEMICOLON(;) END");
}

TEST(no_multi_character_operators) {
    CHECK_EQ(kinds("=="), "ASSIGN(=) ASSIGN(=) END");
    CHECK_EQ(kinds("++"), "PLUS(+) PLUS(+) END");
    CHECK_EQ(kinds("//"), "DIV(/) DIV(/) END");
}

// ---- Whitespace and positions --------------------------------------------

TEST(with_and_without_whitespace_give_same_tokens) {
    CHECK_EQ(kinds("a=(b+c)*5;"), kinds("a = ( b + c ) * 5 ;"));
    CHECK_EQ(withPositions(lex("a=b;")),
             "1:1 ID(a) 1:2 ASSIGN(=) 1:3 ID(b) 1:4 SEMICOLON(;) 1:5 END");
}

TEST(multiple_spaces_and_tabs_count_one_column_each) {
    CHECK_EQ(withPositions(lex("a   =\tb")), "1:1 ID(a) 1:5 ASSIGN(=) 1:7 ID(b) 1:8 END");
    CHECK_EQ(withPositions(lex("\t\ta")), "1:3 ID(a) 1:4 END");
}

TEST(lf_newlines_advance_line_and_reset_column) {
    CHECK_EQ(withPositions(lex("a\nb\n\nc")),
             "1:1 ID(a) 2:1 ID(b) 4:1 ID(c) 4:2 END");
}

TEST(crlf_counts_as_one_line_break) {
    const LexResult result = lex("a = 1;\r\nb = 2;\r\n");
    CHECK(result.ok());
    CHECK_EQ(withPositions(result),
             "1:1 ID(a) 1:3 ASSIGN(=) 1:5 NUMBER(1) 1:6 SEMICOLON(;) "
             "2:1 ID(b) 2:3 ASSIGN(=) 2:5 NUMBER(2) 2:6 SEMICOLON(;) 3:1 END");
}

TEST(crlf_and_lf_give_identical_positions) {
    CHECK_EQ(withPositions(lex("x = 1;\ny = 2;")), withPositions(lex("x = 1;\r\ny = 2;")));
}

TEST(lone_carriage_return_is_a_line_break) {
    CHECK_EQ(withPositions(lex("a\rb")), "1:1 ID(a) 2:1 ID(b) 2:2 END");
}

TEST(error_position_after_crlf_lines) {
    const LexResult result = lex("a = 1;\r\nb = $;");
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(result.diagnostics[0].line, 2);
    CHECK_EQ(result.diagnostics[0].column, 5);
}

// ---- END token ------------------------------------------------------------

TEST(empty_input_gives_only_end_at_1_1) {
    const LexResult result = lex("");
    CHECK(result.ok());
    CHECK_EQ(withPositions(result), "1:1 END");
}

TEST(whitespace_only_input_gives_only_end) {
    CHECK_EQ(withPositions(lex("   ")), "1:4 END");
    CHECK_EQ(withPositions(lex("\n")), "2:1 END");
    CHECK_EQ(withPositions(lex("  \n \t")), "2:3 END");
    CHECK_EQ(withPositions(lex("\r\n\r\n")), "3:1 END");
}

TEST(exactly_one_end_token_and_it_is_last) {
    const LexResult result = lex("a = 1;\nb = 2;\n");
    int ends = 0;
    for (const Token& token : result.tokens) {
        if (token.type == TokenType::END) ++ends;
    }
    CHECK_EQ(ends, 1);
    CHECK(!result.tokens.empty());
    if (!result.tokens.empty()) CHECK(result.tokens.back().type == TokenType::END);
}

TEST(end_follows_trailing_newline) {
    CHECK_EQ(withPositions(lex("a;\n")), "1:1 ID(a) 1:2 SEMICOLON(;) 2:1 END");
}

// ---- Invalid characters ---------------------------------------------------

TEST(invalid_characters_dollar_at_quotes) {
    const char* const samples[] = {"$", "@", "\"", "'", "#", "!", "%", "&", "[", "{", "<", ".", "^"};
    for (const char* sample : samples) {
        const LexResult result = lex(std::string("a ") + sample);
        CHECK(!result.ok());
        CHECK_EQ(result.diagnostics.size(), 1);
        if (result.diagnostics.empty()) continue;
        CHECK_EQ(result.diagnostics[0].line, 1);
        CHECK_EQ(result.diagnostics[0].column, 3);
        CHECK_EQ(result.diagnostics[0].found, sample);
        CHECK(result.diagnostics[0].phase == Phase::Lexical);
    }
}

TEST(invalid_message_names_the_character) {
    CHECK_EQ(lex("@").diagnostics[0].message, "Invalid character '@'");
    CHECK_EQ(lex("\"").diagnostics[0].message, "Invalid character '\"'");
}

TEST(all_invalid_characters_are_reported_with_positions) {
    const LexResult result = lex("a = $ + @;\nb = \"x\";");
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 4);
    if (result.diagnostics.size() != 4) return;
    CHECK_EQ(formatDiagnostic(result.diagnostics[0]),
             "Lexical error at line 1, column 5: Invalid character '$'");
    CHECK_EQ(formatDiagnostic(result.diagnostics[1]),
             "Lexical error at line 1, column 9: Invalid character '@'");
    CHECK_EQ(formatDiagnostic(result.diagnostics[2]),
             "Lexical error at line 2, column 5: Invalid character '\"'");
    CHECK_EQ(formatDiagnostic(result.diagnostics[3]),
             "Lexical error at line 2, column 7: Invalid character '\"'");
}

TEST(lexical_errors_mean_not_successfully_tokenized) {
    const LexResult result = lex("a = b $ c;");
    CHECK(!result.ok());
    for (const Token& token : result.tokens) {
        CHECK(token.type != TokenType::END);
    }
}

TEST(valid_tokens_after_an_invalid_character_keep_correct_positions) {
    const LexResult result = lex("a $ b");
    CHECK_EQ(withPositions(result), "1:1 ID(a) 1:5 ID(b)");
}

TEST(decimal_numbers_are_rejected_at_the_dot) {
    const LexResult result = lex("x = 3.14;");
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(result.diagnostics[0].line, 1);
    CHECK_EQ(result.diagnostics[0].column, 6);
    CHECK_EQ(result.diagnostics[0].found, ".");
    CHECK_EQ(result.diagnostics[0].message,
             "Invalid character '.' (decimal numbers are not supported)");
}

TEST(non_ascii_character_is_one_diagnostic_and_one_column) {
    const LexResult result = lex("a \xC3\xA9 b");  // "a é b"
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(result.diagnostics[0].column, 3);
    CHECK_EQ(result.diagnostics[0].found, "\xC3\xA9");
    CHECK_EQ(withPositions(result), "1:1 ID(a) 1:5 ID(b)");
}

TEST(control_character_is_reported_not_dropped) {
    const LexResult result = lex(std::string("a\0b", 3));
    CHECK(!result.ok());
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(result.diagnostics[0].column, 2);
    CHECK_EQ(result.diagnostics[0].message, "Invalid character '\\x00'");
}

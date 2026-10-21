#include <string>
#include <vector>

#include "core/lexer.h"
#include "core/parser.h"
#include "output/text_printer.h"
#include "test_framework.h"

namespace {

// Compact S-expression of a parse tree: nonterminals as "(name child ...)",
// terminals as TYPE(lexeme).
std::string shape(const ParseNode& node) {
    if (node.symbol == ParseSymbol::Terminal) {
        return std::string(tokenTypeName(node.token.type)) + "(" + node.token.lexeme + ")";
    }
    std::string out = std::string("(") + parseSymbolName(node.symbol);
    for (const auto& child : node.children) {
        out += " " + shape(*child);
    }
    return out + ")";
}

ParseResult parseSource(const std::string& source) {
    const LexResult lexed = lex(source);
    CHECK(lexed.ok());
    return parse(lexed.tokens);
}

// "(program ...)" for a successful parse, "ERROR" otherwise.
std::string treeOf(const std::string& source) {
    const ParseResult result = parseSource(source);
    return result.ok() ? shape(*result.tree) : "ERROR";
}

// Checks that `source` fails with exactly one diagnostic and no tree.
void expectSyntaxError(const std::string& source, int line, int column, const std::string& found,
                       const std::string& message, const std::vector<TokenType>& expected) {
    const ParseResult result = parseSource(source);
    CHECK(!result.ok());
    CHECK(result.tree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.size() != 1) return;
    const Diagnostic& d = result.diagnostics[0];
    CHECK(d.phase == Phase::Syntax);
    CHECK_EQ(d.line, line);
    CHECK_EQ(d.column, column);
    CHECK_EQ(d.found, found);
    CHECK_EQ(d.message, message);
    CHECK(d.expected == expected);
}

const std::vector<TokenType> kOperand = {TokenType::ID, TokenType::NUMBER, TokenType::LPAREN};

}  // namespace

// ---- Valid programs ---------------------------------------------------------

TEST(parse_single_assignment) {
    CHECK_EQ(treeOf("a = 1;"),
             "(program (statement ID(a) ASSIGN(=) (expression (term (factor NUMBER(1)))) "
             "SEMICOLON(;)))");
}

TEST(parse_baseline_precedence_sample) {
    CHECK_EQ(treeOf("a = b + c * 5;"),
             "(program (statement ID(a) ASSIGN(=) "
             "(expression (term (factor ID(b))) PLUS(+) "
             "(term (factor ID(c)) MUL(*) (factor NUMBER(5)))) "
             "SEMICOLON(;)))");
}

TEST(parse_multiplication_binds_tighter_on_the_left_too) {
    CHECK_EQ(treeOf("a = b * c + 5;"),
             "(program (statement ID(a) ASSIGN(=) "
             "(expression (term (factor ID(b)) MUL(*) (factor ID(c))) PLUS(+) "
             "(term (factor NUMBER(5)))) "
             "SEMICOLON(;)))");
}

TEST(parse_additive_chain_is_flat_in_order) {
    CHECK_EQ(treeOf("a = b - c - d;"),
             "(program (statement ID(a) ASSIGN(=) "
             "(expression (term (factor ID(b))) MINUS(-) (term (factor ID(c))) MINUS(-) "
             "(term (factor ID(d)))) "
             "SEMICOLON(;)))");
}

TEST(parse_multiplicative_chain_is_flat_in_order) {
    CHECK_EQ(treeOf("a = b / c / d;"),
             "(program (statement ID(a) ASSIGN(=) "
             "(expression (term (factor ID(b)) DIV(/) (factor ID(c)) DIV(/) (factor ID(d)))) "
             "SEMICOLON(;)))");
}

TEST(parse_mixed_operators_in_one_statement) {
    CHECK_EQ(treeOf("x = a - b / c * d + e;"),
             "(program (statement ID(x) ASSIGN(=) "
             "(expression (term (factor ID(a))) MINUS(-) "
             "(term (factor ID(b)) DIV(/) (factor ID(c)) MUL(*) (factor ID(d))) PLUS(+) "
             "(term (factor ID(e)))) "
             "SEMICOLON(;)))");
}

TEST(parse_parenthesised_baseline_sample) {
    CHECK_EQ(treeOf("result = (x + y) * z;"),
             "(program (statement ID(result) ASSIGN(=) "
             "(expression (term "
             "(factor LPAREN(() (expression (term (factor ID(x))) PLUS(+) (term (factor ID(y)))) "
             "RPAREN())) "
             "MUL(*) (factor ID(z)))) "
             "SEMICOLON(;)))");
}

TEST(parse_nested_parentheses_keep_every_paren_node) {
    const std::string inner = "(expression (term (factor ID(b))))";
    const std::string middle =
        "(expression (term (factor LPAREN(() " + inner + " RPAREN()))))";
    const std::string outer =
        "(expression (term (factor LPAREN(() " + middle + " RPAREN()))))";
    CHECK_EQ(treeOf("a = ((b));"),
             "(program (statement ID(a) ASSIGN(=) " + outer + " SEMICOLON(;)))");
}

TEST(parse_two_parenthesised_operands) {
    const std::string left =
        "(factor LPAREN(() (expression (term (factor ID(b))) PLUS(+) (term (factor ID(c)))) "
        "RPAREN()))";
    const std::string right =
        "(factor LPAREN(() (expression (term (factor ID(d))) MINUS(-) (term (factor ID(e)))) "
        "RPAREN()))";
    CHECK_EQ(treeOf("a = (b + c) * (d - e);"),
             "(program (statement ID(a) ASSIGN(=) (expression (term " + left + " MUL(*) " +
                 right + ")) SEMICOLON(;)))");
}

TEST(parse_multiple_statements) {
    const ParseResult result = parseSource("a = 1; b = a + 2;\nc = (b);");
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(result.tree->children.size(), 3);
    for (const auto& child : result.tree->children) {
        CHECK(child->symbol == ParseSymbol::Statement);
        CHECK_EQ(child->children.size(), 4);
    }
    CHECK_EQ(shape(*result.tree->children[0]),
             "(statement ID(a) ASSIGN(=) (expression (term (factor NUMBER(1)))) SEMICOLON(;))");
    CHECK_EQ(result.tree->children[2]->children[0]->token.lexeme, "c");
    CHECK_EQ(result.tree->children[2]->children[0]->token.line, 2);
}

TEST(parse_empty_and_whitespace_only_input_is_an_empty_program) {
    CHECK_EQ(treeOf(""), "(program)");
    CHECK_EQ(treeOf("  \n\t\n"), "(program)");
    const ParseResult result = parseSource("");
    CHECK(result.ok());
    CHECK(result.diagnostics.empty());
}

TEST(parse_tree_nodes_cover_token_ranges) {
    // Tokens: a(0) =(1) b(2) +(3) c(4) ;(5) END(6)
    const ParseResult result = parseSource("a = b + c;");
    CHECK(result.ok());
    if (!result.ok()) return;
    const ParseNode& program = *result.tree;
    CHECK_EQ(program.firstToken, 0);
    CHECK_EQ(program.endToken, 6);

    const ParseNode& statement = *program.children[0];
    CHECK_EQ(statement.firstToken, 0);
    CHECK_EQ(statement.endToken, 6);

    const ParseNode& expression = *statement.children[2];
    CHECK(expression.symbol == ParseSymbol::Expression);
    CHECK_EQ(expression.firstToken, 2);
    CHECK_EQ(expression.endToken, 5);
    CHECK_EQ(expression.children[0]->firstToken, 2);
    CHECK_EQ(expression.children[0]->endToken, 3);
    CHECK_EQ(expression.children[2]->firstToken, 4);
    CHECK_EQ(expression.children[2]->endToken, 5);
}

TEST(parse_terminals_keep_source_positions) {
    const ParseResult result = parseSource("x = 1;\n  y = 2;");
    CHECK(result.ok());
    if (!result.ok()) return;
    const ParseNode& idY = *result.tree->children[1]->children[0];
    CHECK(idY.symbol == ParseSymbol::Terminal);
    CHECK_EQ(idY.token.lexeme, "y");
    CHECK_EQ(idY.token.line, 2);
    CHECK_EQ(idY.token.column, 3);
}

TEST(parse_deep_but_allowed_nesting_succeeds) {
    std::string source = "a = ";
    source += std::string(kMaxNestingDepth, '(') + "b" + std::string(kMaxNestingDepth, ')') + ";";
    CHECK(parseSource(source).ok());
}

// ---- Syntax errors ----------------------------------------------------------

TEST(error_unexpected_operator_baseline) {
    expectSyntaxError("a = b + * 5;", 1, 9, "*", "Unexpected token '*'", kOperand);
}

TEST(error_missing_semicolon_at_end_of_input) {
    expectSyntaxError("a = b + c", 1, 10, "",
                      "Unexpected end of input; missing ';' after 'c' at line 1, column 9",
                      {TokenType::SEMICOLON});
}

TEST(error_missing_semicolon_before_next_statement) {
    expectSyntaxError("a = b\nc = d;", 2, 1, "c",
                      "Unexpected token 'c'; missing ';' after 'b' at line 1, column 5",
                      {TokenType::SEMICOLON});
}

TEST(error_operand_right_after_operand_may_be_a_missing_operator) {
    // `b c`: a forgotten operator is as likely as a forgotten ';', so the
    // message does not claim a missing ';' and the expected list is honest.
    expectSyntaxError("a = b c;", 1, 7, "c", "Unexpected token 'c' after 'b' at line 1, column 5",
                      {TokenType::PLUS, TokenType::MINUS, TokenType::MUL, TokenType::DIV,
                       TokenType::SEMICOLON});
}

TEST(error_missing_closing_paren_baseline_t4) {
    expectSyntaxError("a = (b + c;", 1, 11, ";",
                      "Unexpected token ';'; missing ')' to close '(' at line 1, column 5",
                      {TokenType::RPAREN});
}

TEST(error_missing_closing_paren_at_end_of_input) {
    expectSyntaxError("a = (b", 1, 7, "",
                      "Unexpected end of input; missing ')' to close '(' at line 1, column 5",
                      {TokenType::RPAREN});
}

TEST(error_missing_inner_closing_paren_names_the_inner_open_paren) {
    expectSyntaxError("a = ((b);", 1, 9, ";",
                      "Unexpected token ';'; missing ')' to close '(' at line 1, column 5",
                      {TokenType::RPAREN});
}

TEST(error_unmatched_closing_paren) {
    expectSyntaxError("a = b + c);", 1, 10, ")", "Unmatched ')': there is no matching '('",
                      {TokenType::SEMICOLON});
}

TEST(error_missing_operand_before_semicolon) {
    expectSyntaxError("a = b +;", 1, 8, ";", "Unexpected token ';'", kOperand);
}

TEST(error_missing_right_hand_side) {
    expectSyntaxError("a = ;", 1, 5, ";", "Unexpected token ';'", kOperand);
}

TEST(error_missing_operand_at_end_of_input) {
    expectSyntaxError("a =", 1, 4, "", "Unexpected end of input", kOperand);
}

TEST(error_operator_without_left_operand) {
    expectSyntaxError("a = * b;", 1, 5, "*", "Unexpected token '*'", kOperand);
}

TEST(error_adjacent_operators) {
    expectSyntaxError("a = b * / c;", 1, 9, "/", "Unexpected token '/'", kOperand);
}

TEST(error_unary_minus_is_not_in_the_grammar) {
    expectSyntaxError("a = -5;", 1, 5, "-", "Unexpected token '-'", kOperand);
}

TEST(error_empty_parentheses) {
    expectSyntaxError("a = ();", 1, 6, ")", "Unexpected token ')'", kOperand);
}

TEST(error_statement_does_not_start_with_identifier) {
    expectSyntaxError("= b;", 1, 1, "=", "Unexpected token '='",
                      {TokenType::ID, TokenType::END});
    expectSyntaxError("5 = b;", 1, 1, "5", "Unexpected token '5'",
                      {TokenType::ID, TokenType::END});
}

TEST(error_stray_closing_paren_at_statement_start) {
    expectSyntaxError("a = 1; );", 1, 8, ")", "Unexpected token ')'",
                      {TokenType::ID, TokenType::END});
}

TEST(error_missing_assignment_operator) {
    expectSyntaxError("a b = c;", 1, 3, "b", "Unexpected token 'b'", {TokenType::ASSIGN});
    expectSyntaxError("a", 1, 2, "", "Unexpected end of input", {TokenType::ASSIGN});
}

TEST(error_in_a_later_statement_reports_its_own_position) {
    expectSyntaxError("a = 1; b = ;", 1, 12, ";", "Unexpected token ';'", kOperand);
}

TEST(error_parsing_stops_at_the_first_error) {
    const ParseResult result = parseSource("a = * 5; b = + ;");
    CHECK(!result.ok());
    CHECK(result.tree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(result.diagnostics[0].column, 5);
}

TEST(error_nesting_beyond_the_limit_is_reported_not_a_crash) {
    const int tooDeep = kMaxNestingDepth + 1;
    std::string source = "a = ";
    source += std::string(tooDeep, '(') + "b" + std::string(tooDeep, ')') + ";";
    const ParseResult result = parseSource(source);
    CHECK(!result.ok());
    CHECK(result.tree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(result.diagnostics[0].column, 4 + tooDeep);
    CHECK(result.diagnostics[0].message.find("nested too deeply") != std::string::npos);
}

TEST(error_token_stream_without_end_is_rejected) {
    const ParseResult empty = parse({});
    CHECK(!empty.ok());
    CHECK_EQ(empty.diagnostics.size(), 1);

    const std::vector<Token> noEnd = {{TokenType::ID, "a", 1, 1}};
    const ParseResult result = parse(noEnd);
    CHECK(!result.ok());
    CHECK(result.tree == nullptr);
}

TEST(error_every_syntax_error_in_the_suite_has_expected_tokens) {
    const char* const sources[] = {"a = b + * 5;", "a = (b + c;", "a = b c;", "= b;", "a = ;"};
    for (const char* source : sources) {
        const ParseResult result = parseSource(source);
        CHECK(!result.ok());
        CHECK_EQ(result.diagnostics.size(), 1);
        if (result.diagnostics.empty()) continue;
        CHECK(!result.diagnostics[0].expected.empty());
    }
}

// ---- Output formatting ------------------------------------------------------

TEST(format_diagnostic_lists_expected_tokens) {
    const ParseResult result = parseSource("a = b + * 5;");
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK_EQ(formatDiagnostic(result.diagnostics[0]),
             "Syntax error at line 1, column 9: Unexpected token '*'\n"
             "  Expected: identifier, number, or '('");
}

TEST(format_diagnostic_expected_two_and_one_items) {
    const ParseResult two = parseSource("= b;");
    CHECK_EQ(two.diagnostics.size(), 1);
    if (!two.diagnostics.empty()) {
        CHECK_EQ(formatDiagnostic(two.diagnostics[0]),
                 "Syntax error at line 1, column 1: Unexpected token '='\n"
                 "  Expected: identifier or end of input");
    }
    const ParseResult one = parseSource("a b = c;");
    CHECK_EQ(one.diagnostics.size(), 1);
    if (!one.diagnostics.empty()) {
        CHECK_EQ(formatDiagnostic(one.diagnostics[0]),
                 "Syntax error at line 1, column 3: Unexpected token 'b'\n"
                 "  Expected: '='");
    }
}

TEST(format_parse_tree_small_example) {
    const ParseResult result = parseSource("a = 1;");
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(formatParseTree(*result.tree),
             "program\n"
             "`-- statement\n"
             "    |-- ID(a)\n"
             "    |-- ASSIGN(=)\n"
             "    |-- expression\n"
             "    |   `-- term\n"
             "    |       `-- factor\n"
             "    |           `-- NUMBER(1)\n"
             "    `-- SEMICOLON(;)\n");
}

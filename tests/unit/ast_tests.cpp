#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/analyzer.h"
#include "core/ast_builder.h"
#include "core/parser.h"
#include "core/lexer.h"
#include "output/text_printer.h"
#include "test_framework.h"

namespace {

// Complete structure as nested text:
//   Program(Assignment(Identifier(a), Add(Identifier(b), Number(5))))
// Leaves print as Kind(value); a Program with no statements prints "Program()".
std::string shape(const AstNode& node) {
    std::string out = astKindName(node.kind);
    if (node.kind == AstKind::Identifier || node.kind == AstKind::Number) {
        return out + "(" + node.value + ")";
    }
    out += "(";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i > 0) out += ", ";
        out += shape(*node.children[i]);
    }
    return out + ")";
}

// Shape of the AST for `source`, or "ERROR" if analysis failed.
std::string astOf(const std::string& source) {
    const AnalysisResult result = analyze(source);
    return result.ok() ? shape(*result.ast) : "ERROR";
}

// "[first,end)" of a node's token span.
std::string span(const AstNode& node) {
    return "[" + std::to_string(node.firstToken) + "," + std::to_string(node.endToken) + ")";
}

// Wrap a single expression into the program AST text for `r = <expr>;`.
std::string assign(const std::string& value) {
    return "Program(Assignment(Identifier(r), " + value + "))";
}

// Recursively verifies the shape rules every AST must satisfy.
void checkInvariants(const AstNode& node) {
    switch (node.kind) {
        case AstKind::Program:
            for (const auto& child : node.children) CHECK(child->kind == AstKind::Assignment);
            break;
        case AstKind::Assignment:
            CHECK_EQ(node.children.size(), 2);
            if (node.children.size() == 2) CHECK(node.children[0]->kind == AstKind::Identifier);
            break;
        case AstKind::Add:
        case AstKind::Subtract:
        case AstKind::Multiply:
        case AstKind::Divide:
            CHECK_EQ(node.children.size(), 2);
            CHECK(node.value.empty());
            break;
        case AstKind::Identifier:
        case AstKind::Number:
            CHECK(node.children.empty());
            CHECK(!node.value.empty());
            break;
    }
    CHECK(node.firstToken < node.endToken || node.kind == AstKind::Program);
    for (const auto& child : node.children) checkInvariants(*child);
}

}  // namespace

// ---- Baseline examples from the proposal ------------------------------------

TEST(ast_baseline_precedence_sample) {
    CHECK_EQ(astOf("a = b + c * 5;"),
             "Program(Assignment(Identifier(a), "
             "Add(Identifier(b), Multiply(Identifier(c), Number(5)))))");
}

TEST(ast_baseline_parentheses_sample) {
    CHECK_EQ(astOf("result = (x + y) * z;"),
             "Program(Assignment(Identifier(result), "
             "Multiply(Add(Identifier(x), Identifier(y)), Identifier(z))))");
}

// ---- Precedence and associativity ---------------------------------------------

TEST(ast_subtraction_is_left_associative) {
    CHECK_EQ(astOf("r = a - b - c;"),
             assign("Subtract(Subtract(Identifier(a), Identifier(b)), Identifier(c))"));
}

TEST(ast_division_is_left_associative) {
    CHECK_EQ(astOf("r = a / b / c;"),
             assign("Divide(Divide(Identifier(a), Identifier(b)), Identifier(c))"));
}

TEST(ast_addition_chain_is_left_nested) {
    CHECK_EQ(astOf("r = a + b + c + d;"),
             assign("Add(Add(Add(Identifier(a), Identifier(b)), Identifier(c)), Identifier(d))"));
}

TEST(ast_multiplication_binds_tighter_than_addition) {
    CHECK_EQ(astOf("r = a * b + c;"),
             assign("Add(Multiply(Identifier(a), Identifier(b)), Identifier(c))"));
    CHECK_EQ(astOf("r = a + b * c;"),
             assign("Add(Identifier(a), Multiply(Identifier(b), Identifier(c)))"));
}

TEST(ast_mixed_operators_follow_precedence_then_left_to_right) {
    // a - ((b / c) * d) + e  ==>  Add(Subtract(a, Multiply(Divide(b, c), d)), e)
    CHECK_EQ(astOf("r = a - b / c * d + e;"),
             assign("Add(Subtract(Identifier(a), "
                    "Multiply(Divide(Identifier(b), Identifier(c)), Identifier(d))), "
                    "Identifier(e))"));
}

TEST(ast_every_operator_maps_to_its_kind) {
    CHECK_EQ(astOf("r = a + b;"), assign("Add(Identifier(a), Identifier(b))"));
    CHECK_EQ(astOf("r = a - b;"), assign("Subtract(Identifier(a), Identifier(b))"));
    CHECK_EQ(astOf("r = a * b;"), assign("Multiply(Identifier(a), Identifier(b))"));
    CHECK_EQ(astOf("r = a / b;"), assign("Divide(Identifier(a), Identifier(b))"));
}

TEST(ast_parentheses_override_associativity) {
    CHECK_EQ(astOf("r = a - (b - c);"),
             assign("Subtract(Identifier(a), Subtract(Identifier(b), Identifier(c)))"));
}

TEST(ast_parentheses_override_precedence) {
    CHECK_EQ(astOf("r = a * (b + c);"),
             assign("Multiply(Identifier(a), Add(Identifier(b), Identifier(c)))"));
    CHECK_EQ(astOf("r = (a + b) * (c - d);"),
             assign("Multiply(Add(Identifier(a), Identifier(b)), "
                    "Subtract(Identifier(c), Identifier(d)))"));
}

TEST(ast_redundant_parentheses_leave_no_trace) {
    CHECK_EQ(astOf("r = ((a));"), assign("Identifier(a)"));
    CHECK_EQ(astOf("r = (a);"), astOf("r = a;"));
    CHECK_EQ(astOf("r = ((a + b)) * c;"), astOf("r = (a + b) * c;"));
}

TEST(ast_literal_and_identifier_values_are_preserved_verbatim) {
    CHECK_EQ(astOf("r = 007;"), assign("Number(007)"));
    CHECK_EQ(astOf("r = 99999999999999999999999;"), assign("Number(99999999999999999999999)"));
    CHECK_EQ(astOf("_x1 = Total_2;"),
             "Program(Assignment(Identifier(_x1), Identifier(Total_2)))");
}

// ---- Program structure ----------------------------------------------------------

TEST(ast_single_statement_still_has_program_root) {
    const AnalysisResult result = analyze("a = 1;");
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK(result.ast->kind == AstKind::Program);
    CHECK_EQ(result.ast->children.size(), 1);
    CHECK_EQ(shape(*result.ast), "Program(Assignment(Identifier(a), Number(1)))");
}

TEST(ast_multiple_statements_keep_order) {
    CHECK_EQ(astOf("a = 1; b = a + 2;\nc = (b);"),
             "Program(Assignment(Identifier(a), Number(1)), "
             "Assignment(Identifier(b), Add(Identifier(a), Number(2))), "
             "Assignment(Identifier(c), Identifier(b)))");
}

TEST(ast_empty_program_is_a_childless_program_node) {
    for (const char* source : {"", "   ", "\n\t\n"}) {
        const AnalysisResult result = analyze(source);
        CHECK(result.ok());
        if (!result.ok()) continue;
        CHECK(result.ast->kind == AstKind::Program);
        CHECK(result.ast->children.empty());
        CHECK_EQ(shape(*result.ast), "Program()");
    }
}

TEST(ast_has_only_allowed_node_shapes) {
    const char* const sources[] = {
        "a = b + c * 5;", "result = (x + y) * z;", "r = a - b - c;", "a = 1; b = ((a));",
        "x = a - b / c * d + e;", "",
    };
    for (const char* source : sources) {
        const AnalysisResult result = analyze(source);
        CHECK(result.ok());
        if (result.ok()) checkInvariants(*result.ast);
    }
}

// ---- Token spans ------------------------------------------------------------------

TEST(ast_spans_for_precedence_sample) {
    // Tokens: a(0) =(1) b(2) +(3) c(4) *(5) 5(6) ;(7) END(8)
    const AnalysisResult result = analyze("a = b + c * 5;");
    CHECK(result.ok());
    if (!result.ok()) return;
    const AstNode& program = *result.ast;
    CHECK_EQ(span(program), "[0,8)");

    const AstNode& assignment = *program.children[0];
    CHECK_EQ(span(assignment), "[0,8)");  // includes ';'
    CHECK_EQ(span(*assignment.children[0]), "[0,1)");

    const AstNode& add = *assignment.children[1];
    CHECK(add.kind == AstKind::Add);
    CHECK_EQ(span(add), "[2,7)");
    CHECK_EQ(span(*add.children[0]), "[2,3)");

    const AstNode& multiply = *add.children[1];
    CHECK(multiply.kind == AstKind::Multiply);
    CHECK_EQ(span(multiply), "[4,7)");
    CHECK_EQ(span(*multiply.children[0]), "[4,5)");
    CHECK_EQ(span(*multiply.children[1]), "[6,7)");
}

TEST(ast_spans_include_parentheses_on_the_operation_only) {
    // Tokens: r(0) =(1) ((2) x(3) +(4) y(5) )(6) *(7) z(8) ;(9) END(10)
    const AnalysisResult result = analyze("r = (x + y) * z;");
    CHECK(result.ok());
    if (!result.ok()) return;
    const AstNode& assignment = *result.ast->children[0];
    CHECK_EQ(span(*result.ast), "[0,10)");
    CHECK_EQ(span(assignment), "[0,10)");

    const AstNode& multiply = *assignment.children[1];
    CHECK(multiply.kind == AstKind::Multiply);
    CHECK_EQ(span(multiply), "[2,9)");  // '(' through z

    const AstNode& add = *multiply.children[0];
    CHECK(add.kind == AstKind::Add);
    CHECK_EQ(span(add), "[3,6)");       // x + y, without the parentheses
    CHECK_EQ(span(*add.children[0]), "[3,4)");
    CHECK_EQ(span(*add.children[1]), "[5,6)");
    CHECK_EQ(span(*multiply.children[1]), "[8,9)");
}

TEST(ast_spans_of_left_nested_chain_share_the_start) {
    // Tokens: r(0) =(1) a(2) -(3) b(4) -(5) c(6) ;(7)
    const AnalysisResult result = analyze("r = a - b - c;");
    CHECK(result.ok());
    if (!result.ok()) return;
    const AstNode& outer = *result.ast->children[0]->children[1];
    CHECK(outer.kind == AstKind::Subtract);
    CHECK_EQ(span(outer), "[2,7)");
    CHECK_EQ(span(*outer.children[0]), "[2,5)");  // a - b
    CHECK_EQ(span(*outer.children[1]), "[6,7)");  // c
}

TEST(ast_spans_of_second_statement) {
    // Tokens: a(0) =(1) 1(2) ;(3) b(4) =(5) 2(6) ;(7)
    const AnalysisResult result = analyze("a = 1; b = 2;");
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(span(*result.ast->children[0]), "[0,4)");
    CHECK_EQ(span(*result.ast->children[1]), "[4,8)");
}

// ---- Invalid input never yields an AST -----------------------------------------------

TEST(ast_is_never_produced_for_invalid_input) {
    const char* const sources[] = {
        "a = b + * 5;",  // unexpected operator
        "a = (b + c;",   // missing ')'
        "a = b + c);",   // unmatched ')'
        "a = b + c",     // missing ';'
        "= 1;",          // missing target
        "a = ;",         // missing operand
        "a = b $ c;",    // lexical error
        "a = 3.14;",     // lexical error
    };
    for (const char* source : sources) {
        const AnalysisResult result = analyze(source);
        CHECK(!result.ok());
        CHECK(result.ast == nullptr);
        CHECK(result.parseTree == nullptr);
        CHECK(!result.diagnostics.empty());
    }
}

TEST(analysis_reports_lexical_errors_without_parsing) {
    const AnalysisResult result = analyze("a = b $ c;");
    CHECK(!result.tokenized);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK(result.diagnostics[0].phase == Phase::Lexical);
    CHECK_EQ(result.diagnostics[0].column, 7);
}

TEST(analysis_reports_syntax_errors_with_tokens_available) {
    const AnalysisResult result = analyze("a = b + * 5;");
    CHECK(result.tokenized);
    CHECK_EQ(result.tokens.size(), 8);  // a = b + * 5 ; END
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.empty()) return;
    CHECK(result.diagnostics[0].phase == Phase::Syntax);
    CHECK_EQ(result.diagnostics[0].line, 1);
    CHECK_EQ(result.diagnostics[0].column, 9);
    CHECK(!result.diagnostics[0].expected.empty());
}

TEST(analysis_success_exposes_tokens_parse_tree_and_ast) {
    const AnalysisResult result = analyze("a = 1;");
    CHECK(result.ok());
    CHECK(result.tokenized);
    CHECK(result.diagnostics.empty());
    CHECK(result.parseTree != nullptr);
    CHECK(result.ast != nullptr);
    CHECK_EQ(result.tokens.size(), 5);  // a = 1 ; END
}

// ---- AST height limit (kMaxAstHeight) -----------------------------------------
//
// Height counts nodes on the longest root-to-leaf path and the root counts, so
// `r = x1 op ... op xn;` has height n + 2. The limit is an implementation
// safety limit, not a grammar rule; see ast_builder.h.

namespace {

// "r = 1+1+...+1;" with `operands` operands.
std::string chainOf(int operands) {
    std::string source = "r = 1";
    for (int i = 1; i < operands; ++i) source += "+1";
    return source + ";";
}

// Longest root-to-leaf path in nodes (leaf = 1, root counted). Recursive, so
// only call it on trees already known to be within kMaxAstHeight.
int heightOf(const AstNode& node) {
    int tallest = 0;
    for (const auto& child : node.children) tallest = std::max(tallest, heightOf(*child));
    return tallest + 1;
}

// Asserts the diagnostic points at a real place in `source`: line and column
// exist, and `found` is the text that starts at that position.
void checkPositionIsInside(const std::string& source, const Diagnostic& d) {
    std::vector<std::string> lines(1);
    for (char c : source) {
        if (c == '\n') lines.emplace_back();
        else lines.back() += c;
    }
    CHECK(d.line >= 1);
    CHECK(d.line <= static_cast<int>(lines.size()));
    if (d.line < 1 || d.line > static_cast<int>(lines.size())) return;
    const std::string& text = lines[d.line - 1];
    CHECK(d.column >= 1);
    CHECK(d.column <= static_cast<int>(text.size()));  // points at a real character
    if (d.column < 1 || d.column > static_cast<int>(text.size())) return;
    CHECK_EQ(text.substr(d.column - 1, d.found.size()), d.found);
}

std::size_t countLines(const std::string& text) {
    std::size_t lines = 0;
    for (char c : text) {
        if (c == '\n') ++lines;
    }
    return lines;
}

}  // namespace

TEST(height_limit_constant_documents_the_expected_boundary) {
    // The boundary tests below are written for this value; if it changes they
    // must be revisited deliberately.
    CHECK_EQ(kMaxAstHeight, 1000);
}

TEST(height_just_below_the_limit_is_accepted) {
    const AnalysisResult result = analyze(chainOf(kMaxAstHeight - 3));
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(heightOf(*result.ast), kMaxAstHeight - 1);
}

TEST(height_exactly_at_the_limit_is_accepted_and_counts_the_root) {
    // 998 operands = 997 Adds + Assignment + Program = 1000 including the root.
    const AnalysisResult result = analyze(chainOf(kMaxAstHeight - 2));
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(heightOf(*result.ast), kMaxAstHeight);
    CHECK(result.diagnostics.empty());
    CHECK(result.parseTree != nullptr);
}

TEST(height_one_above_the_limit_is_rejected) {
    // 999 operands would be height 1001.
    const std::string source = chainOf(kMaxAstHeight - 1);
    const AnalysisResult result = analyze(source);
    CHECK(!result.ok());
    CHECK(result.ast == nullptr);
    CHECK(result.parseTree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
}

TEST(height_over_limit_diagnostic_is_controlled_and_positioned) {
    const std::string source = chainOf(kMaxAstHeight - 1);
    const AnalysisResult result = analyze(source);
    CHECK(result.tokenized);  // lexing and parsing succeeded; only the AST limit failed
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.size() != 1) return;
    const Diagnostic& d = result.diagnostics[0];
    CHECK(d.phase == Phase::Syntax);
    CHECK(d.message.find("too complex") != std::string::npos);
    CHECK(d.message.find(std::to_string(kMaxAstHeight)) != std::string::npos);
    CHECK(d.expected.empty());
    // Only the Program node exceeded the limit, so the tallest subtree is the
    // statement, which starts at line 1, column 1.
    CHECK_EQ(d.line, 1);
    CHECK_EQ(d.column, 1);
    CHECK_EQ(d.found, "r");
    checkPositionIsInside(source, d);
}

TEST(height_far_over_limit_points_at_the_start_of_the_chain) {
    const std::string source = chainOf(kMaxAstHeight + 50);
    const AnalysisResult result = analyze(source);
    CHECK(!result.ok());
    CHECK(result.ast == nullptr);
    CHECK(result.parseTree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.size() != 1) return;
    const Diagnostic& d = result.diagnostics[0];
    CHECK_EQ(d.line, 1);
    CHECK_EQ(d.column, 5);  // the first operand of the chain
    CHECK_EQ(d.found, "1");
    checkPositionIsInside(source, d);
}

TEST(height_over_limit_in_a_later_statement_points_at_that_statement) {
    const std::string source = "a = 1;\n  b = 2;\n" + chainOf(kMaxAstHeight - 1);
    const AnalysisResult result = analyze(source);
    CHECK(!result.ok());
    CHECK(result.ast == nullptr);
    CHECK(result.parseTree == nullptr);
    CHECK_EQ(result.diagnostics.size(), 1);
    if (result.diagnostics.size() != 1) return;
    const Diagnostic& d = result.diagnostics[0];
    CHECK_EQ(d.line, 3);
    CHECK_EQ(d.column, 1);
    checkPositionIsInside(source, d);
}

TEST(height_limit_applies_per_statement_not_to_their_sum) {
    // Two statements that are each exactly at the limit: Program's height is
    // 1 + the tallest statement, not the total.
    const std::string source = chainOf(kMaxAstHeight - 2) + "\n" + chainOf(kMaxAstHeight - 2);
    const AnalysisResult result = analyze(source);
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(result.ast->children.size(), 2);
    CHECK_EQ(heightOf(*result.ast), kMaxAstHeight);
}

TEST(height_parentheses_do_not_add_height) {
    CHECK_EQ(astOf("r = ((((a))));"), assign("Identifier(a)"));
    // An AST of 3 levels regardless of how many parentheses wrap the operand.
    const AnalysisResult result = analyze("r = ((((a))));");
    CHECK(result.ok());
    if (result.ok()) CHECK_EQ(heightOf(*result.ast), 3);
}

TEST(height_very_long_chain_is_rejected_without_crashing) {
    const AnalysisResult result = analyze(chainOf(200000));
    CHECK(!result.ok());
    CHECK(result.ast == nullptr);
    CHECK(result.parseTree == nullptr);
}

// Evidence for the recursion review (see ast_builder.h): the operations that
// recurse over the AST are exercised at the full accepted height in this test
// build (unoptimised, default stack). This shows they work at the limit on this
// configuration; it is not a proof for other stack sizes.
TEST(height_at_limit_survives_formatting_and_destruction) {
    {
        const AnalysisResult result = analyze(chainOf(kMaxAstHeight - 2));
        CHECK(result.ok());
        if (!result.ok()) return;
        const std::string text = formatAst(*result.ast);
        // One line per node: 998 numbers + 997 adds + r + Assignment + Program.
        CHECK_EQ(countLines(text), 998 + 997 + 3);
        CHECK(text.rfind("Program\n", 0) == 0);
    }  // the 1000-level AST is destroyed here
    CHECK(true);
}

TEST(parse_tree_at_the_nesting_limit_survives_formatting_and_destruction) {
    std::string source = "a = ";
    source += std::string(kMaxNestingDepth, '(') + "b" + std::string(kMaxNestingDepth, ')') + ";";
    {
        const AnalysisResult result = analyze(source);
        CHECK(result.ok());
        if (!result.ok()) return;
        CHECK(!formatParseTree(*result.parseTree).empty());
        CHECK_EQ(shape(*result.ast), "Program(Assignment(Identifier(a), Identifier(b)))");
    }
    CHECK(true);
}

// ---- Builder contract -------------------------------------------------------------------------

TEST(builder_rejects_malformed_parse_trees) {
    // Root that is not a program.
    ParseNode notProgram;
    notProgram.symbol = ParseSymbol::Statement;
    bool threw = false;
    try {
        buildAst(notProgram);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    // Program containing something other than a statement.
    ParseNode program;
    program.symbol = ParseSymbol::Program;
    program.children.push_back(std::make_unique<ParseNode>());
    program.children[0]->symbol = ParseSymbol::Term;
    threw = false;
    try {
        buildAst(program);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(builder_works_directly_on_a_parse_result) {
    const LexResult lexed = lex("a = b - c - d;");
    const ParseResult parsed = parse(lexed.tokens);
    CHECK(parsed.ok());
    if (!parsed.ok()) return;
    const AstBuildResult built = buildAst(*parsed.tree);
    CHECK(!built.tooDeep);
    CHECK(built.ast != nullptr);
    if (built.ast == nullptr) return;
    CHECK_EQ(shape(*built.ast),
             "Program(Assignment(Identifier(a), "
             "Subtract(Subtract(Identifier(b), Identifier(c)), Identifier(d))))");
}

// ---- Output formatting ----------------------------------------------------------------------------

TEST(format_ast_precedence_sample) {
    const AnalysisResult result = analyze("a = b + c * 5;");
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(formatAst(*result.ast),
             "Program\n"
             "`-- Assignment\n"
             "    |-- Identifier(a)\n"
             "    `-- Add\n"
             "        |-- Identifier(b)\n"
             "        `-- Multiply\n"
             "            |-- Identifier(c)\n"
             "            `-- Number(5)\n");
}

TEST(format_ast_empty_program) {
    const AnalysisResult result = analyze("");
    CHECK(result.ok());
    if (!result.ok()) return;
    CHECK_EQ(formatAst(*result.ast), "Program\n");
}

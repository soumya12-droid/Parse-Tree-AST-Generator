#include "core/analyzer.h"

#include <utility>

#include "core/ast_builder.h"
#include "core/lexer.h"
#include "core/parser.h"

AnalysisResult analyze(const std::string& source) {
    AnalysisResult result;

    LexResult lexed = lex(source);
    result.tokens = std::move(lexed.tokens);
    if (!lexed.ok()) {
        result.diagnostics = std::move(lexed.diagnostics);
        return result;
    }
    result.tokenized = true;

    ParseResult parsed = parse(result.tokens);
    if (!parsed.ok()) {
        result.diagnostics = std::move(parsed.diagnostics);
        return result;
    }

    AstBuildResult built = buildAst(*parsed.tree);
    if (built.tooDeep) {
        const Token& at = result.tokens[built.tooDeepAt];
        result.diagnostics.push_back(Diagnostic{
            Phase::Syntax,
            "Expression too complex: its syntax tree would be more than " +
                std::to_string(kMaxAstHeight) +
                " levels deep (implementation limit, not a grammar rule)",
            at.line, at.column, at.lexeme, {}, true});
        return result;
    }

    result.parseTree = std::move(parsed.tree);
    result.ast = std::move(built.ast);
    return result;
}

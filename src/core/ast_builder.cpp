#include "core/ast_builder.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using AstPtr = std::unique_ptr<AstNode>;

// A built subtree plus its height, so the depth limit is enforced as we build.
struct Built {
    AstPtr node;
    int height = 0;
};

struct TooDeep {
    std::size_t token;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(std::string("buildAst: malformed parse tree: ") + message);
    }
}

bool isTerminal(const ParseNode& node, TokenType type) {
    return node.symbol == ParseSymbol::Terminal && node.token.type == type;
}

Built makeNode(AstKind kind, std::size_t first, std::size_t end, std::string value,
               std::vector<Built> children) {
    int height = 0;
    std::size_t culprit = first;  // first token of the tallest child
    auto node = std::make_unique<AstNode>();
    node->kind = kind;
    node->value = std::move(value);
    node->firstToken = first;
    node->endToken = end;
    for (Built& child : children) {
        if (child.height > height) {
            height = child.height;
            culprit = child.node->firstToken;
        }
        node->children.push_back(std::move(child.node));
    }
    ++height;
    // Heights are always checked as nodes are created, so no child is ever
    // taller than kMaxAstHeight and the tree is never built past the limit.
    if (height > kMaxAstHeight) throw TooDeep{culprit};
    return Built{std::move(node), height};
}

Built buildExpression(const ParseNode& node);

AstKind binaryKind(TokenType op, bool additive) {
    if (additive) {
        if (op == TokenType::PLUS) return AstKind::Add;
        if (op == TokenType::MINUS) return AstKind::Subtract;
    } else {
        if (op == TokenType::MUL) return AstKind::Multiply;
        if (op == TokenType::DIV) return AstKind::Divide;
    }
    throw std::invalid_argument("buildAst: malformed parse tree: wrong operator for rule");
}

// Folds a flat `operand (op operand)*` node into left-nested binary nodes.
Built buildChain(const ParseNode& node, ParseSymbol self, ParseSymbol operand,
                 Built (*buildOperand)(const ParseNode&), bool additive) {
    require(node.symbol == self, "unexpected node kind");
    require(node.children.size() % 2 == 1, "operator without operand");
    require(node.children[0]->symbol == operand, "bad operand");

    const std::size_t first = node.children[0]->firstToken;
    Built left = buildOperand(*node.children[0]);

    for (std::size_t i = 1; i < node.children.size(); i += 2) {
        const ParseNode& op = *node.children[i];
        const ParseNode& rhs = *node.children[i + 1];
        require(op.symbol == ParseSymbol::Terminal, "operator is not a terminal");
        require(rhs.symbol == operand, "bad operand");

        const AstKind kind = binaryKind(op.token.type, additive);
        Built right = buildOperand(rhs);

        std::vector<Built> operands;
        operands.push_back(std::move(left));
        operands.push_back(std::move(right));
        left = makeNode(kind, first, rhs.endToken, "", std::move(operands));
    }
    return left;
}

// factor -> NUMBER | ID | '(' expression ')'
Built buildFactor(const ParseNode& node) {
    require(node.symbol == ParseSymbol::Factor, "expected factor");

    if (node.children.size() == 1) {
        const ParseNode& leaf = *node.children[0];
        require(leaf.symbol == ParseSymbol::Terminal, "factor leaf is not a terminal");
        if (leaf.token.type == TokenType::ID) {
            return makeNode(AstKind::Identifier, leaf.firstToken, leaf.endToken,
                            leaf.token.lexeme, {});
        }
        if (leaf.token.type == TokenType::NUMBER) {
            return makeNode(AstKind::Number, leaf.firstToken, leaf.endToken, leaf.token.lexeme,
                            {});
        }
        require(false, "factor leaf must be ID or NUMBER");
    }

    require(node.children.size() == 3 && isTerminal(*node.children[0], TokenType::LPAREN) &&
                node.children[1]->symbol == ParseSymbol::Expression &&
                isTerminal(*node.children[2], TokenType::RPAREN),
            "bad parenthesised factor");
    return buildExpression(*node.children[1]);  // parentheses leave no node
}

// term -> factor (('*' | '/') factor)*
Built buildTerm(const ParseNode& node) {
    return buildChain(node, ParseSymbol::Term, ParseSymbol::Factor, buildFactor, false);
}

// expression -> term (('+' | '-') term)*
Built buildExpression(const ParseNode& node) {
    return buildChain(node, ParseSymbol::Expression, ParseSymbol::Term, buildTerm, true);
}

// statement -> ID '=' expression ';'
Built buildStatement(const ParseNode& node) {
    require(node.symbol == ParseSymbol::Statement && node.children.size() == 4 &&
                isTerminal(*node.children[0], TokenType::ID) &&
                isTerminal(*node.children[1], TokenType::ASSIGN) &&
                node.children[2]->symbol == ParseSymbol::Expression &&
                isTerminal(*node.children[3], TokenType::SEMICOLON),
            "bad statement");

    const ParseNode& target = *node.children[0];
    std::vector<Built> parts;
    parts.push_back(makeNode(AstKind::Identifier, target.firstToken, target.endToken,
                             target.token.lexeme, {}));
    parts.push_back(buildExpression(*node.children[2]));
    return makeNode(AstKind::Assignment, node.firstToken, node.endToken, "", std::move(parts));
}

}  // namespace

AstBuildResult buildAst(const ParseNode& root) {
    require(root.symbol == ParseSymbol::Program, "root is not a program");

    AstBuildResult result;
    try {
        std::vector<Built> statements;
        for (const auto& child : root.children) {
            statements.push_back(buildStatement(*child));
        }
        Built program = makeNode(AstKind::Program, root.firstToken, root.endToken, "",
                                 std::move(statements));
        result.ast = std::move(program.node);
    } catch (const TooDeep& deep) {
        result.ast.reset();
        result.tooDeep = true;
        result.tooDeepAt = deep.token;
    }
    return result;
}

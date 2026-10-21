#include "output/json_writer.h"

#include <cstdio>
#include <vector>

#include "core/ast.h"
#include "core/parse_tree.h"

void appendJsonString(std::string& out, const std::string& text) {
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

bool isValidUtf8(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        if (c < 0x80) extra = 0;
        else if (c >= 0xC2 && c <= 0xDF) extra = 1;
        else if (c >= 0xE0 && c <= 0xEF) extra = 2;
        else if (c >= 0xF0 && c <= 0xF4) extra = 3;
        else return false;
        if (i + extra >= text.size()) return false;  // truncated sequence
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if (next < 0x80 || next > 0xBF) return false;
        }
        i += extra + 1;
    }
    return true;
}

namespace {

void writeToken(std::string& out, const Token& token, std::size_t index) {
    out += "{\"index\":" + std::to_string(index) + ",\"type\":\"" + tokenTypeName(token.type) +
           "\",\"lexeme\":";
    appendJsonString(out, token.lexeme);
    out += ",\"line\":" + std::to_string(token.line) +
           ",\"column\":" + std::to_string(token.column) + "}";
}

void writeSpan(std::string& out, std::size_t first, std::size_t end) {
    out += "\"firstToken\":" + std::to_string(first) + ",\"endToken\":" + std::to_string(end);
}

void writeParseNode(std::string& out, const ParseNode& node, const std::vector<Token>& tokens,
                    int& nextId) {
    out += "{\"id\":\"p" + std::to_string(nextId++) + "\",\"symbol\":\"" +
           parseSymbolName(node.symbol) + "\",";
    writeSpan(out, node.firstToken, node.endToken);
    if (node.symbol == ParseSymbol::Terminal) {
        out += ",\"token\":";
        writeToken(out, tokens[node.firstToken], node.firstToken);
    }
    out += ",\"children\":[";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i > 0) out += ',';
        writeParseNode(out, *node.children[i], tokens, nextId);
    }
    out += "]}";
}

void writeAstNode(std::string& out, const AstNode& node, int& nextId) {
    out += "{\"id\":\"a" + std::to_string(nextId++) + "\",\"kind\":\"" + astKindName(node.kind) +
           "\",";
    if (node.kind == AstKind::Identifier || node.kind == AstKind::Number) {
        out += "\"value\":";
        appendJsonString(out, node.value);
        out += ',';
    }
    writeSpan(out, node.firstToken, node.endToken);
    out += ",\"children\":[";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i > 0) out += ',';
        writeAstNode(out, *node.children[i], nextId);
    }
    out += "]}";
}

const char* diagnosticKind(const Diagnostic& d) {
    if (d.phase == Phase::Lexical) return "lexical";
    if (d.implementationLimit) return "limit";
    return "syntax";
}

}  // namespace

std::string analysisToJson(const std::string& source, const AnalysisResult& result) {
    std::string out = "{\"contractVersion\":" + std::to_string(kContractVersion) + ",\"source\":";
    appendJsonString(out, source);
    out += std::string(",\"ok\":") + (result.ok() ? "true" : "false");
    out += std::string(",\"tokenized\":") + (result.tokenized ? "true" : "false");

    out += ",\"tokens\":[";
    for (std::size_t i = 0; i < result.tokens.size(); ++i) {
        if (i > 0) out += ',';
        writeToken(out, result.tokens[i], i);
    }
    out += "],\"diagnostics\":[";
    for (std::size_t i = 0; i < result.diagnostics.size(); ++i) {
        const Diagnostic& d = result.diagnostics[i];
        if (i > 0) out += ',';
        out += std::string("{\"phase\":\"") + (d.phase == Phase::Lexical ? "lexical" : "syntax") +
               "\",\"kind\":\"" + diagnosticKind(d) + "\",\"message\":";
        appendJsonString(out, d.message);
        out += ",\"line\":" + std::to_string(d.line) + ",\"column\":" + std::to_string(d.column) +
               ",\"found\":";
        appendJsonString(out, d.found);
        out += ",\"expected\":[";
        for (std::size_t k = 0; k < d.expected.size(); ++k) {
            if (k > 0) out += ',';
            out += std::string("\"") + tokenTypeName(d.expected[k]) + "\"";
        }
        out += "]}";
    }
    out += "],\"parseTree\":";
    if (result.parseTree) {
        int id = 0;
        writeParseNode(out, *result.parseTree, result.tokens, id);
    } else {
        out += "null";
    }
    out += ",\"ast\":";
    if (result.ast) {
        int id = 0;
        writeAstNode(out, *result.ast, id);
    } else {
        out += "null";
    }
    out += "}";
    return out;
}

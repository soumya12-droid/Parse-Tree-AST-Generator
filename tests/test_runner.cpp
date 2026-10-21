// Test runner: executes all registered unit tests, then the golden-file cases.
//
//   ptagen_tests [cases-directory]     (default: tests/cases)
//
// A golden case is NAME.src (input) plus at least one expected-output file:
//   NAME.tokens  the token listing, or the lexical diagnostics if lexing fails
//   NAME.ptree   the parse tree, or the lexical/syntax diagnostics on failure
//   NAME.ast     the AST, or the lexical/syntax diagnostics on failure
// Expected output is compared against formatTokens() / formatParseTree() /
// formatAst() / formatDiagnostics(). Line endings and trailing whitespace in the expected
// file are ignored.
//
// Exit code: 0 if every test passed, 1 otherwise.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "core/analyzer.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "output/text_printer.h"
#include "test_framework.h"

namespace fs = std::filesystem;

namespace {

bool readBinary(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string normalize(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c != '\r') out += c;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

std::string tokensOutput(const std::string& source) {
    const LexResult lexed = lex(source);
    return lexed.ok() ? formatTokens(lexed.tokens) : formatDiagnostics(lexed.diagnostics);
}

std::string parseTreeOutput(const std::string& source) {
    const LexResult lexed = lex(source);
    if (!lexed.ok()) return formatDiagnostics(lexed.diagnostics);
    const ParseResult parsed = parse(lexed.tokens);
    return parsed.ok() ? formatParseTree(*parsed.tree) : formatDiagnostics(parsed.diagnostics);
}

std::string astOutput(const std::string& source) {
    const AnalysisResult result = analyze(source);
    return result.ok() ? formatAst(*result.ast) : formatDiagnostics(result.diagnostics);
}

struct GoldenKind {
    const char* extension;
    std::string (*produce)(const std::string&);
};

const GoldenKind kKinds[] = {
    {".tokens", tokensOutput},
    {".ptree", parseTreeOutput},
    {".ast", astOutput},
};

// Runs every golden check in `dir`; `total` is the number of checks run.
// Returns the number that failed.
int runGoldenCases(const fs::path& dir, int& total) {
    total = 0;
    int failed = 0;
    if (!fs::is_directory(dir)) {
        std::cout << "FAIL  golden cases: directory not found: " << dir.string() << "\n";
        return 1;
    }

    std::vector<fs::path> sources;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".src") sources.push_back(entry.path());
    }
    std::sort(sources.begin(), sources.end());

    for (const fs::path& srcPath : sources) {
        const std::string name = srcPath.stem().string();
        std::string source;
        if (!readBinary(srcPath, source)) {
            ++total;
            ++failed;
            std::cout << "FAIL  golden/" << name << " (cannot read .src)\n";
            continue;
        }

        int checks = 0;
        for (const GoldenKind& kind : kKinds) {
            fs::path expectedPath = srcPath;
            expectedPath.replace_extension(kind.extension);
            if (!fs::exists(expectedPath)) continue;

            ++checks;
            ++total;
            std::string expected;
            const std::string label = "golden/" + name + kind.extension;
            if (!readBinary(expectedPath, expected)) {
                std::cout << "FAIL  " << label << " (cannot read expected file)\n";
                ++failed;
                continue;
            }
            const std::string actual = normalize(kind.produce(source));
            if (actual == normalize(expected)) {
                std::cout << "PASS  " << label << "\n";
            } else {
                std::cout << "FAIL  " << label << "\n    expected:\n" << normalize(expected)
                          << "\n    actual:\n" << actual << "\n";
                ++failed;
            }
        }

        if (checks == 0) {
            ++total;
            ++failed;
            std::cout << "FAIL  golden/" << name << " (no .tokens, .ptree or .ast file)\n";
        }
    }

    if (sources.empty()) {
        std::cout << "FAIL  golden cases: no .src files found in " << dir.string() << "\n";
        ++total;
        return 1;
    }
    return failed;
}

}  // namespace

int main(int argc, char* argv[]) {
    int failed = 0;
    int total = 0;

    for (const TestCase& test : testRegistry()) {
        currentFailures() = 0;
        test.run();
        ++total;
        if (currentFailures() == 0) {
            std::cout << "PASS  " << test.name << "\n";
        } else {
            std::cout << "FAIL  " << test.name << "\n";
            ++failed;
        }
    }

    int goldenTotal = 0;
    const fs::path casesDir = argc > 1 ? fs::path(argv[1]) : fs::path("tests") / "cases";
    failed += runGoldenCases(casesDir, goldenTotal);
    total += goldenTotal;

    std::cout << "\n" << (total - failed) << " of " << total << " tests passed";
    if (failed > 0) std::cout << ", " << failed << " FAILED";
    std::cout << "\n";
    return failed == 0 ? 0 : 1;
}

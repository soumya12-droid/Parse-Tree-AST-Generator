// Parse Tree and AST Generator - command-line entry point.
// Command-line entry point: analyze a program (tokens, parse tree, AST, errors)
// or serve the web UI and the analysis API on localhost.

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "core/analyzer.h"
#include "output/text_printer.h"
#include "server/serve_command.h"

namespace {

constexpr const char* kName = "ptagen";
constexpr const char* kVersion = "0.5.0-phase5";

enum ExitCode { kOk = 0, kInputErrors = 1, kUsageOrIoError = 2 };

void printBanner() {
    std::cout << kName << " " << kVersion << "\n"
              << "Parse Tree and AST Generator (C++" << __cplusplus / 100 % 100 << ")\n"
              << "Lexer, parser (parse tree), AST builder and local analysis server.\n";
}

void printUsage() {
    std::cout << "\nUsage:\n"
              << "  ptagen                       Show the version banner\n"
              << "  ptagen --version             Show the version banner\n"
              << "  ptagen --help                Show this help\n"
              << "  ptagen analyze --text \"SRC\"  Show tokens, parse tree and AST for the text\n"
              << "  ptagen analyze FILE          Show tokens, parse tree and AST for FILE\n";
    printServeUsage();
    std::cout << "\nExit codes: 0 success, 1 lexical or syntax errors, 2 usage or I/O error.\n";
}

bool readFile(const std::string& path, std::string& contents) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

int runAnalysis(const std::string& source) {
    const AnalysisResult result = analyze(source);

    if (!result.tokenized) {
        std::cout << "Lexical errors (" << result.diagnostics.size() << "):\n"
                  << formatDiagnostics(result.diagnostics);
        return kInputErrors;
    }
    std::cout << "Tokens (" << result.tokens.size() << "):\n" << formatTokens(result.tokens);

    if (!result.ok()) {
        std::cout << "\nSyntax error:\n" << formatDiagnostics(result.diagnostics);
        return kInputErrors;
    }
    std::cout << "\nParse tree:\n" << formatParseTree(*result.parseTree)
              << "\nAST:\n" << formatAst(*result.ast);
    return kOk;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 1) {
        printBanner();
        return kOk;
    }

    const std::string command = argv[1];

    if (command == "--version" || command == "-v") {
        printBanner();
        return kOk;
    }

    if (command == "--help" || command == "-h") {
        printBanner();
        printUsage();
        return kOk;
    }

    if (command == "serve") {
        return runServeCommand(std::vector<std::string>(argv + 2, argv + argc), argv[0]);
    }

    if (command == "analyze") {
        if (argc == 4 && std::string(argv[2]) == "--text") {
            return runAnalysis(argv[3]);
        }
        if (argc == 3 && std::string(argv[2]).rfind("--", 0) != 0) {
            std::string source;
            if (!readFile(argv[2], source)) {
                std::cerr << "error: cannot read file '" << argv[2] << "'\n";
                return kUsageOrIoError;
            }
            return runAnalysis(source);
        }
        std::cerr << "error: 'analyze' needs --text \"SRC\" or a FILE\n";
        printUsage();
        return kUsageOrIoError;
    }

    std::cerr << "error: unknown command '" << command << "'\n";
    printUsage();
    return kUsageOrIoError;
}

// Parse Tree and AST Generator - Phase 0 placeholder.
// No compiler functionality is implemented yet; this only proves the toolchain works.

#include <iostream>

namespace {
constexpr const char* kName = "ptagen";
constexpr const char* kVersion = "0.0.0-phase0";
}  // namespace

int main() {
    std::cout << kName << " " << kVersion << "\n"
              << "Parse Tree and AST Generator (C++" << __cplusplus / 100 % 100 << ")\n"
              << "Phase 0: toolchain check only. No lexer, parser or AST yet.\n";
    return 0;
}

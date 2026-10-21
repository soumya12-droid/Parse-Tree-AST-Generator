#pragma once

// Minimal test framework: TEST(name) registers a test, CHECK / CHECK_EQ record
// failures without aborting, and the runner's exit code reflects the result.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*run)();
};

inline std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> tests;
    return tests;
}

// Failed checks in the test that is currently running.
inline int& currentFailures() {
    static int failures = 0;
    return failures;
}

struct TestRegistrar {
    TestRegistrar(const char* name, void (*run)()) { testRegistry().push_back({name, run}); }
};

#define TEST(name)                                          \
    static void name();                                     \
    static TestRegistrar registrar_##name(#name, name);     \
    static void name()

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            ++currentFailures();                                                     \
            std::cout << "    " << __FILE__ << ":" << __LINE__                       \
                      << ": CHECK failed: " #condition "\n";                         \
        }                                                                            \
    } while (0)

#define CHECK_EQ(actual, expected)                                                   \
    do {                                                                             \
        std::ostringstream actualStream_, expectedStream_;                           \
        actualStream_ << (actual);                                                   \
        expectedStream_ << (expected);                                               \
        if (actualStream_.str() != expectedStream_.str()) {                          \
            ++currentFailures();                                                     \
            std::cout << "    " << __FILE__ << ":" << __LINE__                       \
                      << ": CHECK_EQ failed: " #actual "\n      expected: "          \
                      << expectedStream_.str() << "\n      actual:   "               \
                      << actualStream_.str() << "\n";                                \
        }                                                                            \
    } while (0)

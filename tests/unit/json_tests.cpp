#include <string>

#include "core/analyzer.h"
#include "core/ast_builder.h"
#include "core/parser.h"
#include "output/json_writer.h"
#include "server/json_request.h"
#include "test_framework.h"

namespace {

std::string jsonOf(const std::string& source) { return analysisToJson(source, analyze(source)); }

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string quoted(const std::string& s) {
    std::string out;
    appendJsonString(out, s);
    return out;
}

AnalyzeRequest req(const std::string& body) { return parseAnalyzeRequest(body); }

}  // namespace

// ---- json_writer ---------------------------------------------------------------------

TEST(json_string_escaping) {
    CHECK_EQ(quoted("plain"), "\"plain\"");
    CHECK_EQ(quoted("a\"b\\c"), "\"a\\\"b\\\\c\"");
    CHECK_EQ(quoted("l1\nl2\r\n\t"), "\"l1\\nl2\\r\\n\\t\"");
    CHECK_EQ(quoted(std::string("a\0b", 3)), "\"a\\u0000b\"");
    CHECK_EQ(quoted("\x01\x1f"), "\"\\u0001\\u001f\"");
    CHECK_EQ(quoted("\xC3\xA9"), "\"\xC3\xA9\"");  // UTF-8 passes through
}

TEST(utf8_validation) {
    CHECK(isValidUtf8(""));
    CHECK(isValidUtf8("ascii"));
    CHECK(isValidUtf8("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
    CHECK(!isValidUtf8("\xC3"));          // truncated
    CHECK(!isValidUtf8("\xFF"));          // invalid lead
    CHECK(!isValidUtf8("\xC3\x28"));      // bad continuation
    CHECK(!isValidUtf8("\x80"));          // stray continuation
}

TEST(json_empty_program_exact_output) {
    CHECK_EQ(jsonOf(""),
             "{\"contractVersion\":1,\"source\":\"\",\"ok\":true,\"tokenized\":true,"
             "\"tokens\":[{\"index\":0,\"type\":\"END\",\"lexeme\":\"\",\"line\":1,\"column\":1}],"
             "\"diagnostics\":[],"
             "\"parseTree\":{\"id\":\"p0\",\"symbol\":\"program\",\"firstToken\":0,\"endToken\":0,\"children\":[]},"
             "\"ast\":{\"id\":\"a0\",\"kind\":\"Program\",\"firstToken\":0,\"endToken\":0,\"children\":[]}}");
}

TEST(json_baseline_contains_tokens_spans_and_nested_ast) {
    const std::string j = jsonOf("a = b + c * 5;");
    CHECK(contains(j, "\"ok\":true,\"tokenized\":true"));
    CHECK(contains(j, "{\"index\":5,\"type\":\"MUL\",\"lexeme\":\"*\",\"line\":1,\"column\":11}"));
    CHECK(contains(j, "{\"index\":8,\"type\":\"END\",\"lexeme\":\"\",\"line\":1,\"column\":15}"));
    // Add spans [2,7) and contains Multiply [4,7) as its second child.
    CHECK(contains(j, "\"kind\":\"Add\",\"firstToken\":2,\"endToken\":7"));
    CHECK(contains(j, "\"kind\":\"Multiply\",\"firstToken\":4,\"endToken\":7"));
    CHECK(j.find("\"kind\":\"Add\"") < j.find("\"kind\":\"Multiply\""));
    CHECK(contains(j, "\"diagnostics\":[]"));
}

TEST(json_failures_never_carry_trees) {
    const char* const sources[] = {"a = b + * 5;", "a = (b + c;", "a = b $ c;", "a = b + c", "= 1;"};
    for (const char* source : sources) {
        const std::string j = jsonOf(source);
        CHECK(contains(j, "\"ok\":false"));
        CHECK(contains(j, "\"parseTree\":null,\"ast\":null}"));
        CHECK(!contains(j, "\"id\":\"p0\""));
        CHECK(!contains(j, "\"id\":\"a0\""));
    }
}

TEST(json_diagnostic_fields_and_kinds) {
    const std::string syntax = jsonOf("a = b + * 5;");
    CHECK(contains(syntax,
        "{\"phase\":\"syntax\",\"kind\":\"syntax\",\"message\":\"Unexpected token '*'\","
        "\"line\":1,\"column\":9,\"found\":\"*\",\"expected\":[\"ID\",\"NUMBER\",\"LPAREN\"]}"));

    const std::string lexical = jsonOf("a = b $ c;");
    CHECK(contains(lexical, "\"tokenized\":false"));
    CHECK(contains(lexical, "{\"phase\":\"lexical\",\"kind\":\"lexical\""));
    CHECK(contains(lexical, "\"expected\":[]}"));

    const std::string eof = jsonOf("a = b + c");
    CHECK(contains(eof, "\"found\":\"\""));
}

TEST(json_limit_kinds_only_for_implementation_limits) {
    const std::string deep = "a = " + std::string(kMaxNestingDepth + 1, '(') + "b" +
                             std::string(kMaxNestingDepth + 1, ')') + ";";
    CHECK(contains(jsonOf(deep), "\"phase\":\"syntax\",\"kind\":\"limit\""));

    std::string chain = "r = 1";
    for (int i = 1; i < kMaxAstHeight - 1; ++i) chain += "+1";
    CHECK(contains(jsonOf(chain + ";"), "\"kind\":\"limit\""));

    CHECK(!contains(jsonOf("a = b + * 5;"), "\"kind\":\"limit\""));
    CHECK(!contains(jsonOf("a = b $ c;"), "\"kind\":\"limit\""));
}

TEST(json_source_with_special_characters_round_trips_through_the_request_parser) {
    const std::string source = std::string("a = 1;\r\n\t\"x\\y\"\n\xC3\xA9 ", 17) + std::string("\0", 1);
    const std::string body = "{\"source\":" + quoted(source) + "}";
    const AnalyzeRequest r = req(body);
    CHECK(r.ok);
    CHECK(r.source == source);
}

// ---- request parser -------------------------------------------------------------------------

TEST(request_valid_minimal) {
    const AnalyzeRequest r = req("{\"source\":\"a = 1;\"}");
    CHECK(r.ok);
    CHECK_EQ(r.source, "a = 1;");
}

TEST(request_whitespace_extra_members_and_nesting_are_accepted) {
    const AnalyzeRequest r = req(" \n\t{ \"x\" : [1, 2.5e3, -0.1, true, false, null, {\"k\": [ ]}],\n"
                                 "  \"source\" : \"q\" , \"y\":{} }\r\n");
    CHECK(r.ok);
    CHECK_EQ(r.source, "q");
}

TEST(request_escapes_are_decoded) {
    CHECK_EQ(req("{\"source\":\"a\\nb\\t\\\"\\\\\\/\\b\\f\\r\"}").source, "a\nb\t\"\\/\b\f\r");
    CHECK_EQ(req("{\"source\":\"\\u00e9\"}").source, "\xC3\xA9");
    CHECK_EQ(req("{\"source\":\"\\u20ac\"}").source, "\xE2\x82\xAC");
    CHECK_EQ(req("{\"source\":\"\\ud83d\\ude00\"}").source, "\xF0\x9F\x98\x80");  // surrogate pair
    const AnalyzeRequest nul = req("{\"source\":\"a\\u0000b\"}");
    CHECK(nul.ok);
    CHECK(nul.source == std::string("a\0b", 3));
}

TEST(request_empty_source_is_valid) {
    const AnalyzeRequest r = req("{\"source\":\"\"}");
    CHECK(r.ok);
    CHECK(r.source.empty());
}

TEST(request_rejects_invalid_json) {
    const char* const bad[] = {
        "", "   ", "{", "}", "{\"source\":}", "{\"source\":\"a\"", "{\"source\":\"a\",}",
        "{source:\"a\"}", "{'source':'a'}", "{\"source\":\"a\"} x", "{\"source\":\"a\"}{}",
        "{\"source\":\"a\" \"b\":1}", "{\"source\":\"a\\q\"}", "{\"source\":\"a\\u12\"}",
        "{\"source\":\"a\\uZZZZ\"}", "{\"source\":\"unterminated}", "{\"source\":\"a\nb\"}",
        "{\"source\":\"a\tb\"}", "{\"n\":01,\"source\":\"a\"}", "{\"n\":.5,\"source\":\"a\"}",
        "{\"n\":1.,\"source\":\"a\"}", "{\"n\":1e,\"source\":\"a\"}", "{\"n\":-,\"source\":\"a\"}",
        "{\"n\":tru,\"source\":\"a\"}", "{\"n\":[1,],\"source\":\"a\"}", "{\"n\":[1 2],\"source\":\"a\"}",
        "NaN", "{\"source\":\"a\"}\x01",
    };
    for (const char* body : bad) {
        const AnalyzeRequest r = req(body);
        CHECK(!r.ok);
        CHECK(!r.error.empty());
        CHECK(r.source.empty());
    }
}

TEST(request_rejects_wrong_shapes) {
    CHECK(!req("[]").ok);
    CHECK(!req("\"source\"").ok);
    CHECK(!req("42").ok);
    CHECK(!req("null").ok);
    CHECK(!req("{}").ok);
    CHECK(!req("{\"other\":\"a\"}").ok);
    CHECK(!req("{\"source\":1}").ok);
    CHECK(!req("{\"source\":null}").ok);
    CHECK(!req("{\"source\":[\"a\"]}").ok);
    CHECK(!req("{\"source\":{\"a\":1}}").ok);
    CHECK(!req("{\"source\":\"a\",\"source\":\"b\"}").ok);
    CHECK(contains(req("{}").error, "missing"));
    CHECK(contains(req("{\"source\":1}").error, "must be a string"));
}

TEST(request_rejects_lone_surrogates_and_bad_utf8) {
    CHECK(!req("{\"source\":\"\\ud83d\"}").ok);
    CHECK(!req("{\"source\":\"\\ud83dx\"}").ok);
    CHECK(!req("{\"source\":\"\\ude00\"}").ok);
    CHECK(!req("{\"source\":\"\\ud83d\\u0041\"}").ok);
    CHECK(!req("{\"source\":\"\xFF\"}").ok);
    CHECK(!req("{\"source\":\"\xC3\"}").ok);
    CHECK(!req("\xEF\xBB\xBF{\"source\":\"a\"}").ok);  // a BOM is not JSON whitespace
}

TEST(request_limits_nesting_depth) {
    std::string ok = "{\"x\":";
    for (int i = 0; i < kMaxJsonDepth - 2; ++i) ok += "[";
    for (int i = 0; i < kMaxJsonDepth - 2; ++i) ok += "]";
    ok += ",\"source\":\"a\"}";
    CHECK(req(ok).ok);

    std::string deep = "{\"x\":";
    for (int i = 0; i < 100000; ++i) deep += "[";
    const AnalyzeRequest r = req(deep);          // must fail cleanly, not overflow the stack
    CHECK(!r.ok);
}

TEST(request_error_messages_do_not_echo_the_body) {
    const AnalyzeRequest r = req("{\"source\":\"SECRET-PROGRAM-TEXT\" oops}");
    CHECK(!r.ok);
    CHECK(!contains(r.error, "SECRET"));
}

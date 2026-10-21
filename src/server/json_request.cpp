#include "server/json_request.h"

#include <cstddef>

#include "output/json_writer.h"  // isValidUtf8

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    AnalyzeRequest run() {
        AnalyzeRequest request;
        if (!isValidUtf8(s_)) return fail(request, "body is not valid UTF-8");

        skipSpace();
        if (!parseTopLevelObject(request)) return fail(request, error_);
        skipSpace();
        if (pos_ != s_.size()) return fail(request, "unexpected data after the JSON document");
        if (!sawSource_) return fail(request, "missing required member \"source\"");
        request.ok = true;
        return request;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;
    std::string error_;
    bool sawSource_ = false;

    static AnalyzeRequest fail(AnalyzeRequest& request, const std::string& why) {
        request.ok = false;
        request.source.clear();
        request.error = why;
        return request;
    }

    bool err(const std::string& why) {
        if (error_.empty()) error_ = "invalid JSON: " + why + " (byte " + std::to_string(pos_) + ")";
        return false;
    }

    void skipSpace() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool eat(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool literal(const char* word) {
        std::size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(pos_, n, word) != 0) return err("unexpected token");
        pos_ += n;
        return true;
    }

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool hex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return err("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int v = hexValue(s_[pos_ + i]);
            if (v < 0) return err("bad \\u escape");
            out = out * 16 + static_cast<unsigned>(v);
        }
        pos_ += 4;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // Parses a string literal starting at the opening quote; decoded text goes to `out`.
    bool parseString(std::string& out) {
        if (!eat('"')) return err("expected a string");
        while (pos_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) { --pos_; return err("control character in string"); }
            if (c != '\\') { out += static_cast<char>(c); continue; }

            if (pos_ >= s_.size()) return err("truncated escape");
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {          // high surrogate: needs a low one
                        if (!(pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u')) {
                            return err("lone surrogate");
                        }
                        pos_ += 2;
                        unsigned low = 0;
                        if (!hex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return err("lone surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return err("lone surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    --pos_;
                    return err("invalid escape");
            }
        }
        return err("unterminated string");
    }

    bool parseNumber() {
        const std::size_t start = pos_;
        eat('-');
        if (eat('0')) {
            // no leading zeros
        } else if (pos_ < s_.size() && s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        } else {
            pos_ = start;
            return err("invalid number");
        }
        if (eat('.')) {
            if (!(pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9')) return err("invalid number");
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (!(pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9')) return err("invalid number");
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        return true;
    }

    // Validates (and discards) any JSON value.
    bool skipValue(int depth) {
        if (depth > kMaxJsonDepth) return err("nesting too deep");
        skipSpace();
        if (pos_ >= s_.size()) return err("unexpected end of input");
        const char c = s_[pos_];
        if (c == '"') { std::string ignored; return parseString(ignored); }
        if (c == '{') return skipObject(depth);
        if (c == '[') return skipArray(depth);
        if (c == 't') return literal("true");
        if (c == 'f') return literal("false");
        if (c == 'n') return literal("null");
        return parseNumber();
    }

    bool skipArray(int depth) {
        eat('[');
        skipSpace();
        if (eat(']')) return true;
        for (;;) {
            if (!skipValue(depth + 1)) return false;
            skipSpace();
            if (eat(']')) return true;
            if (!eat(',')) return err("expected ',' or ']'");
        }
    }

    bool skipObject(int depth) {
        eat('{');
        skipSpace();
        if (eat('}')) return true;
        for (;;) {
            skipSpace();
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (!eat(':')) return err("expected ':'");
            if (!skipValue(depth + 1)) return false;
            skipSpace();
            if (eat('}')) return true;
            if (!eat(',')) return err("expected ',' or '}'");
        }
    }

    bool parseTopLevelObject(AnalyzeRequest& request) {
        if (pos_ >= s_.size() || s_[pos_] != '{') return err("the top level must be an object");
        ++pos_;
        skipSpace();
        if (eat('}')) return true;  // empty object: "source" is reported missing by run()
        for (;;) {
            skipSpace();
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (!eat(':')) return err("expected ':'");
            skipSpace();

            if (key == "source") {
                if (sawSource_) { error_ = "duplicate member \"source\""; return false; }
                if (pos_ >= s_.size() || s_[pos_] != '"') {
                    error_ = "member \"source\" must be a string";
                    return false;
                }
                if (!parseString(request.source)) return false;
                sawSource_ = true;
            } else if (!skipValue(1)) {
                return false;
            }

            skipSpace();
            if (eat('}')) return true;
            if (!eat(',')) return err("expected ',' or '}'");
        }
    }
};

}  // namespace

AnalyzeRequest parseAnalyzeRequest(const std::string& body) {
    return Parser(body).run();
}

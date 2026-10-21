// Tests the real HttpServer over real loopback sockets. Each test starts its
// own server on a free port, talks raw HTTP to it, and shuts it down.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// The Windows SDK declares an enumerator named TokenType; rename it while the
// SDK headers are included so it cannot clash with the project's enum class.
#define TokenType WinSdkTokenType
#include <winsock2.h>
#include <ws2tcpip.h>
#undef TokenType

#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/analyzer.h"
#include "core/ast_builder.h"
#include "core/parser.h"
#include "output/json_writer.h"
#include "server/http_server.h"
#include "test_framework.h"

namespace {

std::string webRoot() {
    std::string root = "web";
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "PTAGEN_WEB_ROOT") == 0 && value != nullptr) {
        root = value;
        std::free(value);
    }
    return root;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

struct Reply {
    bool received = false;   // a status line arrived
    int status = 0;
    std::string headers;     // raw header block
    std::string body;
    std::string raw;
    std::string header(const std::string& name) const {
        std::string lowerHeaders = headers;
        for (char& c : lowerHeaders) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string key = "\r\n" + name + ":";
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::size_t at = lowerHeaders.find(key);
        if (at == std::string::npos) return "";
        const std::size_t start = at + key.size();
        const std::size_t end = headers.find("\r\n", start);
        std::string v = headers.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        return v;
    }
};

Reply parseReply(const std::string& raw) {
    Reply r;
    r.raw = raw;
    if (raw.rfind("HTTP/1.1 ", 0) != 0) return r;
    r.status = std::atoi(raw.c_str() + 9);
    r.received = r.status > 0;
    const std::size_t split = raw.find("\r\n\r\n");
    if (split == std::string::npos) { r.headers = raw; return r; }
    r.headers = raw.substr(0, split);
    r.body = raw.substr(split + 4);
    return r;
}

SOCKET connectTo(int port, int timeoutMs) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const DWORD t = static_cast<DWORD>(timeoutMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

std::string readAll(SOCKET s) {
    std::string out;
    char buf[65536];
    for (;;) {
        const int n = ::recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

// Sends `bytes`, optionally half-closes, and reads the whole reply.
Reply exchange(int port, const std::string& bytes, bool halfClose = false, int timeoutMs = 4000) {
    SOCKET s = connectTo(port, timeoutMs);
    if (s == INVALID_SOCKET) return Reply();
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const int n = ::send(s, bytes.data() + sent,
                             static_cast<int>(std::min<std::size_t>(bytes.size() - sent, 1 << 16)), 0);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    if (halfClose) ::shutdown(s, SD_SEND);
    const Reply reply = parseReply(readAll(s));
    ::closesocket(s);
    return reply;
}

struct Harness {
    HttpServer server;
    std::thread thread;
    std::string error;
    bool started;

    explicit Harness(ServerConfig config = ServerConfig())
        : server(prepare(config)), started(server.start(error)) {
        if (started) thread = std::thread([this] { server.serve(); });
    }
    ~Harness() {
        server.stop();
        if (thread.joinable()) thread.join();
    }
    int port() const { return server.port(); }
    std::string hostHeader() const { return "127.0.0.1:" + std::to_string(port()); }

    static ServerConfig prepare(ServerConfig config) {
        config.port = 0;
        config.logRequests = false;
        if (config.webRoot.empty()) config.webRoot = webRoot();
        return config;
    }

    Reply get(const std::string& path, const std::string& extraHeaders = "") {
        return exchange(port(), "GET " + path + " HTTP/1.1\r\nHost: " + hostHeader() + "\r\n" +
                                    extraHeaders + "\r\n");
    }

    Reply post(const std::string& path, const std::string& body,
               const std::string& contentType = "application/json",
               const std::string& extraHeaders = "") {
        std::string head = "POST " + path + " HTTP/1.1\r\nHost: " + hostHeader() + "\r\n";
        if (!contentType.empty()) head += "Content-Type: " + contentType + "\r\n";
        head += "Content-Length: " + std::to_string(body.size()) + "\r\n" + extraHeaders + "\r\n";
        return exchange(port(), head + body);
    }

    Reply analyzeSource(const std::string& source) {
        std::string body = "{\"source\":";
        appendJsonString(body, source);
        body += "}";
        return post("/api/analyze", body);
    }
};

std::string expectedJson(const std::string& source) {
    return analysisToJson(source, analyze(source));
}

// The server must answer a valid request correctly (used after every abuse).
void expectStillHealthy(Harness& h) {
    const Reply ok = h.analyzeSource("a = b + c * 5;");
    CHECK_EQ(ok.status, 200);
    CHECK(ok.body == expectedJson("a = b + c * 5;"));
    CHECK_EQ(h.get("/api/health").status, 200);
}

std::string chain(int operands) {
    std::string source = "r = 1";
    for (int i = 1; i < operands; ++i) source += "+1";
    return source + ";";
}

}  // namespace

// ---- Lifecycle ---------------------------------------------------------------------------------

TEST(server_starts_serves_and_stops_cleanly) {
    int port = 0;
    {
        Harness h;
        CHECK(h.started);
        port = h.port();
        CHECK(port > 0);
        CHECK_EQ(h.get("/api/health").status, 200);
    }  // stop() + join() in the destructor: serve() returned
    // Nothing listens any more.
    CHECK(connectTo(port, 500) == INVALID_SOCKET);
}

TEST(server_refuses_non_loopback_hosts_and_bad_ports) {
    for (const char* host : {"0.0.0.0", "192.168.1.5", "::", "example.com", ""}) {
        ServerConfig c;
        c.host = host;
        c.port = 0;
        HttpServer s(c);
        std::string err;
        CHECK(!s.start(err));
        CHECK(!err.empty());
    }
    ServerConfig c;
    c.port = 70000;
    HttpServer s(c);
    std::string err;
    CHECK(!s.start(err));
}

TEST(server_refuses_to_share_a_port) {
    Harness first;
    CHECK(first.started);
    ServerConfig c;
    c.port = first.port();
    HttpServer second(c);
    std::string err;
    CHECK(!second.start(err));
}

TEST(server_rejects_a_web_root_without_index_html) {
    ServerConfig c;
    c.port = 0;
    c.webRoot = "no-such-directory";
    HttpServer s(c);
    std::string err;
    CHECK(!s.start(err));
    CHECK(contains(err, "index.html"));
}

TEST(server_listens_on_loopback_only) {
    Harness h;
    CHECK(h.started);
    // Find a non-loopback local address and confirm it is refused.
    char name[256];
    CHECK_EQ(gethostname(name, sizeof name), 0);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &list) == 0) {
        for (addrinfo* a = list; a; a = a->ai_next) {
            sockaddr_in addr = *reinterpret_cast<sockaddr_in*>(a->ai_addr);
            if (addr.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) continue;
            addr.sin_port = htons(static_cast<u_short>(h.port()));
            SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            const int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
            ::closesocket(s);
            CHECK(rc != 0);  // the server is not reachable on the machine's own LAN address
        }
        freeaddrinfo(list);
    }
}

// ---- Real analysis over HTTP -----------------------------------------------------------------------

TEST(analyze_returns_the_real_compiler_result_for_the_baselines) {
    Harness h;
    CHECK(h.started);
    for (const char* source : {"a = b + c * 5;", "result = (x + y) * z;", "r = a - b - c;",
                               "x = a - b / c * d + e;", "a = 1;\nb = a + 2;\nc = (a + b) * c;"}) {
        const Reply r = h.analyzeSource(source);
        CHECK_EQ(r.status, 200);
        CHECK(contains(r.header("Content-Type"), "application/json"));
        CHECK(r.body == expectedJson(source));
        CHECK(contains(r.body, "\"ok\":true"));
    }
}

TEST(analyze_preserves_precedence_and_left_associativity_in_the_ast) {
    Harness h;
    CHECK(h.started);
    const std::string add = h.analyzeSource("a = b + c * 5;").body;
    CHECK(add.find("\"kind\":\"Add\"") < add.find("\"kind\":\"Multiply\""));

    const std::string paren = h.analyzeSource("result = (x + y) * z;").body;
    CHECK(paren.find("\"kind\":\"Multiply\"") < paren.find("\"kind\":\"Add\""));

    // r = a - b - c  =>  Subtract(Subtract(a, b), c): the outer Subtract comes
    // first and its first child is the inner Subtract.
    const std::string sub = h.analyzeSource("r = a - b - c;").body;
    const std::size_t outer = sub.find("\"kind\":\"Subtract\"");
    const std::size_t inner = sub.find("\"kind\":\"Subtract\"", outer + 1);
    CHECK(outer != std::string::npos && inner != std::string::npos);
    CHECK(sub.find("\"value\":\"c\"") > inner);
}

TEST(analyze_handles_empty_and_whitespace_input) {
    Harness h;
    CHECK(h.started);
    for (const char* source : {"", "   ", "\n\t\n"}) {
        const Reply r = h.analyzeSource(source);
        CHECK_EQ(r.status, 200);
        CHECK(r.body == expectedJson(source));
        CHECK(contains(r.body, "\"ok\":true"));
        CHECK(contains(r.body, "\"kind\":\"Program\""));
    }
}

TEST(analyze_reports_lexical_and_syntax_errors_without_trees) {
    Harness h;
    CHECK(h.started);
    const Reply lexical = h.analyzeSource("a = b $ c;");
    CHECK_EQ(lexical.status, 200);   // a compiler diagnostic is not an HTTP error
    CHECK(lexical.body == expectedJson("a = b $ c;"));
    CHECK(contains(lexical.body, "\"ok\":false,\"tokenized\":false"));
    CHECK(contains(lexical.body, "\"kind\":\"lexical\""));
    CHECK(contains(lexical.body, "\"parseTree\":null,\"ast\":null"));

    const Reply syntax = h.analyzeSource("a = b + * 5;");
    CHECK_EQ(syntax.status, 200);
    CHECK(contains(syntax.body, "\"ok\":false,\"tokenized\":true"));
    CHECK(contains(syntax.body, "\"line\":1,\"column\":9,\"found\":\"*\""));
    CHECK(contains(syntax.body, "\"expected\":[\"ID\",\"NUMBER\",\"LPAREN\"]"));
    CHECK(contains(syntax.body, "\"parseTree\":null,\"ast\":null"));

    const Reply paren = h.analyzeSource("a = (b + c;");
    CHECK(contains(paren.body, "\"column\":11"));
    CHECK(contains(paren.body, "\"expected\":[\"RPAREN\"]"));
}

TEST(analyze_classifies_implementation_limits) {
    Harness h;
    CHECK(h.started);
    const std::string deep = "a = " + std::string(kMaxNestingDepth + 1, '(') + "b" +
                             std::string(kMaxNestingDepth + 1, ')') + ";";
    const Reply nesting = h.analyzeSource(deep);
    CHECK_EQ(nesting.status, 200);
    CHECK(contains(nesting.body, "\"kind\":\"limit\""));
    CHECK(contains(nesting.body, "\"parseTree\":null,\"ast\":null"));

    const Reply height = h.analyzeSource(chain(kMaxAstHeight - 1));
    CHECK_EQ(height.status, 200);
    CHECK(contains(height.body, "\"kind\":\"limit\""));
    CHECK(contains(height.body, "\"ok\":false"));
    CHECK(contains(height.body, "\"parseTree\":null,\"ast\":null"));

    // Ordinary errors are never classified as limits.
    CHECK(!contains(h.analyzeSource("a = b + * 5;").body, "\"kind\":\"limit\""));
    CHECK(!contains(h.analyzeSource("a = b $ c;").body, "\"kind\":\"limit\""));
}

TEST(analyze_accepts_input_exactly_at_the_limits_on_a_worker_thread) {
    Harness h;
    CHECK(h.started);
    const std::string nesting = "a = " + std::string(kMaxNestingDepth, '(') + "b" +
                                std::string(kMaxNestingDepth, ')') + ";";
    const Reply a = h.analyzeSource(nesting);
    CHECK_EQ(a.status, 200);
    CHECK(contains(a.body, "\"ok\":true"));
    CHECK(a.body == expectedJson(nesting));

    const Reply b = h.analyzeSource(chain(kMaxAstHeight - 2));
    CHECK_EQ(b.status, 200);
    CHECK(contains(b.body, "\"ok\":true"));
    expectStillHealthy(h);
}

TEST(analyze_results_are_never_stale_across_requests) {
    Harness h;
    CHECK(h.started);
    const char* sources[] = {"a = 1;", "b = 2 + 3;", "", "a = $;", "a = 1;", "c = (d);"};
    for (int round = 0; round < 3; ++round) {
        for (const char* source : sources) {
            const Reply r = h.analyzeSource(source);
            CHECK_EQ(r.status, 200);
            CHECK(r.body == expectedJson(source));
        }
    }
}

TEST(analyze_handles_concurrent_clients_without_mixing_results) {
    Harness h;
    CHECK(h.started);
    std::vector<std::thread> clients;
    std::vector<int> failures(8, 0);
    for (int t = 0; t < 8; ++t) {
        clients.emplace_back([&h, &failures, t] {
            for (int i = 0; i < 10; ++i) {
                const std::string source = "v" + std::to_string(t) + " = " + std::to_string(i) +
                                           " + w" + std::to_string(t * 10 + i) + ";";
                const Reply r = h.analyzeSource(source);
                if (r.status != 200 || r.body != expectedJson(source)) ++failures[t];
            }
        });
    }
    for (std::thread& c : clients) c.join();
    for (int f : failures) CHECK_EQ(f, 0);
}

TEST(analyze_accepts_json_whitespace_charset_and_query_string) {
    Harness h;
    CHECK(h.started);
    const std::string body = " {\n \"source\" : \"a = 1;\" , \"extra\": [1,2] }\n";
    CHECK_EQ(h.post("/api/analyze", body, "application/json; charset=utf-8").status, 200);
    CHECK_EQ(h.post("/api/analyze", body, "APPLICATION/JSON").status, 200);
    CHECK_EQ(h.post("/api/analyze?x=1", body).status, 200);
}

// ---- Malformed and hostile requests --------------------------------------------------------------------

TEST(invalid_json_bodies_get_400_and_do_not_disturb_the_server) {
    Harness h;
    CHECK(h.started);
    const char* const bodies[] = {
        "", "{", "not json", "{\"source\":}", "[]", "{}", "{\"source\":1}", "{\"source\":null}",
        "{\"source\":\"a\"} trailing", "{\"source\":\"a\",\"source\":\"b\"}",
        "{\"source\":\"\\ud83d\"}", "{\"source\":\"\xFF\"}",
    };
    for (const char* body : bodies) {
        const Reply r = h.post("/api/analyze", body);
        CHECK_EQ(r.status, 400);
        CHECK(contains(r.body, "\"error\":{\"code\":\"invalid_request\""));
        CHECK(!contains(r.body, "\"ok\""));            // never shaped like an analysis result
        expectStillHealthy(h);
    }
}

TEST(error_bodies_are_json_and_do_not_echo_the_request) {
    Harness h;
    CHECK(h.started);
    const Reply r = h.post("/api/analyze", "{\"source\":\"TOP-SECRET-PROGRAM\" oops}");
    CHECK_EQ(r.status, 400);
    CHECK(contains(r.header("Content-Type"), "application/json"));
    CHECK(!contains(r.body, "TOP-SECRET"));
    CHECK(contains(r.body, "\"message\":"));
}

TEST(wrong_content_types_get_415) {
    Harness h;
    CHECK(h.started);
    const std::string body = "{\"source\":\"a = 1;\"}";
    CHECK_EQ(h.post("/api/analyze", body, "text/plain").status, 415);
    CHECK_EQ(h.post("/api/analyze", body, "application/x-www-form-urlencoded").status, 415);
    CHECK_EQ(h.post("/api/analyze", body, "application/jsonx").status, 415);
    CHECK_EQ(h.post("/api/analyze", body, "").status, 415);   // header missing
    expectStillHealthy(h);
}

TEST(unsupported_methods_and_unknown_paths) {
    Harness h;
    CHECK(h.started);
    const Reply get = h.get("/api/analyze");
    CHECK_EQ(get.status, 405);
    CHECK_EQ(get.header("Allow"), "POST");

    for (const char* method : {"PUT", "DELETE", "PATCH", "OPTIONS", "TRACE", "CONNECT"}) {
        const Reply r = exchange(h.port(), std::string(method) + " /api/analyze HTTP/1.1\r\nHost: " +
                                               h.hostHeader() + "\r\nContent-Length: 0\r\n\r\n");
        CHECK_EQ(r.status, 405);
    }
    CHECK_EQ(h.post("/api/health", "{}").status, 405);
    CHECK_EQ(h.post("/index.html", "{}").status, 405);
    CHECK_EQ(h.post("/", "{}").status, 405);

    CHECK_EQ(h.get("/api/unknown").status, 404);
    CHECK_EQ(h.post("/api/analyse", "{\"source\":\"a\"}").status, 404);
    CHECK_EQ(h.get("/nope").status, 404);
    CHECK_EQ(h.get("/api").status, 404);
    expectStillHealthy(h);
}

TEST(content_length_rules) {
    Harness h;
    CHECK(h.started);
    const std::string head = "POST /api/analyze HTTP/1.1\r\nHost: " + h.hostHeader() +
                             "\r\nContent-Type: application/json\r\n";
    // missing Content-Length
    CHECK_EQ(exchange(h.port(), head + "\r\n{\"source\":\"a\"}").status, 411);
    // chunked bodies are not supported
    CHECK_EQ(exchange(h.port(), head + "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n").status, 501);
    // invalid / negative / huge Content-Length
    CHECK_EQ(exchange(h.port(), head + "Content-Length: abc\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), head + "Content-Length: -5\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), head + "Content-Length: 99999999999999999999999\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), head + "Content-Length: 1\r\nContent-Length: 2\r\n\r\nxx").status, 400);
    expectStillHealthy(h);
}

TEST(oversized_payloads_get_413_without_reading_the_body) {
    Harness h;
    CHECK(h.started);
    const std::string head = "POST /api/analyze HTTP/1.1\r\nHost: " + h.hostHeader() +
                             "\r\nContent-Type: application/json\r\n";
    // Declared length over the limit: rejected before any body is sent.
    const Reply declared = exchange(h.port(), head + "Content-Length: 10000000\r\n\r\n");
    CHECK_EQ(declared.status, 413);
    CHECK(contains(declared.body, "body_too_large"));

    // A real body just over the limit.
    const std::string big(256 * 1024 + 1, 'x');
    CHECK_EQ(h.post("/api/analyze", big).status, 413);
    expectStillHealthy(h);
}

TEST(oversized_source_gets_413_and_the_limit_is_exact) {
    ServerConfig c;
    c.maxSourceBytes = 40;
    Harness h(c);
    CHECK(h.started);
    const std::string atLimit(40, ' ');
    const std::string overLimit(41, ' ');
    CHECK_EQ(h.analyzeSource(atLimit).status, 200);
    const Reply over = h.analyzeSource(overLimit);
    CHECK_EQ(over.status, 413);
    CHECK(contains(over.body, "source_too_large"));
    expectStillHealthy(h);
}

TEST(oversized_headers_get_431) {
    Harness h;
    CHECK(h.started);
    const std::string pad(20000, 'a');
    const Reply r = exchange(h.port(), "GET /api/health HTTP/1.1\r\nHost: " + h.hostHeader() +
                                           "\r\nX-Pad: " + pad + "\r\n\r\n");
    CHECK_EQ(r.status, 431);
    // Too many headers.
    std::string many = "GET /api/health HTTP/1.1\r\nHost: " + h.hostHeader() + "\r\n";
    for (int i = 0; i < 100; ++i) many += "X-" + std::to_string(i) + ": v\r\n";
    CHECK_EQ(exchange(h.port(), many + "\r\n").status, 400);
    expectStillHealthy(h);
}

TEST(malformed_requests_get_400_or_505) {
    Harness h;
    CHECK(h.started);
    const std::string host = "Host: " + h.hostHeader() + "\r\n";
    CHECK_EQ(exchange(h.port(), "GARBAGE\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET /\r\n" + host + "\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET / HTTP/1.1 extra\r\n" + host + "\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET  / HTTP/1.1\r\n" + host + "\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "get / HTTP/1.1\r\n" + host + "\r\n").status, 405);  // unknown method
    CHECK_EQ(exchange(h.port(), "GET index.html HTTP/1.1\r\n" + host + "\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET / HTTX/1.1\r\n" + host + "\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET / HTTP/2.0\r\n" + host + "\r\n").status, 505);
    CHECK_EQ(exchange(h.port(), "GET / HTTP/1.1\r\nNoColonHere\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET / HTTP/1.1\r\n" + host + "Bad Name: v\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET / HTTP/1.1\r\n" + host + "X: a\rb\r\n\r\n").status, 400);
    CHECK_EQ(exchange(h.port(), "GET / HTTP/1.1\r\n\r\n").status, 400);  // no Host
    expectStillHealthy(h);
}

TEST(truncated_and_abandoned_requests_do_not_hurt_the_server) {
    Harness h;
    CHECK(h.started);
    const std::string head = "POST /api/analyze HTTP/1.1\r\nHost: " + h.hostHeader() +
                             "\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n";
    // Body shorter than declared, then the client half-closes.
    const Reply r = exchange(h.port(), head + "{\"source\":", /*halfClose=*/true);
    CHECK_EQ(r.status, 400);
    CHECK(contains(r.body, "incomplete_body"));

    // Connect and immediately drop, repeatedly; and send half a header.
    for (int i = 0; i < 20; ++i) {
        SOCKET s = connectTo(h.port(), 500);
        CHECK(s != INVALID_SOCKET);
        if (i % 2) ::send(s, "GET /api/hea", 12, 0);
        ::closesocket(s);
    }
    expectStillHealthy(h);
}

TEST(slow_clients_time_out_without_blocking_others) {
    ServerConfig c;
    c.ioTimeoutMs = 500;
    Harness h(c);
    CHECK(h.started);

    SOCKET idle = connectTo(h.port(), 3000);   // connects, sends nothing
    CHECK(idle != INVALID_SOCKET);

    const auto begin = std::chrono::steady_clock::now();
    expectStillHealthy(h);                    // served at once while the idle socket is open
    const auto served = std::chrono::steady_clock::now() - begin;
    CHECK(served < std::chrono::milliseconds(400));

    // An idle connection that never sends anything is closed without a response.
    const std::string idleReply = readAll(idle);
    ::closesocket(idle);
    CHECK(idleReply.empty());

    // A client that sends a partial request line and stalls.
    SOCKET stalled = connectTo(h.port(), 3000);
    ::send(stalled, "POST /api/analyze HT", 20, 0);
    CHECK_EQ(parseReply(readAll(stalled)).status, 408);
    ::closesocket(stalled);
    expectStillHealthy(h);
}

TEST(connection_cap_returns_503_and_recovers) {
    ServerConfig c;
    c.maxConnections = 2;
    c.ioTimeoutMs = 1500;
    Harness h(c);
    CHECK(h.started);

    SOCKET a = connectTo(h.port(), 3000);
    SOCKET b = connectTo(h.port(), 3000);
    CHECK(a != INVALID_SOCKET && b != INVALID_SOCKET);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // let both be admitted

    CHECK_EQ(h.get("/api/health").status, 503);
    ::closesocket(a);
    ::closesocket(b);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK_EQ(h.get("/api/health").status, 200);
}

// ---- Host / Origin ----------------------------------------------------------------------------------------

TEST(host_and_origin_are_validated) {
    Harness h;
    CHECK(h.started);
    const std::string p = std::to_string(h.port());
    auto healthWith = [&](const std::string& headers) {
        return exchange(h.port(), "GET /api/health HTTP/1.1\r\n" + headers + "\r\n");
    };
    CHECK_EQ(healthWith("Host: 127.0.0.1:" + p + "\r\n").status, 200);
    CHECK_EQ(healthWith("Host: localhost:" + p + "\r\n").status, 200);
    CHECK_EQ(healthWith("Host: LOCALHOST:" + p + "\r\n").status, 200);
    CHECK_EQ(healthWith("Host: evil.example\r\n").status, 403);
    CHECK_EQ(healthWith("Host: evil.example:" + p + "\r\n").status, 403);
    CHECK_EQ(healthWith("Host: 127.0.0.1:1\r\n").status, 403);
    CHECK_EQ(healthWith("Host: 127.0.0.1:" + p + ".evil.example\r\n").status, 403);

    const std::string host = "Host: 127.0.0.1:" + p + "\r\n";
    CHECK_EQ(healthWith(host + "Origin: http://127.0.0.1:" + p + "\r\n").status, 200);
    CHECK_EQ(healthWith(host + "Origin: http://localhost:" + p + "\r\n").status, 200);
    CHECK_EQ(healthWith(host + "Origin: http://evil.example\r\n").status, 403);
    CHECK_EQ(healthWith(host + "Origin: null\r\n").status, 403);
    CHECK_EQ(healthWith(host + "Origin: https://127.0.0.1:" + p + "\r\n").status, 403);

    // The same rules protect the analysis endpoint.
    const Reply cross = h.post("/api/analyze", "{\"source\":\"a = 1;\"}", "application/json",
                               "Origin: http://evil.example\r\n");
    CHECK_EQ(cross.status, 403);
    CHECK(!contains(cross.body, "\"ok\""));
    expectStillHealthy(h);
}

TEST(responses_carry_no_cors_headers_and_safe_headers) {
    Harness h;
    CHECK(h.started);
    const Reply r = h.get("/api/health");
    CHECK_EQ(r.header("Access-Control-Allow-Origin"), "");
    CHECK_EQ(r.header("X-Content-Type-Options"), "nosniff");
    CHECK_EQ(r.header("Cache-Control"), "no-store");
    CHECK_EQ(r.header("Connection"), "close");
    CHECK(contains(r.header("Content-Security-Policy"), "default-src 'self'"));
    CHECK_EQ(r.header("Content-Length"), std::to_string(r.body.size()));
}

// ---- Static files ------------------------------------------------------------------------------------------------

TEST(static_files_are_served_from_the_allowlist) {
    Harness h;
    CHECK(h.started);
    const Reply index = h.get("/");
    CHECK_EQ(index.status, 200);
    CHECK(contains(index.header("Content-Type"), "text/html"));
    CHECK(contains(index.body, "<title>Parse Tree and AST Generator</title>"));
    CHECK(h.get("/index.html").body == index.body);

    const Reply js = h.get("/js/app.js");
    CHECK_EQ(js.status, 200);
    CHECK(contains(js.header("Content-Type"), "text/javascript"));
    CHECK_EQ(h.get("/css/style.css").status, 200);
    CHECK(contains(h.get("/css/style.css").header("Content-Type"), "text/css"));
    CHECK_EQ(h.get("/fixtures/fixtures.js").status, 200);

    const Reply head = exchange(h.port(), "HEAD /index.html HTTP/1.1\r\nHost: " + h.hostHeader() + "\r\n\r\n");
    CHECK_EQ(head.status, 200);
    CHECK(head.body.empty());
    CHECK_EQ(head.header("Content-Length"), std::to_string(index.body.size()));
    CHECK_EQ(h.get("/favicon.ico").status, 204);
}

TEST(static_serving_cannot_reach_files_outside_the_allowlist) {
    Harness h;
    CHECK(h.started);
    const char* const attempts[] = {
        "/../src/main.cpp", "/js/../index.html", "/js/../../README.md", "/%2e%2e/src/main.cpp",
        "/js/%2e%2e/index.html", "/src/main.cpp", "/README.md", "/js/", "/js", "/css", "/.git/config",
        "/.gitignore", "//index.html", "//etc/passwd", "/js//app.js", "/js/app.js/", "/js/app.js%00.png",
        "/js/..%5cindex.html", "/js\\app.js", "/index.html/", "/tests/unit/server_tests.cpp",
        "/js/app.json", "/js/app.js.map", "/fixtures/../README.md", "/web/index.html",
        "/C:/Windows/win.ini", "/js/.hidden.js", "/js/app.js?x=../../README.md#frag",
    };
    for (const char* path : attempts) {
        const Reply r = h.get(path);
        // Only the last attempt is a legitimate file (the query string is ignored).
        const bool legit = std::string(path).rfind("/js/app.js?", 0) == 0;
        if (legit) {
            CHECK_EQ(r.status, 200);
        } else {
            CHECK_EQ(r.status, 404);
            CHECK(!contains(r.body, "int main"));
            CHECK(!contains(r.body, "Parse Tree and AST Generator"));
        }
    }
    expectStillHealthy(h);
}

TEST(static_path_allowlist_function) {
    CHECK(isSafeStaticPath("/"));
    CHECK(isSafeStaticPath("/index.html"));
    CHECK(isSafeStaticPath("/js/app.js"));
    CHECK(isSafeStaticPath("/css/style.css"));
    CHECK(isSafeStaticPath("/fixtures/fixtures-stress.js"));
    CHECK(!isSafeStaticPath(""));
    CHECK(!isSafeStaticPath("index.html"));
    CHECK(!isSafeStaticPath("/other.html"));
    CHECK(!isSafeStaticPath("/js/x.html"));
    CHECK(!isSafeStaticPath("/src/x.js"));
    CHECK(!isSafeStaticPath("/js/a/b.js"));
    CHECK(!isSafeStaticPath("/js/../app.js"));
    CHECK(!isSafeStaticPath("/js/.js"));
    CHECK(!isSafeStaticPath("/js/a b.js"));
    CHECK(!isSafeStaticPath("/js/a%2eb.js"));
}

TEST(api_only_server_without_a_web_root) {
    // Re-create the harness with an empty web root by building the server directly.
    ServerConfig c;
    c.port = 0;
    c.webRoot = "";
    c.logRequests = false;
    HttpServer server(c);
    std::string err;
    CHECK(server.start(err));
    std::thread t([&server] { server.serve(); });
    const std::string host = "127.0.0.1:" + std::to_string(server.port());
    CHECK_EQ(exchange(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n").status, 404);
    CHECK_EQ(exchange(server.port(), "GET /api/health HTTP/1.1\r\nHost: " + host + "\r\n\r\n").status, 200);
    server.stop();
    t.join();
}

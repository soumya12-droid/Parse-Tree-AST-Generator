#include "server/http_server.h"

#ifndef _WIN32
#error "src/server/http_server.cpp uses Winsock and builds on Windows only."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// The Windows SDK declares an enumerator named TokenType (winnt.h), which
// would clash with this project's `enum class TokenType`. Rename it for the
// duration of the SDK includes only.
#define TokenType WinSdkTokenType
#include <winsock2.h>
#include <ws2tcpip.h>
#undef TokenType

#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "core/analyzer.h"
#include "output/json_writer.h"
#include "server/json_request.h"

namespace {

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
constexpr std::size_t kMaxHeaderCount = 64;
constexpr std::size_t kMaxTargetBytes = 2048;
constexpr std::size_t kMaxStaticBytes = 8 * 1024 * 1024;

// ---- Small helpers -----------------------------------------------------------------

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 417: return "Expectation Failed";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default:  return "Error";
    }
}

struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::string allow;  // for 405
};

Response errorResponse(int status, const std::string& code, const std::string& message) {
    Response r;
    r.status = status;
    r.body = "{\"error\":{\"code\":";
    appendJsonString(r.body, code);
    r.body += ",\"message\":";
    appendJsonString(r.body, message);
    r.body += "}}";
    return r;
}

// ---- Socket I/O ------------------------------------------------------------------------

bool sendAll(SOCKET s, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const int chunk = static_cast<int>(std::min<std::size_t>(size - sent, 1 << 20));
        const int n = ::send(s, data + sent, chunk, 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void setTimeouts(SOCKET s, int ms) {
    const DWORD t = static_cast<DWORD>(ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
}

// ---- Request model and parsing ------------------------------------------------------------

struct Request {
    std::string method;
    std::string target;
    std::string path;                          // target without the query string
    std::map<std::string, std::string> headers;  // lower-case names
    std::string rest;                          // body bytes already received
};

enum class ReadStatus { Ok, Closed, Timeout, TooLarge, Malformed };

bool isTokenChar(char c) {
    if (c >= '0' && c <= '9') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    return std::string("!#$%&'*+-.^_`|~").find(c) != std::string::npos;
}

// Reads up to the blank line that ends the header section.
ReadStatus readHeaderSection(SOCKET s, std::string& buffer, std::size_t& headerEnd) {
    char chunk[4096];
    for (;;) {
        const std::size_t found = buffer.find("\r\n\r\n");
        if (found != std::string::npos) {
            if (found > kMaxHeaderBytes) return ReadStatus::TooLarge;
            headerEnd = found;
            return ReadStatus::Ok;
        }
        if (buffer.size() > kMaxHeaderBytes) return ReadStatus::TooLarge;
        const int n = ::recv(s, chunk, sizeof chunk, 0);
        if (n == 0) return buffer.empty() ? ReadStatus::Closed : ReadStatus::Malformed;
        if (n < 0) {
            return WSAGetLastError() == WSAETIMEDOUT ? ReadStatus::Timeout : ReadStatus::Closed;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
    }
}

// Parses the request line and headers. Returns an empty string on success or
// an explanation (HTTP 400) otherwise; `version` is set to the HTTP minor version.
std::string parseHead(const std::string& head, Request& req, int& minorVersion) {
    // Only CRLF line endings; no stray CR or LF inside lines.
    for (std::size_t i = 0; i < head.size(); ++i) {
        const char c = head[i];
        if (c == '\r' && !(i + 1 < head.size() && head[i + 1] == '\n')) return "bare CR in header";
        if (c == '\n' && !(i > 0 && head[i - 1] == '\r')) return "bare LF in header";
        if (static_cast<unsigned char>(c) < 0x20 && c != '\r' && c != '\n' && c != '\t') {
            return "control character in header";
        }
    }

    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= head.size()) {
        const std::size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) { lines.push_back(head.substr(pos)); break; }
        lines.push_back(head.substr(pos, eol - pos));
        pos = eol + 2;
    }
    if (lines.empty() || lines[0].empty()) return "empty request line";

    // Request line: METHOD SP target SP HTTP/1.x
    const std::string& line = lines[0];
    const std::size_t sp1 = line.find(' ');
    const std::size_t sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos ||
        line.find(' ', sp2 + 1) != std::string::npos) {
        return "malformed request line";
    }
    req.method = line.substr(0, sp1);
    req.target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string version = line.substr(sp2 + 1);
    if (req.method.empty() || req.method.size() > 16) return "malformed method";
    for (char c : req.method) if (!isTokenChar(c)) return "malformed method";
    if (req.target.empty() || req.target[0] != '/' || req.target.size() > kMaxTargetBytes) {
        return "malformed request target";
    }
    for (char c : req.target) {
        if (static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F) {
            return "malformed request target";
        }
    }
    if (version == "HTTP/1.1") minorVersion = 1;
    else if (version == "HTTP/1.0") minorVersion = 0;
    else if (version.rfind("HTTP/", 0) == 0) minorVersion = -1;  // unsupported version
    else return "malformed HTTP version";

    const std::size_t query = req.target.find('?');
    req.path = req.target.substr(0, query);

    if (lines.size() - 1 > kMaxHeaderCount) return "too many headers";
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& h = lines[i];
        const std::size_t colon = h.find(':');
        if (colon == std::string::npos || colon == 0) return "malformed header";
        const std::string name = h.substr(0, colon);
        for (char c : name) if (!isTokenChar(c)) return "malformed header name";
        const std::string key = lower(name);
        const std::string value = trim(h.substr(colon + 1));
        const auto existing = req.headers.find(key);
        if (existing != req.headers.end()) {
            if (key == "content-length" && existing->second != value) {
                return "conflicting Content-Length headers";
            }
            continue;  // keep the first occurrence
        }
        req.headers[key] = value;
    }
    return "";
}

std::string headerOr(const Request& req, const std::string& name) {
    const auto it = req.headers.find(name);
    return it == req.headers.end() ? std::string() : it->second;
}

// ---- Static files (fixed allowlist only) -------------------------------------------------------

bool plainName(const std::string& s) {
    if (s.empty() || s.size() > 64 || s[0] == '.') return false;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string contentTypeFor(const std::string& name) {
    if (endsWith(name, ".html")) return "text/html; charset=utf-8";
    if (endsWith(name, ".js")) return "text/javascript; charset=utf-8";
    return "text/css; charset=utf-8";
}

// Reads one allowlisted static file below `root`.
bool loadStatic(const std::string& root, const std::string& path, Response& out) {
    if (root.empty() || !isSafeStaticPath(path)) return false;
    const std::string rel = path == "/" ? "index.html" : path.substr(1);
    std::ifstream in(root + "/" + rel, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<std::size_t>(size) > kMaxStaticBytes) return false;
    in.seekg(0, std::ios::beg);
    out.body.resize(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(&out.body[0], size)) return false;
    out.contentType = contentTypeFor(rel);
    return true;
}

// ---- Shared state -----------------------------------------------------------------------------------

struct Shared {
    std::mutex mutex;
    std::condition_variable cv;
    int active = 0;
    std::mutex logMutex;
};

struct Context {
    ServerConfig cfg;
    int port = 0;
    std::shared_ptr<Shared> shared;
};

void logLine(const Context& ctx, const Request& req, int status, long long ms) {
    if (!ctx.cfg.logRequests) return;
    // Method and path only (never bodies); anything unusual is masked.
    std::string path;
    for (char c : req.path.substr(0, 100)) {
        const bool plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                           (c >= 'a' && c <= 'z') || std::string("/._-").find(c) != std::string::npos;
        path += plain ? c : '?';
    }
    std::lock_guard<std::mutex> lock(ctx.shared->logMutex);
    std::fprintf(stderr, "%s %s -> %d (%lld ms)\n", req.method.substr(0, 8).c_str(), path.c_str(),
                 status, ms);
}

// ---- Routing ------------------------------------------------------------------------------------------

bool hostAllowed(const std::string& host, int port) {
    const std::string h = lower(host);
    const std::string p = std::to_string(port);
    if (h == "127.0.0.1:" + p || h == "localhost:" + p) return true;
    return port == 80 && (h == "127.0.0.1" || h == "localhost");
}

bool originAllowed(const std::string& origin, int port) {
    const std::string o = lower(origin);
    const std::string p = std::to_string(port);
    return o == "http://127.0.0.1:" + p || o == "http://localhost:" + p;
}

bool mediaTypeIsJson(const std::string& contentType) {
    const std::string v = lower(trim(contentType.substr(0, contentType.find(';'))));
    return v == "application/json";
}

// Parses Content-Length; returns false if it is not a plain decimal number.
bool parseContentLength(const std::string& v, unsigned long long& out) {
    if (v.empty() || v.size() > 19) return false;
    out = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        out = out * 10 + static_cast<unsigned>(c - '0');
    }
    return true;
}

// Handles POST /api/analyze: validation, body read, analysis. Returns the response.
Response handleAnalyze(SOCKET s, const Context& ctx, const Request& req, int minorVersion) {
    if (!mediaTypeIsJson(headerOr(req, "content-type"))) {
        return errorResponse(415, "unsupported_media_type", "Content-Type must be application/json.");
    }
    if (req.headers.count("transfer-encoding")) {
        return errorResponse(501, "chunked_not_supported",
                             "Transfer-Encoding is not supported; send Content-Length.");
    }
    const auto lengthHeader = req.headers.find("content-length");
    if (lengthHeader == req.headers.end()) {
        return errorResponse(411, "length_required", "Content-Length is required.");
    }
    unsigned long long length = 0;
    if (!parseContentLength(lengthHeader->second, length)) {
        return errorResponse(400, "bad_content_length", "Content-Length is not a valid number.");
    }
    if (length > ctx.cfg.maxBodyBytes) {
        return errorResponse(413, "body_too_large",
                             "Request body exceeds " + std::to_string(ctx.cfg.maxBodyBytes) + " bytes.");
    }
    if (lower(headerOr(req, "expect")) == "100-continue" && minorVersion >= 1) {
        const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
        sendAll(s, cont, sizeof cont - 1);
    }

    std::string body = req.rest;
    if (body.size() > length) body.resize(static_cast<std::size_t>(length));
    char chunk[8192];
    while (body.size() < length) {
        const std::size_t want = std::min<std::size_t>(sizeof chunk, static_cast<std::size_t>(length) - body.size());
        const int n = ::recv(s, chunk, static_cast<int>(want), 0);
        if (n == 0) return errorResponse(400, "incomplete_body", "The request body ended early.");
        if (n < 0) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                return errorResponse(408, "request_timeout", "Timed out waiting for the request body.");
            }
            return errorResponse(400, "incomplete_body", "The request body could not be read.");
        }
        body.append(chunk, static_cast<std::size_t>(n));
    }

    const AnalyzeRequest parsed = parseAnalyzeRequest(body);
    if (!parsed.ok) return errorResponse(400, "invalid_request", parsed.error);
    if (parsed.source.size() > ctx.cfg.maxSourceBytes) {
        return errorResponse(413, "source_too_large",
                             "Source exceeds " + std::to_string(ctx.cfg.maxSourceBytes) + " bytes.");
    }

    // The one place analysis happens: the real compiler pipeline, on this
    // request's source only. No state is shared between requests.
    const AnalysisResult result = analyze(parsed.source);
    Response r;
    r.status = 200;
    r.body = analysisToJson(parsed.source, result);
    return r;
}

Response route(SOCKET s, const Context& ctx, const Request& req, int minorVersion) {
    if (minorVersion < 0) {
        return errorResponse(505, "http_version_not_supported", "Only HTTP/1.0 and HTTP/1.1 are supported.");
    }
    if (!req.headers.count("host")) {
        return errorResponse(400, "missing_host", "The Host header is required.");
    }
    if (!hostAllowed(req.headers.at("host"), ctx.port)) {
        return errorResponse(403, "forbidden_host", "This server only answers to localhost.");
    }
    if (req.headers.count("origin") && !originAllowed(req.headers.at("origin"), ctx.port)) {
        return errorResponse(403, "forbidden_origin", "Cross-origin requests are not allowed.");
    }

    const std::string& method = req.method;
    const std::string& path = req.path;

    if (path == "/api/analyze") {
        if (method != "POST") {
            Response r = errorResponse(405, "method_not_allowed", "Use POST for /api/analyze.");
            r.allow = "POST";
            return r;
        }
        return handleAnalyze(s, ctx, req, minorVersion);
    }
    if (path == "/api/health") {
        if (method != "GET" && method != "HEAD") {
            Response r = errorResponse(405, "method_not_allowed", "Use GET for /api/health.");
            r.allow = "GET, HEAD";
            return r;
        }
        Response r;
        r.body = std::string("{\"status\":\"ok\",\"service\":\"ptagen\",\"contractVersion\":") +
                 std::to_string(kContractVersion) + "}";
        return r;
    }
    if (path.rfind("/api/", 0) == 0) {
        return errorResponse(404, "not_found", "Unknown API path.");
    }
    if (path == "/favicon.ico" && (method == "GET" || method == "HEAD")) {
        Response r;
        r.status = 204;
        return r;
    }
    if (isSafeStaticPath(path)) {
        if (method != "GET" && method != "HEAD") {
            Response r = errorResponse(405, "method_not_allowed", "Static files are read-only.");
            r.allow = "GET, HEAD";
            return r;
        }
        Response r;
        if (loadStatic(ctx.cfg.webRoot, path, r)) return r;
    }
    return errorResponse(404, "not_found", "Not found.");
}

std::string serialize(const Response& r, bool headOnly) {
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reasonPhrase(r.status) + "\r\n";
    if (r.status != 204) out += "Content-Type: " + r.contentType + "\r\n";
    out += "Content-Length: " + std::to_string(r.status == 204 ? 0 : r.body.size()) + "\r\n";
    if (!r.allow.empty()) out += "Allow: " + r.allow + "\r\n";
    out += "Cache-Control: no-store\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "Content-Security-Policy: default-src 'self'; frame-ancestors 'none'\r\n"
           "Connection: close\r\n\r\n";
    if (!headOnly && r.status != 204) out += r.body;
    return out;
}

// After the response, half-close and briefly discard unread request bytes so
// the client reliably receives the response (an immediate close with unread
// data can reset the connection and lose it).
void finishConnection(SOCKET s) {
    ::shutdown(s, SD_SEND);
    setTimeouts(s, 300);
    char sink[4096];
    std::size_t total = 0;
    while (total < (2u << 20)) {
        const int n = ::recv(s, sink, sizeof sink, 0);
        if (n <= 0) break;
        total += static_cast<std::size_t>(n);
    }
    ::closesocket(s);
}

void handleConnection(SOCKET s, const Context& ctx) {
    const auto started = std::chrono::steady_clock::now();
    setTimeouts(s, ctx.cfg.ioTimeoutMs);

    Request req;
    Response response;
    int minorVersion = 1;
    bool respond = true;

    try {
        std::string buffer;
        std::size_t headerEnd = 0;
        const ReadStatus status = readHeaderSection(s, buffer, headerEnd);
        if (status == ReadStatus::Closed) {
            respond = false;
        } else if (status == ReadStatus::Timeout) {
            // A connection that never sent a byte (browsers open idle
            // pre-connections) is closed silently; a stalled request gets 408.
            if (buffer.empty()) respond = false;
            else response = errorResponse(408, "request_timeout", "Timed out waiting for the request.");
        } else if (status == ReadStatus::TooLarge) {
            response = errorResponse(431, "headers_too_large", "Request headers are too large.");
        } else if (status == ReadStatus::Malformed) {
            response = errorResponse(400, "malformed_request", "The request ended early.");
        } else {
            const std::string why = parseHead(buffer.substr(0, headerEnd), req, minorVersion);
            if (!why.empty()) {
                response = errorResponse(400, "malformed_request", why);
            } else {
                req.rest = buffer.substr(headerEnd + 4);
                response = route(s, ctx, req, minorVersion);
            }
        }
    } catch (const std::exception&) {
        response = errorResponse(500, "internal_error", "The server failed to process the request.");
    } catch (...) {
        response = errorResponse(500, "internal_error", "The server failed to process the request.");
    }

    if (respond) {
        const std::string wire = serialize(response, req.method == "HEAD");
        sendAll(s, wire.data(), wire.size());
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started).count();
        logLine(ctx, req, response.status, ms);
    }
    finishConnection(s);
}

}  // namespace

bool isSafeStaticPath(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    if (path == "/") return true;

    std::vector<std::string> segments;
    std::size_t start = 1;
    for (;;) {
        const std::size_t slash = path.find('/', start);
        segments.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    for (const std::string& seg : segments) if (!plainName(seg)) return false;

    if (segments.size() == 1) return segments[0] == "index.html";
    if (segments.size() == 2) {
        const bool dirOk = segments[0] == "css" || segments[0] == "js" || segments[0] == "fixtures";
        const bool extOk = endsWith(segments[1], ".js") || endsWith(segments[1], ".css");
        return dirOk && extOk;
    }
    return false;
}

// ---- HttpServer ----------------------------------------------------------------------------------------------

struct HttpServer::Impl {
    ServerConfig cfg;
    SOCKET listener = INVALID_SOCKET;
    int boundPort = 0;
    std::atomic<bool> stopping{false};
    bool wsaStarted = false;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
};

HttpServer::HttpServer(const ServerConfig& config) : impl_(new Impl) {
    impl_->cfg = config;
}

HttpServer::~HttpServer() {
    if (impl_->listener != INVALID_SOCKET) ::closesocket(impl_->listener);
    if (impl_->wsaStarted) WSACleanup();
}

int HttpServer::port() const { return impl_->boundPort; }

void HttpServer::stop() { impl_->stopping.store(true); }

bool HttpServer::start(std::string& error) {
    const ServerConfig& cfg = impl_->cfg;
    if (cfg.host != "127.0.0.1" && cfg.host != "localhost") {
        error = "only the loopback address (127.0.0.1) is supported";
        return false;
    }
    if (cfg.port < 0 || cfg.port > 65535) {
        error = "port must be between 0 and 65535";
        return false;
    }
    if (!cfg.webRoot.empty()) {
        std::ifstream index(cfg.webRoot + "/index.html", std::ios::binary);
        if (!index) {
            error = "web root '" + cfg.webRoot + "' does not contain index.html";
            return false;
        }
    }

    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "WSAStartup failed";
        return false;
    }
    impl_->wsaStarted = true;

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "cannot create a socket";
        return false;
    }
    // Refuse to share the port with another process.
    const BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(cfg.port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        error = "cannot bind 127.0.0.1:" + std::to_string(cfg.port) +
                " (is another server already using it?)";
        ::closesocket(s);
        return false;
    }
    if (::listen(s, SOMAXCONN) != 0) {
        error = "listen failed";
        ::closesocket(s);
        return false;
    }
    int len = sizeof addr;
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        error = "getsockname failed";
        ::closesocket(s);
        return false;
    }
    impl_->boundPort = ntohs(addr.sin_port);
    impl_->listener = s;
    return true;
}

void HttpServer::serve() {
    Impl& impl = *impl_;
    if (impl.listener == INVALID_SOCKET) return;

    auto ctx = std::make_shared<Context>();
    ctx->cfg = impl.cfg;
    ctx->port = impl.boundPort;
    ctx->shared = impl.shared;

    while (!impl.stopping.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(impl.listener, &readSet);
        timeval tv{0, 100 * 1000};  // wake every 100 ms to notice stop()
        const int ready = ::select(0, &readSet, nullptr, nullptr, &tv);
        if (ready == SOCKET_ERROR) break;
        if (ready == 0) continue;

        SOCKET client = ::accept(impl.listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        const BOOL nodelay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);

        bool admitted = false;
        {
            std::lock_guard<std::mutex> lock(impl.shared->mutex);
            if (impl.shared->active < impl.cfg.maxConnections) {
                ++impl.shared->active;
                admitted = true;
            }
        }
        if (!admitted) {
            Response busy = errorResponse(503, "server_busy", "Too many simultaneous connections.");
            const std::string wire = serialize(busy, false);
            sendAll(client, wire.data(), wire.size());
            finishConnection(client);
            continue;
        }

        try {
            std::thread([client, ctx]() {
                try {
                    handleConnection(client, *ctx);
                } catch (...) {
                    ::closesocket(client);
                }
                std::lock_guard<std::mutex> lock(ctx->shared->mutex);
                --ctx->shared->active;
                ctx->shared->cv.notify_all();
            }).detach();
        } catch (...) {
            // Could not start a worker: release the slot and drop the connection.
            ::closesocket(client);
            std::lock_guard<std::mutex> lock(impl.shared->mutex);
            --impl.shared->active;
        }
    }

    ::closesocket(impl.listener);
    impl.listener = INVALID_SOCKET;

    // Let in-flight requests finish (each is bounded by the I/O timeouts).
    std::unique_lock<std::mutex> lock(impl.shared->mutex);
    impl.shared->cv.wait(lock, [&impl] { return impl.shared->active == 0; });
}

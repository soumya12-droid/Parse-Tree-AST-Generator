#pragma once

#include <cstddef>
#include <memory>
#include <string>

// A small local HTTP/1.1 server (Windows, Winsock, no third-party code) that
// connects the web UI to the real C++ analysis pipeline.
//
//   POST /api/analyze   { "source": "..." }  -> contract JSON from analyze()
//   GET  /api/health    liveness probe
//   GET  /, /index.html, /css/*.css, /js/*.js, /fixtures/*.js   the web UI
//
// Everything else is refused. The server binds to loopback only, validates
// Host/Origin (against DNS rebinding and cross-site requests), caps header,
// body and source sizes, applies I/O timeouts, closes every connection after
// one response, and never touches a file outside the fixed static allowlist.
// See docs/frontend-contract.md for the wire contract.

struct ServerConfig {
    std::string host = "127.0.0.1";          // only loopback is accepted
    int port = 8080;                         // 0 = let the OS pick a free port
    std::string webRoot;                     // directory holding index.html, css/, js/, fixtures/
                                             // (empty: serve the API only)
    std::size_t maxBodyBytes = 256 * 1024;   // request body limit  -> HTTP 413
    std::size_t maxSourceBytes = 100 * 1024; // decoded source limit -> HTTP 413
    int ioTimeoutMs = 5000;                  // per read/write; slow clients get 408
    int maxConnections = 32;                 // concurrent connections; more get 503
    bool logRequests = true;                 // one line per request on stderr (never bodies)
};

class HttpServer {
public:
    explicit HttpServer(const ServerConfig& config);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds and listens. Returns false and fills `error` on failure.
    bool start(std::string& error);

    // The bound port (meaningful after a successful start()).
    int port() const;

    // Accepts and serves connections until stop() is called, then waits for
    // in-flight requests to finish and returns. Call from one thread.
    void serve();

    // Asks serve() to return. Safe from any thread and from a console
    // control handler (it only sets an atomic flag).
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// True only for the fixed set of public static paths: "/", "/index.html", and
// "<css|js|fixtures>/<name>.<css|js>" with a plain file name. Exposed for tests.
bool isSafeStaticPath(const std::string& path);

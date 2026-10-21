#include "server/serve_command.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

#include "server/http_server.h"

namespace {

HttpServer* g_server = nullptr;

BOOL WINAPI onConsoleEvent(DWORD event) {
    switch (event) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            if (g_server) g_server->stop();  // only sets an atomic flag
            return TRUE;
        default:
            return FALSE;
    }
}

bool fileExists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

// The web UI directory: next to the build output (build\ptagen.exe -> ..\web),
// or .\web when run from the project root.
std::string findWebRoot(const std::string& exePath) {
    const std::size_t slash = exePath.find_last_of("\\/");
    if (slash != std::string::npos) {
        const std::string candidate = exePath.substr(0, slash) + "\\..\\web";
        if (fileExists(candidate + "\\index.html")) return candidate;
    }
    if (fileExists("web\\index.html")) return "web";
    return "";
}

bool parsePort(const std::string& text, int& port) {
    if (text.empty() || text.size() > 5) return false;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value > 65535) return false;
    port = value;
    return true;
}

}  // namespace

void printServeUsage() {
    std::cout << "  ptagen serve [--port N] [--web DIR] [--quiet] [--stop-on-stdin-close]\n"
              << "                               Start the local server (127.0.0.1 only, default port 8080)\n";
}

int runServeCommand(const std::vector<std::string>& args, const std::string& exePath) {
    ServerConfig config;
    std::string webRoot;
    bool stopOnStdinClose = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--port" && i + 1 < args.size()) {
            if (!parsePort(args[++i], config.port)) {
                std::cerr << "error: --port must be a number from 0 to 65535\n";
                return 2;
            }
        } else if (a == "--web" && i + 1 < args.size()) {
            webRoot = args[++i];
        } else if (a == "--quiet") {
            config.logRequests = false;
        } else if (a == "--stop-on-stdin-close") {
            stopOnStdinClose = true;
        } else {
            std::cerr << "error: unknown or incomplete option '" << a << "' for 'serve'\n";
            printServeUsage();
            return 2;
        }
    }

    config.webRoot = webRoot.empty() ? findWebRoot(exePath) : webRoot;
    if (config.webRoot.empty()) {
        std::cerr << "error: cannot find the web UI (web\\index.html). "
                     "Run from the project root or pass --web DIR.\n";
        return 2;
    }

    HttpServer server(config);
    std::string error;
    if (!server.start(error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    g_server = &server;
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);

    if (stopOnStdinClose) {
        std::thread([&server]() {
            char buffer[256];
            DWORD read = 0;
            HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
            // Returns on end of file (stdin closed) or error.
            while (ReadFile(in, buffer, sizeof buffer, &read, nullptr) && read > 0) {
            }
            server.stop();
        }).detach();
    }

    std::cout << "Listening on http://127.0.0.1:" << server.port() << "/  (Ctrl+C to stop)\n"
              << "Serving " << config.webRoot << " and POST /api/analyze\n" << std::flush;

    server.serve();

    g_server = nullptr;
    std::cout << "Server stopped.\n" << std::flush;
    return 0;
}

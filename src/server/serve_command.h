#pragma once

#include <string>
#include <vector>

// `ptagen serve [--port N] [--web DIR] [--stop-on-stdin-close] [--quiet]`
//
// Starts the local server in the foreground and returns when it has been
// stopped (Ctrl+C, Ctrl+Break, or - with --stop-on-stdin-close - when standard
// input reaches end of file, which is how scripts and tests stop it cleanly).
// Returns a process exit code: 0 clean stop, 2 usage or startup error.
int runServeCommand(const std::vector<std::string>& args, const std::string& exePath);

void printServeUsage();

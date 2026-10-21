# Parse Tree & AST Generator

Compiler Design Lab project (VIT). It takes a program written in a small
assignment-and-arithmetic language and shows what a compiler front end does with
it: the token stream, the concrete parse tree, the abstract syntax tree (AST),
and a clear error message with line and column when the input is invalid.

The lexer, the recursive-descent parser and the AST builder are written by hand
in C++17 (no parser generator). A small local C++ server connects them to a
browser page that draws both trees as SVG.

## Grammar

```
program    -> statement*
statement  -> ID '=' expression ';'
expression -> term (('+' | '-') term)*
term       -> factor (('*' | '/') factor)*
factor     -> NUMBER | ID | '(' expression ')'
```

Identifiers are `[A-Za-z_][A-Za-z0-9_]*`. Numbers are non-negative integers.
The empty program is valid. There are no comments, decimals, unary minus,
strings or keywords.

Example programs:

```
a = b + c * 5;
result = (x + y) * z;
r = a - b - c;
```

## Parse tree vs AST

- The **parse tree** (concrete syntax tree) records every grammar rule the parser
  used, plus every token, including `=`, `;` and parentheses. `b - c - d` is one
  `expression` node with five children.
- The **AST** keeps only the meaning: `Program`, `Assignment`, `Add`, `Subtract`,
  `Multiply`, `Divide`, `Identifier`, `Number`. Punctuation and the
  `expression`/`term`/`factor` layers are gone, precedence comes from the tree
  shape (`b + c * 5` puts `Multiply` under `Add`), and `a - b - c` becomes
  `Subtract(Subtract(a, b), c)`, i.e. left associative.

The AST is built from the parse tree by a separate pass; the parser never builds
it directly.

## Features

- Tokenizer with 1-based line and column for every token.
- Recursive-descent parser that stops at the first error and reports it.
- Error messages for invalid characters, missing operands, unexpected operators,
  a missing `)` or `;`, and a stray `)`, each with its position and the list of
  tokens that were expected.
- Browser UI: editor with line numbers, token table, parse tree and AST drawn as
  SVG, side by side or one at a time.
- Click a node to highlight its source text and tokens and to see its token span.
  Click a token to select its node. Expand/collapse, zoom, Fit, Reset, drag to
  pan, and arrow-key navigation work on both trees.
- A local server (`ptagen serve`) so the page analyzes any program with the real
  C++ code. An offline mode with captured example output also exists.
- Command-line mode: `ptagen analyze`.

## Technology

- C++17 compiled with `clang++`: lexer, parser, AST builder, JSON output, and a
  small HTTP server on Winsock (no third-party libraries).
- HTML, CSS and plain JavaScript; trees are drawn with SVG. No frameworks.
- PowerShell scripts for build, run and test. Node.js (built-in test runner, no
  npm packages) for the frontend tests.

## Project structure

| Path | Contents |
|---|---|
| `src/core/` | Lexer, parser, parse tree, AST, AST builder, analyzer |
| `src/output/` | Text printing and the JSON writer |
| `src/server/` | HTTP server, request parsing, the `serve` command |
| `src/main.cpp` | Command-line entry point (`analyze`, `serve`) |
| `web/` | The UI: `index.html`, `css/`, `js/`, and `fixtures/` (captured example output) |
| `tools/export_fixtures.cpp` | Generates `web/fixtures/` and result files from the real analyzer |
| `tests/unit/`, `tests/cases/` | C++ unit tests and golden files |
| `tests/web/` | Frontend and live-server tests (Node) |
| `scripts/` | `build.ps1`, `run.ps1`, `test.ps1`, `test-web.ps1`, `export-fixtures.ps1` |
| `docs/frontend-contract.md` | The HTTP API and JSON format used by the UI |

How it fits together: the browser sends `POST /api/analyze` with the source text
to `ptagen serve`, which calls the same `analyze()` function as the command line,
turns the result into JSON, and the page draws it. Details are in
[docs/frontend-contract.md](docs/frontend-contract.md).

## Prerequisites

- Windows with PowerShell.
- `clang++` on your `PATH`, with C++17 support. It was developed with LLVM
  22.1.6 on Visual Studio 2019 Build Tools and the Windows SDK (clang needs them
  to link on Windows).
- A browser.
- Node.js (18 or newer) only for the frontend tests; it was run with Node 22.

## Build, run and test

Run these from the project folder.

```powershell
# Build and start the server, then open the UI (http://127.0.0.1:8080/)
powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -Open
```

Stop the server with **Ctrl+C** in its window. Other options:
`-Port 9000` to change the port, `-NoBuild` to skip building.

```powershell
# Build only (output: build\ptagen.exe)
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# Analyze without the browser
.\build\ptagen.exe analyze --text "a = b + c * 5;"
.\build\ptagen.exe analyze path\to\file.txt

# C++ tests
powershell -ExecutionPolicy Bypass -File scripts\test.ps1

# Frontend tests, fixture freshness check, and live tests against the real server
powershell -ExecutionPolicy Bypass -File scripts\test-web.ps1
```

If a build fails because `ptagen.exe` is in use, stop the running server first
(`Stop-Process -Name ptagen`).

What the test commands cover:

- `test.ps1` builds and runs the C++ tests: lexer, parser, AST builder, exact
  error messages, JSON output, the HTTP server over real sockets (including
  malformed and oversized requests), and the golden files in `tests/cases`.
- `test-web.ps1` builds the app, checks that `web/fixtures/*.js` match what the
  current compiler produces, then runs the Node tests, including ones that start
  the real server and compare its answers with the fixtures.
- Browser rendering (layout, appearance, mouse and keyboard use) is not covered
  by automated tests. It was checked by hand.

`scripts\export-fixtures.ps1` regenerates `web/fixtures/`. With
`-File my.src -Out result.json` it analyzes one file offline; load the result in
the UI with "Open result file" when the page is in offline mode.

## Using the app

1. Open `http://127.0.0.1:8080/`. The top bar says **Live** when the page can
   reach the server.
2. Choose an example from **Load example…** or type a program. Loading an example
   only fills the editor; press **Analyze** (or `Ctrl+Enter`) to run it.
3. Read the token table, the parse tree and the AST. Select nodes or tokens to
   see where they come from in the source.
4. For invalid input the status box shows the message, the line and column, the
   expected tokens and the source line. No tree is shown for invalid input.

Editing the text clears the old results so a tree is never shown for text that
did not produce it.

### Error messages

The parser reports only the first error. Examples:

| Input | Message | Expected |
|---|---|---|
| `a = b $ c;` | `Invalid character '$'` (1:7) | - |
| `a = b + * 5;` | `Unexpected token '*'` (1:9) | identifier, number, `(` |
| `a = (b + c;` | `Unexpected token ';'; missing ')' to close '(' at line 1, column 5` (1:11) | `)` |
| `a = b + c` then `d = 1;` | `Unexpected token 'd'; missing ';' after 'c' at line 1, column 9` (2:1) | `;` |
| `a = b c;` | `Unexpected token 'c' after 'b' at line 1, column 5` (1:7) | `+ - * /` or `;` |

The exact wording is pinned by `tests/unit/diagnostic_message_tests.cpp`.

## Limits and limitations

Worth knowing before a demo:

- **Implementation limits (not grammar rules):** parentheses nested more than 256
  deep, or an expression whose syntax tree would be more than 1000 levels tall
  (about 998 operands in one expression), are rejected with an "implementation
  limit" message. They exist because the parser and tree code are recursive and
  a stack overflow cannot be caught. The stack margin is small in unoptimized builds.
- **The server is Windows-only** (Winsock) and implements a small part of
  HTTP/1.1: one request per connection, no TLS, no keep-alive, no chunked
  request bodies. It listens on `127.0.0.1` only and is meant for local use.
  Requests are capped (256 KiB body, 100 KiB of source) and only the UI's own
  files and `/api/*` are served.
- **Fit** shows a whole tree as an overview. A very large tree (for example the
  1000-level example) fits at about 4%, where labels are unreadable; zoom in and
  scroll to read it. Large trees open partly collapsed.
- Only the first syntax error is reported; there is no error recovery.
- The editor is a text area, so Windows line endings are shown as plain line
  breaks. Token positions are not affected.
- Browser testing was done in one browser (an embedded Chromium pane) at about
  1280, 760 and 480 px wide. Other browsers, and opening `web/index.html`
  directly from disk instead of through the server, were not tested.
- Not implemented: declarations, conditionals, loops, functions, a symbol table,
  semantic checks, three-address code.

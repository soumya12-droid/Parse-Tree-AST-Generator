# Parse Tree and AST Generator

An educational compiler front-end (Compiler Design Lab, VIT). It will take source
code for a small assignment/expression language and show, step by step, the token
stream, the concrete parse tree, the Abstract Syntax Tree, and useful syntax errors.

**Status: Phase 0 (project skeleton). No compiler functionality is implemented yet.**

## Planned grammar

```
program    -> statement*
statement  -> ID '=' expression ';'
expression -> term (('+' | '-') term)*
term       -> factor (('*' | '/') factor)*
factor     -> NUMBER | ID | '(' expression ')'
```

## Requirements

- Windows with PowerShell
- `clang++` (C++17) on `PATH`

## Build and run

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
.\build\ptagen.exe
```

## Layout

| Path | Purpose |
|---|---|
| `src/core/` | Lexer, parser, parse tree, AST (compiler logic, no I/O) |
| `src/output/` | JSON and text output |
| `src/server/` | Local HTTP server (later phase) |
| `src/main.cpp` | Entry point |
| `web/` | HTML/CSS/JS visualization |
| `tests/` | Test runner, unit tests, golden cases |
| `docs/` | Grammar, architecture, test matrix |
| `scripts/` | Build, test and run scripts |
| `third_party/` | Vendored single-header libraries (later phase) |

## Roadmap

Phases 0-7: skeleton, lexer, parser and parse tree, AST builder, error handling,
JSON output, web UI and server, polish.

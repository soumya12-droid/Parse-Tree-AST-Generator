# Frontend / server data contract (version 1)

The web UI consumes **one JSON object per analysis**. The C++ types are the
source of truth: this document describes how `AnalysisResult` (`src/core/analyzer.h`)
maps to JSON, and the HTTP API that delivers it. The UI computes no tree itself;
it only draws what is in this object.

## Status

| Piece | State |
|---|---|
| The response contract (below) | Implemented |
| Serializer | `src/output/json_writer.{h,cpp}` - the **single** implementation, used by both the server and the offline fixture exporter, so both produce identical bytes |
| Local HTTP server | **Implemented**: `ptagen serve` (`src/server/`), loopback only |
| Live UI adapter | **Implemented**: `createHttpAnalyzer` in `web/js/analyzer.js` |
| Offline fixture mode | Retained: `createFixtureAnalyzer` + `tools/export_fixtures.cpp` (`?mode=fixtures`, or opening the page without the server) |
| Validator in the UI | `web/js/contract.js` (`validateResponse`); every response is validated before it is shown |

## HTTP API

The server listens on `127.0.0.1` only (default port 8080, `--port N`, `0` = any
free port). It also serves the UI, so the page and the API share one origin.

### `POST /api/analyze`

```
POST /api/analyze HTTP/1.1
Host: 127.0.0.1:8080
Content-Type: application/json
Content-Length: <bytes>

{ "source": "a = b + c * 5;" }
```

* `source` is the program text (any string, including empty). Other members are
  ignored; `source` must appear exactly once and be a string.
* The body must be one strictly valid JSON document in UTF-8 (no BOM, no
  trailing data, nesting depth <= 32).
* **200 OK** - the response object below, for every analysis the compiler
  completed, **including invalid programs** (`ok: false`). A diagnostic about
  the user's program is never an HTTP error.
* Anything else is a transport-level problem and carries an error object, never
  a response object:

```json
{ "error": { "code": "invalid_request", "message": "invalid JSON: ... (byte 12)" } }
```

| Status | `error.code` | When |
|---|---|---|
| 400 | `invalid_request` | Body is not valid JSON/UTF-8, not an object, `source` missing, duplicated or not a string |
| 400 | `malformed_request`, `bad_content_length`, `incomplete_body`, `missing_host` | Malformed HTTP, bad or conflicting `Content-Length`, body shorter than declared, no `Host` header |
| 403 | `forbidden_host`, `forbidden_origin` | `Host` is not `127.0.0.1:<port>` / `localhost:<port>`, or `Origin` is present and not the server's own origin |
| 404 | `not_found` | Unknown path (including anything outside the static allowlist) |
| 405 | `method_not_allowed` | Wrong method; the `Allow` header lists the right one |
| 408 | `request_timeout` | A request that started but stalled (idle connections are closed silently) |
| 411 | `length_required` | POST without `Content-Length` |
| 413 | `body_too_large`, `source_too_large` | Body > 256 KiB, or decoded source > 100 KiB |
| 415 | `unsupported_media_type` | `Content-Type` is not `application/json` (parameters such as `charset` are fine) |
| 431 | `headers_too_large` | Header block > 16 KiB |
| 500 | `internal_error` | An unexpected C++ exception while handling the request |
| 501 | `chunked_not_supported` | `Transfer-Encoding` was used |
| 503 | `server_busy` | More than 32 simultaneous connections |
| 505 | `http_version_not_supported` | Not HTTP/1.0 or 1.1 |

Every response carries `Content-Length`, `Connection: close`,
`Cache-Control: no-store`, `X-Content-Type-Options: nosniff` and a
`Content-Security-Policy` of `default-src 'self'; frame-ancestors 'none'`.
There are **no CORS headers**: only pages served by the server itself can call it.

### `GET /api/health`

`200 {"status":"ok","service":"ptagen","contractVersion":1}`. The UI calls it on
load to decide whether it may call itself "live".

### Static files

Only `GET`/`HEAD` for `/` (= `/index.html`), `/index.html`, `/css/<name>.css`,
`/js/<name>.js` and `/fixtures/<name>.js`, where `<name>` is a plain file name.
Everything else (other directories, `..`, `%`-escapes, backslashes, hidden files,
sub-directories) is a 404. `/favicon.ico` answers 204.

### Restrictions

| Limit | Value | Where |
|---|---|---|
| Request body | 256 KiB | `ServerConfig::maxBodyBytes` |
| Decoded source | 100 KiB | `ServerConfig::maxSourceBytes` |
| Header block / count | 16 KiB / 64 | `http_server.cpp` |
| Concurrent connections | 32 | `ServerConfig::maxConnections` |
| I/O timeout | 5 s per read/write | `ServerConfig::ioTimeoutMs` |
| JSON nesting in the request | 32 | `kMaxJsonDepth` |
| Parenthesis nesting / AST height | 256 / 1000 | compiler limits, reported as `kind: "limit"` |

The server logs one line per request (`METHOD path -> status (ms)`) to stderr;
it never logs bodies or programs.

## Response object

```jsonc
{
  "contractVersion": 1,
  "source": "a = b + c * 5;",   // exactly the analysed text
  "ok": true,                   // AnalysisResult::ok()
  "tokenized": true,            // AnalysisResult::tokenized (lexing succeeded)
  "tokens": [ Token, ... ],
  "diagnostics": [ Diagnostic, ... ],
  "parseTree": ParseNode | null,
  "ast": AstNode | null
}
```

### Invariants (enforced by the C++ analyzer and re-checked by the UI)

* `ok == false` implies `parseTree == null` **and** `ast == null`. A failed
  analysis never carries a tree.
* `ok == true` implies `diagnostics` is empty, `tokenized` is true and `tokens`
  ends with exactly one `END` token.
* `tokenized == false` (lexical error) implies there is **no** `END` token and
  `tokens` holds only what was recognised before/around the invalid characters.
* At most one syntax diagnostic is reported (the parser stops at the first error);
  lexical errors may be several.
* `kind == "lexical"` exactly when `phase == "lexical"`; `kind == "limit"` only
  in the syntax phase and with an empty `expected`.

### Token

| Field | Type | Meaning |
|---|---|---|
| `index` | int | Position in `tokens`; the unit of every token span |
| `type` | string | `ID NUMBER ASSIGN PLUS MINUS MUL DIV LPAREN RPAREN SEMICOLON END` |
| `lexeme` | string | Exact source text; empty for `END` |
| `line`, `column` | int | 1-based; columns count code points, a tab is one column |

### Diagnostic

| Field | Type | Meaning |
|---|---|---|
| `phase` | `"lexical"` \| `"syntax"` | `Diagnostic::phase` |
| `kind` | `"lexical"` \| `"syntax"` \| `"limit"` | `limit` = `Diagnostic::implementationLimit`: rejected by an implementation safety limit, not by the grammar |
| `message` | string | Compiler message |
| `line`, `column` | int | 1-based position of the offending text |
| `found` | string | Offending text; empty at end of input |
| `expected` | string[] | Token types the parser was looking for (`Diagnostic::expected`). A missing `)` or `;` lists just that delimiter; an operand-like token (identifier, number, `(`) directly after an operand lists the four operators plus the delimiter; empty for lexical and limit errors. Message wording is documented in the README ("Error messages"); clients must classify by `kind`/`phase`, never by message text |

### ParseNode (concrete parse tree)

```jsonc
{ "id": "p4", "symbol": "expression",   // program|statement|expression|term|factor|terminal
  "firstToken": 2, "endToken": 7,       // [firstToken, endToken) - endToken EXCLUSIVE
  "token": { "index": 2, "type": "ID", "lexeme": "b", "line": 1, "column": 5 },  // terminals only
  "children": [ ParseNode, ... ] }
```

One node per grammar production plus every consumed token (punctuation
included). `expression` and `term` are flat: `b - c - d` is one `expression`
with five children; associativity is visible only in the AST.

### AstNode

```jsonc
{ "id": "a3", "kind": "Add",            // Program|Assignment|Add|Subtract|Multiply|Divide|Identifier|Number
  "value": "b",                         // Identifier and Number only
  "firstToken": 2, "endToken": 7,
  "children": [ AstNode, ... ] }
```

`Program` children are `Assignment`s; `Assignment` has `[Identifier, value]`;
binary kinds have `[left, right]`. Parentheses leave no node.

### Token spans

`[firstToken, endToken)` are indices into `tokens`, `endToken` exclusive, never
covering `END`. An empty span (`firstToken == endToken`, e.g. the empty program)
maps to no source text. A binary operation's span covers both operands,
including parentheses around them; a parenthesised operand's own node covers
only what is inside the parentheses; an `Assignment` includes its `;`.
Source ranges are derived from the **tokens' own line/column** (`web/js/sourcemap.js`).

### Identifiers

`id` is unique within one tree and stable for one response (`p<N>` / `a<N>`,
pre-order). It is for UI selection only and carries no meaning across analyses.

## Newlines

The editor (a `<textarea>`) reports `\n` for every line break. The UI therefore
normalises `\r\n` and lone `\r` to `\n` before mapping, and compares the
`source` echoed by the server with what it sent after the same normalisation.
This changes no line/column: the lexer treats each of them as one line break and
no token spans one.

## How the UI uses it

```
editor text --POST /api/analyze--> ptagen serve --analyze()--> json_writer --> JSON
   ^                                                                              |
   |  createHttpAnalyzer: validate (contract.js), check source echo, map failures |
   +-- state.js (analyzing / ready / error ...) <---------------------------------+
```

`createHttpAnalyzer` returns `{status:'ok', response}` for every 200 (including
`ok:false`), and `{status:'error', kind, message}` for transport failures
(`network`, `timeout`), HTTP errors (`http`) and unusable answers
(`invalid-response`: not JSON, fails validation, or answers a different source).
A request that starts clears the previous results, and so does a failure.

## Offline workflow (retained)

```powershell
# Regenerate the bundled example outputs from the real compiler
powershell -ExecutionPolicy Bypass -File scripts\export-fixtures.ps1

# Analyse your own file offline and open the result in the UI ("Open result file...")
powershell -ExecutionPolicy Bypass -File scripts\export-fixtures.ps1 -File my.src -Out result.json
```

`scripts\test-web.ps1` fails if the committed fixtures differ from what the
current compiler emits, and a live test checks that the server returns exactly
the fixture responses for every fixture source.

'use strict';
// End-to-end tests against the REAL server process (build\ptagen.exe serve),
// using real HTTP and the real frontend adapter and state code.

const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const h = require('./helpers.js');

let server;

test.before(async () => { server = await h.startServer(); });
test.after(async () => { if (server) await server.stop(); });

async function post(source, init) {
  return fetch(server.baseUrl + '/api/analyze', Object.assign({
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source })
  }, init || {}));
}

// Raw HTTP so we can send things fetch() refuses to.
function raw(bytes) {
  return new Promise((resolve) => {
    const s = net.connect(server.port, '127.0.0.1');
    let data = '';
    s.on('data', (d) => { data += d; });
    s.on('close', () => resolve(data));
    s.on('error', () => resolve(data));
    s.write(bytes);
  });
}
const statusOf = (reply) => Number(/^HTTP\/1\.1 (\d+)/.exec(reply)?.[1] || 0);

test('the server answers the health probe', async () => {
  const res = await fetch(server.baseUrl + '/api/health');
  assert.equal(res.status, 200);
  assert.deepEqual(await res.json(), { status: 'ok', service: 'ptagen', contractVersion: 1 });
});

test('the web UI is served over HTTP', async () => {
  const res = await fetch(server.baseUrl + '/');
  assert.equal(res.status, 200);
  assert.match(await res.text(), /<title>Parse Tree and AST Generator<\/title>/);
  for (const p of ['/js/app.js', '/js/analyzer.js', '/css/style.css', '/fixtures/fixtures.js']) {
    assert.equal((await fetch(server.baseUrl + p)).status, 200, p);
  }
});

test('live responses equal the captured compiler output for every fixture', async () => {
  // The fixtures were produced by the same C++ pipeline offline; the live
  // server must produce exactly the same contract JSON for the same source.
  for (const f of h.fixtures) {
    const res = await post(f.source);
    assert.equal(res.status, 200, f.id);
    const live = await res.json();
    assert.deepEqual(live, h.response(f.id), f.id);
    assert.deepEqual(h.contract.validateResponse(live), [], f.id);
  }
});

test('live analysis works for programs that have no fixture', async () => {
  const source = 'total = (price + tax) * qty - discount / 2;\nz = total;';
  const res = await post(source);
  assert.equal(res.status, 200);
  const live = await res.json();
  assert.equal(live.ok, true);
  assert.equal(live.source, source);
  assert.deepEqual(h.contract.validateResponse(live), []);
  assert.equal(h.astShape(live.ast),
    'Program(Assignment(Identifier(total), ' +
    'Subtract(Multiply(Add(Identifier(price), Identifier(tax)), Identifier(qty)), ' +
    'Divide(Identifier(discount), Number(2)))), Assignment(Identifier(z), Identifier(total)))');
});

test('compiler diagnostics are 200 responses with no trees; limits are classified', async () => {
  const lexical = await (await post('a = b $ c;')).json();
  assert.equal(lexical.ok, false);
  assert.equal(lexical.diagnostics[0].kind, 'lexical');
  assert.equal(lexical.parseTree, null);
  assert.equal(lexical.ast, null);

  const syntax = await (await post('a = b + * 5;')).json();
  assert.equal(syntax.diagnostics[0].kind, 'syntax');
  assert.deepEqual(syntax.diagnostics[0].expected, ['ID', 'NUMBER', 'LPAREN']);

  const nesting = await (await post('a = ' + '('.repeat(257) + 'b' + ')'.repeat(257) + ';')).json();
  assert.equal(nesting.diagnostics[0].kind, 'limit');
  const height = await (await post('r = 1' + '+1'.repeat(998) + ';')).json();
  assert.equal(height.diagnostics[0].kind, 'limit');
  assert.equal(height.ast, null);
});

test('malformed requests are refused with JSON errors and never as analysis results', async () => {
  const cases = [
    ['invalid JSON', { body: '{nope' }, 400],
    ['missing field', { body: '{"other":1}' }, 400],
    ['wrong field type', { body: '{"source":5}' }, 400],
    ['empty body', { body: '' }, 400],
    ['wrong content type', { headers: { 'Content-Type': 'text/plain' }, body: '{"source":"a"}' }, 415],
  ];
  for (const [name, init, status] of cases) {
    const res = await post('', init);
    assert.equal(res.status, status, name);
    const body = await res.json();
    assert.ok(body.error && typeof body.error.code === 'string', name);
    assert.equal('ok' in body, false, name);
  }
  assert.equal((await fetch(server.baseUrl + '/api/analyze')).status, 405);
  assert.equal((await fetch(server.baseUrl + '/api/analyze', { method: 'PUT', body: '{}' })).status, 405);
  assert.equal((await fetch(server.baseUrl + '/api/nope')).status, 404);
  assert.equal((await fetch(server.baseUrl + '/..%2f..%2fREADME.md')).status, 404);
});

test('oversized payloads are rejected', async () => {
  const res = await post('x'.repeat(300 * 1024));
  assert.equal(res.status, 413);
  assert.equal((await res.json()).error.code, 'body_too_large');
  const justSource = await post(' '.repeat(100 * 1024 + 1));
  assert.equal(justSource.status, 413);
  assert.equal((await justSource.json()).error.code, 'source_too_large');
});

test('foreign Host and Origin headers are refused', async () => {
  const bad = await raw(`GET /api/health HTTP/1.1\r\nHost: evil.example\r\n\r\n`);
  assert.equal(statusOf(bad), 403);
  const origin = await raw(`POST /api/analyze HTTP/1.1\r\nHost: 127.0.0.1:${server.port}\r\n` +
    `Origin: http://evil.example\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}`);
  assert.equal(statusOf(origin), 403);
});

test('the server stays responsive after malformed and abusive requests', async () => {
  await raw('GARBAGE\r\n\r\n');
  await raw('POST /api/analyze HTTP/1.1\r\nHost: x\r\nContent-Length: 999999999\r\n\r\n');
  await raw(`POST /api/analyze HTTP/1.1\r\nHost: 127.0.0.1:${server.port}\r\n` +
    'Content-Type: application/json\r\nContent-Length: 50\r\n\r\n{"sou');       // truncated body
  for (let i = 0; i < 10; i++) net.connect(server.port, '127.0.0.1').destroy();
  const res = await post('a = b + c * 5;');
  assert.equal(res.status, 200);
  assert.deepEqual(await res.json(), h.response('precedence'));
});

test('results never leak between consecutive and concurrent requests', async () => {
  const sources = ['a = 1;', 'b = 2 + 3;', '', 'a = $;', 'q = (r);'];
  const results = await Promise.all(sources.concat(sources).map(async (s) => (await post(s)).json()));
  results.forEach((r, i) => assert.equal(r.source, sources.concat(sources)[i]));
  assert.equal(results[2].ok, true);
  assert.equal(results[3].ok, false);
});

// ---- The real frontend adapter against the real server ------------------------------------

test('createHttpAnalyzer gets real results from the live server', async () => {
  const adapter = h.analyzer.createHttpAnalyzer({ baseUrl: server.baseUrl });
  assert.equal(adapter.live, true);
  assert.deepEqual(await adapter.health(), { ok: true, message: '' });

  const ok = await adapter.analyze('result = (x + y) * z;');
  assert.equal(ok.status, 'ok');
  assert.deepEqual(ok.provenance, { kind: 'live' });
  assert.deepEqual(ok.response, h.response('parentheses'));

  const bad = await adapter.analyze('a = b + * 5;');
  assert.equal(bad.status, 'ok');              // a diagnostic is a result, not a failure
  assert.equal(bad.response.ok, false);
  assert.equal(bad.response.ast, null);
});

test('the full path (adapter -> state) shows trees only for the current source', async () => {
  const adapter = h.analyzer.createHttpAnalyzer({ baseUrl: server.baseUrl });
  const S = h.state;
  let s = S.sourceChanged(S.initialState(), 'a = b + c * 5;');
  s = S.analysisStarted(s);
  assert.equal(S.deriveStatus(s).kind, 'analyzing');
  assert.equal(s.models, null);
  const outcome = await adapter.analyze('a = b + c * 5;');
  s = S.withResults(s, outcome.response, outcome.provenance);
  assert.equal(S.deriveStatus(s).kind, 'success');
  assert.ok(s.models.ast);
  assert.match(S.deriveStatus(s).detail, /16 parse-tree nodes, 8 AST nodes/);
  // Any edit clears them again.
  s = S.sourceChanged(s, 'a = b + c * 6;');
  assert.equal(s.models, null);
});

// ---- Shutdown (runs last: it stops the server) ------------------------------------------------

test('the server stops cleanly when told to', async () => {
  const stopped = await server.stop();
  server = null;
  assert.equal(stopped.code, 0);
  assert.match(stopped.output, /Server stopped\./);
});

test('a second server can start and stop independently (port 0)', async () => {
  const other = await h.startServer();
  assert.ok(other.port > 0);
  assert.equal((await fetch(other.baseUrl + '/api/health')).status, 200);
  const stopped = await other.stop();
  assert.equal(stopped.code, 0);
  await assert.rejects(fetch(other.baseUrl + '/api/health'));
});

test('serve refuses an occupied port and non-loopback hosts', async () => {
  const first = await h.startServer();
  const { spawnSync } = require('node:child_process');
  const exe = process.env.PTAGEN_EXE || require('node:path').join(__dirname, '..', '..', 'build', 'ptagen.exe');
  const clash = spawnSync(exe, ['serve', '--port', String(first.port), '--quiet'], { encoding: 'utf8', timeout: 8000 });
  assert.equal(clash.status, 2);
  assert.match(clash.stderr, /cannot bind/);
  const badPort = spawnSync(exe, ['serve', '--port', '99999'], { encoding: 'utf8', timeout: 8000 });
  assert.equal(badPort.status, 2);
  const badOption = spawnSync(exe, ['serve', '--host', '0.0.0.0'], { encoding: 'utf8', timeout: 8000 });
  assert.equal(badOption.status, 2);   // there is no way to ask for a public bind
  await first.stop();
});

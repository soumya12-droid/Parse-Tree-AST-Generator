'use strict';
// The live adapter's failure handling, with a fake fetch (no server needed).

const test = require('node:test');
const assert = require('node:assert/strict');
const h = require('./helpers.js');

function adapterWith(fetchImpl, extra) {
  return h.analyzer.createHttpAnalyzer(Object.assign({ baseUrl: 'http://x', fetch: fetchImpl }, extra));
}

const replying = (status, body, statusText) => async () => ({
  ok: status >= 200 && status < 300, status, statusText: statusText || '',
  text: async () => (typeof body === 'string' ? body : JSON.stringify(body)),
  json: async () => (typeof body === 'string' ? JSON.parse(body) : body)
});

test('a good response is returned with live provenance', async () => {
  const resp = h.response('precedence');
  const out = await adapterWith(replying(200, resp)).analyze(resp.source);
  assert.equal(out.status, 'ok');
  assert.deepEqual(out.provenance, { kind: 'live' });
  assert.deepEqual(out.response, resp);
});

test('the request is a JSON POST of exactly the given source', async () => {
  let seen;
  const resp = h.response('precedence');
  await adapterWith(async (url, init) => {
    seen = { url, init };
    return replying(200, resp)();
  }).analyze(resp.source);
  assert.equal(seen.url, 'http://x/api/analyze');
  assert.equal(seen.init.method, 'POST');
  assert.equal(seen.init.headers['Content-Type'], 'application/json');
  assert.deepEqual(JSON.parse(seen.init.body), { source: resp.source });
});

test('a compiler diagnostic is a normal result, not an error', async () => {
  const resp = h.response('error-unexpected-operator');
  const out = await adapterWith(replying(200, resp)).analyze(resp.source);
  assert.equal(out.status, 'ok');
  assert.equal(out.response.ok, false);
});

test('network failure becomes a network error with guidance', async () => {
  const out = await adapterWith(async () => { throw new TypeError('fetch failed'); }).analyze('a = 1;');
  assert.equal(out.status, 'error');
  assert.equal(out.kind, 'network');
  assert.match(out.message, /Could not reach the analysis server/);
  assert.match(out.message, /scripts\\run\.ps1/);
});

test('a stalled request times out', async () => {
  const hang = (url, init) => new Promise((resolve, reject) => {
    init.signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), { name: 'AbortError' })));
  });
  const out = await adapterWith(hang, { timeoutMs: 50 }).analyze('a = 1;');
  assert.equal(out.status, 'error');
  assert.equal(out.kind, 'timeout');
});

test('HTTP errors surface the server message and status', async () => {
  const body = { error: { code: 'body_too_large', message: 'Request body exceeds 262144 bytes.' } };
  const out = await adapterWith(replying(413, body, 'Payload Too Large')).analyze('a = 1;');
  assert.equal(out.status, 'error');
  assert.equal(out.kind, 'http');
  assert.equal(out.httpStatus, 413);
  assert.match(out.message, /exceeds 262144 bytes/);
  assert.match(out.message, /HTTP 413/);
});

test('HTTP errors without a JSON body still produce a clear message', async () => {
  const out = await adapterWith(replying(502, '<html>bad gateway</html>', 'Bad Gateway')).analyze('a');
  assert.equal(out.kind, 'http');
  assert.match(out.message, /HTTP 502/);
});

test('a 200 that is not JSON, or not a valid result, is rejected', async () => {
  const notJson = await adapterWith(replying(200, 'hello')).analyze('a = 1;');
  assert.equal(notJson.status, 'error');
  assert.equal(notJson.kind, 'invalid-response');

  const wrong = await adapterWith(replying(200, { hello: 1 })).analyze('a = 1;');
  assert.equal(wrong.kind, 'invalid-response');

  // ok:true but no AST: violates the contract invariants
  const broken = JSON.parse(JSON.stringify(h.response('precedence')));
  broken.ast = null;
  const out = await adapterWith(replying(200, broken)).analyze(broken.source);
  assert.equal(out.kind, 'invalid-response');
  assert.match(out.message, /malformed/);
});

test('a result for a different source is discarded', async () => {
  const resp = h.response('precedence');
  const out = await adapterWith(replying(200, resp)).analyze('something = else;');
  assert.equal(out.status, 'error');
  assert.equal(out.kind, 'invalid-response');
  assert.match(out.message, /different source/);
});

test('CRLF vs LF does not make a correct answer look like a different source', async () => {
  const resp = h.response('multiple-statements');
  const out = await adapterWith(replying(200, resp)).analyze(resp.source.replace(/\n/g, '\r\n'));
  assert.equal(out.status, 'ok');
});

test('health reports a dead or foreign server', async () => {
  const down = await adapterWith(async () => { throw new TypeError('nope'); }).health();
  assert.equal(down.ok, false);
  assert.match(down.message, /Could not reach/);
  const foreign = await adapterWith(replying(200, { status: 'ok', service: 'something-else' })).health();
  assert.equal(foreign.ok, false);
  const fine = await adapterWith(replying(200, { status: 'ok', service: 'ptagen' })).health();
  assert.equal(fine.ok, true);
});

test('state: a failed request clears every earlier result', () => {
  const S = h.state;
  const ok = h.loadedState('precedence');
  assert.ok(ok.models);
  const started = S.analysisStarted(ok);
  assert.equal(started.models, null);
  assert.equal(started.response, null);
  assert.equal(S.deriveStatus(started).kind, 'analyzing');
  assert.deepEqual(S.highlightRanges(started), []);

  const failed = S.analysisFailed(ok, 'network', 'down');
  assert.equal(failed.models, null);
  assert.equal(failed.response, null);
  assert.equal(failed.selection, null);
  const st = S.deriveStatus(failed);
  assert.equal(st.kind, 'error');
  assert.equal(st.title, 'Cannot reach the server');
  assert.equal(st.detail, 'down');
  assert.equal(S.selectionDetails(failed), null);

  assert.equal(S.deriveStatus(S.analysisFailed(ok, 'timeout', 'x')).title, 'The server timed out');
  assert.equal(S.deriveStatus(S.analysisFailed(ok, 'http', 'x')).title, 'The server rejected the request');
  assert.equal(S.deriveStatus(S.analysisFailed(ok, 'invalid-response', 'x')).title, 'Unusable server response');
});

test('state: server errors are distinct from compiler diagnostics', () => {
  const S = h.state;
  const kinds = new Set([
    S.deriveStatus(h.loadedState('error-lexical')).kind,
    S.deriveStatus(h.loadedState('error-unexpected-operator')).kind,
    S.deriveStatus(h.loadedState('limit-ast-over')).kind,
    S.deriveStatus(S.analysisFailed(S.initialState(), 'network', 'm')).kind,
    S.deriveStatus(S.analysisStarted(S.initialState())).kind
  ]);
  assert.equal(kinds.size, 5);
});

test('state: editing while a request is in flight clears the pending state', () => {
  const S = h.state;
  let s = S.sourceChanged(S.initialState(), 'a = 1;');
  s = S.analysisStarted(s);
  s = S.sourceChanged(s, 'a = 12;');
  assert.equal(S.deriveStatus(s).kind, 'edited');
  assert.equal(s.models, null);
});

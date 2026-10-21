'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const h = require('./helpers.js');

function adapter(loader) {
  return h.analyzer.createFixtureAnalyzer({
    fixtures: h.fixtures,
    loadStressResponses: loader || (() => Promise.resolve(h.stress))
  });
}

test('the offline adapter reports that it is not live', () => {
  const a = adapter();
  assert.equal(a.live, false);
  assert.match(a.label, /no live compiler/i);
});

test('examples come from the fixtures and include the required six', () => {
  const ex = adapter().examples();
  assert.equal(ex.length, h.fixtures.length);
  assert.ok(ex.every((e) => e.id && e.group && typeof e.source === 'string'));
  assert.equal(ex.filter((e) => e.group === 'Examples').length, 6);
});

test('analyzing an example source returns its captured response', async () => {
  const a = adapter();
  const out = await a.analyze('a = b + c * 5;');
  assert.equal(out.status, 'ok');
  assert.equal(out.response.source, 'a = b + c * 5;');
  assert.equal(out.provenance.kind, 'fixture');
});

test('every inline fixture analyses to exactly its own response', async () => {
  const a = adapter();
  for (const f of h.fixtures) {
    const out = await a.analyze(f.source);
    assert.equal(out.status, 'ok', f.id);
    assert.equal(out.response.source, f.source, f.id);
    assert.equal(out.provenance.id, f.id, f.id);
  }
});

test('an unknown source is unavailable and fabricates nothing', async () => {
  const out = await adapter().analyze('q = 9 + 9;');
  assert.equal(out.status, 'unavailable');
  assert.equal(out.response, undefined);
  assert.match(out.message, /Offline mode/);
  assert.match(out.message, /scripts\\run\.ps1/);
});

test('sources are matched ignoring CRLF vs LF', async () => {
  const out = await adapter().analyze('a = 1;\r\nb = a + 2;\r\nc = (a + b) * c;');
  assert.equal(out.status, 'ok');
  assert.equal(out.provenance.id, 'multiple-statements');
});

test('lazy stress results are fetched on demand', async () => {
  let loads = 0;
  const a = adapter(() => { loads++; return Promise.resolve(h.stress); });
  const out = await a.analyze(h.fixture('stress-ast-at-limit').source);
  assert.equal(out.status, 'ok');
  assert.equal(loads, 1);
});

test('a failed lazy load is reported, not hidden', async () => {
  const a = adapter(() => Promise.reject(new Error('offline')));
  const out = await a.analyze(h.fixture('stress-ast-at-limit').source);
  assert.equal(out.status, 'unavailable');
  assert.match(out.message, /offline/);
});

test('a malformed captured response is refused', async () => {
  const bad = JSON.parse(JSON.stringify(h.fixture('precedence')));
  bad.response.ast = null;                       // ok:true without an AST
  const a = h.analyzer.createFixtureAnalyzer({ fixtures: [bad] });
  const out = await a.analyze(bad.source);
  assert.equal(out.status, 'unavailable');
  assert.match(out.message, /malformed/);
});

test('result files: valid JSON is accepted and takes precedence for its source', async () => {
  const a = adapter();
  const loaded = a.loadResultText(JSON.stringify(h.response('left-associativity')), 'mine.json');
  assert.equal(loaded.status, 'ok');
  assert.equal(loaded.provenance.kind, 'file');
  assert.equal(loaded.provenance.name, 'mine.json');

  const out = await a.analyze('r = a - b - c;');
  assert.equal(out.provenance.kind, 'file');          // the loaded file wins
  const other = await adapter().analyze('r = a - b - c;');
  assert.equal(other.provenance.kind, 'fixture');     // another adapter is unaffected
});

test('result files: invalid JSON and wrong shapes are rejected', () => {
  const a = adapter();
  assert.equal(a.loadResultText('{not json', 'x.json').status, 'unavailable');
  assert.equal(a.loadResultText('{"contractVersion":99}', 'x.json').status, 'unavailable');
  assert.equal(a.loadResultText('[]', 'x.json').status, 'unavailable');
});

test('a result file for a new source can then be analysed from the editor', async () => {
  const a = h.analyzer.createFixtureAnalyzer({ fixtures: [] });
  const resp = h.response('mixed-operators');
  const loaded = a.loadResultText(JSON.stringify(resp), 'm.json');
  assert.equal(loaded.status, 'ok');
  const out = await a.analyze(resp.source);
  assert.equal(out.status, 'ok');
  assert.equal(out.provenance.kind, 'file');
});

test('contract validation catches broken invariants', () => {
  const good = h.response('precedence');
  const v = h.contract.validateResponse;
  assert.deepEqual(v(good), []);

  const withTreeOnError = JSON.parse(JSON.stringify(h.response('error-unexpected-operator')));
  withTreeOnError.ast = good.ast;
  assert.ok(v(withTreeOnError).length > 0);

  const badSpan = JSON.parse(JSON.stringify(good));
  badSpan.ast.children[0].endToken = 99;
  assert.ok(v(badSpan).length > 0);

  const noEnd = JSON.parse(JSON.stringify(good));
  noEnd.tokens.pop();
  assert.ok(v(noEnd).length > 0);

  const wrongVersion = { ...good, contractVersion: 2 };
  assert.ok(v(wrongVersion).length > 0);
});

test('contract validation checks diagnostic phase/kind consistency', () => {
  const v = h.contract.validateResponse;
  const mutate = (id, edit) => {
    const r = JSON.parse(JSON.stringify(h.response(id)));
    edit(r.diagnostics[0]);
    return r;
  };
  assert.deepEqual(v(h.response('limit-ast-over')), []);
  // a lexical-kind diagnostic in the syntax phase
  assert.ok(v(mutate('error-unexpected-operator', (d) => { d.kind = 'lexical'; })).length > 0);
  // a syntax-kind diagnostic in the lexical phase
  assert.ok(v(mutate('error-lexical', (d) => { d.kind = 'syntax'; })).length > 0);
  // a limit that lists expected tokens
  assert.ok(v(mutate('limit-ast-over', (d) => { d.expected = ['ID']; })).length > 0);
  // limit in the lexical phase
  assert.ok(v(mutate('limit-ast-over', (d) => { d.phase = 'lexical'; })).length > 0);
  // unknown kind
  assert.ok(v(mutate('limit-ast-over', (d) => { d.kind = 'other'; })).length > 0);
});

test('expected-token wording mirrors the compiler messages', () => {
  const d = h.contract.describeExpected;
  assert.equal(d(['ID', 'NUMBER', 'LPAREN']), "identifier, number, or '('");
  assert.equal(d(['ID', 'END']), 'identifier or end of input');
  assert.equal(d(['RPAREN']), "')'");
});

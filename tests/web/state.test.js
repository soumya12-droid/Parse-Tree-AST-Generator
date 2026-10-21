'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const h = require('./helpers.js');

const S = h.state;

function selectByKind(s, tree, predicate) {
  const model = s.models[tree];
  const v = model.order.find(predicate);
  assert.ok(v, 'node found');
  return S.select(s, tree, v.id);
}

test('a fresh state has no results and says so', () => {
  const s = S.initialState();
  assert.equal(S.deriveStatus(s).kind, 'idle');
  assert.equal(s.models, null);
  assert.deepEqual(S.highlightRanges(s), []);
});

test('loading an example into the editor does not run or show an analysis', () => {
  const s = S.sourceChanged(S.initialState(), 'a = b + c * 5;');
  assert.equal(s.response, null);
  assert.equal(s.models, null);
  assert.equal(S.deriveStatus(s).kind, 'idle');
});

test('successful analysis exposes both trees and a success status', () => {
  const s = h.loadedState('precedence');
  assert.equal(s.outcome, 'ready');
  assert.ok(s.models.parse && s.models.ast);
  const st = S.deriveStatus(s);
  assert.equal(st.kind, 'success');
  assert.match(st.detail, /1 statement, 8 tokens, 16 parse-tree nodes, 8 AST nodes/);
});

test('editing the source after a success clears results (no stale trees)', () => {
  const ok = h.loadedState('precedence');
  const edited = S.sourceChanged(ok, 'a = b + c * 6;');
  assert.equal(edited.response, null);
  assert.equal(edited.models, null);
  assert.equal(edited.selection, null);
  assert.equal(S.deriveStatus(edited).kind, 'edited');
  assert.deepEqual(S.highlightRanges(edited), []);
  assert.equal(S.selectionDetails(edited), null);
});

test('typing back the same text does not resurrect the old results', () => {
  const ok = h.loadedState('precedence');
  const edited = S.sourceChanged(ok, 'a = b + c * 6;');
  const back = S.sourceChanged(edited, 'a = b + c * 5;');
  assert.equal(back.response, null);
  assert.equal(back.models, null);
});

test('re-sending identical text keeps the results', () => {
  const ok = h.loadedState('precedence');
  const same = S.sourceChanged(ok, ok.source);
  assert.ok(same.response);
  assert.ok(same.models);
});

test('loading another example while results are shown clears them', () => {
  const ok = h.loadedState('precedence');
  const next = S.sourceChanged(ok, h.fixture('parentheses').source);
  assert.equal(next.models, null);
  assert.equal(S.deriveStatus(next).kind, 'edited');
});

test('each failure gives a distinct status and never any trees', () => {
  const cases = {
    'error-lexical': 'lexical',
    'error-unexpected-operator': 'syntax',
    'error-missing-paren': 'syntax',
    'error-missing-semicolon': 'syntax',
    'limit-nesting-over': 'limit',
    'limit-ast-over': 'limit'
  };
  for (const [id, kind] of Object.entries(cases)) {
    const s = h.loadedState(id);
    assert.equal(S.deriveStatus(s).kind, kind, id);
    assert.equal(s.models, null, id + ': no view trees');
    assert.equal(s.response.parseTree, null, id);
    assert.equal(s.response.ast, null, id);
    assert.equal(S.selectionDetails(s), null, id);
  }
});

test('status comes from the diagnostic kind, never from the message text', () => {
  // A syntax error whose message happens to mention "too complex" is not a limit.
  const r = JSON.parse(JSON.stringify(h.response('error-unexpected-operator')));
  r.diagnostics[0].message = 'Expression too complex: nested too deeply (implementation limit)';
  const s = S.withResults(S.sourceChanged(S.initialState(), r.source), r, { kind: 'file', name: 'x' });
  assert.equal(S.deriveStatus(s).kind, 'syntax');
  // And a limit diagnostic is a limit whatever its message says.
  const l = JSON.parse(JSON.stringify(h.response('limit-ast-over')));
  l.diagnostics[0].message = 'something else entirely';
  const ls = S.withResults(S.sourceChanged(S.initialState(), l.source), l, { kind: 'file', name: 'x' });
  assert.equal(S.deriveStatus(ls).kind, 'limit');
  assert.match(S.deriveStatus(ls).detail, /not a rule of the language/);
});

test('a failure after a success leaves no stale tree in the state', () => {
  const ok = h.loadedState('precedence');
  assert.ok(ok.models);
  const bad = S.withResults(S.sourceChanged(ok, 'a = b + * 5;'),
    h.response('error-unexpected-operator'), { kind: 'fixture', id: 'x', title: 'x' });
  assert.equal(bad.models, null);
  assert.equal(bad.selection, null);
  assert.equal(S.deriveStatus(bad).kind, 'syntax');
});

test('unavailable analysis clears earlier results', () => {
  const ok = h.loadedState('precedence');
  const un = S.analysisUnavailable(S.sourceChanged(ok, 'q = 1;'), 'no output');
  assert.equal(un.response, null);
  assert.equal(un.models, null);
  const st = S.deriveStatus(un);
  assert.equal(st.kind, 'unavailable');
  assert.equal(st.detail, 'no output');
});

test('the empty program is a valid, distinct state', () => {
  const s = h.loadedState('empty-program');
  const st = S.deriveStatus(s);
  assert.equal(st.kind, 'empty');
  assert.equal(s.models.ast.count, 1);
  assert.equal(s.models.parse.count, 1);
});

test('error diagnostics are marked in the source at the reported position', () => {
  const s = h.loadedState('error-unexpected-operator');
  const ranges = S.highlightRanges(s);
  assert.equal(ranges.length, 1);
  assert.equal(s.source.slice(ranges[0].start, ranges[0].end), '*');
  assert.equal(ranges[0].cls, 'error');

  const eof = h.loadedState('error-missing-semicolon');
  const er = S.highlightRanges(eof)[0];
  assert.equal(eof.source.slice(er.start, er.end), 'd');

  const lex = h.loadedState('error-lexical');
  const lr = S.highlightRanges(lex)[0];
  assert.equal(lex.source.slice(lr.start, lr.end), '$');
});

test('the offending token row is marked for syntax errors', () => {
  const s = h.loadedState('error-unexpected-operator');
  const marks = S.tokenMarks(s);
  assert.equal(marks.error, 4);                       // the '*' token
  assert.equal(s.response.tokens[4].lexeme, '*');
});

test('selecting an AST node reports its kind, span and source range', () => {
  const s = selectByKind(h.loadedState('precedence'), 'ast', (v) => v.raw.kind === 'Add');
  const d = S.selectionDetails(s);
  assert.equal(d.treeName, 'AST');
  assert.equal(d.name, 'Add');
  assert.equal(d.first, 2);
  assert.equal(d.end, 7);
  assert.equal(d.tokenCount, 5);
  assert.equal(d.childCount, 2);
  assert.equal(d.descendantCount, 4);
  assert.equal(d.location.text, 'b + c * 5');
  assert.equal(d.role, 'AST operation');
});

test('selecting a parse-tree terminal reports its token', () => {
  const s = selectByKind(h.loadedState('precedence'), 'parse',
    (v) => v.raw.symbol === 'terminal' && v.raw.token.type === 'MUL');
  const d = S.selectionDetails(s);
  assert.equal(d.name, 'MUL');
  assert.equal(d.value, '*');
  assert.equal(d.first, 5);
  assert.equal(d.end, 6);
  assert.equal(d.location.text, '*');
  assert.equal(d.role, 'Terminal (token)');
});

test('selecting the empty Program reports an empty span without a source range', () => {
  const s = h.loadedState('empty-program');
  const sel = S.select(s, 'ast', s.models.ast.root.id);
  const d = S.selectionDetails(sel);
  assert.equal(d.tokenCount, 0);
  assert.equal(d.location, null);
  assert.deepEqual(S.highlightRanges(sel), []);
});

test('selection highlights the node source range and its tokens', () => {
  const s = selectByKind(h.loadedState('parentheses'), 'ast', (v) => v.raw.kind === 'Multiply');
  const ranges = S.highlightRanges(s);
  assert.equal(ranges.length, 1);
  assert.equal(s.source.slice(ranges[0].start, ranges[0].end), '(x + y) * z');
  const marks = S.tokenMarks(s);
  assert.deepEqual([...marks.selected].sort((a, b) => a - b), [2, 3, 4, 5, 6, 7, 8]);
});

test('clearing the selection removes highlights', () => {
  let s = selectByKind(h.loadedState('precedence'), 'ast', (v) => v.raw.kind === 'Add');
  s = S.clearSelection(s);
  assert.equal(s.selection, null);
  assert.deepEqual(S.highlightRanges(s), []);
  assert.equal(S.tokenMarks(s).selected.size, 0);
});

test('selecting an unknown id is ignored', () => {
  const s = h.loadedState('precedence');
  assert.equal(S.select(s, 'ast', 'nope'), s);
});

test('toggle, collapse all and expand all', () => {
  let s = h.loadedState('precedence');
  const add = s.models.ast.order.find((v) => v.raw.kind === 'Add');
  s = S.toggleNode(s, 'ast', add.id);
  assert.ok(s.collapsed.ast.has(add.id));
  s = S.toggleNode(s, 'ast', add.id);
  assert.ok(!s.collapsed.ast.has(add.id));
  s = S.collapseAll(s, 'ast');
  assert.ok(!s.collapsed.ast.has(s.models.ast.root.id));
  assert.ok(s.collapsed.ast.size > 0);
  s = S.expandAll(s, 'ast');
  assert.equal(s.collapsed.ast.size, 0);
});

test('collapse state of one tree does not affect the other', () => {
  let s = h.loadedState('precedence');
  s = S.collapseAll(s, 'ast');
  assert.equal(s.collapsed.parse.size, 0);
});

test('a token chosen in the table selects its leaf node and reveals it', () => {
  let s = h.loadedState('stress-ast-at-limit');
  assert.ok(s.collapsed.ast.size > 0, 'large tree starts collapsed');
  // Last operand: deepest leaf-ish token near the bottom of the chain.
  const tokens = s.response.tokens;
  const lastNumber = tokens.filter((t) => t.type === 'NUMBER').pop().index;
  s = S.focusToken(s, lastNumber, 'ast');
  assert.ok(s.selection, 'a node was selected');
  const v = s.models.ast.byId.get(s.selection.id);
  assert.equal(v.first, lastNumber);
  // Every ancestor is expanded, so the node is visible.
  h.treemodel.ancestorIds(s.models.ast, v.id).forEach((a) => {
    assert.ok(!s.collapsed.ast.has(a), 'ancestor expanded');
  });
  const res = h.layout.layoutTree(s.models.ast.root, h.measure, (id) => s.collapsed.ast.has(id));
  assert.ok(res.byId.has(v.id), 'selected node is laid out');
});

test('a token with no matching leaf is highlighted on its own', () => {
  let s = h.loadedState('precedence');
  s = S.focusToken(s, 1, 'ast');                // '=' has no AST node
  assert.equal(s.selection, null);
  assert.equal(s.tokenFocus, 1);
  const r = S.highlightRanges(s);
  assert.equal(s.source.slice(r[0].start, r[0].end), '=');
});

test('view mode switching', () => {
  let s = h.loadedState('precedence');
  assert.equal(s.view, 'both');
  s = S.setView(s, 'ast');
  assert.equal(s.view, 'ast');
  s = S.setView(s, 'parse');
  assert.equal(s.view, 'parse');
});

test('CRLF sources are normalised so editor offsets stay valid', () => {
  const r = h.response('multiple-statements');
  const crlf = JSON.parse(JSON.stringify(r));
  crlf.source = r.source.replace(/\n/g, '\r\n');   // same tokens: positions are identical
  const s = S.withResults(S.initialState(), crlf, { kind: 'file', name: 'x.json' });
  assert.equal(s.source, r.source);
  const sel = S.select(s, 'ast', s.models.ast.root.children[1].id);
  const range = S.highlightRanges(sel)[0];
  assert.equal(s.source.slice(range.start, range.end), 'b = a + 2;');
});

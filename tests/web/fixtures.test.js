'use strict';
// Checks the captured compiler output itself: it must satisfy the data
// contract and show the structures the project requires.

const test = require('node:test');
const assert = require('node:assert/strict');
const h = require('./helpers.js');

test('every captured response passes contract validation', () => {
  for (const f of h.fixtures) {
    const r = h.response(f.id);
    assert.ok(r, 'response present for ' + f.id);
    assert.deepEqual(h.contract.validateResponse(r), [], f.id);
    assert.equal(r.source, f.source, f.id + ': response belongs to the fixture source');
  }
});

test('fixture list contains the six required examples', () => {
  const sources = h.fixtures.filter((f) => f.group === 'Examples').map((f) => f.source);
  assert.deepEqual(sources, [
    'a = b + c * 5;', 'result = (x + y) * z;', 'r = a - b - c;',
    'a = b + * 5;', 'a = (b + c;', ''
  ]);
});

test('example 1: multiplication nests under addition', () => {
  const r = h.response('precedence');
  assert.equal(h.astShape(r.ast),
    'Program(Assignment(Identifier(a), Add(Identifier(b), Multiply(Identifier(c), Number(5)))))');
});

test('example 2: parentheses put addition under multiplication', () => {
  const r = h.response('parentheses');
  assert.equal(h.astShape(r.ast),
    'Program(Assignment(Identifier(result), Multiply(Add(Identifier(x), Identifier(y)), Identifier(z))))');
});

test('example 3: a - b - c is (a - b) - c', () => {
  const r = h.response('left-associativity');
  assert.equal(h.astShape(r.ast),
    'Program(Assignment(Identifier(r), Subtract(Subtract(Identifier(a), Identifier(b)), Identifier(c))))');
});

test('the parse tree keeps punctuation and grammar nodes', () => {
  const r = h.response('precedence');
  assert.equal(h.parseShape(r.parseTree),
    '(program (statement ID(a) ASSIGN(=) ' +
    '(expression (term (factor ID(b))) PLUS(+) (term (factor ID(c)) MUL(*) (factor NUMBER(5)))) ' +
    'SEMICOLON(;)))');
});

test('tokens carry the lexer positions', () => {
  const r = h.response('precedence');
  const flat = r.tokens.map((t) => `${t.type}(${t.lexeme}) ${t.line}:${t.column}`);
  assert.deepEqual(flat, [
    'ID(a) 1:1', 'ASSIGN(=) 1:3', 'ID(b) 1:5', 'PLUS(+) 1:7', 'ID(c) 1:9',
    'MUL(*) 1:11', 'NUMBER(5) 1:13', 'SEMICOLON(;) 1:14', 'END() 1:15'
  ]);
});

test('multiple statements give one assignment each, in order', () => {
  const r = h.response('multiple-statements');
  assert.equal(r.ast.children.length, 3);
  assert.equal(h.astShape(r.ast),
    'Program(Assignment(Identifier(a), Number(1)), ' +
    'Assignment(Identifier(b), Add(Identifier(a), Number(2))), ' +
    'Assignment(Identifier(c), Multiply(Add(Identifier(a), Identifier(b)), Identifier(c))))');
  const lines = r.tokens.map((t) => t.line);
  assert.deepEqual([...new Set(lines)], [1, 2, 3]);
});

test('empty program is valid with a childless Program and only END', () => {
  const r = h.response('empty-program');
  assert.equal(r.ok, true);
  assert.equal(r.ast.kind, 'Program');
  assert.equal(r.ast.children.length, 0);
  assert.equal(r.parseTree.children.length, 0);
  assert.deepEqual(r.tokens.map((t) => t.type), ['END']);
});

test('syntax errors carry a diagnostic and no trees', () => {
  const star = h.response('error-unexpected-operator');
  assert.equal(star.ok, false);
  assert.equal(star.tokenized, true);
  assert.equal(star.parseTree, null);
  assert.equal(star.ast, null);
  assert.equal(star.diagnostics.length, 1);
  const d = star.diagnostics[0];
  assert.equal(d.kind, 'syntax');
  assert.equal(d.line, 1);
  assert.equal(d.column, 9);
  assert.equal(d.found, '*');
  assert.deepEqual(d.expected, ['ID', 'NUMBER', 'LPAREN']);

  const paren = h.response('error-missing-paren').diagnostics[0];
  assert.equal(paren.column, 11);
  assert.deepEqual(paren.expected, ['RPAREN']);
});

test('a lexical error is not tokenized and has no END token', () => {
  const r = h.response('error-lexical');
  assert.equal(r.ok, false);
  assert.equal(r.tokenized, false);
  assert.equal(r.parseTree, null);
  assert.equal(r.ast, null);
  assert.equal(r.diagnostics[0].kind, 'lexical');
  assert.equal(r.diagnostics[0].column, 7);
  assert.equal(r.diagnostics[0].found, '$');
  assert.ok(r.tokens.every((t) => t.type !== 'END'));
});

test('implementation-limit diagnostics are flagged as limits', () => {
  for (const id of ['limit-nesting-over', 'limit-ast-over']) {
    const r = h.response(id);
    assert.equal(r.ok, false, id);
    assert.equal(r.parseTree, null);
    assert.equal(r.ast, null);
    assert.equal(r.diagnostics[0].kind, 'limit', id);
    assert.equal(r.diagnostics[0].phase, 'syntax', id);
  }
});

test('only the implementation-limit fixtures are classified as limits', () => {
  for (const f of h.fixtures) {
    const r = h.response(f.id);
    r.diagnostics.forEach((d) => {
      const isLimitFixture = f.id.startsWith('limit-');
      assert.equal(d.kind === 'limit', isLimitFixture, `${f.id}: kind ${d.kind}`);
      assert.equal(d.kind === 'lexical', d.phase === 'lexical', `${f.id}: phase/kind`);
      if (d.kind === 'limit') assert.deepEqual(d.expected, [], f.id);
    });
  }
  // ...and every failing non-limit fixture really is a lexical or syntax error.
  const kinds = h.fixtures.filter((f) => !f.id.startsWith('limit-') && !h.response(f.id).ok)
    .map((f) => h.response(f.id).diagnostics[0].kind);
  assert.ok(kinds.length >= 4);
  assert.ok(kinds.every((k) => k === 'lexical' || k === 'syntax'));
});

test('stress fixtures at the limits are accepted and large', () => {
  const chain = h.response('stress-ast-at-limit');
  assert.equal(chain.ok, true);
  const model = h.treemodel.buildViewTree(chain.ast, 'ast');
  assert.equal(model.count, 998 + 997 + 3);
  const nest = h.response('stress-nesting-at-limit');
  assert.equal(nest.ok, true);
  assert.equal(h.astShape(nest.ast), 'Program(Assignment(Identifier(a), Identifier(b)))');
});
